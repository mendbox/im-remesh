// SPDX-License-Identifier: GPL-3.0-or-later
//
// In-memory remeshing call on top of the Instant Meshes core.
//
// The sequence of steps in remesh() follows batch_process() in src/batch.cpp of Instant Meshes
// (Copyright (c) 2015 Wenzel Jakob, Daniele Panozzo, Marco Tarini and Olga Sorkine-Hornung, BSD 3-clause,
// full text in licenses/instant-meshes-LICENSE.txt, which travels with this file and with every binary):
// same order of calls, the same formulas for the target edge length and the same boundary-constraint loop.
// File reading and writing, point clouds and console reporting were dropped, input checks were added.

#include "im_core.h"

#include "dedge.h"
#include "subdivide.h"
#include "meshstats.h"
#include "hierarchy.h"
#include "field.h"
#include "normal.h"
#include "extract.h"
#include "bvh.h"

#include <im_portable_math.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <streambuf>

// Thread count read by the optimizer thread of the core (defined in main.cpp upstream, which is not compiled here).
int nprocs = 1;

// Defined in src/field.cpp, not declared in field.h.
void freeze_ivars_orientations(MultiResolutionHierarchy &mRes, int level, bool extrinsic, int rosy);
void freeze_ivars_positions(MultiResolutionHierarchy &mRes, int level, bool extrinsic, int posy);

namespace {

struct NullBuffer : std::streambuf {
    int overflow(int c) override { return c; }
    std::streamsize xsputn(const char *, std::streamsize n) override { return n; }
};

// The core reports progress on std::cout. Unless verbose, send that text nowhere for the duration of a call.
struct CoutSilencer {
    NullBuffer null;
    std::streambuf *saved = nullptr;
    explicit CoutSilencer(bool active) { if (active) saved = std::cout.rdbuf(&null); }
    ~CoutSilencer() { if (saved) std::cout.rdbuf(saved); }
};

// MultiResolutionHierarchy has no destructor: release its adjacency arrays when the call ends, error or not.
struct HierarchyGuard {
    MultiResolutionHierarchy &h;
    explicit HierarchyGuard(MultiResolutionHierarchy &h) : h(h) {}
    ~HierarchyGuard() { h.free(); }
};

// The hierarchical solve, coarse to fine: what Optimizer::run() in src/field.cpp does in batch mode (six sweeps per
// level, then the field is handed to the next finer level; after the finest level the integer variables are frozen
// and, for orientations, the solution is pushed back up the hierarchy). Upstream runs this in a worker thread that
// is woken through a condition variable; the flags it waits on are set without the lock, so a wake-up can be lost
// and both threads then wait forever (seen here as soon as the TBB start-up delay was gone). Calling the kernels
// directly needs no thread and keeps exceptions on the calling thread.
void optimize_field(MultiResolutionHierarchy &mRes, bool orientations, int rosy, int posy, bool extrinsic) {
    const int level_iterations = 6;
    const std::function<void(uint32_t)> progress = [](uint32_t) {};
    if (orientations)
        mRes.setFrozenQ(false);
    else
        mRes.setFrozenO(false);

    for (int level = mRes.levels() - 1; level >= 0; --level) {
        for (int it = 0; it < level_iterations; ++it) {
            if (orientations)
                optimize_orientations(mRes, level, extrinsic, rosy, progress);
            else
                optimize_positions(mRes, level, extrinsic, posy, progress);
        }
        if (level > 0) {
            const MatrixXf &src = orientations ? mRes.Q(level) : mRes.O(level);
            MatrixXf &dest = orientations ? mRes.Q(level - 1) : mRes.O(level - 1);
            const MatrixXf &N = mRes.N(level - 1);
            const MatrixXf &V = mRes.V(level - 1);
            const MatrixXu &toUpper = mRes.toUpper(level - 1);
            for (uint32_t j = 0; j < (uint32_t) src.cols(); ++j) {
                for (int k = 0; k < 2; ++k) {
                    uint32_t d = toUpper(k, j);
                    if (d == INVALID)
                        continue;
                    if (orientations) {
                        Vector3f q = src.col(j), n = N.col(d);
                        dest.col(d) = q - n * n.dot(q);
                    } else {
                        Vector3f o = src.col(j), n = N.col(d), v = V.col(d);
                        o -= n * n.dot(o - v);
                        dest.col(d) = o;
                    }
                }
            }
        }
        if (level == 0 || (!orientations && level == 1)) {      // the counters upstream raises (read by its viewer only)
            if (orientations)
                mRes.setIterationsQ(mRes.iterationsQ() + 1);
            else
                mRes.setIterationsO(mRes.iterationsO() + 1);
        }
    }
    if (orientations) {
        if (!mRes.frozenQ())
            freeze_ivars_orientations(mRes, 0, extrinsic, rosy);
        mRes.propagateSolution(rosy);
    } else {
        if (!mRes.frozenO())
            freeze_ivars_positions(mRes, 0, extrinsic, posy);
    }
}

std::mutex g_call_mutex;

} // namespace

namespace imcore {

void remesh(const float *vertices, std::size_t vertex_count,
            const uint32_t *faces, std::size_t face_count,
            const Params &params, Result &result) {
    int rosy = params.rosy, posy = params.posy;
    if (posy == 6)
        posy = 3;
    if (rosy != 2 && rosy != 4 && rosy != 6)
        throw std::invalid_argument("rosy must be 2, 4 or 6");
    if (posy != 3 && posy != 4)
        throw std::invalid_argument("posy must be 4 (quads) or 3 (triangles)");
    if (!vertices || !faces || vertex_count < 3 || face_count < 1)
        throw std::invalid_argument("the input mesh is empty");
    if (vertex_count > 0x7fffffffu || face_count > 0x3fffffffu)
        throw std::invalid_argument("the input mesh is too large");
    if (params.smooth_iter < 0)
        throw std::invalid_argument("smooth_iter must be 0 or more");
    for (std::size_t i = 0; i < vertex_count * 3; ++i)
        if (!std::isfinite(vertices[i]))
            throw std::invalid_argument("a vertex coordinate is not finite");
    for (std::size_t i = 0; i < face_count * 3; ++i)
        if (faces[i] >= vertex_count)
            throw std::invalid_argument("a face refers to a vertex that does not exist");
    for (std::size_t i = 0; i < face_count; ++i)
        if (faces[3 * i] == faces[3 * i + 1] || faces[3 * i + 1] == faces[3 * i + 2] || faces[3 * i] == faces[3 * i + 2])
            throw std::invalid_argument("a face uses the same vertex twice");

    std::lock_guard<std::mutex> guard(g_call_mutex);
    CoutSilencer silencer(!params.verbose);

    const bool deterministic = params.deterministic;
    Float scale = params.scale;
    int out_faces = params.target_face_count, out_vertices = params.target_vertex_count;

    MatrixXu F = Eigen::Map<const MatrixXu>(faces, 3, (std::ptrdiff_t) face_count);
    MatrixXf V = Eigen::Map<const MatrixXf>(vertices, 3, (std::ptrdiff_t) vertex_count);
    MatrixXf N;
    VectorXf A;
    std::set<uint32_t> crease_in, crease_out;

    MeshStats stats = compute_mesh_stats(F, V, deterministic);
    if (!(stats.mSurfaceArea > 0) || !std::isfinite(stats.mSurfaceArea))
        throw std::invalid_argument("the input mesh has no surface area");

    if (scale <= 0 && out_vertices <= 0 && out_faces <= 0)
        out_vertices = std::max<int>(1, (int) (vertex_count / 16));

    // Upstream also derives the two counts it does not use afterwards; only the edge length matters here.
    if (scale > 0) {
        // given directly
    } else if (out_faces > 0) {
        Float face_area = stats.mSurfaceArea / out_faces;
        scale = posy == 4 ? std::sqrt(face_area) : (2 * std::sqrt(face_area * std::sqrt(1.f / 3.f)));
    } else {
        out_faces = posy == 4 ? out_vertices : (out_vertices * 2);
        Float face_area = stats.mSurfaceArea / out_faces;
        scale = posy == 4 ? std::sqrt(face_area) : (2 * std::sqrt(face_area * std::sqrt(1.f / 3.f)));
    }
    if (!(scale > 0) || !std::isfinite(scale))
        throw std::invalid_argument("the target size gives no usable edge length");

    MultiResolutionHierarchy mRes;
    HierarchyGuard hierarchy_guard(mRes);
    {
        VectorXu V2E, E2E;
        VectorXb boundary, nonManifold;
        if (stats.mMaximumEdgeLength * 2 > scale || stats.mMaximumEdgeLength > stats.mAverageEdgeLength * 2) {
            // The core now splits every edge longer than split_length, with nothing to stop it. Two inputs make that
            // run away (both reproduced during review), so they are refused here. Accepted inputs are untouched.
            const double split_length = std::min((double) scale / 2, stats.mAverageEdgeLength * 2);
            double max_abs = 0;
            for (std::size_t i = 0; i < vertex_count * 3; ++i)
                max_abs = std::max(max_abs, (double) std::fabs(vertices[i]));
            // 1. Far from the origin a float cannot tell the midpoint of a short edge from its ends: the edge never gets
            //    shorter and the loop never ends.
            if (!(split_length >= 16.0 * FLT_EPSILON * max_abs))
                throw std::invalid_argument(
                    "the mesh is too far from the origin (or too large) for this target size: single precision cannot "
                    "resolve the needed edge length. Move the mesh to the origin or scale it down, then try again");
            // 2. Memory follows the target, not the input: estimate the triangle count after the split.
            const double work_triangles = stats.mSurfaceArea / (0.433 * split_length * split_length);
            if (params.max_work_triangles > 0 && work_triangles > (double) params.max_work_triangles) {
                std::ostringstream msg;
                msg << "the target is too fine for this build: the input would first be split into about "
                    << (long long) (work_triangles / 1e6 + 0.5) << " million triangles (limit "
                    << (long long) (params.max_work_triangles / 1000000) << " million). Ask for fewer faces or a larger edge length";
                throw std::invalid_argument(msg.str());
            }
            build_dedge(F, V, V2E, E2E, boundary, nonManifold);
            subdivide(F, V, V2E, E2E, boundary, nonManifold,
                      std::min(scale / 2, (Float) stats.mAverageEdgeLength * 2), deterministic);
        }
        build_dedge(F, V, V2E, E2E, boundary, nonManifold);
        // Handed to the hierarchy at once, so that hierarchy_guard frees it if a later step throws.
        mRes.setAdj(generate_adjacency_matrix_uniform(F, V2E, E2E, nonManifold));

        if (params.crease_angle >= 0)
            generate_crease_normals(F, V, V2E, E2E, boundary, nonManifold, params.crease_angle, N, crease_in);
        else
            generate_smooth_normals(F, V, V2E, E2E, nonManifold, N);

        compute_dual_vertex_areas(F, V, V2E, E2E, nonManifold, A);

        mRes.setE2E(std::move(E2E));
    }
    mRes.setF(std::move(F));
    mRes.setV(std::move(V));
    mRes.setA(std::move(A));
    mRes.setN(std::move(N));
    mRes.setScale(scale);
    mRes.build(deterministic);
    mRes.resetSolution();

    if (params.align_to_boundaries) {
        mRes.clearConstraints();
        for (uint32_t i = 0; i < 3 * mRes.F().cols(); ++i) {
            if (mRes.E2E()[i] == INVALID) {
                uint32_t i0 = mRes.F()(i % 3, i / 3);
                uint32_t i1 = mRes.F()((i + 1) % 3, i / 3);
                Vector3f p0 = mRes.V().col(i0), p1 = mRes.V().col(i1);
                Vector3f edge = p1 - p0;
                if (edge.squaredNorm() > 0) {
                    edge.normalize();
                    mRes.CO().col(i0) = p0;
                    mRes.CO().col(i1) = p1;
                    mRes.CQ().col(i0) = mRes.CQ().col(i1) = edge;
                    mRes.CQw()[i0] = mRes.CQw()[i1] = mRes.COw()[i0] = mRes.COw()[i1] = 1.0f;
                }
            }
        }
        mRes.propagateConstraints(rosy, posy);
    }

    std::unique_ptr<BVH> bvh;
    if (params.smooth_iter > 0) {
        bvh.reset(new BVH(&mRes.F(), &mRes.V(), &mRes.N(), stats.mAABB));
        bvh->build();
    }

    optimize_field(mRes, true, rosy, posy, params.extrinsic);
    optimize_field(mRes, false, rosy, posy, params.extrinsic);

    MatrixXf O_extr, N_extr, Nf_extr;
    std::vector<std::vector<TaggedLink>> adj_extr;
    extract_graph(mRes, params.extrinsic, rosy, posy, adj_extr, O_extr, N_extr,
                  crease_in, crease_out, deterministic);

    MatrixXu F_extr;
    extract_faces(adj_extr, O_extr, N_extr, Nf_extr, F_extr, posy, mRes.scale(), crease_out,
                  true, true, bvh.get(), params.smooth_iter);

    if (F_extr.cols() > 0 && F_extr.rows() != posy)
        throw std::runtime_error("unexpected face layout in the extracted mesh");

    result.face_width = posy;
    result.vertices.assign(O_extr.data(), O_extr.data() + O_extr.size());
    result.faces.assign(F_extr.data(), F_extr.data() + F_extr.size());
}

namespace {

uint64_t fnv_bytes(const void *data, std::size_t n, uint64_t h) {
    const unsigned char *p = (const unsigned char *) data;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
template <typename T> uint64_t fnv_value(T value, uint64_t h) { return fnv_bytes(&value, sizeof(T), h); }

} // namespace

std::string numeric_selftest() {
    const uint64_t seed = 1469598103934665603ull;

    // a * a is 1 + 2^-26 + 2^-54 exactly and rounds to 1 + 2^-26, so a * a - c is 0 when the product is rounded first
    // and 2^-54 when the compiler fuses the multiply and the subtraction. volatile keeps it out of constant folding.
    volatile double a = 1.0 + std::ldexp(1.0, -27), c = 1.0 + std::ldexp(1.0, -26);
    double fused = a * a - c;
    volatile float fa = 1.0f + std::ldexp(1.0f, -12), fc = 1.0f + std::ldexp(1.0f, -11);
    float fused_f = fa * fa - fc;
    const int fma = (fused != 0.0 || fused_f != 0.0f) ? 1 : 0;

    uint64_t math = seed;
    uint32_t state = 12345u;
    for (int i = 0; i < 20000; ++i) {
        double u[5];
        for (int k = 0; k < 5; ++k) {
            state = state * 1664525u + 1013904223u;
            u[k] = (double) state / 4294967296.0;
        }
        const double angle = (u[0] - 0.5) * 40, e = -u[1] * 60, y = (u[2] - 0.5) * 4, x = (u[3] - 0.5) * 4, d = u[4] * 2 - 1;
        math = fnv_value(imdet::sin(angle), math);
        math = fnv_value(imdet::cos(angle), math);
        math = fnv_value(imdet::exp(e), math);
        math = fnv_value(imdet::atan2(y, x), math);
        math = fnv_value(imdet::acos(d), math);
        math = fnv_value(imdet::sin((float) angle), math);
        math = fnv_value(imdet::cos((float) angle), math);
        math = fnv_value(imdet::exp((float) e), math);
        math = fnv_value(imdet::atan2((float) y, (float) x), math);
        math = fnv_value(imdet::acos((float) d), math);
        math = fnv_value(fast_acos((float) d), math);
        math = fnv_value((float) std::sqrt((float) u[0]) * (float) u[1] + (float) u[2] / (float) (u[3] + 0.5), math);
    }

    // Many equal keys: the order they come out in is what differs between standard libraries with std:: containers.
    typedef std::pair<uint32_t, float> Keyed;
    struct ByKey { bool operator()(const Keyed &p, const Keyed &q) const { return p.second < q.second; } };
    uint64_t heap = seed, sort = seed;
    tbb::concurrent_priority_queue<Keyed, ByKey> queue;
    std::vector<Keyed> items;
    for (uint32_t i = 0; i < 5000; ++i) {
        state = state * 1664525u + 1013904223u;
        Keyed item(i, (float) ((state >> 16) % 37));
        queue.push(item);
        items.push_back(item);
        if (i % 7 == 6) {
            Keyed top;
            queue.try_pop(top);
            heap = fnv_value(top.first, heap);
        }
    }
    Keyed top;
    while (queue.try_pop(top))
        heap = fnv_value(top.first, heap);
    tbb::parallel_sort(items.begin(), items.end(), ByKey());
    for (std::size_t i = 0; i < items.size(); ++i)
        sort = fnv_value(items[i].first, sort);

    char text[256];
    std::snprintf(text, sizeof(text), "fma=%d eval=%d simd=%s math=%016llx heap=%016llx sort=%016llx", fma, (int) FLT_EVAL_METHOD,
                  Eigen::SimdInstructionSetsInUse(), (unsigned long long) math, (unsigned long long) heap, (unsigned long long) sort);
    return text;
}

} // namespace imcore

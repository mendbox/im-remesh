// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-alone check of the core without Python.
//
//   im_core_check [reference file] [--out file]
//
// Remeshes a few meshes made here with exact arithmetic (so the input bits are the same everywhere) at several
// settings and prints one line per case: counts and an FNV-1a hash of the result arrays. This text is the
// "core fingerprint". It must be the same on every platform; tests/core_fingerprint_ref.txt is the committed copy
// (written on macOS arm64).
// Exit code: 0 = fine, 1 = a quad result is not quads only, 2 = two runs of the same case differ,
//            3 = the fingerprint differs from the reference file, 4 = file trouble.
#include "im_core.h"
#include "im_portable_math.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace {

const double PI = 3.14159265358979323846;

uint64_t fnv(const void *data, size_t n, uint64_t h) {
    const unsigned char *p = (const unsigned char *) data;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

struct Mesh {
    std::vector<float> V;
    std::vector<uint32_t> F;
    void vertex(double x, double y, double z) { V.push_back((float) x); V.push_back((float) y); V.push_back((float) z); }
    void triangle(uint32_t a, uint32_t b, uint32_t c) { F.push_back(a); F.push_back(b); F.push_back(c); }
    uint64_t hash() const {
        return fnv(F.data(), F.size() * sizeof(uint32_t), fnv(V.data(), V.size() * sizeof(float), 1469598103934665603ull));
    }
};

// Regular grid torus: many equal edge lengths, the case where the order of equal keys matters most.
Mesh torus(int nu, int nv) {
    Mesh m;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            double a = 2 * PI * i / nu, b = 2 * PI * j / nv, r = 1.0 + 0.35 * imdet::cos(b);
            m.vertex(r * imdet::cos(a), r * imdet::sin(a), 0.35 * imdet::sin(b));
        }
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            uint32_t a = i * nv + j, b = ((i + 1) % nu) * nv + j, c = ((i + 1) % nu) * nv + (j + 1) % nv, d = i * nv + (j + 1) % nv;
            m.triangle(a, b, c);
            m.triangle(a, c, d);
        }
    return m;
}

// Icosahedron, each edge split in two `levels` times, vertices pushed to the unit sphere.
Mesh icosphere(int levels) {
    Mesh m;
    const double t = (1.0 + std::sqrt(5.0)) / 2.0, s = std::sqrt(1.0 + t * t);
    const double base[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    const uint32_t faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                   {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                   {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    std::vector<double> P;
    for (int i = 0; i < 12; ++i)
        for (int k = 0; k < 3; ++k)
            P.push_back(base[i][k] / s);
    std::vector<uint32_t> F;
    for (int i = 0; i < 20; ++i)
        for (int k = 0; k < 3; ++k)
            F.push_back(faces[i][k]);
    for (int level = 0; level < levels; ++level) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        std::vector<uint32_t> G;
        for (size_t f = 0; f < F.size(); f += 3) {
            uint32_t c[3], e[3];
            for (int k = 0; k < 3; ++k)
                c[k] = F[f + k];
            for (int k = 0; k < 3; ++k) {
                uint32_t a = c[k], b = c[(k + 1) % 3];
                std::pair<uint32_t, uint32_t> key(a < b ? a : b, a < b ? b : a);
                std::map<std::pair<uint32_t, uint32_t>, uint32_t>::iterator it = mid.find(key);
                if (it == mid.end()) {
                    double q[3], len = 0;
                    for (int d = 0; d < 3; ++d) {
                        q[d] = (P[3 * a + d] + P[3 * b + d]) * 0.5;
                        len += q[d] * q[d];
                    }
                    len = std::sqrt(len);
                    for (int d = 0; d < 3; ++d)
                        P.push_back(q[d] / len);
                    it = mid.insert(std::make_pair(key, (uint32_t) (P.size() / 3 - 1))).first;
                }
                e[k] = it->second;
            }
            const uint32_t tri[12] = {c[0], e[0], e[2], c[1], e[1], e[0], c[2], e[2], e[1], e[0], e[1], e[2]};
            G.insert(G.end(), tri, tri + 12);
        }
        F.swap(G);
    }
    for (size_t i = 0; i < P.size(); i += 3)
        m.vertex(P[i], P[i + 1], P[i + 2]);
    m.F = F;
    return m;
}

// Box 1 x 1 x 2 with n cells per unit, twisted by a quarter turn over its height: sharp edges, flat faces.
Mesh twisted_box(int n) {
    Mesh m;
    const int dims[3] = {n, n, 2 * n};
    std::map<long long, uint32_t> index;
    std::vector<int> grid;
    struct Local {
        static uint32_t vertex(std::map<long long, uint32_t> &index, std::vector<int> &grid, int x, int y, int z) {
            long long key = ((long long) x << 40) | ((long long) y << 20) | z;
            std::map<long long, uint32_t>::iterator it = index.find(key);
            if (it != index.end())
                return it->second;
            grid.push_back(x); grid.push_back(y); grid.push_back(z);
            uint32_t id = (uint32_t) (grid.size() / 3 - 1);
            index[key] = id;
            return id;
        }
    };
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < dims[u]; ++i)
                for (int j = 0; j < dims[v]; ++j) {
                    int c[4][3];
                    const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
                    for (int k = 0; k < 4; ++k) {
                        c[k][axis] = side ? dims[axis] : 0;
                        c[k][u] = i + du[k];
                        c[k][v] = j + dv[k];
                    }
                    uint32_t id[4];
                    for (int k = 0; k < 4; ++k)
                        id[k] = Local::vertex(index, grid, c[k][0], c[k][1], c[k][2]);
                    if (side) { m.triangle(id[0], id[1], id[2]); m.triangle(id[0], id[2], id[3]); }
                    else      { m.triangle(id[0], id[2], id[1]); m.triangle(id[0], id[3], id[2]); }
                }
    }
    for (size_t i = 0; i < grid.size(); i += 3) {
        double x = (double) grid[i] / n - 0.5, y = (double) grid[i + 1] / n - 0.5, z = (double) grid[i + 2] / n - 1.0;
        double angle = 0.25 * PI * (z + 1.0), ca = imdet::cos(angle), sa = imdet::sin(angle);
        m.vertex(x * ca - y * sa, x * sa + y * ca, z);
    }
    return m;
}

// Open sheet with a boundary (for align_to_boundaries), bent by a polynomial.
Mesh sheet(int n) {
    Mesh m;
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j) {
            double x = 2.0 * i / n - 1.0, y = 2.0 * j / n - 1.0;
            m.vertex(x, y * (1.0 + 0.2 * x), 0.3 * x * x - 0.2 * y * y * y + 0.1 * x * y);
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            uint32_t a = i * (n + 1) + j, b = (i + 1) * (n + 1) + j, c = (i + 1) * (n + 1) + j + 1, d = i * (n + 1) + j + 1;
            m.triangle(a, b, c);
            m.triangle(a, c, d);
        }
    return m;
}

struct Case {
    const char *mesh;
    const char *label;
    imcore::Params params;
};

imcore::Params make(int faces, int posy = 4, float crease = -1.f, float scale = -1.f, bool boundaries = false,
                    bool extrinsic = true, int smooth = 2, bool deterministic = true, int rosy = 4) {
    imcore::Params p;
    p.target_face_count = faces;
    p.posy = posy;
    p.rosy = rosy;
    p.crease_angle = crease;
    p.scale = scale;
    p.align_to_boundaries = boundaries;
    p.extrinsic = extrinsic;
    p.smooth_iter = smooth;
    p.deterministic = deterministic;
    return p;
}

} // namespace

int main(int argc, char **argv) {
    std::string reference, out;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--out" && i + 1 < argc)
            out = argv[++i];
        else
            reference = argv[i];
    }

    std::map<std::string, Mesh> meshes;
    meshes["torus"] = torus(160, 80);
    meshes["icosphere"] = icosphere(5);
    meshes["twisted_box"] = twisted_box(24);
    meshes["sheet"] = sheet(60);

    const Case cases[] = {
        {"torus", "faces=500", make(500)},
        {"torus", "faces=2500", make(2500)},
        {"torus", "faces=500 posy=3 rosy=6", make(500, 3, -1.f, -1.f, false, true, 2, true, 6)},
        {"torus", "scale=0.1 crease=0", make(-1, 4, 0.f, 0.1f)},
        {"torus", "faces=400 intrinsic smooth=0 nondet", make(400, 4, -1.f, -1.f, false, false, 0, false)},
        {"icosphere", "faces=300", make(300)},
        {"icosphere", "faces=1500 crease=0", make(1500, 4, 0.f)},
        {"icosphere", "faces=4000", make(4000)},
        {"twisted_box", "faces=400 crease=30", make(400, 4, 30.f)},
        {"twisted_box", "faces=1500", make(1500)},
        {"twisted_box", "faces=1500 crease=0", make(1500, 4, 0.f)},
        {"sheet", "faces=300 boundaries", make(300, 4, -1.f, -1.f, true)},
        {"sheet", "faces=1200 boundaries crease=20", make(1200, 4, 20.f, -1.f, true)},
        {"sheet", "faces=600 posy=3", make(600, 3)},
    };

    std::ostringstream text;
    text << "numeric " << imcore::numeric_selftest() << "\n";
    for (std::map<std::string, Mesh>::const_iterator it = meshes.begin(); it != meshes.end(); ++it) {
        char line[200];
        std::snprintf(line, sizeof(line), "input %s: %zu vertices, %zu triangles, hash %016llx\n", it->first.c_str(),
                      it->second.V.size() / 3, it->second.F.size() / 3, (unsigned long long) it->second.hash());
        text << line;
    }
    int rc = 0;
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
        const Mesh &m = meshes[cases[k].mesh];
        uint64_t hashes[2] = {0, 0};
        size_t nv = 0, nf = 0, bad = 0;
        int width = 0;
        for (int run = 0; run < 2; ++run) {
            imcore::Result r;
            imcore::remesh(m.V.data(), m.V.size() / 3, m.F.data(), m.F.size() / 3, cases[k].params, r);
            uint64_t h = fnv(r.vertices.data(), r.vertices.size() * sizeof(float), 1469598103934665603ull);
            hashes[run] = fnv(r.faces.data(), r.faces.size() * sizeof(uint32_t), h);
            width = r.face_width;
            nv = r.vertices.size() / 3;
            nf = r.faces.size() / width;
            bad = 0;
            if (width == 4)
                for (size_t i = 0; i < nf; ++i)
                    for (int a = 0; a < 4; ++a)
                        for (int b = a + 1; b < 4; ++b)
                            if (r.faces[4 * i + a] == r.faces[4 * i + b]) { ++bad; a = 4; break; }
        }
        char line[300];
        std::snprintf(line, sizeof(line), "%s %s: %zu vertices, %zu faces (width %d), %zu with a repeated corner, hash %016llx\n",
                      cases[k].mesh, cases[k].label, nv, nf, width, bad, (unsigned long long) hashes[0]);
        text << line;
        if (hashes[0] != hashes[1]) {
            std::fprintf(stderr, "two runs differ: %s %s\n", cases[k].mesh, cases[k].label);
            rc = 2;
        }
        // The plain smoothed quad cases must be quads only; with smooth=0 the core does not clean degenerate quads.
        if (rc == 0 && (nf == 0 || (width == 4 && bad && cases[k].params.smooth_iter > 0))) {
            std::fprintf(stderr, "empty result or a face with a repeated corner: %s %s\n", cases[k].mesh, cases[k].label);
            rc = 1;
        }
    }
    std::fputs(text.str().c_str(), stdout);
    if (!out.empty()) {
        std::ofstream f(out.c_str(), std::ios::binary);
        f << text.str();
        if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 4; }
    }
    if (rc)
        return rc;
    if (!reference.empty()) {
        std::ifstream f(reference.c_str(), std::ios::binary);
        if (!f) { std::fprintf(stderr, "cannot read %s\n", reference.c_str()); return 4; }
        std::stringstream want;
        want << f.rdbuf();
        std::string a = want.str(), b = text.str();
        // Line by line, ignoring carriage returns (a Windows checkout may add them).
        std::istringstream sa(a), sb(b);
        std::string la, lb;
        int differ = 0;
        while (true) {
            bool ha = (bool) std::getline(sa, la), hb = (bool) std::getline(sb, lb);
            if (!ha && !hb)
                break;
            if (ha && !la.empty() && la[la.size() - 1] == '\r')
                la.erase(la.size() - 1);
            if (!ha) la.clear();
            if (!hb) lb.clear();
            if (la != lb) {
                std::fprintf(stderr, "FINGERPRINT DIFFERS\n  reference: %s\n  this build: %s\n", la.c_str(), lb.c_str());
                ++differ;
            }
        }
        if (differ) {
            std::fprintf(stderr, "core fingerprint: %d line(s) differ from %s\n", differ, reference.c_str());
            return 3;
        }
        std::printf("core fingerprint equals %s\n", reference.c_str());
    }
    return 0;
}

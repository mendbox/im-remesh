// SPDX-License-Identifier: GPL-3.0-or-later
// Plain C++ entry point to the Instant Meshes remeshing core (no Eigen, no Python in this header).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace imcore {

struct Params {
    int target_vertex_count = -1;
    int target_face_count = -1;
    float scale = -1.f;              // target edge length in input units; wins over the two counts
    int rosy = 4;                    // orientation symmetry: 2, 4 or 6
    int posy = 4;                    // position symmetry: 4 (quads) or 3 (triangles; 6 is accepted as 3)
    float crease_angle = -1.f;       // degrees; negative = smooth normals, no creases
    bool extrinsic = true;
    bool align_to_boundaries = false;
    int smooth_iter = 2;
    bool deterministic = true;
    bool verbose = false;            // print the upstream progress text to stdout
    // Refuse a call whose input would first be subdivided into more triangles than this (0 = no limit).
    // 8 million is about 3.5 GB of working memory and keeps the 32-bit edge ids far from overflow.
    long long max_work_triangles = 8000000;
};

struct Result {
    std::vector<float> vertices;     // k * 3
    std::vector<uint32_t> faces;     // q * face_width
    int face_width = 4;              // 4 for posy == 4, 3 for posy == 3
};

// Throws std::invalid_argument for bad input and std::runtime_error when the core fails.
// One call at a time: concurrent calls are serialised by an internal mutex.
void remesh(const float *vertices, std::size_t vertex_count,
            const uint32_t *faces, std::size_t face_count,
            const Params &params, Result &result);

// One line about the numeric setup of this binary, the same text on every platform when the build is right:
// "fma=0 eval=0 simd=None math=<hash> heap=<hash> sort=<hash>". fma=1 means the compiler fused a*b+c, eval is
// FLT_EVAL_METHOD, simd names Eigen's vector instruction set, math hashes the portable math functions over fixed
// inputs, heap and sort hash the order in which equal keys leave the shim's queue and sort.
std::string numeric_selftest();

} // namespace imcore

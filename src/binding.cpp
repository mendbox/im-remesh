// SPDX-License-Identifier: GPL-3.0-or-later
// Python module im_remesh._core: one function, remesh(), on top of imcore::remesh().

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <nanobind/stl/string.h>

#include <string>
#include <utility>
#include <vector>

#include "im_core.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

typedef nb::ndarray<const float, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> VertexArray;
typedef nb::ndarray<const uint32_t, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> FaceArray;

template <typename T> nb::object to_numpy(std::vector<T> &&values, size_t columns) {
    std::vector<T> *holder = new std::vector<T>(std::move(values));
    nb::capsule owner(holder, [](void *p) noexcept { delete static_cast<std::vector<T> *>(p); });
    size_t shape[2] = { holder->size() / columns, columns };
    return nb::ndarray<nb::numpy, T>(holder->data(), 2, shape, owner).cast();
}

nb::tuple remesh(VertexArray vertices, FaceArray faces, int target_vertex_count, int target_face_count,
                 int rosy, int posy, float crease_angle, bool extrinsic, bool align_to_boundaries,
                 int smooth_iter, bool deterministic, int threads, float scale, bool verbose,
                 long long max_work_triangles) {
    if (threads < 1)
        throw nb::value_error("threads must be 1 or more");

    imcore::Params params;
    params.target_vertex_count = target_vertex_count;
    params.target_face_count = target_face_count;
    params.scale = scale;
    params.rosy = rosy;
    params.posy = posy;
    params.crease_angle = crease_angle;
    params.extrinsic = extrinsic;
    params.align_to_boundaries = align_to_boundaries;
    params.smooth_iter = smooth_iter;
    params.deterministic = deterministic;
    params.verbose = verbose;
    params.max_work_triangles = max_work_triangles;

    imcore::Result result;
    {
        nb::gil_scoped_release release;
        imcore::remesh(vertices.data(), vertices.shape(0), faces.data(), faces.shape(0), params, result);
    }
    size_t width = (size_t) result.face_width;
    nb::object out_vertices = to_numpy(std::move(result.vertices), 3);
    nb::object out_faces = to_numpy(std::move(result.faces), width);
    return nb::make_tuple(out_vertices, out_faces);
}

} // namespace

NB_MODULE(_core, m) {
    m.doc() = "Instant Meshes remeshing core (serial build, no TBB)";
    m.attr("THREADING") = "serial";
    m.attr("INSTANT_MESHES_COMMIT") = IM_UPSTREAM_COMMIT;
    m.def("numeric_selftest", []() { return imcore::numeric_selftest(); },
          "One line about the numeric setup of this binary; the same text on every platform when the build is right.");
    m.def("remesh", &remesh,
          // No silent dtype conversion here: nanobind would cast with wrap-around (an int64 index of 2^32 + 5 became 5).
          // im_remesh/__init__.py converts other dtypes after a range check.
          "vertices"_a.noconvert(), "faces"_a.noconvert(), "target_vertex_count"_a = -1, "target_face_count"_a = -1,
          "rosy"_a = 4, "posy"_a = 4, "crease_angle"_a = -1.f, "extrinsic"_a = true,
          "align_to_boundaries"_a = false, "smooth_iter"_a = 2, "deterministic"_a = true,
          "threads"_a = 1, "scale"_a = -1.f, "verbose"_a = false,
          "max_work_triangles"_a = 8000000,
          "Remesh a triangle mesh with Instant Meshes (raw entry point; im_remesh.remesh() is the one to call).\n\n"
          "vertices: float32 [n, 3]; faces: uint32 [m, 3]; exactly these dtypes, C-contiguous.\n"
          "Size: scale (edge length, input units) wins over target_face_count, which wins over\n"
          "target_vertex_count; with none given the target is n / 16 vertices. The counts are those of\n"
          "the Instant Meshes command line: with posy=4 the pure-quad result has about 4 x the count.\n"
          "posy=4 returns faces uint32 [q, 4] (quads only), posy=3 returns uint32 [q, 3].\n"
          "crease_angle in degrees, negative = off. This build runs on one thread whatever 'threads' says;\n"
          "the same input gives the same output (since 0.1.2 meant to hold across platforms too).\n"
          "Raises ValueError for unusable input and RuntimeError when the core fails.");
}

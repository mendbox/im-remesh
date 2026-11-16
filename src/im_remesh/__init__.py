# SPDX-License-Identifier: GPL-3.0-or-later
"""Own Python binding for the Instant Meshes remeshing core (BSD 3-clause core, serial build without TBB).

    import im_remesh
    quad_vertices, quads = im_remesh.remesh(vertices_float32_n3, triangles_uint32_m3, target_face_count=2500)
"""
import numpy as _np

from ._core import remesh as _remesh, numeric_selftest, THREADING, INSTANT_MESHES_COMMIT

__version__ = "0.1.2"
__all__ = ["remesh", "THREADING", "INSTANT_MESHES_COMMIT", "numeric_selftest", "__version__"]


def _as_vertices(vertices):
    v = _np.asarray(vertices)
    if v.dtype.kind not in "fiu":
        raise TypeError("vertices must be a numeric array of shape [n, 3], got dtype %s" % v.dtype)
    return _np.ascontiguousarray(v, dtype=_np.float32)


def _as_faces(faces):
    f = _np.asarray(faces)
    if f.dtype != _np.uint32:
        if f.dtype.kind not in "iu":
            raise TypeError("faces must be an integer array of shape [m, 3], got dtype %s" % f.dtype)
        if f.size and (int(f.min()) < 0 or int(f.max()) > 0xFFFFFFFF):
            raise ValueError("a face index is negative or does not fit in 32 bits")
    return _np.ascontiguousarray(f, dtype=_np.uint32)


def remesh(vertices, faces, target_vertex_count=-1, target_face_count=-1, rosy=4, posy=4, crease_angle=-1.0, extrinsic=True,
           align_to_boundaries=False, smooth_iter=2, deterministic=True, threads=1, scale=-1.0, verbose=False,
           max_work_triangles=8000000):
    """Remesh a triangle mesh with Instant Meshes. Returns (vertices float32 [k, 3], faces uint32 [q, 4] or [q, 3]).

    vertices: [n, 3] floats; faces: [m, 3] integers (other dtypes are converted, integers after a range check).
    Size: scale (edge length, input units) wins over target_face_count, which wins over target_vertex_count; with none
    given the target is n / 16 vertices. The counts are those of the Instant Meshes command line: with posy=4 the
    pure-quad result has about 4 x the count. posy=4 returns four-corner faces, posy=3 triangles.
    crease_angle in degrees; negative = off, 0 = every vertex is a crease vertex.
    This build runs on one thread whatever 'threads' says; the same input gives the same output, and since
    0.1.2 the build is made to give the same bits on every platform (tests/fingerprint.py checks it).
    max_work_triangles: the call is refused (ValueError) if the input would first have to be subdivided into more
    triangles than this (0 = no limit); memory follows the target, not the input.
    Raises TypeError for wrong shapes or dtypes, ValueError for unusable input (including a mesh too far from the
    origin for the asked edge length) and RuntimeError when the core fails. An empty result (0 faces) is possible.
    """
    return _remesh(_as_vertices(vertices), _as_faces(faces), target_vertex_count, target_face_count, rosy, posy, crease_angle,
                   extrinsic, align_to_boundaries, smooth_iter, deterministic, threads, scale, verbose, max_work_triangles)

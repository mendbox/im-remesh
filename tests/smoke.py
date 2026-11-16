# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke test: <python with numpy> smoke.py <dir of the unpacked wheel>. Torus, API shape, errors, repeatability."""
import sys, hashlib
sys.path.insert(0, sys.argv[1])
import numpy as np, im_remesh
nu, nv = 160, 80
a, b = np.meshgrid(np.arange(nu) * 2 * np.pi / nu, np.arange(nv) * 2 * np.pi / nv, indexing="ij")
r = 1 + 0.35 * np.cos(b)
v = np.stack([r * np.cos(a), r * np.sin(a), 0.35 * np.sin(b)], -1).reshape(-1, 3).astype(np.float32)
i, j = np.meshgrid(np.arange(nu), np.arange(nv), indexing="ij")
q = lambda di, dj: (((i + di) % nu) * nv + (j + dj) % nv).ravel()
f = np.concatenate([np.stack([q(0, 0), q(1, 0), q(1, 1)], 1), np.stack([q(0, 0), q(1, 1), q(0, 1)], 1)]).astype(np.uint32)
h = set()
for _ in range(3):
    rv, rf = im_remesh.remesh(v, f, target_face_count=500)
    h.add(hashlib.sha1(rv.tobytes() + rf.tobytes()).hexdigest()[:12])
assert rv.dtype == np.float32 and rf.dtype == np.uint32 and rv.shape[1] == 3 and rf.shape[1] == 4, (rv.dtype, rf.dtype, rv.shape, rf.shape)
assert len(h) == 1, h
assert (np.diff(np.sort(rf, 1), axis=1) != 0).all(), "face with a repeated corner"
tv, tf = im_remesh.remesh(v, f, target_face_count=500, posy=3)
assert tf.shape[1] == 3
sv, sf = im_remesh.remesh(v.astype(np.float64), f.astype(np.int64), scale=0.1)      # other dtypes are converted
kv, kf = im_remesh.remesh(v, f, target_vertex_count=400, crease_angle=30, align_to_boundaries=True, extrinsic=False, smooth_iter=0, deterministic=False, threads=4)
for bad, exc in (((v, f[:, :2]), TypeError), ((v, f + 100000), ValueError), ((v * np.nan, f), ValueError), ((v, f, -1, 500, 5), ValueError)):
    try: im_remesh.remesh(*bad); raise SystemExit("no error for bad input")
    except exc: pass
print("SMOKE ok python %d.%d numpy %s im_remesh %s %s: quads %d (hash %s), tris %d, scale=0.1 quads %d, misc %d" % (
    *sys.version_info[:2], np.__version__, im_remesh.__version__, im_remesh.THREADING, len(rf), h.pop(), len(tf), len(sf), len(kf)))

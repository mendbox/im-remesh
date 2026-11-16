# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-platform fingerprint of im_remesh.remesh().

    <python with numpy> fingerprint.py <dir of the unpacked wheel> [--out FILE] [--ref FILE] [--write-ref]

Remeshes four small meshes at several settings and prints one line per case: counts and a hash of the result arrays.
The text must be the same on every platform and Python version. It is compared with tests/fingerprint_ref.txt (written
on macOS arm64); exit code 1 and a list of the differing lines when it is not equal. --out also writes the text to a
file (CI uploads it), --write-ref replaces the reference.

The input meshes are made with plain Python floats and + - * / sqrt only (sine and cosine come from a short polynomial
below, not from the math library), so the input bits are the same everywhere too; their hashes are in the text.
"""
import sys, os, math, hashlib

args = sys.argv[1:]
def option(name, with_value=True):
    if name not in args:
        return None
    i = args.index(name); args.pop(i)
    return args.pop(i) if with_value else True
out_path = option("--out"); ref_path = option("--ref"); write_ref = option("--write-ref", False)
ref_path = ref_path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "fingerprint_ref.txt")
sys.path.insert(0, args[0])
import numpy as np, im_remesh

PI = 3.141592653589793

def sincos(x):
    """sin and cos from IEEE-exact operations only (same idea as src/serial_shim/im_portable_math.h)."""
    k = math.floor(x * 0.6366197723675814 + 0.5)
    r = (x - k * 1.5707963267341256) - k * 6.077100506506192e-11
    z = r * r
    s = 0.0
    for d in (355687428096000.0, -1307674368000.0, 6227020800.0, -39916800.0, 362880.0, -5040.0, 120.0, -6.0):
        s = s * z + 1.0 / d
    s = r + r * (z * s)
    c = 0.0
    for d in (20922789888000.0, -87178291200.0, 479001600.0, -3628800.0, 40320.0, -720.0, 24.0, -2.0):
        c = c * z + 1.0 / d
    c = 1.0 + z * c
    return ((s, c), (c, -s), (-s, -c), (-c, s))[k & 3]

def torus(nu, nv):
    v = []
    for i in range(nu):
        sa, ca = sincos(2 * PI * i / nu)
        for j in range(nv):
            sb, cb = sincos(2 * PI * j / nv)
            r = 1.0 + 0.35 * cb
            v.append((r * ca, r * sa, 0.35 * sb))
    f = []
    for i in range(nu):
        for j in range(nv):
            a, b, c, d = i * nv + j, ((i + 1) % nu) * nv + j, ((i + 1) % nu) * nv + (j + 1) % nv, i * nv + (j + 1) % nv
            f += [(a, b, c), (a, c, d)]
    return v, f

def icosphere(levels):
    t = (1.0 + math.sqrt(5.0)) / 2.0; s = math.sqrt(1.0 + t * t)
    v = [(x / s, y / s, z / s) for x, y, z in ((-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                                (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1))]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(levels):
        mid = {}; g = []
        for tri in f:
            e = []
            for k in range(3):
                a, b = tri[k], tri[(k + 1) % 3]
                key = (min(a, b), max(a, b))
                if key not in mid:
                    q = [(v[a][d] + v[b][d]) * 0.5 for d in range(3)]
                    n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2])
                    v.append((q[0] / n, q[1] / n, q[2] / n)); mid[key] = len(v) - 1
                e.append(mid[key])
            g += [(tri[0], e[0], e[2]), (tri[1], e[1], e[0]), (tri[2], e[2], e[1]), (e[0], e[1], e[2])]
        f = g
    return v, f

def twisted_box(n):
    dims = (n, n, 2 * n); index = {}; grid = []; f = []
    def vid(p):
        if p not in index:
            index[p] = len(grid); grid.append(p)
        return index[p]
    for axis in range(3):
        u, w = (axis + 1) % 3, (axis + 2) % 3
        for side in (0, 1):
            for i in range(dims[u]):
                for j in range(dims[w]):
                    ids = []
                    for du, dw in ((0, 0), (1, 0), (1, 1), (0, 1)):
                        p = [0, 0, 0]; p[axis] = dims[axis] if side else 0; p[u] = i + du; p[w] = j + dw
                        ids.append(vid(tuple(p)))
                    f += [(ids[0], ids[1], ids[2]), (ids[0], ids[2], ids[3])] if side else [(ids[0], ids[2], ids[1]), (ids[0], ids[3], ids[2])]
    v = []
    for gx, gy, gz in grid:
        x, y, z = gx / n - 0.5, gy / n - 0.5, gz / n - 1.0
        sa, ca = sincos(0.25 * PI * (z + 1.0))
        v.append((x * ca - y * sa, x * sa + y * ca, z))
    return v, f

def sheet(n):
    v = []
    for i in range(n + 1):
        for j in range(n + 1):
            x, y = 2.0 * i / n - 1.0, 2.0 * j / n - 1.0
            v.append((x, y * (1.0 + 0.2 * x), 0.3 * x * x - 0.2 * y * y * y + 0.1 * x * y))
    f = []
    for i in range(n):
        for j in range(n):
            a, b, c, d = i * (n + 1) + j, (i + 1) * (n + 1) + j, (i + 1) * (n + 1) + j + 1, i * (n + 1) + j + 1
            f += [(a, b, c), (a, c, d)]
    return v, f

def sha(*arrays):
    h = hashlib.sha1()
    for a in arrays:
        h.update(np.ascontiguousarray(a).astype(a.dtype.newbyteorder("<"), copy=False).tobytes())
    return h.hexdigest()[:16]

MESHES = {name: (np.array(v, np.float32), np.array(f, np.uint32)) for name, (v, f) in (
    ("icosphere", icosphere(4)), ("sheet", sheet(40)), ("torus", torus(96, 48)), ("twisted_box", twisted_box(16)))}
CASES = [
    ("torus", dict(target_face_count=300)),
    ("torus", dict(target_face_count=1500)),
    ("torus", dict(target_face_count=300, posy=3, rosy=6)),
    ("torus", dict(scale=0.12, crease_angle=0)),
    ("torus", dict(target_vertex_count=400, crease_angle=30, align_to_boundaries=True, extrinsic=False, smooth_iter=0, deterministic=False)),
    ("icosphere", dict(target_face_count=250)),
    ("icosphere", dict(target_face_count=1000, crease_angle=0)),
    ("icosphere", dict(target_face_count=2500)),
    ("twisted_box", dict(target_face_count=300, crease_angle=30)),
    ("twisted_box", dict(target_face_count=1200)),
    ("twisted_box", dict(target_face_count=1200, crease_angle=0)),
    ("sheet", dict(target_face_count=250, align_to_boundaries=True)),
    ("sheet", dict(target_face_count=1000, align_to_boundaries=True, crease_angle=20)),
    ("sheet", dict(target_face_count=500, posy=3)),
]

lines = ["numeric " + im_remesh.numeric_selftest()]
for name in sorted(MESHES):
    v, f = MESHES[name]
    lines.append("input %s: %d vertices, %d triangles, hash %s" % (name, len(v), len(f), sha(v, f)))
repeat_trouble = []
for name, kw in CASES:
    v, f = MESHES[name]
    rv, rf = im_remesh.remesh(v, f, **kw)
    rv2, rf2 = im_remesh.remesh(v, f, **kw)
    label = " ".join("%s=%s" % (k, kw[k]) for k in kw)
    if sha(rv, rf) != sha(rv2, rf2):
        repeat_trouble.append("%s %s" % (name, label))
    lines.append("%s %s: %d vertices, %d faces (width %d), hash %s" % (name, label, len(rv), len(rf), rf.shape[1], sha(rv, rf)))
text = "\n".join(lines) + "\n"
sys.stdout.write(text)
if out_path:
    with open(out_path, "w", newline="\n") as fh: fh.write(text)
if repeat_trouble:
    raise SystemExit("FINGERPRINT: two runs in one process differ: %s" % "; ".join(repeat_trouble))
if write_ref:
    with open(ref_path, "w", newline="\n") as fh: fh.write(text)
    print("FINGERPRINT reference written: %s" % ref_path)
else:
    with open(ref_path) as fh: want = fh.read().splitlines()
    got = text.splitlines(); bad = 0
    for i in range(max(len(want), len(got))):
        w = want[i] if i < len(want) else "(no line)"; g = got[i] if i < len(got) else "(no line)"
        if w != g:
            bad += 1; print("FINGERPRINT DIFFERS\n  reference:  %s\n  this build: %s" % (w, g))
    if bad:
        raise SystemExit("FINGERPRINT: %d of %d lines differ from %s (python %d.%d, numpy %s, %s)" % (
            bad, len(got), ref_path, sys.version_info[0], sys.version_info[1], np.__version__, sys.platform))
    print("FINGERPRINT ok: %d lines equal to %s (python %d.%d, numpy %s, %s)" % (
        len(got), os.path.basename(ref_path), sys.version_info[0], sys.version_info[1], np.__version__, sys.platform))

# SPDX-License-Identifier: GPL-3.0-or-later
"""Awkward inputs, each in its own process so that a crash of the core shows up as a result, not as the end of the test.
<python with numpy> edge_cases.py <dir of the unpacked wheel>            (prints one line per case; exit 1 if any case crashed, hung or was not refused when it should be)"""
import sys, subprocess, json
CASES = ["cube_coarse", "cube_tiny_target", "cube_huge_target", "single_triangle", "two_islands", "unused_vertices", "zero_area_face", "duplicate_faces",
         "open_plane", "open_plane_aligned", "nonmanifold_fin", "coincident_vertices", "flipped_half", "repeat_200_calls",
         # must be refused at once (see "Two guards" in README.md); run the whole file under a small memory limit to prove "at once"
         "far_from_origin", "target_too_fine", "index_wraps_32bit", "float_faces", "far_but_coarse_target"]
EXPECT = {"far_from_origin": ValueError, "target_too_fine": ValueError, "index_wraps_32bit": ValueError, "float_faces": TypeError}
if len(sys.argv) == 2:
    bad = 0
    for c in CASES:
        try:
            p = subprocess.run([sys.executable, "-I", __file__, sys.argv[1], c], capture_output=True, text=True, timeout=300)
            line = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else "no output; stderr: " + p.stderr.strip()[-200:]
            status = "ok" if p.returncode == 0 else ("WRONG (exit 3)" if p.returncode == 3 else "CRASH (exit %d)" % p.returncode)
        except subprocess.TimeoutExpired:
            line, status = "", "HANG (300 s)"
        bad += status != "ok"
        print("%-20s %-16s %s" % (c, status, line), flush=True)
    sys.exit(1 if bad else 0)

sys.path.insert(0, sys.argv[1]); case = sys.argv[2]
import numpy as np, im_remesh
try: import resource
except ImportError: resource = None                    # Windows: peak working set through ctypes instead
def peak_rss():
    if resource: return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    import ctypes, ctypes.wintypes as w
    class PMC(ctypes.Structure): _fields_ = [("cb", w.DWORD), ("PageFaultCount", w.DWORD)] + [(n, ctypes.c_size_t) for n in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]
    k = ctypes.WinDLL("kernel32", use_last_error=True); p = ctypes.WinDLL("psapi", use_last_error=True)
    k.GetCurrentProcess.restype = w.HANDLE; p.GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(PMC), w.DWORD]
    m = PMC(); m.cb = ctypes.sizeof(PMC); p.GetProcessMemoryInfo(k.GetCurrentProcess(), ctypes.byref(m), m.cb); return m.PeakWorkingSetSize
def cube():
    v = np.array([[x, y, z] for x in (0, 1) for y in (0, 1) for z in (0, 1)], np.float32)
    f = np.array([[0, 1, 3], [0, 3, 2], [4, 6, 7], [4, 7, 5], [0, 4, 5], [0, 5, 1], [2, 3, 7], [2, 7, 6], [0, 2, 6], [0, 6, 4], [1, 5, 7], [1, 7, 3]], np.uint32)
    return v, f
def plane(n=40):
    g = np.arange(n + 1, dtype=np.float32) / n
    v = np.stack([*np.meshgrid(g, g, indexing="ij"), np.zeros((n + 1, n + 1), np.float32)], -1).reshape(-1, 3)
    i, j = np.meshgrid(np.arange(n), np.arange(n), indexing="ij"); a = (i * (n + 1) + j).ravel()
    f = np.concatenate([np.stack([a, a + n + 1, a + n + 2], 1), np.stack([a, a + n + 2, a + 1], 1)]).astype(np.uint32)
    return v, f
kw = dict(target_face_count=200)
v, f = cube()
if case == "cube_tiny_target": kw = dict(target_face_count=1)
elif case == "cube_huge_target": kw = dict(target_face_count=20000)
elif case == "single_triangle": v, f = v[:3].copy(), np.array([[0, 1, 2]], np.uint32)
elif case == "two_islands": v, f = np.concatenate([v, v + 5]), np.concatenate([f, f + 8])
elif case == "unused_vertices": v = np.concatenate([v, v + 5])
elif case == "zero_area_face": v = np.concatenate([v, v[:1], v[:1]]); f = np.concatenate([f, [[0, 8, 9]]]).astype(np.uint32)
elif case == "duplicate_faces": f = np.concatenate([f, f])
elif case == "open_plane": v, f = plane()
elif case == "open_plane_aligned": v, f = plane(); kw["align_to_boundaries"] = True
elif case == "nonmanifold_fin": v = np.concatenate([v, [[0.5, 0.5, 2]]]).astype(np.float32); f = np.concatenate([f, [[1, 3, 8]]]).astype(np.uint32)
elif case == "coincident_vertices": v[1] = v[0]
elif case == "flipped_half": f[:6] = f[:6, ::-1]
elif case == "far_from_origin": v = v + np.float32(1e6); kw = dict(target_face_count=2500)          # used to loop forever, 1.2 GB after 1.8 s
elif case == "target_too_fine": kw = dict(target_face_count=2000000)                                 # about 18 million triangles after the split
elif case == "index_wraps_32bit": f = f.astype(np.int64) + 2 ** 32                                   # used to wrap to valid indices
elif case == "float_faces": f = f.astype(np.float64) + 0.5                                           # used to be truncated
elif case == "far_but_coarse_target": v = v * 1000 + np.float32(1e6); kw = dict(target_face_count=6)  # big and far, but resolvable: must work
try:
    if case == "repeat_200_calls":
        v, f = plane(60); im_remesh.remesh(v, f, **kw); r0 = peak_rss()
        for _ in range(200): rv, rf = im_remesh.remesh(v, f, **kw)
        print("%d quads; peak memory grew by %.1f MB over 200 calls" % (len(rf), (peak_rss() - r0) / 2 ** 20))
    else:
        import time; t0 = time.perf_counter()
        try: rv, rf = im_remesh.remesh(v, f, **kw)
        except Exception as e:
            if isinstance(e, EXPECT.get(case, ())): print("refused in %.3f s, %s: %s" % (time.perf_counter() - t0, type(e).__name__, e)); sys.exit(0)
            raise
        if case in EXPECT: print("NOT REFUSED: expected %s" % EXPECT[case].__name__); sys.exit(3)
        print("%d vertices, %d faces, finite %s, faces with a repeated corner %d" % (len(rv), len(rf), bool(np.isfinite(rv).all()),
              int((np.diff(np.sort(rf, 1), axis=1) == 0).any(axis=1).sum()) if len(rf) else 0))
except (ValueError, RuntimeError) as e:
    print("%s: %s" % (type(e).__name__, e))

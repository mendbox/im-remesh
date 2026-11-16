**Part of [AI Asset Maker for Blender](https://mendboxstudio.com).**

# im-remesh

Deterministic, cross-platform Python binding for Instant Meshes (field-aligned quad remeshing), used by AI Asset Maker for Blender.

`im_remesh.remesh()` takes a triangle mesh as NumPy arrays and returns a quad (or triangle) mesh. It is built on the
[Instant Meshes](https://github.com/wjakob/instant-meshes) remeshing core. The viewer, file readers, nanogui, GLFW and
OpenGL are not part of it, and neither is Intel TBB. The compiled module is about 220 KB.

Why a new binding: an existing Python binding for Instant Meshes had no licence file, so it could not be redistributed
inside a GPL-3.0-or-later Blender extension. This one is written from the Instant Meshes sources and the public
interface names only, and carries clear licences for everything it contains (see Licences).

Status of 0.1.2: wheels are built and tested on macOS arm64 (Python 3.11 and 3.13, Blender 4.2 and 5.2) and Linux x86_64
(manylinux_2_28, in CI). Windows is not built or tested yet.

## Install / build

Needs a C++11 compiler, CMake 3.18+, `patch` or `git` on the PATH (for the one patch applied to a copy of the Instant
Meshes sources at configure time), and Python 3.11+ with NumPy. All sources, including nanobind 3.1.0, are in this
repository (`third_party/`), so the build needs no network except for the build backend `scikit-build-core`.

```sh
pip wheel . --no-deps -w dist          # or: uv build --wheel --out-dir dist .
pip install numpy dist/im_remesh-*.whl
```

On Python 3.11 this gives a normal `cp311` wheel; on 3.12 and later one `abi3` wheel that works for every Python from 3.12.
Prebuilt wheels (macOS arm64, Linux x86_64) and a source tarball are attached to each
[release](https://github.com/mendbox/im-remesh/releases).

Core only, without Python (first thing to run on a new machine):

```sh
cmake -S . -B build/core -DIM_SKIP_PYTHON=ON -DIM_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/core -j4
./build/core/im_core_check tests/core_fingerprint_ref.txt     # exit 0 = equal to the reference
```

Linux wheels are built in the `manylinux_2_28` container by `ci/build_linux.sh`; `.github/workflows/wheels-linux.yml`
runs it on every push to `main`.

## API

```python
import im_remesh
vertices, faces = im_remesh.remesh(
    vertices,                    # float32 [n, 3] (other numeric types are converted)
    faces,                       # uint32 [m, 3] triangles (other integer types are converted after a range check)
    target_vertex_count=-1,
    target_face_count=-1,
    rosy=4,                      # 2, 4 or 6
    posy=4,                      # 4 = quads, 3 = triangles
    crease_angle=-1,             # degrees; negative = off, 0 = every vertex is a crease vertex
    extrinsic=True,
    align_to_boundaries=False,
    smooth_iter=2,
    deterministic=True,
    threads=1,                   # accepted for the call shape; this build always runs on one thread
    scale=-1,                    # target edge length in input units, wins over both counts
    verbose=False,               # print the upstream progress text (to the C++ stdout)
    max_work_triangles=8000000,  # refuse inputs that would first be subdivided beyond this (0 = no limit)
)
```

- Size units are those of the Instant Meshes command line. With `posy=4` the pure-quad result has about 4 x
  `target_face_count` quads. For 10,000 quads ask for 2,500. With no size argument the target is n / 16 vertices.
- `crease_angle=0` is not "off": any value >= 0 is a threshold, so 0 marks every vertex as a crease vertex.
- Errors: `ValueError` for unusable input (empty mesh, index out of range or beyond 32 bits, NaN, a face that repeats a
  vertex, bad `rosy` / `posy`, or an input the two guards refuse), `TypeError` for wrong array shapes or non-integer
  faces, `RuntimeError` if the core fails.
- Two guards refuse inputs that would make upstream run away: a mesh too far from the origin for the asked edge length
  (pass object-local coordinates centred on the bounding box), and a target so fine that the input would first be
  subdivided beyond `max_work_triangles`.
- Input made of exact duplicate faces returns an empty mesh (0 vertices, 0 faces); check for that.
- The GIL is released during the call; one call runs at a time.
- Constants: `im_remesh.__version__`, `im_remesh.THREADING` (`"serial"`), `im_remesh.INSTANT_MESHES_COMMIT`;
  `im_remesh.numeric_selftest()` returns one line describing the numeric setup of the binary.

## Determinism

The same input arrays give the same output arrays, bit for bit, on macOS arm64 and Linux x86_64 (Windows x64 is the
target but not tested yet). Calls on one thread repeat exactly; there is no TBB, the few TBB names the core uses are
answered by plain serial code in `src/serial_shim/`. What was done to make results platform independent:

- floating-point contraction (fused multiply-add) is switched off at compile time, no fast-math;
- `sin`, `cos`, `exp`, `atan2`, `acos` are replaced by own versions built from IEEE-exact operations
  (`src/serial_shim/im_portable_math.h`);
- `std::sort` on keys that can be equal becomes `std::stable_sort`, the priority queue breaks ties by insertion order,
  an `std::unordered_set` becomes a `std::set`;
- Eigen's hand-written SIMD code is disabled.

The Instant Meshes sources in `third_party/` are unmodified; one patch (`patches/instant-meshes-determinism.patch`,
16 small edits, each marked `im_remesh patch`) is applied to a copy at build time. The algorithm is not changed.

`tests/core_check.cpp` (no Python) and `tests/fingerprint.py` (through the wheel) remesh four small synthetic meshes at 14
settings each and compare counts and hashes with references written on macOS arm64
(`tests/core_fingerprint_ref.txt`, `tests/fingerprint_ref.txt`). CI fails when the Linux build differs by one bit.
`tests/smoke.py` and `tests/edge_cases.py` cover the API and error handling.

## Licences

Our code (`src/`, `tests/`, `ci/`, `CMakeLists.txt`, the patch) is **GPL-3.0-or-later**, see `LICENSE`. The compiled
module also contains third-party code under its own licences: Instant Meshes (BSD 3-clause), Eigen 3.2.9 (MPL-2.0, some
files BSD, built with `EIGEN_MPL2_ONLY`), pcg32 (Apache-2.0), dset (zlib-style), RPly (MIT), nanobind 3.1.0 (BSD
3-clause) and tsl::robin_map (MIT). The full table with source commits, the note on the build-time patch and all licence
texts are in [`THIRD_PARTY.md`](THIRD_PARTY.md) and `licenses/`.

Other licence terms: support@mendboxstudio.com

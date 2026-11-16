# Third-party code in im_remesh

The binding itself (`src/binding.cpp`, `src/im_core.cpp`, `src/im_core.h`, `src/im_fp.h`, `src/serial_shim/`, `src/im_remesh/`, `tests/`, `ci/`) is ours,
GPL-3.0-or-later (`LICENSE`). The compiled module also contains the code below. Every licence text is in `licenses/` and is
packed into the wheel under `im_remesh-<version>.dist-info/licenses/`. When the module is copied into a Blender extension,
copy this file and the `licenses/` folder with it.

| Component | What is used | Licence | Text | Source |
|---|---|---|---|---|
| Instant Meshes | `third_party/instant-meshes/src/` core files, **compiled with one patch of ours** (see "Modified third-party code" below; the copy in `third_party/` is unmodified): adjacency, bvh, cleanup, dedge, extract, field, hierarchy, meshstats, normal, reorder, smoothcurve, subdivide, serializer (+ headers). `src/im_core.cpp` follows the call sequence of its `batch.cpp` and `Optimizer::run()` and repeats short blocks of `batch.cpp` (size formulas, boundary-constraint loop), credited in the file header | BSD 3-clause, with an added paragraph on "Enhancements" | `licenses/instant-meshes-LICENSE.txt` | https://github.com/wjakob/instant-meshes, commit 7b3160864a2e1025af498c84cfed91cbfb613698 |
| Eigen 3.2.9 | headers, built with `EIGEN_MPL2_ONLY` (no LGPL parts) | MPL-2.0, some files BSD | `licenses/eigen-COPYING.MPL2.txt`, `licenses/eigen-COPYING.BSD.txt` | https://github.com/libigl/eigen, commit c34a9130bc585b288703bd9716d7efae194974e2 (the copy pinned by Instant Meshes through nanogui) |
| pcg32 | `pcg32.h` (random numbers) | Apache-2.0 | `licenses/pcg32-Apache-2.0.txt` | https://github.com/wjakob/pcg32, commit 0ef13e68ca0be5506e1cfc0db76831e6f916e9e9 |
| dset | `dset.h` (disjoint sets) | zlib-style | `licenses/dset-LICENSE.txt` | https://github.com/wjakob/dset, commit 7967ef0e6041cd9d73b9c7f614ab8ae92e9e587a |
| RPly 1.1.3 | `rply.c`, linked only because the unused save/load code of the core refers to it | MIT | `licenses/rply-LICENSE.txt` | in the Instant Meshes tree, `ext/rply` |
| nanobind 3.1.0 | Python binding layer, linked statically | BSD 3-clause | `licenses/nanobind-LICENSE.txt` | `third_party/nanobind/`, https://github.com/wjakob/nanobind |
| tsl::robin_map | hash map inside nanobind | MIT | `licenses/robin_map-LICENSE.txt` | https://github.com/Tessil/robin-map (copy shipped with nanobind) |

Not used, not linked: Intel TBB (replaced by `src/serial_shim/`, written from the public interface names), pss, nanogui, GLFW,
NanoVG, GLEW, half, and the PyPI package pynanoinstantmeshes (an existing binding that had no licence file; this project does not use it).

Modified third-party code

- **Instant Meshes, since im_remesh 0.1.2 (November 2026).** The build does not compile the upstream files as they are:
  CMake copies `third_party/instant-meshes/src` into the build directory and applies
  `patches/instant-meshes-determinism.patch` to the copy. The patch changes six files (`bvh.cpp`, `common.h`,
  `extract.cpp`, `hierarchy.cpp`, `normal.cpp`, `reorder.cpp`), 16 places, each marked `im_remesh patch`:
  `std::sort` becomes `std::stable_sort` where equal keys occur, `std::cos` / `sin` / `exp` / `atan2` / `acos` become the
  platform-independent versions of `src/serial_shim/im_portable_math.h` (ours, GPL-3.0-or-later), and one
  `std::unordered_set` becomes a `std::set`. Purpose: the same output bits on every platform; the algorithm is not
  changed. The BSD 3-clause licence allows modification and redistribution in source and binary form as long as the
  copyright notice, the conditions and the disclaimer are kept: the upstream files keep their headers, and
  `licenses/instant-meshes-LICENSE.txt` ships with every binary. The patch file states what it changes, who changed it
  and when, and offers the changed lines under the same BSD 3-clause terms. Nothing else in `third_party/` is modified in any way (Eigen, pcg32,
  dset, RPly are compiled as they are; Eigen with `EIGEN_MPL2_ONLY` and `EIGEN_DONT_VECTORIZE`, which are documented
  switches of Eigen, not changes to it).

Notes

- All of the above can be combined with GPL-3.0-or-later (BSD, MIT, zlib, Apache-2.0, MPL-2.0 without the "Incompatible With
  Secondary Licenses" notice).
- MPL-2.0 asks that recipients can get the source of the MPL files: the table gives repository and commit.
- The Instant Meshes licence ends with a paragraph on "Enhancements": whoever makes enhancements of that code available
  "without imposing a separate written license agreement" grants the authors a broad non-exclusive licence to them. The
  upstream files in `third_party/` are unchanged. Our own files each carry an explicit GPL-3.0-or-later notice, which is
  such a separate written licence, so the automatic grant should not apply to them; if it did, it would only be a
  non-exclusive licence to the authors on our additions and would not restrict anyone who receives the code under the
  GPL. The patch of 0.1.2 (16 small edits to upstream files) is offered under the upstream BSD terms themselves; if the
  "Enhancements" grant covers those edits, the upstream authors may use them, which is fine with us. (Our reading and
  the reviewer's, not legal advice.)
- **For a release (GPL-3.0 section 6):** whoever ships the binaries must offer the complete corresponding source: our files
  plus the third-party sources at the commits in the table. This folder is laid out for that: `third_party/` holds exactly
  those sources (`third_party/VERSIONS.txt`) and `patches/` holds the one change made to them, so publishing this repository, or a source archive of it next to the extension,
  covers it. nanobind 3.1.0 is vendored in `third_party/nanobind/` (see `third_party/VERSIONS.txt`); only scikit-build-core, the build backend, comes from PyPI at build time.
- The source copies in `third_party/` are unmodified; the one patch applied at build time is in `patches/` and is part of the
  corresponding source. Eigen's LGPL-licensed files are in that copy but are excluded from every
  build by `EIGEN_MPL2_ONLY`. nanogui is not copied at all (upstream reaches Eigen through it).

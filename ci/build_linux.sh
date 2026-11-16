#!/bin/bash
# Runs INSIDE quay.io/pypa/manylinux_2_28_x86_64 with the repository mounted at /io (see .github/workflows/wheels-linux.yml
# and README.md). Builds the cp311 and the cp312-abi3 wheel, repairs them to manylinux, tests both, leaves them in /io/wheelhouse.
# Fingerprints (text that must be equal on every platform, see README.md "Same result on every platform") go to /io/fingerprint/
# and are compared with tests/core_fingerprint_ref.txt and tests/fingerprint_ref.txt, both written on macOS arm64. A mismatch
# does not stop the script (every fingerprint file is still written, CI uploads them) but makes it exit with 1 at the end.
set -euo pipefail
cd /io
export HOME=${HOME:-/tmp/home}; mkdir -p "$HOME"
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
W=/tmp/im_wheels; rm -rf "$W" wheelhouse fingerprint; mkdir -p "$W" wheelhouse fingerprint
FP=/io/fingerprint; mismatch=0
{ uname -srm; gcc --version | head -1; cmake --version | head -1; } > "$FP/platform.txt"

# 1. Core without Python: every case twice with identical results, quads only, and the core fingerprint against the reference
#    (exit code 3 = it differs: noted, the script goes on).
cmake -S . -B /tmp/im_core_build -DIM_SKIP_PYTHON=ON -DIM_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/im_core_build -j "$CMAKE_BUILD_PARALLEL_LEVEL"
rc=0; /tmp/im_core_build/im_core_check tests/core_fingerprint_ref.txt --out "$FP/core_fingerprint_linux_x86_64.txt" || rc=$?
if [ "$rc" = 3 ]; then mismatch=1; elif [ "$rc" != 0 ]; then echo "im_core_check failed with exit code $rc"; exit "$rc"; fi

# 2. Wheels. Each build gets its own build directory outside the mounted tree.
for py in cp311-cp311 cp313-cp313; do
  /opt/python/$py/bin/python -m pip wheel . --no-deps -w "$W" -C build-dir=/tmp/im_build_$py
done
for w in "$W"/im_remesh-*.whl; do auditwheel repair "$w" -w wheelhouse; done
ls -l wheelhouse

# 3. Tests on the repaired wheels: cp311 wheel on Python 3.11, abi3 wheel on Python 3.13 and 3.12.
t() { # t <python tag> <wheel glob>
  local v=/tmp/venv_$1; rm -rf "$v"; /opt/python/$1/bin/python -m venv "$v"
  "$v/bin/pip" install --quiet numpy wheelhouse/$2
  local site; site=$("$v/bin/python" -c "import im_remesh, os; print(os.path.dirname(os.path.dirname(im_remesh.__file__)))")
  "$v/bin/python" tests/smoke.py "$site" | tee "$FP/smoke_linux_x86_64_$1.txt"
  "$v/bin/python" tests/edge_cases.py "$site"
  "$v/bin/python" tests/fingerprint.py "$site" --out "$FP/fingerprint_linux_x86_64_$1.txt" || mismatch=1
}
t cp311-cp311 'im_remesh-*-cp311-cp311-manylinux*.whl'
t cp313-cp313 'im_remesh-*-cp312-abi3-manylinux*.whl'
t cp312-cp312 'im_remesh-*-cp312-abi3-manylinux*.whl'
if [ "$mismatch" != 0 ]; then
  echo "FINGERPRINT MISMATCH: this build does not give the same bits as the macOS arm64 reference (see the lines above and /io/fingerprint)"
  exit 1
fi
echo "linux wheels ok, fingerprints equal to the macOS arm64 reference"

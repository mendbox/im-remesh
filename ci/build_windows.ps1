# SPDX-License-Identifier: GPL-3.0-or-later
# Windows x64 mirror of ci/build_linux.sh. Run from the repository root in a "x64 Native Tools Command Prompt for VS 2022"
# (or after `vcvarsall.bat x64`), with x64 CPython 3.11 and 3.13 (python.org, `py` launcher), CMake and `patch` or `git` on the PATH:
#   powershell -ExecutionPolicy Bypass -File ci\build_windows.ps1
# Static CRT (/MT): the .pyd needs no vcruntime / msvcp DLLs beyond what Windows and Blender provide, so no delvewheel step.
# Builds the core check, the cp311 wheel and the cp312-abi3 wheel (built with Python 3.13), tests both and leaves them in
# wheelhouse\. Fingerprints go to fingerprint\ and are compared with tests\core_fingerprint_ref.txt and tests\fingerprint_ref.txt
# (written on macOS arm64). A mismatch does not stop the script (every fingerprint file is still written) but makes it exit 1.
param([string]$Py311 = "-3.11-64", [string]$Py313 = "-3.13-64", [string]$Py312 = "-3.12-64")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$root = (Get-Location).Path
$fp = Join-Path $root "fingerprint"; $wh = Join-Path $root "wheelhouse"; $tmp = Join-Path $env:TEMP "im_wheels"
foreach ($d in $fp, $wh, $tmp) { Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue; New-Item -ItemType Directory $d | Out-Null }
$mismatch = 0
$rt = "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
function Run($exe, [string[]]$a) { & $exe @a; if ($LASTEXITCODE -ne 0) { throw "$exe $($a -join ' ') failed ($LASTEXITCODE)" } }
@("$([Environment]::OSVersion.VersionString) $env:PROCESSOR_ARCHITECTURE", (cmd /c "cl 2>&1" | Select-Object -First 1), (cmake --version | Select-Object -First 1)) |
  Set-Content (Join-Path $fp "platform.txt")

# 1. Core without Python: every case twice with identical results, quads only, and the core fingerprint against the reference
#    (exit code 3 = it differs: noted, the script goes on).
$core = Join-Path $env:TEMP "im_core_build"; Remove-Item -Recurse -Force $core -ErrorAction SilentlyContinue
Run cmake @("-S", ".", "-B", $core, "-G", "Visual Studio 17 2022", "-A", "x64", "-DIM_SKIP_PYTHON=ON", "-DIM_BUILD_CLI=ON", $rt)
Run cmake @("--build", $core, "--config", "Release", "--parallel", "4")
& (Join-Path $core "Release\im_core_check.exe") tests\core_fingerprint_ref.txt --out (Join-Path $fp "core_fingerprint_windows_x64.txt")
$rc = $LASTEXITCODE
if ($rc -eq 3) { $mismatch = 1 } elseif ($rc -ne 0) { Write-Output "im_core_check failed with exit code $rc"; exit $rc }

# 2. Wheels (each build in its own build directory outside the tree; scikit-build-core picks the generator, so the
#    Visual Studio generator is named explicitly and the static CRT passed as a CMake define).
foreach ($v in $Py311, $Py313) {
  Run py @($v, "-m", "pip", "wheel", ".", "--no-deps", "-w", $tmp, "-C", "build-dir=$env:TEMP\im_build$($v.Replace('-', '_'))",
           "-C", "cmake.define.CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded", "-C", "cmake.args=-G;Visual Studio 17 2022;-A;x64")
}
Copy-Item "$tmp\im_remesh-*.whl" $wh
Get-ChildItem $wh | Format-Table Name, Length

# 3. Tests on the wheels: cp311 wheel on Python 3.11, abi3 wheel on Python 3.13 and (when installed) 3.12.
function T($pyArg, $tag, $glob) {
  $v = Join-Path $env:TEMP "venv_$tag"; Remove-Item -Recurse -Force $v -ErrorAction SilentlyContinue
  & py $pyArg -m venv $v
  if ($LASTEXITCODE -ne 0) { Write-Output "skip $tag (no such Python)"; return }
  $vp = Join-Path $v "Scripts\python.exe"
  Run $vp @("-m", "pip", "install", "--quiet", "numpy", (Get-ChildItem "$wh\$glob" | Select-Object -First 1).FullName)
  $site = (& $vp -c "import im_remesh, os; print(os.path.dirname(os.path.dirname(im_remesh.__file__)))").Trim()
  & $vp tests\smoke.py $site | Tee-Object (Join-Path $fp "smoke_windows_x64_$tag.txt")
  if ($LASTEXITCODE -ne 0) { throw "smoke failed on $tag" }
  Run $vp @("tests\edge_cases.py", $site)
  & $vp tests\fingerprint.py $site --out (Join-Path $fp "fingerprint_windows_x64_$tag.txt")
  if ($LASTEXITCODE -ne 0) { $script:mismatch = 1 }
}
T $Py311 "cp311" "im_remesh-*-cp311-cp311-win_amd64.whl"
T $Py313 "cp313" "im_remesh-*-cp312-abi3-win_amd64.whl"
T $Py312 "cp312" "im_remesh-*-cp312-abi3-win_amd64.whl"
if ($mismatch -ne 0) {
  Write-Output "FINGERPRINT MISMATCH: this build does not give the same bits as the macOS arm64 reference (see the lines above and fingerprint\)"
  exit 1
}
Write-Output "windows wheels ok, fingerprints equal to the macOS arm64 reference"

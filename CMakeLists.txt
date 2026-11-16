# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.18)
project(im_remesh LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# third_party/ is the committed copy of exactly the upstream files the build reads (the pinned commits are listed in
# third_party/VERSIONS.txt). Point IM_DIR / IM_EIGEN_DIR elsewhere to build against a fresh checkout.
set(IM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/instant-meshes" CACHE PATH "Instant Meshes sources (github.com/wjakob/instant-meshes)")
set(IM_EIGEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen" CACHE PATH "Eigen 3.2 headers (the copy pinned by upstream)")
set(IM_UPSTREAM_COMMIT "7b3160864a2e1025af498c84cfed91cbfb613698" CACHE STRING "Upstream commit of the Instant Meshes sources")
# A git checkout must be at the pinned commit; the committed copy has no .git and is taken as it is.
if(EXISTS "${IM_DIR}/.git")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${IM_DIR}" rev-parse HEAD OUTPUT_VARIABLE IM_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(IM_HEAD AND NOT IM_HEAD STREQUAL IM_UPSTREAM_COMMIT)
      message(FATAL_ERROR "${IM_DIR} is at ${IM_HEAD}, expected ${IM_UPSTREAM_COMMIT}")
    endif()
  endif()
endif()

foreach(f "${CMAKE_CURRENT_SOURCE_DIR}/patches/instant-meshes-determinism.patch" "${IM_DIR}/src/field.cpp" "${IM_DIR}/ext/pcg32/pcg32.h" "${IM_DIR}/ext/dset/dset.h" "${IM_EIGEN_DIR}/Eigen/Core")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "Missing ${f}: see README.md, section 'Sources'")
  endif()
endforeach()

# ---- Patched copy of the upstream sources. third_party/ stays as upstream wrote it; the build compiles a copy in the
# build directory with patches/instant-meshes-determinism.patch applied (same results on every platform; the file
# explains each change). Redone at every configure, and whenever the patch file changes.
set(IM_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/instant-meshes-determinism.patch")
set(IM_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/instant-meshes-patched")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${IM_PATCH_FILE}")
file(REMOVE_RECURSE "${IM_PATCHED_DIR}")
file(COPY "${IM_DIR}/src" DESTINATION "${IM_PATCHED_DIR}" FILES_MATCHING PATTERN "*.cpp" PATTERN "*.h")
find_program(IM_PATCH_TOOL patch)
find_package(Git QUIET)
set(IM_PATCH_RESULT 1)
if(IM_PATCH_TOOL)
  execute_process(COMMAND "${IM_PATCH_TOOL}" -p1 -N -i "${IM_PATCH_FILE}" WORKING_DIRECTORY "${IM_PATCHED_DIR}"
                  RESULT_VARIABLE IM_PATCH_RESULT OUTPUT_VARIABLE IM_PATCH_LOG ERROR_VARIABLE IM_PATCH_LOG)
elseif(GIT_FOUND)
  # GIT_CEILING_DIRECTORIES: git must not look for a repository above the copy (it would then skip the files silently).
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
                          "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${IM_PATCH_FILE}" WORKING_DIRECTORY "${IM_PATCHED_DIR}"
                  RESULT_VARIABLE IM_PATCH_RESULT OUTPUT_VARIABLE IM_PATCH_LOG ERROR_VARIABLE IM_PATCH_LOG)
else()
  message(FATAL_ERROR "Need 'patch' or 'git' on the PATH to apply ${IM_PATCH_FILE}")
endif()
if(NOT IM_PATCH_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not apply ${IM_PATCH_FILE} (line endings changed by git? see .gitattributes):\n${IM_PATCH_LOG}")
endif()
# Every patched file must really carry the change (a tool that "succeeds" without touching anything is caught here).
foreach(f bvh.cpp common.h extract.cpp hierarchy.cpp normal.cpp reorder.cpp)
  file(READ "${IM_PATCHED_DIR}/src/${f}" IM_PATCHED_TEXT)
  string(FIND "${IM_PATCHED_TEXT}" "im_remesh patch" IM_PATCH_MARK)
  if(IM_PATCH_MARK EQUAL -1)
    message(FATAL_ERROR "${IM_PATCH_FILE} was not applied to ${f}")
  endif()
endforeach()

# ---- Numerics: the same floating-point results on every platform (README.md, "Same result on every platform").
# im_fp.h is force-included everywhere and stops the build on fast-math or x87 evaluation.
function(im_numeric_flags target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:precise "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/im_fp.h")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")                      # clang-cl: /fp:precise alone still fuses a*b+c
      target_compile_options(${target} PRIVATE /clang:-ffp-contract=off)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      target_compile_options(${target} PRIVATE /arch:SSE2)
    endif()
  else()
    # No fused multiply-add (Clang fuses by default on arm64, GCC wherever the CPU flags allow it), no fast-math.
    target_compile_options(${target} PRIVATE -fno-fast-math -ffp-contract=off -include "${CMAKE_CURRENT_SOURCE_DIR}/src/im_fp.h")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|X86|AMD64|x86_64)$")
      target_compile_options(${target} PRIVATE -msse2 -mfpmath=sse)   # 32-bit x86: SSE2 instead of the x87 stack
    endif()
  endif()
endfunction()

# ---- Instant Meshes core: the patched upstream files, compiled without the viewer, the file readers and TBB.
# serializer.cpp and rply.c are only there because hierarchy.cpp / field.cpp refer to the (unused) save/load code.
set(IM_CORE_SOURCES adjacency bvh cleanup dedge extract field hierarchy meshstats normal reorder smoothcurve subdivide serializer)
list(TRANSFORM IM_CORE_SOURCES PREPEND "${IM_PATCHED_DIR}/src/")
list(TRANSFORM IM_CORE_SOURCES APPEND ".cpp")

add_library(im_core STATIC ${IM_CORE_SOURCES} "${IM_DIR}/ext/rply/rply.c" src/im_core.cpp)
# gnu++11, not c++11: upstream common.h uses hexadecimal float literals, which GCC refuses in strict C++11 mode
# (upstream's own CMake asks for -std=gnu++11 on GCC for this reason). No effect with MSVC.
set_target_properties(im_core PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
# field.cpp still contains the (unused) worker-thread class: std::thread needs the thread library on Linux.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(im_core PUBLIC Threads::Threads)
# The shim directory comes first so that <tbb/tbb.h>, <parallel_stable_sort.h> and <im_portable_math.h> resolve to our headers.
target_include_directories(im_core BEFORE PRIVATE src/serial_shim)
target_include_directories(im_core PRIVATE "${IM_PATCHED_DIR}/src" src)
target_include_directories(im_core SYSTEM PRIVATE "${IM_EIGEN_DIR}" "${IM_DIR}/ext/pcg32" "${IM_DIR}/ext/dset" "${IM_DIR}/ext/rply")
# EIGEN_DONT_VECTORIZE: Eigen's hand-written SSE2 / NEON paths add up in another order than its plain loops and differ
# per CPU family; without them every platform runs the same scalar code. (The data here are 3-vectors: no speed lost.)
target_compile_definitions(im_core PRIVATE EIGEN_MPL2_ONLY EIGEN_DONT_VECTORIZE)
im_numeric_flags(im_core)
if(WIN32)
  target_compile_definitions(im_core PRIVATE _USE_MATH_DEFINES)      # MSVC and MinGW alike
endif()
if(MSVC)
  target_compile_definitions(im_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(im_core PRIVATE /bigobj /W1)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(im_core PRIVATE EIGEN_DONT_ALIGN)
  endif()
else()
  target_compile_options(im_core PRIVATE -w)
endif()

# ---- Python module
if(IM_BUILD_CLI)
  add_executable(im_core_check tests/core_check.cpp)
  target_link_libraries(im_core_check PRIVATE im_core)
  target_include_directories(im_core_check PRIVATE src src/serial_shim)
  set_target_properties(im_core_check PROPERTIES CXX_STANDARD 11)
  im_numeric_flags(im_core_check)
endif()

if(NOT IM_SKIP_PYTHON)
  if(CMAKE_VERSION VERSION_LESS 3.26)
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
  else()
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS Development.SABIModule)
  endif()
  # nanobind 3.1.0 is vendored in third_party/nanobind (BSD 3-clause), so the build needs no network.
  set(nanobind_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanobind/cmake" CACHE PATH "" FORCE)
  find_package(nanobind CONFIG REQUIRED)
  # STABLE_ABI gives one abi3 binary for Python 3.12 and later; nanobind ignores it on older Pythons.
  nanobind_add_module(_core STABLE_ABI NB_STATIC NOMINSIZE src/binding.cpp)
  target_link_libraries(_core PRIVATE im_core)
  target_include_directories(_core PRIVATE src)
  target_compile_definitions(_core PRIVATE IM_UPSTREAM_COMMIT="${IM_UPSTREAM_COMMIT}")
  im_numeric_flags(_core)
  install(TARGETS _core LIBRARY DESTINATION im_remesh RUNTIME DESTINATION im_remesh)
endif()

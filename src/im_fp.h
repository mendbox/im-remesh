/* SPDX-License-Identifier: GPL-3.0-or-later
 * Force-included in front of every C and C++ file of the build (CMakeLists.txt: -include / /FI).
 * Stops the build when the compiler setup would make floating-point results depend on the platform. */
#ifndef IM_FP_H
#define IM_FP_H

#include <float.h>

#if defined(__FAST_MATH__) || (defined(_M_FP_FAST) && _M_FP_FAST)
#error "im_remesh: fast-math changes results between compilers; build without -ffast-math or /fp:fast"
#endif

/* 0 = every operation is rounded to its own type (SSE2, NEON). Anything else is x87-style extended precision. */
#if defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ != 0
#error "im_remesh: extended-precision float evaluation (x87); build with -msse2 -mfpmath=sse"
#elif !defined(__FLT_EVAL_METHOD__) && defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "im_remesh: extended-precision float evaluation (x87); build with SSE2 (/arch:SSE2)"
#endif

/* MSVC: no fused multiply-add even where /fp:precise would allow it (older toolsets, ARM64).
 * GCC and Clang get -ffp-contract=off on the command line instead. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma fp_contract(off)
#endif

#endif

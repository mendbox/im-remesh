// SPDX-License-Identifier: GPL-3.0-or-later
// Math functions that give the same bits on every platform.
//
// sin, cos, exp, atan2 and acos of the C library are not correctly rounded, and macOS libm, glibc and the MSVC runtime
// round differently in the last place. The Instant Meshes core feeds such values into discrete decisions (the random
// start field, a sort by angle, a best-corner choice), so one differing bit can end in another mesh.
// The functions below use only + - * / and std::sqrt, std::floor, std::ldexp on doubles, all of which IEEE 754 defines
// exactly, so with floating-point contraction switched off (see CMakeLists.txt) every compiler computes the same value.
// They are accurate to a few 1e-15, far below single precision, but are not meant as general replacements: no care is
// taken of huge arguments (|x| > 1e9 for sin / cos) or of infinities beyond what is noted.
//
// patches/instant-meshes-determinism.patch makes the upstream sources call these instead of std::.
#pragma once

#include <cmath>
#include <limits>

namespace imdet {

namespace detail {

// x = k * pi/2 + r with |r| <= pi/4 (about); returns r, k through the pointer. pi/2 is split in two parts so that
// k * hi is exact for the small k that occur.
inline double reduce_pio2(double x, long long *k) {
    const double two_over_pi = 0.63661977236758134308;
    const double pio2_hi = 1.57079632673412561417e+00;   // first 33 bits of pi/2
    const double pio2_lo = 6.07710050650619224932e-11;   // pi/2 - pio2_hi
    double kd = std::floor(x * two_over_pi + 0.5);
    *k = (long long) kd;
    return (x - kd * pio2_hi) - kd * pio2_lo;
}

inline double sin_kernel(double r) {                       // |r| <= 0.8, Taylor series up to r^17
    double z = r * r;
    double p = 1.0 / 355687428096000.0;
    p = p * z - 1.0 / 1307674368000.0;
    p = p * z + 1.0 / 6227020800.0;
    p = p * z - 1.0 / 39916800.0;
    p = p * z + 1.0 / 362880.0;
    p = p * z - 1.0 / 5040.0;
    p = p * z + 1.0 / 120.0;
    p = p * z - 1.0 / 6.0;
    return r + r * (z * p);
}

inline double cos_kernel(double r) {                       // |r| <= 0.8, Taylor series up to r^16
    double z = r * r;
    double p = 1.0 / 20922789888000.0;
    p = p * z - 1.0 / 87178291200.0;
    p = p * z + 1.0 / 479001600.0;
    p = p * z - 1.0 / 3628800.0;
    p = p * z + 1.0 / 40320.0;
    p = p * z - 1.0 / 720.0;
    p = p * z + 1.0 / 24.0;
    p = p * z - 0.5;
    return 1.0 + z * p;
}

inline double atan_kernel(double t) {                      // |t| <= 0.4143, odd series up to t^39
    double z = t * t;
    double p = 0.0;
    for (int n = 39; n >= 3; n -= 2)
        p = (((n - 1) / 2) % 2 ? -1.0 : 1.0) / (double) n + p * z;
    return t + t * (z * p);
}

inline double atan_positive(double a) {                    // a >= 0, finite
    const double pi_4 = 0.78539816339744830962, pi_2 = 1.57079632679489661923;
    if (a > 1.0)
        return pi_2 - atan_positive(1.0 / a);
    if (a > 0.41421356237309504880)                        // tan(pi/8): atan(a) = pi/4 + atan((a-1)/(a+1))
        return pi_4 + atan_kernel((a - 1.0) / (a + 1.0));
    return atan_kernel(a);
}

} // namespace detail

inline double sin(double x) {
    if (!(std::fabs(x) < 1e9))
        return std::numeric_limits<double>::quiet_NaN();
    long long k;
    double r = detail::reduce_pio2(x, &k);
    switch ((int) (k & 3)) {
        case 0: return detail::sin_kernel(r);
        case 1: return detail::cos_kernel(r);
        case 2: return -detail::sin_kernel(r);
        default: return -detail::cos_kernel(r);
    }
}

inline double cos(double x) {
    if (!(std::fabs(x) < 1e9))
        return std::numeric_limits<double>::quiet_NaN();
    long long k;
    double r = detail::reduce_pio2(x, &k);
    switch ((int) (k & 3)) {
        case 0: return detail::cos_kernel(r);
        case 1: return -detail::sin_kernel(r);
        case 2: return -detail::cos_kernel(r);
        default: return detail::sin_kernel(r);
    }
}

inline double exp(double x) {
    if (x != x)
        return x;
    if (x > 709.0)
        return std::numeric_limits<double>::infinity();
    if (x < -745.0)
        return 0.0;
    const double inv_ln2 = 1.44269504088896338700;
    const double ln2_hi = 6.93147180369123816490e-01;     // first 32 bits of ln 2
    const double ln2_lo = 1.90821492927058770002e-10;     // ln 2 - ln2_hi
    double kd = std::floor(x * inv_ln2 + 0.5);
    double r = (x - kd * ln2_hi) - kd * ln2_lo;            // |r| <= 0.347
    double p = 1.0 / 87178291200.0;                        // Taylor series up to r^14
    p = p * r + 1.0 / 6227020800.0;
    p = p * r + 1.0 / 479001600.0;
    p = p * r + 1.0 / 39916800.0;
    p = p * r + 1.0 / 3628800.0;
    p = p * r + 1.0 / 362880.0;
    p = p * r + 1.0 / 40320.0;
    p = p * r + 1.0 / 5040.0;
    p = p * r + 1.0 / 720.0;
    p = p * r + 1.0 / 120.0;
    p = p * r + 1.0 / 24.0;
    p = p * r + 1.0 / 6.0;
    p = p * r + 0.5;
    p = 1.0 + r + r * r * p;
    return std::ldexp(p, (int) kd);                        // exact scaling by a power of two
}

// Finite arguments only (anything else gives NaN). atan2(0, x >= 0) = 0, atan2(0, x < 0) = pi; the sign of a zero
// is ignored.
inline double atan2(double y, double x) {
    const double pi = 3.14159265358979323846, pi_2 = 1.57079632679489661923;
    if (!(std::fabs(x) <= std::numeric_limits<double>::max()) || !(std::fabs(y) <= std::numeric_limits<double>::max()))
        return std::numeric_limits<double>::quiet_NaN();
    double ax = std::fabs(x), ay = std::fabs(y), a;
    if (ay == 0)
        a = 0.0;
    else if (ax == 0)
        a = pi_2;
    else
        a = detail::atan_positive(ay / ax);
    if (x < 0)
        a = pi - a;
    return y < 0 ? -a : a;
}

inline double acos(double x) {                             // NaN outside [-1, 1], like the C library
    if (!(x >= -1.0 && x <= 1.0))
        return std::numeric_limits<double>::quiet_NaN();
    return atan2(std::sqrt((1.0 - x) * (1.0 + x)), x);
}

// Single precision: computed in double, rounded once.
inline float sin(float x) { return (float) sin((double) x); }
inline float cos(float x) { return (float) cos((double) x); }
inline float exp(float x) { return (float) exp((double) x); }
inline float atan2(float y, float x) { return (float) atan2((double) y, (double) x); }
inline float acos(float x) { return (float) acos((double) x); }

} // namespace imdet

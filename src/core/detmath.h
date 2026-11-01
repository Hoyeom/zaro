// Deterministic transcendental functions for gameplay (D-005, D-031): only IEEE-754 basic
// operations, which give the same bits on every platform (ZARO builds with -ffp-contract=off and
// /fp:precise). The C library's sin, cos and exp2 may differ in the last bit between platforms, so
// anything that feeds the simulation or a state hash uses these instead.
#pragma once

#include <math.h>

namespace zaro {

// sin and cos of an angle in degrees, accurate to about 1e-15.
inline void det_sincos_deg(double degrees, double* s, double* c) {
    double d = fmod(degrees, 360.0);  // exact
    if (d < 0.0) d += 360.0;
    // Quadrant and the remainder in [-45, 45] degrees.
    int q = (int)floor(d / 90.0 + 0.5);
    double r = (d - 90.0 * q) * (3.14159265358979323846 / 180.0);
    q &= 3;
    const double r2 = r * r;
    // Taylor series to r^17 / r^16: |r| <= pi/4, so the first omitted term is below 1e-17.
    double sn = 1.0 / 355687428096000.0;
    sn = sn * -r2 + 1.0 / 1307674368000.0;
    sn = sn * -r2 + 1.0 / 6227020800.0;
    sn = sn * -r2 + 1.0 / 39916800.0;
    sn = sn * -r2 + 1.0 / 362880.0;
    sn = sn * -r2 + 1.0 / 5040.0;
    sn = sn * -r2 + 1.0 / 120.0;
    sn = sn * -r2 + 1.0 / 6.0;
    sn = r - r * r2 * sn;
    double cs = 1.0 / 20922789888000.0;
    cs = cs * -r2 + 1.0 / 87178291200.0;
    cs = cs * -r2 + 1.0 / 479001600.0;
    cs = cs * -r2 + 1.0 / 3628800.0;
    cs = cs * -r2 + 1.0 / 40320.0;
    cs = cs * -r2 + 1.0 / 720.0;
    cs = cs * -r2 + 1.0 / 24.0;
    cs = cs * -r2 + 0.5;
    cs = 1.0 - r2 * cs;
    switch (q) {
        case 0: *s = sn; *c = cs; break;
        case 1: *s = cs; *c = -sn; break;
        case 2: *s = -sn; *c = -cs; break;
        default: *s = -cs; *c = sn; break;
    }
}

// 2^x for |x| <= 1000, accurate to about 1e-15 relative.
inline double det_exp2(double x) {
    if (x < -1000.0) return 0.0;
    if (x > 1000.0) x = 1000.0;
    const double n = floor(x + 0.5);
    const double f = (x - n) * 0.69314718055994530942;  // ln 2; |f| <= 0.35
    // e^f by its Taylor series to f^16 (the next term is below 1e-20).
    double e = 1.0, term = 1.0;
    for (int k = 1; k <= 16; ++k) {
        term = term * f / k;
        e += term;
    }
    return ldexp(e, (int)n);
}

}  // namespace zaro

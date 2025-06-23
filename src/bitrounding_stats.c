#include "bitrounding_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#ifndef M_2_SQRTPI
#define M_2_SQRTPI 1.12837916709551257390
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

static int fp_is_equal(double a, double b) {
    return fabs(a - b) < DBL_EPSILON;
}

static double lngamma(double x) {
    const double cof[6] = { 76.18009172947146,  -86.50532032941677,    24.01409824083091,
                            -1.231739572450155, 0.1208650973866179e-2, -0.5395239384953e-5 };

    double b, a = b = x;
    double temp = a + 5.5;
    temp -= (a + 0.5) * log(temp);
    double ser = 1.000000000190015;
    for (int j = 0; j <= 5; ++j) ser += cof[j] / ++b;
    return -temp + log(2.5066282746310005 * ser / a);
}

static double gamma_help_1(double a, double x) {
    const double eps = 1.e-20;

    double gln = lngamma(a);
    double ap = a;
    double sum, del = sum = 1.0 / a;

    for (int i = 1; i <= 100; ++i) {
        ap++;
        del *= x / ap;
        sum += del;
        if (fabs(del) < fabs(sum) * eps) return sum * exp(-x + a * log(x) - (gln));
    }

    fprintf(stderr, "%s: internal error, too many iterations!\n", __func__);
    exit(1);
    return 0;
}

static double gamma_help_2(double a, double x) {
    const double eps = 1.e-20;
    const double very_small = 1000.0 * DBL_MIN;

    double gln = lngamma(a);
    double b = x + 1.0 - a;
    double c = 1.0 / very_small;
    double d = 1.0 / b;
    double h = d;

    for (int i = 1; i <= 100; ++i) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (fabs(d) < very_small) d = very_small;
        c = b + an / c;
        if (fabs(c) < very_small) c = very_small;
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (fabs(del - 1) < eps) return exp(-x + a * log(x) - gln) * h;
    }

    fprintf(stderr, "%s: internal error, too many iterations!\n", __func__);
    exit(1);
    return -1;
}

static double incomplete_gamma(double a, double x) {
    if (x < 0.0 || a <= 0.0) {
        fprintf(stderr, "%s: IMPLEMENTATION ERROR! (Invalid argument)\n", __func__);
        exit(4);
    }

    return (x < (a + 1.0)) ? gamma_help_1(a, x) : 1.0 - gamma_help_2(a, x);
}

double normal_density(double x) {
    return M_2_SQRTPI / 2.0 / M_SQRT2 * exp(-x * x / 2.0);
}

double normal(double x) {
    return (x > 0.0)   ? 0.5 * (1.0 + incomplete_gamma(0.5, x * x / 2.0))
           : (x < 0.0) ? 0.5 * (1.0 - incomplete_gamma(0.5, x * x / 2.0))
                       : 0.5;
}

static double normal_inv_Acklam(double p) {
    /* Acklam coefficients */
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                                4.374664141464968e+00,  2.938163982698783e+00};
    static const double d[] = { 7.784695709041462e-03,  3.224671290700398e-01,
                                2.445134137142996e+00,  3.754408661907416e+00};

    if (p <= 0.0) return -INFINITY;
    if (p >= 1.0) return  INFINITY;

    /* break-points */
    const double p_low  = 0.02425;
    const double p_high = 1.0 - p_low;

    /* lower region */
    if (p < p_low) {
        double q = sqrt(-2.0 * log(p));
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }

    /* upper region */
    if (p > p_high) {
        double q = sqrt(-2.0 * log(1.0 - p));
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
                 ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }

    /* central region */
    double q = p - 0.5;
    double r = q*q;
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q /
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1.0);
}

double normal_inv_original(double p) {
    const double eps = 1.e-10;
    static double last_p = 0.5, last_x = 0.0;

    if (p <= 0.0 || p >= 1.0) {
        fprintf(stderr, "%s: IMPLEMENTATION ERROR! (Invalid argument)\n", __func__);
        exit(4);
    }

    if (fp_is_equal(p, last_p)) return last_x;

    if (p < 0.5) return -normal_inv_original(1 - p);
    if (p > 0.5) {
        double x = 0.0;
        while (1) {
            double xx = x - (normal(x) - p) / normal_density(x);
            if (fabs(xx - x) < x * eps) break;
            x = xx;
        }
        last_p = p;
        last_x = x;
        return x;
    }

    return 0;
}

double normal_inv(double p) {
    return normal_inv_Acklam(p);
}

double binom_confidence(size_t n, double c) {
    double v = 1.0 - (1.0 - c) * 0.5;
    double p = 0.5 + normal_inv(v) / (2.0 * sqrt(n));
    return (p > 1.0) ? 1.0 : p;
}

static double entropy2(double p1, double p2) {
    double result = 0.0;
    if (p1 > 0.0) result -= p1 * log(p1);
    if (p2 > 0.0) result -= p2 * log(p2);
    return result / log(2.0);
}

double binom_free_entropy(size_t n, double c) {
    double p = binom_confidence(n, c);
    return 1.0 - entropy2(p, 1.0 - p);
}
/*
 * Hemlock Shared Math Core Implementation
 *
 * Pure C math functions shared between interpreter and runtime.
 * No dependencies on Value types - works with primitive C types only.
 */

#include "math_core.h"
#include <math.h>

/* ========== TRIGONOMETRIC FUNCTIONS ========== */

double hml_math_sin(double x) {
    return sin(x);
}

double hml_math_cos(double x) {
    return cos(x);
}

double hml_math_tan(double x) {
    return tan(x);
}

double hml_math_asin(double x) {
    return asin(x);
}

double hml_math_acos(double x) {
    return acos(x);
}

double hml_math_atan(double x) {
    return atan(x);
}

double hml_math_atan2(double y, double x) {
    return atan2(y, x);
}

/* ========== ROUNDING FUNCTIONS (f64) ========== */

double hml_math_floor(double x) {
    return floor(x);
}

double hml_math_ceil(double x) {
    return ceil(x);
}

double hml_math_round(double x) {
    return round(x);
}

double hml_math_trunc(double x) {
    return trunc(x);
}

/* ========== ROUNDING FUNCTIONS (i64) ========== */

int64_t hml_math_floori(double x) {
    return (int64_t)floor(x);
}

int64_t hml_math_ceili(double x) {
    return (int64_t)ceil(x);
}

int64_t hml_math_roundi(double x) {
    return (int64_t)round(x);
}

int64_t hml_math_trunci(double x) {
    return (int64_t)trunc(x);
}

/* ========== POWER AND EXPONENTIAL FUNCTIONS ========== */

double hml_math_sqrt(double x) {
    return sqrt(x);
}

double hml_math_pow(double base, double exp) {
    return pow(base, exp);
}

double hml_math_exp(double x) {
    return exp(x);
}

double hml_math_log(double x) {
    return log(x);
}

double hml_math_log10(double x) {
    return log10(x);
}

double hml_math_log2(double x) {
    return log2(x);
}

/* ========== MISC MATH FUNCTIONS ========== */

double hml_math_abs(double x) {
    return x < 0 ? -x : x;
}

double hml_math_min(double a, double b) {
    return a < b ? a : b;
}

double hml_math_max(double a, double b) {
    return a > b ? a : b;
}

double hml_math_clamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

double hml_math_fmod(double a, double b) {
    return fmod(a, b);
}

/* ========== DIVISION FUNCTIONS ========== */

double hml_math_div(double a, double b) {
    /* Caller must check for division by zero */
    return a / b;
}

int64_t hml_math_divi(double a, double b) {
    /* Caller must check for division by zero */
    return (int64_t)floor(a / b);
}

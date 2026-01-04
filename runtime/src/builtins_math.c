/*
 * Hemlock Runtime Library - Math Operations
 *
 * Mathematical functions: trigonometry, rounding, random, etc.
 */

#include "builtins_internal.h"

// Random seed state (shared with other modules)
int g_rand_seeded = 0;

// ========== CORE MATH FUNCTIONS ==========

HmlValue hml_sqrt(HmlValue x) {
    return hml_val_f64(sqrt(hml_to_f64(x)));
}

HmlValue hml_sin(HmlValue x) {
    return hml_val_f64(sin(hml_to_f64(x)));
}

HmlValue hml_cos(HmlValue x) {
    return hml_val_f64(cos(hml_to_f64(x)));
}

HmlValue hml_tan(HmlValue x) {
    return hml_val_f64(tan(hml_to_f64(x)));
}

HmlValue hml_asin(HmlValue x) {
    return hml_val_f64(asin(hml_to_f64(x)));
}

HmlValue hml_acos(HmlValue x) {
    return hml_val_f64(acos(hml_to_f64(x)));
}

HmlValue hml_atan(HmlValue x) {
    return hml_val_f64(atan(hml_to_f64(x)));
}

HmlValue hml_atan2(HmlValue y, HmlValue x) {
    return hml_val_f64(atan2(hml_to_f64(y), hml_to_f64(x)));
}

// ========== ROUNDING FUNCTIONS ==========

HmlValue hml_floor(HmlValue x) {
    return hml_val_f64(floor(hml_to_f64(x)));
}

HmlValue hml_ceil(HmlValue x) {
    return hml_val_f64(ceil(hml_to_f64(x)));
}

HmlValue hml_round(HmlValue x) {
    return hml_val_f64(round(hml_to_f64(x)));
}

HmlValue hml_trunc(HmlValue x) {
    return hml_val_f64(trunc(hml_to_f64(x)));
}

HmlValue hml_floori(HmlValue x) {
    return hml_val_i64((int64_t)floor(hml_to_f64(x)));
}

HmlValue hml_ceili(HmlValue x) {
    return hml_val_i64((int64_t)ceil(hml_to_f64(x)));
}

HmlValue hml_roundi(HmlValue x) {
    return hml_val_i64((int64_t)round(hml_to_f64(x)));
}

HmlValue hml_trunci(HmlValue x) {
    return hml_val_i64((int64_t)trunc(hml_to_f64(x)));
}

// ========== DIVISION FUNCTIONS ==========

HmlValue hml_div(HmlValue a, HmlValue b) {
    double ad = hml_to_f64(a);
    double bd = hml_to_f64(b);
    if (bd == 0.0) hml_runtime_error("Division by zero");
    return hml_val_f64(ad / bd);
}

HmlValue hml_divi(HmlValue a, HmlValue b) {
    double ad = hml_to_f64(a);
    double bd = hml_to_f64(b);
    if (bd == 0.0) hml_runtime_error("Division by zero");
    return hml_val_i64((int64_t)floor(ad / bd));
}

// ========== MISC MATH FUNCTIONS ==========

HmlValue hml_abs(HmlValue x) {
    double val = hml_to_f64(x);
    return hml_val_f64(val < 0 ? -val : val);
}

HmlValue hml_pow(HmlValue base, HmlValue exp) {
    return hml_val_f64(pow(hml_to_f64(base), hml_to_f64(exp)));
}

HmlValue hml_exp(HmlValue x) {
    return hml_val_f64(exp(hml_to_f64(x)));
}

HmlValue hml_log(HmlValue x) {
    return hml_val_f64(log(hml_to_f64(x)));
}

HmlValue hml_log10(HmlValue x) {
    return hml_val_f64(log10(hml_to_f64(x)));
}

HmlValue hml_log2(HmlValue x) {
    return hml_val_f64(log2(hml_to_f64(x)));
}

HmlValue hml_min(HmlValue a, HmlValue b) {
    double va = hml_to_f64(a);
    double vb = hml_to_f64(b);
    return hml_val_f64(va < vb ? va : vb);
}

HmlValue hml_max(HmlValue a, HmlValue b) {
    double va = hml_to_f64(a);
    double vb = hml_to_f64(b);
    return hml_val_f64(va > vb ? va : vb);
}

HmlValue hml_clamp(HmlValue x, HmlValue min_val, HmlValue max_val) {
    double v = hml_to_f64(x);
    double lo = hml_to_f64(min_val);
    double hi = hml_to_f64(max_val);
    if (v < lo) return hml_val_f64(lo);
    if (v > hi) return hml_val_f64(hi);
    return hml_val_f64(v);
}

// ========== RANDOM FUNCTIONS ==========

HmlValue hml_rand(void) {
    if (!g_rand_seeded) {
        srand((unsigned int)time(NULL));
        g_rand_seeded = 1;
    }
    return hml_val_f64((double)rand() / RAND_MAX);
}

HmlValue hml_rand_range(HmlValue min_val, HmlValue max_val) {
    if (!g_rand_seeded) {
        srand((unsigned int)time(NULL));
        g_rand_seeded = 1;
    }
    double lo = hml_to_f64(min_val);
    double hi = hml_to_f64(max_val);
    double r = (double)rand() / RAND_MAX;
    return hml_val_f64(lo + r * (hi - lo));
}

HmlValue hml_seed_val(HmlValue seed) {
    srand((unsigned int)hml_to_i32(seed));
    g_rand_seeded = 1;
    return hml_val_null();
}

void hml_seed(HmlValue seed) {
    srand((unsigned int)hml_to_i32(seed));
    g_rand_seeded = 1;
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_sin(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_sin(x);
}

HmlValue hml_builtin_cos(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_cos(x);
}

HmlValue hml_builtin_tan(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_tan(x);
}

HmlValue hml_builtin_asin(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_asin(x);
}

HmlValue hml_builtin_acos(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_acos(x);
}

HmlValue hml_builtin_atan(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_atan(x);
}

HmlValue hml_builtin_atan2(HmlClosureEnv *env, HmlValue y, HmlValue x) {
    (void)env;
    return hml_atan2(y, x);
}

HmlValue hml_builtin_sqrt(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_sqrt(x);
}

HmlValue hml_builtin_pow(HmlClosureEnv *env, HmlValue base, HmlValue exp) {
    (void)env;
    return hml_pow(base, exp);
}

HmlValue hml_builtin_exp(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_exp(x);
}

HmlValue hml_builtin_log(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log(x);
}

HmlValue hml_builtin_log10(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log10(x);
}

HmlValue hml_builtin_log2(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log2(x);
}

HmlValue hml_builtin_floor(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_floor(x);
}

HmlValue hml_builtin_ceil(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_ceil(x);
}

HmlValue hml_builtin_round(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_round(x);
}

HmlValue hml_builtin_trunc(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_trunc(x);
}

HmlValue hml_builtin_floori(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_floori(x);
}

HmlValue hml_builtin_ceili(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_ceili(x);
}

HmlValue hml_builtin_roundi(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_roundi(x);
}

HmlValue hml_builtin_trunci(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_trunci(x);
}

HmlValue hml_builtin_div(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_div(a, b);
}

HmlValue hml_builtin_divi(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_divi(a, b);
}

HmlValue hml_builtin_abs(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_abs(x);
}

HmlValue hml_builtin_min(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_min(a, b);
}

HmlValue hml_builtin_max(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_max(a, b);
}

HmlValue hml_builtin_clamp(HmlClosureEnv *env, HmlValue x, HmlValue lo, HmlValue hi) {
    (void)env;
    return hml_clamp(x, lo, hi);
}

HmlValue hml_builtin_rand(HmlClosureEnv *env) {
    (void)env;
    return hml_rand();
}

HmlValue hml_builtin_rand_range(HmlClosureEnv *env, HmlValue min_val, HmlValue max_val) {
    (void)env;
    return hml_rand_range(min_val, max_val);
}

HmlValue hml_builtin_seed(HmlClosureEnv *env, HmlValue seed) {
    (void)env;
    return hml_seed_val(seed);
}

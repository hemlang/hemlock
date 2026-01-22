/*
 * Hemlock Shared Math Core
 *
 * Pure C math functions shared between interpreter and runtime.
 * These functions work with primitive C types and have no dependencies
 * on the Value or HmlValue types.
 *
 * Both backends extract values from their respective value types,
 * call these functions, then wrap the result.
 */

#ifndef HEMLOCK_MATH_CORE_H
#define HEMLOCK_MATH_CORE_H

#include <stdint.h>

/* Trigonometric functions - return f64 */
double hml_math_sin(double x);
double hml_math_cos(double x);
double hml_math_tan(double x);
double hml_math_asin(double x);
double hml_math_acos(double x);
double hml_math_atan(double x);
double hml_math_atan2(double y, double x);

/* Rounding functions - return f64 */
double hml_math_floor(double x);
double hml_math_ceil(double x);
double hml_math_round(double x);
double hml_math_trunc(double x);

/* Rounding functions - return i64 */
int64_t hml_math_floori(double x);
int64_t hml_math_ceili(double x);
int64_t hml_math_roundi(double x);
int64_t hml_math_trunci(double x);

/* Power and exponential functions */
double hml_math_sqrt(double x);
double hml_math_pow(double base, double exp);
double hml_math_exp(double x);
double hml_math_log(double x);
double hml_math_log10(double x);
double hml_math_log2(double x);

/* Misc math functions */
double hml_math_abs(double x);
double hml_math_min(double a, double b);
double hml_math_max(double a, double b);
double hml_math_clamp(double x, double lo, double hi);
double hml_math_fmod(double a, double b);

/* Division functions
 * Note: These do NOT handle division by zero - caller must check
 */
double hml_math_div(double a, double b);
int64_t hml_math_divi(double a, double b);

#endif /* HEMLOCK_MATH_CORE_H */

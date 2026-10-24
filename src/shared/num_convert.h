/*
 * Hemlock Shared Numeric Conversions
 *
 * Float-to-integer conversion without undefined behavior. In C, converting
 * a floating value whose truncation does not fit the target type (or NaN)
 * is undefined, so every such conversion in both backends goes through
 * these checks. Callers decide what to do on failure; the convention is to
 * raise "Float value out of range for integer conversion to <type>", as the
 * explicit i32()/i64() conversions do.
 */

#ifndef HEMLOCK_SHARED_NUM_CONVERT_H
#define HEMLOCK_SHARED_NUM_CONVERT_H

#include <stdint.h>

/*
 * x truncated toward zero, if that fits in int64_t: exactly the doubles in
 * [-2^63, 2^63). NaN fails both comparisons. Returns 1 and stores the
 * result on success, 0 otherwise.
 */
static inline int hml_f64_to_i64(double x, int64_t *out) {
    if (!(x >= -0x1p63 && x < 0x1p63)) return 0;
    *out = (int64_t)x;
    return 1;
}

/* Same for int32_t: doubles in (-2^31 - 1, 2^31). */
static inline int hml_f64_to_i32(double x, int32_t *out) {
    if (!(x > -0x1p31 - 1.0 && x < 0x1p31)) return 0;
    *out = (int32_t)x;
    return 1;
}

/*
 * Saturating variants, for lenient conversions where an error is not
 * wanted (timeouts, durations): out-of-range values clamp to the type's
 * limits and NaN becomes 0. In-range values truncate as a plain cast would.
 */
static inline int64_t hml_f64_to_i64_sat(double x) {
    if (x != x) return 0;
    if (x >= 0x1p63) return INT64_MAX;
    if (x < -0x1p63) return INT64_MIN;
    return (int64_t)x;
}

static inline int32_t hml_f64_to_i32_sat(double x) {
    if (x != x) return 0;
    if (x >= 0x1p31) return INT32_MAX;
    if (x <= -0x1p31 - 1.0) return INT32_MIN;
    return (int32_t)x;
}

#endif /* HEMLOCK_SHARED_NUM_CONVERT_H */

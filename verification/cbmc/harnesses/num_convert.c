/*
 * PROOF: the float-to-integer helpers in src/shared/num_convert.h, which
 * both backends use wherever a float becomes an integer, are exact and free
 * of undefined behavior for every double (including NaN, +/-Inf, and the
 * values on either side of each boundary).
 *
 *   - checked:    succeed iff trunc(x) fits the type, and then return it
 *   - saturating: always defined; in range = trunc(x), NaN = 0, otherwise
 *                 the nearer limit
 *
 * Two runs (proofs.txt): all doubles for the functional asserts, and
 * --conversion-check (flags any out-of-range float-to-int conversion) with
 * -DSKIP_INT64_MIN. CBMC 6.4.1's check is off by one at exactly -2^63: it
 * rejects (int64_t)-0x1p63, a valid conversion (C11 6.3.1.4: the value is
 * representable, it is INT64_MIN). The functional run still covers it.
 */
#include "nondet.h"
#include <math.h>
#include "num_convert.h"

void harness(void) {
    double x = nondet_double();
#ifdef SKIP_INT64_MIN
    __CPROVER_assume(x != -0x1p63);
#endif
    double t = trunc(x);
    int fits64 = !isnan(x) && t >= -0x1p63 && t <= 0x1p63 - 1024.0;  /* largest double < 2^63 */
    int fits32 = !isnan(x) && t >= -0x1p31 && t <= 0x1p31 - 1.0;

    int64_t r64 = 0;
    int ok64 = hml_f64_to_i64(x, &r64);
    assert(ok64 == fits64);
    if (ok64) assert((double)r64 == t);

    int32_t r32 = 0;
    int ok32 = hml_f64_to_i32(x, &r32);
    assert(ok32 == fits32);
    if (ok32) assert((double)r32 == t);

    int64_t s64 = hml_f64_to_i64_sat(x);
    if (isnan(x)) assert(s64 == 0);
    else if (fits64) assert((double)s64 == t);
    else assert(s64 == (x > 0 ? INT64_MAX : INT64_MIN));

    int32_t s32 = hml_f64_to_i32_sat(x);
    if (isnan(x)) assert(s32 == 0);
    else if (fits32) assert((double)s32 == t);
    else assert(s32 == (x > 0 ? INT32_MAX : INT32_MIN));
}

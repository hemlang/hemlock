/*
 * Reference definitions of __builtin_{add,sub,mul}_overflow, force-included
 * (-include) into every CBMC proof that uses the Bitwuzla backend.
 *
 * Why: CBMC (6.4.1 and 6.8.0) flattens the {value, overflow} struct these
 * builtins produce into one bit-vector for solvers without datatypes, and
 * reads the two fields back from the wrong bit positions. Under --bitwuzla
 * that makes any formula containing them meaningless (it "proves" and
 * "refutes" arbitrary overflow facts; see README.md). Replacing them with
 * plain __int128 arithmetic sidesteps the bug, and also means the proofs no
 * longer take the builtin's semantics on trust: overflow is defined here as
 * "the exact result does not fit".
 *
 * _Generic dispatches on the result pointer type, which is how the runtime
 * and interpreter call them (int32_t * or int64_t *).
 */
#ifndef HML_CBMC_OVERFLOW_BUILTINS_H
#define HML_CBMC_OVERFLOW_BUILTINS_H

#include <stdint.h>

#define HML_CBMC_OVF(name, T, MIN, MAX, OP)                                    \
    static inline int name(T a, T b, T *r) {                                  \
        __int128 x = (__int128)a OP (__int128)b;                              \
        *r = (T)(uint64_t)x;                                                  \
        return x < (__int128)(MIN) || x > (__int128)(MAX);                    \
    }
HML_CBMC_OVF(hml_cbmc_add_ovf_i32, int32_t, INT32_MIN, INT32_MAX, +)
HML_CBMC_OVF(hml_cbmc_sub_ovf_i32, int32_t, INT32_MIN, INT32_MAX, -)
HML_CBMC_OVF(hml_cbmc_mul_ovf_i32, int32_t, INT32_MIN, INT32_MAX, *)
HML_CBMC_OVF(hml_cbmc_add_ovf_i64, int64_t, INT64_MIN, INT64_MAX, +)
HML_CBMC_OVF(hml_cbmc_sub_ovf_i64, int64_t, INT64_MIN, INT64_MAX, -)
HML_CBMC_OVF(hml_cbmc_mul_ovf_i64, int64_t, INT64_MIN, INT64_MAX, *)
#undef HML_CBMC_OVF

#define __builtin_add_overflow(a, b, r) \
    _Generic((r), int32_t *: hml_cbmc_add_ovf_i32, int64_t *: hml_cbmc_add_ovf_i64)((a), (b), (r))
#define __builtin_sub_overflow(a, b, r) \
    _Generic((r), int32_t *: hml_cbmc_sub_ovf_i32, int64_t *: hml_cbmc_sub_ovf_i64)((a), (b), (r))
#define __builtin_mul_overflow(a, b, r) \
    _Generic((r), int32_t *: hml_cbmc_mul_ovf_i32, int64_t *: hml_cbmc_mul_ovf_i64)((a), (b), (r))

#endif

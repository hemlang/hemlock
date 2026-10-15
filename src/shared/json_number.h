/*
 * JSON number scanning shared by the interpreter and the compiled runtime.
 *
 * Validates the RFC 8259 grammar  -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
 * and converts with strtoll/strtod, so values are exact (the old
 * digit-by-digit loops lost precision, wrapped int64 silently and could
 * spin ~2^31 times on a huge exponent). Integers that fit i32 are i32,
 * those that fit i64 are i64, and larger integers become f64.
 */
#ifndef HEMLOCK_JSON_NUMBER_H
#define HEMLOCK_JSON_NUMBER_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    HML_JSON_NUM_INVALID = 0,
    HML_JSON_NUM_I32,
    HML_JSON_NUM_I64,
    HML_JSON_NUM_F64
} HmlJsonNumKind;

// Longest number text converted from a stack buffer; longer ones are copied
// to the heap.
#define HML_JSON_NUM_STACK_BUF 64

static inline int hml_json_is_digit(char c) { return c >= '0' && c <= '9'; }

// Scan a JSON number at `s`. On success sets *consumed and one of
// *ival / *dval, and returns its kind; returns HML_JSON_NUM_INVALID if the
// text is not a valid JSON number.
static inline HmlJsonNumKind hml_json_scan_number(const char *s, size_t *consumed,
                                                  int64_t *ival, double *dval) {
    const char *p = s;
    int is_float = 0;

    if (*p == '-') p++;
    if (*p == '0') {
        p++;
    } else if (*p >= '1' && *p <= '9') {
        while (hml_json_is_digit(*p)) p++;
    } else {
        return HML_JSON_NUM_INVALID;
    }
    if (*p == '.') {
        p++;
        if (!hml_json_is_digit(*p)) return HML_JSON_NUM_INVALID;
        while (hml_json_is_digit(*p)) p++;
        is_float = 1;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!hml_json_is_digit(*p)) return HML_JSON_NUM_INVALID;
        while (hml_json_is_digit(*p)) p++;
        is_float = 1;
    }

    size_t len = (size_t)(p - s);
    *consumed = len;

    // Convert from an exact copy of the validated text (strtod alone would
    // also accept hex, inf, nan, ... past what JSON allows).
    char stack_buf[HML_JSON_NUM_STACK_BUF];
    char *buf = len < sizeof(stack_buf) ? stack_buf : (char *)malloc(len + 1);
    if (!buf) return HML_JSON_NUM_INVALID;
    memcpy(buf, s, len);
    buf[len] = '\0';

    HmlJsonNumKind kind;
    if (!is_float) {
        errno = 0;
        long long v = strtoll(buf, NULL, 10);
        if (errno == ERANGE) {
            *dval = strtod(buf, NULL);
            kind = HML_JSON_NUM_F64;
        } else {
            *ival = (int64_t)v;
            kind = (v >= INT32_MIN && v <= INT32_MAX) ? HML_JSON_NUM_I32 : HML_JSON_NUM_I64;
        }
    } else {
        *dval = strtod(buf, NULL);
        kind = HML_JSON_NUM_F64;
    }

    if (buf != stack_buf) free(buf);
    return kind;
}

#endif // HEMLOCK_JSON_NUMBER_H

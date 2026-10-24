/*
 * PROPERTY: hml_utf8_decode_next never advances the cursor past the NUL
 * terminator. A caller iterating `while (*p)` relies on this.
 */
#include "nondet.h"
#include "utf8.h"

#ifndef MAX_LEN
#define MAX_LEN 4
#endif

void harness(void) {
    int len = nondet_int();
    __CPROVER_assume(len >= 1 && len <= MAX_LEN);
    char *s = nondet_nul_terminated(len);

    const char *p = s;
    (void)hml_utf8_decode_next(&p);
    assert(p <= s + len);
    free(s);
}

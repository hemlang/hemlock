/*
 * PROPERTY: hml_utf8_validate accepts only well-formed UTF-8 (RFC 3629 /
 * Unicode Table 3-7): no overlong encodings, no surrogates (U+D800..DFFF),
 * nothing above U+10FFFF.
 *
 * Checked by decoding every sequence of an accepted buffer and requiring the
 * decoded scalar to be in range and minimally encoded.
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
    __CPROVER_assume(hml_utf8_validate(s, len));

    int pos = 0;
    while (pos < len) {
        int n;
        uint32_t cp = hml_utf8_decode(s + pos, &n);
        assert(cp <= 0x10FFFF);                  /* in Unicode range */
        assert(cp < 0xD800 || cp > 0xDFFF);      /* not a surrogate */
        assert(hml_utf8_encode_len(cp) == n);    /* not overlong */
        pos += n;
    }
    free(s);
}

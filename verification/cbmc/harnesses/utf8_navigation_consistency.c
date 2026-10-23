/*
 * PROPERTY: the two ways the runtime walks a string agree.
 *
 *   index-based:    .length / char_at / substr use hml_utf8_count_codepoints
 *                   and hml_utf8_byte_offset (count non-continuation bytes)
 *   iterator-based: chars() / for-in use hml_utf8_decode and advance by
 *                   bytes_read
 *
 * The comments in src/shared/utf8.c claim these match "even for malformed
 * input". Two variants:
 *   -DASSUME_VALID   restrict to hml_utf8_validate()-accepted strings
 *   (default)        arbitrary bytes, i.e. what from_bytes() can produce
 */
#include "nondet.h"
#include "utf8.h"

#ifndef MAX_LEN
#define MAX_LEN 5
#endif

void harness(void) {
    int len = nondet_int();
    __CPROVER_assume(len >= 0 && len <= MAX_LEN);
    char *s = nondet_nul_terminated(len);
#ifdef ASSUME_VALID
    __CPROVER_assume(hml_utf8_validate(s, len));
#endif

    int count = hml_utf8_count_codepoints(s, len);

    int pos = 0, steps = 0;
    while (pos < len) {
        /* Iterator step i must land where the index API says codepoint i is,
         * and decode the same codepoint that char_at(i) would. */
        assert(hml_utf8_byte_offset(s, len, steps) == pos);
        int bytes_read;
        uint32_t cp = hml_utf8_decode(s + pos, &bytes_read);
        assert(cp == hml_utf8_decode_at(s, pos));
        pos += bytes_read;
        steps++;
    }
    assert(steps == count);
    free(s);
}

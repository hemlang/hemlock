/*
 * PROOF: the two ways the runtime walks a string agree.
 *
 *   index-based:    .length / s[i] / char_at / substr use
 *                   hml_utf8_count_codepoints and hml_utf8_byte_offset
 *   iterator-based: chars() / for-in start at hml_utf8_byte_offset(.., 0) and
 *                   step with hml_utf8_next_start; hml_utf8_decode's
 *                   bytes_read must give the same step
 *
 * on arbitrary bytes (what from_bytes() can produce), or with -DASSUME_VALID
 * on hml_utf8_validate()-accepted strings only.
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

    int pos = hml_utf8_byte_offset(s, len, 0), steps = 0;
    while (pos < len) {
        /* Iterator step i must land where the index API says codepoint i is,
         * and decode the same codepoint that char_at(i) would. */
        assert(hml_utf8_byte_offset(s, len, steps) == pos);
        int bytes_read;
        uint32_t cp = hml_utf8_decode(s + pos, &bytes_read);
        assert(cp == hml_utf8_decode_at(s, pos));
        int next = hml_utf8_next_start(s, len, pos);
        assert(pos + bytes_read == next);
        pos = next;
        steps++;
    }
    assert(steps == count);
    free(s);
}

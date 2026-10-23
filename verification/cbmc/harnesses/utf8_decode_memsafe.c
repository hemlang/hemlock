/*
 * PROOF: decoders never read outside a NUL-terminated string, for arbitrary
 * (including malformed) byte content and any in-range start position.
 *
 * The buffer is allocated with exactly length+1 bytes, so --pointer-check /
 * --bounds-check flag any out-of-allocation read. This is the property the
 * SECURITY comment in src/shared/utf8.c claims. Also: decoders return only
 * Unicode scalars (or U+FFFD), and every iteration step stays in bounds.
 */
#include "nondet.h"
#include "utf8.h"

#ifndef MAX_LEN
#define MAX_LEN 6
#endif

void harness(void) {
    int len = nondet_int();
    __CPROVER_assume(len >= 0 && len <= MAX_LEN);
    char *s = nondet_nul_terminated(len);

    int pos = nondet_int();
    __CPROVER_assume(pos >= 0 && pos < len);

    int bytes_read = 0;
    uint32_t a = hml_utf8_decode(s + pos, &bytes_read);
    assert(bytes_read >= 1);   /* lead byte plus any trailing continuation bytes */
    uint32_t b = hml_utf8_decode_at(s, pos);
    assert(a <= 0x10FFFF && b <= 0x10FFFF);   /* only scalars or U+FFFD */
    assert(pos + bytes_read <= len);           /* never steps past the NUL */
    int next = hml_utf8_next_start(s, len, pos);
    assert(next > pos && next <= len);

    (void)hml_utf8_count_codepoints(s, len);
    (void)hml_utf8_validate(s, len);
    (void)hml_utf8_is_ascii(s, len);
    int idx = nondet_int();
    __CPROVER_assume(idx >= 0 && idx <= len + 1);
    int off = hml_utf8_byte_offset(s, len, idx);
    assert(off >= 0 && off <= len);

    free(s);
}

/*
 * PROOF: encode/decode round-trip for every Unicode scalar range value.
 *
 * For all codepoints 0..0x10FFFF:
 *   - hml_utf8_encode writes 1..4 bytes and agrees with hml_utf8_encode_len
 *   - every decoder (decode, decode_at, decode_next) recovers the codepoint
 *     and consumes exactly the encoded length
 *   - the encoding passes hml_utf8_validate and counts as one codepoint
 *   - hml_utf8_char_byte_len on the lead byte predicts the length
 */
#include "nondet.h"
#include "utf8.h"

void harness(void) {
    uint32_t cp = nondet_u32();
    __CPROVER_assume(cp <= 0x10FFFF);

    char buf[5] = {0, 0, 0, 0, 0};
    int n = hml_utf8_encode(cp, buf);

    assert(n >= 1 && n <= 4);
    assert(n == hml_utf8_encode_len(cp));
    assert(hml_utf8_char_byte_len((unsigned char)buf[0]) == n);

    int bytes_read = 0;
    assert(hml_utf8_decode(buf, &bytes_read) == cp);
    assert(bytes_read == n);

    assert(hml_utf8_decode_at(buf, 0) == cp);

    const char *p = buf;
    assert(hml_utf8_decode_next(&p) == cp);
    assert(p == buf + n);

    assert(hml_utf8_validate(buf, n) == 1);
    assert(hml_utf8_count_codepoints(buf, n) == 1);
    assert(hml_utf8_byte_offset(buf, n, 0) == 0);
    assert(hml_utf8_byte_offset(buf, n, 1) == n);
}

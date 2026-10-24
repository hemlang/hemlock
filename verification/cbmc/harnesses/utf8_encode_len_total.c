/*
 * PROPERTY: hml_utf8_encode_len(cp) == bytes written by hml_utf8_encode(cp)
 * for EVERY uint32_t, not just valid scalars. Callers size buffers with
 * encode_len and then call encode, so a mismatch is a length-bookkeeping bug.
 */
#include "nondet.h"
#include "utf8.h"

void harness(void) {
    uint32_t cp = nondet_u32();
    char buf[4];
    int n = hml_utf8_encode(cp, buf);
    assert(n == hml_utf8_encode_len(cp));
}

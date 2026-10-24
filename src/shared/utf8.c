/*
 * Hemlock Shared UTF-8 Utilities - Implementation
 *
 * Pure UTF-8 encoding/decoding functions shared between
 * the interpreter and compiled program runtime.
 */

#include "utf8.h"

/* ========== COUNTING AND NAVIGATION ========== */

int hml_utf8_count_codepoints(const char *data, int byte_len) {
    int count = 0;
    for (int i = 0; i < byte_len; i++) {
        // Count bytes that are NOT continuation bytes (10xxxxxx)
        if ((data[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

int hml_utf8_byte_offset(const char *data, int byte_len, int char_idx) {
    int pos = 0;
    int codepoint_count = 0;

    while (pos < byte_len) {
        unsigned char byte = (unsigned char)data[pos];

        // If this is a start byte (not a continuation byte)
        if ((byte & 0xC0) != 0x80) {
            // Have we reached the target codepoint?
            if (codepoint_count == char_idx) {
                return pos;  // Return position of start of this codepoint
            }
            codepoint_count++;
        }

        pos++;
    }

    return pos;  // Reached end of string
}

int hml_utf8_next_start(const char *data, int byte_len, int pos) {
    pos++;
    while (pos < byte_len && ((unsigned char)data[pos] & 0xC0) == 0x80) {
        pos++;
    }
    return pos;
}

/* ========== BYTE LENGTH ========== */

int hml_utf8_char_byte_len(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;      // 0xxxxxxx - ASCII
    if ((first_byte & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;   // 11110xxx
    return 1;  // Invalid start byte, treat as single byte
}

int hml_utf8_encode_len(uint32_t codepoint) {
    if (codepoint < 0x80) return 1;
    if (codepoint < 0x800) return 2;
    if (codepoint < 0x10000) return 3;
    if (codepoint <= 0x10FFFF) return 4;
    return 3;  // hml_utf8_encode writes U+FFFD for out-of-range codepoints
}

/* ========== DECODING ========== */

/*
 * SECURITY: These decoders must never read past a continuation byte that is
 * not a valid 10xxxxxx byte. Hemlock strings are always NUL-terminated
 * (allocated as length+1), and the NUL terminator is not a valid continuation
 * byte, so validating each continuation byte BEFORE reading the next one
 * guarantees we never read beyond the string's allocation when the trailing
 * bytes form a truncated/malformed multibyte sequence (e.g. produced by
 * from_bytes() on arbitrary input). On any malformed sequence we stop and
 * return U+FFFD.
 */
#define HML_UTF8_IS_CONT(b) (((b) & 0xC0) == 0x80)

/*
 * A codepoint is a non-continuation byte plus every continuation byte after
 * it (the same rule hml_utf8_count_codepoints and hml_utf8_byte_offset
 * use), so iterating decoders advance to the next non-continuation byte.
 * The NUL terminator is not a continuation byte, so this never passes it.
 */
static int hml_utf8_extent(const char *data) {
    int n = 1;
    while (HML_UTF8_IS_CONT((unsigned char)data[n])) n++;
    return n;
}

/* 4-byte sequences can encode up to 0x1FFFFF; only scalars are returned. */
static uint32_t hml_utf8_in_range(uint32_t cp) {
    return cp <= 0x10FFFF ? cp : 0xFFFD;
}

uint32_t hml_utf8_decode_at(const char *data, int byte_pos) {
    unsigned char b1 = (unsigned char)data[byte_pos];

    // 1-byte (ASCII): 0xxxxxxx
    if ((b1 & 0x80) == 0) {
        return b1;
    }

    // 2-byte: 110xxxxx 10xxxxxx
    if ((b1 & 0xE0) == 0xC0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        if (!HML_UTF8_IS_CONT(b2)) return 0xFFFD;
        return ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    }

    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF0) == 0xE0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        if (!HML_UTF8_IS_CONT(b2)) return 0xFFFD;
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        if (!HML_UTF8_IS_CONT(b3)) return 0xFFFD;
        return ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }

    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF8) == 0xF0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        if (!HML_UTF8_IS_CONT(b2)) return 0xFFFD;
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        if (!HML_UTF8_IS_CONT(b3)) return 0xFFFD;
        unsigned char b4 = (unsigned char)data[byte_pos + 3];
        if (!HML_UTF8_IS_CONT(b4)) return 0xFFFD;
        return hml_utf8_in_range(((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) |
                                 ((b3 & 0x3F) << 6) | (b4 & 0x3F));
    }

    // Invalid sequence, return replacement character
    return 0xFFFD;
}

uint32_t hml_utf8_decode_next(const char **data_ptr) {
    const char *data = *data_ptr;
    *data_ptr += hml_utf8_extent(data);
    return hml_utf8_decode_at(data, 0);
}

uint32_t hml_utf8_decode(const char *data, int *bytes_read) {
    *bytes_read = hml_utf8_extent(data);
    return hml_utf8_decode_at(data, 0);
}

/* ========== ENCODING ========== */

int hml_utf8_encode(uint32_t codepoint, char *buffer) {
    if (codepoint <= 0x7F) {
        // 1 byte: 0xxxxxxx
        buffer[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        // 2 bytes: 110xxxxx 10xxxxxx
        buffer[0] = (char)(0xC0 | (codepoint >> 6));
        buffer[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint <= 0xFFFF) {
        // 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
        buffer[0] = (char)(0xE0 | (codepoint >> 12));
        buffer[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        // 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        buffer[0] = (char)(0xF0 | (codepoint >> 18));
        buffer[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buffer[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    } else {
        // Invalid codepoint, encode replacement character
        buffer[0] = (char)0xEF;
        buffer[1] = (char)0xBF;
        buffer[2] = (char)0xBD;
        return 3;
    }
}

/* ========== VALIDATION ========== */

int hml_utf8_validate(const char *data, int byte_len) {
    // Well-formed UTF-8 per RFC 3629 / Unicode Table 3-7: no overlong
    // encodings, no surrogates (U+D800..DFFF), nothing above U+10FFFF.
    int pos = 0;

    while (pos < byte_len) {
        unsigned char b = (unsigned char)data[pos];
        int char_len;
        unsigned char lo = 0x80, hi = 0xBF;  // allowed range of the 2nd byte

        if (b < 0x80) {
            pos++;
            continue;
        } else if (b >= 0xC2 && b <= 0xDF) {
            char_len = 2;
        } else if (b >= 0xE0 && b <= 0xEF) {
            char_len = 3;
            if (b == 0xE0) lo = 0xA0;        // overlong
            if (b == 0xED) hi = 0x9F;        // surrogates
        } else if (b >= 0xF0 && b <= 0xF4) {
            char_len = 4;
            if (b == 0xF0) lo = 0x90;        // overlong
            if (b == 0xF4) hi = 0x8F;        // > U+10FFFF
        } else {
            return 0;  // C0, C1, F5..FF, or a stray continuation byte
        }

        if (pos + char_len > byte_len) return 0;  // Truncated sequence
        unsigned char b2 = (unsigned char)data[pos + 1];
        if (b2 < lo || b2 > hi) return 0;
        for (int i = 2; i < char_len; i++) {
            if (!HML_UTF8_IS_CONT((unsigned char)data[pos + i])) return 0;
        }

        pos += char_len;
    }

    return 1;  // Valid UTF-8
}

int hml_utf8_is_ascii(const char *data, int byte_len) {
    for (int i = 0; i < byte_len; i++) {
        if ((unsigned char)data[i] & 0x80) {
            return 0;  // Non-ASCII byte found
        }
    }
    return 1;  // All ASCII
}

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
    return 4;
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
        return ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    }

    // Invalid sequence, return replacement character
    return 0xFFFD;
}

uint32_t hml_utf8_decode_next(const char **data_ptr) {
    const char *data = *data_ptr;
    unsigned char b1 = (unsigned char)*data;
    int len = hml_utf8_char_byte_len(b1);
    // Advance by the lead byte's nominal length (1-4) so iteration matches
    // hml_utf8_count_codepoints / hml_utf8_byte_offset navigation on both
    // backends, even for malformed input. Continuation bytes are validated
    // before being read, so a truncated tail returns U+FFFD instead of reading
    // past the (always NUL-terminated) buffer.
    *data_ptr += len;

    if (len == 1) {
        return (b1 < 0x80) ? b1 : 0xFFFD;  // invalid lead byte -> U+FFFD
    } else if (len == 2) {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        return ((b1 & 0x1F) << 6) | (data[1] & 0x3F);
    } else if (len == 3) {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[2])) return 0xFFFD;
        return ((b1 & 0x0F) << 12) | ((data[1] & 0x3F) << 6) | (data[2] & 0x3F);
    } else {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[2])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[3])) return 0xFFFD;
        return ((b1 & 0x07) << 18) | ((data[1] & 0x3F) << 12) |
               ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
    }
}

uint32_t hml_utf8_decode(const char *data, int *bytes_read) {
    unsigned char c = (unsigned char)data[0];
    int len = hml_utf8_char_byte_len(c);
    // Report the lead byte's nominal length (1-4, always >= 1 so callers make
    // progress) so iteration matches hml_utf8_count_codepoints navigation on
    // both backends, even for malformed input. Continuation bytes are validated
    // before being read, so a truncated tail returns U+FFFD rather than reading
    // past the (always NUL-terminated) buffer.
    *bytes_read = len;

    if (len == 1) {
        return (c < 0x80) ? c : 0xFFFD;  // invalid lead byte -> U+FFFD
    } else if (len == 2) {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        return ((c & 0x1F) << 6) | (data[1] & 0x3F);
    } else if (len == 3) {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[2])) return 0xFFFD;
        return ((c & 0x0F) << 12) | ((data[1] & 0x3F) << 6) | (data[2] & 0x3F);
    } else {
        if (!HML_UTF8_IS_CONT((unsigned char)data[1])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[2])) return 0xFFFD;
        if (!HML_UTF8_IS_CONT((unsigned char)data[3])) return 0xFFFD;
        return ((c & 0x07) << 18) | ((data[1] & 0x3F) << 12) |
               ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
    }
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
    int pos = 0;

    while (pos < byte_len) {
        unsigned char b = (unsigned char)data[pos];
        int char_len;

        if ((b & 0x80) == 0) {
            // 1-byte character (ASCII)
            char_len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            // 2-byte character
            char_len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            // 3-byte character
            char_len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            // 4-byte character
            char_len = 4;
        } else {
            return 0;  // Invalid start byte
        }

        // Check continuation bytes
        for (int i = 1; i < char_len; i++) {
            if (pos + i >= byte_len) return 0;  // Truncated sequence
            unsigned char cont = (unsigned char)data[pos + i];
            if ((cont & 0xC0) != 0x80) return 0;  // Invalid continuation byte
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

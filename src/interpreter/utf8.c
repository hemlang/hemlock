#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>

// ========== UTF-8 UTILITY IMPLEMENTATIONS ==========

// Count number of Unicode codepoints in a UTF-8 string
int utf8_count_codepoints(const char *data, int byte_length) {
    int count = 0;
    int pos = 0;

    while (pos < byte_length) {
        unsigned char byte = (unsigned char)data[pos];

        // Skip continuation bytes (10xxxxxx)
        if ((byte & 0xC0) != 0x80) {
            count++;
        }
        pos++;
    }

    return count;
}

// Find byte offset of the i-th codepoint (0-indexed)
int utf8_byte_offset(const char *data, int byte_length, int char_index) {
    int pos = 0;
    int codepoint_count = 0;

    while (pos < byte_length) {
        unsigned char byte = (unsigned char)data[pos];

        // If this is a start byte (not a continuation byte)
        if ((byte & 0xC0) != 0x80) {
            // Have we reached the target codepoint?
            if (codepoint_count == char_index) {
                return pos;  // Return position of start of this codepoint
            }
            codepoint_count++;
        }

        pos++;
    }

    return pos;  // Reached end of string
}

// Get the byte length of a UTF-8 character from its first byte
int utf8_char_byte_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;      // 0xxxxxxx
    if ((first_byte & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;   // 11110xxx

    fprintf(stderr, "Runtime error: Invalid UTF-8 start byte: 0x%02X\n", first_byte);
    exit(1);
}

// Decode UTF-8 codepoint at given byte position
uint32_t utf8_decode_at(const char *data, int byte_pos) {
    unsigned char b1 = (unsigned char)data[byte_pos];

    // 1-byte (ASCII): 0xxxxxxx
    if ((b1 & 0x80) == 0) {
        return b1;
    }

    // 2-byte: 110xxxxx 10xxxxxx
    if ((b1 & 0xE0) == 0xC0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        return ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    }

    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF0) == 0xE0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        return ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }

    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF8) == 0xF0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        unsigned char b4 = (unsigned char)data[byte_pos + 3];
        return ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    }

    fprintf(stderr, "Runtime error: Invalid UTF-8 sequence at byte %d\n", byte_pos);
    exit(1);
}

// Decode next UTF-8 codepoint and advance pointer
uint32_t utf8_decode_next(const char **data_ptr) {
    const char *data = *data_ptr;
    unsigned char b1 = (unsigned char)*data;

    // 1-byte (ASCII): 0xxxxxxx
    if ((b1 & 0x80) == 0) {
        *data_ptr += 1;
        return b1;
    }

    // 2-byte: 110xxxxx 10xxxxxx
    if ((b1 & 0xE0) == 0xC0) {
        unsigned char b2 = (unsigned char)data[1];
        *data_ptr += 2;
        return ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    }

    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF0) == 0xE0) {
        unsigned char b2 = (unsigned char)data[1];
        unsigned char b3 = (unsigned char)data[2];
        *data_ptr += 3;
        return ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }

    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((b1 & 0xF8) == 0xF0) {
        unsigned char b2 = (unsigned char)data[1];
        unsigned char b3 = (unsigned char)data[2];
        unsigned char b4 = (unsigned char)data[3];
        *data_ptr += 4;
        return ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    }

    fprintf(stderr, "Runtime error: Invalid UTF-8 sequence\n");
    exit(1);
}

// Encode a Unicode codepoint to UTF-8
int utf8_encode(uint32_t codepoint, char *buffer) {
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
        fprintf(stderr, "Runtime error: Invalid Unicode codepoint: 0x%X\n", codepoint);
        exit(1);
    }
}

// Validate that a buffer contains valid UTF-8
int utf8_validate(const char *data, int byte_length) {
    int pos = 0;

    while (pos < byte_length) {
        unsigned char b = (unsigned char)data[pos];
        int char_len;

        if ((b & 0x80) == 0) {
            // 1-byte character
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
            if (pos + i >= byte_length) return 0;  // Truncated sequence
            unsigned char cont = (unsigned char)data[pos + i];
            if ((cont & 0xC0) != 0x80) return 0;  // Invalid continuation byte
        }

        pos += char_len;
    }

    return 1;  // Valid UTF-8
}

// Check if string contains only ASCII characters
int utf8_is_ascii(const char *data, int byte_length) {
    for (int i = 0; i < byte_length; i++) {
        if ((unsigned char)data[i] & 0x80) {
            return 0;  // Non-ASCII byte found
        }
    }
    return 1;  // All ASCII
}

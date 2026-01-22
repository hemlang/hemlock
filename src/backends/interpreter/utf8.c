/*
 * Hemlock Interpreter UTF-8 Utilities
 *
 * Thin wrappers around the shared UTF-8 module.
 * These functions maintain the interpreter's API while
 * delegating to the shared implementation.
 */

#include "utf8.h"
#include "shared/utf8.h"

// ========== UTF-8 UTILITY WRAPPERS ==========

int utf8_count_codepoints(const char *data, int byte_length) {
    return hml_utf8_count_codepoints(data, byte_length);
}

int utf8_byte_offset(const char *data, int byte_length, int char_index) {
    return hml_utf8_byte_offset(data, byte_length, char_index);
}

int utf8_char_byte_length(unsigned char first_byte) {
    return hml_utf8_char_byte_len(first_byte);
}

uint32_t utf8_decode_at(const char *data, int byte_pos) {
    return hml_utf8_decode_at(data, byte_pos);
}

uint32_t utf8_decode_next(const char **data_ptr) {
    return hml_utf8_decode_next(data_ptr);
}

int utf8_encode(uint32_t codepoint, char *buffer) {
    return hml_utf8_encode(codepoint, buffer);
}

int utf8_validate(const char *data, int byte_length) {
    return hml_utf8_validate(data, byte_length);
}

int utf8_is_ascii(const char *data, int byte_length) {
    return hml_utf8_is_ascii(data, byte_length);
}

#ifndef HEMLOCK_UTF8_H
#define HEMLOCK_UTF8_H

#include <stdint.h>

// ========== UTF-8 UTILITY FUNCTIONS ==========

// Count number of Unicode codepoints in a UTF-8 string
int utf8_count_codepoints(const char *data, int byte_length);

// Find byte offset of the i-th codepoint (0-indexed)
// Returns the byte position where the i-th codepoint starts
int utf8_byte_offset(const char *data, int byte_length, int char_index);

// Decode UTF-8 codepoint at given byte position
// Returns the Unicode codepoint value
uint32_t utf8_decode_at(const char *data, int byte_pos);

// Decode next UTF-8 codepoint from current position
// Advances the position pointer past the decoded character
// Returns the Unicode codepoint value
uint32_t utf8_decode_next(const char **data_ptr);

// Encode a Unicode codepoint to UTF-8
// Writes the UTF-8 bytes to buffer and returns number of bytes written (1-4)
int utf8_encode(uint32_t codepoint, char *buffer);

// Get the byte length of a UTF-8 character from its first byte
int utf8_char_byte_length(unsigned char first_byte);

// Validate that a buffer contains valid UTF-8
// Returns 1 if valid, 0 if invalid
int utf8_validate(const char *data, int byte_length);

// Check if string contains only ASCII characters (fast path optimization)
int utf8_is_ascii(const char *data, int byte_length);

#endif // HEMLOCK_UTF8_H

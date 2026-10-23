/*
 * Hemlock Shared UTF-8 Utilities
 *
 * Pure UTF-8 encoding/decoding functions shared between
 * the interpreter and compiled program runtime.
 *
 * All functions are prefixed with hml_utf8_ to avoid
 * conflicts with other UTF-8 implementations.
 */

#ifndef HEMLOCK_SHARED_UTF8_H
#define HEMLOCK_SHARED_UTF8_H

#include <stdint.h>

/*
 * Count number of Unicode codepoints in a UTF-8 string.
 * This counts characters, not bytes.
 *
 * @param data       UTF-8 encoded string
 * @param byte_len   Length in bytes
 * @return Number of codepoints (characters)
 */
int hml_utf8_count_codepoints(const char *data, int byte_len);

/*
 * Find byte offset of the i-th codepoint (0-indexed).
 * Returns the byte position where the i-th codepoint starts.
 *
 * @param data       UTF-8 encoded string
 * @param byte_len   Length in bytes
 * @param char_idx   Character index (0-based)
 * @return Byte offset of the character, or byte_len if past end
 */
int hml_utf8_byte_offset(const char *data, int byte_len, int char_idx);

/*
 * Byte offset of the codepoint after the one starting at `pos`: the next
 * non-continuation byte, or byte_len. This is the iteration step that agrees
 * with hml_utf8_count_codepoints / hml_utf8_byte_offset on any input,
 * including malformed bytes. Start iteration at hml_utf8_byte_offset(.., 0).
 *
 * @param data       UTF-8 encoded string
 * @param byte_len   Length in bytes
 * @param pos        Byte offset of the current codepoint
 * @return Byte offset of the next codepoint (<= byte_len)
 */
int hml_utf8_next_start(const char *data, int byte_len, int pos);

/*
 * Get the byte length of a UTF-8 character from its first byte.
 *
 * @param first_byte The first byte of a UTF-8 sequence
 * @return Byte length (1-4), or 1 for invalid bytes
 */
int hml_utf8_char_byte_len(unsigned char first_byte);

/*
 * Decode UTF-8 codepoint at given byte position.
 * Malformed or out-of-range (> U+10FFFF) sequences decode to U+FFFD.
 *
 * @param data     UTF-8 encoded string
 * @param byte_pos Byte position to decode from
 * @return Unicode codepoint value
 */
uint32_t hml_utf8_decode_at(const char *data, int byte_pos);

/*
 * Decode next UTF-8 codepoint and advance pointer past it and any
 * continuation bytes that follow (never past the NUL terminator).
 *
 * @param data_ptr Pointer to string pointer (advanced on return)
 * @return Unicode codepoint value
 */
uint32_t hml_utf8_decode_next(const char **data_ptr);

/*
 * Decode UTF-8 codepoint and report bytes read.
 * Useful when iterating through a string.
 *
 * @param data       Pointer to UTF-8 encoded data
 * @param bytes_read Output: bytes up to the next non-continuation byte
 *                   (>= 1; consistent with hml_utf8_next_start)
 * @return Unicode codepoint value
 */
uint32_t hml_utf8_decode(const char *data, int *bytes_read);

/*
 * Encode a Unicode codepoint to UTF-8.
 *
 * @param codepoint Unicode codepoint (0 to 0x10FFFF)
 * @param buffer    Output buffer (must have room for 4 bytes)
 * @return Number of bytes written (1-4)
 */
int hml_utf8_encode(uint32_t codepoint, char *buffer);

/*
 * Get the byte length needed to encode a codepoint as UTF-8.
 *
 * @param codepoint Unicode codepoint
 * @return Byte length (1-4)
 */
int hml_utf8_encode_len(uint32_t codepoint);

/*
 * Validate that a buffer contains well-formed UTF-8 (RFC 3629): rejects
 * overlong encodings, surrogates and codepoints above U+10FFFF.
 *
 * @param data     Buffer to validate
 * @param byte_len Length in bytes
 * @return 1 if valid, 0 if invalid
 */
int hml_utf8_validate(const char *data, int byte_len);

/*
 * Check if string contains only ASCII characters.
 * This is a fast-path optimization for strings that don't
 * need full UTF-8 processing.
 *
 * @param data     String to check
 * @param byte_len Length in bytes
 * @return 1 if all ASCII, 0 if contains non-ASCII
 */
int hml_utf8_is_ascii(const char *data, int byte_len);

#endif /* HEMLOCK_SHARED_UTF8_H */

/*
 * Hemlock Runtime - String Methods
 *
 * String manipulation operations including:
 * - length, char_at, byte_at, substr, slice
 * - find, contains, split, trim
 * - to_upper, to_lower, starts_with, ends_with
 * - replace, replace_all, repeat
 * - concat3, concat4, concat5, concat_many
 * - UTF-8 operations (chars, bytes, rune_at, char_count)
 * - Buffer operations
 *
 * Uses shared UTF-8 module for encoding/decoding.
 */

#include "builtins_internal.h"
#include "utf8.h"

// ========== STRING METHODS ==========

HmlValue hml_string_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    HmlString *s = str.as.as_string;
    // Return character (codepoint) count, not byte count
    return hml_val_i32(hml_utf8_count_codepoints(s->data, s->length));
}

HmlValue hml_string_byte_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    return hml_val_i32(str.as.as_string->length);
}

// Compute (and cache) the codepoint length of a string
static int hml_string_codepoint_length(HmlString *s) {
    if (s->char_length < 0) {
        s->char_length = hml_utf8_count_codepoints(s->data, s->length);
    }
    return s->char_length;
}

HmlValue hml_string_char_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);

    int char_len = hml_string_codepoint_length(s);

    if (idx < 0 || idx >= char_len) {
        hml_runtime_error("char_at() index %d out of bounds (length=%d)", idx, char_len);
    }

    // Find byte offset of the character and decode the codepoint
    int byte_pos = hml_utf8_byte_offset(s->data, s->length, idx);
    uint32_t codepoint = hml_utf8_decode_at(s->data, byte_pos);

    return hml_val_rune(codepoint);
}

HmlValue hml_string_byte_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    if (idx < 0 || idx >= s->length) {
        hml_runtime_error("byte_at() index %d out of bounds (byte_length=%d)", idx, s->length);
    }
    return hml_val_u8((uint8_t)s->data[idx]);
}

// substr/slice operate on codepoint positions, not bytes (mirrors the
// interpreter). Out-of-range positions are clamped.
HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int char_len = hml_string_codepoint_length(s);
    int32_t start_idx = hml_to_i32(start);
    int32_t len = hml_to_i32(length);

    // Clamp bounds (codepoint positions)
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= char_len) return hml_val_string("");
    if (len < 0) len = 0;
    if (len > char_len - start_idx) len = char_len - start_idx;

    int start_byte = hml_utf8_byte_offset(s->data, s->length, start_idx);
    int end_byte = hml_utf8_byte_offset(s->data, s->length, start_idx + len);
    int byte_len = end_byte - start_byte;

    char *result = malloc(byte_len + 1);
    if (!result) hml_runtime_error("substr() out of memory");
    memcpy(result, s->data + start_byte, byte_len);
    result[byte_len] = '\0';
    return hml_val_string_owned(result, byte_len, byte_len + 1);
}

HmlValue hml_string_substr_from(HmlValue str, HmlValue start) {
    // One-argument substr(start): everything from start to the end
    return hml_string_substr(str, start, hml_val_i32(INT32_MAX));
}

HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int char_len = hml_string_codepoint_length(s);
    int32_t start_idx = hml_to_i32(start);
    int32_t end_idx = hml_to_i32(end);

    // Clamp bounds (codepoint positions)
    if (start_idx < 0) start_idx = 0;
    if (start_idx > char_len) start_idx = char_len;
    if (end_idx < start_idx) end_idx = start_idx;
    if (end_idx > char_len) end_idx = char_len;

    int start_byte = hml_utf8_byte_offset(s->data, s->length, start_idx);
    int end_byte = hml_utf8_byte_offset(s->data, s->length, end_idx);
    int byte_len = end_byte - start_byte;

    char *result = malloc(byte_len + 1);
    if (!result) hml_runtime_error("slice() out of memory");
    memcpy(result, s->data + start_byte, byte_len);
    result[byte_len] = '\0';
    return hml_val_string_owned(result, byte_len, byte_len + 1);
}

HmlValue hml_string_find(HmlValue str, HmlValue needle) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        needle.type != HML_VAL_STRING || !needle.as.as_string) {
        return hml_val_i32(-1);
    }
    HmlString *s = str.as.as_string;
    HmlString *n = needle.as.as_string;

    if (n->length == 0) return hml_val_i32(0);
    if (n->length > s->length) return hml_val_i32(-1);

    // Report the codepoint index so the result composes with
    // substr()/slice()/char_at() (matching the interpreter)
    for (int i = 0; i <= s->length - n->length; i++) {
        if (memcmp(s->data + i, n->data, n->length) == 0) {
            return hml_val_i32(hml_utf8_count_codepoints(s->data, i));
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_string_rfind(HmlValue str, HmlValue needle) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        needle.type != HML_VAL_STRING || !needle.as.as_string) {
        return hml_val_i32(-1);
    }
    HmlString *s = str.as.as_string;
    HmlString *n = needle.as.as_string;

    // Empty string found at end (Python/JS behavior)
    if (n->length == 0) return hml_val_i32(hml_string_codepoint_length(s));
    if (n->length > s->length) return hml_val_i32(-1);

    // Codepoint index, matching find() and the interpreter
    for (int i = s->length - n->length; i >= 0; i--) {
        if (memcmp(s->data + i, n->data, n->length) == 0) {
            return hml_val_i32(hml_utf8_count_codepoints(s->data, i));
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_string_contains(HmlValue str, HmlValue needle) {
    HmlValue pos = hml_string_find(str, needle);
    return hml_val_bool(pos.as.as_i32 >= 0);
}

HmlValue hml_string_split(HmlValue str, HmlValue delimiter) {
    HmlValue result = hml_val_array();

    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        delimiter.type != HML_VAL_STRING || !delimiter.as.as_string) {
        return result;
    }

    HmlString *s = str.as.as_string;
    HmlString *d = delimiter.as.as_string;

    if (d->length == 0) {
        // Split into individual characters
        for (int i = 0; i < s->length; i++) {
            char *c = malloc(2);
            c[0] = s->data[i];
            c[1] = '\0';
            HmlValue part_val = hml_val_string_owned(c, 1, 2);
            hml_array_push(result, part_val);
            // push() retained it; drop the creation ref so the array
            // holds the sole reference (same orphaned-creation-ref
            // class as the fixed hml_object_keys bug).
            hml_release(&part_val);
        }
        return result;
    }

    int start = 0;
    for (int i = 0; i <= s->length - d->length; i++) {
        if (memcmp(s->data + i, d->data, d->length) == 0) {
            int len = i - start;
            char *part = malloc(len + 1);
            memcpy(part, s->data + start, len);
            part[len] = '\0';
            HmlValue part_val = hml_val_string_owned(part, len, len + 1);
            hml_array_push(result, part_val);
            hml_release(&part_val);
            i += d->length - 1;
            start = i + 1;
        }
    }

    // Add remaining part
    int len = s->length - start;
    char *part = malloc(len + 1);
    memcpy(part, s->data + start, len);
    part[len] = '\0';
    HmlValue part_val = hml_val_string_owned(part, len, len + 1);
    hml_array_push(result, part_val);
    hml_release(&part_val);

    return result;
}

HmlValue hml_string_trim(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int start = 0;
    int end = s->length - 1;

    while (start < s->length && (s->data[start] == ' ' || s->data[start] == '\t' ||
                                  s->data[start] == '\n' || s->data[start] == '\r')) {
        start++;
    }

    while (end >= start && (s->data[end] == ' ' || s->data[end] == '\t' ||
                            s->data[end] == '\n' || s->data[end] == '\r')) {
        end--;
    }

    int len = end - start + 1;
    if (len <= 0) return hml_val_string("");

    char *result = malloc(len + 1);
    memcpy(result, s->data + start, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_trim_start(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int start = 0;

    while (start < s->length && (s->data[start] == ' ' || s->data[start] == '\t' ||
                                  s->data[start] == '\n' || s->data[start] == '\r')) {
        start++;
    }

    int len = s->length - start;
    if (len <= 0) return hml_val_string("");

    char *result = malloc(len + 1);
    memcpy(result, s->data + start, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_trim_end(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int end = s->length - 1;

    while (end >= 0 && (s->data[end] == ' ' || s->data[end] == '\t' ||
                         s->data[end] == '\n' || s->data[end] == '\r')) {
        end--;
    }

    int len = end + 1;
    if (len <= 0) return hml_val_string("");

    char *result = malloc(len + 1);
    memcpy(result, s->data, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_to_upper(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'a' && c <= 'z') {
            result[i] = c - 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_to_lower(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'A' && c <= 'Z') {
            result[i] = c + 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_starts_with(HmlValue str, HmlValue prefix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        prefix.type != HML_VAL_STRING || !prefix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = prefix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    return hml_val_bool(memcmp(s->data, p->data, p->length) == 0);
}

HmlValue hml_string_ends_with(HmlValue str, HmlValue suffix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        suffix.type != HML_VAL_STRING || !suffix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = suffix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    int offset = s->length - p->length;
    return hml_val_bool(memcmp(s->data + offset, p->data, p->length) == 0);
}

HmlValue hml_string_replace(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Find first occurrence
    int pos = -1;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            pos = i;
            break;
        }
    }

    if (pos == -1) {
        hml_retain(&str);
        return str;
    }

    int64_t new_len64 = (int64_t)s->length - (int64_t)o->length + (int64_t)n->length;
    if (new_len64 < 0 || new_len64 > INT_MAX) {
        hml_runtime_error("replace() result string too large");
    }
    int new_len = (int)new_len64;
    char *result = malloc((size_t)new_len + 1);
    if (!result) {
        hml_runtime_error("replace() memory allocation failed");
    }
    memcpy(result, s->data, pos);
    memcpy(result + pos, n->data, n->length);
    memcpy(result + pos + n->length, s->data + pos + o->length, s->length - pos - o->length);
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

HmlValue hml_string_replace_all(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Count occurrences
    int count = 0;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            count++;
            i += o->length - 1;
        }
    }

    if (count == 0) {
        hml_retain(&str);
        return str;
    }

    // Calculate new length with overflow checking
    // new_len = s->length - (count * o->length) + (count * n->length)
    // Simplified: new_len = s->length + count * (n->length - o->length)
    size_t base_len = (size_t)s->length;
    int len_diff = n->length - o->length;  // Can be negative
    size_t new_len;

    if (len_diff >= 0) {
        // Result is larger - check for overflow
        size_t add_amount = (size_t)count * (size_t)len_diff;
        if (add_amount / (size_t)count != (size_t)len_diff ||
            base_len > SIZE_MAX - add_amount ||
            base_len + add_amount > INT_MAX) {
            hml_runtime_error("String replace_all overflow: result too large");
        }
        new_len = base_len + add_amount;
    } else {
        // Result is smaller - no overflow possible
        size_t sub_amount = (size_t)count * (size_t)(-len_diff);
        new_len = base_len - sub_amount;
    }

    char *result = malloc(new_len + 1);
    if (!result) {
        hml_runtime_error("Failed to allocate memory for string replace_all");
    }
    int result_pos = 0;

    for (int i = 0; i < s->length; ) {
        if (i <= s->length - o->length && memcmp(s->data + i, o->data, o->length) == 0) {
            memcpy(result + result_pos, n->data, n->length);
            result_pos += n->length;
            i += o->length;
        } else {
            result[result_pos++] = s->data[i++];
        }
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, (int)new_len, (int)new_len + 1);
}

HmlValue hml_string_repeat(HmlValue str, HmlValue count) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t n = hml_to_i32(count);
    if (n <= 0) return hml_val_string("");

    // Check for integer overflow: new_len = s->length * n
    // Use size_t for safe multiplication
    size_t str_len = (size_t)s->length;
    size_t repeat_count = (size_t)n;
    if (str_len > 0 && repeat_count > SIZE_MAX / str_len) {
        hml_runtime_error("String repeat overflow: result too large");
    }
    size_t new_len = str_len * repeat_count;
    if (new_len > INT_MAX) {
        hml_runtime_error("String repeat overflow: result too large");
    }

    char *result = malloc(new_len + 1);
    if (!result) {
        hml_runtime_error("Failed to allocate memory for string repeat");
    }
    for (size_t i = 0; i < repeat_count; i++) {
        memcpy(result + i * str_len, s->data, str_len);
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, (int)new_len, (int)new_len + 1);
}

// OPTIMIZATION: Concatenate 3 strings in a single allocation
HmlValue hml_string_concat3(HmlValue a, HmlValue b, HmlValue c) {
    // Convert all to strings if needed
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string_concat(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string_concat(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string_concat(c);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int total = len_a + len_b + len_c;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); }  // No need to update pos after last copy
    result[total] = '\0';

    // Release converted strings
    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);

    return hml_val_string_owned(result, total, total + 1);
}

// OPTIMIZATION: Concatenate 4 strings in a single allocation
HmlValue hml_string_concat4(HmlValue a, HmlValue b, HmlValue c, HmlValue d) {
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string_concat(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string_concat(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string_concat(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string_concat(d);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;
    HmlString *sd = str_d.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int len_d = sd ? sd->length : 0;
    int total = len_a + len_b + len_c + len_d;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); pos += len_c; }
    if (sd) { memcpy(result + pos, sd->data, len_d); }  // No need to update pos after last copy
    result[total] = '\0';

    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);
    if (d.type != HML_VAL_STRING) hml_release(&str_d);

    return hml_val_string_owned(result, total, total + 1);
}

// OPTIMIZATION: Concatenate 5 strings in a single allocation
HmlValue hml_string_concat5(HmlValue a, HmlValue b, HmlValue c, HmlValue d, HmlValue e) {
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string_concat(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string_concat(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string_concat(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string_concat(d);
    HmlValue str_e = (e.type == HML_VAL_STRING) ? e : hml_to_string_concat(e);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;
    HmlString *sd = str_d.as.as_string;
    HmlString *se = str_e.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int len_d = sd ? sd->length : 0;
    int len_e = se ? se->length : 0;
    int total = len_a + len_b + len_c + len_d + len_e;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); pos += len_c; }
    if (sd) { memcpy(result + pos, sd->data, len_d); pos += len_d; }
    if (se) { memcpy(result + pos, se->data, len_e); }  // No need to update pos after last copy
    result[total] = '\0';

    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);
    if (d.type != HML_VAL_STRING) hml_release(&str_d);
    if (e.type != HML_VAL_STRING) hml_release(&str_e);

    return hml_val_string_owned(result, total, total + 1);
}

// Concatenate an array of strings into a single string
HmlValue hml_string_concat_many(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("string_concat_many() requires array argument");
    }

    HmlArray *a = arr.as.as_array;

    // Handle empty array
    if (a->length == 0) {
        return hml_val_string("");
    }

    // Calculate total length needed
    int total_len = 0;
    for (int i = 0; i < a->length; i++) {
        if (a->elements[i].type == HML_VAL_STRING && a->elements[i].as.as_string) {
            total_len += a->elements[i].as.as_string->length;
        }
    }

    // Allocate and build result
    char *result = malloc(total_len + 1);
    int pos = 0;
    for (int i = 0; i < a->length; i++) {
        if (a->elements[i].type == HML_VAL_STRING && a->elements[i].as.as_string) {
            HmlString *s = a->elements[i].as.as_string;
            memcpy(result + pos, s->data, s->length);
            pos += s->length;
        }
    }
    result[total_len] = '\0';

    return hml_val_string_owned(result, total_len, total_len + 1);
}

// String indexing (returns char at position as rune)
HmlValue hml_string_index(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    int char_len = hml_string_codepoint_length(s);

    if (idx < 0 || idx >= char_len) {
        hml_runtime_error_loc("String index %d out of bounds (length=%d)", idx, char_len);
    }

    int byte_pos = hml_utf8_byte_offset(s->data, s->length, idx);
    return hml_val_rune(hml_utf8_decode_at(s->data, byte_pos));
}

// ========== UTF-8 HELPERS ==========
// Note: Most UTF-8 functions are now in the shared utf8.h module.
// The functions below are thin wrappers or use the shared module directly.

// Helper: get byte length of UTF-8 character at position
static int utf8_char_len_at(const char *s, int pos, int len) {
    if (pos >= len) return 0;
    return hml_utf8_char_byte_len((unsigned char)s[pos]);
}

void hml_string_index_assign(HmlValue str, HmlValue index, HmlValue val) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("String index assignment requires string");
    }

    // Refuse to mutate immortal (pooled) strings: every 1-char ASCII literal
    // in the process shares one static pool entry, so writing through it
    // would silently rewrite every other occurrence of that literal (e.g.
    // `let s = "x"; s[0] = 'y';` used to make every later "x" print as "y").
    // Assignments whose target is a plain variable never reach this check -
    // codegen routes them through hml_string_index_assign_var, which
    // copy-on-writes the pooled entry into a private mutable string first.
    if (atomic_load(&str.as.as_string->ref_count) >= HML_REFCOUNT_IMMORTAL_MIN) {
        hml_runtime_error("Cannot mutate a shared string literal");
    }

    // Accept both rune and integer types (convert integer to rune)
    uint32_t rune_val = 0;
    if (val.type == HML_VAL_RUNE) {
        rune_val = val.as.as_rune;
    } else if (hml_is_integer_type(val)) {
        int64_t int_val = hml_val_to_int64(val);
        if (int_val < 0 || int_val > 0x10FFFF) {
            hml_runtime_error("Integer value %" PRId64 " out of range for rune [0, 0x10FFFF]", int_val);
        }
        rune_val = (uint32_t)int_val;
    } else {
        hml_runtime_error("String index assignment requires rune or integer value");
    }

    int idx = hml_to_i32(index);
    HmlString *s = str.as.as_string;

    if (idx < 0 || idx >= s->length) {
        hml_runtime_error("String index %d out of bounds (length %d)", idx, s->length);
    }

    // Calculate bytes needed for new rune and current character at position
    int new_len = hml_utf8_encode_len(rune_val);
    int old_len = utf8_char_len_at(s->data, idx, s->length);

    // Ensure we don't read past end of string
    if (idx + old_len > s->length) {
        old_len = s->length - idx;
    }

    if (new_len == old_len) {
        // Same size - just overwrite in place
        hml_utf8_encode(rune_val, s->data + idx);
    } else {
        // Different size - need to resize string
        int new_total = s->length - old_len + new_len;

        // Check if we can keep using SSO
        if (new_total <= HML_SSO_THRESHOLD && s->is_sso) {
            // Stay in SSO mode - shift data in inline buffer
            int suffix_start = idx + old_len;
            int suffix_len = s->length - suffix_start;
            if (suffix_len > 0) {
                memmove(s->inline_data + idx + new_len, s->inline_data + suffix_start, suffix_len);
            }
            hml_utf8_encode(rune_val, s->inline_data + idx);
            s->inline_data[new_total] = '\0';
            s->length = new_total;
            s->char_length = -1;
        } else {
            // Need heap allocation
            char *new_data = malloc(new_total + 1);
            if (!new_data) {
                hml_runtime_error("Failed to allocate memory for string resize");
            }

            // Copy prefix (before idx)
            memcpy(new_data, s->data, idx);

            // Encode new rune
            hml_utf8_encode(rune_val, new_data + idx);

            // Copy suffix (after old character)
            int suffix_start = idx + old_len;
            int suffix_len = s->length - suffix_start;
            if (suffix_len > 0) {
                memcpy(new_data + idx + new_len, s->data + suffix_start, suffix_len);
            }

            new_data[new_total] = '\0';

            // Replace string data - only free if not SSO
            if (!s->is_sso) {
                free(s->data);
            }
            s->data = new_data;
            s->length = new_total;
            s->capacity = new_total + 1;
            s->is_sso = 0;
            s->char_length = -1;  // Invalidate cached character count
        }
    }
}

// Index-assign through a variable slot. When the slot holds an immortal
// (pooled) string, copy-on-write it into a private mutable string first and
// rebind the slot, so the mutation is visible through the variable without
// corrupting the shared pool entry. Non-pooled strings mutate in place,
// preserving alias semantics (`let b = a; a[0] = 'H';` changes both).
void hml_string_index_assign_var(HmlValue *slot, HmlValue index, HmlValue val) {
    if (slot && slot->type == HML_VAL_STRING && slot->as.as_string &&
        atomic_load(&slot->as.as_string->ref_count) >= HML_REFCOUNT_IMMORTAL_MIN) {
        HmlString *s = slot->as.as_string;
        char *copy = malloc((size_t)s->length + 1);
        if (!copy) {
            hml_runtime_error("Out of memory copying string for mutation");
        }
        memcpy(copy, s->data, (size_t)s->length);
        copy[s->length] = '\0';
        // hml_val_string_owned never returns a pooled entry. The immortal
        // source needs (and must have) no release.
        *slot = hml_val_string_owned(copy, s->length, s->length + 1);
    }
    hml_string_index_assign(*slot, index, val);
}

// UTF-8 helper wrappers for legacy code
// These use the shared UTF-8 module
#define utf8_char_len(c)  hml_utf8_char_byte_len(c)
#define utf8_decode_char(s, bytes_read)  hml_utf8_decode(s, bytes_read)

// Count UTF-8 codepoints in a string
HmlValue hml_string_char_count(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    HmlString *s = str.as.as_string;

    // Use cached char_length if available
    if (s->char_length >= 0) {
        return hml_val_i32(s->char_length);
    }

    // Count UTF-8 codepoints
    int count = 0;
    int byte_pos = 0;
    while (byte_pos < s->length) {
        byte_pos += utf8_char_len((unsigned char)s->data[byte_pos]);
        count++;
    }

    // Cache the result
    s->char_length = count;
    return hml_val_i32(count);
}

// Get rune at character index (UTF-8 aware)
HmlValue hml_string_rune_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);

    if (idx < 0) {
        return hml_val_null();
    }

    // Navigate to the character index
    int byte_pos = 0;
    int char_idx = 0;
    while (byte_pos < s->length && char_idx < idx) {
        byte_pos += utf8_char_len((unsigned char)s->data[byte_pos]);
        char_idx++;
    }

    // Check if we found the character
    if (byte_pos >= s->length) {
        return hml_val_null();
    }

    // Decode the UTF-8 codepoint at this position
    int bytes_read;
    uint32_t codepoint = utf8_decode_char(s->data + byte_pos, &bytes_read);
    return hml_val_rune(codepoint);
}

// Convert string to array of runes (codepoints)
HmlValue hml_string_chars(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("chars() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlValue arr = hml_val_array();

    int byte_pos = 0;
    while (byte_pos < s->length) {
        int bytes_read;
        uint32_t codepoint = utf8_decode_char(s->data + byte_pos, &bytes_read);
        HmlValue rune = hml_val_rune(codepoint);
        hml_array_push(arr, rune);
        byte_pos += bytes_read;
    }

    return arr;
}

// Convert string to array of bytes (u8 values)
HmlValue hml_string_bytes(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("bytes() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlValue arr = hml_val_array();

    for (int i = 0; i < s->length; i++) {
        HmlValue byte = hml_val_u8((unsigned char)s->data[i]);
        hml_array_push(arr, byte);
    }

    return arr;
}

// Get raw pointer to string bytes (no allocation)
HmlValue hml_string_byte_ptr(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("byte_ptr() requires string");
    }

    HmlString *s = str.as.as_string;
    return hml_val_ptr(s->data);
}

// Convert string to buffer (raw bytes)
HmlValue hml_string_to_bytes(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("to_bytes() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
    buf->data = malloc(s->length);
    memcpy(buf->data, s->data, s->length);
    buf->length = s->length;
    buf->capacity = s->length;
    buf->ref_count = 1;
    atomic_store(&buf->freed, 0);
    buf->parent = NULL;

    return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
}

// ========== BUFFER OPERATIONS ==========

// Buffer indexing
HmlValue hml_buffer_get(HmlValue buf, HmlValue index) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("Buffer index requires buffer");
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        hml_runtime_error("Buffer index %d out of bounds (length %d)", idx, b->length);
    }

    uint8_t *data = (uint8_t *)b->data;
    return hml_val_u8(data[idx]);
}

void hml_buffer_set(HmlValue buf, HmlValue index, HmlValue val) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("Buffer index assignment requires buffer");
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        hml_runtime_error("Buffer index %d out of bounds (length %d)", idx, b->length);
    }

    uint8_t *data = (uint8_t *)b->data;
    data[idx] = (uint8_t)hml_to_i32(val);
}

HmlValue hml_buffer_length(HmlValue buf) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("length requires buffer");
    }
    return hml_val_i32(buf.as.as_buffer->length);
}

HmlValue hml_buffer_capacity(HmlValue buf) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("capacity requires buffer");
    }
    return hml_val_i32(buf.as.as_buffer->capacity);
}

HmlValue hml_buffer_slice(HmlValue buf, HmlValue start, HmlValue end) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("slice() requires buffer");
    }

    HmlBuffer *b = buf.as.as_buffer;
    int s = hml_to_i32(start);
    int e = hml_to_i32(end);

    // Clamp bounds
    if (s < 0) s = 0;
    if (e > b->length) e = b->length;
    if (s > e) s = e;

    // Find the root owner (follow parent chain)
    HmlBuffer *root = b;
    while (root->parent) {
        root = root->parent;
    }

    // Create zero-copy view
    HmlBuffer *view = calloc(1, sizeof(HmlBuffer));
    if (!view) {
        hml_runtime_error("Failed to allocate buffer slice");
    }
    view->data = (uint8_t *)b->data + s;
    view->length = e - s;
    view->capacity = e - s;
    view->ref_count = 1;
    atomic_store(&view->freed, 0);
    view->parent = root;
    atomic_fetch_add(&root->ref_count, 1);  // Keep root alive

    HmlValue val;
    val.type = HML_VAL_BUFFER;
    val.as.as_buffer = view;
    return val;
}

// Convert buffer contents to a string (interpret as UTF-8), matching the
// interpreter's buffer.to_string() method
HmlValue hml_buffer_to_string(HmlValue buf) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("to_string() requires buffer");
    }

    HmlBuffer *b = buf.as.as_buffer;
    char *str_data = malloc(b->length + 1);
    if (!str_data) {
        hml_runtime_error("Failed to allocate string for buffer to_string");
    }
    memcpy(str_data, b->data, b->length);
    str_data[b->length] = '\0';
    HmlValue result = hml_val_string(str_data);
    free(str_data);
    return result;
}

// ========== Buffer Typed Read/Write Methods ==========
//
// These methods allow reading/writing typed values at arbitrary offsets
// within a buffer, with bounds checking and explicit endianness.
// This eliminates the need to create intermediate small buffers when
// building binary packets.

static void buffer_validate(HmlValue buf, const char *method) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("%s requires a buffer", method);
    }
}

static void buffer_bounds_check(HmlBuffer *b, int offset, int size, const char *method) {
    // Use 64-bit math: offset is attacker-controlled up to INT_MAX, so computing
    // offset + size in `int` could overflow to a negative value and bypass the
    // check, yielding an out-of-bounds access ~2GB past the buffer.
    if (offset < 0 || size < 0 || (int64_t)offset + (int64_t)size > (int64_t)b->length) {
        hml_runtime_error("%s: offset %d with size %d out of bounds (buffer length %d)",
                         method, offset, size, b->length);
    }
}

// --- write_u8 / write_i8 (no endianness needed for single byte) ---

void hml_buffer_write_u8(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u8");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 1, "write_u8");
    ((uint8_t *)b->data)[off] = (uint8_t)hml_to_i32(value);
}

void hml_buffer_write_i8(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i8");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 1, "write_i8");
    ((int8_t *)b->data)[off] = (int8_t)hml_to_i32(value);
}

// --- 16-bit write methods ---

void hml_buffer_write_u16_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u16_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "write_u16_le");
    uint16_t v = (uint16_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void hml_buffer_write_u16_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u16_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "write_u16_be");
    uint16_t v = (uint16_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

void hml_buffer_write_i16_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i16_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "write_i16_le");
    uint16_t v = (uint16_t)(int16_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void hml_buffer_write_i16_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i16_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "write_i16_be");
    uint16_t v = (uint16_t)(int16_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

// --- 32-bit write methods ---

void hml_buffer_write_u32_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_u32_le");
    uint32_t v = (uint32_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void hml_buffer_write_u32_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_u32_be");
    uint32_t v = (uint32_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

void hml_buffer_write_i32_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_i32_le");
    uint32_t v = (uint32_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void hml_buffer_write_i32_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_i32_be");
    uint32_t v = (uint32_t)hml_to_i32(value);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

// --- 64-bit write methods ---

void hml_buffer_write_i64_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_i64_le");
    uint64_t v = (uint64_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void hml_buffer_write_i64_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_i64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_i64_be");
    uint64_t v = (uint64_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void hml_buffer_write_u64_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_u64_le");
    uint64_t v = (uint64_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void hml_buffer_write_u64_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_u64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_u64_be");
    uint64_t v = (uint64_t)hml_to_i64(value);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

// --- Float write methods ---

void hml_buffer_write_f32_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_f32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_f32_le");
    float f = (float)hml_to_f64(value);
    uint32_t v;
    memcpy(&v, &f, 4);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void hml_buffer_write_f32_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_f32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "write_f32_be");
    float f = (float)hml_to_f64(value);
    uint32_t v;
    memcpy(&v, &f, 4);
    uint8_t *p = (uint8_t *)b->data + off;
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

void hml_buffer_write_f64_le(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_f64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_f64_le");
    double d = hml_to_f64(value);
    uint64_t v;
    memcpy(&v, &d, 8);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void hml_buffer_write_f64_be(HmlValue buf, HmlValue offset, HmlValue value) {
    buffer_validate(buf, "write_f64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "write_f64_be");
    double d = hml_to_f64(value);
    uint64_t v;
    memcpy(&v, &d, 8);
    uint8_t *p = (uint8_t *)b->data + off;
    for (int i = 0; i < 8; i++) p[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

// --- write_bytes(offset, src_buffer, len) ---

void hml_buffer_write_bytes(HmlValue buf, HmlValue offset, HmlValue src, HmlValue len) {
    buffer_validate(buf, "write_bytes");
    if (src.type != HML_VAL_BUFFER || !src.as.as_buffer) {
        hml_runtime_error("write_bytes: source must be a buffer");
    }
    HmlBuffer *b = buf.as.as_buffer;
    HmlBuffer *s = src.as.as_buffer;
    int off = hml_to_i32(offset);
    int n = hml_to_i32(len);
    if (n < 0) {
        hml_runtime_error("write_bytes: length must be non-negative");
    }
    buffer_bounds_check(b, off, n, "write_bytes");
    if (n > s->length) {
        hml_runtime_error("write_bytes: source buffer length %d less than requested %d", s->length, n);
    }
    memcpy((uint8_t *)b->data + off, s->data, n);
}

// ========== Buffer Typed Read Methods ==========

HmlValue hml_buffer_read_u8(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u8");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 1, "read_u8");
    return hml_val_u8(((uint8_t *)b->data)[off]);
}

HmlValue hml_buffer_read_i8(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i8");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 1, "read_i8");
    return hml_val_i8(((int8_t *)b->data)[off]);
}

HmlValue hml_buffer_read_u16_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u16_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "read_u16_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return hml_val_u16(v);
}

HmlValue hml_buffer_read_u16_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u16_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "read_u16_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint16_t v = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    return hml_val_u16(v);
}

HmlValue hml_buffer_read_i16_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i16_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "read_i16_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return hml_val_i16((int16_t)v);
}

HmlValue hml_buffer_read_i16_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i16_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 2, "read_i16_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint16_t v = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    return hml_val_i16((int16_t)v);
}

HmlValue hml_buffer_read_u32_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_u32_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return hml_val_u32(v);
}

HmlValue hml_buffer_read_u32_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_u32_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return hml_val_u32(v);
}

HmlValue hml_buffer_read_i32_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_i32_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return hml_val_i32((int32_t)v);
}

HmlValue hml_buffer_read_i32_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_i32_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return hml_val_i32((int32_t)v);
}

HmlValue hml_buffer_read_i64_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_i64_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (i * 8));
    return hml_val_i64((int64_t)v);
}

HmlValue hml_buffer_read_i64_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_i64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_i64_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[7 - i] << (i * 8));
    return hml_val_i64((int64_t)v);
}

HmlValue hml_buffer_read_u64_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_u64_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (i * 8));
    return hml_val_u64(v);
}

HmlValue hml_buffer_read_u64_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_u64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_u64_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[7 - i] << (i * 8));
    return hml_val_u64(v);
}

HmlValue hml_buffer_read_f32_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_f32_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_f32_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &v, 4);
    return hml_val_f32(f);
}

HmlValue hml_buffer_read_f32_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_f32_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 4, "read_f32_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    float f;
    memcpy(&f, &v, 4);
    return hml_val_f32(f);
}

HmlValue hml_buffer_read_f64_le(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_f64_le");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_f64_le");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (i * 8));
    double d;
    memcpy(&d, &v, 8);
    return hml_val_f64(d);
}

HmlValue hml_buffer_read_f64_be(HmlValue buf, HmlValue offset) {
    buffer_validate(buf, "read_f64_be");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    buffer_bounds_check(b, off, 8, "read_f64_be");
    uint8_t *p = (uint8_t *)b->data + off;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[7 - i] << (i * 8));
    double d;
    memcpy(&d, &v, 8);
    return hml_val_f64(d);
}

// --- read_bytes(offset, len) - returns a new buffer ---

HmlValue hml_buffer_read_bytes(HmlValue buf, HmlValue offset, HmlValue len) {
    buffer_validate(buf, "read_bytes");
    HmlBuffer *b = buf.as.as_buffer;
    int off = hml_to_i32(offset);
    int n = hml_to_i32(len);
    if (n < 0) {
        hml_runtime_error("read_bytes: length must be non-negative");
    }
    buffer_bounds_check(b, off, n, "read_bytes");
    HmlValue result = hml_val_buffer(n);
    memcpy(result.as.as_buffer->data, (uint8_t *)b->data + off, n);
    return result;
}

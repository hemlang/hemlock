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

HmlValue hml_string_char_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);

    // Compute character length if not cached
    int char_len = s->char_length;
    if (char_len < 0) {
        char_len = hml_utf8_count_codepoints(s->data, s->length);
        s->char_length = char_len;
    }

    if (idx < 0 || idx >= char_len) {
        return hml_val_null();
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
        return hml_val_null();
    }
    return hml_val_u8((uint8_t)s->data[idx]);
}

HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t len = hml_to_i32(length);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= s->length) return hml_val_string("");
    if (len < 0) len = 0;
    if (start_idx + len > s->length) len = s->length - start_idx;

    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t end_idx = hml_to_i32(end);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx > s->length) start_idx = s->length;
    if (end_idx < start_idx) end_idx = start_idx;
    if (end_idx > s->length) end_idx = s->length;

    int len = end_idx - start_idx;
    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
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

    for (int i = 0; i <= s->length - n->length; i++) {
        if (memcmp(s->data + i, n->data, n->length) == 0) {
            return hml_val_i32(i);
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

    int new_len = s->length - o->length + n->length;
    char *result = malloc(new_len + 1);
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
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);

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
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string(d);

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
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string(d);
    HmlValue str_e = (e.type == HML_VAL_STRING) ? e : hml_to_string(e);

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
    return hml_string_char_at(str, index);
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
        hml_runtime_error("String index %d out of bounds", idx);
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
    root->ref_count++;  // Keep root alive

    HmlValue val;
    val.type = HML_VAL_BUFFER;
    val.as.as_buffer = view;
    return val;
}

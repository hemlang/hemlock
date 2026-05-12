/*
 * Hemlock Runtime Library - Builtins Implementation
 *
 * This file contains miscellaneous builtin functions not in specialized files:
 * - String operations (concat, append_inplace, to_string)
 * - Internal helper operations
 *
 * See also:
 * - builtins_core.c: print, typeof, assert, panic, type checking
 * - builtins_process.c: exec, fork, wait, signals
 * - builtins_ops.c: binary/unary operations
 * - builtins_memory2.c: alloc, free, ptr operations
 * - builtins_object.c: object field operations
 * - builtins_io.c: file I/O, filesystem, system info
 * - builtins_func.c: function calls, defer, exceptions
 * - builtins_types.c: type definitions, enum registry
 * - builtins_http.c: HTTP/WebSocket support
 */

#include "builtins_internal.h"

// ========== STRING OPERATIONS ==========

// OPTIMIZATION: In-place string append for pattern "x = x + y"
// If the left string has refcount == 1, we can mutate it in place
// This turns O(n²) repeated concatenation into O(n) amortized
HmlValue hml_string_append_inplace(HmlValue *dest, HmlValue src) {
    // Only works if dest is a string with refcount 1
    if (dest->type != HML_VAL_STRING || !dest->as.as_string) {
        // Fall back to regular concat
        HmlValue result = hml_string_concat(*dest, src);
        hml_release(dest);
        *dest = result;
        return result;
    }

    HmlString *sd = dest->as.as_string;

    // If refcount > 1, we can't mutate - fall back to concat
    if (atomic_load(&sd->ref_count) > 1) {
        HmlValue result = hml_string_concat(*dest, src);
        hml_release(dest);
        *dest = result;
        return result;
    }

    // FAST PATH: Appending a single rune (common in character-by-character string building)
    if (src.type == HML_VAL_RUNE) {
        char utf8_buf[4];
        int char_len = encode_utf8(src.as.as_rune, utf8_buf);
        int new_len = sd->length + char_len;

        // Grow capacity if needed
        if (new_len + 1 > sd->capacity) {
            // SECURITY: Check for integer overflow before doubling capacity
            int new_capacity;
            if (sd->capacity > INT_MAX / 2) {
                new_capacity = new_len + 1;  // Fall back to exact size needed
            } else {
                new_capacity = sd->capacity * 2;
            }
            if (new_capacity < new_len + 1) new_capacity = new_len + 1;
            if (new_capacity < 32) new_capacity = 32;

            char *new_data;
            if (sd->is_sso) {
                // SSO strings use inline storage - must allocate new heap buffer
                new_data = malloc(new_capacity);
                if (new_data) {
                    memcpy(new_data, sd->data, sd->length);
                    sd->is_sso = 0;
                }
            } else {
                new_data = realloc(sd->data, new_capacity);
            }
            if (!new_data) {
                HmlValue result = hml_string_concat(*dest, src);
                hml_release(dest);
                *dest = result;
                return result;
            }
            sd->data = new_data;
            sd->capacity = new_capacity;
        }

        memcpy(sd->data + sd->length, utf8_buf, char_len);
        sd->length = new_len;
        sd->data[new_len] = '\0';
        sd->char_length = -1;
        return *dest;
    }

    // Get source string
    HmlValue str_src = (src.type == HML_VAL_STRING) ? src : hml_to_string(src);
    HmlString *ss = str_src.as.as_string;
    int src_len = ss ? ss->length : 0;

    if (src_len == 0) {
        if (src.type != HML_VAL_STRING) hml_release(&str_src);
        return *dest;
    }

    int new_len = sd->length + src_len;

    // Grow capacity if needed (with 2x growth factor for amortized O(1))
    if (new_len + 1 > sd->capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        int new_capacity;
        if (sd->capacity > INT_MAX / 2) {
            new_capacity = new_len + 1;  // Fall back to exact size needed
        } else {
            new_capacity = sd->capacity * 2;
        }
        if (new_capacity < new_len + 1) new_capacity = new_len + 1;
        if (new_capacity < 32) new_capacity = 32;  // Minimum capacity

        char *new_data;
        if (sd->is_sso) {
            // SSO strings use inline storage - must allocate new heap buffer
            new_data = malloc(new_capacity);
            if (new_data) {
                memcpy(new_data, sd->data, sd->length);
                sd->is_sso = 0;
            }
        } else {
            new_data = realloc(sd->data, new_capacity);
        }
        if (!new_data) {
            // Allocation failed - fall back to regular concat
            if (src.type != HML_VAL_STRING) hml_release(&str_src);
            HmlValue result = hml_string_concat(*dest, src);
            hml_release(dest);
            *dest = result;
            return result;
        }
        sd->data = new_data;
        sd->capacity = new_capacity;
    }

    // Append in place
    memcpy(sd->data + sd->length, ss->data, src_len);
    sd->length = new_len;
    sd->data[new_len] = '\0';
    sd->char_length = -1;  // Invalidate cached char length

    if (src.type != HML_VAL_STRING) hml_release(&str_src);

    // Return the same value (refcount unchanged since we're mutating in place)
    return *dest;
}

HmlValue hml_string_concat(HmlValue a, HmlValue b) {
    // FAST PATH: Both are already strings - avoid hml_to_string overhead
    if (a.type == HML_VAL_STRING && b.type == HML_VAL_STRING &&
        a.as.as_string && b.as.as_string) {
        HmlString *sa = a.as.as_string;
        HmlString *sb = b.as.as_string;
        int total = sa->length + sb->length;

        char *result = malloc(total + 1);
        memcpy(result, sa->data, sa->length);
        memcpy(result + sa->length, sb->data, sb->length);
        result[total] = '\0';

        return hml_val_string_owned(result, total, total + 1);
    }

    // Convert both to strings
    HmlValue str_a = hml_to_string(a);
    HmlValue str_b = hml_to_string(b);

    const char *s1 = hml_to_string_ptr(str_a);
    const char *s2 = hml_to_string_ptr(str_b);

    if (!s1) s1 = "";
    if (!s2) s2 = "";

    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int total = len1 + len2;

    char *result = malloc(total + 1);
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2);
    result[total] = '\0';

    // Release converted strings if they were newly created
    hml_release(&str_a);
    hml_release(&str_b);

    return hml_val_string_owned(result, total, total + 1);
}

HmlValue hml_to_string(HmlValue val) {
    if (val.type == HML_VAL_STRING) {
        hml_retain(&val);
        return val;
    }

    char buffer[256];
    switch (val.type) {
        case HML_VAL_I8:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            snprintf(buffer, sizeof(buffer), "%" PRId64, val.as.as_i64);
            break;
        case HML_VAL_U8:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            snprintf(buffer, sizeof(buffer), "%" PRIu64, val.as.as_u64);
            break;
        case HML_VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            snprintf(buffer, sizeof(buffer), "%.15g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            return hml_val_string(val.as.as_bool ? "true" : "false");
        case HML_VAL_NULL:
            return hml_val_string("null");
        case HML_VAL_RUNE:
            // Encode rune to UTF-8
            if (val.as.as_rune < 0x80) {
                buffer[0] = (char)val.as.as_rune;
                buffer[1] = '\0';
            } else if (val.as.as_rune < 0x800) {
                buffer[0] = (char)(0xC0 | (val.as.as_rune >> 6));
                buffer[1] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[2] = '\0';
            } else if (val.as.as_rune < 0x10000) {
                buffer[0] = (char)(0xE0 | (val.as.as_rune >> 12));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[2] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[3] = '\0';
            } else {
                buffer[0] = (char)(0xF0 | (val.as.as_rune >> 18));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 12) & 0x3F));
                buffer[2] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[3] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[4] = '\0';
            }
            return hml_val_string(buffer);
        default:
            return hml_val_string("<value>");
    }

    return hml_val_string(buffer);
}

// ========== STRING METHODS ==========
// Moved to builtins_string.c:
// - hml_string_length, hml_string_byte_length, hml_string_char_at, hml_string_byte_at
// - hml_string_substr, hml_string_slice, hml_string_find, hml_string_contains
// - hml_string_split, hml_string_trim, hml_string_to_upper, hml_string_to_lower
// - hml_string_starts_with, hml_string_ends_with, hml_string_replace, hml_string_replace_all
// - hml_string_repeat, hml_string_concat3/4/5, hml_string_concat_many
// - UTF-8 helpers, hml_string_index_assign, hml_string_char_count
// - hml_string_rune_at, hml_string_chars, hml_string_bytes, hml_string_to_bytes
// - hml_buffer_get, hml_buffer_set, hml_buffer_length, hml_buffer_capacity

// ========== ARRAY OPERATIONS ==========
// Moved to builtins_array.c:
// - Basic: push, pop, shift, unshift, insert, remove
// - Access: get, set, length, first, last, clear
// - Search: find, contains
// - Transform: slice, join, concat, reverse
// - Higher-order: map, filter, reduce
// - Typed arrays: validate and set element type constraints

// ========== OPTIMIZED SERIALIZATION (JSON) ==========
// Moved to builtins_serialization.c:
// - hml_serialize() - Convert values to JSON strings
// - hml_deserialize() - Parse JSON strings to values
// - JSON buffer, visited set, parser helpers

// ========== INTERNAL HELPER OPERATIONS ==========

// Read a u32 value from a pointer
HmlValue hml_read_u32(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_u32() requires a pointer");
    }
    uint32_t *ptr = (uint32_t*)ptr_val.as.as_ptr;
    return hml_val_u32(*ptr);
}

// Read a u64 value from a pointer
HmlValue hml_read_u64(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_u64() requires a pointer");
    }
    uint64_t *ptr = (uint64_t*)ptr_val.as.as_ptr;
    return hml_val_u64(*ptr);
}

// Read a pointer value from a pointer (double indirection)
HmlValue hml_read_ptr(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_ptr() requires a pointer");
    }
    void **pptr = (void**)ptr_val.as.as_ptr;
    return hml_val_ptr(*pptr);
}

// Get the last error string (strerror(errno))
HmlValue hml_strerror(void) {
    return hml_val_string(strerror(errno));
}

// Get the name field from a dirent structure
HmlValue hml_dirent_name(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__dirent_name() requires a pointer");
    }
    struct dirent *entry = (struct dirent*)ptr_val.as.as_ptr;
    return hml_val_string(entry->d_name);
}

// Convert a Hemlock string to a C string (returns allocated ptr)
HmlValue hml_string_to_cstr(HmlValue str_val) {
    if (str_val.type != HML_VAL_STRING) {
        hml_runtime_error("__string_to_cstr() requires a string");
    }
    HmlString *str = str_val.as.as_string;
    char *cstr = malloc(str->length + 1);
    if (!cstr) {
        hml_runtime_error("__string_to_cstr() memory allocation failed");
    }
    memcpy(cstr, str->data, str->length);
    cstr[str->length] = '\0';
    return hml_val_ptr(cstr);
}

// Convert a C string (ptr) to a Hemlock string
HmlValue hml_cstr_to_string(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__cstr_to_string() requires a pointer");
    }
    char *cstr = (char*)ptr_val.as.as_ptr;
    if (!cstr) {
        return hml_val_string("");
    }
    return hml_val_string(cstr);
}

// Forward declaration for hml_string_from_bytes wrapper
HmlValue hml_builtin_string_from_bytes(HmlClosureEnv *env, HmlValue arg);

// Convert an array of bytes or buffer to a UTF-8 string (wrapper for direct calls)
HmlValue hml_string_from_bytes(HmlValue arg) {
    return hml_builtin_string_from_bytes(NULL, arg);
}

// Wrapper functions for internal helpers
HmlValue hml_builtin_read_u32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u32(ptr);
}

HmlValue hml_builtin_read_u64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u64(ptr);
}

HmlValue hml_builtin_read_ptr(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_ptr(ptr);
}

HmlValue hml_builtin_strerror(HmlClosureEnv *env) {
    (void)env;
    return hml_strerror();
}

HmlValue hml_builtin_dirent_name(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_dirent_name(ptr);
}

HmlValue hml_builtin_string_to_cstr(HmlClosureEnv *env, HmlValue str) {
    (void)env;
    return hml_string_to_cstr(str);
}

HmlValue hml_builtin_cstr_to_string(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_cstr_to_string(ptr);
}

// Convert an array of bytes or buffer to a UTF-8 string
HmlValue hml_builtin_string_from_bytes(HmlClosureEnv *env, HmlValue arg) {
    (void)env;

    char *data = NULL;
    int length = 0;

    if (arg.type == HML_VAL_BUFFER) {
        // Handle buffer input
        HmlBuffer *buf = arg.as.as_buffer;
        if (!buf || !buf->data) {
            return hml_val_string("");
        }
        length = buf->length;
        data = malloc(length + 1);
        if (!data) {
            hml_runtime_error("__string_from_bytes() memory allocation failed");
        }
        memcpy(data, buf->data, length);
        data[length] = '\0';
    } else if (arg.type == HML_VAL_ARRAY) {
        // Handle array input - each element should be an integer byte value
        HmlArray *arr = arg.as.as_array;
        if (!arr || arr->length == 0) {
            return hml_val_string("");
        }
        length = arr->length;
        data = malloc(length + 1);
        if (!data) {
            hml_runtime_error("__string_from_bytes() memory allocation failed");
        }

        for (int i = 0; i < arr->length; i++) {
            HmlValue elem = arr->elements[i];
            int byte_val = 0;

            // Accept any integer type
            switch (elem.type) {
                case HML_VAL_I8:
                    byte_val = (unsigned char)elem.as.as_i8;
                    break;
                case HML_VAL_I16:
                    byte_val = elem.as.as_i16 & 0xFF;
                    break;
                case HML_VAL_I32:
                    byte_val = elem.as.as_i32 & 0xFF;
                    break;
                case HML_VAL_I64:
                    byte_val = (int)(elem.as.as_i64 & 0xFF);
                    break;
                case HML_VAL_U8:
                    byte_val = elem.as.as_u8;
                    break;
                case HML_VAL_U16:
                    byte_val = elem.as.as_u16 & 0xFF;
                    break;
                case HML_VAL_U32:
                    byte_val = elem.as.as_u32 & 0xFF;
                    break;
                case HML_VAL_U64:
                    byte_val = (int)(elem.as.as_u64 & 0xFF);
                    break;
                default:
                    free(data);
                    hml_runtime_error("__string_from_bytes() array element at index %d is not an integer", i);
            }

            data[i] = (char)byte_val;
        }
        data[length] = '\0';
    } else {
        hml_runtime_error("__string_from_bytes() requires array or buffer argument");
    }

    // Create string from the data - use hml_val_string_owned which takes ownership of data
    return hml_val_string_owned(data, length, length + 1);
}

// Socket and networking operations moved to builtins_socket.c


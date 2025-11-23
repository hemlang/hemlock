#include "internal.h"
#include <stdarg.h>

// ========== RUNTIME ERROR HELPER ==========

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    value_retain(ctx->exception_state.exception_value);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// ========== STRING METHOD HANDLING ==========

Value call_string_method(String *str, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    // substr(start, length) - extract substring by codepoint positions
    if (strcmp(method, "substr") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "substr() expects 2 arguments (start, length)");
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            return throw_runtime_error(ctx, "substr() arguments must be integers");
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t start = value_to_int(args[0]);
        int32_t char_length = value_to_int(args[1]);

        // Clamp bounds to valid range
        if (start < 0) start = 0;
        if (start >= str->char_length) {
            // Start beyond string length - return empty string
            return val_string("");
        }
        if (char_length < 0) char_length = 0;

        // Clamp length to available characters
        if (start + char_length > str->char_length) {
            char_length = str->char_length - start;
        }

        // Convert codepoint positions to byte offsets
        int start_byte = utf8_byte_offset(str->data, str->length, start);
        int end_byte = utf8_byte_offset(str->data, str->length, start + char_length);
        int byte_length = end_byte - start_byte;

        // Create new string
        char *new_data = malloc(byte_length + 1);
        memcpy(new_data, str->data + start_byte, byte_length);
        new_data[byte_length] = '\0';

        return val_string_take(new_data, byte_length, byte_length + 1);
    }

    // slice(start, end) - Python-style slicing by codepoint (end is exclusive)
    if (strcmp(method, "slice") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "slice() expects 2 arguments (start, end)");
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            return throw_runtime_error(ctx, "slice() arguments must be integers");
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t start = value_to_int(args[0]);
        int32_t end = value_to_int(args[1]);

        // Clamp bounds to valid range (Python/JS/Rust behavior)
        if (start < 0) start = 0;
        if (start > str->char_length) start = str->char_length;
        if (end < start) end = start;  // Empty slice if end < start
        if (end > str->char_length) end = str->char_length;

        // Convert codepoint positions to byte offsets
        int start_byte = utf8_byte_offset(str->data, str->length, start);
        int end_byte = utf8_byte_offset(str->data, str->length, end);
        int byte_length = end_byte - start_byte;

        // Create new string
        char *new_data = malloc(byte_length + 1);
        memcpy(new_data, str->data + start_byte, byte_length);
        new_data[byte_length] = '\0';

        return val_string_take(new_data, byte_length, byte_length + 1);
    }

    // find(needle) - find first occurrence, returns index or -1
    if (strcmp(method, "find") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "find() expects 1 argument (substring)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "find() argument must be a string");
        }

        String *needle = args[0].as.as_string;
        if (needle->length == 0) {
            return val_i32(0);  // Empty string found at position 0
        }
        if (needle->length > str->length) {
            return val_i32(-1);  // Needle longer than haystack
        }

        // Search for needle in haystack
        for (int i = 0; i <= str->length - needle->length; i++) {
            if (memcmp(str->data + i, needle->data, needle->length) == 0) {
                return val_i32(i);
            }
        }
        return val_i32(-1);  // Not found
    }

    // contains(needle) - check if string contains substring
    if (strcmp(method, "contains") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "contains() expects 1 argument (substring)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "contains() argument must be a string");
        }

        String *needle = args[0].as.as_string;
        if (needle->length == 0) {
            return val_bool(1);  // Empty string is always contained
        }
        if (needle->length > str->length) {
            return val_bool(0);
        }

        // Search for needle
        for (int i = 0; i <= str->length - needle->length; i++) {
            if (memcmp(str->data + i, needle->data, needle->length) == 0) {
                return val_bool(1);
            }
        }
        return val_bool(0);
    }

    // split(delimiter) - split string into array
    if (strcmp(method, "split") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "split() expects 1 argument (delimiter)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "split() delimiter must be a string");
        }

        String *delim = args[0].as.as_string;
        Array *result = array_new();

        if (delim->length == 0) {
            // Empty delimiter: split into individual characters
            for (int i = 0; i < str->length; i++) {
                char *char_str = malloc(2);
                char_str[0] = str->data[i];
                char_str[1] = '\0';
                array_push(result, val_string_take(char_str, 1, 2));
            }
            return val_array(result);
        }

        // Split by delimiter
        int start = 0;
        for (int i = 0; i <= str->length - delim->length; i++) {
            if (memcmp(str->data + i, delim->data, delim->length) == 0) {
                // Found delimiter, extract substring
                int len = i - start;
                char *part = malloc(len + 1);
                memcpy(part, str->data + start, len);
                part[len] = '\0';
                array_push(result, val_string_take(part, len, len + 1));
                i += delim->length - 1;  // Skip past delimiter
                start = i + 1;
            }
        }

        // Add remaining part
        int len = str->length - start;
        char *part = malloc(len + 1);
        memcpy(part, str->data + start, len);
        part[len] = '\0';
        array_push(result, val_string_take(part, len, len + 1));

        return val_array(result);
    }

    // trim() - remove whitespace from both ends
    if (strcmp(method, "trim") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "trim() expects no arguments");
        }

        int start = 0;
        int end = str->length - 1;

        // Find first non-whitespace
        while (start < str->length && (str->data[start] == ' ' || str->data[start] == '\t' ||
                                       str->data[start] == '\n' || str->data[start] == '\r')) {
            start++;
        }

        // Find last non-whitespace
        while (end >= start && (str->data[end] == ' ' || str->data[end] == '\t' ||
                                str->data[end] == '\n' || str->data[end] == '\r')) {
            end--;
        }

        int len = end - start + 1;
        if (len <= 0) {
            return val_string("");  // All whitespace
        }

        char *trimmed = malloc(len + 1);
        memcpy(trimmed, str->data + start, len);
        trimmed[len] = '\0';

        return val_string_take(trimmed, len, len + 1);
    }

    // to_upper() - convert to uppercase
    if (strcmp(method, "to_upper") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "to_upper() expects no arguments");
        }

        char *upper = malloc(str->length + 1);
        for (int i = 0; i < str->length; i++) {
            char c = str->data[i];
            if (c >= 'a' && c <= 'z') {
                upper[i] = c - 32;  // Convert to uppercase
            } else {
                upper[i] = c;
            }
        }
        upper[str->length] = '\0';

        return val_string_take(upper, str->length, str->length + 1);
    }

    // to_lower() - convert to lowercase
    if (strcmp(method, "to_lower") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "to_lower() expects no arguments");
        }

        char *lower = malloc(str->length + 1);
        for (int i = 0; i < str->length; i++) {
            char c = str->data[i];
            if (c >= 'A' && c <= 'Z') {
                lower[i] = c + 32;  // Convert to lowercase
            } else {
                lower[i] = c;
            }
        }
        lower[str->length] = '\0';

        return val_string_take(lower, str->length, str->length + 1);
    }

    // starts_with(prefix) - check if string starts with prefix
    if (strcmp(method, "starts_with") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "starts_with() expects 1 argument (prefix)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "starts_with() argument must be a string");
        }

        String *prefix = args[0].as.as_string;
        if (prefix->length > str->length) {
            return val_bool(0);
        }
        return val_bool(memcmp(str->data, prefix->data, prefix->length) == 0);
    }

    // ends_with(suffix) - check if string ends with suffix
    if (strcmp(method, "ends_with") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "ends_with() expects 1 argument (suffix)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "ends_with() argument must be a string");
        }

        String *suffix = args[0].as.as_string;
        if (suffix->length > str->length) {
            return val_bool(0);
        }
        int offset = str->length - suffix->length;
        return val_bool(memcmp(str->data + offset, suffix->data, suffix->length) == 0);
    }

    // replace(old, new) - replace first occurrence
    if (strcmp(method, "replace") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "replace() expects 2 arguments (old, new)");
        }
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
            return throw_runtime_error(ctx, "replace() arguments must be strings");
        }

        String *old = args[0].as.as_string;
        String *new = args[1].as.as_string;

        // Find first occurrence
        int pos = -1;
        for (int i = 0; i <= str->length - old->length; i++) {
            if (memcmp(str->data + i, old->data, old->length) == 0) {
                pos = i;
                break;
            }
        }

        // If not found, return original string
        if (pos == -1) {
            return val_string(str->data);
        }

        // Build new string with replacement
        int new_len = str->length - old->length + new->length;
        char *result = malloc(new_len + 1);

        memcpy(result, str->data, pos);
        memcpy(result + pos, new->data, new->length);
        memcpy(result + pos + new->length, str->data + pos + old->length, str->length - pos - old->length);
        result[new_len] = '\0';

        return val_string_take(result, new_len, new_len + 1);
    }

    // replace_all(old, new) - replace all occurrences
    if (strcmp(method, "replace_all") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "replace_all() expects 2 arguments (old, new)");
        }
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
            return throw_runtime_error(ctx, "replace_all() arguments must be strings");
        }

        String *old = args[0].as.as_string;
        String *new = args[1].as.as_string;

        if (old->length == 0) {
            return val_string(str->data);  // Cannot replace empty string
        }

        // Count occurrences
        int count = 0;
        for (int i = 0; i <= str->length - old->length; i++) {
            if (memcmp(str->data + i, old->data, old->length) == 0) {
                count++;
                i += old->length - 1;  // Skip past this occurrence
            }
        }

        if (count == 0) {
            return val_string(str->data);  // No replacements needed
        }

        // Build new string
        int new_len = str->length - (count * old->length) + (count * new->length);
        char *result = malloc(new_len + 1);
        int result_pos = 0;

        for (int i = 0; i < str->length; ) {
            // Check for match
            if (i <= str->length - old->length && memcmp(str->data + i, old->data, old->length) == 0) {
                memcpy(result + result_pos, new->data, new->length);
                result_pos += new->length;
                i += old->length;
            } else {
                result[result_pos++] = str->data[i++];
            }
        }
        result[new_len] = '\0';

        return val_string_take(result, new_len, new_len + 1);
    }

    // repeat(count) - repeat string n times
    if (strcmp(method, "repeat") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "repeat() expects 1 argument (count)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "repeat() count must be an integer");
        }

        int32_t count = value_to_int(args[0]);
        if (count < 0) {
            return throw_runtime_error(ctx, "repeat() count cannot be negative");
        }
        if (count == 0) {
            return val_string("");
        }

        int new_len = str->length * count;
        char *result = malloc(new_len + 1);

        for (int i = 0; i < count; i++) {
            memcpy(result + (i * str->length), str->data, str->length);
        }
        result[new_len] = '\0';

        return val_string_take(result, new_len, new_len + 1);
    }

    // char_at(index) - get character at index (returns rune)
    if (strcmp(method, "char_at") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "char_at() expects 1 argument (index)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "char_at() index must be an integer");
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= str->char_length) {
            return throw_runtime_error(ctx, "char_at() index %d out of bounds (length=%d)",
                    index, str->char_length);
        }

        // Find byte offset and decode codepoint
        int byte_pos = utf8_byte_offset(str->data, str->length, index);
        uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

        return val_rune(codepoint);
    }

    // byte_at(index) - get byte at index (returns u8)
    if (strcmp(method, "byte_at") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "byte_at() expects 1 argument (index)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "byte_at() index must be an integer");
        }

        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= str->length) {
            return throw_runtime_error(ctx, "byte_at() index %d out of bounds (byte_length=%d)",
                    index, str->length);
        }

        return val_u8((uint8_t)str->data[index]);
    }

    // chars() - convert string to array of runes
    if (strcmp(method, "chars") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "chars() expects no arguments");
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        Array *arr = array_new();
        int byte_pos = 0;

        while (byte_pos < str->length) {
            uint32_t codepoint = utf8_decode_at(str->data, byte_pos);
            array_push(arr, val_rune(codepoint));

            // Move to next character
            byte_pos += utf8_char_byte_length((unsigned char)str->data[byte_pos]);
        }

        return val_array(arr);
    }

    // bytes() - convert string to array of bytes (u8)
    if (strcmp(method, "bytes") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "bytes() expects no arguments");
        }

        Array *arr = array_new();
        for (int i = 0; i < str->length; i++) {
            array_push(arr, val_u8((uint8_t)str->data[i]));
        }

        return val_array(arr);
    }

    // to_bytes() - convert string to buffer
    if (strcmp(method, "to_bytes") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "to_bytes() expects no arguments");
        }

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = malloc(str->length);
        memcpy(buf->data, str->data, str->length);
        buf->length = str->length;
        buf->capacity = str->length;
        buf->ref_count = 1;  // Start with 1 - caller owns the first reference

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    // deserialize() - parse JSON string to value
    if (strcmp(method, "deserialize") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "deserialize() expects no arguments");
        }

        JSONParser parser;
        parser.input = str->data;
        parser.pos = 0;

        Value result = json_parse_value(&parser, ctx);

        // Check if parsing threw an error
        if (ctx->exception_state.is_throwing) {
            return val_null();  // Error already set in ctx
        }

        // Check that we consumed all the input
        json_skip_whitespace(&parser);
        if (parser.input[parser.pos] != '\0') {
            return throw_runtime_error(ctx, "Unexpected trailing characters in JSON");
        }

        return result;
    }

    return throw_runtime_error(ctx, "String has no method '%s'", method);
}

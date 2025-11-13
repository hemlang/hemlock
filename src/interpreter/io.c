#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

// ========== FILE METHOD HANDLING ==========

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args) {
    if (file->closed) {
        fprintf(stderr, "Runtime error: Cannot call method on closed file\n");
        exit(1);
    }

    if (strcmp(method, "read_text") == 0) {
        // Read up to N bytes as string
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_text() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        char *buffer = malloc(size + 1);
        size_t read = fread(buffer, 1, size, file->fp);
        buffer[read] = '\0';

        String *str = malloc(sizeof(String));
        str->data = buffer;
        str->length = read;
        str->capacity = size + 1;

        return (Value){ .type = VAL_STRING, .as.as_string = str };
    }

    if (strcmp(method, "read_bytes") == 0) {
        // Read up to N bytes as buffer
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_bytes() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        void *data = malloc(size);
        size_t read = fread(data, 1, size, file->fp);

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = data;
        buf->length = read;
        buf->capacity = size;

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    if (strcmp(method, "write") == 0) {
        // Write string or buffer
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: write() expects 1 argument (data)\n");
            exit(1);
        }

        size_t written = 0;
        if (args[0].type == VAL_STRING) {
            String *str = args[0].as.as_string;
            written = fwrite(str->data, 1, str->length, file->fp);
        } else if (args[0].type == VAL_BUFFER) {
            Buffer *buf = args[0].as.as_buffer;
            written = fwrite(buf->data, 1, buf->length, file->fp);
        } else {
            fprintf(stderr, "Runtime error: write() expects string or buffer\n");
            exit(1);
        }

        return val_i32((int32_t)written);
    }

    if (strcmp(method, "seek") == 0) {
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: seek() expects 1 integer argument (offset)\n");
            exit(1);
        }

        int offset = value_to_int(args[0]);
        fseek(file->fp, offset, SEEK_SET);
        return val_null();
    }

    if (strcmp(method, "tell") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: tell() expects no arguments\n");
            exit(1);
        }

        long pos = ftell(file->fp);
        return val_i32((int32_t)pos);
    }

    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: close() expects no arguments\n");
            exit(1);
        }

        if (file->fp) {
            fclose(file->fp);
            file->fp = NULL;
        }
        file->closed = 1;
        return val_null();
    }

    fprintf(stderr, "Runtime error: File has no method '%s'\n", method);
    exit(1);
}

// ========== ARRAY METHOD HANDLING ==========

// Helper: Compare two values for equality
int values_equal(Value a, Value b) {
    if (a.type != b.type) {
        return 0;
    }

    switch (a.type) {
        case VAL_I8: return a.as.as_i8 == b.as.as_i8;
        case VAL_I16: return a.as.as_i16 == b.as.as_i16;
        case VAL_I32: return a.as.as_i32 == b.as.as_i32;
        case VAL_U8: return a.as.as_u8 == b.as.as_u8;
        case VAL_U16: return a.as.as_u16 == b.as.as_u16;
        case VAL_U32: return a.as.as_u32 == b.as.as_u32;
        case VAL_F32: return a.as.as_f32 == b.as.as_f32;
        case VAL_F64: return a.as.as_f64 == b.as.as_f64;
        case VAL_BOOL: return a.as.as_bool == b.as.as_bool;
        case VAL_STRING:
            return strcmp(a.as.as_string->data, b.as.as_string->data) == 0;
        case VAL_PTR: return a.as.as_ptr == b.as.as_ptr;
        case VAL_NULL: return 1;
        default: return 0;  // Objects, arrays, functions compared by reference
    }
}

Value call_array_method(Array *arr, const char *method, Value *args, int num_args) {
    // push(value) - add element to end
    if (strcmp(method, "push") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: push() expects 1 argument\n");
            exit(1);
        }
        array_push(arr, args[0]);
        return val_null();
    }

    // pop() - remove and return last element
    if (strcmp(method, "pop") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: pop() expects no arguments\n");
            exit(1);
        }
        return array_pop(arr);
    }

    // shift() - remove and return first element
    if (strcmp(method, "shift") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: shift() expects no arguments\n");
            exit(1);
        }
        if (arr->length == 0) {
            return val_null();
        }
        Value first = arr->elements[0];
        // Shift all elements left
        for (int i = 1; i < arr->length; i++) {
            arr->elements[i - 1] = arr->elements[i];
        }
        arr->length--;
        return first;
    }

    // unshift(value) - add element to beginning
    if (strcmp(method, "unshift") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: unshift() expects 1 argument\n");
            exit(1);
        }
        // Ensure capacity
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        // Shift all elements right
        for (int i = arr->length; i > 0; i--) {
            arr->elements[i] = arr->elements[i - 1];
        }
        arr->elements[0] = args[0];
        arr->length++;
        return val_null();
    }

    // insert(index, value) - insert element at index
    if (strcmp(method, "insert") == 0) {
        if (num_args != 2) {
            fprintf(stderr, "Runtime error: insert() expects 2 arguments (index, value)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: insert() index must be an integer\n");
            exit(1);
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index > arr->length) {
            fprintf(stderr, "Runtime error: insert() index out of bounds\n");
            exit(1);
        }
        // Ensure capacity
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        // Shift elements right from index
        for (int i = arr->length; i > index; i--) {
            arr->elements[i] = arr->elements[i - 1];
        }
        arr->elements[index] = args[1];
        arr->length++;
        return val_null();
    }

    // remove(index) - remove and return element at index
    if (strcmp(method, "remove") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: remove() expects 1 argument (index)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: remove() index must be an integer\n");
            exit(1);
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= arr->length) {
            fprintf(stderr, "Runtime error: remove() index out of bounds\n");
            exit(1);
        }
        Value removed = arr->elements[index];
        // Shift elements left from index
        for (int i = index; i < arr->length - 1; i++) {
            arr->elements[i] = arr->elements[i + 1];
        }
        arr->length--;
        return removed;
    }

    // find(value) - find first occurrence, return index or -1
    if (strcmp(method, "find") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: find() expects 1 argument (value)\n");
            exit(1);
        }
        for (int i = 0; i < arr->length; i++) {
            if (values_equal(arr->elements[i], args[0])) {
                return val_i32(i);
            }
        }
        return val_i32(-1);
    }

    // contains(value) - check if array contains value
    if (strcmp(method, "contains") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: contains() expects 1 argument (value)\n");
            exit(1);
        }
        for (int i = 0; i < arr->length; i++) {
            if (values_equal(arr->elements[i], args[0])) {
                return val_bool(1);
            }
        }
        return val_bool(0);
    }

    // slice(start, end) - extract subarray (end is exclusive)
    if (strcmp(method, "slice") == 0) {
        if (num_args != 2) {
            fprintf(stderr, "Runtime error: slice() expects 2 arguments (start, end)\n");
            exit(1);
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            fprintf(stderr, "Runtime error: slice() arguments must be integers\n");
            exit(1);
        }
        int32_t start = value_to_int(args[0]);
        int32_t end = value_to_int(args[1]);

        if (start < 0 || start > arr->length) {
            fprintf(stderr, "Runtime error: slice() start index out of bounds\n");
            exit(1);
        }
        if (end < start || end > arr->length) {
            fprintf(stderr, "Runtime error: slice() end index out of bounds\n");
            exit(1);
        }

        Array *result = array_new();
        for (int i = start; i < end; i++) {
            array_push(result, arr->elements[i]);
        }
        return val_array(result);
    }

    // join(delimiter) - join array elements into string
    if (strcmp(method, "join") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: join() expects 1 argument (delimiter)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: join() delimiter must be a string\n");
            exit(1);
        }
        String *delim = args[0].as.as_string;

        if (arr->length == 0) {
            return val_string("");
        }

        // Calculate total size needed
        size_t total_len = 0;
        for (int i = 0; i < arr->length; i++) {
            if (arr->elements[i].type == VAL_STRING) {
                total_len += arr->elements[i].as.as_string->length;
            } else {
                // For non-strings, estimate size (we'll use sprintf later)
                total_len += 32;  // Generous estimate for numbers
            }
            if (i < arr->length - 1) {
                total_len += delim->length;
            }
        }

        char *result = malloc(total_len + 1);
        if (!result) {
            fprintf(stderr, "Runtime error: Memory allocation failed in join()\n");
            exit(1);
        }
        size_t pos = 0;

        for (int i = 0; i < arr->length; i++) {
            // Convert element to string
            size_t remaining = total_len + 1 - pos;

            if (arr->elements[i].type == VAL_STRING) {
                String *s = arr->elements[i].as.as_string;
                memcpy(result + pos, s->data, s->length);
                pos += s->length;
            } else if (arr->elements[i].type == VAL_I8) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i8);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_I16) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i16);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_I32) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U8) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u8);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U16) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u16);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U32) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_F32) {
                int written = snprintf(result + pos, remaining, "%g", arr->elements[i].as.as_f32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_F64) {
                int written = snprintf(result + pos, remaining, "%g", arr->elements[i].as.as_f64);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_BOOL) {
                const char *s = arr->elements[i].as.as_bool ? "true" : "false";
                size_t len = strlen(s);
                memcpy(result + pos, s, len);
                pos += len;
            } else if (arr->elements[i].type == VAL_NULL) {
                memcpy(result + pos, "null", 4);
                pos += 4;
            } else {
                memcpy(result + pos, "[object]", 8);
                pos += 8;
            }

            // Add delimiter between elements
            if (i < arr->length - 1) {
                memcpy(result + pos, delim->data, delim->length);
                pos += delim->length;
            }
        }
        result[pos] = '\0';

        return val_string_take(result, pos, total_len + 1);
    }

    // concat(other) - concatenate arrays (returns new array)
    if (strcmp(method, "concat") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: concat() expects 1 argument (array)\n");
            exit(1);
        }
        if (args[0].type != VAL_ARRAY) {
            fprintf(stderr, "Runtime error: concat() argument must be an array\n");
            exit(1);
        }
        Array *other = args[0].as.as_array;
        Array *result = array_new();

        // Copy elements from first array
        for (int i = 0; i < arr->length; i++) {
            array_push(result, arr->elements[i]);
        }
        // Copy elements from second array
        for (int i = 0; i < other->length; i++) {
            array_push(result, other->elements[i]);
        }
        return val_array(result);
    }

    // reverse() - reverse array in-place
    if (strcmp(method, "reverse") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: reverse() expects no arguments\n");
            exit(1);
        }
        int left = 0;
        int right = arr->length - 1;
        while (left < right) {
            Value temp = arr->elements[left];
            arr->elements[left] = arr->elements[right];
            arr->elements[right] = temp;
            left++;
            right--;
        }
        return val_null();
    }

    // first() - get first element
    if (strcmp(method, "first") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: first() expects no arguments\n");
            exit(1);
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[0];
    }

    // last() - get last element
    if (strcmp(method, "last") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: last() expects no arguments\n");
            exit(1);
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[arr->length - 1];
    }

    // clear() - remove all elements
    if (strcmp(method, "clear") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: clear() expects no arguments\n");
            exit(1);
        }
        arr->length = 0;
        return val_null();
    }

    fprintf(stderr, "Runtime error: Array has no method '%s'\n", method);
    exit(1);
}

// ========== STRING METHOD HANDLING ==========

Value call_string_method(String *str, const char *method, Value *args, int num_args) {
    // substr(start, length) - extract substring by codepoint positions
    if (strcmp(method, "substr") == 0) {
        if (num_args != 2) {
            fprintf(stderr, "Runtime error: substr() expects 2 arguments (start, length)\n");
            exit(1);
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            fprintf(stderr, "Runtime error: substr() arguments must be integers\n");
            exit(1);
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t start = value_to_int(args[0]);
        int32_t char_length = value_to_int(args[1]);

        // Bounds checking (now on codepoint positions)
        if (start < 0 || start >= str->char_length) {
            fprintf(stderr, "Runtime error: substr() start index %d out of bounds (length=%d)\n",
                    start, str->char_length);
            exit(1);
        }
        if (char_length < 0) {
            fprintf(stderr, "Runtime error: substr() length cannot be negative\n");
            exit(1);
        }

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
            fprintf(stderr, "Runtime error: slice() expects 2 arguments (start, end)\n");
            exit(1);
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            fprintf(stderr, "Runtime error: slice() arguments must be integers\n");
            exit(1);
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t start = value_to_int(args[0]);
        int32_t end = value_to_int(args[1]);

        // Bounds checking (now on codepoint positions)
        if (start < 0 || start > str->char_length) {
            fprintf(stderr, "Runtime error: slice() start index %d out of bounds (length=%d)\n",
                    start, str->char_length);
            exit(1);
        }
        if (end < start || end > str->char_length) {
            fprintf(stderr, "Runtime error: slice() end index %d out of bounds (length=%d)\n",
                    end, str->char_length);
            exit(1);
        }

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
            fprintf(stderr, "Runtime error: find() expects 1 argument (substring)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: find() argument must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: contains() expects 1 argument (substring)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: contains() argument must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: split() expects 1 argument (delimiter)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: split() delimiter must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: trim() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: to_upper() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: to_lower() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: starts_with() expects 1 argument (prefix)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: starts_with() argument must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: ends_with() expects 1 argument (suffix)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: ends_with() argument must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: replace() expects 2 arguments (old, new)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: replace() arguments must be strings\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: replace_all() expects 2 arguments (old, new)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: replace_all() arguments must be strings\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: repeat() expects 1 argument (count)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: repeat() count must be an integer\n");
            exit(1);
        }

        int32_t count = value_to_int(args[0]);
        if (count < 0) {
            fprintf(stderr, "Runtime error: repeat() count cannot be negative\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: char_at() expects 1 argument (index)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: char_at() index must be an integer\n");
            exit(1);
        }

        // Compute character length if not cached
        if (str->char_length < 0) {
            str->char_length = utf8_count_codepoints(str->data, str->length);
        }

        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= str->char_length) {
            fprintf(stderr, "Runtime error: char_at() index %d out of bounds (length=%d)\n",
                    index, str->char_length);
            exit(1);
        }

        // Find byte offset and decode codepoint
        int byte_pos = utf8_byte_offset(str->data, str->length, index);
        uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

        return val_rune(codepoint);
    }

    // byte_at(index) - get byte at index (returns u8)
    if (strcmp(method, "byte_at") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: byte_at() expects 1 argument (index)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: byte_at() index must be an integer\n");
            exit(1);
        }

        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= str->length) {
            fprintf(stderr, "Runtime error: byte_at() index %d out of bounds (byte_length=%d)\n",
                    index, str->length);
            exit(1);
        }

        return val_u8((uint8_t)str->data[index]);
    }

    // chars() - convert string to array of runes
    if (strcmp(method, "chars") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: chars() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: bytes() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: to_bytes() expects no arguments\n");
            exit(1);
        }

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = malloc(str->length);
        memcpy(buf->data, str->data, str->length);
        buf->length = str->length;
        buf->capacity = str->length;

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    fprintf(stderr, "Runtime error: String has no method '%s'\n", method);
    exit(1);
}

// ========== I/O BUILTIN FUNCTIONS ==========

Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_file() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file
    char *buffer = malloc(size + 1);
    size_t read = fread(buffer, 1, size, fp);
    buffer[read] = '\0';
    fclose(fp);

    String *str = malloc(sizeof(String));
    str->data = buffer;
    str->length = read;
    str->capacity = size + 1;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: write_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: append_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: append_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "ab");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: append_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

Value builtin_read_bytes(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_bytes() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file into buffer
    void *data = malloc(size);
    size_t read = fread(data, 1, size, fp);
    fclose(fp);

    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = data;
    buf->length = read;
    buf->capacity = size;

    return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
}

Value builtin_write_bytes(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_bytes() expects 2 arguments (path, data)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_bytes() path must be a string\n");
        exit(1);
    }

    if (args[1].type != VAL_BUFFER) {
        fprintf(stderr, "Runtime error: write_bytes() data must be a buffer\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    Buffer *buf = args[1].as.as_buffer;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    fwrite(buf->data, 1, buf->length, fp);
    fclose(fp);
    return val_null();
}

Value builtin_file_exists(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: file_exists() expects 1 string argument\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return val_bool(1);
    }
    return val_bool(0);
}

Value builtin_read_line(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: read_line() expects no arguments\n");
        exit(1);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, stdin);

    if (read == -1) {
        free(line);
        return val_null();  // EOF
    }

    // Strip newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
        read--;
    }
    if (read > 0 && line[read - 1] == '\r') {
        line[read - 1] = '\0';
        read--;
    }

    String *str = malloc(sizeof(String));
    str->data = line;
    str->length = read;
    str->capacity = len;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

Value builtin_eprint(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: eprint() expects 1 argument\n");
        exit(1);
    }

    // Print to stderr
    switch (args[0].type) {
        case VAL_I8:
            fprintf(stderr, "%d", args[0].as.as_i8);
            break;
        case VAL_I16:
            fprintf(stderr, "%d", args[0].as.as_i16);
            break;
        case VAL_I32:
            fprintf(stderr, "%d", args[0].as.as_i32);
            break;
        case VAL_U8:
            fprintf(stderr, "%u", args[0].as.as_u8);
            break;
        case VAL_U16:
            fprintf(stderr, "%u", args[0].as.as_u16);
            break;
        case VAL_U32:
            fprintf(stderr, "%u", args[0].as.as_u32);
            break;
        case VAL_F32:
            fprintf(stderr, "%g", args[0].as.as_f32);
            break;
        case VAL_F64:
            fprintf(stderr, "%g", args[0].as.as_f64);
            break;
        case VAL_BOOL:
            fprintf(stderr, "%s", args[0].as.as_bool ? "true" : "false");
            break;
        case VAL_STRING:
            fprintf(stderr, "%s", args[0].as.as_string->data);
            break;
        case VAL_NULL:
            fprintf(stderr, "null");
            break;
        default:
            // For complex types, use simpler representation
            fprintf(stderr, "<value>");
            break;
    }
    fprintf(stderr, "\n");
    return val_null();
}

Value builtin_open(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: open() expects 1-2 arguments (path, [mode])\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: open() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    const char *mode = "r";  // Default mode

    if (num_args == 2) {
        if (args[1].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: open() mode must be a string\n");
            exit(1);
        }
        mode = args[1].as.as_string->data;
    }

    FILE *fp = fopen(path, mode);
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s' with mode '%s': %s\n",
                path, mode, strerror(errno));
        exit(1);
    }

    FileHandle *file = malloc(sizeof(FileHandle));
    file->fp = fp;
    file->path = strdup(path);
    file->mode = strdup(mode);
    file->closed = 0;

    return val_file(file);
}

// ========== CHANNEL METHODS ==========

Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args) {
    pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;
    pthread_cond_t *not_empty = (pthread_cond_t*)ch->not_empty;
    pthread_cond_t *not_full = (pthread_cond_t*)ch->not_full;

    // send(value) - send a message to the channel
    if (strcmp(method, "send") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: send() expects 1 argument\n");
            exit(1);
        }

        Value msg = args[0];

        pthread_mutex_lock(mutex);

        // Check if channel is closed
        if (ch->closed) {
            pthread_mutex_unlock(mutex);
            fprintf(stderr, "Runtime error: cannot send to closed channel\n");
            exit(1);
        }

        if (ch->capacity == 0) {
            // Unbuffered channel - would need rendezvous
            pthread_mutex_unlock(mutex);
            fprintf(stderr, "Runtime error: unbuffered channels not yet supported (use buffered channel)\n");
            exit(1);
        }

        // Wait while buffer is full
        while (ch->count >= ch->capacity && !ch->closed) {
            pthread_cond_wait(not_full, mutex);
        }

        // Check again if closed after waking up
        if (ch->closed) {
            pthread_mutex_unlock(mutex);
            fprintf(stderr, "Runtime error: cannot send to closed channel\n");
            exit(1);
        }

        // Add message to buffer
        ch->buffer[ch->tail] = msg;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;

        // Signal that buffer is not empty
        pthread_cond_signal(not_empty);
        pthread_mutex_unlock(mutex);

        return val_null();
    }

    // recv() - receive a message from the channel
    if (strcmp(method, "recv") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: recv() expects 0 arguments\n");
            exit(1);
        }

        pthread_mutex_lock(mutex);

        // Wait while buffer is empty and channel not closed
        while (ch->count == 0 && !ch->closed) {
            pthread_cond_wait(not_empty, mutex);
        }

        // If channel is closed and empty, return null
        if (ch->count == 0 && ch->closed) {
            pthread_mutex_unlock(mutex);
            return val_null();
        }

        // Get message from buffer
        Value msg = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        // Signal that buffer is not full
        pthread_cond_signal(not_full);
        pthread_mutex_unlock(mutex);

        return msg;
    }

    // close() - close the channel
    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: close() expects 0 arguments\n");
            exit(1);
        }

        pthread_mutex_lock(mutex);
        ch->closed = 1;
        // Wake up all waiting threads
        pthread_cond_broadcast(not_empty);
        pthread_cond_broadcast(not_full);
        pthread_mutex_unlock(mutex);

        return val_null();
    }

    fprintf(stderr, "Runtime error: Unknown channel method '%s'\n", method);
    exit(1);
}

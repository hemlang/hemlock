#include "internal.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <inttypes.h>

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

// ========== OPTIMIZED SERIALIZATION ==========

// JSON StringBuilder - accumulates output in a single growing buffer
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} JsonBuffer;

static inline void jbuf_init(JsonBuffer *buf, size_t initial_capacity) {
    buf->capacity = initial_capacity > 64 ? initial_capacity : 64;
    buf->data = malloc(buf->capacity);
    buf->len = 0;
}

static inline void jbuf_ensure(JsonBuffer *buf, size_t additional) {
    size_t needed = buf->len + additional;
    if (needed > buf->capacity) {
        // Grow by at least 2x or to fit needed
        size_t new_cap = buf->capacity * 2;
        if (new_cap < needed) new_cap = needed;
        buf->data = realloc(buf->data, new_cap);
        buf->capacity = new_cap;
    }
}

static inline void jbuf_append_char(JsonBuffer *buf, char c) {
    jbuf_ensure(buf, 1);
    buf->data[buf->len++] = c;
}

static inline void jbuf_append_str(JsonBuffer *buf, const char *s, size_t len) {
    jbuf_ensure(buf, len);
    memcpy(buf->data + buf->len, s, len);
    buf->len += len;
}

// Fast integer to string - write directly to buffer
static inline void jbuf_append_i64(JsonBuffer *buf, int64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);
    int negative = val < 0;
    if (negative) val = -val;

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    if (negative) *--p = '-';

    jbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

static inline void jbuf_append_u64(JsonBuffer *buf, uint64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    jbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

// Append escaped JSON string directly to buffer (single pass)
static inline void jbuf_append_escaped_string(JsonBuffer *buf, const char *str, size_t str_len) {
    jbuf_append_char(buf, '"');

    // Reserve worst-case space (every char escaped = 2x)
    jbuf_ensure(buf, str_len * 2);

    const char *end = str + str_len;
    while (str < end) {
        unsigned char c = *str++;
        switch (c) {
            case '"':  buf->data[buf->len++] = '\\'; buf->data[buf->len++] = '"'; break;
            case '\\': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = '\\'; break;
            case '\n': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'n'; break;
            case '\r': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'r'; break;
            case '\t': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 't'; break;
            case '\b': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'b'; break;
            case '\f': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'f'; break;
            default:
                // Control characters (0x00-0x1F) need \uXXXX encoding
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    jbuf_ensure(buf, 6);
                    buf->data[buf->len++] = '\\';
                    buf->data[buf->len++] = 'u';
                    buf->data[buf->len++] = '0';
                    buf->data[buf->len++] = '0';
                    buf->data[buf->len++] = hex[c >> 4];
                    buf->data[buf->len++] = hex[c & 0xF];
                } else {
                    buf->data[buf->len++] = c;
                }
                break;
        }
    }

    jbuf_append_char(buf, '"');
}

// Forward declaration for recursive serialization
static int serialize_to_buffer(Value val, JsonBuffer *buf, VisitedSet *visited, ExecutionContext *ctx);

// Recursive serialization that writes directly to buffer
static int serialize_to_buffer(Value val, JsonBuffer *buf, VisitedSet *visited, ExecutionContext *ctx) {
    char tmp[32];

    switch (val.type) {
        case VAL_I8:
            jbuf_append_i64(buf, val.as.as_i8);
            return 1;
        case VAL_I16:
            jbuf_append_i64(buf, val.as.as_i16);
            return 1;
        case VAL_I32:
            jbuf_append_i64(buf, val.as.as_i32);
            return 1;
        case VAL_I64:
            jbuf_append_i64(buf, val.as.as_i64);
            return 1;
        case VAL_U8:
            jbuf_append_u64(buf, val.as.as_u8);
            return 1;
        case VAL_U16:
            jbuf_append_u64(buf, val.as.as_u16);
            return 1;
        case VAL_U32:
            jbuf_append_u64(buf, val.as.as_u32);
            return 1;
        case VAL_U64:
            jbuf_append_u64(buf, val.as.as_u64);
            return 1;
        case VAL_F32: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f32);
            jbuf_append_str(buf, tmp, len);
            return 1;
        }
        case VAL_F64: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f64);
            jbuf_append_str(buf, tmp, len);
            return 1;
        }
        case VAL_BOOL:
            if (val.as.as_bool) {
                jbuf_append_str(buf, "true", 4);
            } else {
                jbuf_append_str(buf, "false", 5);
            }
            return 1;
        case VAL_STRING: {
            String *s = val.as.as_string;
            jbuf_append_escaped_string(buf, s->data, s->length);
            return 1;
        }
        case VAL_NULL:
            jbuf_append_str(buf, "null", 4);
            return 1;
        case VAL_OBJECT: {
            Object *obj = val.as.as_object;

            // Check for cycles
            if (visited_contains(visited, obj)) {
                throw_runtime_error(ctx, "serialize() detected circular reference");
                return 0;
            }
            visited_add(visited, obj);

            jbuf_append_char(buf, '{');

            for (int i = 0; i < obj->num_fields; i++) {
                if (i > 0) jbuf_append_char(buf, ',');

                // Field name (escaped)
                const char *name = obj->field_names[i];
                jbuf_append_escaped_string(buf, name, strlen(name));
                jbuf_append_char(buf, ':');

                // Field value
                if (!serialize_to_buffer(obj->field_values[i], buf, visited, ctx)) {
                    return 0;
                }
            }

            jbuf_append_char(buf, '}');
            return 1;
        }
        case VAL_ARRAY: {
            Array *arr = val.as.as_array;

            // Check for cycles
            if (visited_contains(visited, (Object*)arr)) {
                throw_runtime_error(ctx, "serialize() detected circular reference");
                return 0;
            }
            visited_add(visited, (Object*)arr);

            jbuf_append_char(buf, '[');

            for (int i = 0; i < arr->length; i++) {
                if (i > 0) jbuf_append_char(buf, ',');

                if (!serialize_to_buffer(arr->elements[i], buf, visited, ctx)) {
                    return 0;
                }
            }

            jbuf_append_char(buf, ']');
            return 1;
        }
        default:
            throw_runtime_error(ctx, "Cannot serialize value of this type");
            return 0;
    }
}

// ========== LEGACY API (for compatibility) ==========

void visited_init(VisitedSet *set) {
    set->capacity = 16;
    set->count = 0;
    set->visited = malloc(sizeof(Object*) * set->capacity);
}

int visited_contains(VisitedSet *set, Object *obj) {
    for (int i = 0; i < set->count; i++) {
        if (set->visited[i] == obj) {
            return 1;
        }
    }
    return 0;
}

void visited_add(VisitedSet *set, Object *obj) {
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->visited = realloc(set->visited, sizeof(Object*) * set->capacity);
    }
    set->visited[set->count++] = obj;
}

void visited_free(VisitedSet *set) {
    free(set->visited);
}

// Helper to escape strings for JSON (legacy - kept for compatibility)
char* escape_json_string(const char *str) {
    int escape_count = 0;
    for (const char *p = str; *p; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') {
            escape_count++;
        }
    }

    int len = strlen(str);
    char *escaped = malloc(len + escape_count + 1);
    char *out = escaped;

    for (const char *p = str; *p; p++) {
        if (*p == '"') {
            *out++ = '\\';
            *out++ = '"';
        } else if (*p == '\\') {
            *out++ = '\\';
            *out++ = '\\';
        } else if (*p == '\n') {
            *out++ = '\\';
            *out++ = 'n';
        } else if (*p == '\r') {
            *out++ = '\\';
            *out++ = 'r';
        } else if (*p == '\t') {
            *out++ = '\\';
            *out++ = 't';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
    return escaped;
}

// Optimized serialize_value using the buffer-based approach
char* serialize_value(Value val, VisitedSet *visited, ExecutionContext *ctx) {
    JsonBuffer buf;
    jbuf_init(&buf, 256);

    if (!serialize_to_buffer(val, &buf, visited, ctx)) {
        free(buf.data);
        return NULL;
    }

    // Null-terminate
    jbuf_append_char(&buf, '\0');
    return buf.data;
}

// Optimized whitespace skip
void json_skip_whitespace(JSONParser *p) {
    const char *s = p->input + p->pos;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    p->pos = s - p->input;
}

// Optimized string parsing - scan first to get length, handle escapes efficiently
Value json_parse_string(JSONParser *p, ExecutionContext *ctx) {
    if (p->input[p->pos] != '"') {
        return throw_runtime_error(ctx, "Expected '\"' in JSON");
    }
    p->pos++;  // skip opening quote

    const char *start = p->input + p->pos;
    const char *s = start;

    // Fast path: scan for end quote and check if we need escape handling
    int has_escapes = 0;
    while (*s != '"' && *s != '\0') {
        if (*s == '\\') {
            has_escapes = 1;
            s++;
            if (*s) s++;
        } else {
            s++;
        }
    }

    if (*s != '"') {
        return throw_runtime_error(ctx, "Unterminated string in JSON");
    }

    size_t raw_len = s - start;

    if (!has_escapes) {
        // Fast path: no escapes, direct copy - use val_string_take to avoid double allocation
        char *buf = malloc(raw_len + 1);
        memcpy(buf, start, raw_len);
        buf[raw_len] = '\0';
        p->pos += raw_len + 1;  // +1 for closing quote
        return val_string_take(buf, raw_len, raw_len + 1);
    }

    // Slow path: handle escapes
    char *buf = malloc(raw_len + 1);  // Can't be larger than raw
    char *out = buf;
    s = start;

    while (*s != '"') {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case 'n': *out++ = '\n'; break;
                case 'r': *out++ = '\r'; break;
                case 't': *out++ = '\t'; break;
                case 'b': *out++ = '\b'; break;
                case 'f': *out++ = '\f'; break;
                case '"': *out++ = '"'; break;
                case '\\': *out++ = '\\'; break;
                case '/': *out++ = '/'; break;
                case 'u': {
                    // Unicode escape \uXXXX - simplified handling
                    s++;
                    if (s[0] && s[1] && s[2] && s[3]) {
                        // Parse 4 hex digits
                        int codepoint = 0;
                        for (int i = 0; i < 4; i++) {
                            char c = s[i];
                            codepoint <<= 4;
                            if (c >= '0' && c <= '9') codepoint |= c - '0';
                            else if (c >= 'a' && c <= 'f') codepoint |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') codepoint |= c - 'A' + 10;
                        }
                        // Simple ASCII range only for now
                        if (codepoint < 128) {
                            *out++ = (char)codepoint;
                        } else {
                            // UTF-8 encode
                            if (codepoint < 0x80) {
                                *out++ = codepoint;
                            } else if (codepoint < 0x800) {
                                *out++ = 0xC0 | (codepoint >> 6);
                                *out++ = 0x80 | (codepoint & 0x3F);
                            } else {
                                *out++ = 0xE0 | (codepoint >> 12);
                                *out++ = 0x80 | ((codepoint >> 6) & 0x3F);
                                *out++ = 0x80 | (codepoint & 0x3F);
                            }
                        }
                        s += 3;  // +1 more at end of loop
                    }
                    break;
                }
                default:
                    free(buf);
                    return throw_runtime_error(ctx, "Invalid escape sequence in JSON string");
            }
            s++;
        } else {
            *out++ = *s++;
        }
    }

    *out = '\0';
    p->pos = (s - p->input) + 1;  // +1 for closing quote

    // Use val_string_take to avoid double allocation
    return val_string_take(buf, out - buf, raw_len + 1);
}

// Optimized number parsing - parse directly without allocation
Value json_parse_number(JSONParser *p, ExecutionContext *ctx) {
    (void)ctx;
    const char *s = p->input + p->pos;
    int negative = 0;

    if (*s == '-') {
        negative = 1;
        s++;
    }

    // Parse integer part
    int64_t int_val = 0;
    while (*s >= '0' && *s <= '9') {
        int_val = int_val * 10 + (*s - '0');
        s++;
    }

    // Check for decimal
    if (*s == '.') {
        s++;
        double frac = 0.0;
        double divisor = 10.0;
        while (*s >= '0' && *s <= '9') {
            frac += (*s - '0') / divisor;
            divisor *= 10.0;
            s++;
        }

        // Handle exponent
        if (*s == 'e' || *s == 'E') {
            s++;
            int exp_negative = 0;
            if (*s == '-') { exp_negative = 1; s++; }
            else if (*s == '+') { s++; }

            int exp = 0;
            while (*s >= '0' && *s <= '9') {
                exp = exp * 10 + (*s - '0');
                s++;
            }

            double multiplier = 1.0;
            for (int i = 0; i < exp; i++) multiplier *= 10.0;
            if (exp_negative) {
                frac = (int_val + frac) / multiplier;
                int_val = 0;
            } else {
                frac = (int_val + frac) * multiplier;
                int_val = 0;
            }
        }

        p->pos = s - p->input;
        double result = int_val + frac;
        return val_f64(negative ? -result : result);
    }

    // Handle exponent on integer
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_negative = 0;
        if (*s == '-') { exp_negative = 1; s++; }
        else if (*s == '+') { s++; }

        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }

        p->pos = s - p->input;
        double result = (double)int_val;
        double multiplier = 1.0;
        for (int i = 0; i < exp; i++) multiplier *= 10.0;
        result = exp_negative ? result / multiplier : result * multiplier;
        return val_f64(negative ? -result : result);
    }

    p->pos = s - p->input;

    // Check if value fits in i32
    if (int_val <= 2147483647) {
        return val_i32(negative ? -(int32_t)int_val : (int32_t)int_val);
    }
    return val_i64(negative ? -int_val : int_val);
}

Value json_parse_object(JSONParser *p, ExecutionContext *ctx) {
    if (p->input[p->pos] != '{') {
        return throw_runtime_error(ctx, "Expected '{' in JSON");
    }
    p->pos++;  // skip opening brace

    char **field_names = malloc(sizeof(char*) * 32);
    Value *field_values = malloc(sizeof(Value) * 32);
    int num_fields = 0;

    json_skip_whitespace(p);

    // Handle empty object
    if (p->input[p->pos] == '}') {
        p->pos++;
        Object *obj = malloc(sizeof(Object));
        obj->field_names = field_names;
        obj->field_values = field_values;
        obj->num_fields = 0;
        obj->capacity = 32;
        obj->type_name = NULL;
        obj->ref_count = 1;  // Start with 1 - caller owns the first reference
        atomic_store(&obj->freed, 0);  // Not freed
        obj->hash_table = NULL;  // No hash table for empty objects
        obj->hash_capacity = 0;
        return val_object(obj);
    }

    while (p->input[p->pos] != '}' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Parse field name (must be a string)
        Value name_val = json_parse_string(p, ctx);
        if (ctx->exception_state.is_throwing) {
            // Clean up and propagate error
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                value_release(field_values[i]);
            }
            free(field_names);
            free(field_values);
            return val_null();
        }
        field_names[num_fields] = strdup(name_val.as.as_string->data);
        value_release(name_val);  // Release the temporary string after copying

        json_skip_whitespace(p);

        // Expect colon
        if (p->input[p->pos] != ':') {
            // Clean up allocated memory before error
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                value_release(field_values[i]);
            }
            free(field_names);
            free(field_values);
            return throw_runtime_error(ctx, "Expected ':' in JSON object");
        }
        p->pos++;

        json_skip_whitespace(p);

        // Parse field value
        field_values[num_fields] = json_parse_value(p, ctx);
        if (ctx->exception_state.is_throwing) {
            // Clean up and propagate error
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                value_release(field_values[i]);
            }
            free(field_names);
            free(field_values);
            return val_null();
        }
        num_fields++;

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != '}') {
            // Clean up allocated memory
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                value_release(field_values[i]);
            }
            free(field_names);
            free(field_values);
            return throw_runtime_error(ctx, "Expected ',' or '}' in JSON object");
        }
    }

    if (p->input[p->pos] != '}') {
        // Clean up allocated memory
        for (int i = 0; i < num_fields; i++) {
            free(field_names[i]);
            value_release(field_values[i]);
        }
        free(field_names);
        free(field_values);
        return throw_runtime_error(ctx, "Unterminated object in JSON");
    }
    p->pos++;  // skip closing brace

    Object *obj = malloc(sizeof(Object));
    obj->field_names = field_names;
    obj->field_values = field_values;
    obj->num_fields = num_fields;
    obj->capacity = 32;
    obj->type_name = NULL;
    obj->ref_count = 1;  // Start with 1 - caller owns the first reference
    atomic_store(&obj->freed, 0);  // Not freed
    obj->hash_table = NULL;  // No hash table - use linear search fallback
    obj->hash_capacity = 0;
    return val_object(obj);
}

Value json_parse_array(JSONParser *p, ExecutionContext *ctx) {
    if (p->input[p->pos] != '[') {
        return throw_runtime_error(ctx, "Expected '[' in JSON");
    }
    p->pos++;  // skip opening bracket

    Array *arr = array_new();

    json_skip_whitespace(p);

    // Handle empty array
    if (p->input[p->pos] == ']') {
        p->pos++;
        return val_array(arr);
    }

    while (p->input[p->pos] != ']' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Parse element value
        Value element = json_parse_value(p, ctx);
        if (ctx->exception_state.is_throwing) {
            // Error already set, just return
            return val_null();
        }
        array_push(arr, element);

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != ']') {
            // Note: arr will be cleaned up by value system when error is thrown
            return throw_runtime_error(ctx, "Expected ',' or ']' in JSON array");
        }
    }

    if (p->input[p->pos] != ']') {
        // Note: arr will be cleaned up by value system when error is thrown
        return throw_runtime_error(ctx, "Unterminated array in JSON");
    }
    p->pos++;  // skip closing bracket

    return val_array(arr);
}

// Optimized json_parse_value with direct character comparisons
Value json_parse_value(JSONParser *p, ExecutionContext *ctx) {
    json_skip_whitespace(p);

    const char *s = p->input + p->pos;
    char c = *s;

    switch (c) {
        case '"':
            return json_parse_string(p, ctx);
        case '{':
            return json_parse_object(p, ctx);
        case '[':
            return json_parse_array(p, ctx);
        case 't':
            if (s[1] == 'r' && s[2] == 'u' && s[3] == 'e') {
                p->pos += 4;
                return val_bool(1);
            }
            break;
        case 'f':
            if (s[1] == 'a' && s[2] == 'l' && s[3] == 's' && s[4] == 'e') {
                p->pos += 5;
                return val_bool(0);
            }
            break;
        case 'n':
            if (s[1] == 'u' && s[2] == 'l' && s[3] == 'l') {
                p->pos += 4;
                return val_null();
            }
            break;
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return json_parse_number(p, ctx);
    }

    return throw_runtime_error(ctx, "Unexpected character in JSON: '%c'", c);
}

// ========== OBJECT METHOD HANDLING ==========

Value call_object_method(Object *obj, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;  // Unused for some methods

    // keys() - return array of object keys
    if (strcmp(method, "keys") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "keys() expects no arguments");
        }

        // Create array of keys
        Array *keys_array = array_new();
        for (int i = 0; i < obj->num_fields; i++) {
            array_push(keys_array, val_string(obj->field_names[i]));
        }

        return val_array(keys_array);
    }

    // has(key) - check if object has a field
    if (strcmp(method, "has") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "has() expects 1 argument (key)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "has() key must be a string");
        }

        const char *key = args[0].as.as_string->data;
        for (int i = 0; i < obj->num_fields; i++) {
            if (strcmp(obj->field_names[i], key) == 0) {
                return val_bool(1);
            }
        }
        return val_bool(0);
    }

    // serialize() - convert object to JSON string
    if (strcmp(method, "serialize") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "serialize() expects no arguments");
        }

        VisitedSet visited;
        visited_init(&visited);

        Value obj_val = val_object(obj);
        char *json = serialize_value(obj_val, &visited, ctx);

        visited_free(&visited);

        if (json == NULL) {
            // Exception was already thrown by serialize_value
            return val_null();
        }

        Value result = val_string(json);
        free(json);

        return result;
    }

    // delete(key) - remove a field from the object
    if (strcmp(method, "delete") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "delete() expects 1 argument (key)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "delete() key must be a string");
        }

        const char *key = args[0].as.as_string->data;

        // Find the field index
        int found_index = -1;
        for (int i = 0; i < obj->num_fields; i++) {
            if (strcmp(obj->field_names[i], key) == 0) {
                found_index = i;
                break;
            }
        }

        if (found_index == -1) {
            return val_bool(0);  // Field not found, return false
        }

        // Release the value being deleted
        VALUE_RELEASE(obj->field_values[found_index]);

        // Free the field name
        free(obj->field_names[found_index]);

        // Shift remaining fields down
        for (int i = found_index; i < obj->num_fields - 1; i++) {
            obj->field_names[i] = obj->field_names[i + 1];
            obj->field_values[i] = obj->field_values[i + 1];
        }

        obj->num_fields--;

        // Rebuild hash table to reflect new indices
        if (obj->hash_table && obj->num_fields > 0) {
            // Clear hash table
            for (int i = 0; i < obj->hash_capacity; i++) {
                obj->hash_table[i] = -1;
            }
            // Rehash all remaining fields
            for (int i = 0; i < obj->num_fields; i++) {
                uint32_t hash = 5381;  // DJB2 hash
                const char *str = obj->field_names[i];
                int c;
                while ((c = *str++)) {
                    hash = ((hash << 5) + hash) + c;
                }
                int slot = hash % obj->hash_capacity;
                while (obj->hash_table[slot] != -1) {
                    slot = (slot + 1) % obj->hash_capacity;
                }
                obj->hash_table[slot] = i;
            }
        } else if (obj->hash_table && obj->num_fields == 0) {
            // Free hash table if object is now empty
            free(obj->hash_table);
            obj->hash_table = NULL;
            obj->hash_capacity = 0;
        }

        return val_bool(1);  // Field deleted, return true
    }

    return throw_runtime_error(ctx, "Object has no method '%s'", method);
}

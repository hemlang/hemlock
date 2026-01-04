/*
 * Hemlock Runtime - Serialization (JSON)
 *
 * Optimized JSON serialization and parsing:
 * - hml_serialize() - Convert values to JSON strings
 * - hml_deserialize() - Parse JSON strings to values
 *
 * Features:
 * - Cycle detection for circular references
 * - Single-allocation string building
 * - Fast path for strings without escapes
 * - Direct array/object building during parsing
 */

#include "builtins_internal.h"
#include <stdatomic.h>

// ========== OPTIMIZED SERIALIZATION (JSON) ==========

// Visited set for cycle detection
typedef struct HmlVisitedSet {
    void **items;
    int count;
    int capacity;
} HmlVisitedSet;

static void visited_init(HmlVisitedSet *set) {
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void visited_free(HmlVisitedSet *set) {
    free(set->items);
}

static int visited_contains(HmlVisitedSet *set, void *ptr) {
    for (int i = 0; i < set->count; i++) {
        if (set->items[i] == ptr) return 1;
    }
    return 0;
}

static void visited_add(HmlVisitedSet *set, void *ptr) {
    if (set->count >= set->capacity) {
        int new_cap = (set->capacity == 0) ? 16 : set->capacity * 2;
        set->items = realloc(set->items, new_cap * sizeof(void*));
        set->capacity = new_cap;
    }
    set->items[set->count++] = ptr;
}

// JSON StringBuilder - accumulates output in a single growing buffer
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} HmlJsonBuffer;

static inline void hjbuf_init(HmlJsonBuffer *buf, size_t initial_capacity) {
    buf->capacity = initial_capacity > 64 ? initial_capacity : 64;
    buf->data = malloc(buf->capacity);
    buf->len = 0;
}

static inline void hjbuf_ensure(HmlJsonBuffer *buf, size_t additional) {
    size_t needed = buf->len + additional;
    if (needed > buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        if (new_cap < needed) new_cap = needed;
        buf->data = realloc(buf->data, new_cap);
        buf->capacity = new_cap;
    }
}

static inline void hjbuf_append_char(HmlJsonBuffer *buf, char c) {
    hjbuf_ensure(buf, 1);
    buf->data[buf->len++] = c;
}

static inline void hjbuf_append_str(HmlJsonBuffer *buf, const char *s, size_t len) {
    hjbuf_ensure(buf, len);
    memcpy(buf->data + buf->len, s, len);
    buf->len += len;
}

// Fast integer to string - write directly to buffer
static inline void hjbuf_append_i64(HmlJsonBuffer *buf, int64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);
    int negative = val < 0;
    if (negative) val = -val;

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    if (negative) *--p = '-';
    hjbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

static inline void hjbuf_append_u64(HmlJsonBuffer *buf, uint64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    hjbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

// Append escaped JSON string directly to buffer
static inline void hjbuf_append_escaped_string(HmlJsonBuffer *buf, const char *str, size_t str_len) {
    hjbuf_append_char(buf, '"');
    hjbuf_ensure(buf, str_len * 2);

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
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    hjbuf_ensure(buf, 6);
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
    hjbuf_append_char(buf, '"');
}

// Forward declaration
static int serialize_to_buffer_impl(HmlValue val, HmlJsonBuffer *buf, HmlVisitedSet *visited);

static int serialize_to_buffer_impl(HmlValue val, HmlJsonBuffer *buf, HmlVisitedSet *visited) {
    char tmp[32];

    switch (val.type) {
        case HML_VAL_I8:
            hjbuf_append_i64(buf, val.as.as_i8);
            return 1;
        case HML_VAL_I16:
            hjbuf_append_i64(buf, val.as.as_i16);
            return 1;
        case HML_VAL_I32:
            hjbuf_append_i64(buf, val.as.as_i32);
            return 1;
        case HML_VAL_I64:
            hjbuf_append_i64(buf, val.as.as_i64);
            return 1;
        case HML_VAL_U8:
            hjbuf_append_u64(buf, val.as.as_u8);
            return 1;
        case HML_VAL_U16:
            hjbuf_append_u64(buf, val.as.as_u16);
            return 1;
        case HML_VAL_U32:
            hjbuf_append_u64(buf, val.as.as_u32);
            return 1;
        case HML_VAL_U64:
            hjbuf_append_u64(buf, val.as.as_u64);
            return 1;
        case HML_VAL_F32: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f32);
            hjbuf_append_str(buf, tmp, len);
            return 1;
        }
        case HML_VAL_F64: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f64);
            hjbuf_append_str(buf, tmp, len);
            return 1;
        }
        case HML_VAL_BOOL:
            if (val.as.as_bool) {
                hjbuf_append_str(buf, "true", 4);
            } else {
                hjbuf_append_str(buf, "false", 5);
            }
            return 1;
        case HML_VAL_STRING: {
            HmlString *s = val.as.as_string;
            hjbuf_append_escaped_string(buf, s->data, s->length);
            return 1;
        }
        case HML_VAL_NULL:
            hjbuf_append_str(buf, "null", 4);
            return 1;
        case HML_VAL_OBJECT: {
            HmlObject *obj = val.as.as_object;
            if (!obj) {
                hjbuf_append_str(buf, "null", 4);
                return 1;
            }

            if (visited_contains(visited, obj)) {
                hml_runtime_error("serialize() detected circular reference");
            }
            visited_add(visited, obj);

            hjbuf_append_char(buf, '{');

            for (int i = 0; i < obj->num_fields; i++) {
                if (i > 0) hjbuf_append_char(buf, ',');

                const char *name = obj->field_names[i];
                hjbuf_append_escaped_string(buf, name, strlen(name));
                hjbuf_append_char(buf, ':');

                if (!serialize_to_buffer_impl(obj->field_values[i], buf, visited)) {
                    return 0;
                }
            }

            hjbuf_append_char(buf, '}');
            return 1;
        }
        case HML_VAL_ARRAY: {
            HmlArray *arr = val.as.as_array;
            if (!arr) {
                hjbuf_append_str(buf, "null", 4);
                return 1;
            }

            if (visited_contains(visited, arr)) {
                hml_runtime_error("serialize() detected circular reference");
            }
            visited_add(visited, arr);

            hjbuf_append_char(buf, '[');

            for (int i = 0; i < arr->length; i++) {
                if (i > 0) hjbuf_append_char(buf, ',');

                if (!serialize_to_buffer_impl(arr->elements[i], buf, visited)) {
                    return 0;
                }
            }

            hjbuf_append_char(buf, ']');
            return 1;
        }
        default:
            hml_runtime_error("Cannot serialize value of this type");
    }
    return 0;
}

HmlValue hml_serialize(HmlValue val) {
    HmlVisitedSet visited;
    visited_init(&visited);

    HmlJsonBuffer buf;
    hjbuf_init(&buf, 256);

    serialize_to_buffer_impl(val, &buf, &visited);

    visited_free(&visited);

    hjbuf_append_char(&buf, '\0');
    return hml_val_string_owned(buf.data, buf.len - 1, buf.capacity);
}

// ========== JSON PARSER ==========

// JSON Parser state
typedef struct {
    const char *input;
    int pos;
} HmlJSONParser;

// Optimized whitespace skip
static inline void json_skip_whitespace(HmlJSONParser *p) {
    const char *s = p->input + p->pos;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    p->pos = s - p->input;
}

// Forward declarations
static HmlValue json_parse_value(HmlJSONParser *p);
static HmlValue json_parse_string(HmlJSONParser *p);
static HmlValue json_parse_number(HmlJSONParser *p);
static HmlValue json_parse_object(HmlJSONParser *p);
static HmlValue json_parse_array(HmlJSONParser *p);

// Optimized string parsing with fast path for strings without escapes
static HmlValue json_parse_string(HmlJSONParser *p) {
    if (p->input[p->pos] != '"') {
        hml_runtime_error("Expected '\"' in JSON");
    }
    p->pos++;  // skip opening quote

    const char *start = p->input + p->pos;
    const char *s = start;

    // Fast scan for end quote and check for escapes
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
        hml_runtime_error("Unterminated string in JSON");
    }

    size_t raw_len = s - start;

    if (!has_escapes) {
        // Fast path: direct copy
        char *buf = malloc(raw_len + 1);
        memcpy(buf, start, raw_len);
        buf[raw_len] = '\0';
        p->pos += raw_len + 1;
        return hml_val_string_owned(buf, raw_len, raw_len + 1);
    }

    // Slow path: handle escapes
    char *buf = malloc(raw_len + 1);
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
                    s++;
                    if (s[0] && s[1] && s[2] && s[3]) {
                        int codepoint = 0;
                        for (int i = 0; i < 4; i++) {
                            char c = s[i];
                            codepoint <<= 4;
                            if (c >= '0' && c <= '9') codepoint |= c - '0';
                            else if (c >= 'a' && c <= 'f') codepoint |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') codepoint |= c - 'A' + 10;
                        }
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
                        s += 3;
                    }
                    break;
                }
                default:
                    free(buf);
                    hml_runtime_error("Invalid escape sequence in JSON");
            }
            s++;
        } else {
            *out++ = *s++;
        }
    }

    *out = '\0';
    p->pos = (s - p->input) + 1;
    size_t len = out - buf;
    return hml_val_string_owned(buf, len, raw_len + 1);
}

// Optimized number parsing - no allocation
static HmlValue json_parse_number(HmlJSONParser *p) {
    const char *s = p->input + p->pos;
    int negative = 0;

    if (*s == '-') {
        negative = 1;
        s++;
    }

    int64_t int_val = 0;
    while (*s >= '0' && *s <= '9') {
        int_val = int_val * 10 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double frac = 0.0;
        double divisor = 10.0;
        while (*s >= '0' && *s <= '9') {
            frac += (*s - '0') / divisor;
            divisor *= 10.0;
            s++;
        }

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
        return hml_val_f64(negative ? -result : result);
    }

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
        return hml_val_f64(negative ? -result : result);
    }

    p->pos = s - p->input;

    if (int_val <= 2147483647) {
        return hml_val_i32(negative ? -(int32_t)int_val : (int32_t)int_val);
    }
    return hml_val_i64(negative ? -int_val : int_val);
}

static HmlValue json_parse_object(HmlJSONParser *p) {
    if (p->input[p->pos] != '{') {
        hml_runtime_error("Expected '{' in JSON");
    }
    p->pos++;

    // Pre-allocate arrays for direct building (O(n) instead of O(n²))
    int capacity = 8;
    char **field_names = malloc(sizeof(char*) * capacity);
    HmlValue *field_values = malloc(sizeof(HmlValue) * capacity);
    int num_fields = 0;

    json_skip_whitespace(p);

    if (p->input[p->pos] == '}') {
        p->pos++;
        // Build empty object directly
        HmlObject *obj = malloc(sizeof(HmlObject));
        obj->type_name = NULL;
        obj->field_names = field_names;
        obj->field_values = field_values;
        obj->num_fields = 0;
        obj->capacity = capacity;
        obj->ref_count = 1;
        atomic_store(&obj->freed, 0);
        obj->hash_table = NULL;  // Lazy initialization
        obj->hash_capacity = 0;
        HmlValue result;
        result.type = HML_VAL_OBJECT;
        result.as.as_object = obj;
        return result;
    }

    while (p->input[p->pos] != '}' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        HmlValue name_val = json_parse_string(p);

        // Grow arrays if needed
        if (num_fields >= capacity) {
            capacity *= 2;
            field_names = realloc(field_names, sizeof(char*) * capacity);
            field_values = realloc(field_values, sizeof(HmlValue) * capacity);
        }

        // Direct assignment - no duplicate check needed for JSON parsing
        field_names[num_fields] = strdup(name_val.as.as_string->data);
        hml_release(&name_val);

        json_skip_whitespace(p);

        if (p->input[p->pos] != ':') {
            // Cleanup on error
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                hml_release(&field_values[i]);
            }
            free(field_names);
            free(field_values);
            hml_runtime_error("Expected ':' in JSON object");
        }
        p->pos++;

        json_skip_whitespace(p);

        // Parse value directly into the array (already has ref_count=1)
        field_values[num_fields] = json_parse_value(p);
        num_fields++;

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != '}') {
            // Cleanup on error
            for (int i = 0; i < num_fields; i++) {
                free(field_names[i]);
                hml_release(&field_values[i]);
            }
            free(field_names);
            free(field_values);
            hml_runtime_error("Expected ',' or '}' in JSON object");
        }
    }

    if (p->input[p->pos] != '}') {
        // Cleanup on error
        for (int i = 0; i < num_fields; i++) {
            free(field_names[i]);
            hml_release(&field_values[i]);
        }
        free(field_names);
        free(field_values);
        hml_runtime_error("Unterminated object in JSON");
    }
    p->pos++;

    // Build object directly with pre-populated arrays
    HmlObject *obj = malloc(sizeof(HmlObject));
    obj->type_name = NULL;
    obj->field_names = field_names;
    obj->field_values = field_values;
    obj->num_fields = num_fields;
    obj->capacity = capacity;
    obj->ref_count = 1;
    atomic_store(&obj->freed, 0);
    obj->hash_table = NULL;  // Lazy initialization
    obj->hash_capacity = 0;

    HmlValue result;
    result.type = HML_VAL_OBJECT;
    result.as.as_object = obj;
    return result;
}

static HmlValue json_parse_array(HmlJSONParser *p) {
    if (p->input[p->pos] != '[') {
        hml_runtime_error("Expected '[' in JSON");
    }
    p->pos++;

    // Pre-allocate array for direct building (avoids retain/release overhead)
    int capacity = 8;
    HmlValue *elements = malloc(sizeof(HmlValue) * capacity);
    int length = 0;

    json_skip_whitespace(p);

    if (p->input[p->pos] == ']') {
        p->pos++;
        // Build empty array directly
        HmlArray *arr = malloc(sizeof(HmlArray));
        arr->elements = elements;
        arr->length = 0;
        arr->capacity = capacity;
        arr->ref_count = 1;
        arr->element_type = HML_VAL_NULL;
        atomic_store(&arr->freed, 0);
        HmlValue result;
        result.type = HML_VAL_ARRAY;
        result.as.as_array = arr;
        return result;
    }

    while (p->input[p->pos] != ']' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Grow if needed
        if (length >= capacity) {
            capacity *= 2;
            elements = realloc(elements, sizeof(HmlValue) * capacity);
        }

        // Parse directly into array - value already has ref_count=1
        elements[length] = json_parse_value(p);
        length++;

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != ']') {
            // Cleanup on error
            for (int i = 0; i < length; i++) {
                hml_release(&elements[i]);
            }
            free(elements);
            hml_runtime_error("Expected ',' or ']' in JSON array");
        }
    }

    if (p->input[p->pos] != ']') {
        // Cleanup on error
        for (int i = 0; i < length; i++) {
            hml_release(&elements[i]);
        }
        free(elements);
        hml_runtime_error("Unterminated array in JSON");
    }
    p->pos++;

    // Build array directly with pre-populated elements
    HmlArray *arr = malloc(sizeof(HmlArray));
    arr->elements = elements;
    arr->length = length;
    arr->capacity = capacity;
    arr->ref_count = 1;
    arr->element_type = HML_VAL_NULL;
    atomic_store(&arr->freed, 0);

    HmlValue result;
    result.type = HML_VAL_ARRAY;
    result.as.as_array = arr;
    return result;
}

// Optimized json_parse_value with direct character comparisons
static HmlValue json_parse_value(HmlJSONParser *p) {
    json_skip_whitespace(p);

    const char *s = p->input + p->pos;
    char c = *s;

    switch (c) {
        case '"':
            return json_parse_string(p);
        case '{':
            return json_parse_object(p);
        case '[':
            return json_parse_array(p);
        case 't':
            if (s[1] == 'r' && s[2] == 'u' && s[3] == 'e') {
                p->pos += 4;
                return hml_val_bool(1);
            }
            break;
        case 'f':
            if (s[1] == 'a' && s[2] == 'l' && s[3] == 's' && s[4] == 'e') {
                p->pos += 5;
                return hml_val_bool(0);
            }
            break;
        case 'n':
            if (s[1] == 'u' && s[2] == 'l' && s[3] == 'l') {
                p->pos += 4;
                return hml_val_null();
            }
            break;
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return json_parse_number(p);
    }

    hml_runtime_error("Unexpected character '%c' in JSON", c);
}

HmlValue hml_deserialize(HmlValue json_str) {
    if (json_str.type != HML_VAL_STRING || !json_str.as.as_string) {
        hml_runtime_error("deserialize() requires string argument");
    }

    HmlJSONParser parser = {
        .input = json_str.as.as_string->data,
        .pos = 0
    };

    return json_parse_value(&parser);
}

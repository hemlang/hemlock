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
#include "json_number.h"
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
        // SECURITY: Check for integer overflow before doubling capacity
        if (set->capacity > INT_MAX / 2) {
            hml_runtime_error("Visited set capacity overflow");
        }
        int new_cap = (set->capacity == 0) ? 16 : set->capacity * 2;
        void **new_items = realloc(set->items, new_cap * sizeof(void*));
        if (!new_items) {
            hml_runtime_error("Out of memory expanding visited set");
        }
        set->items = new_items;
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
        // SECURITY: Check for overflow before doubling capacity
        if (buf->capacity > SIZE_MAX / 2) {
            hml_runtime_error("JSON buffer capacity overflow");
        }
        size_t new_cap = buf->capacity * 2;
        if (new_cap < needed) new_cap = needed;
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) {
            hml_runtime_error("Out of memory expanding JSON buffer");
        }
        buf->data = new_data;
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
    // Negate in unsigned space: -INT64_MIN overflows int64_t.
    uint64_t mag = negative ? 0 - (uint64_t)val : (uint64_t)val;

    do {
        *--p = (char)('0' + (mag % 10));
        mag /= 10;
    } while (mag > 0);

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

// Shortest %g form that parses back to the same value (f32: 6-9 digits,
// f64: 15-17). Plain "%g" kept only 6 significant digits, so a
// serialize()/deserialize() round trip silently lost precision.
static int json_format_float(char *buf, size_t size, double value, int is_f32) {
    int lo = is_f32 ? 6 : 15, hi = is_f32 ? 9 : 17;
    int len = 0;
    for (int prec = lo; prec <= hi; prec++) {
        len = snprintf(buf, size, "%.*g", prec, value);
        double back = strtod(buf, NULL);
        if (is_f32 ? ((float)back == (float)value) : (back == value)) break;
    }
    return len;
}

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
            if (!isfinite(val.as.as_f32)) {
                hml_runtime_error("serialize() cannot represent NaN or Infinity in JSON");
            }
            int len = json_format_float(tmp, sizeof(tmp), val.as.as_f32, 1);
            hjbuf_append_str(buf, tmp, len);
            return 1;
        }
        case HML_VAL_F64: {
            if (!isfinite(val.as.as_f64)) {
                hml_runtime_error("serialize() cannot represent NaN or Infinity in JSON");
            }
            int len = json_format_float(tmp, sizeof(tmp), val.as.as_f64, 0);
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

                const char *name = obj->fields[i].name;
                hjbuf_append_escaped_string(buf, name, strlen(name));
                hjbuf_append_char(buf, ':');

                if (!serialize_to_buffer_impl(obj->fields[i].value, buf, visited)) {
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
    int depth;   // current nesting depth (bounds recursion on untrusted JSON)
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
static HmlValue json_parse_value_inner(HmlJSONParser *p);
// Parse exactly four hex digits; returns 0 if any is not a hex digit.
static int json_parse_hex4(const char *s, uint32_t *out) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        value = (value << 4) | (uint32_t)digit;
    }
    *out = value;
    return 1;
}

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

    // Slow path: handle escapes. The scan above found the real closing
    // quote, so every read below stays inside [start, end).
    const char *end = s;
    char *buf = malloc(raw_len + 1);
    if (!buf) {
        hml_runtime_error("Out of memory parsing JSON string");
    }
    char *out = buf;
    s = start;

    while (s < end) {
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
                    const char *hex_start = s + 1;
                    uint32_t codepoint = 0;
                    if (end - hex_start < 4 || !json_parse_hex4(hex_start, &codepoint)) {
                        free(buf);
                        hml_runtime_error("Invalid Unicode escape in JSON string");
                    }

                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        const char *pair_start = hex_start + 4;
                        uint32_t low_surrogate = 0;
                        if (end - pair_start < 6 || pair_start[0] != '\\' || pair_start[1] != 'u' ||
                            !json_parse_hex4(pair_start + 2, &low_surrogate) ||
                            low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
                            free(buf);
                            hml_runtime_error("Invalid Unicode surrogate pair in JSON string");
                        }
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low_surrogate - 0xDC00);
                        s = pair_start + 5;
                    } else {
                        if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                            free(buf);
                            hml_runtime_error("Invalid Unicode surrogate pair in JSON string");
                        }
                        s = hex_start + 3;
                    }

                    if (codepoint <= 0x7F) {
                        *out++ = (char)codepoint;
                    } else if (codepoint <= 0x7FF) {
                        *out++ = (char)(0xC0 | (codepoint >> 6));
                        *out++ = (char)(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0xFFFF) {
                        *out++ = (char)(0xE0 | (codepoint >> 12));
                        *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *out++ = (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        *out++ = (char)(0xF0 | (codepoint >> 18));
                        *out++ = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                        *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *out++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    free(buf);
                    hml_runtime_error("Invalid escape sequence in JSON string");
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
    size_t consumed = 0;
    int64_t ival = 0;
    double dval = 0.0;
    HmlJsonNumKind kind = hml_json_scan_number(p->input + p->pos, &consumed, &ival, &dval);
    if (kind == HML_JSON_NUM_INVALID) {
        hml_runtime_error("Invalid number in JSON");
    }
    p->pos += consumed;
    switch (kind) {
        case HML_JSON_NUM_I32: return hml_val_i32((int32_t)ival);
        case HML_JSON_NUM_I64: return hml_val_i64(ival);
        default:               return hml_val_f64(dval);
    }
}

static HmlValue json_parse_object(HmlJSONParser *p) {
    if (p->input[p->pos] != '{') {
        hml_runtime_error("Expected '{' in JSON");
    }
    p->pos++;

    // Pre-allocate unified fields array for direct building (O(n) instead of O(n²))
    int capacity = 8;
    HmlFieldEntry *fields = malloc(sizeof(HmlFieldEntry) * capacity);
    int num_fields = 0;

    json_skip_whitespace(p);

    if (p->input[p->pos] == '}') {
        p->pos++;
        // Build empty object directly
        HmlObject *obj = malloc(sizeof(HmlObject));
        obj->type_name = NULL;
        obj->fields = fields;
        obj->num_fields = 0;
        obj->capacity = capacity;
        obj->ref_count = 1;
        atomic_store(&obj->freed, 0);
        // Hand-built (not via obj_pool_alloc): must mark non-pooled.
        // Otherwise is_pooled is indeterminate malloc garbage and
        // object_free() can take the obj_pool_free() branch on a
        // foreign block — the object is never freed and the pool
        // freelist is corrupted. Every parsed JSON object leaked its
        // entire tree this way (multi-GB on a server that parses a
        // response per request).
        obj->is_pooled = 0;
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

        // Grow array if needed
        if (num_fields >= capacity) {
            // SECURITY: Check for integer overflow before doubling capacity
            if (capacity > INT_MAX / 2) {
                for (int i = 0; i < num_fields; i++) {
                    free(fields[i].name);
                    hml_release(&fields[i].value);
                }
                free(fields);
                hml_runtime_error("JSON object field capacity overflow");
            }
            int new_capacity = capacity * 2;
            HmlFieldEntry *new_fields = realloc(fields, sizeof(HmlFieldEntry) * new_capacity);
            if (!new_fields) {
                // Cleanup on OOM
                for (int i = 0; i < num_fields; i++) {
                    free(fields[i].name);
                    hml_release(&fields[i].value);
                }
                free(fields);
                hml_runtime_error("Out of memory parsing JSON object");
            }
            fields = new_fields;
            capacity = new_capacity;
        }

        // Direct assignment - no duplicate check needed for JSON parsing
        fields[num_fields].name = strdup(name_val.as.as_string->data);
        hml_release(&name_val);

        json_skip_whitespace(p);

        if (p->input[p->pos] != ':') {
            // Cleanup on error
            for (int i = 0; i < num_fields; i++) {
                free(fields[i].name);
                hml_release(&fields[i].value);
            }
            free(fields);
            hml_runtime_error("Expected ':' in JSON object");
        }
        p->pos++;

        json_skip_whitespace(p);

        // Parse value directly into the array (already has ref_count=1)
        fields[num_fields].value = json_parse_value(p);
        num_fields++;

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
            json_skip_whitespace(p);
            if (p->input[p->pos] == '}') {
                for (int i = 0; i < num_fields; i++) {
                    free(fields[i].name);
                    hml_release(&fields[i].value);
                }
                free(fields);
                hml_runtime_error("Trailing comma in JSON object");
            }
        } else if (p->input[p->pos] != '}') {
            // Cleanup on error
            for (int i = 0; i < num_fields; i++) {
                free(fields[i].name);
                hml_release(&fields[i].value);
            }
            free(fields);
            hml_runtime_error("Expected ',' or '}' in JSON object");
        }
    }

    if (p->input[p->pos] != '}') {
        // Cleanup on error
        for (int i = 0; i < num_fields; i++) {
            free(fields[i].name);
            hml_release(&fields[i].value);
        }
        free(fields);
        hml_runtime_error("Unterminated object in JSON");
    }
    p->pos++;

    // Build object directly with pre-populated fields array
    HmlObject *obj = malloc(sizeof(HmlObject));
    obj->type_name = NULL;
    obj->fields = fields;
    obj->num_fields = num_fields;
    obj->capacity = capacity;
    obj->ref_count = 1;
    atomic_store(&obj->freed, 0);
    // Hand-built (not via obj_pool_alloc): must mark non-pooled, or
    // is_pooled is indeterminate malloc garbage and object_free() can
    // route this foreign block through obj_pool_free() — never freeing
    // it and corrupting the pool freelist. This is the leak that grew
    // a Witchgrid control-plane multiple GB: it parses a JSON response
    // per agent per dashboard poll, and every parsed object tree
    // leaked in full.
    obj->is_pooled = 0;
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
            // SECURITY: Check for integer overflow before doubling capacity
            if (capacity > INT_MAX / 2) {
                for (int i = 0; i < length; i++) {
                    hml_release(&elements[i]);
                }
                free(elements);
                hml_runtime_error("JSON array capacity overflow");
            }
            int new_capacity = capacity * 2;
            HmlValue *new_elements = realloc(elements, sizeof(HmlValue) * new_capacity);
            if (!new_elements) {
                for (int i = 0; i < length; i++) {
                    hml_release(&elements[i]);
                }
                free(elements);
                hml_runtime_error("Out of memory parsing JSON array");
            }
            elements = new_elements;
            capacity = new_capacity;
        }

        // Parse directly into array - value already has ref_count=1
        elements[length] = json_parse_value(p);
        length++;

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
            json_skip_whitespace(p);
            if (p->input[p->pos] == ']') {
                for (int i = 0; i < length; i++) {
                    hml_release(&elements[i]);
                }
                free(elements);
                hml_runtime_error("Trailing comma in JSON array");
            }
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
// Public entry: bound recursion on deeply nested JSON so untrusted input raises
// a catchable error instead of overflowing the C stack. depth is incremented
// per nesting level and decremented when the subtree finishes, so flat siblings
// do not accumulate.
static HmlValue json_parse_value(HmlJSONParser *p) {
    if (p->depth >= HML_MAX_JSON_DEPTH) {
        hml_runtime_error("JSON nesting too deep (max %d)", HML_MAX_JSON_DEPTH);
    }
    p->depth++;
    HmlValue v = json_parse_value_inner(p);
    p->depth--;
    return v;
}

static HmlValue json_parse_value_inner(HmlJSONParser *p) {
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
        .pos = 0,
        .depth = 0
    };

    HmlValue result = json_parse_value(&parser);

    // The whole input must be one JSON value (matches the interpreter)
    json_skip_whitespace(&parser);
    if (parser.input[parser.pos] != '\0') {
        hml_release(&result);
        hml_runtime_error("Unexpected trailing characters in JSON");
    }
    return result;
}

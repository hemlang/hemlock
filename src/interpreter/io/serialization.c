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

// ========== SERIALIZATION SUPPORT ==========

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

// Helper to escape strings for JSON
char* escape_json_string(const char *str) {
    // Count how many chars need escaping
    int escape_count = 0;
    for (const char *p = str; *p; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') {
            escape_count++;
        }
    }

    // Allocate new string with room for escapes
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

// Recursive serialization helper
char* serialize_value(Value val, VisitedSet *visited) {
    char buffer[256];

    switch (val.type) {
        case VAL_I8:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i8);
            return strdup(buffer);
        case VAL_I16:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i16);
            return strdup(buffer);
        case VAL_I32:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i32);
            return strdup(buffer);
        case VAL_I64:
            snprintf(buffer, sizeof(buffer), "%ld", val.as.as_i64);
            return strdup(buffer);
        case VAL_U8:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u8);
            return strdup(buffer);
        case VAL_U16:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u16);
            return strdup(buffer);
        case VAL_U32:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u32);
            return strdup(buffer);
        case VAL_U64:
            snprintf(buffer, sizeof(buffer), "%lu", val.as.as_u64);
            return strdup(buffer);
        case VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            return strdup(buffer);
        case VAL_F64:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f64);
            return strdup(buffer);
        case VAL_BOOL:
            return strdup(val.as.as_bool ? "true" : "false");
        case VAL_STRING: {
            char *escaped = escape_json_string(val.as.as_string->data);
            int len = strlen(escaped) + 3;  // quotes + null
            char *result = malloc(len);
            snprintf(result, len, "\"%s\"", escaped);
            free(escaped);
            return result;
        }
        case VAL_NULL:
            return strdup("null");
        case VAL_OBJECT: {
            Object *obj = val.as.as_object;

            // Check for cycles
            if (visited_contains(visited, obj)) {
                fprintf(stderr, "Runtime error: serialize() detected circular reference\n");
                exit(1);
            }

            // Mark as visited
            visited_add(visited, obj);

            // Build JSON object
            size_t capacity = 256;
            size_t len = 0;
            char *json = malloc(capacity);
            json[len++] = '{';

            for (int i = 0; i < obj->num_fields; i++) {
                // Escape field name
                char *escaped_name = escape_json_string(obj->field_names[i]);

                // Serialize field value
                char *value_str = serialize_value(obj->field_values[i], visited);

                // Calculate space needed
                size_t needed = len + strlen(escaped_name) + strlen(value_str) + 10;
                if (needed > capacity) {
                    // Ensure capacity is at least needed, not just doubled once
                    while (capacity < needed) {
                        capacity *= 2;
                    }
                    json = realloc(json, capacity);
                }

                // Add field to JSON
                len += snprintf(json + len, capacity - len, "\"%s\":%s",
                               escaped_name, value_str);

                if (i < obj->num_fields - 1) {
                    json[len++] = ',';
                }

                free(escaped_name);
                free(value_str);
            }

            json[len++] = '}';
            json[len] = '\0';

            return json;
        }
        case VAL_ARRAY: {
            Array *arr = val.as.as_array;

            // Check for cycles (cast array pointer to object pointer for visited set)
            if (visited_contains(visited, (Object*)arr)) {
                fprintf(stderr, "Runtime error: serialize() detected circular reference\n");
                exit(1);
            }

            // Mark as visited
            visited_add(visited, (Object*)arr);

            // Build JSON array
            size_t capacity = 256;
            size_t len = 0;
            char *json = malloc(capacity);
            json[len++] = '[';

            for (int i = 0; i < arr->length; i++) {
                // Serialize element
                char *elem_str = serialize_value(arr->elements[i], visited);

                // Calculate space needed
                size_t needed = len + strlen(elem_str) + 2;
                if (needed > capacity) {
                    // Ensure capacity is at least needed, not just doubled once
                    while (capacity < needed) {
                        capacity *= 2;
                    }
                    json = realloc(json, capacity);
                }

                // Add element to JSON
                len += snprintf(json + len, capacity - len, "%s", elem_str);

                if (i < arr->length - 1) {
                    json[len++] = ',';
                }

                free(elem_str);
            }

            json[len++] = ']';
            json[len] = '\0';

            return json;
        }
        default:
            fprintf(stderr, "Runtime error: Cannot serialize value of this type\n");
            exit(1);
    }
}

void json_skip_whitespace(JSONParser *p) {
    while (p->input[p->pos] == ' ' || p->input[p->pos] == '\t' ||
           p->input[p->pos] == '\n' || p->input[p->pos] == '\r') {
        p->pos++;
    }
}

Value json_parse_string(JSONParser *p) {
    if (p->input[p->pos] != '"') {
        fprintf(stderr, "Runtime error: Expected '\"' in JSON\n");
        exit(1);
    }
    p->pos++;  // skip opening quote

    int len = 0;
    char *buf = malloc(256);
    int capacity = 256;

    while (p->input[p->pos] != '"' && p->input[p->pos] != '\0') {
        if (len >= capacity - 1) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }

        if (p->input[p->pos] == '\\') {
            p->pos++;
            switch (p->input[p->pos]) {
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                case '"': buf[len++] = '"'; break;
                case '\\': buf[len++] = '\\'; break;
                default:
                    fprintf(stderr, "Runtime error: Invalid escape sequence in JSON string\n");
                    exit(1);
            }
            p->pos++;
        } else {
            buf[len++] = p->input[p->pos++];
        }
    }

    if (p->input[p->pos] != '"') {
        fprintf(stderr, "Runtime error: Unterminated string in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing quote

    buf[len] = '\0';
    Value result = val_string(buf);
    free(buf);
    return result;
}

Value json_parse_number(JSONParser *p) {
    int start = p->pos;
    int is_float = 0;

    // Handle negative sign
    if (p->input[p->pos] == '-') {
        p->pos++;
    }

    // Parse digits
    while (p->input[p->pos] >= '0' && p->input[p->pos] <= '9') {
        p->pos++;
    }

    // Check for decimal point
    if (p->input[p->pos] == '.') {
        is_float = 1;
        p->pos++;
        while (p->input[p->pos] >= '0' && p->input[p->pos] <= '9') {
            p->pos++;
        }
    }

    // Parse the number
    char *num_str = strndup(p->input + start, p->pos - start);
    Value result;
    if (is_float) {
        result = val_f64(atof(num_str));
    } else {
        result = val_i32(atoi(num_str));
    }
    free(num_str);
    return result;
}

Value json_parse_object(JSONParser *p) {
    if (p->input[p->pos] != '{') {
        fprintf(stderr, "Runtime error: Expected '{' in JSON\n");
        exit(1);
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
        return val_object(obj);
    }

    while (p->input[p->pos] != '}' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Parse field name (must be a string)
        Value name_val = json_parse_string(p);
        field_names[num_fields] = strdup(name_val.as.as_string->data);

        json_skip_whitespace(p);

        // Expect colon
        if (p->input[p->pos] != ':') {
            fprintf(stderr, "Runtime error: Expected ':' in JSON object\n");
            exit(1);
        }
        p->pos++;

        json_skip_whitespace(p);

        // Parse field value
        field_values[num_fields] = json_parse_value(p);
        num_fields++;

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != '}') {
            fprintf(stderr, "Runtime error: Expected ',' or '}' in JSON object\n");
            exit(1);
        }
    }

    if (p->input[p->pos] != '}') {
        fprintf(stderr, "Runtime error: Unterminated object in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing brace

    Object *obj = malloc(sizeof(Object));
    obj->field_names = field_names;
    obj->field_values = field_values;
    obj->num_fields = num_fields;
    obj->capacity = 32;
    obj->type_name = NULL;
    return val_object(obj);
}

Value json_parse_array(JSONParser *p) {
    if (p->input[p->pos] != '[') {
        fprintf(stderr, "Runtime error: Expected '[' in JSON\n");
        exit(1);
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
        Value element = json_parse_value(p);
        array_push(arr, element);

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != ']') {
            fprintf(stderr, "Runtime error: Expected ',' or ']' in JSON array\n");
            exit(1);
        }
    }

    if (p->input[p->pos] != ']') {
        fprintf(stderr, "Runtime error: Unterminated array in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing bracket

    return val_array(arr);
}

Value json_parse_value(JSONParser *p) {
    json_skip_whitespace(p);

    // Check first character to determine type
    char c = p->input[p->pos];

    if (c == '"') {
        return json_parse_string(p);
    } else if (c == '{') {
        return json_parse_object(p);
    } else if (c == '[') {
        return json_parse_array(p);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        return json_parse_number(p);
    } else if (strncmp(p->input + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return val_bool(1);
    } else if (strncmp(p->input + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return val_bool(0);
    } else if (strncmp(p->input + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return val_null();
    } else {
        fprintf(stderr, "Runtime error: Unexpected character in JSON: '%c'\n", c);
        exit(1);
    }
}

// ========== OBJECT METHOD HANDLING ==========

Value call_object_method(Object *obj, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;  // Currently no object methods use args
    // serialize() - convert object to JSON string
    if (strcmp(method, "serialize") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "serialize() expects no arguments");
        }

        VisitedSet visited;
        visited_init(&visited);

        Value obj_val = val_object(obj);
        char *json = serialize_value(obj_val, &visited);
        Value result = val_string(json);

        visited_free(&visited);
        free(json);

        return result;
    }

    return throw_runtime_error(ctx, "Object has no method '%s'", method);
}

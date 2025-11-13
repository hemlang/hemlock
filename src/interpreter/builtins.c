#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

// ========== BUILTIN FUNCTIONS ==========

// Helper: Get the size of a type
static int get_type_size(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:
        case TYPE_U8:
            return 1;
        case TYPE_I16:
        case TYPE_U16:
            return 2;
        case TYPE_I32:
        case TYPE_U32:
        case TYPE_F32:
            return 4;
        case TYPE_I64:
        case TYPE_U64:
        case TYPE_F64:
            return 8;
        case TYPE_PTR:
        case TYPE_BUFFER:
            return sizeof(void*);  // 8 on 64-bit systems
        case TYPE_BOOL:
            return sizeof(int);    // bool is stored as int
        case TYPE_STRING:
            return sizeof(String*); // pointer to String struct
        default:
            fprintf(stderr, "Runtime error: Cannot get size of this type\n");
            exit(1);
    }
}


static Value builtin_print(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: print() expects 1 argument\n");
        exit(1);
    }

    print_value(args[0]);
    printf("\n");
    return val_null();
}

static Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: alloc() expects 1 argument (size in bytes)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: alloc() size must be an integer\n");
        exit(1);
    }

    int32_t size = value_to_int(args[0]);

    if (size <= 0) {
        fprintf(stderr, "Runtime error: alloc() size must be positive\n");
        exit(1);
    }

    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Runtime error: alloc() failed to allocate memory\n");
        exit(1);
    }

    return val_ptr(ptr);
}

static Value builtin_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: free() expects 1 argument (pointer or buffer)\n");
        exit(1);
    }

    if (args[0].type == VAL_PTR) {
        free(args[0].as.as_ptr);
        return val_null();
    } else if (args[0].type == VAL_BUFFER) {
        buffer_free(args[0].as.as_buffer);
        return val_null();
    } else {
        fprintf(stderr, "Runtime error: free() requires a pointer or buffer\n");
        exit(1);
    }
}

static Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: memset() expects 3 arguments (ptr, byte, size)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: memset() requires pointer as first argument\n");
        exit(1);
    }

    if (!is_integer(args[1]) || !is_integer(args[2])) {
        fprintf(stderr, "Runtime error: memset() byte and size must be integers\n");
        exit(1);
    }

    void *ptr = args[0].as.as_ptr;
    int byte = value_to_int(args[1]);
    int size = value_to_int(args[2]);

    memset(ptr, byte, size);
    return val_null();
}

static Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: memcpy() expects 3 arguments (dest, src, size)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR || args[1].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: memcpy() requires pointers for dest and src\n");
        exit(1);
    }

    if (!is_integer(args[2])) {
        fprintf(stderr, "Runtime error: memcpy() size must be an integer\n");
        exit(1);
    }

    void *dest = args[0].as.as_ptr;
    void *src = args[1].as.as_ptr;
    int size = value_to_int(args[2]);

    memcpy(dest, src, size);
    return val_null();
}

static Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sizeof() expects 1 argument (type)\n");
        exit(1);
    }

    if (args[0].type != VAL_TYPE) {
        fprintf(stderr, "Runtime error: sizeof() requires a type argument\n");
        exit(1);
    }

    TypeKind kind = args[0].as.as_type;
    int size = get_type_size(kind);
    return val_i32(size);
}

static Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: buffer() expects 1 argument (size in bytes)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: buffer() size must be an integer\n");
        exit(1);
    }

    int32_t size = value_to_int(args[0]);
    return val_buffer(size);
}

static Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: talloc() expects 2 arguments (type, count)\n");
        exit(1);
    }

    if (args[0].type != VAL_TYPE) {
        fprintf(stderr, "Runtime error: talloc() first argument must be a type\n");
        exit(1);
    }

    if (!is_integer(args[1])) {
        fprintf(stderr, "Runtime error: talloc() count must be an integer\n");
        exit(1);
    }

    TypeKind type = args[0].as.as_type;
    int32_t count = value_to_int(args[1]);

    if (count <= 0) {
        fprintf(stderr, "Runtime error: talloc() count must be positive\n");
        exit(1);
    }

    int elem_size = get_type_size(type);
    size_t total_size = (size_t)elem_size * (size_t)count;

    void *ptr = malloc(total_size);
    if (ptr == NULL) {
        fprintf(stderr, "Runtime error: talloc() failed to allocate memory\n");
        exit(1);
    }

    return val_ptr(ptr);
}

static Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: realloc() expects 2 arguments (ptr, new_size)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: realloc() first argument must be a pointer\n");
        exit(1);
    }

    if (!is_integer(args[1])) {
        fprintf(stderr, "Runtime error: realloc() new_size must be an integer\n");
        exit(1);
    }

    void *old_ptr = args[0].as.as_ptr;
    int32_t new_size = value_to_int(args[1]);

    if (new_size <= 0) {
        fprintf(stderr, "Runtime error: realloc() new_size must be positive\n");
        exit(1);
    }

    void *new_ptr = realloc(old_ptr, new_size);
    if (new_ptr == NULL) {
        fprintf(stderr, "Runtime error: realloc() failed to allocate memory\n");
        exit(1);
    }

    return val_ptr(new_ptr);
}

// Cycle detection for serialization
typedef struct {
    Object **visited;
    int count;
    int capacity;
} VisitedSet;

static void visited_init(VisitedSet *set) {
    set->capacity = 16;
    set->count = 0;
    set->visited = malloc(sizeof(Object*) * set->capacity);
}

static int visited_contains(VisitedSet *set, Object *obj) {
    for (int i = 0; i < set->count; i++) {
        if (set->visited[i] == obj) {
            return 1;
        }
    }
    return 0;
}

static void visited_add(VisitedSet *set, Object *obj) {
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->visited = realloc(set->visited, sizeof(Object*) * set->capacity);
    }
    set->visited[set->count++] = obj;
}

static void visited_free(VisitedSet *set) {
    free(set->visited);
}

// Helper to escape strings for JSON
static char* escape_json_string(const char *str) {
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
static char* serialize_value(Value val, VisitedSet *visited) {
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
                    capacity *= 2;
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
                    capacity *= 2;
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

static Value builtin_serialize(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: serialize() expects 1 argument\n");
        exit(1);
    }

    VisitedSet visited;
    visited_init(&visited);

    char *json = serialize_value(args[0], &visited);
    Value result = val_string(json);

    visited_free(&visited);
    free(json);

    return result;
}

// JSON deserialization
typedef struct {
    const char *input;
    int pos;
} JSONParser;

static void json_skip_whitespace(JSONParser *p) {
    while (p->input[p->pos] == ' ' || p->input[p->pos] == '\t' ||
           p->input[p->pos] == '\n' || p->input[p->pos] == '\r') {
        p->pos++;
    }
}

static Value json_parse_value(JSONParser *p);
static Value json_parse_array(JSONParser *p);

static Value json_parse_string(JSONParser *p) {
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

static Value json_parse_number(JSONParser *p) {
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

static Value json_parse_object(JSONParser *p) {
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

static Value json_parse_array(JSONParser *p) {
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

static Value json_parse_value(JSONParser *p) {
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

static Value builtin_deserialize(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: deserialize() expects 1 argument (JSON string)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: deserialize() requires a string argument\n");
        exit(1);
    }

    JSONParser parser;
    parser.input = args[0].as.as_string->data;
    parser.pos = 0;

    Value result = json_parse_value(&parser);

    // Check that we consumed all the input
    json_skip_whitespace(&parser);
    if (parser.input[parser.pos] != '\0') {
        fprintf(stderr, "Runtime error: Unexpected trailing characters in JSON\n");
        exit(1);
    }

    return result;
}

static Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: typeof() expects 1 argument\n");
        exit(1);
    }

    const char *type_name;
    switch (args[0].type) {
        case VAL_I8:
            type_name = "i8";
            break;
        case VAL_I16:
            type_name = "i16";
            break;
        case VAL_I32:
            type_name = "i32";
            break;
        case VAL_I64:
            type_name = "i64";
            break;
        case VAL_U8:
            type_name = "u8";
            break;
        case VAL_U16:
            type_name = "u16";
            break;
        case VAL_U32:
            type_name = "u32";
            break;
        case VAL_U64:
            type_name = "u64";
            break;
        case VAL_F32:
            type_name = "f32";
            break;
        case VAL_F64:
            type_name = "f64";
            break;
        case VAL_BOOL:
            type_name = "bool";
            break;
        case VAL_STRING:
            type_name = "string";
            break;
        case VAL_RUNE:
            type_name = "rune";
            break;
        case VAL_PTR:
            type_name = "ptr";
            break;
        case VAL_BUFFER:
            type_name = "buffer";
            break;
        case VAL_ARRAY:
            type_name = "array";
            break;
        case VAL_FILE:
            type_name = "file";
            break;
        case VAL_NULL:
            type_name = "null";
            break;
        case VAL_FUNCTION:
            type_name = "function";
            break;
        case VAL_BUILTIN_FN:
            type_name = "builtin";
            break;
        case VAL_OBJECT:
            // Check if object has a custom type name
            if (args[0].as.as_object->type_name) {
                type_name = args[0].as.as_object->type_name;
            } else {
                type_name = "object";
            }
            break;
        case VAL_TYPE:
            type_name = "type";
            break;
        default:
            type_name = "unknown";
            break;
    }

    return val_string(type_name);
}

// ========== I/O BUILTIN FUNCTIONS ==========
// Note: File/array method handlers are in io.c


static Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: assert() expects 1-2 arguments (condition, [message])\n");
        exit(1);
    }

    // Check if condition is truthy
    int is_truthy = 0;
    switch (args[0].type) {
        case VAL_I8:
            is_truthy = args[0].as.as_i8 != 0;
            break;
        case VAL_I16:
            is_truthy = args[0].as.as_i16 != 0;
            break;
        case VAL_I32:
            is_truthy = args[0].as.as_i32 != 0;
            break;
        case VAL_U8:
            is_truthy = args[0].as.as_u8 != 0;
            break;
        case VAL_U16:
            is_truthy = args[0].as.as_u16 != 0;
            break;
        case VAL_U32:
            is_truthy = args[0].as.as_u32 != 0;
            break;
        case VAL_F32:
            is_truthy = args[0].as.as_f32 != 0.0f;
            break;
        case VAL_F64:
            is_truthy = args[0].as.as_f64 != 0.0;
            break;
        case VAL_BOOL:
            is_truthy = args[0].as.as_bool;
            break;
        case VAL_NULL:
            is_truthy = 0;
            break;
        case VAL_STRING:
            // Non-empty string is truthy
            is_truthy = args[0].as.as_string->length > 0;
            break;
        case VAL_PTR:
            is_truthy = args[0].as.as_ptr != NULL;
            break;
        default:
            // All other types (objects, arrays, functions, etc.) are truthy
            is_truthy = 1;
            break;
    }

    // If condition is false, throw exception
    if (!is_truthy) {
        Value exception_msg;
        if (num_args == 2) {
            // Use provided message
            exception_msg = args[1];
        } else {
            // Use default message
            exception_msg = val_string("assertion failed");
        }

        ctx->exception_state.exception_value = exception_msg;
        ctx->exception_state.is_throwing = 1;
    }

    return val_null();
}

// ========== ASYNC/CONCURRENCY BUILTINS ==========

// Global task ID counter
static int next_task_id = 1;

// Thread wrapper function that executes a task
static void* task_thread_wrapper(void* arg) {
    Task *task = (Task*)arg;
    Function *fn = task->function;

    // Mark as running
    task->state = TASK_RUNNING;

    // Create new environment for function execution
    Environment *func_env = env_new(task->env);

    // Bind parameters
    for (int i = 0; i < fn->num_params && i < task->num_args; i++) {
        Value arg = task->args[i];
        // Type check if parameter has type annotation
        if (fn->param_types[i]) {
            arg = convert_to_type(arg, fn->param_types[i], func_env, task->ctx);
        }
        env_define(func_env, fn->param_names[i], arg, 0, task->ctx);
    }

    // Execute function body
    eval_stmt(fn->body, func_env, task->ctx);

    // Get return value
    Value result = val_null();
    if (task->ctx->return_state.is_returning) {
        result = task->ctx->return_state.return_value;
        task->ctx->return_state.is_returning = 0;
    }

    // Store result in task
    task->result = malloc(sizeof(Value));
    *task->result = result;
    task->state = TASK_COMPLETED;

    // Clean up function environment
    env_free(func_env);

    return NULL;
}

static Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Not used in spawn

    if (num_args < 1) {
        fprintf(stderr, "Runtime error: spawn() expects at least 1 argument (async function)\n");
        exit(1);
    }

    Value func_val = args[0];

    if (func_val.type != VAL_FUNCTION) {
        fprintf(stderr, "Runtime error: spawn() expects an async function\n");
        exit(1);
    }

    Function *fn = func_val.as.as_function;

    if (!fn->is_async) {
        fprintf(stderr, "Runtime error: spawn() requires an async function\n");
        exit(1);
    }

    // Create task with remaining args as function arguments
    Value *task_args = NULL;
    int task_num_args = num_args - 1;

    if (task_num_args > 0) {
        task_args = malloc(sizeof(Value) * task_num_args);
        for (int i = 0; i < task_num_args; i++) {
            task_args[i] = args[i + 1];
        }
    }

    // Create task
    Task *task = task_new(next_task_id++, fn, task_args, task_num_args, fn->closure_env);

    // Allocate pthread_t
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    // Create thread to execute task
    int rc = pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);
    if (rc != 0) {
        fprintf(stderr, "Runtime error: Failed to create thread: %d\n", rc);
        exit(1);
    }

    return val_task(task);
}

static Value builtin_join(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: join() expects 1 argument (task handle)\n");
        exit(1);
    }

    Value task_val = args[0];

    if (task_val.type != VAL_TASK) {
        fprintf(stderr, "Runtime error: join() expects a task handle\n");
        exit(1);
    }

    Task *task = task_val.as.as_task;

    if (task->joined) {
        fprintf(stderr, "Runtime error: task handle already joined\n");
        exit(1);
    }

    if (task->detached) {
        fprintf(stderr, "Runtime error: cannot join detached task\n");
        exit(1);
    }

    // Mark as joined
    task->joined = 1;

    // Wait for thread to complete
    if (task->thread) {
        int rc = pthread_join(*(pthread_t*)task->thread, NULL);
        if (rc != 0) {
            fprintf(stderr, "Runtime error: pthread_join failed: %d\n", rc);
            exit(1);
        }
    }

    // Check if task threw an exception
    if (task->ctx->exception_state.is_throwing) {
        // Re-throw the exception in the current context
        ctx->exception_state = task->ctx->exception_state;
        return val_null();
    }

    // Return the result
    if (task->result) {
        return *task->result;
    }

    return val_null();
}

static Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx) {
    // detach() is like spawn() but detaches the thread
    Value task = builtin_spawn(args, num_args, ctx);

    // Detach the thread (fire and forget)
    if (task.type == VAL_TASK) {
        Task *t = task.as.as_task;
        t->detached = 1;

        if (t->thread) {
            int rc = pthread_detach(*(pthread_t*)t->thread);
            if (rc != 0) {
                fprintf(stderr, "Runtime error: pthread_detach failed: %d\n", rc);
                exit(1);
            }
        }

        // Note: We don't free the task here - it will clean itself up
        // when the thread completes. In a production system, we'd have
        // a cleanup mechanism.
    }

    return val_null();
}

static Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    int capacity = 0;  // unbuffered by default

    if (num_args > 0) {
        if (args[0].type != VAL_I32 && args[0].type != VAL_U32) {
            fprintf(stderr, "Runtime error: channel() capacity must be an integer\n");
            exit(1);
        }
        capacity = value_to_int(args[0]);

        if (capacity < 0) {
            fprintf(stderr, "Runtime error: channel() capacity cannot be negative\n");
            exit(1);
        }
    }

    Channel *ch = channel_new(capacity);
    return val_channel(ch);
}

// Structure to hold builtin function info
typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinInfo;

static BuiltinInfo builtins[] = {
    {"print", builtin_print},
    {"alloc", builtin_alloc},
    {"talloc", builtin_talloc},
    {"realloc", builtin_realloc},
    {"free", builtin_free},
    {"memset", builtin_memset},
    {"memcpy", builtin_memcpy},
    {"sizeof", builtin_sizeof},
    {"buffer", builtin_buffer},
    {"typeof", builtin_typeof},
    {"serialize", builtin_serialize},
    {"deserialize", builtin_deserialize},
    {"read_line", builtin_read_line},
    {"eprint", builtin_eprint},
    {"open", builtin_open},
    {"assert", builtin_assert},
    {"spawn", builtin_spawn},
    {"join", builtin_join},
    {"detach", builtin_detach},
    {"channel", builtin_channel},
    {NULL, NULL}  // Sentinel
};

Value val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx) {
    // Register type constants FIRST for use with sizeof() and talloc()
    // These must be registered before builtin functions to avoid conflicts
    env_set(env, "i8", val_type(TYPE_I8), ctx);
    env_set(env, "i16", val_type(TYPE_I16), ctx);
    env_set(env, "i32", val_type(TYPE_I32), ctx);
    env_set(env, "u8", val_type(TYPE_U8), ctx);
    env_set(env, "u16", val_type(TYPE_U16), ctx);
    env_set(env, "u32", val_type(TYPE_U32), ctx);
    env_set(env, "f32", val_type(TYPE_F32), ctx);
    env_set(env, "f64", val_type(TYPE_F64), ctx);
    env_set(env, "ptr", val_type(TYPE_PTR), ctx);

    // Type aliases
    env_set(env, "integer", val_type(TYPE_I32), ctx);
    env_set(env, "number", val_type(TYPE_F64), ctx);
    env_set(env, "char", val_type(TYPE_U8), ctx);

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn), ctx);
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        array_push(args_array, val_string(argv[i]));
    }
    env_set(env, "args", val_array(args_array), ctx);
}

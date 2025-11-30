/*
 * LSP JSON-RPC Protocol Implementation
 */

#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

// ============================================================================
// JSON Value Constructors
// ============================================================================

JSONValue *json_null(void) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_NULL;
    return v;
}

JSONValue *json_bool(bool value) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_BOOL;
    v->as.boolean = value;
    return v;
}

JSONValue *json_number(double value) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_NUMBER;
    v->as.number = value;
    return v;
}

JSONValue *json_string(const char *value) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_STRING;
    v->as.string = value ? strdup(value) : NULL;
    return v;
}

JSONValue *json_array(void) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_ARRAY;
    v->as.array = malloc(sizeof(JSONArray));
    v->as.array->items = NULL;
    v->as.array->count = 0;
    v->as.array->capacity = 0;
    return v;
}

JSONValue *json_object(void) {
    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_OBJECT;
    v->as.object = malloc(sizeof(JSONObject));
    v->as.object->keys = NULL;
    v->as.object->values = NULL;
    v->as.object->count = 0;
    v->as.object->capacity = 0;
    return v;
}

// ============================================================================
// JSON Array Operations
// ============================================================================

void json_array_push(JSONValue *arr, JSONValue *item) {
    if (arr->type != JSON_ARRAY) return;

    JSONArray *a = arr->as.array;
    if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 8 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(JSONValue *));
    }
    a->items[a->count++] = item;
}

// ============================================================================
// JSON Object Operations
// ============================================================================

void json_object_set(JSONValue *obj, const char *key, JSONValue *value) {
    if (obj->type != JSON_OBJECT) return;

    JSONObject *o = obj->as.object;

    // Check if key exists
    for (int i = 0; i < o->count; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            json_free(o->values[i]);
            o->values[i] = value;
            return;
        }
    }

    // Add new key
    if (o->count >= o->capacity) {
        o->capacity = o->capacity == 0 ? 8 : o->capacity * 2;
        o->keys = realloc(o->keys, o->capacity * sizeof(char *));
        o->values = realloc(o->values, o->capacity * sizeof(JSONValue *));
    }
    o->keys[o->count] = strdup(key);
    o->values[o->count] = value;
    o->count++;
}

JSONValue *json_object_get(JSONValue *obj, const char *key) {
    if (obj == NULL || obj->type != JSON_OBJECT) return NULL;

    JSONObject *o = obj->as.object;
    for (int i = 0; i < o->count; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            return o->values[i];
        }
    }
    return NULL;
}

const char *json_object_get_string(JSONValue *obj, const char *key) {
    JSONValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) return v->as.string;
    return NULL;
}

double json_object_get_number(JSONValue *obj, const char *key) {
    JSONValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->as.number;
    return 0;
}

bool json_object_get_bool(JSONValue *obj, const char *key) {
    JSONValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->as.boolean;
    return false;
}

JSONValue *json_object_get_object(JSONValue *obj, const char *key) {
    JSONValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_OBJECT) return v;
    return NULL;
}

JSONValue *json_object_get_array(JSONValue *obj, const char *key) {
    JSONValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_ARRAY) return v;
    return NULL;
}

bool json_object_has(JSONValue *obj, const char *key) {
    return json_object_get(obj, key) != NULL;
}

// ============================================================================
// JSON Parser
// ============================================================================

typedef struct {
    const char *input;
    const char *current;
    const char *error;
} JSONParser;

static void skip_whitespace(JSONParser *p) {
    while (*p->current && isspace(*p->current)) {
        p->current++;
    }
}

static JSONValue *parse_value(JSONParser *p);

static JSONValue *parse_string(JSONParser *p) {
    if (*p->current != '"') {
        p->error = "Expected '\"'";
        return NULL;
    }
    p->current++;  // Skip opening quote

    // Find string length (handle escapes)
    const char *start = p->current;
    int len = 0;
    while (*p->current && *p->current != '"') {
        if (*p->current == '\\') {
            p->current++;
            if (!*p->current) {
                p->error = "Unterminated string escape";
                return NULL;
            }
        }
        p->current++;
        len++;
    }

    if (*p->current != '"') {
        p->error = "Unterminated string";
        return NULL;
    }

    // Allocate and copy string (handling escapes)
    char *str = malloc(len + 1);
    char *dst = str;
    p->current = start;

    while (*p->current && *p->current != '"') {
        if (*p->current == '\\') {
            p->current++;
            switch (*p->current) {
                case '"':  *dst++ = '"';  break;
                case '\\': *dst++ = '\\'; break;
                case '/':  *dst++ = '/';  break;
                case 'b':  *dst++ = '\b'; break;
                case 'f':  *dst++ = '\f'; break;
                case 'n':  *dst++ = '\n'; break;
                case 'r':  *dst++ = '\r'; break;
                case 't':  *dst++ = '\t'; break;
                case 'u': {
                    // Unicode escape \uXXXX
                    p->current++;
                    char hex[5] = {0};
                    for (int i = 0; i < 4 && *p->current; i++) {
                        hex[i] = *p->current++;
                    }
                    p->current--;  // Will be incremented below
                    int codepoint = (int)strtol(hex, NULL, 16);
                    // Simple UTF-8 encoding for BMP
                    if (codepoint < 0x80) {
                        *dst++ = (char)codepoint;
                    } else if (codepoint < 0x800) {
                        *dst++ = (char)(0xC0 | (codepoint >> 6));
                        *dst++ = (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        *dst++ = (char)(0xE0 | (codepoint >> 12));
                        *dst++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *dst++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:   *dst++ = *p->current; break;
            }
        } else {
            *dst++ = *p->current;
        }
        p->current++;
    }
    *dst = '\0';

    p->current++;  // Skip closing quote

    JSONValue *v = malloc(sizeof(JSONValue));
    v->type = JSON_STRING;
    v->as.string = str;
    return v;
}

static JSONValue *parse_number(JSONParser *p) {
    const char *start = p->current;

    // Handle negative
    if (*p->current == '-') p->current++;

    // Integer part
    if (*p->current == '0') {
        p->current++;
    } else if (isdigit(*p->current)) {
        while (isdigit(*p->current)) p->current++;
    } else {
        p->error = "Invalid number";
        return NULL;
    }

    // Fractional part
    if (*p->current == '.') {
        p->current++;
        if (!isdigit(*p->current)) {
            p->error = "Invalid number";
            return NULL;
        }
        while (isdigit(*p->current)) p->current++;
    }

    // Exponent
    if (*p->current == 'e' || *p->current == 'E') {
        p->current++;
        if (*p->current == '+' || *p->current == '-') p->current++;
        if (!isdigit(*p->current)) {
            p->error = "Invalid number";
            return NULL;
        }
        while (isdigit(*p->current)) p->current++;
    }

    char *numstr = strndup(start, p->current - start);
    double num = strtod(numstr, NULL);
    free(numstr);

    return json_number(num);
}

static JSONValue *parse_array(JSONParser *p) {
    if (*p->current != '[') {
        p->error = "Expected '['";
        return NULL;
    }
    p->current++;

    JSONValue *arr = json_array();

    skip_whitespace(p);
    if (*p->current == ']') {
        p->current++;
        return arr;
    }

    while (1) {
        skip_whitespace(p);
        JSONValue *item = parse_value(p);
        if (!item) {
            json_free(arr);
            return NULL;
        }
        json_array_push(arr, item);

        skip_whitespace(p);
        if (*p->current == ']') {
            p->current++;
            return arr;
        }
        if (*p->current != ',') {
            p->error = "Expected ',' or ']'";
            json_free(arr);
            return NULL;
        }
        p->current++;
    }
}

static JSONValue *parse_object(JSONParser *p) {
    if (*p->current != '{') {
        p->error = "Expected '{'";
        return NULL;
    }
    p->current++;

    JSONValue *obj = json_object();

    skip_whitespace(p);
    if (*p->current == '}') {
        p->current++;
        return obj;
    }

    while (1) {
        skip_whitespace(p);
        if (*p->current != '"') {
            p->error = "Expected string key";
            json_free(obj);
            return NULL;
        }

        JSONValue *key = parse_string(p);
        if (!key) {
            json_free(obj);
            return NULL;
        }

        skip_whitespace(p);
        if (*p->current != ':') {
            p->error = "Expected ':'";
            json_free(key);
            json_free(obj);
            return NULL;
        }
        p->current++;

        skip_whitespace(p);
        JSONValue *value = parse_value(p);
        if (!value) {
            json_free(key);
            json_free(obj);
            return NULL;
        }

        json_object_set(obj, key->as.string, value);
        json_free(key);

        skip_whitespace(p);
        if (*p->current == '}') {
            p->current++;
            return obj;
        }
        if (*p->current != ',') {
            p->error = "Expected ',' or '}'";
            json_free(obj);
            return NULL;
        }
        p->current++;
    }
}

static JSONValue *parse_value(JSONParser *p) {
    skip_whitespace(p);

    if (*p->current == 'n' && strncmp(p->current, "null", 4) == 0) {
        p->current += 4;
        return json_null();
    }
    if (*p->current == 't' && strncmp(p->current, "true", 4) == 0) {
        p->current += 4;
        return json_bool(true);
    }
    if (*p->current == 'f' && strncmp(p->current, "false", 5) == 0) {
        p->current += 5;
        return json_bool(false);
    }
    if (*p->current == '"') {
        return parse_string(p);
    }
    if (*p->current == '[') {
        return parse_array(p);
    }
    if (*p->current == '{') {
        return parse_object(p);
    }
    if (*p->current == '-' || isdigit(*p->current)) {
        return parse_number(p);
    }

    p->error = "Unexpected character";
    return NULL;
}

JSONValue *json_parse(const char *input, const char **error) {
    JSONParser p = {
        .input = input,
        .current = input,
        .error = NULL
    };

    JSONValue *result = parse_value(&p);

    if (error) {
        *error = p.error;
    }

    return result;
}

// ============================================================================
// JSON Serializer
// ============================================================================

typedef struct {
    char *buffer;
    int length;
    int capacity;
} StringBuilder;

static void sb_init(StringBuilder *sb) {
    sb->capacity = 256;
    sb->buffer = malloc(sb->capacity);
    sb->buffer[0] = '\0';
    sb->length = 0;
}

static void sb_append(StringBuilder *sb, const char *str) {
    int len = strlen(str);
    while (sb->length + len + 1 > sb->capacity) {
        sb->capacity *= 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    strcpy(sb->buffer + sb->length, str);
    sb->length += len;
}

static void sb_append_char(StringBuilder *sb, char c) {
    if (sb->length + 2 > sb->capacity) {
        sb->capacity *= 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    sb->buffer[sb->length++] = c;
    sb->buffer[sb->length] = '\0';
}

static void serialize_value(StringBuilder *sb, JSONValue *v);

static void serialize_string(StringBuilder *sb, const char *str) {
    sb_append_char(sb, '"');
    for (const char *c = str; *c; c++) {
        switch (*c) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\b': sb_append(sb, "\\b");  break;
            case '\f': sb_append(sb, "\\f");  break;
            case '\n': sb_append(sb, "\\n");  break;
            case '\r': sb_append(sb, "\\r");  break;
            case '\t': sb_append(sb, "\\t");  break;
            default:
                if ((unsigned char)*c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*c);
                    sb_append(sb, buf);
                } else {
                    sb_append_char(sb, *c);
                }
                break;
        }
    }
    sb_append_char(sb, '"');
}

static void serialize_value(StringBuilder *sb, JSONValue *v) {
    if (!v) {
        sb_append(sb, "null");
        return;
    }

    switch (v->type) {
        case JSON_NULL:
            sb_append(sb, "null");
            break;

        case JSON_BOOL:
            sb_append(sb, v->as.boolean ? "true" : "false");
            break;

        case JSON_NUMBER: {
            char buf[64];
            // Check if it's an integer
            if (v->as.number == (double)(long long)v->as.number) {
                snprintf(buf, sizeof(buf), "%lld", (long long)v->as.number);
            } else {
                snprintf(buf, sizeof(buf), "%g", v->as.number);
            }
            sb_append(sb, buf);
            break;
        }

        case JSON_STRING:
            if (v->as.string) {
                serialize_string(sb, v->as.string);
            } else {
                sb_append(sb, "null");
            }
            break;

        case JSON_ARRAY: {
            sb_append_char(sb, '[');
            JSONArray *a = v->as.array;
            for (int i = 0; i < a->count; i++) {
                if (i > 0) sb_append_char(sb, ',');
                serialize_value(sb, a->items[i]);
            }
            sb_append_char(sb, ']');
            break;
        }

        case JSON_OBJECT: {
            sb_append_char(sb, '{');
            JSONObject *o = v->as.object;
            for (int i = 0; i < o->count; i++) {
                if (i > 0) sb_append_char(sb, ',');
                serialize_string(sb, o->keys[i]);
                sb_append_char(sb, ':');
                serialize_value(sb, o->values[i]);
            }
            sb_append_char(sb, '}');
            break;
        }
    }
}

char *json_serialize(JSONValue *value) {
    StringBuilder sb;
    sb_init(&sb);
    serialize_value(&sb, value);
    return sb.buffer;
}

// ============================================================================
// JSON Cleanup
// ============================================================================

void json_free(JSONValue *value) {
    if (!value) return;

    switch (value->type) {
        case JSON_STRING:
            free(value->as.string);
            break;

        case JSON_ARRAY: {
            JSONArray *a = value->as.array;
            for (int i = 0; i < a->count; i++) {
                json_free(a->items[i]);
            }
            free(a->items);
            free(a);
            break;
        }

        case JSON_OBJECT: {
            JSONObject *o = value->as.object;
            for (int i = 0; i < o->count; i++) {
                free(o->keys[i]);
                json_free(o->values[i]);
            }
            free(o->keys);
            free(o->values);
            free(o);
            break;
        }

        default:
            break;
    }

    free(value);
}

// ============================================================================
// LSP Message I/O
// ============================================================================

// Read a line from fd into buffer (up to max_len-1 chars)
// Returns number of chars read, or -1 on error/EOF
static int read_line(int fd, char *buffer, int max_len) {
    int i = 0;
    char c;

    while (i < max_len - 1) {
        ssize_t n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            // EOF
            if (i == 0) return -1;
            break;
        }

        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            buffer[i++] = c;
        }
    }

    buffer[i] = '\0';
    return i;
}

// Read exactly n bytes from fd
static int read_exact(int fd, char *buffer, int n) {
    int total = 0;
    while (total < n) {
        ssize_t r = read(fd, buffer + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;  // EOF
        total += r;
    }
    return total;
}

LSPMessage *lsp_read_message(int fd) {
    char header[256];
    int content_length = -1;

    // Read headers until empty line
    while (1) {
        int len = read_line(fd, header, sizeof(header));
        if (len < 0) return NULL;  // EOF or error
        if (len == 0) break;       // Empty line = end of headers

        // Parse Content-Length header
        if (strncmp(header, "Content-Length:", 15) == 0) {
            content_length = atoi(header + 15);
        }
    }

    if (content_length < 0) {
        return NULL;
    }

    // Read body
    char *body = malloc(content_length + 1);
    if (read_exact(fd, body, content_length) < 0) {
        free(body);
        return NULL;
    }
    body[content_length] = '\0';

    // Parse JSON
    const char *error = NULL;
    JSONValue *json = json_parse(body, &error);
    free(body);

    if (!json) {
        return NULL;
    }

    // Create message
    LSPMessage *msg = calloc(1, sizeof(LSPMessage));
    msg->jsonrpc = strdup(json_object_get_string(json, "jsonrpc") ?: "2.0");
    msg->method = json_object_get_string(json, "method")
                  ? strdup(json_object_get_string(json, "method"))
                  : NULL;

    // Copy id if present
    JSONValue *id = json_object_get(json, "id");
    if (id) {
        if (id->type == JSON_NUMBER) {
            msg->id = json_number(id->as.number);
        } else if (id->type == JSON_STRING) {
            msg->id = json_string(id->as.string);
        }
    }

    // Copy params if present
    JSONValue *params = json_object_get(json, "params");
    if (params) {
        // Need to deep copy - for now, just take ownership
        // This is a bit hacky but works for our use case
        msg->params = params;
        // Remove from original object so it doesn't get freed
        JSONObject *o = json->as.object;
        for (int i = 0; i < o->count; i++) {
            if (strcmp(o->keys[i], "params") == 0) {
                o->values[i] = json_null();
                break;
            }
        }
    }

    json_free(json);
    return msg;
}

void lsp_write_message(int fd, LSPMessage *msg) {
    // Build response JSON
    JSONValue *response = json_object();
    json_object_set(response, "jsonrpc", json_string("2.0"));

    if (msg->id) {
        if (msg->id->type == JSON_NUMBER) {
            json_object_set(response, "id", json_number(msg->id->as.number));
        } else if (msg->id->type == JSON_STRING) {
            json_object_set(response, "id", json_string(msg->id->as.string));
        }
    }

    if (msg->method) {
        json_object_set(response, "method", json_string(msg->method));
    }

    if (msg->result) {
        // Take ownership of result
        json_object_set(response, "result", msg->result);
        msg->result = NULL;
    }

    if (msg->error) {
        json_object_set(response, "error", msg->error);
        msg->error = NULL;
    }

    if (msg->params) {
        json_object_set(response, "params", msg->params);
        msg->params = NULL;
    }

    // Serialize
    char *body = json_serialize(response);
    int body_len = strlen(body);

    // Write header + body
    char header[64];
    snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", body_len);

    ssize_t h_written = write(fd, header, strlen(header));
    ssize_t b_written = write(fd, body, body_len);
    (void)h_written;
    (void)b_written;

    free(body);
    json_free(response);
}

LSPMessage *lsp_response(JSONValue *id, JSONValue *result) {
    LSPMessage *msg = calloc(1, sizeof(LSPMessage));
    msg->jsonrpc = strdup("2.0");
    if (id) {
        if (id->type == JSON_NUMBER) {
            msg->id = json_number(id->as.number);
        } else if (id->type == JSON_STRING) {
            msg->id = json_string(id->as.string);
        }
    }
    msg->result = result;
    return msg;
}

LSPMessage *lsp_error_response(JSONValue *id, int code, const char *message) {
    LSPMessage *msg = calloc(1, sizeof(LSPMessage));
    msg->jsonrpc = strdup("2.0");
    if (id) {
        if (id->type == JSON_NUMBER) {
            msg->id = json_number(id->as.number);
        } else if (id->type == JSON_STRING) {
            msg->id = json_string(id->as.string);
        }
    }
    JSONValue *err = json_object();
    json_object_set(err, "code", json_number(code));
    json_object_set(err, "message", json_string(message));
    msg->error = err;
    return msg;
}

LSPMessage *lsp_notification(const char *method, JSONValue *params) {
    LSPMessage *msg = calloc(1, sizeof(LSPMessage));
    msg->jsonrpc = strdup("2.0");
    msg->method = strdup(method);
    msg->params = params;
    return msg;
}

void lsp_message_free(LSPMessage *msg) {
    if (!msg) return;
    free(msg->jsonrpc);
    free(msg->method);
    json_free(msg->id);
    json_free(msg->params);
    json_free(msg->result);
    json_free(msg->error);
    free(msg);
}

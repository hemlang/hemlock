/*
 * LSP JSON-RPC Protocol Handling
 *
 * Implements the JSON-RPC 2.0 protocol used by LSP:
 * - Message reading (Content-Length header + JSON body)
 * - Message writing
 * - JSON parsing for LSP messages
 * - JSON serialization for responses
 */

#ifndef HEMLOCK_LSP_PROTOCOL_H
#define HEMLOCK_LSP_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Forward declarations
typedef struct JSONValue JSONValue;
typedef struct JSONObject JSONObject;
typedef struct JSONArray JSONArray;

/*
 * JSON Value Types
 */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JSONType;

/*
 * JSON Object (key-value pairs)
 */
struct JSONObject {
    char **keys;
    JSONValue **values;
    int count;
    int capacity;
};

/*
 * JSON Array
 */
struct JSONArray {
    JSONValue **items;
    int count;
    int capacity;
};

/*
 * JSON Value (tagged union)
 */
struct JSONValue {
    JSONType type;
    union {
        bool boolean;
        double number;
        char *string;
        JSONArray *array;
        JSONObject *object;
    } as;
};

/*
 * JSON constructors
 */
JSONValue *json_null(void);
JSONValue *json_bool(bool value);
JSONValue *json_number(double value);
JSONValue *json_string(const char *value);
JSONValue *json_array(void);
JSONValue *json_object(void);

/*
 * JSON array operations
 */
void json_array_push(JSONValue *arr, JSONValue *item);

/*
 * JSON object operations
 */
void json_object_set(JSONValue *obj, const char *key, JSONValue *value);
JSONValue *json_object_get(JSONValue *obj, const char *key);
const char *json_object_get_string(JSONValue *obj, const char *key);
double json_object_get_number(JSONValue *obj, const char *key);
bool json_object_get_bool(JSONValue *obj, const char *key);
JSONValue *json_object_get_object(JSONValue *obj, const char *key);
JSONValue *json_object_get_array(JSONValue *obj, const char *key);
bool json_object_has(JSONValue *obj, const char *key);

/*
 * JSON parsing
 */
JSONValue *json_parse(const char *input, const char **error);

/*
 * JSON serialization
 */
char *json_serialize(JSONValue *value);

/*
 * JSON cleanup
 */
void json_free(JSONValue *value);

/*
 * LSP Message structure
 */
typedef struct {
    char *jsonrpc;      // Always "2.0"
    char *method;       // Request/notification method
    JSONValue *id;      // Request ID (null for notifications)
    JSONValue *params;  // Parameters
    JSONValue *result;  // Response result
    JSONValue *error;   // Response error
} LSPMessage;

/*
 * Read an LSP message from file descriptor
 * Returns NULL on EOF or error
 */
LSPMessage *lsp_read_message(int fd);

/*
 * Write an LSP message to file descriptor
 */
void lsp_write_message(int fd, LSPMessage *msg);

/*
 * Create an LSP response
 */
LSPMessage *lsp_response(JSONValue *id, JSONValue *result);

/*
 * Create an LSP error response
 */
LSPMessage *lsp_error_response(JSONValue *id, int code, const char *message);

/*
 * Create an LSP notification
 */
LSPMessage *lsp_notification(const char *method, JSONValue *params);

/*
 * Free an LSP message
 */
void lsp_message_free(LSPMessage *msg);

/*
 * LSP Error Codes
 */
#define LSP_ERROR_PARSE_ERROR      -32700
#define LSP_ERROR_INVALID_REQUEST  -32600
#define LSP_ERROR_METHOD_NOT_FOUND -32601
#define LSP_ERROR_INVALID_PARAMS   -32602
#define LSP_ERROR_INTERNAL_ERROR   -32603
#define LSP_ERROR_SERVER_NOT_INITIALIZED -32002
#define LSP_ERROR_REQUEST_CANCELLED -32800

#endif // HEMLOCK_LSP_PROTOCOL_H

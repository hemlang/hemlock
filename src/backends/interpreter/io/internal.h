#ifndef HEMLOCK_IO_INTERNAL_H
#define HEMLOCK_IO_INTERNAL_H

#include "../internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

// ========== SERIALIZATION SUPPORT ==========

// Cycle detection for JSON serialization (uses Object** specifically)
// Note: This is distinct from VisitedSet in internal.h which uses void** for general cycle detection
typedef struct {
    Object **visited;
    int count;
    int capacity;
} SerializeVisitedSet;

// JSON parsing
typedef struct {
    const char *input;
    int pos;
    int depth;   // current nesting depth (bounds recursion on untrusted JSON)
} JSONParser;

// Serialization functions
void serialize_visited_init(SerializeVisitedSet *set);
int serialize_visited_contains(SerializeVisitedSet *set, Object *obj);
int serialize_visited_add(SerializeVisitedSet *set, Object *obj);  // Returns 1 on success, 0 on alloc failure
void serialize_visited_free(SerializeVisitedSet *set);
char* escape_json_string(const char *str);
char* serialize_value(Value val, SerializeVisitedSet *visited, ExecutionContext *ctx);

// JSON parsing functions
void json_skip_whitespace(JSONParser *p);
Value json_parse_string(JSONParser *p, ExecutionContext *ctx);
Value json_parse_number(JSONParser *p, ExecutionContext *ctx);
Value json_parse_object(JSONParser *p, ExecutionContext *ctx);
Value json_parse_array(JSONParser *p, ExecutionContext *ctx);
Value json_parse_value(JSONParser *p, ExecutionContext *ctx);

// ========== ARRAY HELPERS ==========

// Value comparison for array methods
int values_equal(Value a, Value b);
// Loose (==) equality for switch cases and match literal patterns
int values_equal_loose(Value a, Value b);

// ========== METHOD HANDLERS ==========

// File methods
Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// Array methods
Value call_array_method(Array *arr, const char *method, Value *args, int num_args, int line, ExecutionContext *ctx);

// String methods
Value call_string_method(String *str, const char *method, Value *args, int num_args, int line, ExecutionContext *ctx);

// Channel methods
Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// Object methods
Value call_object_method(Object *obj, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// ========== I/O BUILTINS ==========

Value builtin_read_line(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_eprint(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_open(Value *args, int num_args, ExecutionContext *ctx);

#endif // HEMLOCK_IO_INTERNAL_H

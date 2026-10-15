#ifndef HEMLOCK_RUNTIME_MEMORY_H
#define HEMLOCK_RUNTIME_MEMORY_H

#include "runtime/types.h"
#include <stdint.h>

// Value constructors
Value val_int(int value);    // creates i32
Value val_float(double value); // creates f64
Value val_i8(int8_t value);
Value val_i16(int16_t value);
Value val_i32(int32_t value);
Value val_i64(int64_t value);
Value val_u8(uint8_t value);
Value val_u16(uint16_t value);
Value val_u32(uint32_t value);
Value val_u64(uint64_t value);
Value val_f32(float value);
Value val_f64(double value);
Value val_bool(int value);
Value val_string(const char *str);
Value val_string_take(char *str, int length, int capacity);
Value val_rune(uint32_t codepoint);
Value val_ptr(void *ptr);
Value val_buffer(int size);
Value val_array(Array *arr);
Value val_file(FileHandle *file);
Value val_type(TypeKind kind);
Value val_builtin_fn(BuiltinFn fn);
Value val_function(Function *fn);
Value val_object(Object *obj);
Value val_task(Task *task);
Value val_channel(Channel *channel);
Value val_socket(SocketHandle *sock);
Value val_websocket(WebSocketHandle *ws);
Value val_ref(Reference *ref);
Value val_null(void);

// Value operations
void print_value(Value val);
Value value_deep_copy(Value val);  // Deep copy for thread isolation
void value_retain(Value val);
void value_release(Value val);

// String operations
void string_free(String *str);
void string_retain(String *str);
void string_release(String *str);
String* string_concat(String *a, String *b);
String* string_concat_many(String **strings, int count);
String* string_copy(String *str);

// Buffer operations
void buffer_free(Buffer *buf);
void buffer_retain(Buffer *buf);
void buffer_release(Buffer *buf);

// Array operations
void array_free(Array *arr);
void array_release(Array *arr);
void array_retain(Array *arr);
Array* array_new(void);
void array_push(Array *arr, Value val);
Value array_pop(Array *arr);
Value array_get(Array *arr, int index, ExecutionContext *ctx);
void array_set(Array *arr, int index, Value val, ExecutionContext *ctx);

// File operations
void file_free(FileHandle *file);

// Socket operations
void socket_free(SocketHandle *sock);
void socket_retain(SocketHandle *sock);
void socket_release(SocketHandle *sock);

// WebSocket operations
void websocket_free(WebSocketHandle *ws);
void websocket_retain(WebSocketHandle *ws);
void websocket_release(WebSocketHandle *ws);

// Object operations
void object_free(Object *obj);
Object* object_new(char *type_name, int initial_capacity);
void object_retain(Object *obj);
void object_release(Object *obj);

// Function operations
void function_free(Function *fn);
void function_retain(Function *fn);
void function_release(Function *fn);

// Task operations
void task_free(Task *task);
Task* task_new(int id, Function *function, Value *args, int num_args, Environment *env);
void task_retain(Task *task);
void task_release(Task *task);

// Channel operations
void channel_free(Channel *channel);
Channel* channel_new(int capacity);
void channel_retain(Channel *channel);
void channel_release(Channel *channel);

// Reference operations (for pass-by-reference)
void reference_free(Reference *ref);
Reference* reference_new_variable(Environment *env, const char *name);
Reference* reference_new_array_index(Array *array, int index);
Reference* reference_new_object_property(Object *object, const char *property);
Value ref_deref(Reference *ref, ExecutionContext *ctx);  // Read through reference
void ref_assign(Reference *ref, Value value, ExecutionContext *ctx);  // Write through reference

#endif // HEMLOCK_RUNTIME_MEMORY_H

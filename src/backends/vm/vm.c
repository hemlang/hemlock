/*
 * Hemlock Bytecode VM - Virtual Machine Implementation
 *
 * Stack-based bytecode interpreter with computed goto dispatch.
 */

#define _POSIX_C_SOURCE 200809L

#include "vm.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>

// Debug tracing
static int vm_trace_enabled = 0;

// ============================================
// Value Helpers (matching interpreter semantics)
// ============================================

static Value vm_null_value(void) {
    Value v = {.type = VAL_NULL};
    return v;
}

static Value val_bool_vm(int b) {
    Value v = {.type = VAL_BOOL, .as.as_bool = b};
    return v;
}

static Value val_i32_vm(int32_t i) {
    Value v = {.type = VAL_I32, .as.as_i32 = i};
    return v;
}

static Value val_i64_vm(int64_t i) {
    Value v = {.type = VAL_I64, .as.as_i64 = i};
    return v;
}

static Value val_f64_vm(double f) {
    Value v = {.type = VAL_F64, .as.as_f64 = f};
    return v;
}

static int value_is_truthy(Value v) {
    switch (v.type) {
        case VAL_NULL: return 0;
        case VAL_BOOL: return v.as.as_bool != 0;
        case VAL_I32: return v.as.as_i32 != 0;
        case VAL_I64: return v.as.as_i64 != 0;
        case VAL_F64: return v.as.as_f64 != 0.0;
        case VAL_STRING: return v.as.as_string && v.as.as_string->length > 0;
        case VAL_ARRAY: return v.as.as_array && v.as.as_array->length > 0;
        default: return 1;  // Non-null objects are truthy
    }
}

// Convert value to double for arithmetic
static double value_to_f64(Value v) {
    switch (v.type) {
        case VAL_I8: return (double)v.as.as_i8;
        case VAL_I16: return (double)v.as.as_i16;
        case VAL_I32: return (double)v.as.as_i32;
        case VAL_I64: return (double)v.as.as_i64;
        case VAL_U8: return (double)v.as.as_u8;
        case VAL_U16: return (double)v.as.as_u16;
        case VAL_U32: return (double)v.as.as_u32;
        case VAL_U64: return (double)v.as.as_u64;
        case VAL_F32: return (double)v.as.as_f32;
        case VAL_F64: return v.as.as_f64;
        default: return 0.0;
    }
}

// Convert value to i64 for integer ops
static int64_t value_to_i64(Value v) {
    switch (v.type) {
        case VAL_I8: return (int64_t)v.as.as_i8;
        case VAL_I16: return (int64_t)v.as.as_i16;
        case VAL_I32: return (int64_t)v.as.as_i32;
        case VAL_I64: return v.as.as_i64;
        case VAL_U8: return (int64_t)v.as.as_u8;
        case VAL_U16: return (int64_t)v.as.as_u16;
        case VAL_U32: return (int64_t)v.as.as_u32;
        case VAL_U64: return (int64_t)v.as.as_u64;
        case VAL_F32: return (int64_t)v.as.as_f32;
        case VAL_F64: return (int64_t)v.as.as_f64;
        default: return 0;
    }
}

// Convert value to i32
static int32_t value_to_i32(Value v) {
    return (int32_t)value_to_i64(v);
}

// Compare two values for equality
static bool vm_values_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NULL: return true;
        case VAL_BOOL: return a.as.as_bool == b.as.as_bool;
        case VAL_I8: return a.as.as_i8 == b.as.as_i8;
        case VAL_I16: return a.as.as_i16 == b.as.as_i16;
        case VAL_I32: return a.as.as_i32 == b.as.as_i32;
        case VAL_I64: return a.as.as_i64 == b.as.as_i64;
        case VAL_U8: return a.as.as_u8 == b.as.as_u8;
        case VAL_U16: return a.as.as_u16 == b.as.as_u16;
        case VAL_U32: return a.as.as_u32 == b.as.as_u32;
        case VAL_U64: return a.as.as_u64 == b.as.as_u64;
        case VAL_F32: return a.as.as_f32 == b.as.as_f32;
        case VAL_F64: return a.as.as_f64 == b.as.as_f64;
        case VAL_STRING:
            if (!a.as.as_string || !b.as.as_string) return a.as.as_string == b.as.as_string;
            return strcmp(a.as.as_string->data, b.as.as_string->data) == 0;
        case VAL_ARRAY: return a.as.as_array == b.as.as_array;
        case VAL_OBJECT: return a.as.as_object == b.as.as_object;
        default: return false;
    }
}

// Create a new string Value
static Value vm_make_string(const char *data, int len) {
    String *s = malloc(sizeof(String));
    s->data = malloc(len + 1);
    memcpy(s->data, data, len);
    s->data[len] = '\0';
    s->length = len;
    s->char_length = len;
    s->capacity = len + 1;
    s->ref_count = 1;
    Value v;
    v.type = VAL_STRING;
    v.as.as_string = s;
    return v;
}

// ============================================
// Simple JSON Parser for deserialize()
// ============================================

static const char *json_skip_whitespace(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static Value vm_json_parse_value(const char **pp, const char *end);

static Value vm_json_parse_string(const char **pp, const char *end) {
    const char *p = *pp;
    if (p >= end || *p != '"') return vm_null_value();
    p++;  // Skip opening quote

    // Find closing quote and calculate length
    const char *start = p;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) p++;  // Skip escaped char
        p++;
    }
    int len = p - start;
    char *buf = malloc(len + 1);

    // Copy with escape handling
    const char *src = start;
    char *dst = buf;
    while (src < p) {
        if (*src == '\\' && src + 1 < p) {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    int actual_len = dst - buf;

    if (p < end) p++;  // Skip closing quote
    *pp = p;

    Value result = vm_make_string(buf, actual_len);
    free(buf);
    return result;
}

static Value vm_json_parse_number(const char **pp, const char *end) {
    const char *p = *pp;
    const char *start = p;
    bool is_float = false;

    if (p < end && *p == '-') p++;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p < end && *p == '.') {
        is_float = true;
        p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        is_float = true;
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }

    char buf[64];
    int len = p - start;
    if (len >= 63) len = 63;
    memcpy(buf, start, len);
    buf[len] = '\0';

    *pp = p;

    Value result;
    if (is_float) {
        result.type = VAL_F64;
        result.as.as_f64 = atof(buf);
    } else {
        int64_t n = strtoll(buf, NULL, 10);
        if (n >= INT32_MIN && n <= INT32_MAX) {
            result.type = VAL_I32;
            result.as.as_i32 = (int32_t)n;
        } else {
            result.type = VAL_I64;
            result.as.as_i64 = n;
        }
    }
    return result;
}

static Value vm_json_parse_array(const char **pp, const char *end) {
    const char *p = *pp;
    p++;  // Skip '['
    p = json_skip_whitespace(p, end);

    Array *arr = malloc(sizeof(Array));
    arr->elements = malloc(sizeof(Value) * 8);
    arr->length = 0;
    arr->capacity = 8;
    arr->element_type = NULL;
    arr->ref_count = 1;

    while (p < end && *p != ']') {
        Value elem = vm_json_parse_value(&p, end);
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        arr->elements[arr->length++] = elem;

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ',') {
            p++;
            p = json_skip_whitespace(p, end);
        }
    }
    if (p < end) p++;  // Skip ']'
    *pp = p;

    Value result;
    result.type = VAL_ARRAY;
    result.as.as_array = arr;
    return result;
}

static Value vm_json_parse_object(const char **pp, const char *end) {
    const char *p = *pp;
    p++;  // Skip '{'
    p = json_skip_whitespace(p, end);

    Object *obj = malloc(sizeof(Object));
    obj->type_name = NULL;
    obj->field_names = malloc(sizeof(char*) * 8);
    obj->field_values = malloc(sizeof(Value) * 8);
    obj->num_fields = 0;
    obj->capacity = 8;
    obj->ref_count = 1;

    while (p < end && *p != '}') {
        // Parse key
        Value key = vm_json_parse_string(&p, end);
        if (key.type != VAL_STRING) break;

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ':') p++;
        p = json_skip_whitespace(p, end);

        // Parse value
        Value val = vm_json_parse_value(&p, end);

        // Add to object
        if (obj->num_fields >= obj->capacity) {
            obj->capacity *= 2;
            obj->field_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
            obj->field_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
        }
        obj->field_names[obj->num_fields] = strdup(key.as.as_string->data);
        obj->field_values[obj->num_fields] = val;
        obj->num_fields++;

        // Free key string
        free(key.as.as_string->data);
        free(key.as.as_string);

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ',') {
            p++;
            p = json_skip_whitespace(p, end);
        }
    }
    if (p < end) p++;  // Skip '}'
    *pp = p;

    Value result;
    result.type = VAL_OBJECT;
    result.as.as_object = obj;
    return result;
}

static Value vm_json_parse_value(const char **pp, const char *end) {
    const char *p = json_skip_whitespace(*pp, end);
    *pp = p;

    if (p >= end) return vm_null_value();

    if (*p == '"') {
        return vm_json_parse_string(pp, end);
    } else if (*p == '{') {
        return vm_json_parse_object(pp, end);
    } else if (*p == '[') {
        return vm_json_parse_array(pp, end);
    } else if (*p == 't' && p + 4 <= end && strncmp(p, "true", 4) == 0) {
        *pp = p + 4;
        return val_bool_vm(true);
    } else if (*p == 'f' && p + 5 <= end && strncmp(p, "false", 5) == 0) {
        *pp = p + 5;
        return val_bool_vm(false);
    } else if (*p == 'n' && p + 4 <= end && strncmp(p, "null", 4) == 0) {
        *pp = p + 4;
        return vm_null_value();
    } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return vm_json_parse_number(pp, end);
    }

    return vm_null_value();
}

static Value vm_json_parse(const char *json, int len) {
    const char *p = json;
    const char *end = json + len;
    return vm_json_parse_value(&p, end);
}

// Check if value is a numeric type
static int is_numeric(ValueType t) {
    return t >= VAL_I8 && t <= VAL_F64;
}

// Check if value is a float type
static int is_float(ValueType t) {
    return t == VAL_F32 || t == VAL_F64;
}

// Convert ValueType to a string name
static const char* val_type_name(ValueType t) {
    switch (t) {
        case VAL_NULL: return "null";
        case VAL_BOOL: return "bool";
        case VAL_I8: return "i8";
        case VAL_I16: return "i16";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U8: return "u8";
        case VAL_U16: return "u16";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F32: return "f32";
        case VAL_F64: return "f64";
        case VAL_STRING: return "string";
        case VAL_RUNE: return "rune";
        case VAL_PTR: return "ptr";
        case VAL_BUFFER: return "buffer";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FILE: return "file";
        case VAL_FUNCTION: return "function";
        case VAL_TASK: return "task";
        case VAL_CHANNEL: return "channel";
        default: return "unknown";
    }
}

// Convert a value to a string representation (allocates memory)
static char* value_to_string_alloc(Value v) {
    char buf[256];
    switch (v.type) {
        case VAL_NULL:
            return strdup("null");
        case VAL_BOOL:
            return strdup(v.as.as_bool ? "true" : "false");
        case VAL_I8:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i8);
            return strdup(buf);
        case VAL_I16:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i16);
            return strdup(buf);
        case VAL_I32:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i32);
            return strdup(buf);
        case VAL_I64:
            snprintf(buf, sizeof(buf), "%ld", (long)v.as.as_i64);
            return strdup(buf);
        case VAL_U8:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u8);
            return strdup(buf);
        case VAL_U16:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u16);
            return strdup(buf);
        case VAL_U32:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u32);
            return strdup(buf);
        case VAL_U64:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)v.as.as_u64);
            return strdup(buf);
        case VAL_F32:
        case VAL_F64: {
            double d = v.type == VAL_F32 ? v.as.as_f32 : v.as.as_f64;
            if (d == (long)d) {
                snprintf(buf, sizeof(buf), "%.0f", d);
            } else {
                snprintf(buf, sizeof(buf), "%g", d);
            }
            return strdup(buf);
        }
        case VAL_STRING:
            if (v.as.as_string) {
                return strdup(v.as.as_string->data);
            }
            return strdup("");
        case VAL_RUNE: {
            // Convert rune to UTF-8 string
            uint32_t r = v.as.as_rune;
            if (r < 0x80) {
                buf[0] = (char)r;
                buf[1] = '\0';
            } else if (r < 0x800) {
                buf[0] = (char)(0xC0 | (r >> 6));
                buf[1] = (char)(0x80 | (r & 0x3F));
                buf[2] = '\0';
            } else if (r < 0x10000) {
                buf[0] = (char)(0xE0 | (r >> 12));
                buf[1] = (char)(0x80 | ((r >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (r & 0x3F));
                buf[3] = '\0';
            } else {
                buf[0] = (char)(0xF0 | (r >> 18));
                buf[1] = (char)(0x80 | ((r >> 12) & 0x3F));
                buf[2] = (char)(0x80 | ((r >> 6) & 0x3F));
                buf[3] = (char)(0x80 | (r & 0x3F));
                buf[4] = '\0';
            }
            return strdup(buf);
        }
        case VAL_ARRAY:
            return strdup("[array]");
        case VAL_OBJECT:
            return strdup("[object]");
        case VAL_FUNCTION:
            return strdup("[function]");
        default:
            return strdup("[unknown]");
    }
}

// ============================================
// VM Lifecycle
// ============================================

VM* vm_new(void) {
    VM *vm = malloc(sizeof(VM));
    if (!vm) return NULL;

    // Initialize stack
    vm->stack = malloc(sizeof(Value) * VM_STACK_INITIAL);
    vm->stack_top = vm->stack;
    vm->stack_capacity = VM_STACK_INITIAL;

    // Initialize call frames
    vm->frames = malloc(sizeof(CallFrame) * VM_FRAMES_INITIAL);
    vm->frame_count = 0;
    vm->frame_capacity = VM_FRAMES_INITIAL;

    // Initialize globals
    vm->globals.names = malloc(sizeof(char*) * VM_GLOBALS_INITIAL);
    vm->globals.values = malloc(sizeof(Value) * VM_GLOBALS_INITIAL);
    vm->globals.is_const = malloc(sizeof(bool) * VM_GLOBALS_INITIAL);
    vm->globals.count = 0;
    vm->globals.capacity = VM_GLOBALS_INITIAL;
    vm->globals.hash_table = NULL;
    vm->globals.hash_capacity = 0;

    // Control flow state
    vm->is_returning = false;
    vm->return_value = vm_null_value();
    vm->is_throwing = false;
    vm->exception = vm_null_value();
    vm->exception_frame = NULL;
    vm->is_breaking = false;
    vm->is_continuing = false;

    // Defers
    vm->defers = malloc(sizeof(DeferEntry) * VM_DEFER_INITIAL);
    vm->defer_count = 0;
    vm->defer_capacity = VM_DEFER_INITIAL;

    // Exception handlers
    vm->handlers = malloc(sizeof(ExceptionHandler) * 16);
    vm->handler_count = 0;
    vm->handler_capacity = 16;

    // Module cache
    vm->module_cache.paths = NULL;
    vm->module_cache.modules = NULL;
    vm->module_cache.count = 0;
    vm->module_cache.capacity = 0;

    // GC/memory
    vm->open_upvalues = NULL;
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;  // 1MB

    vm->max_stack_depth = 1024;
    vm->task = NULL;

    vm->pending_error = NULL;

    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;

    free(vm->stack);
    free(vm->frames);

    // Free globals
    for (int i = 0; i < vm->globals.count; i++) {
        free(vm->globals.names[i]);
        // TODO: VALUE_RELEASE(vm->globals.values[i]);
    }
    free(vm->globals.names);
    free(vm->globals.values);
    free(vm->globals.is_const);
    free(vm->globals.hash_table);

    free(vm->defers);

    // Free module cache
    for (int i = 0; i < vm->module_cache.count; i++) {
        free(vm->module_cache.paths[i]);
    }
    free(vm->module_cache.paths);
    free(vm->module_cache.modules);

    // TODO: Free all allocated objects

    free(vm);
}

void vm_reset(VM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->is_returning = false;
    vm->is_throwing = false;
    vm->is_breaking = false;
    vm->is_continuing = false;
    vm->defer_count = 0;
}

// ============================================
// Stack Operations
// ============================================

void vm_push(VM *vm, Value value) {
    if (vm->stack_top - vm->stack >= vm->stack_capacity) {
        // Grow stack
        int old_top = vm->stack_top - vm->stack;
        vm->stack_capacity *= 2;
        vm->stack = realloc(vm->stack, sizeof(Value) * vm->stack_capacity);
        vm->stack_top = vm->stack + old_top;
    }
    *vm->stack_top++ = value;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top <= vm->stack) {
        vm_runtime_error(vm, "Stack underflow");
        return vm_null_value();
    }
    return *--vm->stack_top;
}

Value vm_peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

void vm_popn(VM *vm, int count) {
    vm->stack_top -= count;
    if (vm->stack_top < vm->stack) {
        vm->stack_top = vm->stack;
        vm_runtime_error(vm, "Stack underflow");
    }
}

// ============================================
// Globals
// ============================================

static uint32_t hash_string_vm(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619;
    }
    return hash;
}

void vm_define_global(VM *vm, const char *name, Value value, bool is_const) {
    // Check for existing
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            vm->globals.values[i] = value;
            return;
        }
    }

    // Add new
    if (vm->globals.count >= vm->globals.capacity) {
        vm->globals.capacity *= 2;
        vm->globals.names = realloc(vm->globals.names, sizeof(char*) * vm->globals.capacity);
        vm->globals.values = realloc(vm->globals.values, sizeof(Value) * vm->globals.capacity);
        vm->globals.is_const = realloc(vm->globals.is_const, sizeof(bool) * vm->globals.capacity);
    }

    vm->globals.names[vm->globals.count] = strdup(name);
    vm->globals.values[vm->globals.count] = value;
    vm->globals.is_const[vm->globals.count] = is_const;
    vm->globals.count++;
}

bool vm_get_global(VM *vm, const char *name, Value *out) {
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            *out = vm->globals.values[i];
            return true;
        }
    }
    return false;
}

// Helper macros for setting catchable errors from static helper functions
// After calling a helper function, check vm->pending_error and handle it
#define SET_ERROR(vm, msg) do { (vm)->pending_error = (msg); } while(0)
#define SET_ERROR_FMT(vm, fmt, ...) do { \
    snprintf((vm)->error_buf, sizeof((vm)->error_buf), fmt, ##__VA_ARGS__); \
    (vm)->pending_error = (vm)->error_buf; \
} while(0)

bool vm_set_global(VM *vm, const char *name, Value value) {
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            if (vm->globals.is_const[i]) {
                SET_ERROR_FMT(vm, "Cannot reassign constant '%s'", name);
                return false;
            }
            vm->globals.values[i] = value;
            return true;
        }
    }
    SET_ERROR_FMT(vm, "Undefined variable '%s'", name);
    return false;
}

// ============================================
// VM Closures
// ============================================

VMClosure* vm_closure_new(Chunk *chunk) {
    VMClosure *closure = malloc(sizeof(VMClosure));
    closure->chunk = chunk;
    closure->upvalue_count = chunk->upvalue_count;
    if (closure->upvalue_count > 0) {
        closure->upvalues = malloc(sizeof(ObjUpvalue*) * closure->upvalue_count);
        for (int i = 0; i < closure->upvalue_count; i++) {
            closure->upvalues[i] = NULL;
        }
    } else {
        closure->upvalues = NULL;
    }
    closure->ref_count = 1;
    return closure;
}

void vm_closure_free(VMClosure *closure) {
    if (!closure) return;
    if (--closure->ref_count <= 0) {
        free(closure->upvalues);
        // Note: Don't free the chunk - it's owned by the constant pool
        free(closure);
    }
}

// Create a Value from a VMClosure (stores as function pointer)
static Value val_vm_closure(VMClosure *closure) {
    Value v;
    v.type = VAL_FUNCTION;
    // Store the closure pointer - we'll cast it back when calling
    v.as.as_function = (Function*)closure;
    return v;
}

// Check if a Value is a VM closure (vs an interpreter function)
// VM closures have a special marker - they came from the VM context
// For now, all functions in the VM are closures
static bool is_vm_closure(Value v) {
    return v.type == VAL_FUNCTION && v.as.as_function != NULL;
}

// Get the VMClosure from a Value
static VMClosure* as_vm_closure(Value v) {
    return (VMClosure*)v.as.as_function;
}

// ============================================
// Error Handling
// ============================================

void vm_runtime_error(VM *vm, const char *format, ...) {
    fflush(stdout);  // Ensure all output is printed before error
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Runtime error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);

    // Print stack trace
    vm_print_stack_trace(vm);

    vm->is_throwing = true;
}

int vm_current_line(VM *vm) {
    if (vm->frame_count == 0) return 0;
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    int offset = frame->ip - frame->chunk->code;
    return chunk_get_line(frame->chunk, offset);
}

void vm_print_stack_trace(VM *vm) {
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        int offset = frame->ip - frame->chunk->code;
        int line = chunk_get_line(frame->chunk, offset);
        fprintf(stderr, "  at %s:%d\n",
                frame->chunk->name ? frame->chunk->name : "<script>", line);
    }
}

// ============================================
// Upvalues (for closures)
// ============================================

ObjUpvalue* vm_capture_upvalue(VM *vm, Value *local) {
    ObjUpvalue *prev = NULL;
    ObjUpvalue *upvalue = vm->open_upvalues;

    // Find existing upvalue or insertion point
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    // Create new upvalue
    ObjUpvalue *new_upvalue = malloc(sizeof(ObjUpvalue));
    new_upvalue->location = local;
    new_upvalue->closed = vm_null_value();
    new_upvalue->next = upvalue;

    if (prev == NULL) {
        vm->open_upvalues = new_upvalue;
    } else {
        prev->next = new_upvalue;
    }

    return new_upvalue;
}

void vm_close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        ObjUpvalue *upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

// ============================================
// Binary Operations with Type Promotion
// ============================================

// Determine result type for binary operation
static ValueType promote_types(ValueType a, ValueType b) {
    // Float always wins
    if (is_float(a) || is_float(b)) {
        return VAL_F64;
    }

    // Both are integers - use larger type
    int rank_a = a - VAL_I8;
    int rank_b = b - VAL_I8;
    return (rank_a > rank_b) ? a : b;
}

// Create value of specified type from int64
static Value val_int_typed(int64_t val, ValueType type) {
    Value v;
    v.type = type;
    switch (type) {
        case VAL_I8:  v.as.as_i8 = (int8_t)val; break;
        case VAL_I16: v.as.as_i16 = (int16_t)val; break;
        case VAL_I32: v.as.as_i32 = (int32_t)val; break;
        case VAL_I64: v.as.as_i64 = val; break;
        case VAL_U8:  v.as.as_u8 = (uint8_t)val; break;
        case VAL_U16: v.as.as_u16 = (uint16_t)val; break;
        case VAL_U32: v.as.as_u32 = (uint32_t)val; break;
        case VAL_U64: v.as.as_u64 = (uint64_t)val; break;
        default: v.type = VAL_I32; v.as.as_i32 = (int32_t)val; break;
    }
    return v;
}

// Create value of specified float type
static Value val_float_typed(double val, ValueType type) {
    Value v;
    v.type = type;
    if (type == VAL_F32) {
        v.as.as_f32 = (float)val;
    } else {
        v.as.as_f64 = val;
    }
    return v;
}

// Binary arithmetic with type promotion
static Value binary_add(VM *vm, Value a, Value b) {
    (void)vm; // May be unused after removing error

    // i32 fast path
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 + b.as.as_i32);
    }

    // String concatenation - if either operand is a string, convert and concat
    if (a.type == VAL_STRING || b.type == VAL_STRING) {
        char *str_a = value_to_string_alloc(a);
        char *str_b = value_to_string_alloc(b);
        int len_a = strlen(str_a);
        int len_b = strlen(str_b);
        int total_len = len_a + len_b;

        String *s = malloc(sizeof(String));
        s->data = malloc(total_len + 1);
        memcpy(s->data, str_a, len_a);
        memcpy(s->data + len_a, str_b, len_b);
        s->data[total_len] = '\0';
        s->length = total_len;
        s->char_length = -1;
        s->capacity = total_len + 1;
        s->ref_count = 1;

        free(str_a);
        free(str_b);

        Value v = {.type = VAL_STRING, .as.as_string = s};
        return v;
    }

    // Numeric
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            // Float promotion: use larger float type, or f32 if int + f32
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) + value_to_f64(b), result_type);
        }
        // Both integers - use proper promoted type
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) + value_to_i64(b), result_type);
    }

    SET_ERROR_FMT(vm, "Cannot add %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_sub(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 - b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) - value_to_f64(b), result_type);
        }
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) - value_to_i64(b), result_type);
    }
    SET_ERROR_FMT(vm, "Cannot subtract %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_mul(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 * b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) * value_to_f64(b), result_type);
        }
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) * value_to_i64(b), result_type);
    }
    SET_ERROR_FMT(vm, "Cannot multiply %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_div(VM *vm, Value a, Value b) {
    // Division always returns f64 (Hemlock semantics)
    double bval = value_to_f64(b);
    if (bval == 0.0) {
        SET_ERROR(vm, "Division by zero");
        return vm_null_value();
    }
    return val_f64_vm(value_to_f64(a) / bval);
}

static Value binary_mod(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        if (b.as.as_i32 == 0) {
            SET_ERROR(vm, "Division by zero");
            return vm_null_value();
        }
        return val_i32_vm(a.as.as_i32 % b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            double bval = value_to_f64(b);
            if (bval == 0.0) {
                SET_ERROR(vm, "Division by zero");
                return vm_null_value();
            }
            return val_f64_vm(fmod(value_to_f64(a), bval));
        }
        int64_t bval = value_to_i64(b);
        if (bval == 0) {
            SET_ERROR(vm, "Division by zero");
            return vm_null_value();
        }
        if (a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) % bval);
        }
        return val_i32_vm((int32_t)(value_to_i64(a) % bval));
    }
    SET_ERROR_FMT(vm, "Cannot modulo %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

// Comparison operations
static Value binary_eq(Value a, Value b) {
    if (a.type != b.type) {
        // Cross-type numeric comparison
        if (is_numeric(a.type) && is_numeric(b.type)) {
            return val_bool_vm(value_to_f64(a) == value_to_f64(b));
        }
        return val_bool_vm(0);
    }
    switch (a.type) {
        case VAL_NULL: return val_bool_vm(1);
        case VAL_BOOL: return val_bool_vm(a.as.as_bool == b.as.as_bool);
        case VAL_I32: return val_bool_vm(a.as.as_i32 == b.as.as_i32);
        case VAL_I64: return val_bool_vm(a.as.as_i64 == b.as.as_i64);
        case VAL_F64: return val_bool_vm(a.as.as_f64 == b.as.as_f64);
        case VAL_STRING:
            if (a.as.as_string == b.as.as_string) return val_bool_vm(1);
            if (!a.as.as_string || !b.as.as_string) return val_bool_vm(0);
            return val_bool_vm(strcmp(a.as.as_string->data, b.as.as_string->data) == 0);
        default:
            // Pointer equality for other types
            return val_bool_vm(a.as.as_ptr == b.as.as_ptr);
    }
}

static Value binary_lt(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_bool_vm(a.as.as_i32 < b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        return val_bool_vm(value_to_f64(a) < value_to_f64(b));
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        return val_bool_vm(strcmp(a.as.as_string->data, b.as.as_string->data) < 0);
    }
    SET_ERROR_FMT(vm, "Cannot compare %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

// ============================================
// Closure Call Helper (for array methods)
// ============================================

// Forward declaration of the execution loop
static VMResult vm_execute(VM *vm, int base_frame_count);

// Call a closure and return its result
// Returns vm_null_value() on error
static Value vm_call_closure(VM *vm, VMClosure *closure, Value *args, int argc) {
    // Push closure to stack (using val_vm_closure helper)
    Value closure_val = val_vm_closure(closure);
    *vm->stack_top++ = closure_val;

    // Push arguments
    for (int i = 0; i < argc; i++) {
        *vm->stack_top++ = args[i];
    }

    // Save base frame count to know when callback returns
    int base_frame_count = vm->frame_count;

    // Set up call frame (similar to BC_CALL)
    if (vm->frame_count >= vm->frame_capacity) {
        vm->frame_capacity *= 2;
        vm->frames = realloc(vm->frames, sizeof(CallFrame) * vm->frame_capacity);
    }

    Chunk *fn_chunk = closure->chunk;
    CallFrame *new_frame = &vm->frames[vm->frame_count++];
    new_frame->chunk = fn_chunk;
    new_frame->ip = fn_chunk->code;
    new_frame->slots = vm->stack_top - argc - 1;
    new_frame->upvalues = NULL;
    new_frame->slot_count = fn_chunk->local_count;

    // Store closure in slot 0 for upvalue access
    new_frame->slots[0] = closure_val;

    // Execute until we return to base frame
    VMResult result = vm_execute(vm, base_frame_count);

    if (result != VM_OK) {
        return vm_null_value();
    }

    // Result should be on stack (pushed by BC_RETURN)
    if (vm->stack_top > vm->stack) {
        return *--vm->stack_top;
    }
    return vm_null_value();
}

// ============================================
// Main Execution Loop
// ============================================

// Core execution loop - runs until frame_count reaches base_frame_count
static VMResult vm_execute(VM *vm, int base_frame_count) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    uint8_t *ip = frame->ip;
    Value *slots = frame->slots;

// Undefine instruction.h's parametrized macros, use local versions
#undef READ_BYTE
#undef READ_SHORT
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() (frame->chunk->constants[READ_SHORT()])
#define PUSH(v) (*vm->stack_top++ = (v))
#define POP() (*--vm->stack_top)
#define PEEK(n) (vm->stack_top[-1 - (n)])

// Macro for throwing catchable errors
// If a handler exists, jump to it; otherwise return runtime error
#define THROW_ERROR(msg) do { \
    pending_exception_msg = (msg); \
    goto handle_exception; \
} while(0)

#define THROW_ERROR_FMT(fmt, ...) do { \
    snprintf(exception_buf, sizeof(exception_buf), fmt, ##__VA_ARGS__); \
    pending_exception_msg = exception_buf; \
    goto handle_exception; \
} while(0)

    // Exception handling state
    const char *pending_exception_msg = NULL;
    char exception_buf[256];

    for (;;) {
        if (vm_trace_enabled) {
            // Print stack
            printf("          ");
            for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
                printf("[ ");
                print_value(*slot);
                printf(" ]");
            }
            printf("\n");
            disassemble_instruction(frame->chunk, (int)(ip - frame->chunk->code));
        }

        uint8_t instruction = READ_BYTE();

        switch (instruction) {
            // Constants
            case BC_CONST: {
                Constant c = READ_CONSTANT();
                Value v;
                switch (c.type) {
                    case CONST_I32: v = val_i32_vm(c.as.i32); break;
                    case CONST_I64: v = val_i64_vm(c.as.i64); break;
                    case CONST_F64: v = val_f64_vm(c.as.f64); break;
                    case CONST_STRING: {
                        // Create string value
                        String *s = malloc(sizeof(String));
                        s->data = strdup(c.as.string.data);
                        s->length = c.as.string.length;
                        s->char_length = -1;
                        s->capacity = c.as.string.length + 1;
                        s->ref_count = 1;
                        v.type = VAL_STRING;
                        v.as.as_string = s;
                        break;
                    }
                    default:
                        v = vm_null_value();
                }
                PUSH(v);
                break;
            }

            case BC_CONST_BYTE: {
                PUSH(val_i32_vm(READ_BYTE()));
                break;
            }

            case BC_NULL:
                PUSH(vm_null_value());
                break;

            case BC_TRUE:
                PUSH(val_bool_vm(1));
                break;

            case BC_FALSE:
                PUSH(val_bool_vm(0));
                break;

            case BC_ARRAY: {
                uint16_t count = READ_SHORT();
                // Allocate array
                Array *arr = malloc(sizeof(Array));
                arr->elements = malloc(sizeof(Value) * (count > 0 ? count : 1));
                arr->length = count;
                arr->capacity = count > 0 ? count : 1;
                arr->element_type = NULL;
                arr->ref_count = 1;
                // Pop elements from stack (they're in reverse order)
                for (int i = count - 1; i >= 0; i--) {
                    arr->elements[i] = POP();
                }
                Value v = {.type = VAL_ARRAY, .as.as_array = arr};
                PUSH(v);
                break;
            }

            case BC_OBJECT: {
                uint16_t count = READ_SHORT();
                // Allocate object
                Object *obj = malloc(sizeof(Object));
                obj->field_names = malloc(sizeof(char*) * (count > 0 ? count : 1));
                obj->field_values = malloc(sizeof(Value) * (count > 0 ? count : 1));
                obj->num_fields = count;
                obj->capacity = count > 0 ? count : 1;
                obj->ref_count = 1;
                // Pop key-value pairs (value first, then key, in reverse order)
                for (int i = count - 1; i >= 0; i--) {
                    Value val = POP();
                    Value key = POP();
                    if (key.type == VAL_STRING && key.as.as_string) {
                        obj->field_names[i] = strdup(key.as.as_string->data);
                    } else {
                        obj->field_names[i] = strdup("?");
                    }
                    obj->field_values[i] = val;
                }
                Value v = {.type = VAL_OBJECT, .as.as_object = obj};
                PUSH(v);
                break;
            }

            case BC_STRING_INTERP: {
                uint16_t count = READ_SHORT();
                // Concatenate all parts on the stack into a single string
                // First, compute total length
                size_t total_len = 0;
                Value *parts = vm->stack_top - count;
                for (int i = 0; i < count; i++) {
                    Value v = parts[i];
                    if (v.type == VAL_STRING && v.as.as_string) {
                        total_len += v.as.as_string->length;
                    } else {
                        // Convert to string representation
                        char buf[64];
                        switch (v.type) {
                            case VAL_NULL: total_len += 4; break;  // "null"
                            case VAL_BOOL: total_len += v.as.as_bool ? 4 : 5; break;  // "true"/"false"
                            case VAL_I32: snprintf(buf, 64, "%d", v.as.as_i32); total_len += strlen(buf); break;
                            case VAL_I64: snprintf(buf, 64, "%lld", (long long)v.as.as_i64); total_len += strlen(buf); break;
                            case VAL_F64: snprintf(buf, 64, "%g", v.as.as_f64); total_len += strlen(buf); break;
                            default: total_len += 16; break;  // "<type>"
                        }
                    }
                }

                // Allocate result string
                char *result = malloc(total_len + 1);
                char *ptr = result;

                // Build the result
                for (int i = 0; i < count; i++) {
                    Value v = parts[i];
                    if (v.type == VAL_STRING && v.as.as_string) {
                        memcpy(ptr, v.as.as_string->data, v.as.as_string->length);
                        ptr += v.as.as_string->length;
                    } else {
                        char buf[64];
                        int len = 0;
                        switch (v.type) {
                            case VAL_NULL: strcpy(ptr, "null"); len = 4; break;
                            case VAL_BOOL: strcpy(ptr, v.as.as_bool ? "true" : "false"); len = v.as.as_bool ? 4 : 5; break;
                            case VAL_I8: len = sprintf(ptr, "%d", v.as.as_i8); break;
                            case VAL_I16: len = sprintf(ptr, "%d", v.as.as_i16); break;
                            case VAL_I32: len = sprintf(ptr, "%d", v.as.as_i32); break;
                            case VAL_I64: len = sprintf(ptr, "%lld", (long long)v.as.as_i64); break;
                            case VAL_U8: len = sprintf(ptr, "%u", v.as.as_u8); break;
                            case VAL_U16: len = sprintf(ptr, "%u", v.as.as_u16); break;
                            case VAL_U32: len = sprintf(ptr, "%u", v.as.as_u32); break;
                            case VAL_U64: len = sprintf(ptr, "%llu", (unsigned long long)v.as.as_u64); break;
                            case VAL_F32: len = sprintf(ptr, "%g", v.as.as_f32); break;
                            case VAL_F64: len = sprintf(ptr, "%g", v.as.as_f64); break;
                            default: len = sprintf(ptr, "<%s>", val_type_name(v.type)); break;
                        }
                        ptr += len;
                    }
                }
                *ptr = '\0';

                // Pop all parts
                vm_popn(vm, count);

                // Create and push result string
                String *s = malloc(sizeof(String));
                s->data = result;
                s->length = ptr - result;
                s->char_length = s->length;  // TODO: compute UTF-8 char length
                s->capacity = total_len + 1;
                s->ref_count = 1;

                Value str_val = {.type = VAL_STRING, .as.as_string = s};
                PUSH(str_val);
                break;
            }

            // Variables
            case BC_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                PUSH(slots[slot]);
                break;
            }

            case BC_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                slots[slot] = PEEK(0);
                break;
            }

            case BC_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                // The closure is stored in slot 0 of this frame
                Value closure_val = slots[0];
                if (is_vm_closure(closure_val)) {
                    VMClosure *closure = as_vm_closure(closure_val);
                    if (closure && slot < closure->upvalue_count && closure->upvalues[slot]) {
                        ObjUpvalue *upvalue = closure->upvalues[slot];
                        PUSH(*upvalue->location);
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    PUSH(vm_null_value());
                }
                break;
            }

            case BC_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                Value closure_val = slots[0];
                if (is_vm_closure(closure_val)) {
                    VMClosure *closure = as_vm_closure(closure_val);
                    if (closure && slot < closure->upvalue_count && closure->upvalues[slot]) {
                        ObjUpvalue *upvalue = closure->upvalues[slot];
                        *upvalue->location = PEEK(0);
                    }
                }
                break;
            }

            case BC_GET_SELF: {
                // Push the method receiver (self) onto the stack
                PUSH(vm->method_self);
                break;
            }

            case BC_SET_SELF: {
                // Pop and set the method receiver (self)
                vm->method_self = POP();
                break;
            }

            case BC_GET_KEY: {
                // [iterable, index] -> [key]
                // For objects: return field name at index
                // For arrays: return index as i32
                Value idx = POP();
                Value obj = POP();
                int i = (int)value_to_i64(idx);

                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (i >= 0 && i < o->num_fields) {
                        // Return field name as string
                        String *s = malloc(sizeof(String));
                        s->data = strdup(o->field_names[i]);
                        s->length = strlen(s->data);
                        s->char_length = s->length;
                        s->capacity = s->length + 1;
                        s->ref_count = 1;
                        Value v = {.type = VAL_STRING, .as.as_string = s};
                        PUSH(v);
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    // For arrays, the key is just the index
                    PUSH(val_i32_vm(i));
                }
                break;
            }

            case BC_SET_OBJ_TYPE: {
                // Set the type name on the object at top of stack
                Constant c = READ_CONSTANT();
                Value obj = PEEK(0);
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    // Free old type_name if present
                    if (o->type_name) {
                        free(o->type_name);
                    }
                    o->type_name = strdup(c.as.string.data);
                }
                break;
            }

            case BC_GET_GLOBAL: {
                Constant c = READ_CONSTANT();
                Value v;
                if (!vm_get_global(vm, c.as.string.data, &v)) {
                    THROW_ERROR_FMT("Undefined variable '%s'", c.as.string.data);
                }
                PUSH(v);
                break;
            }

            case BC_SET_GLOBAL: {
                Constant c = READ_CONSTANT();
                if (!vm_set_global(vm, c.as.string.data, PEEK(0))) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_DEFINE_GLOBAL: {
                Constant c = READ_CONSTANT();
                vm_define_global(vm, c.as.string.data, POP(), false);
                break;
            }

            case BC_GET_PROPERTY: {
                Constant c = READ_CONSTANT();
                Value obj = POP();
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    const char *key = c.as.string.data;
                    // Check for built-in length property
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(o->num_fields));
                        goto property_found;
                    }
                    for (int i = 0; i < o->num_fields; i++) {
                        if (strcmp(o->field_names[i], key) == 0) {
                            PUSH(o->field_values[i]);
                            goto property_found;
                        }
                    }
                    PUSH(vm_null_value());
                    property_found:;
                } else if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    // Array properties: length
                    const char *key = c.as.string.data;
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(obj.as.as_array->length));
                    } else {
                        PUSH(vm_null_value());
                    }
                } else if (obj.type == VAL_STRING && obj.as.as_string) {
                    // String properties: length
                    const char *key = c.as.string.data;
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(obj.as.as_string->length));
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    THROW_ERROR_FMT("Cannot get property of %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_SET_PROPERTY: {
                Constant c = READ_CONSTANT();
                Value val = POP();
                Value obj = POP();
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    const char *key = c.as.string.data;
                    // Find existing key or add new
                    for (int i = 0; i < o->num_fields; i++) {
                        if (strcmp(o->field_names[i], key) == 0) {
                            o->field_values[i] = val;
                            PUSH(val);
                            goto property_set;
                        }
                    }
                    // Add new key
                    if (o->num_fields >= o->capacity) {
                        o->capacity = o->capacity * 2;
                        o->field_names = realloc(o->field_names, sizeof(char*) * o->capacity);
                        o->field_values = realloc(o->field_values, sizeof(Value) * o->capacity);
                    }
                    o->field_names[o->num_fields] = strdup(key);
                    o->field_values[o->num_fields] = val;
                    o->num_fields++;
                    PUSH(val);
                    property_set:;
                } else {
                    THROW_ERROR_FMT("Cannot set property on %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_CLOSE_UPVALUE: {
                // Close the upvalue at the top of the stack
                // This is called when a captured variable goes out of scope
                vm_close_upvalues(vm, vm->stack_top - 1);
                POP();  // Pop the closed value
                break;
            }

            case BC_GET_INDEX: {
                Value idx = POP();
                Value obj = POP();
                if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    Array *arr = obj.as.as_array;
                    int i = (int)value_to_i64(idx);
                    if (i < 0 || i >= arr->length) {
                        PUSH(vm_null_value());
                    } else {
                        PUSH(arr->elements[i]);
                    }
                } else if (obj.type == VAL_STRING && obj.as.as_string) {
                    String *s = obj.as.as_string;
                    int i = (int)value_to_i64(idx);
                    // Count codepoints to get to index i
                    const char *p = s->data;
                    const char *end = s->data + s->length;
                    int cp_idx = 0;
                    uint32_t codepoint = 0;
                    while (p < end && cp_idx < i) {
                        // Skip UTF-8 sequence
                        unsigned char c = (unsigned char)*p;
                        if ((c & 0x80) == 0) { p += 1; }
                        else if ((c & 0xE0) == 0xC0) { p += 2; }
                        else if ((c & 0xF0) == 0xE0) { p += 3; }
                        else { p += 4; }
                        cp_idx++;
                    }
                    if (p >= end) {
                        PUSH(vm_null_value());
                    } else {
                        // Decode UTF-8 codepoint at position p
                        unsigned char c = (unsigned char)*p;
                        if ((c & 0x80) == 0) {
                            codepoint = c;
                        } else if ((c & 0xE0) == 0xC0) {
                            codepoint = (c & 0x1F) << 6;
                            codepoint |= ((unsigned char)p[1] & 0x3F);
                        } else if ((c & 0xF0) == 0xE0) {
                            codepoint = (c & 0x0F) << 12;
                            codepoint |= ((unsigned char)p[1] & 0x3F) << 6;
                            codepoint |= ((unsigned char)p[2] & 0x3F);
                        } else {
                            codepoint = (c & 0x07) << 18;
                            codepoint |= ((unsigned char)p[1] & 0x3F) << 12;
                            codepoint |= ((unsigned char)p[2] & 0x3F) << 6;
                            codepoint |= ((unsigned char)p[3] & 0x3F);
                        }
                        Value v = {.type = VAL_RUNE, .as.as_rune = codepoint};
                        PUSH(v);
                    }
                } else if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (idx.type == VAL_STRING && idx.as.as_string) {
                        const char *key = idx.as.as_string->data;
                        for (int i = 0; i < o->num_fields; i++) {
                            if (strcmp(o->field_names[i], key) == 0) {
                                PUSH(o->field_values[i]);
                                goto index_found;
                            }
                        }
                        PUSH(vm_null_value());
                    } else {
                        // Integer index - get by field index
                        int i = (int)value_to_i64(idx);
                        if (i >= 0 && i < o->num_fields) {
                            PUSH(o->field_values[i]);
                        } else {
                            PUSH(vm_null_value());
                        }
                    }
                    index_found:;
                } else {
                    THROW_ERROR_FMT("Cannot index %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_SET_INDEX: {
                Value val = POP();
                Value idx = POP();
                Value obj = POP();
                if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    Array *arr = obj.as.as_array;
                    int i = (int)value_to_i64(idx);
                    if (i < 0) {
                        THROW_ERROR_FMT("Array index out of bounds: %d", i);
                    }
                    // Grow array if needed
                    while (i >= arr->capacity) {
                        arr->capacity = arr->capacity * 2;
                        arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                    }
                    // Fill with nulls if needed
                    while (arr->length <= i) {
                        arr->elements[arr->length++] = vm_null_value();
                    }
                    arr->elements[i] = val;
                    PUSH(val);
                } else if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (idx.type == VAL_STRING && idx.as.as_string) {
                        const char *key = idx.as.as_string->data;
                        for (int i = 0; i < o->num_fields; i++) {
                            if (strcmp(o->field_names[i], key) == 0) {
                                o->field_values[i] = val;
                                PUSH(val);
                                goto index_set;
                            }
                        }
                        // Add new key
                        if (o->num_fields >= o->capacity) {
                            o->capacity = o->capacity * 2;
                            o->field_names = realloc(o->field_names, sizeof(char*) * o->capacity);
                            o->field_values = realloc(o->field_values, sizeof(Value) * o->capacity);
                        }
                        o->field_names[o->num_fields] = strdup(key);
                        o->field_values[o->num_fields] = val;
                        o->num_fields++;
                        PUSH(val);
                        index_set:;
                    } else {
                        THROW_ERROR("Object key must be string");
                    }
                } else {
                    THROW_ERROR_FMT("Cannot set index on %s", val_type_name(obj.type));
                }
                break;
            }

            // Arithmetic
            case BC_ADD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_add(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_SUB: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_sub(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_MUL: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mul(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_DIV: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_div(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_MOD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mod(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_NEGATE: {
                Value a = POP();
                switch (a.type) {
                    case VAL_I32: PUSH(val_i32_vm(-a.as.as_i32)); break;
                    case VAL_I64: PUSH(val_i64_vm(-a.as.as_i64)); break;
                    case VAL_F64: PUSH(val_f64_vm(-a.as.as_f64)); break;
                    default:
                        THROW_ERROR_FMT("Cannot negate %s", val_type_name(a.type));
                }
                break;
            }

            // i32 fast paths
            case BC_ADD_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 + b.as.as_i32));
                break;
            }

            case BC_SUB_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 - b.as.as_i32));
                break;
            }

            case BC_MUL_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 * b.as.as_i32));
                break;
            }

            // Comparison
            case BC_EQ: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_eq(a, b));
                break;
            }

            case BC_NE: {
                Value b = POP();
                Value a = POP();
                Value eq = binary_eq(a, b);
                PUSH(val_bool_vm(!eq.as.as_bool));
                break;
            }

            case BC_LT: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_lt(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_LE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                Value eq = binary_eq(a, b);
                PUSH(val_bool_vm(lt.as.as_bool || eq.as.as_bool));
                break;
            }

            case BC_GT: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, b, a);  // Swap operands
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                PUSH(lt);
                break;
            }

            case BC_GE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                PUSH(val_bool_vm(!lt.as.as_bool));
                break;
            }

            case BC_EQ_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_bool_vm(a.as.as_i32 == b.as.as_i32));
                break;
            }

            case BC_LT_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_bool_vm(a.as.as_i32 < b.as.as_i32));
                break;
            }

            // Logical
            case BC_NOT: {
                Value a = POP();
                PUSH(val_bool_vm(!value_is_truthy(a)));
                break;
            }

            // Bitwise
            case BC_BIT_NOT: {
                Value a = POP();
                if (a.type == VAL_I32) {
                    PUSH(val_i32_vm(~a.as.as_i32));
                } else if (a.type == VAL_I64) {
                    PUSH(val_i64_vm(~a.as.as_i64));
                } else {
                    THROW_ERROR_FMT("Cannot bitwise NOT %s", val_type_name(a.type));
                }
                break;
            }

            case BC_BIT_AND: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 & b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) & value_to_i64(b)));
                }
                break;
            }

            case BC_BIT_OR: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 | b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) | value_to_i64(b)));
                }
                break;
            }

            case BC_BIT_XOR: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 ^ b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) ^ value_to_i64(b)));
                }
                break;
            }

            case BC_LSHIFT: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 << b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) << value_to_i64(b)));
                }
                break;
            }

            case BC_RSHIFT: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 >> b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) >> value_to_i64(b)));
                }
                break;
            }

            // Control flow
            case BC_JUMP: {
                uint16_t offset = READ_SHORT();
                ip += offset;
                break;
            }

            case BC_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                Value cond = PEEK(0);  // Don't pop - leave for explicit POP
                if (!value_is_truthy(cond)) {
                    ip += offset;
                }
                break;
            }

            case BC_JUMP_IF_TRUE: {
                uint16_t offset = READ_SHORT();
                Value cond = PEEK(0);  // Don't pop - leave for explicit POP
                if (value_is_truthy(cond)) {
                    ip += offset;
                }
                break;
            }

            case BC_COALESCE: {
                // Null coalescing: if value is NOT null, jump (keep value)
                // Otherwise, pop null and continue to evaluate fallback
                uint16_t offset = READ_SHORT();
                Value val = PEEK(0);
                if (val.type != VAL_NULL) {
                    ip += offset;  // Value is not null, skip fallback
                }
                // If null, leave on stack for explicit POP before fallback
                break;
            }

            case BC_OPTIONAL_CHAIN: {
                // Optional chaining: if value IS null, jump (keep null)
                // Otherwise, continue to property access/method call
                uint16_t offset = READ_SHORT();
                Value val = PEEK(0);
                if (val.type == VAL_NULL) {
                    ip += offset;  // Value is null, skip property access
                }
                // If not null, leave on stack for property/index/method
                break;
            }

            case BC_LOOP: {
                uint16_t offset = READ_SHORT();
                ip -= offset;
                break;
            }

            case BC_FOR_IN_INIT: {
                // No longer used - kept for compatibility
                break;
            }

            case BC_FOR_IN_NEXT: {
                // Stack: [array, index] -> [element] or jump (consume array, index)
                uint16_t offset = READ_SHORT();
                Value idx_val = POP();
                Value arr_val = POP();

                if (arr_val.type != VAL_ARRAY) {
                    THROW_ERROR("for-in requires an array or object");
                }

                int32_t idx = idx_val.as.as_i32;
                Array *arr = arr_val.as.as_array;

                if (idx >= arr->length) {
                    ip += offset;
                } else {
                    PUSH(arr->elements[idx]);
                }
                break;
            }

            case BC_POP:
                POP();
                break;

            case BC_POPN: {
                uint8_t n = READ_BYTE();
                vm_popn(vm, n);
                break;
            }

            case BC_DUP: {
                // Duplicate top of stack
                Value top = PEEK(0);
                PUSH(top);
                break;
            }

            case BC_DUP2: {
                // Duplicate top two stack values
                // [a, b] -> [a, b, a, b]
                Value b = PEEK(0);
                Value a = PEEK(1);
                PUSH(a);
                PUSH(b);
                break;
            }

            case BC_SWAP: {
                // Swap top two stack values
                // [a, b] -> [b, a]
                Value top = PEEK(0);
                Value second = PEEK(1);
                vm->stack_top[-1] = second;
                vm->stack_top[-2] = top;
                break;
            }

            case BC_BURY3: {
                // Move second-from-top under bottom of 4
                // [a, b, c, d] -> [c, a, b, d]
                // (where d is top of stack)
                Value d = PEEK(0);  // top
                Value c = PEEK(1);  // 2nd from top - this moves to bottom
                Value b = PEEK(2);
                Value a = PEEK(3);  // bottom of 4
                vm->stack_top[-4] = c;  // c moves to bottom
                vm->stack_top[-3] = a;  // a moves up
                vm->stack_top[-2] = b;  // b moves up
                vm->stack_top[-1] = d;  // d stays on top
                break;
            }

            case BC_ROT3: {
                // Rotate 3 elements, bring bottom to top
                // [a, b, c] -> [b, c, a]
                // (where c is top of stack)
                Value c = PEEK(0);  // top
                Value b = PEEK(1);
                Value a = PEEK(2);  // bottom of 3
                vm->stack_top[-3] = b;  // b moves to bottom
                vm->stack_top[-2] = c;  // c moves to middle
                vm->stack_top[-1] = a;  // a moves to top
                break;
            }

            // Print (builtin)
            case BC_PRINT: {
                uint8_t argc = READ_BYTE();
                // Print args in order (they're on stack in reverse)
                Value *args = vm->stack_top - argc;
                for (int i = 0; i < argc; i++) {
                    if (i > 0) printf(" ");
                    Value v = args[i];
                    switch (v.type) {
                        case VAL_NULL: printf("null"); break;
                        case VAL_BOOL: printf("%s", v.as.as_bool ? "true" : "false"); break;
                        case VAL_I8: printf("%d", v.as.as_i8); break;
                        case VAL_I16: printf("%d", v.as.as_i16); break;
                        case VAL_I32: printf("%d", v.as.as_i32); break;
                        case VAL_I64: printf("%lld", (long long)v.as.as_i64); break;
                        case VAL_U8: printf("%u", v.as.as_u8); break;
                        case VAL_U16: printf("%u", v.as.as_u16); break;
                        case VAL_U32: printf("%u", v.as.as_u32); break;
                        case VAL_U64: printf("%llu", (unsigned long long)v.as.as_u64); break;
                        case VAL_F32: printf("%g", v.as.as_f32); break;
                        case VAL_F64: printf("%g", v.as.as_f64); break;
                        case VAL_STRING:
                            if (v.as.as_string) printf("%s", v.as.as_string->data);
                            break;
                        case VAL_RUNE: {
                            // Print rune as character if printable, otherwise as U+XXXX
                            uint32_t r = v.as.as_rune;
                            if (r >= 32 && r < 127) {
                                printf("'%c'", (char)r);
                            } else if (r < 0x10000) {
                                printf("U+%04X", r);
                            } else {
                                printf("U+%X", r);
                            }
                            break;
                        }
                        default:
                            printf("<%s>", val_type_name(v.type));
                    }
                }
                printf("\n");
                vm_popn(vm, argc);
                PUSH(vm_null_value());  // Push null as return value
                break;
            }

            case BC_CALL_BUILTIN: {
                uint16_t builtin_id = READ_SHORT();
                uint8_t argc = READ_BYTE();
                Value *args = vm->stack_top - argc;
                Value result = vm_null_value();

                switch (builtin_id) {
                    case BUILTIN_TYPEOF: {
                        if (argc >= 1) {
                            Value v = args[0];
                            const char *type_str;
                            // Check for custom object type name
                            if (v.type == VAL_OBJECT && v.as.as_object && v.as.as_object->type_name) {
                                type_str = v.as.as_object->type_name;
                            } else {
                                type_str = val_type_name(v.type);
                            }
                            String *s = malloc(sizeof(String));
                            s->data = strdup(type_str);
                            s->length = strlen(type_str);
                            s->char_length = s->length;
                            s->capacity = s->length + 1;
                            s->ref_count = 1;
                            result.type = VAL_STRING;
                            result.as.as_string = s;
                        }
                        break;
                    }
                    case BUILTIN_PRINT: {
                        for (int i = 0; i < argc; i++) {
                            if (i > 0) printf(" ");
                            Value v = args[i];
                            switch (v.type) {
                                case VAL_NULL: printf("null"); break;
                                case VAL_BOOL: printf("%s", v.as.as_bool ? "true" : "false"); break;
                                case VAL_I32: printf("%d", v.as.as_i32); break;
                                case VAL_I64: printf("%lld", (long long)v.as.as_i64); break;
                                case VAL_F64: printf("%g", v.as.as_f64); break;
                                case VAL_STRING:
                                    if (v.as.as_string) printf("%s", v.as.as_string->data);
                                    break;
                                default:
                                    printf("<%s>", val_type_name(v.type));
                            }
                        }
                        printf("\n");
                        break;
                    }
                    case BUILTIN_EPRINT: {
                        for (int i = 0; i < argc; i++) {
                            if (i > 0) fprintf(stderr, " ");
                            Value v = args[i];
                            switch (v.type) {
                                case VAL_NULL: fprintf(stderr, "null"); break;
                                case VAL_BOOL: fprintf(stderr, "%s", v.as.as_bool ? "true" : "false"); break;
                                case VAL_I32: fprintf(stderr, "%d", v.as.as_i32); break;
                                case VAL_I64: fprintf(stderr, "%lld", (long long)v.as.as_i64); break;
                                case VAL_F64: fprintf(stderr, "%g", v.as.as_f64); break;
                                case VAL_STRING:
                                    if (v.as.as_string) fprintf(stderr, "%s", v.as.as_string->data);
                                    break;
                                default:
                                    fprintf(stderr, "<%s>", val_type_name(v.type));
                            }
                        }
                        fprintf(stderr, "\n");
                        break;
                    }
                    case BUILTIN_ASSERT: {
                        if (argc >= 1 && !value_is_truthy(args[0])) {
                            const char *msg = (argc >= 2 && args[1].type == VAL_STRING)
                                ? args[1].as.as_string->data : "Assertion failed";
                            THROW_ERROR(msg);
                        }
                        break;
                    }
                    case BUILTIN_PANIC: {
                        fflush(stdout);  // Ensure all output is printed before panic
                        const char *msg = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : "panic!";
                        fprintf(stderr, "panic: %s\n", msg);
                        exit(1);
                    }
                    case BUILTIN_DIVI: {
                        // Integer division (floor)
                        if (argc >= 2) {
                            int64_t a = value_to_i64(args[0]);
                            int64_t b = value_to_i64(args[1]);
                            if (b == 0) {
                                THROW_ERROR("Division by zero");
                            }
                            // Floor division: towards negative infinity
                            int64_t q = a / b;
                            if ((a ^ b) < 0 && a % b != 0) {
                                q--;  // Adjust for floor behavior
                            }
                            result = val_i64_vm(q);
                        }
                        break;
                    }
                    case BUILTIN_MODI: {
                        // Integer modulo
                        if (argc >= 2) {
                            int64_t a = value_to_i64(args[0]);
                            int64_t b = value_to_i64(args[1]);
                            if (b == 0) {
                                THROW_ERROR("Modulo by zero");
                            }
                            result = val_i64_vm(a % b);
                        }
                        break;
                    }
                    case BUILTIN_STRING_CONCAT_MANY: {
                        // string_concat_many(array) - concatenate array of strings
                        if (argc >= 1 && args[0].type == VAL_ARRAY && args[0].as.as_array) {
                            Array *arr = args[0].as.as_array;
                            // Calculate total length
                            int total_len = 0;
                            for (int i = 0; i < arr->length; i++) {
                                if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                    total_len += arr->elements[i].as.as_string->length;
                                }
                            }
                            // Allocate and build result
                            char *buf = malloc(total_len + 1);
                            char *p = buf;
                            for (int i = 0; i < arr->length; i++) {
                                if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                    String *s = arr->elements[i].as.as_string;
                                    memcpy(p, s->data, s->length);
                                    p += s->length;
                                }
                            }
                            *p = '\0';
                            result = vm_make_string(buf, total_len);
                            free(buf);
                        } else {
                            result = vm_make_string("", 0);
                        }
                        break;
                    }
                    case BUILTIN_OPEN: {
                        // open(path, mode?) - open file, returns file handle
                        if (argc < 1 || args[0].type != VAL_STRING) {
                            THROW_ERROR("open() expects (path: string, mode?: string)");
                        }
                        const char *path = args[0].as.as_string->data;
                        const char *mode = "r";  // Default mode
                        if (argc >= 2 && args[1].type == VAL_STRING) {
                            mode = args[1].as.as_string->data;
                        }
                        FILE *fp = fopen(path, mode);
                        if (!fp) {
                            THROW_ERROR_FMT("Failed to open '%s': %s", path, strerror(errno));
                        }
                        FileHandle *file = malloc(sizeof(FileHandle));
                        file->fp = fp;
                        file->path = strdup(path);
                        file->mode = strdup(mode);
                        file->closed = 0;
                        result.type = VAL_FILE;
                        result.as.as_file = file;
                        break;
                    }
                    case BUILTIN_READ_LINE: {
                        // read_line() - read line from stdin
                        char *line = NULL;
                        size_t len = 0;
                        ssize_t read_len = getline(&line, &len, stdin);
                        if (read_len == -1) {
                            free(line);
                            result = vm_null_value();  // EOF
                        } else {
                            // Strip newline
                            if (read_len > 0 && line[read_len - 1] == '\n') {
                                line[read_len - 1] = '\0';
                                read_len--;
                            }
                            if (read_len > 0 && line[read_len - 1] == '\r') {
                                line[read_len - 1] = '\0';
                                read_len--;
                            }
                            result = vm_make_string(line, read_len);
                            free(line);
                        }
                        break;
                    }
                    default:
                        vm_runtime_error(vm, "Builtin %d not implemented", builtin_id);
                        return VM_RUNTIME_ERROR;
                }

                vm_popn(vm, argc);
                PUSH(result);
                break;
            }

            case BC_CALL: {
                uint8_t argc = READ_BYTE();
                Value callee = PEEK(argc);

                if (!is_vm_closure(callee)) {
                    THROW_ERROR("Can only call functions");
                }

                VMClosure *closure = as_vm_closure(callee);
                Chunk *fn_chunk = closure->chunk;

                // Handle rest parameters
                if (fn_chunk->has_rest_param) {
                    // Rest param functions: collect extra args into array
                    int regular_params = fn_chunk->arity;
                    int rest_count = (argc > regular_params) ? argc - regular_params : 0;

                    // Create array for rest arguments
                    Array *rest_arr = malloc(sizeof(Array));
                    rest_arr->elements = malloc(sizeof(Value) * (rest_count > 0 ? rest_count : 1));
                    rest_arr->length = rest_count;
                    rest_arr->capacity = rest_count > 0 ? rest_count : 1;
                    rest_arr->element_type = NULL;
                    rest_arr->ref_count = 1;

                    // Copy rest arguments to array
                    Value *args_start = vm->stack_top - argc;
                    for (int i = 0; i < rest_count; i++) {
                        rest_arr->elements[i] = args_start[regular_params + i];
                    }

                    // Pop extra arguments from stack (keep only regular params)
                    if (rest_count > 0) {
                        vm->stack_top -= rest_count;
                    }

                    // Push null for missing regular params
                    if (argc < regular_params) {
                        int required = regular_params - fn_chunk->optional_count;
                        if (argc < required) {
                            free(rest_arr->elements);
                            free(rest_arr);
                            THROW_ERROR_FMT("Expected at least %d arguments but got %d", required, argc);
                        }
                        int missing = regular_params - argc;
                        for (int i = 0; i < missing; i++) {
                            PUSH(vm_null_value());
                        }
                    }

                    // Push the rest array
                    Value rest_val;
                    rest_val.type = VAL_ARRAY;
                    rest_val.as.as_array = rest_arr;
                    PUSH(rest_val);

                    argc = regular_params + 1;  // regular params + rest array
                } else {
                    // Check arity for non-rest-param functions
                    if (argc < fn_chunk->arity) {
                        // Allow optional params
                        int required = fn_chunk->arity - fn_chunk->optional_count;
                        if (argc < required) {
                            THROW_ERROR_FMT("Expected at least %d arguments but got %d", required, argc);
                        }
                        // Push null for missing optional parameters
                        int missing = fn_chunk->arity - argc;
                        for (int i = 0; i < missing; i++) {
                            PUSH(vm_null_value());
                        }
                        argc = fn_chunk->arity;
                    }
                }

                // Save current frame state
                frame->ip = ip;

                // Check for stack overflow
                if (vm->frame_count >= vm->frame_capacity) {
                    vm->frame_capacity *= 2;
                    vm->frames = realloc(vm->frames, sizeof(CallFrame) * vm->frame_capacity);
                }

                // Set up new call frame
                // The stack layout is: [callee] [arg0] [arg1] ... [argN]
                // After call, slots points to where callee was
                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->chunk = fn_chunk;
                new_frame->ip = fn_chunk->code;
                new_frame->slots = vm->stack_top - argc - 1;  // Include the callee slot
                new_frame->upvalues = NULL;  // TODO: support upvalues
                new_frame->slot_count = fn_chunk->local_count;

                // Update frame pointers
                frame = new_frame;
                ip = frame->ip;
                slots = frame->slots;
                break;
            }

            case BC_CLOSURE: {
                // Read function index from constant pool
                Constant c = READ_CONSTANT();
                uint8_t upvalue_count = READ_BYTE();

                if (c.type != CONST_FUNCTION) {
                    vm_runtime_error(vm, "Expected function in constant pool");
                    return VM_RUNTIME_ERROR;
                }

                Chunk *fn_chunk = c.as.function;
                VMClosure *closure = vm_closure_new(fn_chunk);

                // Capture upvalues
                for (int i = 0; i < upvalue_count; i++) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        closure->upvalues[i] = vm_capture_upvalue(vm, slots + index);
                    } else {
                        // Get from enclosing closure's upvalues
                        // The enclosing closure is in slot 0
                        Value enclosing_val = slots[0];
                        if (is_vm_closure(enclosing_val)) {
                            VMClosure *enclosing = as_vm_closure(enclosing_val);
                            if (enclosing && index < enclosing->upvalue_count) {
                                closure->upvalues[i] = enclosing->upvalues[index];
                            } else {
                                closure->upvalues[i] = NULL;
                            }
                        } else {
                            closure->upvalues[i] = NULL;
                        }
                    }
                }

                PUSH(val_vm_closure(closure));
                break;
            }

            case BC_DEFER: {
                // Pop the closure from the stack and add to defer stack
                Value closure_val = POP();
                if (!is_vm_closure(closure_val)) {
                    THROW_ERROR("defer requires a closure");
                }

                // Ensure defer stack has capacity
                if (vm->defer_count >= vm->defer_capacity) {
                    vm->defer_capacity *= 2;
                    vm->defers = realloc(vm->defers, sizeof(DeferEntry) * vm->defer_capacity);
                }

                // Store the deferred closure
                DeferEntry *entry = &vm->defers[vm->defer_count++];
                VMClosure *closure = as_vm_closure(closure_val);
                entry->chunk = closure->chunk;
                entry->args = NULL;
                entry->arg_count = 0;
                entry->frame_index = vm->frame_count;  // Current frame index
                break;
            }

            case BC_RETURN: {
                Value result = POP();

                // Execute deferred closures in LIFO order for this frame
                int returning_frame_index = vm->frame_count;
                frame->ip = ip;  // Save IP

                while (vm->defer_count > 0 &&
                       vm->defers[vm->defer_count - 1].frame_index == returning_frame_index) {
                    DeferEntry *entry = &vm->defers[--vm->defer_count];

                    // Call the deferred closure using vm_call_closure
                    VMClosure *defer_closure = vm_closure_new(entry->chunk);
                    Value defer_result = vm_call_closure(vm, defer_closure, NULL, 0);
                    (void)defer_result;  // Ignore return value
                }

                // Close upvalues
                vm_close_upvalues(vm, slots);

                vm->frame_count--;
                if (vm->frame_count == 0) {
                    // Script returning - just exit
                    POP();  // Pop script slot
                    return VM_OK;
                }

                if (vm->frame_count <= base_frame_count) {
                    // Callback returning to caller (vm_call_closure)
                    vm->stack_top = slots;
                    PUSH(result);
                    return VM_OK;
                }

                // Normal function return - restore previous frame
                vm->stack_top = slots;
                PUSH(result);

                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                slots = frame->slots;
                break;
            }

            case BC_CALL_METHOD: {
                Constant method_c = READ_CONSTANT();
                uint8_t argc = READ_BYTE();
                const char *method = method_c.as.string.data;
                Value *args = vm->stack_top - argc;
                Value receiver = args[-1];  // Object is before args
                Value result = vm_null_value();

                if (receiver.type == VAL_ARRAY && receiver.as.as_array) {
                    Array *arr = receiver.as.as_array;
                    if (strcmp(method, "push") == 0 && argc >= 1) {
                        // Ensure capacity
                        if (arr->length >= arr->capacity) {
                            arr->capacity = arr->capacity * 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        arr->elements[arr->length++] = args[0];
                        result = val_i32_vm(arr->length);
                    } else if (strcmp(method, "pop") == 0) {
                        if (arr->length > 0) {
                            result = arr->elements[--arr->length];
                        }
                    } else if (strcmp(method, "shift") == 0) {
                        if (arr->length > 0) {
                            result = arr->elements[0];
                            memmove(arr->elements, arr->elements + 1, sizeof(Value) * (arr->length - 1));
                            arr->length--;
                        }
                    } else if (strcmp(method, "unshift") == 0 && argc >= 1) {
                        if (arr->length >= arr->capacity) {
                            arr->capacity = arr->capacity * 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        memmove(arr->elements + 1, arr->elements, sizeof(Value) * arr->length);
                        arr->elements[0] = args[0];
                        arr->length++;
                        result = val_i32_vm(arr->length);
                    } else if (strcmp(method, "join") == 0) {
                        const char *sep = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : ",";
                        // Calculate total length
                        int total = 0;
                        int sep_len = strlen(sep);
                        for (int i = 0; i < arr->length; i++) {
                            if (i > 0) total += sep_len;
                            if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                total += arr->elements[i].as.as_string->length;
                            } else if (arr->elements[i].type == VAL_I32) {
                                total += 12;  // max int length
                            } else {
                                total += 10;  // placeholder
                            }
                        }
                        char *buf = malloc(total + 1);
                        buf[0] = '\0';
                        for (int i = 0; i < arr->length; i++) {
                            if (i > 0) strcat(buf, sep);
                            if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                strcat(buf, arr->elements[i].as.as_string->data);
                            } else if (arr->elements[i].type == VAL_I32) {
                                char num[20];
                                sprintf(num, "%d", arr->elements[i].as.as_i32);
                                strcat(buf, num);
                            }
                        }
                        String *s = malloc(sizeof(String));
                        s->data = buf;
                        s->length = strlen(buf);
                        s->char_length = s->length;
                        s->capacity = total + 1;
                        s->ref_count = 1;
                        result.type = VAL_STRING;
                        result.as.as_string = s;
                    } else if (strcmp(method, "map") == 0 && argc >= 1) {
                        // map(callback) - transform each element
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("map() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Create new result array
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (arr->length > 0 ? arr->length : 1));
                        new_arr->length = 0;
                        new_arr->capacity = arr->length > 0 ? arr->length : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;

                        // Save frame state before calling closures
                        frame->ip = ip;

                        for (int i = 0; i < arr->length; i++) {
                            Value elem = arr->elements[i];
                            Value mapped = vm_call_closure(vm, callback, &elem, 1);
                            new_arr->elements[new_arr->length++] = mapped;
                        }

                        // Restore frame state after closure calls
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "filter") == 0 && argc >= 1) {
                        // filter(callback) - keep elements where callback returns true
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("filter() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Create new result array
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (arr->length > 0 ? arr->length : 1));
                        new_arr->length = 0;
                        new_arr->capacity = arr->length > 0 ? arr->length : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;

                        // Save frame state
                        frame->ip = ip;

                        for (int i = 0; i < arr->length; i++) {
                            Value elem = arr->elements[i];
                            Value keep = vm_call_closure(vm, callback, &elem, 1);
                            if (value_is_truthy(keep)) {
                                if (new_arr->length >= new_arr->capacity) {
                                    new_arr->capacity *= 2;
                                    new_arr->elements = realloc(new_arr->elements, sizeof(Value) * new_arr->capacity);
                                }
                                new_arr->elements[new_arr->length++] = elem;
                            }
                        }

                        // Restore frame state
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "reduce") == 0 && argc >= 1) {
                        // reduce(callback, initial?) - accumulate values
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("reduce() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Determine starting accumulator and index
                        Value accumulator;
                        int start_idx;
                        if (argc >= 2) {
                            accumulator = args[1];
                            start_idx = 0;
                        } else {
                            if (arr->length == 0) {
                                THROW_ERROR("reduce() on empty array with no initial value");
                            }
                            accumulator = arr->elements[0];
                            start_idx = 1;
                        }

                        // Save frame state
                        frame->ip = ip;

                        for (int i = start_idx; i < arr->length; i++) {
                            Value callback_args[2] = {accumulator, arr->elements[i]};
                            accumulator = vm_call_closure(vm, callback, callback_args, 2);
                        }

                        // Restore frame state
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result = accumulator;
                    } else if (strcmp(method, "slice") == 0 && argc >= 2) {
                        // slice(start, end) - return new subarray
                        int start = value_to_i32(args[0]);
                        int end = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > arr->length) start = arr->length;
                        if (end < start) end = start;
                        if (end > arr->length) end = arr->length;

                        Array *new_arr = malloc(sizeof(Array));
                        int new_len = end - start;
                        new_arr->elements = malloc(sizeof(Value) * (new_len > 0 ? new_len : 1));
                        new_arr->length = new_len;
                        new_arr->capacity = new_len > 0 ? new_len : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;
                        for (int i = 0; i < new_len; i++) {
                            new_arr->elements[i] = arr->elements[start + i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "concat") == 0 && argc >= 1) {
                        // concat(other) - return new concatenated array
                        if (args[0].type != VAL_ARRAY || !args[0].as.as_array) {
                            THROW_ERROR("concat() argument must be an array");
                        }
                        Array *other = args[0].as.as_array;
                        int new_len = arr->length + other->length;
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (new_len > 0 ? new_len : 1));
                        new_arr->length = new_len;
                        new_arr->capacity = new_len > 0 ? new_len : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;
                        for (int i = 0; i < arr->length; i++) {
                            new_arr->elements[i] = arr->elements[i];
                        }
                        for (int i = 0; i < other->length; i++) {
                            new_arr->elements[arr->length + i] = other->elements[i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "find") == 0 && argc >= 1) {
                        // find(value) - return index or -1
                        result = val_i32_vm(-1);
                        for (int i = 0; i < arr->length; i++) {
                            if (vm_values_equal(arr->elements[i], args[0])) {
                                result = val_i32_vm(i);
                                break;
                            }
                        }
                    } else if (strcmp(method, "contains") == 0 && argc >= 1) {
                        // contains(value) - return true/false
                        result = val_bool_vm(false);
                        for (int i = 0; i < arr->length; i++) {
                            if (vm_values_equal(arr->elements[i], args[0])) {
                                result = val_bool_vm(true);
                                break;
                            }
                        }
                    } else if (strcmp(method, "first") == 0) {
                        // first() - return first element or null
                        if (arr->length > 0) {
                            result = arr->elements[0];
                        }
                    } else if (strcmp(method, "last") == 0) {
                        // last() - return last element or null
                        if (arr->length > 0) {
                            result = arr->elements[arr->length - 1];
                        }
                    } else if (strcmp(method, "clear") == 0) {
                        // clear() - remove all elements
                        arr->length = 0;
                    } else if (strcmp(method, "reverse") == 0) {
                        // reverse() - reverse in place
                        int left = 0, right = arr->length - 1;
                        while (left < right) {
                            Value temp = arr->elements[left];
                            arr->elements[left] = arr->elements[right];
                            arr->elements[right] = temp;
                            left++;
                            right--;
                        }
                    } else if (strcmp(method, "insert") == 0 && argc >= 2) {
                        // insert(index, value)
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index > arr->length) {
                            THROW_ERROR_FMT("insert index %d out of bounds (length %d)", index, arr->length);
                        }
                        if (arr->length >= arr->capacity) {
                            arr->capacity *= 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        memmove(arr->elements + index + 1, arr->elements + index, sizeof(Value) * (arr->length - index));
                        arr->elements[index] = args[1];
                        arr->length++;
                    } else if (strcmp(method, "remove") == 0 && argc >= 1) {
                        // remove(index) - remove and return element at index
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index >= arr->length) {
                            THROW_ERROR_FMT("remove index %d out of bounds (length %d)", index, arr->length);
                        }
                        result = arr->elements[index];
                        memmove(arr->elements + index, arr->elements + index + 1, sizeof(Value) * (arr->length - index - 1));
                        arr->length--;
                    } else {
                        THROW_ERROR_FMT("Unknown array method: %s", method);
                    }
                } else if (receiver.type == VAL_STRING && receiver.as.as_string) {
                    String *str = receiver.as.as_string;
                    if (strcmp(method, "split") == 0) {
                        const char *sep = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : "";
                        // Simple split implementation
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * 8);
                        arr->length = 0;
                        arr->capacity = 8;
                        arr->element_type = NULL;
                        arr->ref_count = 1;

                        if (strlen(sep) == 0) {
                            // Split into chars
                            for (int i = 0; i < str->length; i++) {
                                if (arr->length >= arr->capacity) {
                                    arr->capacity *= 2;
                                    arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                                }
                                String *ch = malloc(sizeof(String));
                                ch->data = malloc(2);
                                ch->data[0] = str->data[i];
                                ch->data[1] = '\0';
                                ch->length = 1;
                                ch->char_length = 1;
                                ch->capacity = 2;
                                ch->ref_count = 1;
                                arr->elements[arr->length].type = VAL_STRING;
                                arr->elements[arr->length].as.as_string = ch;
                                arr->length++;
                            }
                        } else {
                            // Split by separator
                            char *copy = strdup(str->data);
                            char *token = strtok(copy, sep);
                            while (token) {
                                if (arr->length >= arr->capacity) {
                                    arr->capacity *= 2;
                                    arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                                }
                                String *s = malloc(sizeof(String));
                                s->data = strdup(token);
                                s->length = strlen(token);
                                s->char_length = s->length;
                                s->capacity = s->length + 1;
                                s->ref_count = 1;
                                arr->elements[arr->length].type = VAL_STRING;
                                arr->elements[arr->length].as.as_string = s;
                                arr->length++;
                                token = strtok(NULL, sep);
                            }
                            free(copy);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "contains") == 0 && argc >= 1) {
                        // Check if string contains substring
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *needle = args[0].as.as_string->data;
                            result = val_bool_vm(strstr(str->data, needle) != NULL);
                        }
                    } else if (strcmp(method, "length") == 0) {
                        // String length (just return char_length)
                        result.type = VAL_I32;
                        result.as.as_i32 = str->char_length;
                    } else if (strcmp(method, "substr") == 0 && argc >= 2) {
                        // substr(start, length)
                        int start = value_to_i32(args[0]);
                        int len = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > str->length) start = str->length;
                        if (len < 0) len = 0;
                        if (start + len > str->length) len = str->length - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "slice") == 0 && argc >= 2) {
                        // slice(start, end)
                        int start = value_to_i32(args[0]);
                        int end = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > str->length) start = str->length;
                        if (end < start) end = start;
                        if (end > str->length) end = str->length;
                        int len = end - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "find") == 0 && argc >= 1) {
                        // find(substring)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_i32_vm(-1);
                        } else {
                            const char *needle = args[0].as.as_string->data;
                            char *found = strstr(str->data, needle);
                            result = val_i32_vm(found ? (int)(found - str->data) : -1);
                        }
                    } else if (strcmp(method, "trim") == 0) {
                        // trim() - remove leading/trailing whitespace
                        int start = 0, end = str->length;
                        while (start < end && (str->data[start] == ' ' || str->data[start] == '\t' ||
                               str->data[start] == '\n' || str->data[start] == '\r')) start++;
                        while (end > start && (str->data[end-1] == ' ' || str->data[end-1] == '\t' ||
                               str->data[end-1] == '\n' || str->data[end-1] == '\r')) end--;
                        int len = end - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "to_upper") == 0) {
                        // to_upper()
                        char *buf = malloc(str->length + 1);
                        for (int i = 0; i < str->length; i++) {
                            buf[i] = (str->data[i] >= 'a' && str->data[i] <= 'z')
                                   ? str->data[i] - 32 : str->data[i];
                        }
                        buf[str->length] = '\0';
                        result = vm_make_string(buf, str->length);
                        free(buf);
                    } else if (strcmp(method, "to_lower") == 0) {
                        // to_lower()
                        char *buf = malloc(str->length + 1);
                        for (int i = 0; i < str->length; i++) {
                            buf[i] = (str->data[i] >= 'A' && str->data[i] <= 'Z')
                                   ? str->data[i] + 32 : str->data[i];
                        }
                        buf[str->length] = '\0';
                        result = vm_make_string(buf, str->length);
                        free(buf);
                    } else if (strcmp(method, "starts_with") == 0 && argc >= 1) {
                        // starts_with(prefix)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *prefix = args[0].as.as_string->data;
                            int prefix_len = args[0].as.as_string->length;
                            result = val_bool_vm(str->length >= prefix_len &&
                                                 strncmp(str->data, prefix, prefix_len) == 0);
                        }
                    } else if (strcmp(method, "ends_with") == 0 && argc >= 1) {
                        // ends_with(suffix)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *suffix = args[0].as.as_string->data;
                            int suffix_len = args[0].as.as_string->length;
                            result = val_bool_vm(str->length >= suffix_len &&
                                strcmp(str->data + str->length - suffix_len, suffix) == 0);
                        }
                    } else if (strcmp(method, "replace") == 0 && argc >= 2) {
                        // replace(old, new) - replace first occurrence
                        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
                            result.type = VAL_STRING;
                            result.as.as_string = str;
                        } else {
                            const char *old_str = args[0].as.as_string->data;
                            const char *new_str = args[1].as.as_string->data;
                            char *found = strstr(str->data, old_str);
                            if (!found) {
                                result.type = VAL_STRING;
                                result.as.as_string = str;
                            } else {
                                int old_len = args[0].as.as_string->length;
                                int new_len = args[1].as.as_string->length;
                                int result_len = str->length - old_len + new_len;
                                char *buf = malloc(result_len + 1);
                                int prefix_len = found - str->data;
                                memcpy(buf, str->data, prefix_len);
                                memcpy(buf + prefix_len, new_str, new_len);
                                memcpy(buf + prefix_len + new_len, found + old_len,
                                       str->length - prefix_len - old_len);
                                buf[result_len] = '\0';
                                result = vm_make_string(buf, result_len);
                                free(buf);
                            }
                        }
                    } else if (strcmp(method, "replace_all") == 0 && argc >= 2) {
                        // replace_all(old, new)
                        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
                            result.type = VAL_STRING;
                            result.as.as_string = str;
                        } else {
                            const char *old_str = args[0].as.as_string->data;
                            const char *new_str = args[1].as.as_string->data;
                            int old_len = args[0].as.as_string->length;
                            int new_len = args[1].as.as_string->length;
                            if (old_len == 0) {
                                result.type = VAL_STRING;
                                result.as.as_string = str;
                            } else {
                                // Count occurrences
                                int count = 0;
                                const char *p = str->data;
                                while ((p = strstr(p, old_str)) != NULL) {
                                    count++;
                                    p += old_len;
                                }
                                int result_len = str->length + count * (new_len - old_len);
                                char *buf = malloc(result_len + 1);
                                char *dest = buf;
                                p = str->data;
                                const char *prev = p;
                                while ((p = strstr(prev, old_str)) != NULL) {
                                    memcpy(dest, prev, p - prev);
                                    dest += p - prev;
                                    memcpy(dest, new_str, new_len);
                                    dest += new_len;
                                    prev = p + old_len;
                                }
                                strcpy(dest, prev);
                                result = vm_make_string(buf, result_len);
                                free(buf);
                            }
                        }
                    } else if (strcmp(method, "repeat") == 0 && argc >= 1) {
                        // repeat(count)
                        int count = value_to_i32(args[0]);
                        if (count <= 0) {
                            result = vm_make_string("", 0);
                        } else {
                            int result_len = str->length * count;
                            char *buf = malloc(result_len + 1);
                            for (int i = 0; i < count; i++) {
                                memcpy(buf + i * str->length, str->data, str->length);
                            }
                            buf[result_len] = '\0';
                            result = vm_make_string(buf, result_len);
                            free(buf);
                        }
                    } else if (strcmp(method, "char_at") == 0 && argc >= 1) {
                        // char_at(index)
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index >= str->length) {
                            result = vm_make_string("", 0);
                        } else {
                            char buf[2] = {str->data[index], '\0'};
                            result = vm_make_string(buf, 1);
                        }
                    } else if (strcmp(method, "byte_at") == 0 && argc >= 1) {
                        // byte_at(index)
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index >= str->length) {
                            result = val_i32_vm(0);
                        } else {
                            result = val_i32_vm((unsigned char)str->data[index]);
                        }
                    } else if (strcmp(method, "chars") == 0) {
                        // chars() - return array of single-char strings
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (str->length > 0 ? str->length : 1));
                        arr->length = str->length;
                        arr->capacity = str->length > 0 ? str->length : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        for (int i = 0; i < str->length; i++) {
                            char buf[2] = {str->data[i], '\0'};
                            arr->elements[i] = vm_make_string(buf, 1);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "bytes") == 0) {
                        // bytes() - return array of byte values
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (str->length > 0 ? str->length : 1));
                        arr->length = str->length;
                        arr->capacity = str->length > 0 ? str->length : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        for (int i = 0; i < str->length; i++) {
                            arr->elements[i] = val_i32_vm((unsigned char)str->data[i]);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "to_bytes") == 0) {
                        // to_bytes() - return array of byte values (u8)
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (str->length > 0 ? str->length : 1));
                        arr->length = str->length;
                        arr->capacity = str->length > 0 ? str->length : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        for (int i = 0; i < str->length; i++) {
                            arr->elements[i].type = VAL_U8;
                            arr->elements[i].as.as_u8 = (uint8_t)str->data[i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "deserialize") == 0) {
                        // deserialize() - parse JSON string into object/array/value
                        result = vm_json_parse(str->data, str->length);
                    } else {
                        THROW_ERROR_FMT("Unknown string method: %s", method);
                    }
                } else if (receiver.type == VAL_FILE && receiver.as.as_file) {
                    // File method calls
                    FileHandle *file = receiver.as.as_file;

                    if (strcmp(method, "read") == 0) {
                        // read() or read(size) - read text from file
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot read from closed file '%s'", file->path);
                        }
                        if (argc == 0) {
                            // Read entire file from current position
                            long current_pos = ftell(file->fp);
                            int is_seekable = (current_pos != -1 && fseek(file->fp, 0, SEEK_END) == 0);
                            if (is_seekable) {
                                long end_pos = ftell(file->fp);
                                fseek(file->fp, current_pos, SEEK_SET);
                                long size = end_pos - current_pos;
                                if (size <= 0) {
                                    result = vm_make_string("", 0);
                                } else {
                                    char *buffer = malloc(size + 1);
                                    size_t read_bytes = fread(buffer, 1, size, file->fp);
                                    buffer[read_bytes] = '\0';
                                    result = vm_make_string(buffer, read_bytes);
                                    free(buffer);
                                }
                            } else {
                                // Non-seekable: read in chunks
                                size_t capacity = 4096;
                                size_t total_read = 0;
                                char *buffer = malloc(capacity);
                                while (1) {
                                    if (total_read + 4096 > capacity) {
                                        capacity *= 2;
                                        buffer = realloc(buffer, capacity);
                                    }
                                    size_t bytes = fread(buffer + total_read, 1, 4096, file->fp);
                                    total_read += bytes;
                                    if (bytes < 4096) break;
                                }
                                buffer[total_read] = '\0';
                                result = vm_make_string(buffer, total_read);
                                free(buffer);
                            }
                        } else {
                            // Read specified size
                            int size = value_to_i32(args[0]);
                            if (size <= 0) {
                                result = vm_make_string("", 0);
                            } else {
                                char *buffer = malloc(size + 1);
                                size_t read_bytes = fread(buffer, 1, size, file->fp);
                                buffer[read_bytes] = '\0';
                                result = vm_make_string(buffer, read_bytes);
                                free(buffer);
                            }
                        }
                    } else if (strcmp(method, "write") == 0) {
                        // write(data) - write string to file
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot write to closed file '%s'", file->path);
                        }
                        if (argc < 1 || args[0].type != VAL_STRING) {
                            THROW_ERROR("write() expects string argument");
                        }
                        String *str = args[0].as.as_string;
                        size_t written = fwrite(str->data, 1, str->length, file->fp);
                        result = val_i32_vm((int32_t)written);
                    } else if (strcmp(method, "seek") == 0) {
                        // seek(position) - move file pointer
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot seek in closed file '%s'", file->path);
                        }
                        if (argc < 1) {
                            THROW_ERROR("seek() expects 1 argument (position)");
                        }
                        int position = value_to_i32(args[0]);
                        if (fseek(file->fp, position, SEEK_SET) != 0) {
                            THROW_ERROR_FMT("Seek error on file '%s': %s", file->path, strerror(errno));
                        }
                        result = val_i32_vm((int32_t)ftell(file->fp));
                    } else if (strcmp(method, "tell") == 0) {
                        // tell() - get current file position
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot tell position in closed file '%s'", file->path);
                        }
                        long pos = ftell(file->fp);
                        result = val_i32_vm((int32_t)pos);
                    } else if (strcmp(method, "close") == 0) {
                        // close() - close file
                        if (!file->closed && file->fp) {
                            fclose(file->fp);
                            file->fp = NULL;
                            file->closed = 1;
                        }
                        result = vm_null_value();
                    } else if (strcmp(method, "flush") == 0) {
                        // flush() - flush file buffer
                        if (!file->closed && file->fp) {
                            fflush(file->fp);
                        }
                        result = vm_null_value();
                    } else {
                        THROW_ERROR_FMT("File has no method '%s'", method);
                    }
                } else if (receiver.type == VAL_OBJECT && receiver.as.as_object) {
                    // Object method call - first check for built-in methods
                    Object *obj = receiver.as.as_object;

                    if (strcmp(method, "keys") == 0) {
                        // Return array of property names
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (obj->num_fields + 1));
                        arr->length = obj->num_fields;
                        arr->capacity = obj->num_fields + 1;
                        for (int i = 0; i < obj->num_fields; i++) {
                            String *s = malloc(sizeof(String));
                            s->data = strdup(obj->field_names[i]);
                            s->length = strlen(s->data);
                            s->char_length = s->length;
                            s->capacity = s->length + 1;
                            s->ref_count = 1;
                            arr->elements[i].type = VAL_STRING;
                            arr->elements[i].as.as_string = s;
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                        goto object_method_done;
                    } else if (strcmp(method, "has") == 0 && argc >= 1) {
                        // Check if property exists
                        if (args[0].type != VAL_STRING) {
                            result = val_bool_vm(false);
                        } else {
                            const char *key = args[0].as.as_string->data;
                            bool found = false;
                            for (int i = 0; i < obj->num_fields; i++) {
                                if (strcmp(obj->field_names[i], key) == 0) {
                                    found = true;
                                    break;
                                }
                            }
                            result = val_bool_vm(found);
                        }
                        goto object_method_done;
                    } else if (strcmp(method, "serialize") == 0) {
                        // Simple JSON serialization
                        size_t buf_size = 1024;
                        char *buf = malloc(buf_size);
                        size_t pos = 0;
                        buf[pos++] = '{';
                        for (int i = 0; i < obj->num_fields; i++) {
                            if (i > 0) buf[pos++] = ',';
                            pos += snprintf(buf + pos, buf_size - pos, "\"%s\":", obj->field_names[i]);
                            Value v = obj->field_values[i];
                            char *val_str = value_to_string_alloc(v);
                            if (v.type == VAL_STRING) {
                                pos += snprintf(buf + pos, buf_size - pos, "\"%s\"", val_str);
                            } else {
                                pos += snprintf(buf + pos, buf_size - pos, "%s", val_str);
                            }
                            free(val_str);
                        }
                        buf[pos++] = '}';
                        buf[pos] = '\0';
                        String *s = malloc(sizeof(String));
                        s->data = buf;
                        s->length = pos;
                        s->char_length = pos;
                        s->capacity = buf_size;
                        s->ref_count = 1;
                        result.type = VAL_STRING;
                        result.as.as_string = s;
                        goto object_method_done;
                    }

                    // Not a built-in method - look up method property
                    Value method_val = vm_null_value();
                    bool found = false;

                    // Find the method in object properties
                    for (int i = 0; i < obj->num_fields; i++) {
                        if (strcmp(obj->field_names[i], method) == 0) {
                            method_val = obj->field_values[i];
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        THROW_ERROR_FMT("Object has no method '%s'", method);
                    }

                    if (!is_vm_closure(method_val)) {
                        THROW_ERROR_FMT("Property '%s' is not a function", method);
                    }

                    VMClosure *closure = as_vm_closure(method_val);

                    // Save frame state before calling closure
                    frame->ip = ip;

                    // Set method_self so 'self' can be accessed inside the method
                    Value prev_self = vm->method_self;
                    vm->method_self = receiver;

                    // Call the method with the provided arguments
                    result = vm_call_closure(vm, closure, args, argc);

                    // Restore previous self
                    vm->method_self = prev_self;

                    // Restore frame state
                    frame = &vm->frames[vm->frame_count - 1];
                    ip = frame->ip;
                    slots = frame->slots;

                object_method_done:;
                } else {
                    THROW_ERROR_FMT("Cannot call method on %s", val_type_name(receiver.type));
                }

                // Pop args and receiver, push result
                vm_popn(vm, argc + 1);
                PUSH(result);
                break;
            }

            case BC_TYPEOF: {
                Value v = POP();
                const char *type_str;
                // Check for custom object type name
                if (v.type == VAL_OBJECT && v.as.as_object && v.as.as_object->type_name) {
                    type_str = v.as.as_object->type_name;
                } else {
                    type_str = val_type_name(v.type);
                }
                // Create string value
                String *s = malloc(sizeof(String));
                s->data = strdup(type_str);
                s->length = strlen(type_str);
                s->char_length = s->length;
                s->capacity = s->length + 1;
                s->ref_count = 1;
                Value sv;
                sv.type = VAL_STRING;
                sv.as.as_string = s;
                PUSH(sv);
                break;
            }

            case BC_CAST: {
                // Cast top of stack to specified type
                uint8_t target_type = READ_BYTE();
                Value v = POP();
                Value result;

                // Convert based on target type
                switch (target_type) {
                    case TYPE_ID_I8:
                        result.type = VAL_I8;
                        result.as.as_i8 = (int8_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I16:
                        result.type = VAL_I16;
                        result.as.as_i16 = (int16_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I32:
                        result.type = VAL_I32;
                        result.as.as_i32 = (int32_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I64:
                        result.type = VAL_I64;
                        result.as.as_i64 = value_to_i64(v);
                        break;
                    case TYPE_ID_U8:
                        result.type = VAL_U8;
                        result.as.as_u8 = (uint8_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U16:
                        result.type = VAL_U16;
                        result.as.as_u16 = (uint16_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U32:
                        result.type = VAL_U32;
                        result.as.as_u32 = (uint32_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U64:
                        result.type = VAL_U64;
                        result.as.as_u64 = (uint64_t)value_to_i64(v);
                        break;
                    case TYPE_ID_F32:
                        result.type = VAL_F32;
                        result.as.as_f32 = (float)value_to_f64(v);
                        break;
                    case TYPE_ID_F64:
                        result.type = VAL_F64;
                        result.as.as_f64 = value_to_f64(v);
                        break;
                    case TYPE_ID_BOOL:
                        result.type = VAL_BOOL;
                        result.as.as_bool = value_is_truthy(v);
                        break;
                    default:
                        // For other types, keep as-is
                        result = v;
                        break;
                }
                PUSH(result);
                break;
            }

            case BC_HALT:
                return VM_OK;

            case BC_NOP:
                break;

            // Exception handling
            case BC_TRY: {
                // Read catch and finally offsets (relative to position after both offsets)
                uint16_t catch_offset = READ_SHORT();
                uint16_t finally_offset = READ_SHORT();

                // Push exception handler
                if (vm->handler_count >= vm->handler_capacity) {
                    vm->handler_capacity *= 2;
                    vm->handlers = realloc(vm->handlers, sizeof(ExceptionHandler) * vm->handler_capacity);
                }

                ExceptionHandler *handler = &vm->handlers[vm->handler_count++];
                handler->catch_ip = ip + catch_offset;  // ip is now after both offsets
                handler->finally_ip = ip + finally_offset;
                handler->stack_top = vm->stack_top;
                handler->frame = frame;
                handler->frame_count = vm->frame_count;
                break;
            }

            case BC_THROW: {
                Value exception = POP();

                // Find nearest exception handler
                if (vm->handler_count > 0) {
                    ExceptionHandler *handler = &vm->handlers[vm->handler_count - 1];

                    // Restore stack to handler's saved state
                    vm->stack_top = handler->stack_top;

                    // Push exception for catch block
                    PUSH(exception);

                    // Jump to catch handler
                    ip = handler->catch_ip;
                } else {
                    // No handler - propagate as runtime error
                    vm->is_throwing = true;
                    vm->exception = exception;
                    if (exception.type == VAL_STRING && exception.as.as_string) {
                        vm_runtime_error(vm, "%s", exception.as.as_string->data);
                    } else {
                        vm_runtime_error(vm, "Uncaught exception");
                    }
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            case BC_CATCH:
                // Exception value is already on stack from BC_THROW
                // Just continue execution of catch block
                break;

            case BC_FINALLY:
                // Finally block just executes - nothing special needed
                break;

            case BC_END_TRY:
                // Pop exception handler
                if (vm->handler_count > 0) {
                    vm->handler_count--;
                }
                break;

            default:
                vm_runtime_error(vm, "Unknown opcode %d", instruction);
                return VM_RUNTIME_ERROR;
        }

        // Skip exception handling if no exception pending
        continue;

handle_exception:
        // Handle a thrown exception (from THROW_ERROR macro)
        if (vm->handler_count > 0) {
            ExceptionHandler *handler = &vm->handlers[vm->handler_count - 1];

            // Restore stack to handler's saved state
            vm->stack_top = handler->stack_top;

            // Create exception string value and push it
            Value exception = vm_make_string(pending_exception_msg, strlen(pending_exception_msg));
            PUSH(exception);

            // Jump to catch handler
            ip = handler->catch_ip;

            // Restore frame if we unwound the stack
            if (vm->frame_count != handler->frame_count) {
                vm->frame_count = handler->frame_count;
                frame = &vm->frames[vm->frame_count - 1];
                slots = frame->slots;
            }

            // Clear pending exception and continue execution
            pending_exception_msg = NULL;
            continue;
        } else {
            // No handler - fatal error
            vm_runtime_error(vm, "%s", pending_exception_msg);
            return VM_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef PUSH
#undef POP
#undef PEEK
#undef THROW_ERROR
#undef THROW_ERROR_FMT
}

// ============================================
// Public Entry Point
// ============================================

VMResult vm_run(VM *vm, Chunk *chunk) {
    // Set up initial call frame
    CallFrame *initial_frame = &vm->frames[vm->frame_count++];
    initial_frame->chunk = chunk;
    initial_frame->ip = chunk->code;
    initial_frame->slots = vm->stack;
    initial_frame->upvalues = NULL;
    initial_frame->slot_count = chunk->local_count;

    // Execute from base frame 0
    return vm_execute(vm, 0);
}

// ============================================
// Debug
// ============================================

void vm_trace_execution(VM *vm, bool enable) {
    (void)vm;
    vm_trace_enabled = enable;
}

void vm_dump_stack(VM *vm) {
    printf("Stack: ");
    for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("[ ");
        print_value(*slot);
        printf(" ]");
    }
    printf("\n");
}

void vm_dump_globals(VM *vm) {
    printf("Globals:\n");
    for (int i = 0; i < vm->globals.count; i++) {
        printf("  %s = ", vm->globals.names[i]);
        print_value(vm->globals.values[i]);
        printf("\n");
    }
}

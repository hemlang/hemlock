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

bool vm_set_global(VM *vm, const char *name, Value value) {
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            if (vm->globals.is_const[i]) {
                vm_runtime_error(vm, "Cannot reassign constant '%s'", name);
                return false;
            }
            vm->globals.values[i] = value;
            return true;
        }
    }
    vm_runtime_error(vm, "Undefined variable '%s'", name);
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
            return val_f64_vm(value_to_f64(a) + value_to_f64(b));
        }
        // Both integers
        ValueType result_type = promote_types(a.type, b.type);
        if (result_type == VAL_I64 || a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) + value_to_i64(b));
        }
        return val_i32_vm((int32_t)(value_to_i64(a) + value_to_i64(b)));
    }

    vm_runtime_error(vm, "Cannot add %s and %s",
                     val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_sub(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 - b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            return val_f64_vm(value_to_f64(a) - value_to_f64(b));
        }
        if (a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) - value_to_i64(b));
        }
        return val_i32_vm((int32_t)(value_to_i64(a) - value_to_i64(b)));
    }
    vm_runtime_error(vm, "Cannot subtract %s and %s",
                     val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_mul(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 * b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            return val_f64_vm(value_to_f64(a) * value_to_f64(b));
        }
        if (a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) * value_to_i64(b));
        }
        return val_i32_vm((int32_t)(value_to_i64(a) * value_to_i64(b)));
    }
    vm_runtime_error(vm, "Cannot multiply %s and %s",
                     val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_div(VM *vm, Value a, Value b) {
    // Division always returns f64 (Hemlock semantics)
    double bval = value_to_f64(b);
    if (bval == 0.0) {
        vm_runtime_error(vm, "Division by zero");
        return vm_null_value();
    }
    return val_f64_vm(value_to_f64(a) / bval);
}

static Value binary_mod(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        if (b.as.as_i32 == 0) {
            vm_runtime_error(vm, "Division by zero");
            return vm_null_value();
        }
        return val_i32_vm(a.as.as_i32 % b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            double bval = value_to_f64(b);
            if (bval == 0.0) {
                vm_runtime_error(vm, "Division by zero");
                return vm_null_value();
            }
            return val_f64_vm(fmod(value_to_f64(a), bval));
        }
        int64_t bval = value_to_i64(b);
        if (bval == 0) {
            vm_runtime_error(vm, "Division by zero");
            return vm_null_value();
        }
        if (a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) % bval);
        }
        return val_i32_vm((int32_t)(value_to_i64(a) % bval));
    }
    vm_runtime_error(vm, "Cannot modulo %s and %s",
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
    vm_runtime_error(vm, "Cannot compare %s and %s",
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

            case BC_GET_GLOBAL: {
                Constant c = READ_CONSTANT();
                Value v;
                if (!vm_get_global(vm, c.as.string.data, &v)) {
                    vm_runtime_error(vm, "Undefined variable '%s'", c.as.string.data);
                    return VM_RUNTIME_ERROR;
                }
                PUSH(v);
                break;
            }

            case BC_SET_GLOBAL: {
                Constant c = READ_CONSTANT();
                if (!vm_set_global(vm, c.as.string.data, PEEK(0))) {
                    return VM_RUNTIME_ERROR;
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
                    vm_runtime_error(vm, "Cannot get property of %s", val_type_name(obj.type));
                    return VM_RUNTIME_ERROR;
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
                    vm_runtime_error(vm, "Cannot set property on %s", val_type_name(obj.type));
                    return VM_RUNTIME_ERROR;
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
                    if (i < 0 || i >= s->length) {
                        PUSH(vm_null_value());
                    } else {
                        // Create single-char string
                        String *ch = malloc(sizeof(String));
                        ch->data = malloc(2);
                        ch->data[0] = s->data[i];
                        ch->data[1] = '\0';
                        ch->length = 1;
                        ch->char_length = 1;
                        ch->capacity = 2;
                        ch->ref_count = 1;
                        Value v = {.type = VAL_STRING, .as.as_string = ch};
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
                    }
                    PUSH(vm_null_value());
                    index_found:;
                } else {
                    vm_runtime_error(vm, "Cannot index %s", val_type_name(obj.type));
                    return VM_RUNTIME_ERROR;
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
                        vm_runtime_error(vm, "Array index out of bounds: %d", i);
                        return VM_RUNTIME_ERROR;
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
                        vm_runtime_error(vm, "Object key must be string");
                        return VM_RUNTIME_ERROR;
                    }
                } else {
                    vm_runtime_error(vm, "Cannot set index on %s", val_type_name(obj.type));
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            // Arithmetic
            case BC_ADD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_add(vm, a, b));
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_SUB: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_sub(vm, a, b));
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_MUL: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mul(vm, a, b));
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_DIV: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_div(vm, a, b));
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_MOD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mod(vm, a, b));
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_NEGATE: {
                Value a = POP();
                switch (a.type) {
                    case VAL_I32: PUSH(val_i32_vm(-a.as.as_i32)); break;
                    case VAL_I64: PUSH(val_i64_vm(-a.as.as_i64)); break;
                    case VAL_F64: PUSH(val_f64_vm(-a.as.as_f64)); break;
                    default:
                        vm_runtime_error(vm, "Cannot negate %s", val_type_name(a.type));
                        return VM_RUNTIME_ERROR;
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
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                break;
            }

            case BC_LE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                Value eq = binary_eq(a, b);
                PUSH(val_bool_vm(lt.as.as_bool || eq.as.as_bool));
                break;
            }

            case BC_GT: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, b, a);  // Swap operands
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
                PUSH(lt);
                break;
            }

            case BC_GE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->is_throwing) return VM_RUNTIME_ERROR;
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
                    vm_runtime_error(vm, "Cannot bitwise NOT %s", val_type_name(a.type));
                    return VM_RUNTIME_ERROR;
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
                    vm_runtime_error(vm, "for-in requires an array");
                    return VM_RUNTIME_ERROR;
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
                        case VAL_RUNE:
                            // UTF-8 encode the rune
                            if (v.as.as_rune < 0x80) {
                                printf("%c", (char)v.as.as_rune);
                            } else {
                                // TODO: Proper UTF-8 encoding
                                printf("\\u%04X", v.as.as_rune);
                            }
                            break;
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
                            const char *type_str = val_type_name(args[0].type);
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
                    case BUILTIN_ASSERT: {
                        if (argc >= 1 && !value_is_truthy(args[0])) {
                            const char *msg = (argc >= 2 && args[1].type == VAL_STRING)
                                ? args[1].as.as_string->data : "Assertion failed";
                            vm_runtime_error(vm, "%s", msg);
                            return VM_RUNTIME_ERROR;
                        }
                        break;
                    }
                    case BUILTIN_PANIC: {
                        const char *msg = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : "panic";
                        fprintf(stderr, "panic: %s\n", msg);
                        exit(1);
                    }
                    case BUILTIN_DIVI: {
                        // Integer division (floor)
                        if (argc >= 2) {
                            int64_t a = value_to_i64(args[0]);
                            int64_t b = value_to_i64(args[1]);
                            if (b == 0) {
                                vm_runtime_error(vm, "Division by zero");
                                return VM_RUNTIME_ERROR;
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
                                vm_runtime_error(vm, "Modulo by zero");
                                return VM_RUNTIME_ERROR;
                            }
                            result = val_i64_vm(a % b);
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
                    vm_runtime_error(vm, "Can only call functions");
                    return VM_RUNTIME_ERROR;
                }

                VMClosure *closure = as_vm_closure(callee);
                Chunk *fn_chunk = closure->chunk;

                // Check arity
                if (argc < fn_chunk->arity) {
                    // Allow optional params
                    int required = fn_chunk->arity - fn_chunk->optional_count;
                    if (argc < required) {
                        vm_runtime_error(vm, "Expected at least %d arguments but got %d", required, argc);
                        return VM_RUNTIME_ERROR;
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

            case BC_RETURN: {
                Value result = POP();

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
                            vm_runtime_error(vm, "map() callback must be a function");
                            return VM_RUNTIME_ERROR;
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
                            vm_runtime_error(vm, "filter() callback must be a function");
                            return VM_RUNTIME_ERROR;
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
                    } else if (strcmp(method, "reduce") == 0 && argc >= 2) {
                        // reduce(callback, initial) - accumulate values
                        if (!is_vm_closure(args[0])) {
                            vm_runtime_error(vm, "reduce() callback must be a function");
                            return VM_RUNTIME_ERROR;
                        }
                        VMClosure *callback = as_vm_closure(args[0]);
                        Value accumulator = args[1];

                        // Save frame state
                        frame->ip = ip;

                        for (int i = 0; i < arr->length; i++) {
                            Value callback_args[2] = {accumulator, arr->elements[i]};
                            accumulator = vm_call_closure(vm, callback, callback_args, 2);
                        }

                        // Restore frame state
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result = accumulator;
                    } else {
                        vm_runtime_error(vm, "Unknown array method: %s", method);
                        return VM_RUNTIME_ERROR;
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
                    } else {
                        vm_runtime_error(vm, "Unknown string method: %s", method);
                        return VM_RUNTIME_ERROR;
                    }
                } else if (receiver.type == VAL_OBJECT && receiver.as.as_object) {
                    // Object method call - look up method property and call it
                    Object *obj = receiver.as.as_object;
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
                        vm_runtime_error(vm, "Object has no method '%s'", method);
                        return VM_RUNTIME_ERROR;
                    }

                    if (!is_vm_closure(method_val)) {
                        vm_runtime_error(vm, "Property '%s' is not a function", method);
                        return VM_RUNTIME_ERROR;
                    }

                    VMClosure *closure = as_vm_closure(method_val);

                    // Save frame state before calling closure
                    frame->ip = ip;

                    // Call the method with the provided arguments
                    result = vm_call_closure(vm, closure, args, argc);

                    // Restore frame state
                    frame = &vm->frames[vm->frame_count - 1];
                    ip = frame->ip;
                    slots = frame->slots;
                } else {
                    vm_runtime_error(vm, "Cannot call method on %s", val_type_name(receiver.type));
                    return VM_RUNTIME_ERROR;
                }

                // Pop args and receiver, push result
                vm_popn(vm, argc + 1);
                PUSH(result);
                break;
            }

            case BC_TYPEOF: {
                Value v = POP();
                const char *type_str = val_type_name(v.type);
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

            case BC_HALT:
                return VM_OK;

            case BC_NOP:
                break;

            default:
                vm_runtime_error(vm, "Unknown opcode %d", instruction);
                return VM_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef PUSH
#undef POP
#undef PEEK
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

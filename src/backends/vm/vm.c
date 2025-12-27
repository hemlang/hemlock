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
    // i32 fast path
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 + b.as.as_i32);
    }

    // String concatenation
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        // TODO: Implement string concatenation
        return vm_null_value();
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
// Main Execution Loop
// ============================================

VMResult vm_run(VM *vm, Chunk *chunk) {
    // Set up initial call frame
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack;
    frame->upvalues = NULL;
    frame->slot_count = chunk->local_count;

    // Reserve space for locals
    for (int i = 0; i < chunk->local_count; i++) {
        vm_push(vm, vm_null_value());
    }

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

            case BC_POP:
                POP();
                break;

            case BC_POPN: {
                uint8_t n = READ_BYTE();
                vm_popn(vm, n);
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

            case BC_RETURN: {
                Value result = POP();

                // Close upvalues
                vm_close_upvalues(vm, slots);

                vm->frame_count--;
                if (vm->frame_count == 0) {
                    // Done
                    POP();  // Pop script slot
                    return VM_OK;
                }

                // Restore previous frame
                vm->stack_top = slots;
                PUSH(result);

                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                slots = frame->slots;
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

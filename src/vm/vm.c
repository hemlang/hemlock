/*
 * Hemlock Bytecode VM - Main Implementation
 */

#include "vm.h"
#include "bytecode.h"
#include "chunk.h"
#include "compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

// Initial stack size
#define INITIAL_STACK_SIZE 256

// ========== VM Creation/Destruction ==========

VM* vm_new(void) {
    VM *vm = malloc(sizeof(VM));

    vm->frame_count = 0;

    vm->stack_capacity = INITIAL_STACK_SIZE;
    vm->stack = malloc(vm->stack_capacity * sizeof(Value));
    vm->stack_top = vm->stack;

    vm->global_names = NULL;
    vm->global_values = NULL;
    vm->global_count = 0;
    vm->global_capacity = 0;

    vm->open_upvalues = NULL;
    vm->defer_count = 0;

    vm->has_exception = false;
    vm->exception = val_null();

    vm->error_message = NULL;
    vm->error_line = 0;

    vm->builtins = NULL;
    vm->builtin_names = NULL;
    vm->builtin_count = 0;

    vm->exec_ctx = exec_context_new();

    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;

    free(vm->stack);

    // Free globals
    for (int i = 0; i < vm->global_count; i++) {
        free(vm->global_names[i]);
    }
    free(vm->global_names);
    free(vm->global_values);

    // Free builtins
    for (int i = 0; i < vm->builtin_count; i++) {
        free(vm->builtin_names[i]);
    }
    free(vm->builtins);
    free(vm->builtin_names);

    // Free open upvalues
    VMUpvalue *uv = vm->open_upvalues;
    while (uv) {
        VMUpvalue *next = uv->next;
        free(uv);
        uv = next;
    }

    free(vm->error_message);
    exec_context_free(vm->exec_ctx);

    free(vm);
}

void vm_reset(VM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->defer_count = 0;
    vm->has_exception = false;
    vm->exception = val_null();
    free(vm->error_message);
    vm->error_message = NULL;
    vm->error_line = 0;
}

// ========== Stack Operations ==========

void vm_push(VM *vm, Value value) {
    // Grow stack if needed
    int size = vm->stack_top - vm->stack;
    if (size >= vm->stack_capacity) {
        int new_capacity = vm->stack_capacity * 2;
        int offset = vm->stack_top - vm->stack;
        vm->stack = realloc(vm->stack, new_capacity * sizeof(Value));
        vm->stack_top = vm->stack + offset;
        vm->stack_capacity = new_capacity;

        // Update frame slot pointers
        for (int i = 0; i < vm->frame_count; i++) {
            // Frame slots are offsets into the stack
            // We need to recalculate them after realloc
        }
    }

    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM *vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

Value vm_peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

// ========== Global Variables ==========

static int find_global(VM *vm, const char *name) {
    for (int i = 0; i < vm->global_count; i++) {
        if (strcmp(vm->global_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void vm_define_global(VM *vm, const char *name, Value value) {
    int idx = find_global(vm, name);
    if (idx >= 0) {
        // Update existing
        vm->global_values[idx] = value;
        return;
    }

    // Add new
    if (vm->global_count >= vm->global_capacity) {
        int new_cap = vm->global_capacity < 8 ? 8 : vm->global_capacity * 2;
        vm->global_names = realloc(vm->global_names, new_cap * sizeof(char*));
        vm->global_values = realloc(vm->global_values, new_cap * sizeof(Value));
        vm->global_capacity = new_cap;
    }

    vm->global_names[vm->global_count] = strdup(name);
    vm->global_values[vm->global_count] = value;
    vm->global_count++;
}

bool vm_get_global(VM *vm, const char *name, Value *value) {
    int idx = find_global(vm, name);
    if (idx < 0) return false;
    *value = vm->global_values[idx];
    return true;
}

bool vm_set_global(VM *vm, const char *name, Value value) {
    int idx = find_global(vm, name);
    if (idx < 0) return false;
    vm->global_values[idx] = value;
    return true;
}

// ========== Builtin Registration ==========

void vm_register_builtin(VM *vm, const char *name, BuiltinFn fn) {
    // Also register as global
    vm_define_global(vm, name, val_builtin_fn(fn));

    // Add to builtins array for direct access
    vm->builtins = realloc(vm->builtins, (vm->builtin_count + 1) * sizeof(BuiltinFn));
    vm->builtin_names = realloc(vm->builtin_names, (vm->builtin_count + 1) * sizeof(char*));
    vm->builtins[vm->builtin_count] = fn;
    vm->builtin_names[vm->builtin_count] = strdup(name);
    vm->builtin_count++;
}

// ========== Upvalue Management ==========

VMUpvalue* vm_capture_upvalue(VM *vm, Value *local) {
    VMUpvalue *prev = NULL;
    VMUpvalue *uv = vm->open_upvalues;

    // Find insertion point (sorted by stack location)
    while (uv != NULL && uv->location > local) {
        prev = uv;
        uv = uv->next;
    }

    // Already captured?
    if (uv != NULL && uv->location == local) {
        return uv;
    }

    // Create new upvalue
    VMUpvalue *new_uv = malloc(sizeof(VMUpvalue));
    new_uv->location = local;
    new_uv->closed = val_null();
    new_uv->next = uv;

    if (prev == NULL) {
        vm->open_upvalues = new_uv;
    } else {
        prev->next = new_uv;
    }

    return new_uv;
}

void vm_close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        VMUpvalue *uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}

// ========== Error Handling ==========

void vm_runtime_error(VM *vm, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char buf[512];
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // Get line number from current frame
    if (vm->frame_count > 0) {
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        int offset = frame->ip - frame->chunk->code - 1;
        vm->error_line = chunk_get_line(frame->chunk, offset);
    }

    free(vm->error_message);
    vm->error_message = strdup(buf);

    fprintf(stderr, "[line %d] Runtime error: %s\n", vm->error_line, buf);

    // Print stack trace
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        Chunk *chunk = frame->chunk;
        int offset = frame->ip - chunk->code - 1;
        int line = chunk_get_line(chunk, offset);

        fprintf(stderr, "  at %s (line %d)\n",
                chunk->name ? chunk->name : "<script>", line);
    }
}

const char* vm_get_error(VM *vm) {
    return vm->error_message;
}

// ========== Value Operations ==========

static bool is_falsey(Value val) {
    switch (val.type) {
        case VAL_NULL: return true;
        case VAL_BOOL: return !val.as.as_bool;
        case VAL_I32: return val.as.as_i32 == 0;
        case VAL_I64: return val.as.as_i64 == 0;
        case VAL_F64: return val.as.as_f64 == 0.0;
        default: return false;
    }
}

static bool values_equal(Value a, Value b) {
    if (a.type != b.type) {
        // Type coercion for numbers
        if ((a.type >= VAL_I8 && a.type <= VAL_F64) &&
            (b.type >= VAL_I8 && b.type <= VAL_F64)) {
            // Convert both to double for comparison
            double da, db;
            switch (a.type) {
                case VAL_I8: da = a.as.as_i8; break;
                case VAL_I16: da = a.as.as_i16; break;
                case VAL_I32: da = a.as.as_i32; break;
                case VAL_I64: da = (double)a.as.as_i64; break;
                case VAL_U8: da = a.as.as_u8; break;
                case VAL_U16: da = a.as.as_u16; break;
                case VAL_U32: da = a.as.as_u32; break;
                case VAL_U64: da = (double)a.as.as_u64; break;
                case VAL_F32: da = a.as.as_f32; break;
                case VAL_F64: da = a.as.as_f64; break;
                default: da = 0;
            }
            switch (b.type) {
                case VAL_I8: db = b.as.as_i8; break;
                case VAL_I16: db = b.as.as_i16; break;
                case VAL_I32: db = b.as.as_i32; break;
                case VAL_I64: db = (double)b.as.as_i64; break;
                case VAL_U8: db = b.as.as_u8; break;
                case VAL_U16: db = b.as.as_u16; break;
                case VAL_U32: db = b.as.as_u32; break;
                case VAL_U64: db = (double)b.as.as_u64; break;
                case VAL_F32: db = b.as.as_f32; break;
                case VAL_F64: db = b.as.as_f64; break;
                default: db = 0;
            }
            return da == db;
        }
        return false;
    }

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
        case VAL_RUNE: return a.as.as_rune == b.as.as_rune;
        case VAL_STRING:
            if (a.as.as_string == b.as.as_string) return true;
            if (a.as.as_string->length != b.as.as_string->length) return false;
            return memcmp(a.as.as_string->data, b.as.as_string->data,
                         a.as.as_string->length) == 0;
        default:
            // Reference equality for other types
            return a.as.as_ptr == b.as.as_ptr;
    }
}

// Convert value to double for arithmetic
static double value_to_double(Value val) {
    switch (val.type) {
        case VAL_I8: return val.as.as_i8;
        case VAL_I16: return val.as.as_i16;
        case VAL_I32: return val.as.as_i32;
        case VAL_I64: return (double)val.as.as_i64;
        case VAL_U8: return val.as.as_u8;
        case VAL_U16: return val.as.as_u16;
        case VAL_U32: return val.as.as_u32;
        case VAL_U64: return (double)val.as.as_u64;
        case VAL_F32: return val.as.as_f32;
        case VAL_F64: return val.as.as_f64;
        default: return 0.0;
    }
}

// Convert value to int64 for bitwise ops
static int64_t value_to_int64(Value val) {
    switch (val.type) {
        case VAL_I8: return val.as.as_i8;
        case VAL_I16: return val.as.as_i16;
        case VAL_I32: return val.as.as_i32;
        case VAL_I64: return val.as.as_i64;
        case VAL_U8: return val.as.as_u8;
        case VAL_U16: return val.as.as_u16;
        case VAL_U32: return val.as.as_u32;
        case VAL_U64: return (int64_t)val.as.as_u64;
        case VAL_F32: return (int64_t)val.as.as_f32;
        case VAL_F64: return (int64_t)val.as.as_f64;
        default: return 0;
    }
}

// Check if value is a number
static bool is_number(Value val) {
    return val.type >= VAL_I8 && val.type <= VAL_F64;
}

// Check if value is an integer
static bool is_integer(Value val) {
    return val.type >= VAL_I8 && val.type <= VAL_U64;
}

// Determine result type for binary arithmetic
static ValueType arithmetic_result_type(Value a, Value b) {
    // If either is float, result is float
    if (a.type == VAL_F64 || b.type == VAL_F64) return VAL_F64;
    if (a.type == VAL_F32 || b.type == VAL_F32) return VAL_F64;

    // Otherwise use larger integer type
    if (a.type == VAL_I64 || b.type == VAL_I64) return VAL_I64;
    if (a.type == VAL_U64 || b.type == VAL_U64) return VAL_I64;

    return VAL_I32;
}

// Create result value with appropriate type
static Value make_number(double val, ValueType type) {
    switch (type) {
        case VAL_I32: return val_i32((int32_t)val);
        case VAL_I64: return val_i64((int64_t)val);
        case VAL_F64: return val_f64(val);
        default: return val_f64(val);
    }
}

// ========== Main Execution Loop ==========

VMResult vm_run(VM *vm, Chunk *chunk) {
    // Set up initial frame
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack;
    frame->upvalues = NULL;
    frame->num_upvalues = 0;
    frame->return_dest = 0;  // Not used for top-level

    // Ensure enough stack space
    while ((int)(vm->stack_top - vm->stack) + chunk->max_stack_size >= vm->stack_capacity) {
        int old_offset = vm->stack_top - vm->stack;
        vm->stack_capacity *= 2;
        vm->stack = realloc(vm->stack, vm->stack_capacity * sizeof(Value));
        vm->stack_top = vm->stack + old_offset;
        frame->slots = vm->stack;  // Update slot pointer
    }

    // Reserve stack space for registers
    for (int i = 0; i < chunk->max_stack_size; i++) {
        vm_push(vm, val_null());
    }

    // Macros for bytecode dispatch
    #define READ_BYTE() (*frame->ip++)
    #define READ_INSTRUCTION() (frame->ip++, frame->ip[-1])
    #define READ_CONSTANT(idx) (chunk_get_constant(frame->chunk, idx))

    #define REG(i) (frame->slots[i])

    #define BINARY_OP(op) do { \
        Value b = REG(c); \
        Value a = REG(b_); \
        if (!is_number(a) || !is_number(b)) { \
            vm_runtime_error(vm, "Operands must be numbers"); \
            return VM_RUNTIME_ERROR; \
        } \
        double da = value_to_double(a); \
        double db = value_to_double(b); \
        ValueType rt = arithmetic_result_type(a, b); \
        REG(a_) = make_number(da op db, rt); \
    } while (0)

    #define COMPARISON_OP(op) do { \
        Value b = REG(c); \
        Value a = REG(b_); \
        if (!is_number(a) || !is_number(b)) { \
            vm_runtime_error(vm, "Operands must be numbers"); \
            return VM_RUNTIME_ERROR; \
        } \
        double da = value_to_double(a); \
        double db = value_to_double(b); \
        REG(a_) = val_bool(da op db); \
    } while (0)

    #define BITWISE_OP(op) do { \
        Value b = REG(c); \
        Value a = REG(b_); \
        if (!is_integer(a) || !is_integer(b)) { \
            vm_runtime_error(vm, "Operands must be integers for bitwise operation"); \
            return VM_RUNTIME_ERROR; \
        } \
        int64_t ia = value_to_int64(a); \
        int64_t ib = value_to_int64(b); \
        REG(a_) = val_i64(ia op ib); \
    } while (0)

    // Main dispatch loop
    for (;;) {
        uint32_t instruction = *frame->ip++;
        Opcode op = DECODE_OP(instruction);
        uint8_t a_ = DECODE_A(instruction);
        uint8_t b_ = DECODE_B(instruction);
        uint8_t c = DECODE_C(instruction);
        uint16_t bx = DECODE_Bx(instruction);
        int16_t sbx = DECODE_sBx(instruction);
        int32_t sax = DECODE_sAx(instruction);

        switch (op) {
            case BC_LOAD_CONST: {
                Constant *k = READ_CONSTANT(bx);
                if (!k) {
                    vm_runtime_error(vm, "Invalid constant index");
                    return VM_RUNTIME_ERROR;
                }
                switch (k->type) {
                    case CONST_NULL: REG(a_) = val_null(); break;
                    case CONST_BOOL: REG(a_) = val_bool(k->as.as_bool); break;
                    case CONST_I32: REG(a_) = val_i32(k->as.as_i32); break;
                    case CONST_I64: REG(a_) = val_i64(k->as.as_i64); break;
                    case CONST_F64: REG(a_) = val_f64(k->as.as_f64); break;
                    case CONST_RUNE: REG(a_) = val_rune(k->as.as_rune); break;
                    case CONST_STRING:
                        REG(a_) = val_string(k->as.as_string.data);
                        break;
                }
                break;
            }

            case BC_LOAD_NULL:
                REG(a_) = val_null();
                break;

            case BC_LOAD_TRUE:
                REG(a_) = val_bool(1);
                break;

            case BC_LOAD_FALSE:
                REG(a_) = val_bool(0);
                break;

            case BC_MOVE:
                REG(a_) = REG(b_);
                break;

            case BC_LOAD_GLOBAL: {
                Constant *k = READ_CONSTANT(bx);
                if (!k || k->type != CONST_STRING) {
                    vm_runtime_error(vm, "Invalid global name");
                    return VM_RUNTIME_ERROR;
                }
                Value val;
                if (!vm_get_global(vm, k->as.as_string.data, &val)) {
                    vm_runtime_error(vm, "Undefined variable '%s'", k->as.as_string.data);
                    return VM_RUNTIME_ERROR;
                }
                REG(a_) = val;
                break;
            }

            case BC_STORE_GLOBAL: {
                Constant *k = READ_CONSTANT(bx);
                if (!k || k->type != CONST_STRING) {
                    vm_runtime_error(vm, "Invalid global name");
                    return VM_RUNTIME_ERROR;
                }
                vm_define_global(vm, k->as.as_string.data, REG(a_));
                break;
            }

            case BC_LOAD_UPVALUE: {
                if (frame->upvalues && bx < (uint16_t)frame->num_upvalues) {
                    REG(a_) = *frame->upvalues[bx]->location;
                }
                break;
            }

            case BC_STORE_UPVALUE: {
                if (frame->upvalues && bx < (uint16_t)frame->num_upvalues) {
                    *frame->upvalues[bx]->location = REG(a_);
                }
                break;
            }

            // Arithmetic operations
            case BC_ADD: {
                Value a = REG(b_);
                Value b = REG(c);

                // String concatenation
                if (a.type == VAL_STRING && b.type == VAL_STRING) {
                    String *result = string_concat(a.as.as_string, b.as.as_string);
                    REG(a_) = val_string(result->data);
                    string_free(result);
                    break;
                }

                if (!is_number(a) || !is_number(b)) {
                    vm_runtime_error(vm, "Operands must be numbers or strings for +");
                    return VM_RUNTIME_ERROR;
                }

                double da = value_to_double(a);
                double db = value_to_double(b);
                ValueType rt = arithmetic_result_type(a, b);
                REG(a_) = make_number(da + db, rt);
                break;
            }

            case BC_SUB: BINARY_OP(-); break;
            case BC_MUL: BINARY_OP(*); break;

            case BC_DIV: {
                Value a = REG(b_);
                Value b = REG(c);
                if (!is_number(a) || !is_number(b)) {
                    vm_runtime_error(vm, "Operands must be numbers");
                    return VM_RUNTIME_ERROR;
                }
                double db = value_to_double(b);
                if (db == 0) {
                    vm_runtime_error(vm, "Division by zero");
                    return VM_RUNTIME_ERROR;
                }
                double da = value_to_double(a);
                REG(a_) = val_f64(da / db);
                break;
            }

            case BC_MOD: {
                Value a = REG(b_);
                Value b = REG(c);
                if (!is_integer(a) || !is_integer(b)) {
                    vm_runtime_error(vm, "Operands must be integers for %%");
                    return VM_RUNTIME_ERROR;
                }
                int64_t ib = value_to_int64(b);
                if (ib == 0) {
                    vm_runtime_error(vm, "Modulo by zero");
                    return VM_RUNTIME_ERROR;
                }
                int64_t ia = value_to_int64(a);
                REG(a_) = val_i64(ia % ib);
                break;
            }

            case BC_POW: {
                Value a = REG(b_);
                Value b = REG(c);
                if (!is_number(a) || !is_number(b)) {
                    vm_runtime_error(vm, "Operands must be numbers");
                    return VM_RUNTIME_ERROR;
                }
                double da = value_to_double(a);
                double db = value_to_double(b);
                REG(a_) = val_f64(pow(da, db));
                break;
            }

            case BC_NEG: {
                Value a = REG(b_);
                if (!is_number(a)) {
                    vm_runtime_error(vm, "Operand must be a number");
                    return VM_RUNTIME_ERROR;
                }
                if (a.type == VAL_F64) {
                    REG(a_) = val_f64(-a.as.as_f64);
                } else if (a.type == VAL_F32) {
                    REG(a_) = val_f32(-a.as.as_f32);
                } else if (a.type == VAL_I64) {
                    REG(a_) = val_i64(-a.as.as_i64);
                } else {
                    REG(a_) = val_i32(-value_to_int64(a));
                }
                break;
            }

            // Bitwise operations
            case BC_BAND: BITWISE_OP(&); break;
            case BC_BOR:  BITWISE_OP(|); break;
            case BC_BXOR: BITWISE_OP(^); break;
            case BC_SHL:  BITWISE_OP(<<); break;
            case BC_SHR:  BITWISE_OP(>>); break;

            case BC_BNOT: {
                Value a = REG(b_);
                if (!is_integer(a)) {
                    vm_runtime_error(vm, "Operand must be an integer for ~");
                    return VM_RUNTIME_ERROR;
                }
                REG(a_) = val_i64(~value_to_int64(a));
                break;
            }

            // Comparison operations
            case BC_EQ:
                REG(a_) = val_bool(values_equal(REG(b_), REG(c)));
                break;

            case BC_NE:
                REG(a_) = val_bool(!values_equal(REG(b_), REG(c)));
                break;

            case BC_LT: COMPARISON_OP(<); break;
            case BC_LE: COMPARISON_OP(<=); break;
            case BC_GT: COMPARISON_OP(>); break;
            case BC_GE: COMPARISON_OP(>=); break;

            // Logical operations
            case BC_NOT:
                REG(a_) = val_bool(is_falsey(REG(b_)));
                break;

            // Control flow
            case BC_JMP:
                frame->ip += sax;
                break;

            case BC_JMP_IF_FALSE:
                if (is_falsey(REG(a_))) {
                    frame->ip += sbx;
                }
                break;

            case BC_JMP_IF_TRUE:
                if (!is_falsey(REG(a_))) {
                    frame->ip += sbx;
                }
                break;

            case BC_LOOP:
                frame->ip -= sax;
                break;

            // Function calls
            case BC_CALL: {
                Value callee = REG(a_);
                int arg_count = b_;
                int num_results = c;
                (void)num_results; // TODO: handle multiple returns
                int call_dest = a_;  // Where to store result

                if (callee.type == VAL_BUILTIN_FN) {
                    // Call builtin
                    Value args[256];
                    for (int i = 0; i < arg_count; i++) {
                        args[i] = REG(a_ + 1 + i);
                    }
                    Value result = callee.as.as_builtin_fn(args, arg_count, vm->exec_ctx);
                    REG(a_) = result;
                } else if (callee.type == VAL_FUNCTION) {
                    Function *fn = callee.as.as_function;
                    Chunk *fn_chunk = (Chunk *)fn->bytecode_chunk;

                    if (!fn_chunk) {
                        vm_runtime_error(vm, "Function has no bytecode (AST-only function)");
                        return VM_RUNTIME_ERROR;
                    }

                    // Check arg count
                    if (arg_count != fn->num_params) {
                        vm_runtime_error(vm, "Expected %d arguments but got %d",
                                        fn->num_params, arg_count);
                        return VM_RUNTIME_ERROR;
                    }

                    // Check call stack depth
                    if (vm->frame_count >= VM_MAX_FRAMES) {
                        vm_runtime_error(vm, "Stack overflow (too many nested calls)");
                        return VM_RUNTIME_ERROR;
                    }

                    // Set up new call frame
                    CallFrame *new_frame = &vm->frames[vm->frame_count++];
                    new_frame->chunk = fn_chunk;
                    new_frame->ip = fn_chunk->code;
                    new_frame->upvalues = NULL;
                    new_frame->num_upvalues = 0;
                    new_frame->return_dest = call_dest;  // Where to store result in caller

                    // Ensure stack space
                    while ((int)(vm->stack_top - vm->stack) + fn_chunk->max_stack_size >= vm->stack_capacity) {
                        int old_offset = vm->stack_top - vm->stack;
                        int old_slot_offset = frame->slots - vm->stack;
                        vm->stack_capacity *= 2;
                        vm->stack = realloc(vm->stack, vm->stack_capacity * sizeof(Value));
                        vm->stack_top = vm->stack + old_offset;
                        frame->slots = vm->stack + old_slot_offset;
                    }

                    // New frame's slots start where stack_top currently is
                    new_frame->slots = vm->stack_top;

                    // Copy arguments to new frame's first registers (params become locals 0..n-1)
                    for (int i = 0; i < arg_count; i++) {
                        new_frame->slots[i] = REG(a_ + 1 + i);
                    }

                    // Initialize remaining registers with null
                    for (int i = arg_count; i < fn_chunk->max_stack_size; i++) {
                        new_frame->slots[i] = val_null();
                    }

                    // Advance stack_top
                    vm->stack_top = new_frame->slots + fn_chunk->max_stack_size;

                    // Switch to new frame
                    frame = new_frame;
                } else {
                    vm_runtime_error(vm, "Can only call functions");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            case BC_RETURN: {
                int return_count = b_;
                Value result = return_count > 0 ? REG(a_) : val_null();
                int dest = frame->return_dest;  // Save before switching frames

                // Close upvalues
                vm_close_upvalues(vm, frame->slots);

                // Execute defers
                while (vm->defer_count > 0) {
                    vm->defer_count--;
                    Value deferred = vm->defer_stack[vm->defer_count];
                    if (deferred.type == VAL_BUILTIN_FN) {
                        deferred.as.as_builtin_fn(NULL, 0, vm->exec_ctx);
                    }
                    // TODO: Call user functions
                }

                vm->frame_count--;
                if (vm->frame_count == 0) {
                    // Top-level return - we're done
                    return VM_OK;
                }

                // Restore caller frame
                CallFrame *caller = &vm->frames[vm->frame_count - 1];
                frame = caller;
                vm->stack_top = caller->slots + caller->chunk->max_stack_size;

                // Store result in caller's destination register
                caller->slots[dest] = result;
                break;
            }

            case BC_CLOSURE: {
                Chunk *proto = frame->chunk->protos[bx];

                // Create function value
                Function *fn = malloc(sizeof(Function));
                fn->is_async = proto->is_async;
                fn->param_names = NULL;  // Not needed at runtime
                fn->param_types = NULL;
                fn->param_defaults = NULL;
                fn->num_params = proto->arity;
                fn->return_type = NULL;
                fn->body = NULL;  // Body is in bytecode
                fn->closure_env = NULL;
                fn->ref_count = 1;
                fn->is_bound = false;
                fn->bytecode_chunk = proto;  // Store bytecode chunk for VM calls

                REG(a_) = val_function(fn);

                // Capture upvalues
                // TODO: Implement upvalue capture from proto->upvalues
                break;
            }

            // Object/Array operations
            case BC_NEW_ARRAY: {
                int num_elements = b_;
                Array *arr = array_new();

                for (int i = 0; i < num_elements; i++) {
                    array_push(arr, REG(a_ + 1 + i));
                }

                REG(a_) = val_array(arr);
                break;
            }

            case BC_NEW_OBJECT: {
                int num_fields = b_;
                Object *obj = object_new(NULL, num_fields);

                // Field names would be in constants
                // TODO: Implement field assignment
                (void)num_fields;

                REG(a_) = val_object(obj);
                break;
            }

            case BC_GET_INDEX: {
                Value container = REG(b_);
                Value index = REG(c);

                if (container.type == VAL_ARRAY) {
                    if (!is_integer(index)) {
                        vm_runtime_error(vm, "Array index must be an integer");
                        return VM_RUNTIME_ERROR;
                    }
                    int idx = (int)value_to_int64(index);
                    REG(a_) = array_get(container.as.as_array, idx, vm->exec_ctx);
                } else if (container.type == VAL_STRING) {
                    // String indexing returns character
                    if (!is_integer(index)) {
                        vm_runtime_error(vm, "String index must be an integer");
                        return VM_RUNTIME_ERROR;
                    }
                    // TODO: Implement string indexing
                    REG(a_) = val_null();
                } else if (container.type == VAL_OBJECT) {
                    // Object indexing by key
                    // TODO: Implement object indexing
                    REG(a_) = val_null();
                } else {
                    vm_runtime_error(vm, "Cannot index this value");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            case BC_SET_INDEX: {
                Value container = REG(a_);
                Value index = REG(b_);
                Value value = REG(c);

                if (container.type == VAL_ARRAY) {
                    if (!is_integer(index)) {
                        vm_runtime_error(vm, "Array index must be an integer");
                        return VM_RUNTIME_ERROR;
                    }
                    int idx = (int)value_to_int64(index);
                    array_set(container.as.as_array, idx, value, vm->exec_ctx);
                } else {
                    vm_runtime_error(vm, "Cannot set index on this value");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            case BC_GET_FIELD: {
                Value obj = REG(b_);
                Constant *k = READ_CONSTANT(c);

                if (obj.type != VAL_OBJECT) {
                    vm_runtime_error(vm, "Only objects have fields");
                    return VM_RUNTIME_ERROR;
                }
                if (!k || k->type != CONST_STRING) {
                    vm_runtime_error(vm, "Invalid field name");
                    return VM_RUNTIME_ERROR;
                }

                Object *o = obj.as.as_object;
                const char *field = k->as.as_string.data;

                // Find field
                for (int i = 0; i < o->num_fields; i++) {
                    if (strcmp(o->field_names[i], field) == 0) {
                        REG(a_) = o->field_values[i];
                        goto field_found;
                    }
                }
                REG(a_) = val_null();
                field_found:
                break;
            }

            case BC_SET_FIELD: {
                Value obj = REG(a_);
                Constant *k = READ_CONSTANT(b_);
                Value value = REG(c);

                if (obj.type != VAL_OBJECT) {
                    vm_runtime_error(vm, "Only objects have fields");
                    return VM_RUNTIME_ERROR;
                }
                if (!k || k->type != CONST_STRING) {
                    vm_runtime_error(vm, "Invalid field name");
                    return VM_RUNTIME_ERROR;
                }

                Object *o = obj.as.as_object;
                const char *field = k->as.as_string.data;

                // Find or add field
                for (int i = 0; i < o->num_fields; i++) {
                    if (strcmp(o->field_names[i], field) == 0) {
                        o->field_values[i] = value;
                        goto field_set;
                    }
                }

                // Add new field
                if (o->num_fields >= o->capacity) {
                    int new_cap = o->capacity < 4 ? 4 : o->capacity * 2;
                    o->field_names = realloc(o->field_names, new_cap * sizeof(char*));
                    o->field_values = realloc(o->field_values, new_cap * sizeof(Value));
                    o->capacity = new_cap;
                }
                o->field_names[o->num_fields] = strdup(field);
                o->field_values[o->num_fields] = value;
                o->num_fields++;
                field_set:
                break;
            }

            // Increment/Decrement
            case BC_INC: {
                Value a = REG(a_);
                if (!is_number(a)) {
                    vm_runtime_error(vm, "Operand must be a number for ++");
                    return VM_RUNTIME_ERROR;
                }
                if (a.type == VAL_F64) {
                    REG(a_) = val_f64(a.as.as_f64 + 1);
                } else if (a.type == VAL_I64) {
                    REG(a_) = val_i64(a.as.as_i64 + 1);
                } else {
                    REG(a_) = val_i32((int32_t)(value_to_int64(a) + 1));
                }
                break;
            }

            case BC_DEC: {
                Value a = REG(a_);
                if (!is_number(a)) {
                    vm_runtime_error(vm, "Operand must be a number for --");
                    return VM_RUNTIME_ERROR;
                }
                if (a.type == VAL_F64) {
                    REG(a_) = val_f64(a.as.as_f64 - 1);
                } else if (a.type == VAL_I64) {
                    REG(a_) = val_i64(a.as.as_i64 - 1);
                } else {
                    REG(a_) = val_i32((int32_t)(value_to_int64(a) - 1));
                }
                break;
            }

            // String concatenation
            case BC_CONCAT: {
                Value a = REG(b_);
                Value b = REG(c);

                // TODO: Full implementation with type coercion
                if (a.type == VAL_STRING && b.type == VAL_STRING) {
                    String *result = string_concat(a.as.as_string, b.as.as_string);
                    REG(a_) = val_string(result->data);
                    string_free(result);
                } else {
                    vm_runtime_error(vm, "Can only concatenate strings");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            // Exception handling
            case BC_THROW: {
                vm->has_exception = true;
                vm->exception = REG(a_);

                // TODO: Unwind to nearest catch block
                vm_runtime_error(vm, "Unhandled exception");
                return VM_RUNTIME_ERROR;
            }

            case BC_TRY_BEGIN:
            case BC_TRY_END:
            case BC_CATCH:
                // TODO: Implement exception handling
                break;

            // Defer
            case BC_DEFER_PUSH:
                if (vm->defer_count >= VM_MAX_DEFERS) {
                    vm_runtime_error(vm, "Defer stack overflow");
                    return VM_RUNTIME_ERROR;
                }
                vm->defer_stack[vm->defer_count++] = REG(a_);
                break;

            case BC_DEFER_POP:
                if (vm->defer_count > 0) {
                    vm->defer_count--;
                    Value deferred = vm->defer_stack[vm->defer_count];
                    if (deferred.type == VAL_BUILTIN_FN) {
                        deferred.as.as_builtin_fn(NULL, 0, vm->exec_ctx);
                    }
                }
                break;

            case BC_DEFER_EXEC_ALL:
                while (vm->defer_count > 0) {
                    vm->defer_count--;
                    Value deferred = vm->defer_stack[vm->defer_count];
                    if (deferred.type == VAL_BUILTIN_FN) {
                        deferred.as.as_builtin_fn(NULL, 0, vm->exec_ctx);
                    }
                }
                break;

            // Misc
            case BC_NOP:
                break;

            case BC_PRINT:
                print_value(REG(a_));
                printf("\n");
                break;

            case BC_PANIC: {
                Value msg = REG(a_);
                if (msg.type == VAL_STRING) {
                    vm_runtime_error(vm, "panic: %s", msg.as.as_string->data);
                } else {
                    vm_runtime_error(vm, "panic");
                }
                return VM_RUNTIME_ERROR;
            }

            // Async (stubs for now)
            case BC_SPAWN:
            case BC_AWAIT:
            case BC_YIELD:
                vm_runtime_error(vm, "Async operations not yet implemented in VM");
                return VM_RUNTIME_ERROR;

            // Type operations
            case BC_TYPEOF: {
                Value a = REG(b_);
                const char *type_name;
                switch (a.type) {
                    case VAL_NULL: type_name = "null"; break;
                    case VAL_BOOL: type_name = "bool"; break;
                    case VAL_I8: type_name = "i8"; break;
                    case VAL_I16: type_name = "i16"; break;
                    case VAL_I32: type_name = "i32"; break;
                    case VAL_I64: type_name = "i64"; break;
                    case VAL_U8: type_name = "u8"; break;
                    case VAL_U16: type_name = "u16"; break;
                    case VAL_U32: type_name = "u32"; break;
                    case VAL_U64: type_name = "u64"; break;
                    case VAL_F32: type_name = "f32"; break;
                    case VAL_F64: type_name = "f64"; break;
                    case VAL_STRING: type_name = "string"; break;
                    case VAL_RUNE: type_name = "rune"; break;
                    case VAL_ARRAY: type_name = "array"; break;
                    case VAL_OBJECT: type_name = "object"; break;
                    case VAL_FUNCTION:
                    case VAL_BUILTIN_FN: type_name = "function"; break;
                    case VAL_PTR: type_name = "ptr"; break;
                    case VAL_BUFFER: type_name = "buffer"; break;
                    case VAL_TASK: type_name = "task"; break;
                    case VAL_CHANNEL: type_name = "channel"; break;
                    default: type_name = "unknown";
                }
                REG(a_) = val_string(type_name);
                break;
            }

            case BC_CAST:
            case BC_INSTANCEOF:
            case BC_GET_FIELD_CHAIN:
            case BC_IMPORT:
            case BC_EXPORT:
            case BC_TAILCALL:
            case BC_ASSERT:
            case BC_CALL_BUILTIN:
                // TODO: Implement
                break;

            default:
                vm_runtime_error(vm, "Unknown opcode %d", op);
                return VM_RUNTIME_ERROR;
        }
    }

    #undef READ_BYTE
    #undef READ_INSTRUCTION
    #undef READ_CONSTANT
    #undef REG
    #undef BINARY_OP
    #undef COMPARISON_OP
    #undef BITWISE_OP

    return VM_OK;
}

// ========== Debug Functions ==========

void vm_print_stack(VM *vm) {
    printf("Stack: [");
    for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
        print_value(*slot);
        if (slot + 1 < vm->stack_top) printf(", ");
    }
    printf("]\n");
}

void vm_print_globals(VM *vm) {
    printf("Globals:\n");
    for (int i = 0; i < vm->global_count; i++) {
        printf("  %s = ", vm->global_names[i]);
        print_value(vm->global_values[i]);
        printf("\n");
    }
}

// ========== Builtin Registration ==========

// External builtin declarations from interpreter/builtins
extern Value builtin_print(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_read_line(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_panic(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_free(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_open(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_join(Value *args, int num_args, ExecutionContext *ctx);
extern Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx);

void vm_register_all_builtins(VM *vm) {
    // Core I/O
    vm_register_builtin(vm, "print", builtin_print);
    vm_register_builtin(vm, "read_line", builtin_read_line);

    // Type inspection
    vm_register_builtin(vm, "typeof", builtin_typeof);
    vm_register_builtin(vm, "sizeof", builtin_sizeof);

    // Control
    vm_register_builtin(vm, "assert", builtin_assert);
    vm_register_builtin(vm, "panic", builtin_panic);

    // Memory
    vm_register_builtin(vm, "alloc", builtin_alloc);
    vm_register_builtin(vm, "free", builtin_free);
    vm_register_builtin(vm, "buffer", builtin_buffer);
    vm_register_builtin(vm, "memset", builtin_memset);
    vm_register_builtin(vm, "memcpy", builtin_memcpy);

    // File I/O
    vm_register_builtin(vm, "open", builtin_open);

    // Concurrency
    vm_register_builtin(vm, "spawn", builtin_spawn);
    vm_register_builtin(vm, "join", builtin_join);
    vm_register_builtin(vm, "channel", builtin_channel);

    // Note: More builtins can be registered as needed
    // The full list is in src/interpreter/builtins/
}

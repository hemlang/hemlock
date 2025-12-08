/*
 * Hemlock Bytecode VM
 *
 * Register-based virtual machine for executing Hemlock bytecode.
 * Uses computed gotos for fast dispatch on GCC/Clang.
 */

#ifndef HEMLOCK_VM_H
#define HEMLOCK_VM_H

#include "chunk.h"
#include "bytecode.h"
#include "../../include/interpreter.h"
#include <stdbool.h>

// Forward declarations
typedef struct VM VM;
typedef struct CallFrame CallFrame;
typedef struct VMUpvalue VMUpvalue;

// Maximum call stack depth
#define VM_MAX_FRAMES 256

// Maximum defer stack size
#define VM_MAX_DEFERS 64

// ========== Upvalue (Closed-over Variable) ==========

struct VMUpvalue {
    Value *location;        // Points to stack slot or closed value
    Value closed;           // Storage for closed-over value
    struct VMUpvalue *next; // Linked list for open upvalues
};

// ========== Call Frame ==========

struct CallFrame {
    Chunk *chunk;           // Bytecode chunk being executed
    uint32_t *ip;           // Instruction pointer within chunk
    Value *slots;           // Base of register window
    VMUpvalue **upvalues;   // Upvalues for this frame
    int num_upvalues;
};

// ========== VM State ==========

typedef enum {
    VM_OK,
    VM_COMPILE_ERROR,
    VM_RUNTIME_ERROR,
} VMResult;

struct VM {
    // Call stack
    CallFrame frames[VM_MAX_FRAMES];
    int frame_count;

    // Register/value stack
    Value *stack;
    Value *stack_top;
    int stack_capacity;

    // Global variables (hash table)
    char **global_names;
    Value *global_values;
    int global_count;
    int global_capacity;

    // Open upvalues (linked list)
    VMUpvalue *open_upvalues;

    // Defer stack
    Value defer_stack[VM_MAX_DEFERS];
    int defer_count;

    // Exception handling
    bool has_exception;
    Value exception;

    // Error info
    char *error_message;
    int error_line;

    // Built-in functions
    BuiltinFn *builtins;
    char **builtin_names;
    int builtin_count;

    // Execution context (for compatibility with interpreter builtins)
    ExecutionContext *exec_ctx;
};

// ========== VM API ==========

// Create/destroy VM
VM* vm_new(void);
void vm_free(VM *vm);

// Reset VM state
void vm_reset(VM *vm);

// Execute bytecode
VMResult vm_run(VM *vm, Chunk *chunk);

// Execute source code (compile + run)
VMResult vm_interpret(VM *vm, const char *source, const char *source_file);

// Global variable access
void vm_define_global(VM *vm, const char *name, Value value);
bool vm_get_global(VM *vm, const char *name, Value *value);
bool vm_set_global(VM *vm, const char *name, Value value);

// Builtin registration
void vm_register_builtin(VM *vm, const char *name, BuiltinFn fn);
void vm_register_all_builtins(VM *vm);

// Error handling
const char* vm_get_error(VM *vm);
void vm_runtime_error(VM *vm, const char *format, ...);

// Stack manipulation
void vm_push(VM *vm, Value value);
Value vm_pop(VM *vm);
Value vm_peek(VM *vm, int distance);

// Upvalue management
VMUpvalue* vm_capture_upvalue(VM *vm, Value *local);
void vm_close_upvalues(VM *vm, Value *last);

// Debug
void vm_print_stack(VM *vm);
void vm_print_globals(VM *vm);

#endif // HEMLOCK_VM_H

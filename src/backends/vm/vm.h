/*
 * Hemlock Bytecode VM - Virtual Machine
 *
 * Stack-based virtual machine for executing Hemlock bytecode.
 * Supports closures, async/await, channels, and full parity with interpreter.
 */

#ifndef HEMLOCK_VM_VM_H
#define HEMLOCK_VM_VM_H

#include <stdint.h>
#include <stdbool.h>
#include "chunk.h"
#include "instruction.h"

// Use the shared Value type from interpreter
#include "interpreter.h"

// ============================================
// Forward Declarations
// ============================================

typedef struct VM VM;
typedef struct CallFrame CallFrame;
typedef struct ObjUpvalue ObjUpvalue;
typedef struct DeferEntry DeferEntry;

// ============================================
// Call Frame
// ============================================

struct CallFrame {
    Chunk *chunk;           // Bytecode being executed
    uint8_t *ip;            // Instruction pointer
    Value *slots;           // First slot in VM stack for this frame
    ObjUpvalue *upvalues;   // Captured variables (for closures)
    int slot_count;         // Number of local slots
};

// ============================================
// VM Closure (compiled function + captured upvalues)
// ============================================

typedef struct VMClosure {
    Chunk *chunk;           // Compiled function bytecode
    ObjUpvalue **upvalues;  // Captured upvalue pointers
    int upvalue_count;      // Number of upvalues
    int ref_count;          // Reference count
} VMClosure;

// Create a new closure
VMClosure* vm_closure_new(Chunk *chunk);
void vm_closure_free(VMClosure *closure);

// ============================================
// Open Upvalue (for closures)
// ============================================

struct ObjUpvalue {
    Value *location;        // Points to stack slot while open
    Value closed;           // Holds value when closed
    struct ObjUpvalue *next;// Linked list of open upvalues
};

// ============================================
// Defer Entry
// ============================================

struct DeferEntry {
    Chunk *chunk;           // Deferred function chunk
    Value *args;            // Captured arguments
    int arg_count;
    CallFrame *frame;       // Frame that registered the defer
};

// ============================================
// VM State
// ============================================

typedef enum {
    VM_OK,
    VM_RUNTIME_ERROR,
    VM_COMPILE_ERROR,
} VMResult;

struct VM {
    // Value stack
    Value *stack;
    Value *stack_top;
    int stack_capacity;

    // Call frames
    CallFrame *frames;
    int frame_count;
    int frame_capacity;

    // Open upvalues (for closures)
    ObjUpvalue *open_upvalues;

    // Global variables
    struct {
        char **names;
        Value *values;
        bool *is_const;
        int count;
        int capacity;
        int *hash_table;
        int hash_capacity;
    } globals;

    // Control flow state
    bool is_returning;
    Value return_value;

    bool is_throwing;
    Value exception;
    CallFrame *exception_frame;

    bool is_breaking;
    bool is_continuing;

    // Defer stack
    DeferEntry *defers;
    int defer_count;
    int defer_capacity;

    // Module cache
    struct {
        char **paths;
        Value *modules;
        int count;
        int capacity;
    } module_cache;

    // Object tracking (for cleanup)
    void *objects;          // Linked list of allocated objects
    size_t bytes_allocated;
    size_t next_gc;

    // Stack limit
    int max_stack_depth;

    // Async task context (when running in spawned task)
    void *task;             // Task* when running async
};

// ============================================
// VM Lifecycle
// ============================================

VM* vm_new(void);
void vm_free(VM *vm);
void vm_reset(VM *vm);

// ============================================
// Execution
// ============================================

// Run a compiled chunk (main entry point)
VMResult vm_run(VM *vm, Chunk *chunk);

// Run a source file (compile + execute)
VMResult vm_run_file(VM *vm, const char *path);

// Run a source string (compile + execute)
VMResult vm_run_source(VM *vm, const char *source, const char *path);

// Call a function value with arguments
Value vm_call(VM *vm, Value callable, int argc, Value *args);

// ============================================
// Stack Operations
// ============================================

void vm_push(VM *vm, Value value);
Value vm_pop(VM *vm);
Value vm_peek(VM *vm, int distance);
void vm_popn(VM *vm, int count);

// ============================================
// Global Variables
// ============================================

void vm_define_global(VM *vm, const char *name, Value value, bool is_const);
bool vm_get_global(VM *vm, const char *name, Value *out);
bool vm_set_global(VM *vm, const char *name, Value value);

// ============================================
// Builtins
// ============================================

// VM-specific builtin function signature (different from interpreter's BuiltinFn)
typedef Value (*VMBuiltinFn)(VM *vm, int argc, Value *args);

typedef struct {
    const char *name;
    VMBuiltinFn fn;
    int min_arity;
    int max_arity;      // -1 for variadic
} BuiltinEntry;

// Get builtin by ID
const BuiltinEntry* vm_get_builtin(BuiltinId id);

// Register all builtins
void vm_register_builtins(VM *vm);

// ============================================
// Error Handling
// ============================================

// Report runtime error (sets is_throwing)
void vm_runtime_error(VM *vm, const char *format, ...);

// Get current line number
int vm_current_line(VM *vm);

// Print stack trace
void vm_print_stack_trace(VM *vm);

// ============================================
// Closures & Upvalues
// ============================================

ObjUpvalue* vm_capture_upvalue(VM *vm, Value *local);
void vm_close_upvalues(VM *vm, Value *last);

// ============================================
// Defer
// ============================================

void vm_push_defer(VM *vm, Chunk *chunk, Value *args, int arg_count);
void vm_execute_defers(VM *vm, CallFrame *until_frame);

// ============================================
// Modules
// ============================================

Value vm_import_module(VM *vm, const char *path);
Value vm_import_stdlib(VM *vm, const char *module_name);

// ============================================
// Async Support
// ============================================

// Create task for async function
Value vm_spawn_task(VM *vm, Value func, int argc, Value *args);

// Wait for task completion
Value vm_join_task(VM *vm, Value task);

// Create channel
Value vm_create_channel(VM *vm, int capacity);

// ============================================
// Debug
// ============================================

void vm_trace_execution(VM *vm, bool enable);
void vm_dump_stack(VM *vm);
void vm_dump_globals(VM *vm);

// ============================================
// Configuration
// ============================================

#define VM_STACK_INITIAL    256
#define VM_STACK_MAX        65536
#define VM_FRAMES_INITIAL   64
#define VM_FRAMES_MAX       1024
#define VM_GLOBALS_INITIAL  64
#define VM_DEFER_INITIAL    16

// ============================================
// Dispatch Loop Macros
// ============================================

// Computed goto dispatch (if supported)
#ifdef __GNUC__
#define VM_USE_COMPUTED_GOTO 1
#else
#define VM_USE_COMPUTED_GOTO 0
#endif

#if VM_USE_COMPUTED_GOTO
    #define DISPATCH()      goto *dispatch_table[READ_BYTE(ip)]
    #define CASE(op)        case_##op:
    #define NEXT()          DISPATCH()
#else
    #define DISPATCH()      switch (READ_BYTE(ip))
    #define CASE(op)        case op:
    #define NEXT()          break
#endif

// Stack manipulation macros
#define PUSH(v)         (*vm->stack_top++ = (v))
#define POP()           (*--vm->stack_top)
#define PEEK(n)         (vm->stack_top[-1 - (n)])
#define DROP(n)         (vm->stack_top -= (n))

// Binary operation macro with type promotion
#define BINARY_OP(op, result_type) do { \
    Value b = POP(); \
    Value a = POP(); \
    PUSH(value_binary_##op(a, b)); \
} while (0)

// Fast path i32 binary operation
#define BINARY_OP_I32(op) do { \
    Value b = POP(); \
    Value a = POP(); \
    PUSH(VALUE_I32(a.as.as_i32 op b.as.as_i32)); \
} while (0)

#endif // HEMLOCK_VM_VM_H

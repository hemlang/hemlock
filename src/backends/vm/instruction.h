/*
 * Hemlock Bytecode VM - Instruction Set
 *
 * 82 opcodes organized into 11 categories.
 * Each opcode is 1 byte, with 0-3 bytes of operands.
 *
 * Prefix: BC_ (bytecode) to avoid conflicts with AST BinaryOp enum
 */

#ifndef HEMLOCK_VM_INSTRUCTION_H
#define HEMLOCK_VM_INSTRUCTION_H

#include <stdint.h>
#include <stddef.h>

// Builtin function IDs (for BC_CALL_BUILTIN)
typedef enum {
    // Memory (0-10)
    BUILTIN_ALLOC = 0,
    BUILTIN_TALLOC,
    BUILTIN_REALLOC,
    BUILTIN_FREE,
    BUILTIN_MEMSET,
    BUILTIN_MEMCPY,
    BUILTIN_SIZEOF,
    BUILTIN_BUFFER,
    BUILTIN_PTR_TO_BUFFER,
    BUILTIN_BUFFER_PTR,
    BUILTIN_PTR_NULL,

    // I/O (11-14)
    BUILTIN_PRINT,
    BUILTIN_EPRINT,
    BUILTIN_READ_LINE,
    BUILTIN_OPEN,

    // Type (15-17)
    BUILTIN_TYPEOF,
    BUILTIN_ASSERT,
    BUILTIN_PANIC,

    // Async (18-24)
    BUILTIN_SPAWN,
    BUILTIN_JOIN,
    BUILTIN_DETACH,
    BUILTIN_CHANNEL,
    BUILTIN_SELECT,
    BUILTIN_TASK_DEBUG_INFO,
    BUILTIN_APPLY,

    // Signal (25-26)
    BUILTIN_SIGNAL,
    BUILTIN_RAISE,

    // Exec (27-28)
    BUILTIN_EXEC,
    BUILTIN_EXEC_ARGV,

    // Pointer read (29-41)
    BUILTIN_PTR_READ_I8,
    BUILTIN_PTR_READ_I16,
    BUILTIN_PTR_READ_I32,
    BUILTIN_PTR_READ_I64,
    BUILTIN_PTR_READ_U8,
    BUILTIN_PTR_READ_U16,
    BUILTIN_PTR_READ_U32,
    BUILTIN_PTR_READ_U64,
    BUILTIN_PTR_READ_F32,
    BUILTIN_PTR_READ_F64,
    BUILTIN_PTR_READ_PTR,
    BUILTIN_PTR_OFFSET,
    BUILTIN_PTR_DEREF_I32,

    // Pointer write (42-52)
    BUILTIN_PTR_WRITE_I8,
    BUILTIN_PTR_WRITE_I16,
    BUILTIN_PTR_WRITE_I32,
    BUILTIN_PTR_WRITE_I64,
    BUILTIN_PTR_WRITE_U8,
    BUILTIN_PTR_WRITE_U16,
    BUILTIN_PTR_WRITE_U32,
    BUILTIN_PTR_WRITE_U64,
    BUILTIN_PTR_WRITE_F32,
    BUILTIN_PTR_WRITE_F64,
    BUILTIN_PTR_WRITE_PTR,

    // Atomics i32 (53-61)
    BUILTIN_ATOMIC_LOAD_I32,
    BUILTIN_ATOMIC_STORE_I32,
    BUILTIN_ATOMIC_ADD_I32,
    BUILTIN_ATOMIC_SUB_I32,
    BUILTIN_ATOMIC_AND_I32,
    BUILTIN_ATOMIC_OR_I32,
    BUILTIN_ATOMIC_XOR_I32,
    BUILTIN_ATOMIC_CAS_I32,
    BUILTIN_ATOMIC_EXCHANGE_I32,

    // Atomics i64 (62-70)
    BUILTIN_ATOMIC_LOAD_I64,
    BUILTIN_ATOMIC_STORE_I64,
    BUILTIN_ATOMIC_ADD_I64,
    BUILTIN_ATOMIC_SUB_I64,
    BUILTIN_ATOMIC_AND_I64,
    BUILTIN_ATOMIC_OR_I64,
    BUILTIN_ATOMIC_XOR_I64,
    BUILTIN_ATOMIC_CAS_I64,
    BUILTIN_ATOMIC_EXCHANGE_I64,

    // Atomics misc (71)
    BUILTIN_ATOMIC_FENCE,

    // Callback (72-73)
    BUILTIN_CALLBACK,
    BUILTIN_CALLBACK_FREE,

    // Stack (74-75)
    BUILTIN_SET_STACK_LIMIT,
    BUILTIN_GET_STACK_LIMIT,

    // DNS/Network (76-78)
    BUILTIN_DNS_RESOLVE,
    BUILTIN_SOCKET_CREATE,
    BUILTIN_POLL,

    // Math (79-80)
    BUILTIN_DIVI,       // Integer division (floor)
    BUILTIN_MODI,       // Integer modulo

    BUILTIN_COUNT  // Total number of builtins
} BuiltinId;

// Type IDs for BC_CAST and BC_CHECK_TYPE
typedef enum {
    TYPE_ID_I8 = 0,
    TYPE_ID_I16,
    TYPE_ID_I32,
    TYPE_ID_I64,
    TYPE_ID_U8,
    TYPE_ID_U16,
    TYPE_ID_U32,
    TYPE_ID_U64,
    TYPE_ID_F32,
    TYPE_ID_F64,
    TYPE_ID_BOOL,
    TYPE_ID_STRING,
    TYPE_ID_RUNE,
    TYPE_ID_ARRAY,
    TYPE_ID_OBJECT,
    TYPE_ID_PTR,
    TYPE_ID_BUFFER,
    TYPE_ID_NULL,
    TYPE_ID_FUNCTION,
    TYPE_ID_TASK,
    TYPE_ID_CHANNEL,
    TYPE_ID_FILE,
    TYPE_ID_ENUM,
} TypeId;

// Bytecode opcodes (BC_ prefix to avoid conflicts with AST enums)
typedef enum {
    // ========================================
    // Category 1: Constants & Literals (0x00-0x0F)
    // ========================================
    BC_CONST            = 0x00,  // [idx:16] Push constant from pool
    BC_CONST_BYTE       = 0x01,  // [val:8]  Push small integer (0-255)
    BC_NULL             = 0x02,  // []       Push null
    BC_TRUE             = 0x03,  // []       Push true
    BC_FALSE            = 0x04,  // []       Push false
    BC_ARRAY            = 0x05,  // [cnt:16] Pop n elements, push array
    BC_OBJECT           = 0x06,  // [cnt:16] Pop n key-value pairs, push object
    BC_STRING_INTERP    = 0x07,  // [cnt:16] Interpolate n parts into string
    BC_CLOSURE          = 0x08,  // [idx:16][upvals:8] Create closure
    BC_ENUM_VALUE       = 0x09,  // [idx:16] Push enum variant value

    // ========================================
    // Category 2: Variables (0x10-0x1F)
    // ========================================
    BC_GET_LOCAL        = 0x10,  // [slot:8]  Load local variable
    BC_SET_LOCAL        = 0x11,  // [slot:8]  Store to local
    BC_GET_UPVALUE      = 0x12,  // [slot:8]  Load captured variable
    BC_SET_UPVALUE      = 0x13,  // [slot:8]  Store to captured variable
    BC_GET_GLOBAL       = 0x14,  // [idx:16]  Load global by name
    BC_SET_GLOBAL       = 0x15,  // [idx:16]  Store to global
    BC_DEFINE_GLOBAL    = 0x16,  // [idx:16]  Define new global
    BC_GET_PROPERTY     = 0x17,  // [idx:16]  Get object property
    BC_SET_PROPERTY     = 0x18,  // [idx:16]  Set object property
    BC_GET_INDEX        = 0x19,  // []        array[index] or object[key]
    BC_SET_INDEX        = 0x1A,  // []        array[index] = value
    BC_CLOSE_UPVALUE    = 0x1B,  // []        Close upvalue on stack top

    // ========================================
    // Category 3: Arithmetic (0x20-0x2F)
    // ========================================
    BC_ADD              = 0x20,  // []  a + b (type promotion, string concat)
    BC_SUB              = 0x21,  // []  a - b
    BC_MUL              = 0x22,  // []  a * b
    BC_DIV              = 0x23,  // []  a / b (always returns f64)
    BC_MOD              = 0x24,  // []  a % b
    BC_NEGATE           = 0x25,  // []  -a
    BC_INC              = 0x26,  // []  ++a (in-place)
    BC_DEC              = 0x27,  // []  --a (in-place)
    BC_ADD_I32          = 0x28,  // []  Fast path: i32 + i32
    BC_SUB_I32          = 0x29,  // []  Fast path: i32 - i32
    BC_MUL_I32          = 0x2A,  // []  Fast path: i32 * i32

    // ========================================
    // Category 4: Comparison (0x30-0x3F)
    // ========================================
    BC_EQ               = 0x30,  // []  a == b
    BC_NE               = 0x31,  // []  a != b
    BC_LT               = 0x32,  // []  a < b
    BC_LE               = 0x33,  // []  a <= b
    BC_GT               = 0x34,  // []  a > b
    BC_GE               = 0x35,  // []  a >= b
    BC_EQ_I32           = 0x36,  // []  Fast path: i32 == i32
    BC_LT_I32           = 0x37,  // []  Fast path: i32 < i32

    // ========================================
    // Category 5: Logical & Bitwise (0x40-0x4F)
    // ========================================
    BC_NOT              = 0x40,  // []        !a
    BC_BIT_NOT          = 0x41,  // []        ~a
    BC_BIT_AND          = 0x42,  // []        a & b
    BC_BIT_OR           = 0x43,  // []        a | b
    BC_BIT_XOR          = 0x44,  // []        a ^ b
    BC_LSHIFT           = 0x45,  // []        a << b
    BC_RSHIFT           = 0x46,  // []        a >> b
    BC_COALESCE         = 0x47,  // [off:16]  a ?? b (short-circuit)
    BC_OPTIONAL_CHAIN   = 0x48,  // [off:16]  a?.b (null short-circuit)

    // ========================================
    // Category 6: Control Flow (0x50-0x5F)
    // ========================================
    BC_JUMP             = 0x50,  // [off:16]  Unconditional jump
    BC_JUMP_IF_FALSE    = 0x51,  // [off:16]  Jump if top is falsy
    BC_JUMP_IF_TRUE     = 0x52,  // [off:16]  Jump if top is truthy
    BC_JUMP_IF_FALSE_POP= 0x53,  // [off:16]  Jump if false, always pop
    BC_LOOP             = 0x54,  // [off:16]  Jump backward (loop)
    BC_BREAK            = 0x55,  // []        Break from loop
    BC_CONTINUE         = 0x56,  // []        Continue to next iteration
    BC_SWITCH           = 0x57,  // [cnt:16]  Jump table dispatch
    BC_CASE             = 0x58,  // [off:16]  Case label marker
    BC_FOR_IN_INIT      = 0x59,  // []        Initialize for-in iterator
    BC_FOR_IN_NEXT      = 0x5A,  // [off:16]  Get next or jump to end
    BC_POP              = 0x5B,  // []        Discard top of stack
    BC_POPN             = 0x5C,  // [n:8]     Discard n values from stack
    BC_DUP              = 0x5D,  // []        Duplicate top of stack
    BC_DUP2             = 0x5E,  // []        Duplicate top two stack values
    BC_SWAP             = 0x5F,  // []        Swap top two stack values

    // ========================================
    // Category 7: Functions & Calls (0x60-0x6F)
    // ========================================
    BC_CALL             = 0x60,  // [argc:8]        Call function
    BC_CALL_METHOD      = 0x61,  // [idx:16][argc:8] Call method on object
    BC_CALL_BUILTIN     = 0x62,  // [id:16][argc:8]  Call builtin function
    BC_RETURN           = 0x63,  // []               Return from function
    BC_APPLY            = 0x64,  // []               apply(fn, args_array)
    BC_TAIL_CALL        = 0x65,  // [argc:8]         Tail call optimization
    BC_SUPER            = 0x66,  // [idx:16]         Access super method
    BC_INVOKE           = 0x67,  // [idx:16][argc:8] Optimized method call

    // ========================================
    // Category 8: Exception Handling (0x70-0x7F)
    // ========================================
    BC_TRY              = 0x70,  // [catch:16][finally:16] Begin try block
    BC_CATCH            = 0x71,  // []        Begin catch (push exception)
    BC_FINALLY          = 0x72,  // []        Begin finally block
    BC_END_TRY          = 0x73,  // []        End try-catch-finally
    BC_THROW            = 0x74,  // []        Throw exception
    BC_DEFER            = 0x75,  // [idx:16]  Register deferred call

    // ========================================
    // Category 9: Async & Concurrency (0x80-0x8F)
    // ========================================
    BC_SPAWN            = 0x80,  // [argc:8]  Spawn async task
    BC_AWAIT            = 0x81,  // []        Await task result
    BC_JOIN             = 0x82,  // []        Join task (explicit)
    BC_DETACH           = 0x83,  // []        Detach task
    BC_CHANNEL          = 0x84,  // []        Create channel (capacity on stack)
    BC_SEND             = 0x85,  // []        Send on channel
    BC_RECV             = 0x86,  // []        Receive from channel
    BC_SELECT           = 0x87,  // []        Select on multiple channels

    // ========================================
    // Category 10: Type Operations (0x90-0x9F)
    // ========================================
    BC_TYPEOF           = 0x90,  // []        Get type string
    BC_CAST             = 0x91,  // [type:8]  Explicit type cast
    BC_CHECK_TYPE       = 0x92,  // [type:8]  Runtime type check
    BC_DEFINE_TYPE      = 0x93,  // [idx:16]  Register type definition
    BC_DEFINE_ENUM      = 0x94,  // [idx:16]  Register enum definition

    // ========================================
    // Category 11: Debug & Misc (0xF0-0xFF)
    // ========================================
    BC_NOP              = 0xF0,  // []        No operation
    BC_PRINT            = 0xF1,  // [argc:8]  Print values
    BC_ASSERT           = 0xF2,  // []        Assert with optional message
    BC_BURY3            = 0xF3,  // []        [a,b,c,d] -> [c,a,b,d] (move 2nd under 4th)
    BC_ROT3             = 0xF4,  // []        [a,b,c] -> [b,c,a] (rotate 3, bottom to top)
    BC_DEBUG_BREAK      = 0xFE,  // []        Debugger breakpoint
    BC_HALT             = 0xFF,  // []        Stop execution

} OpCode;

// Instruction metadata for disassembly and verification
typedef struct {
    const char *name;       // Human-readable name
    int operand_bytes;      // Number of operand bytes (0, 1, 2, 3, 4)
    int stack_effect;       // Net stack change (-128 = variable)
} InstructionInfo;

// Get instruction info by opcode
const InstructionInfo* instruction_info(OpCode op);

// Get instruction size in bytes
int instruction_size(OpCode op);

// Get builtin function name
const char* builtin_name(BuiltinId id);

// Lookup builtin ID by name (-1 if not found)
BuiltinId builtin_lookup(const char *name);

// Get type name by ID
const char* type_id_name(TypeId id);

// Operand encoding helpers
#define READ_BYTE(ip)    (*(ip)++)
#define READ_SHORT(ip)   ((ip) += 2, (uint16_t)((ip)[-2] << 8 | (ip)[-1]))
#define READ_SIGNED_SHORT(ip) ((int16_t)READ_SHORT(ip))

// Instruction size helpers
#define INSTR_SIZE_0  1  // opcode only
#define INSTR_SIZE_1  2  // opcode + 1 byte operand
#define INSTR_SIZE_2  3  // opcode + 2 byte operand
#define INSTR_SIZE_3  4  // opcode + 3 byte operand
#define INSTR_SIZE_4  5  // opcode + 4 byte operand

// Stack effect constants
#define STACK_EFFECT_VARIABLE -128

#endif // HEMLOCK_VM_INSTRUCTION_H

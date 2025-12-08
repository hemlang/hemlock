/*
 * Hemlock Bytecode VM - Opcode Definitions
 *
 * Register-based VM with 32-bit fixed-width instructions.
 *
 * Instruction formats:
 *   [opcode:8][A:8][B:8][C:8]     - 3-register operations (ABC)
 *   [opcode:8][A:8][Bx:16]        - Load/store + 16-bit unsigned (ABx)
 *   [opcode:8][A:8][sBx:16]       - Signed offset for jumps (AsBx)
 *   [opcode:8][Ax:24]             - 24-bit unsigned operand (Ax)
 *   [opcode:8][sAx:24]            - 24-bit signed operand (sAx)
 */

#ifndef HEMLOCK_BYTECODE_H
#define HEMLOCK_BYTECODE_H

#include <stdint.h>

// Bytecode file magic number and version
#define HBC_MAGIC       0x48424300  // "HBC\0"
#define HBC_VERSION     1

// Maximum values
#define MAX_REGISTERS   256
#define MAX_CONSTANTS   65536       // 16-bit constant index
#define MAX_UPVALUES    256
#define MAX_LOCALS      256

// Opcode definitions
typedef enum {
    // ========== Load/Store Operations ==========
    OP_LOAD_CONST,      // A Bx    : R(A) = K(Bx)         -- Load constant
    OP_LOAD_NULL,       // A       : R(A) = null
    OP_LOAD_TRUE,       // A       : R(A) = true
    OP_LOAD_FALSE,      // A       : R(A) = false

    OP_MOVE,            // A B     : R(A) = R(B)          -- Copy register

    OP_LOAD_LOCAL,      // A Bx    : R(A) = locals[Bx]    -- Load local variable
    OP_STORE_LOCAL,     // A Bx    : locals[Bx] = R(A)    -- Store local variable

    OP_LOAD_UPVALUE,    // A Bx    : R(A) = upvalues[Bx]  -- Load captured variable
    OP_STORE_UPVALUE,   // A Bx    : upvalues[Bx] = R(A)  -- Store captured variable

    OP_LOAD_GLOBAL,     // A Bx    : R(A) = globals[K(Bx)] -- Load global by name
    OP_STORE_GLOBAL,    // A Bx    : globals[K(Bx)] = R(A) -- Store global by name

    // ========== Arithmetic Operations ==========
    OP_ADD,             // A B C   : R(A) = R(B) + R(C)
    OP_SUB,             // A B C   : R(A) = R(B) - R(C)
    OP_MUL,             // A B C   : R(A) = R(B) * R(C)
    OP_DIV,             // A B C   : R(A) = R(B) / R(C)
    OP_MOD,             // A B C   : R(A) = R(B) % R(C)
    OP_POW,             // A B C   : R(A) = R(B) ** R(C)
    OP_NEG,             // A B     : R(A) = -R(B)

    // ========== Bitwise Operations ==========
    OP_BAND,            // A B C   : R(A) = R(B) & R(C)
    OP_BOR,             // A B C   : R(A) = R(B) | R(C)
    OP_BXOR,            // A B C   : R(A) = R(B) ^ R(C)
    OP_BNOT,            // A B     : R(A) = ~R(B)
    OP_SHL,             // A B C   : R(A) = R(B) << R(C)
    OP_SHR,             // A B C   : R(A) = R(B) >> R(C)

    // ========== Comparison Operations ==========
    OP_EQ,              // A B C   : R(A) = R(B) == R(C)
    OP_NE,              // A B C   : R(A) = R(B) != R(C)
    OP_LT,              // A B C   : R(A) = R(B) < R(C)
    OP_LE,              // A B C   : R(A) = R(B) <= R(C)
    OP_GT,              // A B C   : R(A) = R(B) > R(C)
    OP_GE,              // A B C   : R(A) = R(B) >= R(C)

    // ========== Logical Operations ==========
    OP_NOT,             // A B     : R(A) = !R(B)

    // ========== Control Flow ==========
    OP_JMP,             // sAx     : pc += sAx            -- Unconditional jump
    OP_JMP_IF_FALSE,    // A sBx   : if !R(A) then pc += sBx
    OP_JMP_IF_TRUE,     // A sBx   : if R(A) then pc += sBx
    OP_LOOP,            // sAx     : pc -= sAx            -- Loop back (for optimization hints)

    // ========== Function Operations ==========
    OP_CALL,            // A B C   : R(A..A+C-1) = R(A)(R(A+1)..R(A+B))
                        //           A = base, B = num args, C = num results
    OP_RETURN,          // A B     : return R(A)..R(A+B-1)  -- B=0 means no return value
    OP_CLOSURE,         // A Bx    : R(A) = closure(proto[Bx])
    OP_TAILCALL,        // A B     : return R(A)(R(A+1)..R(A+B))  -- Tail call optimization

    // ========== Object/Array Operations ==========
    OP_NEW_ARRAY,       // A B     : R(A) = new array with R(A+1)..R(A+B) elements
    OP_NEW_OBJECT,      // A B     : R(A) = new object with B key-value pairs from stack

    OP_GET_INDEX,       // A B C   : R(A) = R(B)[R(C)]    -- Array/object index access
    OP_SET_INDEX,       // A B C   : R(A)[R(B)] = R(C)    -- Array/object index set

    OP_GET_FIELD,       // A B C   : R(A) = R(B).K(C)     -- Object field access (K = constant string)
    OP_SET_FIELD,       // A B C   : R(A).K(B) = R(C)     -- Object field set

    OP_GET_FIELD_CHAIN, // A B C   : R(A) = R(B)?.K(C)    -- Optional chaining

    // ========== Type Operations ==========
    OP_TYPEOF,          // A B     : R(A) = typeof(R(B))
    OP_CAST,            // A B C   : R(A) = cast<K(C)>(R(B))  -- Type cast
    OP_INSTANCEOF,      // A B C   : R(A) = R(B) instanceof K(C)

    // ========== Async Operations ==========
    OP_SPAWN,           // A B C   : R(A) = spawn R(B)(R(B+1)..R(B+C))
    OP_AWAIT,           // A B     : R(A) = await R(B)
    OP_YIELD,           // A       : yield R(A)           -- Future: generators

    // ========== Exception Handling ==========
    OP_THROW,           // A       : throw R(A)
    OP_TRY_BEGIN,       // A sBx   : Begin try block, A = catch target reg, sBx = catch offset
    OP_TRY_END,         // (none)  : End try block
    OP_CATCH,           // A       : R(A) = caught exception

    // ========== Defer ==========
    OP_DEFER_PUSH,      // A       : Push R(A) onto defer stack (callable)
    OP_DEFER_POP,       // (none)  : Pop and execute top of defer stack
    OP_DEFER_EXEC_ALL,  // (none)  : Execute all deferred calls (on return/throw)

    // ========== Increment/Decrement ==========
    OP_INC,             // A       : R(A) = R(A) + 1
    OP_DEC,             // A       : R(A) = R(A) - 1

    // ========== String Operations ==========
    OP_CONCAT,          // A B C   : R(A) = R(B) .. R(C)  -- String concatenation

    // ========== Miscellaneous ==========
    OP_NOP,             // (none)  : No operation
    OP_PANIC,           // A       : panic(R(A))          -- Unrecoverable error
    OP_ASSERT,          // A B     : if !R(A) panic(R(B)) -- Debug assertion
    OP_PRINT,           // A       : print R(A)           -- Debug print

    // ========== Module Operations ==========
    OP_IMPORT,          // A Bx    : R(A) = import K(Bx)  -- Import module
    OP_EXPORT,          // A Bx    : export R(A) as K(Bx)

    // ========== Builtin Call ==========
    OP_CALL_BUILTIN,    // A B C   : R(A) = builtin[B](R(A+1)..R(A+C))

    OP_COUNT            // Number of opcodes
} Opcode;

// Instruction encoding/decoding macros

// Create instructions
#define ENCODE_ABC(op, a, b, c)   (((uint32_t)(op)) | ((uint32_t)(a) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 24))
#define ENCODE_ABx(op, a, bx)     (((uint32_t)(op)) | ((uint32_t)(a) << 8) | ((uint32_t)(bx) << 16))
#define ENCODE_AsBx(op, a, sbx)   (((uint32_t)(op)) | ((uint32_t)(a) << 8) | ((uint32_t)((sbx) + 32768) << 16))
#define ENCODE_Ax(op, ax)         (((uint32_t)(op)) | ((uint32_t)(ax) << 8))
#define ENCODE_sAx(op, sax)       (((uint32_t)(op)) | ((uint32_t)((sax) + 8388608) << 8))

// Decode instructions
#define DECODE_OP(instr)          ((Opcode)((instr) & 0xFF))
#define DECODE_A(instr)           (((instr) >> 8) & 0xFF)
#define DECODE_B(instr)           (((instr) >> 16) & 0xFF)
#define DECODE_C(instr)           (((instr) >> 24) & 0xFF)
#define DECODE_Bx(instr)          (((instr) >> 16) & 0xFFFF)
#define DECODE_sBx(instr)         ((int32_t)(((instr) >> 16) & 0xFFFF) - 32768)
#define DECODE_Ax(instr)          (((instr) >> 8) & 0xFFFFFF)
#define DECODE_sAx(instr)         ((int32_t)(((instr) >> 8) & 0xFFFFFF) - 8388608)

// Opcode names for debugging
extern const char *opcode_names[];

// Get opcode name
const char* opcode_name(Opcode op);

// Get instruction format type for an opcode
typedef enum {
    FMT_ABC,    // A B C format
    FMT_AB,     // A B format (C unused)
    FMT_A,      // A format only
    FMT_ABx,    // A Bx format
    FMT_AsBx,   // A sBx format
    FMT_Ax,     // Ax format
    FMT_sAx,    // sAx format
    FMT_NONE,   // No operands
} InstrFormat;

InstrFormat opcode_format(Opcode op);

#endif // HEMLOCK_BYTECODE_H

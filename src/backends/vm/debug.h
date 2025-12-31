/*
 * Hemlock Bytecode VM - Debug Utilities
 *
 * Disassembler and debugging tools.
 */

#ifndef HEMLOCK_VM_DEBUG_H
#define HEMLOCK_VM_DEBUG_H

#include "chunk.h"
#include "interpreter.h"  // For Value type

// Disassemble an entire chunk
void disassemble_chunk(Chunk *chunk, const char *name);

// Disassemble a single instruction, returns next instruction offset
int disassemble_instruction(Chunk *chunk, int offset);

// Print constant value
void print_constant(Constant *constant);

// Print value (for stack traces)
void print_value(Value value);

#endif // HEMLOCK_VM_DEBUG_H

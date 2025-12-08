/*
 * Hemlock Bytecode VM - Public API
 *
 * This header provides the public interface for integrating the bytecode VM
 * with the Hemlock CLI and other components.
 *
 * Usage:
 *   // Compile AST to bytecode
 *   Chunk *chunk = vm_compile_ast(statements, count, "source.hml");
 *
 *   // Create VM and run
 *   VM *vm = vm_new();
 *   vm_register_all_builtins(vm);
 *   VMResult result = vm_run(vm, chunk);
 *
 *   // Save/load bytecode
 *   chunk_write_to_file(chunk, "output.hbc");
 *   Chunk *loaded = chunk_read_from_file("output.hbc");
 *
 *   // Cleanup
 *   chunk_free(chunk);
 *   vm_free(vm);
 *
 * CLI Integration:
 *   hemlock --vm file.hml         # Compile to bytecode and run
 *   hemlock --bc file.hml -o f.hbc  # Compile to bytecode file
 *   hemlock file.hbc               # Run bytecode file
 *   hemlock --disasm file.hbc      # Disassemble bytecode
 */

#ifndef HEMLOCK_VM_API_H
#define HEMLOCK_VM_API_H

#include "vm.h"
#include "chunk.h"
#include "compiler.h"
#include "bytecode.h"
#include "../../include/ast.h"

// ========== Compilation ==========

// Compile AST statements to bytecode
// Returns NULL on error
Chunk* vm_compile_ast(Stmt **statements, int count, const char *source_file);

// ========== Execution ==========

// Execute a bytecode chunk
VMResult vm_execute(VM *vm, Chunk *chunk);

// Execute with tracing (prints disassembly and state)
void vm_trace_execution(VM *vm, Chunk *chunk, bool trace);

// ========== Serialization ==========

// Write bytecode to file (.hbc format)
bool chunk_write_to_file(Chunk *chunk, const char *path);

// Read bytecode from file
Chunk* chunk_read_from_file(const char *path);

// ========== Convenience Functions ==========

// Run a Hemlock source file using the VM
// Returns 0 on success, non-zero on error
static inline int vm_run_file(const char *path) {
    // This would need full integration with lexer/parser
    // Placeholder for now
    (void)path;
    return -1;
}

// Run a bytecode file
// Returns 0 on success, non-zero on error
static inline int vm_run_bytecode_file(const char *path) {
    Chunk *chunk = chunk_read_from_file(path);
    if (!chunk) return 1;

    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    VMResult result = vm_run(vm, chunk);

    vm_free(vm);
    chunk_free(chunk);

    return (result == VM_OK) ? 0 : 1;
}

#endif // HEMLOCK_VM_API_H

/*
 * Hemlock Bytecode VM - Debug Utilities
 */

#include "vm.h"
#include "chunk.h"
#include "compiler.h"
#include "bytecode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== VM Interpretation (Compile + Run) ==========

// External declarations from lexer/parser
extern void lexer_init(const char *source);
extern int yyparse(void);
extern void *parsed_statements;
extern int parsed_statement_count;

VMResult vm_interpret(VM *vm, const char *source, const char *source_file) {
    // This would need the lexer/parser integrated
    // For now, return error indicating direct interpretation isn't ready
    (void)vm;
    (void)source;
    (void)source_file;

    fprintf(stderr, "vm_interpret: Direct source interpretation not yet implemented.\n");
    fprintf(stderr, "Use the AST compiler path instead.\n");
    return VM_COMPILE_ERROR;
}

// ========== Compile AST to Bytecode ==========

Chunk* vm_compile_ast(Stmt **statements, int count, const char *source_file) {
    Compiler *compiler = compiler_new(source_file);
    Chunk *chunk = compile_program(compiler, statements, count);

    if (compiler_had_error(compiler)) {
        fprintf(stderr, "Compilation error: %s\n", compiler_get_error(compiler));
        compiler_free(compiler);
        return NULL;
    }

    compiler_free(compiler);
    return chunk;
}

// ========== Run Bytecode ==========

VMResult vm_execute(VM *vm, Chunk *chunk) {
    return vm_run(vm, chunk);
}

// ========== Debug Trace Execution ==========

void vm_trace_execution(VM *vm, Chunk *chunk, bool trace) {
    (void)trace;  // TODO: Add tracing flag to vm_run

    printf("=== Executing bytecode ===\n");
    chunk_disassemble(chunk, "main");
    printf("\n");

    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("\n=== Execution completed successfully ===\n");
    } else {
        printf("\n=== Execution failed ===\n");
        printf("Error: %s\n", vm_get_error(vm));
    }

    printf("\nFinal state:\n");
    vm_print_globals(vm);
}

// ========== Bytecode Serialization Stubs ==========

bool chunk_write_to_file(Chunk *chunk, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open file for writing: %s\n", path);
        return false;
    }

    // Write magic and version
    uint32_t magic = HBC_MAGIC;
    uint16_t version = HBC_VERSION;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);

    // Write chunk name
    uint16_t name_len = chunk->name ? strlen(chunk->name) : 0;
    fwrite(&name_len, sizeof(name_len), 1, f);
    if (name_len > 0) {
        fwrite(chunk->name, 1, name_len, f);
    }

    // Write function metadata
    fwrite(&chunk->arity, sizeof(chunk->arity), 1, f);
    fwrite(&chunk->is_async, sizeof(chunk->is_async), 1, f);
    fwrite(&chunk->max_stack_size, sizeof(chunk->max_stack_size), 1, f);

    // Write constants
    fwrite(&chunk->constants.count, sizeof(chunk->constants.count), 1, f);
    for (int i = 0; i < chunk->constants.count; i++) {
        Constant *c = &chunk->constants.values[i];
        fwrite(&c->type, sizeof(c->type), 1, f);

        switch (c->type) {
            case CONST_NULL:
                break;
            case CONST_BOOL:
                fwrite(&c->as.as_bool, sizeof(c->as.as_bool), 1, f);
                break;
            case CONST_I32:
                fwrite(&c->as.as_i32, sizeof(c->as.as_i32), 1, f);
                break;
            case CONST_I64:
                fwrite(&c->as.as_i64, sizeof(c->as.as_i64), 1, f);
                break;
            case CONST_F64:
                fwrite(&c->as.as_f64, sizeof(c->as.as_f64), 1, f);
                break;
            case CONST_RUNE:
                fwrite(&c->as.as_rune, sizeof(c->as.as_rune), 1, f);
                break;
            case CONST_STRING: {
                int32_t len = c->as.as_string.length;
                fwrite(&len, sizeof(len), 1, f);
                fwrite(c->as.as_string.data, 1, len, f);
                break;
            }
        }
    }

    // Write bytecode
    fwrite(&chunk->code_count, sizeof(chunk->code_count), 1, f);
    fwrite(chunk->code, sizeof(uint32_t), chunk->code_count, f);

    // Write line info
    fwrite(&chunk->line_count, sizeof(chunk->line_count), 1, f);
    for (int i = 0; i < chunk->line_count; i++) {
        fwrite(&chunk->lines[i].pc, sizeof(int), 1, f);
        fwrite(&chunk->lines[i].line, sizeof(int), 1, f);
    }

    // Write upvalues
    fwrite(&chunk->upvalue_count, sizeof(chunk->upvalue_count), 1, f);
    for (int i = 0; i < chunk->upvalue_count; i++) {
        fwrite(&chunk->upvalues[i].index, sizeof(uint8_t), 1, f);
        fwrite(&chunk->upvalues[i].is_local, sizeof(bool), 1, f);
    }

    // Write nested protos count (recursive write would go here)
    fwrite(&chunk->proto_count, sizeof(chunk->proto_count), 1, f);
    // TODO: Recursively write nested protos

    fclose(f);
    return true;
}

Chunk* chunk_read_from_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open bytecode file: %s\n", path);
        return NULL;
    }

    // Read and verify magic
    uint32_t magic;
    fread(&magic, sizeof(magic), 1, f);
    if (magic != HBC_MAGIC) {
        fprintf(stderr, "Invalid bytecode file (wrong magic): %s\n", path);
        fclose(f);
        return NULL;
    }

    // Read and check version
    uint16_t version;
    fread(&version, sizeof(version), 1, f);
    if (version != HBC_VERSION) {
        fprintf(stderr, "Unsupported bytecode version %d (expected %d)\n",
                version, HBC_VERSION);
        fclose(f);
        return NULL;
    }

    Chunk *chunk = chunk_new(NULL);

    // Read chunk name
    uint16_t name_len;
    fread(&name_len, sizeof(name_len), 1, f);
    if (name_len > 0) {
        chunk->name = malloc(name_len + 1);
        fread(chunk->name, 1, name_len, f);
        chunk->name[name_len] = '\0';
    }

    // Read function metadata
    fread(&chunk->arity, sizeof(chunk->arity), 1, f);
    fread(&chunk->is_async, sizeof(chunk->is_async), 1, f);
    fread(&chunk->max_stack_size, sizeof(chunk->max_stack_size), 1, f);

    // Read constants
    int const_count;
    fread(&const_count, sizeof(const_count), 1, f);
    for (int i = 0; i < const_count; i++) {
        ConstantType type;
        fread(&type, sizeof(type), 1, f);

        switch (type) {
            case CONST_NULL:
                chunk_add_constant_null(chunk);
                break;
            case CONST_BOOL: {
                bool val;
                fread(&val, sizeof(val), 1, f);
                chunk_add_constant_bool(chunk, val);
                break;
            }
            case CONST_I32: {
                int32_t val;
                fread(&val, sizeof(val), 1, f);
                chunk_add_constant_i32(chunk, val);
                break;
            }
            case CONST_I64: {
                int64_t val;
                fread(&val, sizeof(val), 1, f);
                chunk_add_constant_i64(chunk, val);
                break;
            }
            case CONST_F64: {
                double val;
                fread(&val, sizeof(val), 1, f);
                chunk_add_constant_f64(chunk, val);
                break;
            }
            case CONST_RUNE: {
                uint32_t val;
                fread(&val, sizeof(val), 1, f);
                chunk_add_constant_rune(chunk, val);
                break;
            }
            case CONST_STRING: {
                int32_t len;
                fread(&len, sizeof(len), 1, f);
                char *data = malloc(len + 1);
                fread(data, 1, len, f);
                data[len] = '\0';
                chunk_add_constant_string(chunk, data, len);
                free(data);
                break;
            }
        }
    }

    // Read bytecode
    fread(&chunk->code_count, sizeof(chunk->code_count), 1, f);
    chunk->code_capacity = chunk->code_count;
    chunk->code = malloc(chunk->code_count * sizeof(uint32_t));
    fread(chunk->code, sizeof(uint32_t), chunk->code_count, f);

    // Read line info
    fread(&chunk->line_count, sizeof(chunk->line_count), 1, f);
    chunk->line_capacity = chunk->line_count;
    chunk->lines = malloc(chunk->line_count * sizeof(LineInfo));
    for (int i = 0; i < chunk->line_count; i++) {
        fread(&chunk->lines[i].pc, sizeof(int), 1, f);
        fread(&chunk->lines[i].line, sizeof(int), 1, f);
    }

    // Read upvalues
    fread(&chunk->upvalue_count, sizeof(chunk->upvalue_count), 1, f);
    if (chunk->upvalue_count > 0) {
        chunk->upvalues = malloc(chunk->upvalue_count * sizeof(UpvalueDesc));
        for (int i = 0; i < chunk->upvalue_count; i++) {
            fread(&chunk->upvalues[i].index, sizeof(uint8_t), 1, f);
            fread(&chunk->upvalues[i].is_local, sizeof(bool), 1, f);
            chunk->upvalues[i].name = NULL;
        }
    }

    // Read nested proto count (recursive read would go here)
    fread(&chunk->proto_count, sizeof(chunk->proto_count), 1, f);
    // TODO: Recursively read nested protos

    fclose(f);
    return chunk;
}

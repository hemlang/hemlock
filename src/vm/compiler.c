/*
 * Hemlock Bytecode Compiler - Main Implementation
 */

#include "compiler.h"
#include "bytecode.h"
#include "chunk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== Compiler Creation/Destruction ==========

Compiler* compiler_new(const char *source_file) {
    Compiler *compiler = malloc(sizeof(Compiler));

    compiler->chunk = chunk_new(NULL);
    compiler->enclosing = NULL;
    compiler->fn_type = FN_TYPE_SCRIPT;

    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->upvalue_count = 0;

    compiler->next_register = 0;
    compiler->max_register = 0;

    compiler->current_loop = NULL;
    compiler->current_try = NULL;
    compiler->defer_count = 0;

    compiler->had_error = false;
    compiler->panic_mode = false;
    compiler->error_message = NULL;

    compiler->source_file = source_file;
    compiler->current_line = 1;

    return compiler;
}

void compiler_free(Compiler *compiler) {
    if (!compiler) return;

    // Note: chunk ownership transfers to caller on successful compile
    free(compiler->error_message);
    free(compiler);
}

// ========== Error Handling ==========

void compiler_error(Compiler *compiler, const char *message) {
    compiler_error_at(compiler, compiler->current_line, message);
}

void compiler_error_at(Compiler *compiler, int line, const char *message) {
    if (compiler->panic_mode) return;  // Suppress cascading errors
    compiler->panic_mode = true;
    compiler->had_error = true;

    char buf[512];
    snprintf(buf, sizeof(buf), "[line %d] Error: %s", line, message);
    compiler->error_message = strdup(buf);

    fprintf(stderr, "%s\n", buf);
}

bool compiler_had_error(Compiler *compiler) {
    return compiler->had_error;
}

const char* compiler_get_error(Compiler *compiler) {
    return compiler->error_message;
}

// ========== Register Allocation ==========

int compiler_alloc_register(Compiler *compiler) {
    int reg = compiler->next_register++;
    if (compiler->next_register > compiler->max_register) {
        compiler->max_register = compiler->next_register;
    }
    if (reg >= MAX_REGISTERS) {
        compiler_error(compiler, "Too many registers (expression too complex)");
        return 0;
    }
    return reg;
}

void compiler_free_registers(Compiler *compiler, int to) {
    compiler->next_register = to;
}

int compiler_register_state(Compiler *compiler) {
    return compiler->next_register;
}

// ========== Scope Management ==========

void compiler_begin_scope(Compiler *compiler) {
    compiler->scope_depth++;
}

void compiler_end_scope(Compiler *compiler) {
    compiler->scope_depth--;

    // Pop locals that are going out of scope
    while (compiler->local_count > 0 &&
           compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {

        Local *local = &compiler->locals[compiler->local_count - 1];

        // Mark end of scope in debug info
        chunk_mark_local_end(compiler->chunk, compiler->local_count - 1,
                            chunk_current_offset(compiler->chunk));

        // If captured, need to close upvalue (handled at runtime)
        if (local->is_captured) {
            // Emit OP_CLOSE_UPVALUE in future
        }

        free(local->name);
        compiler->local_count--;
    }
}

// ========== Variable Management ==========

int compiler_declare_local(Compiler *compiler, const char *name, bool is_const) {
    // Check for existing variable in current scope
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scope_depth) {
            break;  // Hit outer scope, stop checking
        }
        if (strcmp(local->name, name) == 0) {
            compiler_error(compiler, "Variable already declared in this scope");
            return -1;
        }
    }

    if (compiler->local_count >= MAX_LOCALS) {
        compiler_error(compiler, "Too many local variables");
        return -1;
    }

    Local *local = &compiler->locals[compiler->local_count];
    local->name = strdup(name);
    local->depth = -1;  // Mark uninitialized
    local->is_const = is_const;
    local->is_captured = false;

    // Register slot for local
    int slot = compiler->next_register++;
    if (compiler->next_register > compiler->max_register) {
        compiler->max_register = compiler->next_register;
    }

    // Add to chunk debug info
    chunk_add_local(compiler->chunk, name, compiler->scope_depth, slot, is_const);

    return compiler->local_count++;
}

void compiler_define_local(Compiler *compiler, int local_index) {
    if (local_index >= 0 && local_index < compiler->local_count) {
        compiler->locals[local_index].depth = compiler->scope_depth;
    }
}

int compiler_resolve_local(Compiler *compiler, const char *name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (strcmp(local->name, name) == 0) {
            if (local->depth == -1) {
                compiler_error(compiler, "Cannot reference variable in its own initializer");
                return -1;
            }
            return i;
        }
    }
    return -1;  // Not found
}

// Add upvalue to compiler
static int add_upvalue(Compiler *compiler, uint8_t index, bool is_local) {
    int count = compiler->upvalue_count;

    // Check if already captured
    for (int i = 0; i < count; i++) {
        Upvalue *uv = &compiler->upvalues[i];
        if (uv->index == index && uv->is_local == is_local) {
            return i;
        }
    }

    if (count >= MAX_UPVALUES) {
        compiler_error(compiler, "Too many closure variables");
        return 0;
    }

    compiler->upvalues[count].index = index;
    compiler->upvalues[count].is_local = is_local;
    return compiler->upvalue_count++;
}

int compiler_resolve_upvalue(Compiler *compiler, const char *name) {
    if (compiler->enclosing == NULL) {
        return -1;  // Not in a nested function
    }

    // Look for local in enclosing function
    int local = compiler_resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        chunk_mark_local_captured(compiler->enclosing->chunk, local);
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    // Look for upvalue in enclosing function
    int upvalue = compiler_resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

// ========== Bytecode Emission Helpers ==========

void compiler_emit(Compiler *compiler, uint32_t instruction) {
    chunk_emit(compiler->chunk, instruction, compiler->current_line);
}

static void emit_abc(Compiler *compiler, Opcode op, uint8_t a, uint8_t b, uint8_t c) {
    chunk_emit_abc(compiler->chunk, op, a, b, c, compiler->current_line);
}

static void emit_abx(Compiler *compiler, Opcode op, uint8_t a, uint16_t bx) {
    chunk_emit_abx(compiler->chunk, op, a, bx, compiler->current_line);
}

static void emit_asbx(Compiler *compiler, Opcode op, uint8_t a, int16_t sbx) {
    chunk_emit_asbx(compiler->chunk, op, a, sbx, compiler->current_line);
}

static void emit_sax(Compiler *compiler, Opcode op, int32_t sax) {
    chunk_emit_sax(compiler->chunk, op, sax, compiler->current_line);
}

static int emit_jump(Compiler *compiler, Opcode op) {
    // Emit placeholder jump, return offset for patching
    emit_sax(compiler, op, 0);
    return chunk_current_offset(compiler->chunk) - 1;
}

static int emit_jump_cond(Compiler *compiler, Opcode op, uint8_t reg) {
    emit_asbx(compiler, op, reg, 0);
    return chunk_current_offset(compiler->chunk) - 1;
}

static void patch_jump(Compiler *compiler, int offset) {
    int jump = chunk_current_offset(compiler->chunk) - offset - 1;
    chunk_patch_jump(compiler->chunk, offset, chunk_current_offset(compiler->chunk));
}

// ========== Expression Compilation ==========

// Forward declarations
static void compile_stmt(Compiler *compiler, Stmt *stmt);
static void compile_block(Compiler *compiler, Stmt **statements, int count);

// Compile expression into dest_reg
void compile_expression(Compiler *compiler, Expr *expr, int dest_reg) {
    if (!expr) {
        emit_abx(compiler, OP_LOAD_NULL, dest_reg, 0);
        return;
    }

    compiler->current_line = expr->line;

    switch (expr->type) {
        case EXPR_NUMBER: {
            int const_idx;
            if (expr->as.number.is_float) {
                const_idx = chunk_add_constant_f64(compiler->chunk, expr->as.number.float_value);
            } else {
                int64_t val = expr->as.number.int_value;
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    const_idx = chunk_add_constant_i32(compiler->chunk, (int32_t)val);
                } else {
                    const_idx = chunk_add_constant_i64(compiler->chunk, val);
                }
            }
            emit_abx(compiler, OP_LOAD_CONST, dest_reg, const_idx);
            break;
        }

        case EXPR_BOOL:
            if (expr->as.boolean) {
                emit_abx(compiler, OP_LOAD_TRUE, dest_reg, 0);
            } else {
                emit_abx(compiler, OP_LOAD_FALSE, dest_reg, 0);
            }
            break;

        case EXPR_STRING: {
            int len = strlen(expr->as.string);
            int const_idx = chunk_add_constant_string(compiler->chunk, expr->as.string, len);
            emit_abx(compiler, OP_LOAD_CONST, dest_reg, const_idx);
            break;
        }

        case EXPR_RUNE: {
            int const_idx = chunk_add_constant_rune(compiler->chunk, expr->as.rune);
            emit_abx(compiler, OP_LOAD_CONST, dest_reg, const_idx);
            break;
        }

        case EXPR_NULL:
            emit_abx(compiler, OP_LOAD_NULL, dest_reg, 0);
            break;

        case EXPR_IDENT: {
            const char *name = expr->as.ident;

            // Try local first
            int local = compiler_resolve_local(compiler, name);
            if (local != -1) {
                // Local is stored in a register - move to dest
                emit_abc(compiler, OP_MOVE, dest_reg, local, 0);
                break;
            }

            // Try upvalue
            int upvalue = compiler_resolve_upvalue(compiler, name);
            if (upvalue != -1) {
                emit_abx(compiler, OP_LOAD_UPVALUE, dest_reg, upvalue);
                break;
            }

            // Must be global
            int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));
            emit_abx(compiler, OP_LOAD_GLOBAL, dest_reg, name_idx);
            break;
        }

        case EXPR_BINARY: {
            BinaryOp op = expr->as.binary.op;

            // Short-circuit for && and ||
            if (op == OP_AND) {
                compile_expression(compiler, expr->as.binary.left, dest_reg);
                int jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, dest_reg);
                compile_expression(compiler, expr->as.binary.right, dest_reg);
                patch_jump(compiler, jump);
                break;
            }
            if (op == OP_OR) {
                compile_expression(compiler, expr->as.binary.left, dest_reg);
                int jump = emit_jump_cond(compiler, OP_JMP_IF_TRUE, dest_reg);
                compile_expression(compiler, expr->as.binary.right, dest_reg);
                patch_jump(compiler, jump);
                break;
            }

            // Regular binary ops
            int state = compiler_register_state(compiler);
            int left_reg = dest_reg;
            int right_reg = compiler_alloc_register(compiler);

            compile_expression(compiler, expr->as.binary.left, left_reg);
            compile_expression(compiler, expr->as.binary.right, right_reg);

            Opcode bytecode_op;
            switch (op) {
                case OP_ADD:           bytecode_op = OP_ADD; break;
                case OP_SUB:           bytecode_op = OP_SUB; break;
                case OP_MUL:           bytecode_op = OP_MUL; break;
                case OP_DIV:           bytecode_op = OP_DIV; break;
                case OP_MOD:           bytecode_op = OP_MOD; break;
                case OP_EQUAL:         bytecode_op = OP_EQ; break;
                case OP_NOT_EQUAL:     bytecode_op = OP_NE; break;
                case OP_LESS:          bytecode_op = OP_LT; break;
                case OP_LESS_EQUAL:    bytecode_op = OP_LE; break;
                case OP_GREATER:       bytecode_op = OP_GT; break;
                case OP_GREATER_EQUAL: bytecode_op = OP_GE; break;
                case OP_BIT_AND:       bytecode_op = OP_BAND; break;
                case OP_BIT_OR:        bytecode_op = OP_BOR; break;
                case OP_BIT_XOR:       bytecode_op = OP_BXOR; break;
                case OP_BIT_LSHIFT:    bytecode_op = OP_SHL; break;
                case OP_BIT_RSHIFT:    bytecode_op = OP_SHR; break;
                default:
                    compiler_error(compiler, "Unknown binary operator");
                    bytecode_op = OP_ADD;
            }

            emit_abc(compiler, bytecode_op, dest_reg, left_reg, right_reg);
            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_UNARY: {
            compile_expression(compiler, expr->as.unary.operand, dest_reg);

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    emit_abc(compiler, OP_NOT, dest_reg, dest_reg, 0);
                    break;
                case UNARY_NEGATE:
                    emit_abc(compiler, OP_NEG, dest_reg, dest_reg, 0);
                    break;
                case UNARY_BIT_NOT:
                    emit_abc(compiler, OP_BNOT, dest_reg, dest_reg, 0);
                    break;
            }
            break;
        }

        case EXPR_TERNARY: {
            compile_expression(compiler, expr->as.ternary.condition, dest_reg);
            int else_jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, dest_reg);

            compile_expression(compiler, expr->as.ternary.true_expr, dest_reg);
            int end_jump = emit_jump(compiler, OP_JMP);

            patch_jump(compiler, else_jump);
            compile_expression(compiler, expr->as.ternary.false_expr, dest_reg);

            patch_jump(compiler, end_jump);
            break;
        }

        case EXPR_CALL: {
            Expr *callee = expr->as.call.func;
            int num_args = expr->as.call.num_args;

            // Evaluate callee
            int state = compiler_register_state(compiler);
            int func_reg = dest_reg;
            compile_expression(compiler, callee, func_reg);

            // Evaluate arguments into consecutive registers
            for (int i = 0; i < num_args; i++) {
                int arg_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, expr->as.call.args[i], arg_reg);
            }

            // CALL: A=base, B=num_args, C=num_results (1 for now)
            emit_abc(compiler, OP_CALL, func_reg, num_args, 1);

            compiler_free_registers(compiler, state);
            // Result is in func_reg (dest_reg)
            break;
        }

        case EXPR_ASSIGN: {
            const char *name = expr->as.assign.name;

            // Compile the value
            compile_expression(compiler, expr->as.assign.value, dest_reg);

            // Try local
            int local = compiler_resolve_local(compiler, name);
            if (local != -1) {
                if (compiler->locals[local].is_const) {
                    compiler_error(compiler, "Cannot assign to const variable");
                }
                emit_abc(compiler, OP_MOVE, local, dest_reg, 0);
                break;
            }

            // Try upvalue
            int upvalue = compiler_resolve_upvalue(compiler, name);
            if (upvalue != -1) {
                emit_abx(compiler, OP_STORE_UPVALUE, dest_reg, upvalue);
                break;
            }

            // Global
            int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));
            emit_abx(compiler, OP_STORE_GLOBAL, dest_reg, name_idx);
            break;
        }

        case EXPR_GET_PROPERTY: {
            int state = compiler_register_state(compiler);
            int obj_reg = dest_reg;

            compile_expression(compiler, expr->as.get_property.object, obj_reg);

            int name_idx = chunk_add_constant_string(compiler->chunk,
                                                     expr->as.get_property.property,
                                                     strlen(expr->as.get_property.property));
            emit_abc(compiler, OP_GET_FIELD, dest_reg, obj_reg, name_idx);

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_SET_PROPERTY: {
            int state = compiler_register_state(compiler);
            int obj_reg = compiler_alloc_register(compiler);
            int val_reg = dest_reg;

            compile_expression(compiler, expr->as.set_property.object, obj_reg);
            compile_expression(compiler, expr->as.set_property.value, val_reg);

            int name_idx = chunk_add_constant_string(compiler->chunk,
                                                     expr->as.set_property.property,
                                                     strlen(expr->as.set_property.property));
            emit_abc(compiler, OP_SET_FIELD, obj_reg, name_idx, val_reg);

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_INDEX: {
            int state = compiler_register_state(compiler);
            int obj_reg = dest_reg;
            int idx_reg = compiler_alloc_register(compiler);

            compile_expression(compiler, expr->as.index.object, obj_reg);
            compile_expression(compiler, expr->as.index.index, idx_reg);

            emit_abc(compiler, OP_GET_INDEX, dest_reg, obj_reg, idx_reg);

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            int state = compiler_register_state(compiler);
            int obj_reg = compiler_alloc_register(compiler);
            int idx_reg = compiler_alloc_register(compiler);
            int val_reg = dest_reg;

            compile_expression(compiler, expr->as.index_assign.object, obj_reg);
            compile_expression(compiler, expr->as.index_assign.index, idx_reg);
            compile_expression(compiler, expr->as.index_assign.value, val_reg);

            emit_abc(compiler, OP_SET_INDEX, obj_reg, idx_reg, val_reg);

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_FUNCTION: {
            // Create nested compiler for the function body
            Compiler nested;
            memset(&nested, 0, sizeof(Compiler));

            nested.chunk = chunk_new(NULL);
            nested.enclosing = compiler;
            nested.fn_type = FN_TYPE_CLOSURE;
            nested.source_file = compiler->source_file;
            nested.current_line = expr->line;
            nested.scope_depth = 0;

            nested.chunk->is_async = expr->as.function.is_async;
            nested.chunk->arity = expr->as.function.num_params;

            compiler_begin_scope(&nested);

            // Declare parameters
            for (int i = 0; i < expr->as.function.num_params; i++) {
                int local = compiler_declare_local(&nested,
                                                   expr->as.function.param_names[i],
                                                   false);
                compiler_define_local(&nested, local);
            }

            // Compile body
            if (expr->as.function.body) {
                compile_stmt(&nested, expr->as.function.body);
            }

            // Emit implicit return null
            emit_abx(&nested, OP_LOAD_NULL, 0, 0);
            emit_abc(&nested, OP_RETURN, 0, 1, 0);

            compiler_end_scope(&nested);

            // Add upvalue descriptors to nested chunk
            for (int i = 0; i < nested.upvalue_count; i++) {
                chunk_add_upvalue(nested.chunk, nested.upvalues[i].index,
                                 nested.upvalues[i].is_local, NULL);
            }

            nested.chunk->max_stack_size = nested.max_register;

            // Add as nested proto
            int proto_idx = chunk_add_proto(compiler->chunk, nested.chunk);

            // Emit closure instruction
            emit_abx(compiler, OP_CLOSURE, dest_reg, proto_idx);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            int num_elements = expr->as.array_literal.num_elements;
            int state = compiler_register_state(compiler);

            // Compile elements into consecutive registers
            for (int i = 0; i < num_elements; i++) {
                int elem_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, expr->as.array_literal.elements[i], elem_reg);
            }

            // NEW_ARRAY: A=dest, B=num_elements (elements in A+1..A+B)
            emit_abc(compiler, OP_NEW_ARRAY, dest_reg, num_elements, 0);

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            int num_fields = expr->as.object_literal.num_fields;
            int state = compiler_register_state(compiler);

            // Compile key-value pairs (keys as constants, values in registers)
            for (int i = 0; i < num_fields; i++) {
                int val_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, expr->as.object_literal.field_values[i], val_reg);
            }

            // NEW_OBJECT: A=dest, B=num_fields
            // Field names stored as constants, referenced during VM execution
            emit_abc(compiler, OP_NEW_OBJECT, dest_reg, num_fields, 0);

            // Store field name indices in chunk for VM to use
            // (This is simplified - a full impl would store the mapping)

            compiler_free_registers(compiler, state);
            break;
        }

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC: {
            // ++x or --x: increment/decrement then return new value
            Expr *operand = (expr->type == EXPR_PREFIX_INC) ?
                           expr->as.prefix_inc.operand :
                           expr->as.prefix_dec.operand;

            // Must be assignable (ident for now)
            if (operand->type != EXPR_IDENT) {
                compiler_error(compiler, "Invalid operand for prefix increment/decrement");
                break;
            }

            const char *name = operand->as.ident;
            int local = compiler_resolve_local(compiler, name);

            if (local != -1) {
                Opcode op = (expr->type == EXPR_PREFIX_INC) ? OP_INC : OP_DEC;
                emit_abc(compiler, op, local, 0, 0);
                emit_abc(compiler, OP_MOVE, dest_reg, local, 0);
            } else {
                // Global - load, modify, store
                int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));
                emit_abx(compiler, OP_LOAD_GLOBAL, dest_reg, name_idx);
                Opcode op = (expr->type == EXPR_PREFIX_INC) ? OP_INC : OP_DEC;
                emit_abc(compiler, op, dest_reg, 0, 0);
                emit_abx(compiler, OP_STORE_GLOBAL, dest_reg, name_idx);
            }
            break;
        }

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC: {
            // x++ or x--: return old value, then increment/decrement
            Expr *operand = (expr->type == EXPR_POSTFIX_INC) ?
                           expr->as.postfix_inc.operand :
                           expr->as.postfix_dec.operand;

            if (operand->type != EXPR_IDENT) {
                compiler_error(compiler, "Invalid operand for postfix increment/decrement");
                break;
            }

            const char *name = operand->as.ident;
            int local = compiler_resolve_local(compiler, name);

            if (local != -1) {
                // Copy old value
                emit_abc(compiler, OP_MOVE, dest_reg, local, 0);
                // Increment/decrement in place
                Opcode op = (expr->type == EXPR_POSTFIX_INC) ? OP_INC : OP_DEC;
                emit_abc(compiler, op, local, 0, 0);
            } else {
                // Global
                int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));
                emit_abx(compiler, OP_LOAD_GLOBAL, dest_reg, name_idx);

                int temp_reg = compiler_alloc_register(compiler);
                emit_abc(compiler, OP_MOVE, temp_reg, dest_reg, 0);

                Opcode op = (expr->type == EXPR_POSTFIX_INC) ? OP_INC : OP_DEC;
                emit_abc(compiler, op, temp_reg, 0, 0);
                emit_abx(compiler, OP_STORE_GLOBAL, temp_reg, name_idx);

                compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            }
            break;
        }

        case EXPR_AWAIT: {
            compile_expression(compiler, expr->as.await_expr.awaited_expr, dest_reg);
            emit_abc(compiler, OP_AWAIT, dest_reg, dest_reg, 0);
            break;
        }

        case EXPR_STRING_INTERPOLATION: {
            int num_parts = expr->as.string_interpolation.num_parts;
            char **string_parts = expr->as.string_interpolation.string_parts;
            Expr **expr_parts = expr->as.string_interpolation.expr_parts;

            // Start with first string part
            int result_reg = dest_reg;
            int const_idx = chunk_add_constant_string(compiler->chunk,
                                                      string_parts[0],
                                                      strlen(string_parts[0]));
            emit_abx(compiler, OP_LOAD_CONST, result_reg, const_idx);

            int state = compiler_register_state(compiler);

            for (int i = 0; i < num_parts; i++) {
                // Concatenate expression part
                int expr_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, expr_parts[i], expr_reg);
                emit_abc(compiler, OP_CONCAT, result_reg, result_reg, expr_reg);
                compiler_free_registers(compiler, state);

                // Concatenate next string part
                int str_reg = compiler_alloc_register(compiler);
                const_idx = chunk_add_constant_string(compiler->chunk,
                                                      string_parts[i + 1],
                                                      strlen(string_parts[i + 1]));
                emit_abx(compiler, OP_LOAD_CONST, str_reg, const_idx);
                emit_abc(compiler, OP_CONCAT, result_reg, result_reg, str_reg);
                compiler_free_registers(compiler, state);
            }
            break;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // obj?.prop - evaluate to null if obj is null
            int obj_reg = dest_reg;
            compile_expression(compiler, expr->as.optional_chain.object, obj_reg);

            // Check if null, jump to end if so
            int null_jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, obj_reg);
            // TODO: Need proper null check opcode

            if (expr->as.optional_chain.is_property) {
                int name_idx = chunk_add_constant_string(compiler->chunk,
                                                        expr->as.optional_chain.property,
                                                        strlen(expr->as.optional_chain.property));
                emit_abc(compiler, OP_GET_FIELD_CHAIN, dest_reg, obj_reg, name_idx);
            } else if (expr->as.optional_chain.is_call) {
                // Optional method call - compile args and call
                // (simplified for now)
            } else {
                // Optional indexing
                int state = compiler_register_state(compiler);
                int idx_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, expr->as.optional_chain.index, idx_reg);
                emit_abc(compiler, OP_GET_INDEX, dest_reg, obj_reg, idx_reg);
                compiler_free_registers(compiler, state);
            }

            patch_jump(compiler, null_jump);
            break;
        }

        case EXPR_NULL_COALESCE: {
            // left ?? right - return left if not null, else right
            compile_expression(compiler, expr->as.null_coalesce.left, dest_reg);

            // If not null, skip right side
            // TODO: Need proper null check - for now use JMP_IF_TRUE
            int skip_jump = emit_jump_cond(compiler, OP_JMP_IF_TRUE, dest_reg);

            compile_expression(compiler, expr->as.null_coalesce.right, dest_reg);

            patch_jump(compiler, skip_jump);
            break;
        }

        default:
            compiler_error(compiler, "Unknown expression type");
            emit_abx(compiler, OP_LOAD_NULL, dest_reg, 0);
            break;
    }
}

// ========== Statement Compilation ==========

static void compile_stmt(Compiler *compiler, Stmt *stmt) {
    if (!stmt) return;

    compiler->current_line = stmt->line;

    switch (stmt->type) {
        case STMT_LET:
        case STMT_CONST: {
            bool is_const = (stmt->type == STMT_CONST);
            const char *name = is_const ? stmt->as.const_stmt.name : stmt->as.let.name;
            Expr *init = is_const ? stmt->as.const_stmt.value : stmt->as.let.value;

            if (compiler->scope_depth > 0) {
                // Local variable
                int local = compiler_declare_local(compiler, name, is_const);
                if (local == -1) break;

                if (init) {
                    compile_expression(compiler, init, local);
                } else {
                    emit_abx(compiler, OP_LOAD_NULL, local, 0);
                }

                compiler_define_local(compiler, local);
            } else {
                // Global variable
                int reg = compiler_alloc_register(compiler);
                if (init) {
                    compile_expression(compiler, init, reg);
                } else {
                    emit_abx(compiler, OP_LOAD_NULL, reg, 0);
                }

                int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));
                emit_abx(compiler, OP_STORE_GLOBAL, reg, name_idx);
                compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            }
            break;
        }

        case STMT_EXPR: {
            int reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.expr, reg);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_IF: {
            int cond_reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.if_stmt.condition, cond_reg);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);

            int else_jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, cond_reg);

            compile_stmt(compiler, stmt->as.if_stmt.then_branch);

            if (stmt->as.if_stmt.else_branch) {
                int end_jump = emit_jump(compiler, OP_JMP);
                patch_jump(compiler, else_jump);
                compile_stmt(compiler, stmt->as.if_stmt.else_branch);
                patch_jump(compiler, end_jump);
            } else {
                patch_jump(compiler, else_jump);
            }
            break;
        }

        case STMT_WHILE: {
            int loop_start = chunk_current_offset(compiler->chunk);

            // Set up loop context
            Loop loop = {
                .start = loop_start,
                .scope_depth = compiler->scope_depth,
                .breaks = NULL,
                .break_count = 0,
                .break_capacity = 0,
                .enclosing = compiler->current_loop
            };
            compiler->current_loop = &loop;

            int cond_reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.while_stmt.condition, cond_reg);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);

            int exit_jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, cond_reg);

            compile_stmt(compiler, stmt->as.while_stmt.body);

            // Loop back
            int loop_offset = chunk_current_offset(compiler->chunk) - loop_start;
            emit_sax(compiler, OP_LOOP, loop_offset);

            patch_jump(compiler, exit_jump);

            // Patch breaks
            for (int i = 0; i < loop.break_count; i++) {
                patch_jump(compiler, loop.breaks[i]);
            }
            free(loop.breaks);

            compiler->current_loop = loop.enclosing;
            break;
        }

        case STMT_FOR: {
            compiler_begin_scope(compiler);

            // Initializer
            if (stmt->as.for_loop.initializer) {
                compile_stmt(compiler, stmt->as.for_loop.initializer);
            }

            int loop_start = chunk_current_offset(compiler->chunk);

            // Set up loop context
            Loop loop = {
                .start = loop_start,
                .scope_depth = compiler->scope_depth,
                .breaks = NULL,
                .break_count = 0,
                .break_capacity = 0,
                .enclosing = compiler->current_loop
            };
            compiler->current_loop = &loop;

            int exit_jump = -1;
            if (stmt->as.for_loop.condition) {
                int cond_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, stmt->as.for_loop.condition, cond_reg);
                compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
                exit_jump = emit_jump_cond(compiler, OP_JMP_IF_FALSE, cond_reg);
            }

            compile_stmt(compiler, stmt->as.for_loop.body);

            // Increment
            if (stmt->as.for_loop.increment) {
                int inc_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, stmt->as.for_loop.increment, inc_reg);
                compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            }

            // Loop back
            int loop_offset = chunk_current_offset(compiler->chunk) - loop_start;
            emit_sax(compiler, OP_LOOP, loop_offset);

            if (exit_jump != -1) {
                patch_jump(compiler, exit_jump);
            }

            // Patch breaks
            for (int i = 0; i < loop.break_count; i++) {
                patch_jump(compiler, loop.breaks[i]);
            }
            free(loop.breaks);

            compiler->current_loop = loop.enclosing;
            compiler_end_scope(compiler);
            break;
        }

        case STMT_FOR_IN: {
            // for (item in array) { ... }
            compiler_begin_scope(compiler);

            // Evaluate iterable into a register
            int iter_reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.for_in.iterable, iter_reg);

            // Index counter
            int idx_reg = compiler_alloc_register(compiler);
            int zero_idx = chunk_add_constant_i32(compiler->chunk, 0);
            emit_abx(compiler, OP_LOAD_CONST, idx_reg, zero_idx);

            // Loop variable
            int var_local = compiler_declare_local(compiler, stmt->as.for_in.value_var, false);
            compiler_define_local(compiler, var_local);

            int loop_start = chunk_current_offset(compiler->chunk);

            // Set up loop context
            Loop loop = {
                .start = loop_start,
                .scope_depth = compiler->scope_depth,
                .breaks = NULL,
                .break_count = 0,
                .break_capacity = 0,
                .enclosing = compiler->current_loop
            };
            compiler->current_loop = &loop;

            // Check index < length (simplified - assumes array for now)
            // TODO: proper iteration protocol
            int cond_reg = compiler_alloc_register(compiler);
            // This would need a GET_LENGTH opcode or similar
            // For now, use a simplified approach

            compile_stmt(compiler, stmt->as.for_in.body);

            // Increment index
            emit_abc(compiler, OP_INC, idx_reg, 0, 0);

            // Loop back
            int loop_offset = chunk_current_offset(compiler->chunk) - loop_start;
            emit_sax(compiler, OP_LOOP, loop_offset);

            // Patch breaks
            for (int i = 0; i < loop.break_count; i++) {
                patch_jump(compiler, loop.breaks[i]);
            }
            free(loop.breaks);

            compiler->current_loop = loop.enclosing;
            compiler_end_scope(compiler);
            break;
        }

        case STMT_BREAK: {
            if (!compiler->current_loop) {
                compiler_error(compiler, "'break' outside of loop");
                break;
            }

            // Execute defers before breaking
            emit_abx(compiler, OP_DEFER_EXEC_ALL, 0, 0);

            // Add break jump to patch list
            Loop *loop = compiler->current_loop;
            if (loop->break_count >= loop->break_capacity) {
                int new_cap = loop->break_capacity < 4 ? 4 : loop->break_capacity * 2;
                loop->breaks = realloc(loop->breaks, new_cap * sizeof(int));
                loop->break_capacity = new_cap;
            }
            loop->breaks[loop->break_count++] = emit_jump(compiler, OP_JMP);
            break;
        }

        case STMT_CONTINUE: {
            if (!compiler->current_loop) {
                compiler_error(compiler, "'continue' outside of loop");
                break;
            }

            // Execute defers before continuing
            emit_abx(compiler, OP_DEFER_EXEC_ALL, 0, 0);

            // Jump back to loop start
            int offset = chunk_current_offset(compiler->chunk) - compiler->current_loop->start;
            emit_sax(compiler, OP_LOOP, offset);
            break;
        }

        case STMT_BLOCK: {
            compile_block(compiler, stmt->as.block.statements, stmt->as.block.count);
            break;
        }

        case STMT_RETURN: {
            // Execute defers before returning
            if (compiler->defer_count > 0) {
                emit_abx(compiler, OP_DEFER_EXEC_ALL, 0, 0);
            }

            if (stmt->as.return_stmt.value) {
                int reg = compiler_alloc_register(compiler);
                compile_expression(compiler, stmt->as.return_stmt.value, reg);
                emit_abc(compiler, OP_RETURN, reg, 1, 0);
                compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            } else {
                emit_abc(compiler, OP_RETURN, 0, 0, 0);
            }
            break;
        }

        case STMT_DEFINE_OBJECT: {
            // Object type definition - store in type registry
            // For now, just register the type name as a global
            const char *name = stmt->as.define_object.name;
            int name_idx = chunk_add_constant_string(compiler->chunk, name, strlen(name));

            // Store type info (simplified - full impl would store field schema)
            int reg = compiler_alloc_register(compiler);
            emit_abx(compiler, OP_LOAD_CONST, reg, name_idx);
            emit_abx(compiler, OP_STORE_GLOBAL, reg, name_idx);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_ENUM: {
            // Enum declaration - register variants
            // For now, store as global object with variant values
            break;
        }

        case STMT_TRY: {
            // Begin try block
            int try_start = chunk_current_offset(compiler->chunk);
            int catch_reg = compiler_alloc_register(compiler);

            // TRY_BEGIN: A=catch_reg, sBx=offset to catch
            int try_begin = emit_jump_cond(compiler, OP_TRY_BEGIN, catch_reg);

            TryBlock try_block = {
                .try_start = try_start,
                .catch_jump = try_begin,
                .has_catch = (stmt->as.try_stmt.catch_block != NULL),
                .has_finally = (stmt->as.try_stmt.finally_block != NULL),
                .enclosing = compiler->current_try
            };
            compiler->current_try = &try_block;

            // Compile try body
            compile_stmt(compiler, stmt->as.try_stmt.try_block);

            // End try block - jump over catch
            emit_abx(compiler, OP_TRY_END, 0, 0);
            int end_try_jump = emit_jump(compiler, OP_JMP);

            // Patch try_begin to jump here on exception
            patch_jump(compiler, try_begin);

            // Catch block
            if (stmt->as.try_stmt.catch_block) {
                compiler_begin_scope(compiler);

                // Bind exception to catch parameter
                if (stmt->as.try_stmt.catch_param) {
                    int local = compiler_declare_local(compiler, stmt->as.try_stmt.catch_param, false);
                    compiler_define_local(compiler, local);
                    emit_abc(compiler, OP_CATCH, local, 0, 0);
                }

                compile_stmt(compiler, stmt->as.try_stmt.catch_block);

                compiler_end_scope(compiler);
            }

            patch_jump(compiler, end_try_jump);

            // Finally block
            if (stmt->as.try_stmt.finally_block) {
                compile_stmt(compiler, stmt->as.try_stmt.finally_block);
            }

            compiler->current_try = try_block.enclosing;
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_THROW: {
            int reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.throw_stmt.value, reg);
            emit_abc(compiler, OP_THROW, reg, 0, 0);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_SWITCH: {
            int value_reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.switch_stmt.expr, value_reg);

            int num_cases = stmt->as.switch_stmt.num_cases;
            int *case_jumps = malloc(num_cases * sizeof(int));
            int default_idx = -1;

            // Generate comparisons and conditional jumps
            for (int i = 0; i < num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    // Default case
                    default_idx = i;
                    continue;
                }

                int case_reg = compiler_alloc_register(compiler);
                compile_expression(compiler, stmt->as.switch_stmt.case_values[i], case_reg);

                int cmp_reg = compiler_alloc_register(compiler);
                emit_abc(compiler, OP_EQ, cmp_reg, value_reg, case_reg);
                case_jumps[i] = emit_jump_cond(compiler, OP_JMP_IF_TRUE, cmp_reg);

                compiler_free_registers(compiler, compiler_register_state(compiler) - 2);
            }

            // Jump to default or end if no match
            int default_jump = emit_jump(compiler, OP_JMP);

            // Generate case bodies
            int *end_jumps = malloc(num_cases * sizeof(int));
            for (int i = 0; i < num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] != NULL) {
                    patch_jump(compiler, case_jumps[i]);
                }
                compile_stmt(compiler, stmt->as.switch_stmt.case_bodies[i]);
                end_jumps[i] = emit_jump(compiler, OP_JMP);
            }

            // Default case
            patch_jump(compiler, default_jump);
            if (default_idx >= 0) {
                compile_stmt(compiler, stmt->as.switch_stmt.case_bodies[default_idx]);
            }

            // Patch all end jumps
            for (int i = 0; i < num_cases; i++) {
                patch_jump(compiler, end_jumps[i]);
            }

            free(case_jumps);
            free(end_jumps);
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_DEFER: {
            // Push deferred call onto defer stack
            int reg = compiler_alloc_register(compiler);
            compile_expression(compiler, stmt->as.defer_stmt.call, reg);
            emit_abc(compiler, OP_DEFER_PUSH, reg, 0, 0);
            compiler->defer_count++;
            compiler_free_registers(compiler, compiler_register_state(compiler) - 1);
            break;
        }

        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
            // Module operations - handled separately
            // For now, skip
            break;

        default:
            compiler_error(compiler, "Unknown statement type");
            break;
    }
}

static void compile_block(Compiler *compiler, Stmt **statements, int count) {
    compiler_begin_scope(compiler);

    for (int i = 0; i < count; i++) {
        compile_stmt(compiler, statements[i]);
        if (compiler->had_error) break;
    }

    compiler_end_scope(compiler);
}

// ========== Program Compilation ==========

Chunk* compile_program(Compiler *compiler, Stmt **statements, int count) {
    for (int i = 0; i < count; i++) {
        compile_stmt(compiler, statements[i]);
        if (compiler->had_error) {
            chunk_free(compiler->chunk);
            return NULL;
        }
    }

    // Emit final return
    emit_abc(compiler, OP_RETURN, 0, 0, 0);

    compiler->chunk->max_stack_size = compiler->max_register;

    Chunk *result = compiler->chunk;
    compiler->chunk = NULL;  // Transfer ownership
    return result;
}

bool compile_statement(Compiler *compiler, Stmt *stmt) {
    compile_stmt(compiler, stmt);
    return !compiler->had_error;
}

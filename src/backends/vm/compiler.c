/*
 * Hemlock Bytecode VM - Compiler Implementation
 *
 * AST to bytecode compilation.
 */

#include "compiler.h"
#include "instruction.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Convert AST TypeKind to VM TypeId
static int type_kind_to_id(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:     return TYPE_ID_I8;
        case TYPE_I16:    return TYPE_ID_I16;
        case TYPE_I32:    return TYPE_ID_I32;
        case TYPE_I64:    return TYPE_ID_I64;
        case TYPE_U8:     return TYPE_ID_U8;
        case TYPE_U16:    return TYPE_ID_U16;
        case TYPE_U32:    return TYPE_ID_U32;
        case TYPE_U64:    return TYPE_ID_U64;
        case TYPE_F32:    return TYPE_ID_F32;
        case TYPE_F64:    return TYPE_ID_F64;
        case TYPE_BOOL:   return TYPE_ID_BOOL;
        case TYPE_STRING: return TYPE_ID_STRING;
        case TYPE_RUNE:   return TYPE_ID_RUNE;
        case TYPE_ARRAY:  return TYPE_ID_ARRAY;
        case TYPE_NULL:   return TYPE_ID_NULL;
        default:          return -1;  // Unknown/unsupported type
    }
}

// ============================================
// Compiler Lifecycle
// ============================================

static Compiler* compiler_new(Compiler *enclosing) {
    Compiler *compiler = malloc(sizeof(Compiler));
    compiler->builder = chunk_builder_new(enclosing ? enclosing->builder : NULL);
    compiler->enclosing = enclosing;
    compiler->function_name = NULL;
    compiler->is_async = false;
    compiler->current_line = 1;
    compiler->had_error = false;
    compiler->panic_mode = false;
    return compiler;
}

static void compiler_free(Compiler *compiler) {
    if (compiler->builder) {
        chunk_builder_free(compiler->builder);
    }
    free(compiler->function_name);
    free(compiler);
}

// ============================================
// Error Handling
// ============================================

void compiler_error(Compiler *compiler, const char *message) {
    compiler_error_at(compiler, compiler->current_line, message);
}

void compiler_error_at(Compiler *compiler, int line, const char *message) {
    if (compiler->panic_mode) return;
    compiler->panic_mode = true;
    compiler->had_error = true;
    fprintf(stderr, "[line %d] Error: %s\n", line, message);
}

// ============================================
// Bytecode Emission Helpers
// ============================================

static void emit_byte(Compiler *compiler, uint8_t byte) {
    chunk_write_byte(compiler->builder->chunk, byte, compiler->current_line);
}

static void emit_bytes(Compiler *compiler, uint8_t b1, uint8_t b2) {
    emit_byte(compiler, b1);
    emit_byte(compiler, b2);
}

static void emit_short(Compiler *compiler, uint16_t value) {
    chunk_write_short(compiler->builder->chunk, value, compiler->current_line);
}

static int emit_jump(Compiler *compiler, OpCode op) {
    return chunk_write_jump(compiler->builder->chunk, op, compiler->current_line);
}

static void patch_jump(Compiler *compiler, int offset) {
    chunk_patch_jump(compiler->builder->chunk, offset);
}

static void emit_loop(Compiler *compiler, int loop_start) {
    int offset = compiler->builder->chunk->code_count - loop_start + 3;
    if (offset > 0xFFFF) {
        compiler_error(compiler, "Loop body too large");
        return;
    }
    emit_byte(compiler, BC_LOOP);
    emit_byte(compiler, (offset >> 8) & 0xFF);
    emit_byte(compiler, offset & 0xFF);
}

static int make_constant(Compiler *compiler, Constant constant) {
    int idx = chunk_add_constant(compiler->builder->chunk, constant);
    if (idx > 0xFFFF) {
        compiler_error(compiler, "Too many constants in one chunk");
        return 0;
    }
    return idx;
}

static void emit_constant(Compiler *compiler, Constant constant) {
    int idx = make_constant(compiler, constant);
    emit_byte(compiler, BC_CONST);
    emit_short(compiler, idx);
}

// ============================================
// Expression Compilation
// ============================================

// Forward declarations
static void compile_expression(Compiler *compiler, Expr *expr);
static void compile_statement(Compiler *compiler, Stmt *stmt);

static void compile_number(Compiler *compiler, Expr *expr) {
    // Check if we can use a byte constant (integer 0-255)
    if (!expr->as.number.is_float && expr->as.number.int_value >= 0 && expr->as.number.int_value <= 255) {
        emit_byte(compiler, BC_CONST_BYTE);
        emit_byte(compiler, (uint8_t)expr->as.number.int_value);
        return;
    }

    Constant c;
    if (!expr->as.number.is_float) {
        if (expr->as.number.int_value >= INT32_MIN && expr->as.number.int_value <= INT32_MAX) {
            c.type = CONST_I32;
            c.as.i32 = (int32_t)expr->as.number.int_value;
        } else {
            c.type = CONST_I64;
            c.as.i64 = expr->as.number.int_value;
        }
    } else {
        c.type = CONST_F64;
        c.as.f64 = expr->as.number.float_value;
    }
    emit_constant(compiler, c);
}

static void compile_bool(Compiler *compiler, Expr *expr) {
    emit_byte(compiler, expr->as.boolean ? BC_TRUE : BC_FALSE);
}

static void compile_null(Compiler *compiler) {
    emit_byte(compiler, BC_NULL);
}

static void compile_string(Compiler *compiler, Expr *expr) {
    const char *str = expr->as.string;
    int len = strlen(str);
    int idx = chunk_add_string(compiler->builder->chunk, str, len);
    emit_byte(compiler, BC_CONST);
    emit_short(compiler, idx);
}

static void compile_rune(Compiler *compiler, Expr *expr) {
    // Runes are stored as i32 constants
    Constant c = {.type = CONST_I32, .as.i32 = (int32_t)expr->as.rune};
    int idx = make_constant(compiler, c);
    emit_byte(compiler, BC_CONST);
    emit_short(compiler, idx);
    // TODO: Mark as rune type
}

static void compile_identifier(Compiler *compiler, Expr *expr) {
    const char *name = expr->as.ident.name;

    // Special handling for 'self' - get method receiver
    if (strcmp(name, "self") == 0) {
        emit_byte(compiler, BC_GET_SELF);
        return;
    }

    // Check for resolved local
    if (expr->as.ident.resolved.is_resolved) {
        int depth = expr->as.ident.resolved.depth;
        int slot = expr->as.ident.resolved.slot;

        if (depth == 0) {
            emit_byte(compiler, BC_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
        } else {
            // Upvalue
            int upvalue = builder_resolve_upvalue(compiler->builder, name);
            if (upvalue != -1) {
                emit_byte(compiler, BC_GET_UPVALUE);
                emit_byte(compiler, (uint8_t)upvalue);
            } else {
                compiler_error(compiler, "Cannot resolve variable");
            }
        }
        return;
    }

    // Try local
    int local = builder_resolve_local(compiler->builder, name);
    if (local != -1) {
        emit_byte(compiler, BC_GET_LOCAL);
        emit_byte(compiler, (uint8_t)local);
        return;
    }

    // Try upvalue
    int upvalue = builder_resolve_upvalue(compiler->builder, name);
    if (upvalue != -1) {
        emit_byte(compiler, BC_GET_UPVALUE);
        emit_byte(compiler, (uint8_t)upvalue);
        return;
    }

    // Global
    int idx = chunk_add_identifier(compiler->builder->chunk, name);
    emit_byte(compiler, BC_GET_GLOBAL);
    emit_short(compiler, idx);
}

static void compile_binary(Compiler *compiler, Expr *expr) {
    BinaryOp op = expr->as.binary.op;

    // Short-circuit operators need special handling
    if (op == OP_AND) {
        compile_expression(compiler, expr->as.binary.left);
        int end_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
        emit_byte(compiler, BC_POP);
        compile_expression(compiler, expr->as.binary.right);
        patch_jump(compiler, end_jump);
        return;
    }

    if (op == OP_OR) {
        compile_expression(compiler, expr->as.binary.left);
        int else_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
        int end_jump = emit_jump(compiler, BC_JUMP);
        patch_jump(compiler, else_jump);
        emit_byte(compiler, BC_POP);
        compile_expression(compiler, expr->as.binary.right);
        patch_jump(compiler, end_jump);
        return;
    }

    // Regular binary ops: compile both operands, then operator
    compile_expression(compiler, expr->as.binary.left);
    compile_expression(compiler, expr->as.binary.right);

    switch (op) {
        case OP_ADD: emit_byte(compiler, BC_ADD); break;
        case OP_SUB: emit_byte(compiler, BC_SUB); break;
        case OP_MUL: emit_byte(compiler, BC_MUL); break;
        case OP_DIV: emit_byte(compiler, BC_DIV); break;
        case OP_MOD: emit_byte(compiler, BC_MOD); break;

        case OP_EQUAL: emit_byte(compiler, BC_EQ); break;
        case OP_NOT_EQUAL: emit_byte(compiler, BC_NE); break;
        case OP_LESS: emit_byte(compiler, BC_LT); break;
        case OP_LESS_EQUAL: emit_byte(compiler, BC_LE); break;
        case OP_GREATER: emit_byte(compiler, BC_GT); break;
        case OP_GREATER_EQUAL: emit_byte(compiler, BC_GE); break;

        case OP_BIT_AND: emit_byte(compiler, BC_BIT_AND); break;
        case OP_BIT_OR: emit_byte(compiler, BC_BIT_OR); break;
        case OP_BIT_XOR: emit_byte(compiler, BC_BIT_XOR); break;
        case OP_BIT_LSHIFT: emit_byte(compiler, BC_LSHIFT); break;
        case OP_BIT_RSHIFT: emit_byte(compiler, BC_RSHIFT); break;

        default:
            compiler_error(compiler, "Unknown binary operator");
    }
}

static void compile_unary(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.unary.operand);

    switch (expr->as.unary.op) {
        case UNARY_NEGATE:
            emit_byte(compiler, BC_NEGATE);
            break;
        case UNARY_NOT:
            emit_byte(compiler, BC_NOT);
            break;
        case UNARY_BIT_NOT:
            emit_byte(compiler, BC_BIT_NOT);
            break;
        default:
            compiler_error(compiler, "Unknown unary operator");
    }
}

static void compile_ternary(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.ternary.condition);

    int then_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
    emit_byte(compiler, BC_POP);  // Pop condition

    compile_expression(compiler, expr->as.ternary.true_expr);

    int else_jump = emit_jump(compiler, BC_JUMP);
    patch_jump(compiler, then_jump);
    emit_byte(compiler, BC_POP);  // Pop condition

    compile_expression(compiler, expr->as.ternary.false_expr);

    patch_jump(compiler, else_jump);
}

static void compile_assign(Compiler *compiler, Expr *expr) {
    const char *name = expr->as.assign.name;

    // Compile the value first
    compile_expression(compiler, expr->as.assign.value);

    // Check for resolved local
    if (expr->as.assign.resolved.is_resolved) {
        int depth = expr->as.assign.resolved.depth;
        int slot = expr->as.assign.resolved.slot;

        if (depth == 0) {
            emit_byte(compiler, BC_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
        } else {
            int upvalue = builder_resolve_upvalue(compiler->builder, name);
            emit_byte(compiler, BC_SET_UPVALUE);
            emit_byte(compiler, (uint8_t)upvalue);
        }
        return;
    }

    // Try local
    int local = builder_resolve_local(compiler->builder, name);
    if (local != -1) {
        emit_byte(compiler, BC_SET_LOCAL);
        emit_byte(compiler, (uint8_t)local);
        return;
    }

    // Try upvalue
    int upvalue = builder_resolve_upvalue(compiler->builder, name);
    if (upvalue != -1) {
        emit_byte(compiler, BC_SET_UPVALUE);
        emit_byte(compiler, (uint8_t)upvalue);
        return;
    }

    // Global
    int idx = chunk_add_identifier(compiler->builder->chunk, name);
    emit_byte(compiler, BC_SET_GLOBAL);
    emit_short(compiler, idx);
}

static void compile_call(Compiler *compiler, Expr *expr) {
    Expr *func = expr->as.call.func;
    int argc = expr->as.call.num_args;

    // Check for builtin calls
    if (func->type == EXPR_IDENT) {
        const char *name = func->as.ident.name;

        // Check if it's a builtin
        BuiltinId builtin = builtin_lookup(name);
        if (builtin != (BuiltinId)-1) {
            // Compile arguments
            for (int i = 0; i < argc; i++) {
                compile_expression(compiler, expr->as.call.args[i]);
            }

            // Special case for print (it has its own opcode)
            if (builtin == BUILTIN_PRINT) {
                emit_byte(compiler, BC_PRINT);
                emit_byte(compiler, (uint8_t)argc);
                return;
            }

            // General builtin call
            emit_byte(compiler, BC_CALL_BUILTIN);
            emit_short(compiler, (uint16_t)builtin);
            emit_byte(compiler, (uint8_t)argc);
            return;
        }
    }

    // Check for method call: obj.method(args)
    if (func->type == EXPR_GET_PROPERTY) {
        // Compile the receiver object
        compile_expression(compiler, func->as.get_property.object);

        // Compile arguments
        for (int i = 0; i < argc; i++) {
            compile_expression(compiler, expr->as.call.args[i]);
        }

        // Emit method call with method name
        emit_byte(compiler, BC_CALL_METHOD);
        const char *method = func->as.get_property.property;
        int idx = chunk_add_string(compiler->builder->chunk, method, strlen(method));
        emit_short(compiler, idx);
        emit_byte(compiler, (uint8_t)argc);
        return;
    }

    // General function call
    compile_expression(compiler, func);

    // Compile arguments
    for (int i = 0; i < argc; i++) {
        compile_expression(compiler, expr->as.call.args[i]);
    }

    emit_byte(compiler, BC_CALL);
    emit_byte(compiler, (uint8_t)argc);
}

static void compile_array_literal(Compiler *compiler, Expr *expr) {
    int count = expr->as.array_literal.num_elements;

    // Compile each element
    for (int i = 0; i < count; i++) {
        compile_expression(compiler, expr->as.array_literal.elements[i]);
    }

    emit_byte(compiler, BC_ARRAY);
    emit_short(compiler, (uint16_t)count);
}

static void compile_object_literal(Compiler *compiler, Expr *expr) {
    int count = expr->as.object_literal.num_fields;

    // Compile each key-value pair
    for (int i = 0; i < count; i++) {
        // Key as string constant
        const char *key = expr->as.object_literal.field_names[i];
        int idx = chunk_add_string(compiler->builder->chunk, key, strlen(key));
        emit_byte(compiler, BC_CONST);
        emit_short(compiler, idx);

        // Value
        compile_expression(compiler, expr->as.object_literal.field_values[i]);
    }

    emit_byte(compiler, BC_OBJECT);
    emit_short(compiler, (uint16_t)count);
}

static void compile_get_property(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.get_property.object);

    const char *name = expr->as.get_property.property;
    int idx = chunk_add_identifier(compiler->builder->chunk, name);
    emit_byte(compiler, BC_GET_PROPERTY);
    emit_short(compiler, idx);
}

static void compile_set_property(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.set_property.object);
    compile_expression(compiler, expr->as.set_property.value);

    const char *name = expr->as.set_property.property;
    int idx = chunk_add_identifier(compiler->builder->chunk, name);
    emit_byte(compiler, BC_SET_PROPERTY);
    emit_short(compiler, idx);
}

static void compile_index(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.index.object);
    compile_expression(compiler, expr->as.index.index);
    emit_byte(compiler, BC_GET_INDEX);
}

static void compile_index_assign(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.index_assign.object);
    compile_expression(compiler, expr->as.index_assign.index);
    compile_expression(compiler, expr->as.index_assign.value);
    emit_byte(compiler, BC_SET_INDEX);
}

// Helper to compile increment/decrement for different operand types
// is_increment: true for ++, false for --
// is_prefix: true for ++a/--a (return new value), false for a++/a-- (return old value)
static void compile_inc_dec(Compiler *compiler, Expr *operand, bool is_increment, bool is_prefix) {
    if (operand->type == EXPR_IDENT) {
        const char *name = operand->as.ident.name;
        int slot = builder_resolve_local(compiler->builder, name);

        if (slot != -1) {
            // Local variable
            emit_byte(compiler, BC_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);

            if (!is_prefix) {
                // Postfix: duplicate old value for return
                emit_byte(compiler, BC_DUP);
            }

            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);

            if (is_prefix) {
                // Prefix: duplicate new value for return
                emit_byte(compiler, BC_DUP);
            }

            emit_byte(compiler, BC_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, BC_POP);  // Pop the set result
        } else {
            // Global variable
            int idx = chunk_add_identifier(compiler->builder->chunk, name);
            emit_byte(compiler, BC_GET_GLOBAL);
            emit_short(compiler, idx);

            if (!is_prefix) {
                emit_byte(compiler, BC_DUP);
            }

            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);

            if (is_prefix) {
                emit_byte(compiler, BC_DUP);
            }

            emit_byte(compiler, BC_SET_GLOBAL);
            emit_short(compiler, idx);
            emit_byte(compiler, BC_POP);  // Pop the set value
        }
    } else if (operand->type == EXPR_INDEX) {
        // arr[idx]++ or ++arr[idx]
        compile_expression(compiler, operand->as.index.object);
        compile_expression(compiler, operand->as.index.index);

        // Duplicate object and index for later SET_INDEX
        emit_byte(compiler, BC_DUP2);  // [arr, idx, arr, idx]

        // Get current value
        emit_byte(compiler, BC_GET_INDEX);  // [arr, idx, old]

        if (is_prefix) {
            // Prefix: compute new value and set
            // [arr, idx, old]
            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);
            // [arr, idx, new]
            emit_byte(compiler, BC_SET_INDEX);
            // SET_INDEX pops [new, idx, arr], sets arr[idx]=new, pushes new
            // [new] - this is our result
        } else {
            // Postfix: save old, compute new, rearrange, set
            // [arr, idx, old]
            emit_byte(compiler, BC_DUP);  // [arr, idx, old, old]
            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);
            // [arr, idx, old, new]
            emit_byte(compiler, BC_BURY3);
            // [old, arr, idx, new]
            emit_byte(compiler, BC_SET_INDEX);
            // [old, new]
            emit_byte(compiler, BC_POP);
            // [old] - this is our result
        }
    } else if (operand->type == EXPR_GET_PROPERTY) {
        // obj.x++ or ++obj.x
        int prop_idx = chunk_add_string(compiler->builder->chunk,
            operand->as.get_property.property,
            strlen(operand->as.get_property.property));

        compile_expression(compiler, operand->as.get_property.object);
        emit_byte(compiler, BC_DUP);  // [obj, obj]

        // Get current value
        emit_byte(compiler, BC_GET_PROPERTY);
        emit_short(compiler, prop_idx);  // [obj, old]

        if (is_prefix) {
            // Prefix: compute new and set
            // [obj, old]
            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);
            // [obj, new]
            emit_byte(compiler, BC_SET_PROPERTY);
            emit_short(compiler, prop_idx);
            // SET_PROPERTY pops [new, obj], sets obj.x=new, pushes new
            // [new] - this is our result
        } else {
            // Postfix: save old, compute new, rearrange, set
            // [obj, old]
            emit_byte(compiler, BC_DUP);  // [obj, old, old]
            emit_byte(compiler, BC_CONST_BYTE);
            emit_byte(compiler, 1);
            emit_byte(compiler, is_increment ? BC_ADD : BC_SUB);
            // [obj, old, new]
            emit_byte(compiler, BC_ROT3);
            // [old, new, obj]
            emit_byte(compiler, BC_SWAP);
            // [old, obj, new]
            emit_byte(compiler, BC_SET_PROPERTY);
            emit_short(compiler, prop_idx);
            // [old, new]
            emit_byte(compiler, BC_POP);
            // [old] - this is our result
        }
    } else {
        compiler_error(compiler, "Invalid operand for increment/decrement");
        emit_byte(compiler, BC_NULL);
    }
}

static void compile_prefix_inc(Compiler *compiler, Expr *expr) {
    compile_inc_dec(compiler, expr->as.prefix_inc.operand, true, true);
}

static void compile_prefix_dec(Compiler *compiler, Expr *expr) {
    compile_inc_dec(compiler, expr->as.prefix_dec.operand, false, true);
}

static void compile_postfix_inc(Compiler *compiler, Expr *expr) {
    compile_inc_dec(compiler, expr->as.postfix_inc.operand, true, false);
}

static void compile_postfix_dec(Compiler *compiler, Expr *expr) {
    compile_inc_dec(compiler, expr->as.postfix_dec.operand, false, false);
}

static void compile_string_interpolation(Compiler *compiler, Expr *expr) {
    int num_parts = expr->as.string_interpolation.num_parts;
    char **string_parts = expr->as.string_interpolation.string_parts;
    Expr **expr_parts = expr->as.string_interpolation.expr_parts;

    // String interpolation creates a string from alternating literal parts and expressions
    // string_parts has num_parts + 1 elements (strings between expressions)
    // expr_parts has num_parts elements (the ${...} expressions)

    // Push all parts onto the stack: str0, expr0, str1, expr1, ..., strN
    int total_parts = 0;
    for (int i = 0; i <= num_parts; i++) {
        // Push string part (even if empty)
        if (string_parts[i] && strlen(string_parts[i]) > 0) {
            int idx = chunk_add_string(compiler->builder->chunk, string_parts[i], strlen(string_parts[i]));
            emit_byte(compiler, BC_CONST);
            emit_short(compiler, idx);
            total_parts++;
        }

        // Push expression part (if not at the end)
        if (i < num_parts) {
            compile_expression(compiler, expr_parts[i]);
            total_parts++;
        }
    }

    emit_byte(compiler, BC_STRING_INTERP);
    emit_short(compiler, (uint16_t)total_parts);
}

static void compile_null_coalesce(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr->as.null_coalesce.left);
    // If not null, skip the right side
    int end_jump = emit_jump(compiler, BC_COALESCE);
    emit_byte(compiler, BC_POP);
    compile_expression(compiler, expr->as.null_coalesce.right);
    patch_jump(compiler, end_jump);
}

static void compile_optional_chain(Compiler *compiler, Expr *expr) {
    // Compile the object expression
    compile_expression(compiler, expr->as.optional_chain.object);

    // If null, jump to end (keeping null on stack)
    int end_jump = emit_jump(compiler, BC_OPTIONAL_CHAIN);

    // Object is not null - perform the operation
    if (expr->as.optional_chain.is_call) {
        // Method call: obj?.method(args)
        const char *method_name = expr->as.optional_chain.property;
        int idx = chunk_add_identifier(compiler->builder->chunk, method_name);

        // Compile arguments
        for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
            compile_expression(compiler, expr->as.optional_chain.args[i]);
        }

        emit_byte(compiler, BC_CALL_METHOD);
        emit_short(compiler, idx);
        emit_byte(compiler, expr->as.optional_chain.num_args);
    } else if (expr->as.optional_chain.is_property) {
        // Property access: obj?.prop
        const char *name = expr->as.optional_chain.property;
        int idx = chunk_add_identifier(compiler->builder->chunk, name);
        emit_byte(compiler, BC_GET_PROPERTY);
        emit_short(compiler, idx);
    } else {
        // Index access: obj?.[index]
        compile_expression(compiler, expr->as.optional_chain.index);
        emit_byte(compiler, BC_GET_INDEX);
    }

    patch_jump(compiler, end_jump);
}

static void compile_function(Compiler *compiler, Expr *expr) {
    // Create a new compiler for the function body
    Compiler *fn_compiler = malloc(sizeof(Compiler));
    fn_compiler->builder = chunk_builder_new(compiler->builder);
    fn_compiler->enclosing = compiler;
    fn_compiler->function_name = NULL;
    fn_compiler->is_async = expr->as.function.is_async;
    fn_compiler->current_line = expr->line;
    fn_compiler->had_error = false;
    fn_compiler->panic_mode = false;

    // Set function metadata
    fn_compiler->builder->chunk->arity = expr->as.function.num_params;
    fn_compiler->builder->chunk->optional_count = 0;
    fn_compiler->builder->chunk->has_rest_param = expr->as.function.rest_param != NULL;
    fn_compiler->builder->chunk->is_async = expr->as.function.is_async;

    // Begin the function scope
    builder_begin_scope(fn_compiler->builder);

    // Reserve slot 0 for the function/closure itself
    builder_declare_local(fn_compiler->builder, "", false, TYPE_ID_NULL);
    builder_mark_initialized(fn_compiler->builder);

    // Declare parameters as locals (starting at slot 1)
    for (int i = 0; i < expr->as.function.num_params; i++) {
        builder_declare_local(fn_compiler->builder,
                              expr->as.function.param_names[i],
                              false, TYPE_ID_NULL);
        builder_mark_initialized(fn_compiler->builder);

        // Count optional params
        if (expr->as.function.param_defaults &&
            expr->as.function.param_defaults[i]) {
            fn_compiler->builder->chunk->optional_count++;
        }
    }

    // Declare rest parameter if present (comes after all regular params)
    if (expr->as.function.rest_param) {
        builder_declare_local(fn_compiler->builder,
                              expr->as.function.rest_param,
                              false, TYPE_ID_NULL);
        builder_mark_initialized(fn_compiler->builder);
    }

    // Emit default value initialization for optional parameters
    // Parameter slots are 1, 2, 3, ... (slot 0 is the closure)
    for (int i = 0; i < expr->as.function.num_params; i++) {
        if (expr->as.function.param_defaults &&
            expr->as.function.param_defaults[i]) {
            // Get current parameter value
            emit_byte(fn_compiler, BC_GET_LOCAL);
            emit_byte(fn_compiler, (uint8_t)(i + 1));  // +1 because slot 0 is closure

            // If not null, skip default assignment
            int skip_jump = emit_jump(fn_compiler, BC_COALESCE);

            // Pop the null value
            emit_byte(fn_compiler, BC_POP);

            // Compile the default expression
            compile_expression(fn_compiler, expr->as.function.param_defaults[i]);

            // Store in parameter slot
            emit_byte(fn_compiler, BC_SET_LOCAL);
            emit_byte(fn_compiler, (uint8_t)(i + 1));

            // Patch the skip jump
            patch_jump(fn_compiler, skip_jump);

            // Pop the value (either from coalesce or set_local)
            emit_byte(fn_compiler, BC_POP);
        }
    }

    // Compile the function body
    if (expr->as.function.body) {
        if (expr->as.function.body->type == STMT_BLOCK) {
            // Compile block statements directly (don't create nested scope)
            for (int i = 0; i < expr->as.function.body->as.block.count; i++) {
                compile_statement(fn_compiler, expr->as.function.body->as.block.statements[i]);
            }
        } else {
            compile_statement(fn_compiler, expr->as.function.body);
        }
    }

    // Implicit return null if no explicit return
    emit_byte(fn_compiler, BC_NULL);
    emit_byte(fn_compiler, BC_RETURN);

    // End scope (will emit POPs for locals)
    builder_end_scope(fn_compiler->builder);

    // Finish building the function chunk
    Chunk *fn_chunk = chunk_builder_finish(fn_compiler->builder);
    fn_compiler->builder = NULL;

    // Check for compilation errors
    if (fn_compiler->had_error) {
        compiler->had_error = true;
    }

    free(fn_compiler);

    // Add the function to the constant pool and emit closure
    int fn_index = chunk_add_function(compiler->builder->chunk, fn_chunk);
    emit_byte(compiler, BC_CLOSURE);
    emit_short(compiler, fn_index);
    emit_byte(compiler, (uint8_t)fn_chunk->upvalue_count);  // Upvalue count

    // Emit upvalue info
    for (int i = 0; i < fn_chunk->upvalue_count; i++) {
        emit_byte(compiler, fn_chunk->upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler, fn_chunk->upvalues[i].index);
    }
}

static void compile_expression(Compiler *compiler, Expr *expr) {
    if (!expr) {
        emit_byte(compiler, BC_NULL);
        return;
    }

    compiler->current_line = expr->line;

    switch (expr->type) {
        case EXPR_NUMBER:
            compile_number(compiler, expr);
            break;
        case EXPR_BOOL:
            compile_bool(compiler, expr);
            break;
        case EXPR_STRING:
            compile_string(compiler, expr);
            break;
        case EXPR_RUNE:
            compile_rune(compiler, expr);
            break;
        case EXPR_IDENT:
            compile_identifier(compiler, expr);
            break;
        case EXPR_NULL:
            compile_null(compiler);
            break;
        case EXPR_BINARY:
            compile_binary(compiler, expr);
            break;
        case EXPR_UNARY:
            compile_unary(compiler, expr);
            break;
        case EXPR_TERNARY:
            compile_ternary(compiler, expr);
            break;
        case EXPR_CALL:
            compile_call(compiler, expr);
            break;
        case EXPR_ASSIGN:
            compile_assign(compiler, expr);
            break;
        case EXPR_GET_PROPERTY:
            compile_get_property(compiler, expr);
            break;
        case EXPR_SET_PROPERTY:
            compile_set_property(compiler, expr);
            break;
        case EXPR_INDEX:
            compile_index(compiler, expr);
            break;
        case EXPR_INDEX_ASSIGN:
            compile_index_assign(compiler, expr);
            break;
        case EXPR_ARRAY_LITERAL:
            compile_array_literal(compiler, expr);
            break;
        case EXPR_OBJECT_LITERAL:
            compile_object_literal(compiler, expr);
            break;
        case EXPR_PREFIX_INC:
            compile_prefix_inc(compiler, expr);
            break;
        case EXPR_PREFIX_DEC:
            compile_prefix_dec(compiler, expr);
            break;
        case EXPR_POSTFIX_INC:
            compile_postfix_inc(compiler, expr);
            break;
        case EXPR_POSTFIX_DEC:
            compile_postfix_dec(compiler, expr);
            break;
        case EXPR_NULL_COALESCE:
            compile_null_coalesce(compiler, expr);
            break;
        case EXPR_OPTIONAL_CHAIN:
            compile_optional_chain(compiler, expr);
            break;
        case EXPR_STRING_INTERPOLATION:
            compile_string_interpolation(compiler, expr);
            break;
        case EXPR_FUNCTION:
            compile_function(compiler, expr);
            break;
        case EXPR_AWAIT:
            // TODO: Compile await
            compile_expression(compiler, expr->as.await_expr.awaited_expr);
            emit_byte(compiler, BC_AWAIT);
            break;
        default:
            compiler_error(compiler, "Unsupported expression type");
            emit_byte(compiler, BC_NULL);
    }
}

// ============================================
// Statement Compilation
// ============================================

static void compile_let(Compiler *compiler, Stmt *stmt, bool is_const) {
    const char *name = stmt->as.let.name;

    // Compile initializer
    if (stmt->as.let.value) {
        compile_expression(compiler, stmt->as.let.value);
    } else {
        emit_byte(compiler, BC_NULL);
    }

    // Cast to declared type if type annotation is present
    if (stmt->as.let.type_annotation && stmt->as.let.type_annotation->kind != TYPE_INFER) {
        int type_id = type_kind_to_id(stmt->as.let.type_annotation->kind);
        if (type_id >= 0) {
            emit_byte(compiler, BC_CAST);
            emit_byte(compiler, (uint8_t)type_id);
        }
        // Set custom object type name for typeof()
        if (stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
            stmt->as.let.type_annotation->type_name) {
            int idx = chunk_add_identifier(compiler->builder->chunk,
                                           stmt->as.let.type_annotation->type_name);
            emit_byte(compiler, BC_SET_OBJ_TYPE);
            emit_short(compiler, idx);
        }
    }

    // Define variable
    if (compiler->builder->scope_depth > 0) {
        // Local variable
        int slot = builder_declare_local(compiler->builder, name, is_const, TYPE_ID_NULL);
        if (slot == -1) {
            compiler_error(compiler, "Variable already declared in this scope");
        }
        builder_mark_initialized(compiler->builder);
        // Value is already on stack in the right slot
    } else {
        // Global variable
        int idx = chunk_add_identifier(compiler->builder->chunk, name);
        emit_byte(compiler, BC_DEFINE_GLOBAL);
        emit_short(compiler, idx);
    }
}

static void compile_expr_stmt(Compiler *compiler, Stmt *stmt) {
    compile_expression(compiler, stmt->as.expr);
    emit_byte(compiler, BC_POP);
}

static void compile_if(Compiler *compiler, Stmt *stmt) {
    compile_expression(compiler, stmt->as.if_stmt.condition);

    int then_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
    emit_byte(compiler, BC_POP);  // Pop condition

    compile_statement(compiler, stmt->as.if_stmt.then_branch);

    int else_jump = emit_jump(compiler, BC_JUMP);
    patch_jump(compiler, then_jump);
    emit_byte(compiler, BC_POP);  // Pop condition

    if (stmt->as.if_stmt.else_branch) {
        compile_statement(compiler, stmt->as.if_stmt.else_branch);
    }

    patch_jump(compiler, else_jump);
}

static void compile_while(Compiler *compiler, Stmt *stmt) {
    int loop_start = compiler->builder->chunk->code_count;
    builder_begin_loop(compiler->builder);
    // For while loops, continue jumps directly to condition check
    builder_set_continue_target(compiler->builder);

    compile_expression(compiler, stmt->as.while_stmt.condition);

    int exit_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
    emit_byte(compiler, BC_POP);  // Pop condition

    compile_statement(compiler, stmt->as.while_stmt.body);

    emit_loop(compiler, loop_start);

    patch_jump(compiler, exit_jump);
    emit_byte(compiler, BC_POP);  // Pop condition

    builder_end_loop(compiler->builder);
}

static void compile_for(Compiler *compiler, Stmt *stmt) {
    builder_begin_scope(compiler->builder);

    // Initializer
    if (stmt->as.for_loop.initializer) {
        compile_statement(compiler, stmt->as.for_loop.initializer);
    }

    int loop_start = compiler->builder->chunk->code_count;
    builder_begin_loop(compiler->builder);
    // Note: continue_target left as -1, will be set before increment

    // Condition
    int exit_jump = -1;
    if (stmt->as.for_loop.condition) {
        compile_expression(compiler, stmt->as.for_loop.condition);
        exit_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
        emit_byte(compiler, BC_POP);
    }

    // Body
    compile_statement(compiler, stmt->as.for_loop.body);

    // Set continue target here (before increment)
    // This patches any pending continue jumps to this point
    builder_set_continue_target(compiler->builder);

    // Increment
    if (stmt->as.for_loop.increment) {
        compile_expression(compiler, stmt->as.for_loop.increment);
        emit_byte(compiler, BC_POP);
    }

    emit_loop(compiler, loop_start);

    if (exit_jump != -1) {
        patch_jump(compiler, exit_jump);
        emit_byte(compiler, BC_POP);
    }

    builder_end_loop(compiler->builder);
    builder_end_scope(compiler->builder);
}

static void compile_block(Compiler *compiler, Stmt *stmt) {
    builder_begin_scope(compiler->builder);
    for (int i = 0; i < stmt->as.block.count; i++) {
        compile_statement(compiler, stmt->as.block.statements[i]);
    }
    builder_end_scope(compiler->builder);
}

static void compile_return(Compiler *compiler, Stmt *stmt) {
    if (stmt->as.return_stmt.value) {
        compile_expression(compiler, stmt->as.return_stmt.value);
    } else {
        emit_byte(compiler, BC_NULL);
    }
    emit_byte(compiler, BC_RETURN);
}

static void compile_break(Compiler *compiler) {
    builder_emit_break(compiler->builder);
}

static void compile_continue(Compiler *compiler) {
    builder_emit_continue(compiler->builder);
}

static void compile_for_in(Compiler *compiler, Stmt *stmt) {
    builder_begin_scope(compiler->builder);

    // Compile iterable expression -> array/object on stack
    compile_expression(compiler, stmt->as.for_in.iterable);

    // Reserve hidden local for iterable
    int iterable_slot = builder_declare_local(compiler->builder, " iter", false, TYPE_ID_NULL);
    builder_mark_initialized(compiler->builder);

    // Push initial index 0 and reserve local
    emit_byte(compiler, BC_CONST_BYTE);
    emit_byte(compiler, 0);
    int index_slot = builder_declare_local(compiler->builder, " idx", false, TYPE_ID_NULL);
    builder_mark_initialized(compiler->builder);

    // Declare key variable if present (for "for (let key, value in obj)")
    int key_slot = -1;
    if (stmt->as.for_in.key_var) {
        emit_byte(compiler, BC_NULL);
        key_slot = builder_declare_local(compiler->builder, stmt->as.for_in.key_var, false, TYPE_ID_NULL);
        builder_mark_initialized(compiler->builder);
    }

    // Push placeholder for value variable and reserve local
    emit_byte(compiler, BC_NULL);
    int var_slot = builder_declare_local(compiler->builder, stmt->as.for_in.value_var, false, TYPE_ID_NULL);
    builder_mark_initialized(compiler->builder);

    // Loop start: check if index < iterable.length
    int loop_start = compiler->builder->chunk->code_count;
    builder_begin_loop(compiler->builder);

    // Check: index < iterable.length
    emit_byte(compiler, BC_GET_LOCAL);
    emit_byte(compiler, (uint8_t)index_slot);
    emit_byte(compiler, BC_GET_LOCAL);
    emit_byte(compiler, (uint8_t)iterable_slot);
    emit_byte(compiler, BC_GET_PROPERTY);
    int len_idx = chunk_add_identifier(compiler->builder->chunk, "length");
    emit_short(compiler, len_idx);
    emit_byte(compiler, BC_LT);  // index < length

    int exit_jump = emit_jump(compiler, BC_JUMP_IF_FALSE);
    emit_byte(compiler, BC_POP);  // Pop condition

    // Set key variable if present (for objects: key = field name at index)
    if (key_slot >= 0) {
        // We use BC_CALL_BUILTIN with "key_at" to get the key at the index
        // Actually, let's use a simpler approach: call keys() method and index into it
        // But that's expensive. Instead, let's add a special opcode.
        // For now, use BC_GET_KEY opcode (we'll add it)
        emit_byte(compiler, BC_GET_LOCAL);
        emit_byte(compiler, (uint8_t)iterable_slot);
        emit_byte(compiler, BC_GET_LOCAL);
        emit_byte(compiler, (uint8_t)index_slot);
        emit_byte(compiler, BC_GET_KEY);
        emit_byte(compiler, BC_SET_LOCAL);
        emit_byte(compiler, (uint8_t)key_slot);
        emit_byte(compiler, BC_POP);
    }

    // Set value variable: x = iterable[index]
    emit_byte(compiler, BC_GET_LOCAL);
    emit_byte(compiler, (uint8_t)iterable_slot);
    emit_byte(compiler, BC_GET_LOCAL);
    emit_byte(compiler, (uint8_t)index_slot);
    emit_byte(compiler, BC_GET_INDEX);
    emit_byte(compiler, BC_SET_LOCAL);
    emit_byte(compiler, (uint8_t)var_slot);
    emit_byte(compiler, BC_POP);  // Pop assignment result

    // Compile body
    compile_statement(compiler, stmt->as.for_in.body);

    // Set continue target here (after body, before increment)
    // This patches any pending continue jumps to this point
    builder_set_continue_target(compiler->builder);

    // Increment index: index = index + 1
    emit_byte(compiler, BC_GET_LOCAL);
    emit_byte(compiler, (uint8_t)index_slot);
    emit_byte(compiler, BC_CONST_BYTE);
    emit_byte(compiler, 1);
    emit_byte(compiler, BC_ADD);
    emit_byte(compiler, BC_SET_LOCAL);
    emit_byte(compiler, (uint8_t)index_slot);
    emit_byte(compiler, BC_POP);  // Pop assignment result

    // Loop back
    emit_loop(compiler, loop_start);

    // Exit point
    patch_jump(compiler, exit_jump);
    emit_byte(compiler, BC_POP);  // Pop condition

    builder_end_loop(compiler->builder);
    builder_end_scope(compiler->builder);
}

static void compile_switch(Compiler *compiler, Stmt *stmt) {
    // Create scope for switch (needed for hidden local at global scope)
    builder_begin_scope(compiler->builder);

    // Compile switch expression
    compile_expression(compiler, stmt->as.switch_stmt.expr);

    // Store in a hidden local so we can compare multiple times
    int switch_slot = builder_declare_local(compiler->builder, " switch", false, TYPE_ID_NULL);
    builder_mark_initialized(compiler->builder);

    // Register as pseudo-loop so break works (exits switch, not enclosing loop)
    builder_begin_loop(compiler->builder);

    int num_cases = stmt->as.switch_stmt.num_cases;
    int *case_jumps = malloc(sizeof(int) * num_cases);
    int default_idx = -1;

    // First pass: emit comparisons and conditional jumps to case bodies
    for (int i = 0; i < num_cases; i++) {
        if (stmt->as.switch_stmt.case_values[i] == NULL) {
            // Default case - remember index, handle after all comparisons
            default_idx = i;
            case_jumps[i] = -1;
            continue;
        }

        // Get switch value
        emit_byte(compiler, BC_GET_LOCAL);
        emit_byte(compiler, (uint8_t)switch_slot);

        // Compile case value
        compile_expression(compiler, stmt->as.switch_stmt.case_values[i]);

        // Compare
        emit_byte(compiler, BC_EQ);

        // Jump to case body if equal
        case_jumps[i] = emit_jump(compiler, BC_JUMP_IF_TRUE);
        emit_byte(compiler, BC_POP);  // Pop false comparison result
    }

    // No case matched - jump to default or end
    int default_jump = emit_jump(compiler, BC_JUMP);

    // Second pass: emit case bodies and patch jumps
    // Cases without break should fall through to the next case
    for (int i = 0; i < num_cases; i++) {
        if (i == default_idx) {
            // Patch default jump to here
            patch_jump(compiler, default_jump);
        } else if (case_jumps[i] >= 0) {
            // Patch case jump to here
            patch_jump(compiler, case_jumps[i]);
            emit_byte(compiler, BC_POP);  // Pop true comparison result
        }

        // Compile case body - no automatic jump at end (allows fallthrough)
        // break statements will jump to end via builder_end_loop
        if (stmt->as.switch_stmt.case_bodies[i]) {
            compile_statement(compiler, stmt->as.switch_stmt.case_bodies[i]);
        }
        // Fall through to next case (no jump emitted)
    }

    // If no default case, patch default_jump to end
    if (default_idx < 0) {
        patch_jump(compiler, default_jump);
    }

    // End pseudo-loop (for break support) - patches break jumps to here
    builder_end_loop(compiler->builder);

    // End switch scope (will pop the hidden local)
    builder_end_scope(compiler->builder);

    free(case_jumps);
}

static void compile_try(Compiler *compiler, Stmt *stmt) {
    Chunk *chunk = compiler->builder->chunk;

    // Emit BC_TRY with placeholder offsets for catch and finally
    emit_byte(compiler, BC_TRY);
    int catch_offset_pos = chunk->code_count;
    emit_short(compiler, 0);  // Placeholder for catch offset
    int finally_offset_pos = chunk->code_count;
    emit_short(compiler, 0);  // Placeholder for finally offset

    // Position after all BC_TRY operands (where ip will be during execution)
    int base_pos = chunk->code_count;

    // Compile try block
    compile_statement(compiler, stmt->as.try_stmt.try_block);

    // Jump to finally (not to end - finally always runs)
    int try_to_finally_jump = emit_jump(compiler, BC_JUMP);

    // Patch catch offset (relative to base_pos)
    int catch_offset = chunk->code_count - base_pos;
    chunk->code[catch_offset_pos] = (catch_offset >> 8) & 0xFF;
    chunk->code[catch_offset_pos + 1] = catch_offset & 0xFF;

    // Compile catch block (if present)
    if (stmt->as.try_stmt.catch_block) {
        emit_byte(compiler, BC_CATCH);

        // Begin a scope for the catch parameter
        builder_begin_scope(compiler->builder);

        // Declare catch parameter
        if (stmt->as.try_stmt.catch_param) {
            builder_declare_local(compiler->builder, stmt->as.try_stmt.catch_param, false, TYPE_ID_STRING);
            builder_mark_initialized(compiler->builder);
        } else {
            // No parameter - just pop the exception
            emit_byte(compiler, BC_POP);
        }

        compile_statement(compiler, stmt->as.try_stmt.catch_block);
        builder_end_scope(compiler->builder);
    }
    // Note: catch block falls through to finally

    // Patch finally offset (relative to base_pos)
    int finally_offset = chunk->code_count - base_pos;
    chunk->code[finally_offset_pos] = (finally_offset >> 8) & 0xFF;
    chunk->code[finally_offset_pos + 1] = finally_offset & 0xFF;

    // Patch try jump to here (before finally)
    patch_jump(compiler, try_to_finally_jump);

    // Compile finally block (if present)
    if (stmt->as.try_stmt.finally_block) {
        emit_byte(compiler, BC_FINALLY);
        compile_statement(compiler, stmt->as.try_stmt.finally_block);
    }

    emit_byte(compiler, BC_END_TRY);
}

static void compile_throw(Compiler *compiler, Stmt *stmt) {
    compile_expression(compiler, stmt->as.throw_stmt.value);
    emit_byte(compiler, BC_THROW);
}

static void compile_defer(Compiler *compiler, Stmt *stmt) {
    // Create a closure that wraps the deferred call
    // The closure captures the current environment and will be called at function exit

    // Create a new compiler for the deferred expression
    Compiler *defer_compiler = malloc(sizeof(Compiler));
    defer_compiler->builder = chunk_builder_new(compiler->builder);
    defer_compiler->enclosing = compiler;
    defer_compiler->function_name = NULL;
    defer_compiler->is_async = false;
    defer_compiler->current_line = stmt->line;
    defer_compiler->had_error = false;
    defer_compiler->panic_mode = false;

    // Set function metadata (no params, not async)
    defer_compiler->builder->chunk->arity = 0;
    defer_compiler->builder->chunk->optional_count = 0;
    defer_compiler->builder->chunk->has_rest_param = false;
    defer_compiler->builder->chunk->is_async = false;

    // Begin the function scope
    builder_begin_scope(defer_compiler->builder);

    // Reserve slot 0 for the closure itself
    builder_declare_local(defer_compiler->builder, "", false, TYPE_ID_NULL);
    builder_mark_initialized(defer_compiler->builder);

    // Compile the deferred expression (usually a call)
    compile_expression(defer_compiler, stmt->as.defer_stmt.call);
    emit_byte(defer_compiler, BC_POP);  // Discard result

    // Return null
    emit_byte(defer_compiler, BC_NULL);
    emit_byte(defer_compiler, BC_RETURN);

    // End scope
    builder_end_scope(defer_compiler->builder);

    // Finish building the function chunk
    Chunk *defer_chunk = chunk_builder_finish(defer_compiler->builder);
    defer_compiler->builder = NULL;

    if (defer_compiler->had_error) {
        compiler->had_error = true;
    }

    free(defer_compiler);

    // Add the deferred function to the constant pool
    Constant c = {.type = CONST_FUNCTION, .as.function = defer_chunk};
    int idx = chunk_add_constant(compiler->builder->chunk, c);

    // Emit closure creation with no upvalues (for simplicity)
    emit_byte(compiler, BC_CLOSURE);
    emit_short(compiler, idx);
    emit_byte(compiler, 0);  // No upvalues

    // Register the closure as a deferred call
    emit_byte(compiler, BC_DEFER);
}

static void compile_statement(Compiler *compiler, Stmt *stmt) {
    if (!stmt) return;

    compiler->current_line = stmt->line;

    switch (stmt->type) {
        case STMT_LET:
            compile_let(compiler, stmt, false);
            break;
        case STMT_CONST:
            compile_let(compiler, stmt, true);
            break;
        case STMT_EXPR:
            compile_expr_stmt(compiler, stmt);
            break;
        case STMT_IF:
            compile_if(compiler, stmt);
            break;
        case STMT_WHILE:
            compile_while(compiler, stmt);
            break;
        case STMT_FOR:
            compile_for(compiler, stmt);
            break;
        case STMT_FOR_IN:
            compile_for_in(compiler, stmt);
            break;
        case STMT_BLOCK:
            compile_block(compiler, stmt);
            break;
        case STMT_RETURN:
            compile_return(compiler, stmt);
            break;
        case STMT_BREAK:
            compile_break(compiler);
            break;
        case STMT_CONTINUE:
            compile_continue(compiler);
            break;
        case STMT_SWITCH:
            compile_switch(compiler, stmt);
            break;
        case STMT_TRY:
            compile_try(compiler, stmt);
            break;
        case STMT_THROW:
            compile_throw(compiler, stmt);
            break;
        case STMT_DEFER:
            compile_defer(compiler, stmt);
            break;
        case STMT_ENUM: {
            // Create an object with enum variants
            // enum Color { RED, GREEN, BLUE } -> { RED: 0, GREEN: 1, BLUE: 2 }
            int num_variants = stmt->as.enum_decl.num_variants;
            int auto_value = 0;

            // Push each key-value pair
            for (int i = 0; i < num_variants; i++) {
                // Push variant name as string constant
                const char *name = stmt->as.enum_decl.variant_names[i];
                int idx = chunk_add_string(compiler->builder->chunk, name, strlen(name));
                emit_byte(compiler, BC_CONST);
                emit_short(compiler, idx);

                // Push variant value
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    compile_expression(compiler, stmt->as.enum_decl.variant_values[i]);
                } else {
                    emit_byte(compiler, BC_CONST_BYTE);
                    emit_byte(compiler, (uint8_t)auto_value);
                }
                auto_value++;
            }

            // Create the object with N pairs
            emit_byte(compiler, BC_OBJECT);
            emit_short(compiler, num_variants);

            // Define as global
            int name_idx = chunk_add_identifier(compiler->builder->chunk, stmt->as.enum_decl.name);
            emit_byte(compiler, BC_DEFINE_GLOBAL);
            emit_short(compiler, name_idx);
            break;
        }
        default:
            // TODO: Handle other statement types
            break;
    }
}

// ============================================
// Public API
// ============================================

void compile_stmt(Compiler *compiler, Stmt *stmt) {
    compile_statement(compiler, stmt);
}

void compile_expr(Compiler *compiler, Expr *expr) {
    compile_expression(compiler, expr);
}

Chunk* compile_program(Stmt **stmts, int count) {
    Compiler *compiler = compiler_new(NULL);

    // Compile all statements
    for (int i = 0; i < count; i++) {
        compile_statement(compiler, stmts[i]);
    }

    // End with return
    emit_byte(compiler, BC_NULL);
    emit_byte(compiler, BC_RETURN);

    if (compiler->had_error) {
        chunk_builder_free(compiler->builder);
        free(compiler);
        return NULL;
    }

    Chunk *chunk = chunk_builder_finish(compiler->builder);
    compiler->builder = NULL;
    free(compiler);

    return chunk;
}

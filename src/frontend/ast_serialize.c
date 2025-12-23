#include "ast_serialize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== INTERNAL HELPERS ==========

// Initial buffer capacity
#define INITIAL_BUFFER_SIZE 4096
#define INITIAL_STRING_TABLE_SIZE 256

// Marker for NULL pointers in serialized data
#define NULL_MARKER 0xFF

// Header size (magic + version + flags + string_count + stmt_count + checksum)
#define HEADER_SIZE 20
#define CHECKSUM_OFFSET 16

// Compute djb2 hash for checksum validation
static uint32_t compute_checksum(const uint8_t *data, size_t size) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + data[i];  // hash * 33 + c
    }
    return hash;
}

// ========== STRING TABLE ==========

static void string_table_init(StringTable *table) {
    table->strings = malloc(INITIAL_STRING_TABLE_SIZE * sizeof(char*));
    table->lengths = malloc(INITIAL_STRING_TABLE_SIZE * sizeof(uint32_t));
    if (!table->strings || !table->lengths) {
        fprintf(stderr, "Error: Failed to allocate string table\n");
        exit(1);
    }
    table->count = 0;
    table->capacity = INITIAL_STRING_TABLE_SIZE;
}

static void string_table_free(StringTable *table) {
    // Note: strings are borrowed, not owned - don't free them
    free(table->strings);
    free(table->lengths);
    table->strings = NULL;
    table->lengths = NULL;
    table->count = 0;
    table->capacity = 0;
}

// Add string to table, return its index
// Returns existing index if string already in table
static uint32_t string_table_add(StringTable *table, const char *str) {
    if (str == NULL) {
        return UINT32_MAX;  // Marker for NULL string
    }

    // Check if already in table
    for (uint32_t i = 0; i < table->count; i++) {
        if (strcmp(table->strings[i], str) == 0) {
            return i;
        }
    }

    // Add new entry
    if (table->count >= table->capacity) {
        // Handle zero capacity case (defensive)
        size_t new_capacity = (table->capacity == 0) ? INITIAL_STRING_TABLE_SIZE : table->capacity * 2;
        char **new_strings = realloc(table->strings, new_capacity * sizeof(char*));
        uint32_t *new_lengths = realloc(table->lengths, new_capacity * sizeof(uint32_t));
        if (!new_strings || !new_lengths) {
            fprintf(stderr, "Error: Failed to grow string table\n");
            exit(1);
        }
        table->strings = new_strings;
        table->lengths = new_lengths;
        table->capacity = new_capacity;
    }

    table->strings[table->count] = (char*)str;  // Borrow pointer
    table->lengths[table->count] = strlen(str);
    return table->count++;
}

// ========== SERIALIZATION BUFFER ==========

static void ctx_init(SerializeContext *ctx, uint16_t flags) {
    string_table_init(&ctx->strings);
    ctx->buffer = malloc(INITIAL_BUFFER_SIZE);
    if (!ctx->buffer) {
        fprintf(stderr, "Error: Failed to allocate serialization buffer\n");
        exit(1);
    }
    ctx->buffer_size = 0;
    ctx->buffer_capacity = INITIAL_BUFFER_SIZE;
    ctx->flags = flags;
}

static void __attribute__((unused)) ctx_free(SerializeContext *ctx) {
    string_table_free(&ctx->strings);
    // Note: buffer ownership is transferred to caller
    ctx->buffer = NULL;
    ctx->buffer_size = 0;
    ctx->buffer_capacity = 0;
}

static void ctx_ensure_capacity(SerializeContext *ctx, size_t additional) {
    while (ctx->buffer_size + additional > ctx->buffer_capacity) {
        ctx->buffer_capacity *= 2;
        ctx->buffer = realloc(ctx->buffer, ctx->buffer_capacity);
    }
}

// Write primitives
static void write_u8(SerializeContext *ctx, uint8_t val) {
    ctx_ensure_capacity(ctx, 1);
    ctx->buffer[ctx->buffer_size++] = val;
}

static void write_u16(SerializeContext *ctx, uint16_t val) {
    ctx_ensure_capacity(ctx, 2);
    ctx->buffer[ctx->buffer_size++] = val & 0xFF;
    ctx->buffer[ctx->buffer_size++] = (val >> 8) & 0xFF;
}

static void write_u32(SerializeContext *ctx, uint32_t val) {
    ctx_ensure_capacity(ctx, 4);
    ctx->buffer[ctx->buffer_size++] = val & 0xFF;
    ctx->buffer[ctx->buffer_size++] = (val >> 8) & 0xFF;
    ctx->buffer[ctx->buffer_size++] = (val >> 16) & 0xFF;
    ctx->buffer[ctx->buffer_size++] = (val >> 24) & 0xFF;
}

static void write_i64(SerializeContext *ctx, int64_t val) {
    ctx_ensure_capacity(ctx, 8);
    uint64_t uval = (uint64_t)val;
    for (int i = 0; i < 8; i++) {
        ctx->buffer[ctx->buffer_size++] = (uval >> (i * 8)) & 0xFF;
    }
}

static void write_f64(SerializeContext *ctx, double val) {
    ctx_ensure_capacity(ctx, 8);
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    for (int i = 0; i < 8; i++) {
        ctx->buffer[ctx->buffer_size++] = (bits >> (i * 8)) & 0xFF;
    }
}

static void write_string_id(SerializeContext *ctx, const char *str) {
    uint32_t id = string_table_add(&ctx->strings, str);
    write_u32(ctx, id);
}

static void write_bytes(SerializeContext *ctx, const void *data, size_t len) {
    ctx_ensure_capacity(ctx, len);
    memcpy(ctx->buffer + ctx->buffer_size, data, len);
    ctx->buffer_size += len;
}

// ========== DESERIALIZATION CONTEXT ==========

static void dctx_init(DeserializeContext *ctx, const uint8_t *data, size_t size) {
    ctx->data = data;
    ctx->data_size = size;
    ctx->offset = 0;
    ctx->strings = NULL;
    ctx->string_count = 0;
    ctx->flags = 0;
}

static void dctx_free(DeserializeContext *ctx) {
    if (ctx->strings) {
        for (uint32_t i = 0; i < ctx->string_count; i++) {
            free(ctx->strings[i]);
        }
        free(ctx->strings);
    }
    ctx->strings = NULL;
    ctx->string_count = 0;
}

static int dctx_has_bytes(DeserializeContext *ctx, size_t count) {
    return ctx->offset + count <= ctx->data_size;
}

static uint8_t read_u8(DeserializeContext *ctx) {
    if (!dctx_has_bytes(ctx, 1)) return 0;
    return ctx->data[ctx->offset++];
}

static uint16_t read_u16(DeserializeContext *ctx) {
    if (!dctx_has_bytes(ctx, 2)) return 0;
    uint16_t val = ctx->data[ctx->offset] |
                   ((uint16_t)ctx->data[ctx->offset + 1] << 8);
    ctx->offset += 2;
    return val;
}

static uint32_t read_u32(DeserializeContext *ctx) {
    if (!dctx_has_bytes(ctx, 4)) return 0;
    uint32_t val = ctx->data[ctx->offset] |
                   ((uint32_t)ctx->data[ctx->offset + 1] << 8) |
                   ((uint32_t)ctx->data[ctx->offset + 2] << 16) |
                   ((uint32_t)ctx->data[ctx->offset + 3] << 24);
    ctx->offset += 4;
    return val;
}

static int64_t read_i64(DeserializeContext *ctx) {
    if (!dctx_has_bytes(ctx, 8)) return 0;
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)ctx->data[ctx->offset + i] << (i * 8));
    }
    ctx->offset += 8;
    return (int64_t)val;
}

static double read_f64(DeserializeContext *ctx) {
    if (!dctx_has_bytes(ctx, 8)) return 0.0;
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        bits |= ((uint64_t)ctx->data[ctx->offset + i] << (i * 8));
    }
    ctx->offset += 8;
    double val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

static char* read_string_id(DeserializeContext *ctx) {
    uint32_t id = read_u32(ctx);
    if (id == UINT32_MAX) return NULL;
    if (id >= ctx->string_count) return NULL;
    return strdup(ctx->strings[id]);
}

// ========== FORWARD DECLARATIONS ==========

static void serialize_type(SerializeContext *ctx, Type *type);
static void serialize_expr(SerializeContext *ctx, Expr *expr);
static void serialize_stmt(SerializeContext *ctx, Stmt *stmt);

static Type* deserialize_type(DeserializeContext *ctx);
static Expr* deserialize_expr(DeserializeContext *ctx);
static Stmt* deserialize_stmt(DeserializeContext *ctx);

// ========== TYPE SERIALIZATION ==========

static void serialize_type(SerializeContext *ctx, Type *type) {
    if (type == NULL) {
        write_u8(ctx, NULL_MARKER);
        return;
    }

    write_u8(ctx, (uint8_t)type->kind);

    // Write type name for custom types
    if (type->kind == TYPE_CUSTOM_OBJECT || type->kind == TYPE_ENUM) {
        write_string_id(ctx, type->type_name);
    }

    // Write element type for arrays
    if (type->kind == TYPE_ARRAY) {
        serialize_type(ctx, type->element_type);
    }
}

static Type* deserialize_type(DeserializeContext *ctx) {
    uint8_t kind_byte = read_u8(ctx);
    if (kind_byte == NULL_MARKER) return NULL;

    Type *type = malloc(sizeof(Type));
    type->kind = (TypeKind)kind_byte;
    type->type_name = NULL;
    type->element_type = NULL;

    if (type->kind == TYPE_CUSTOM_OBJECT || type->kind == TYPE_ENUM) {
        type->type_name = read_string_id(ctx);
    }

    if (type->kind == TYPE_ARRAY) {
        type->element_type = deserialize_type(ctx);
    }

    return type;
}

// ========== EXPRESSION SERIALIZATION ==========

static void serialize_expr(SerializeContext *ctx, Expr *expr) {
    if (expr == NULL) {
        write_u8(ctx, NULL_MARKER);
        return;
    }

    write_u8(ctx, (uint8_t)expr->type);

    // Write line number if debug info requested
    if (ctx->flags & HMLC_FLAG_DEBUG) {
        write_u32(ctx, (uint32_t)expr->line);
    }

    switch (expr->type) {
        case EXPR_NUMBER:
            write_u8(ctx, expr->as.number.is_float);
            if (expr->as.number.is_float) {
                write_f64(ctx, expr->as.number.float_value);
            } else {
                write_i64(ctx, expr->as.number.int_value);
            }
            break;

        case EXPR_BOOL:
            write_u8(ctx, expr->as.boolean ? 1 : 0);
            break;

        case EXPR_STRING:
            write_string_id(ctx, expr->as.string);
            break;

        case EXPR_RUNE:
            write_u32(ctx, expr->as.rune);
            break;

        case EXPR_IDENT:
            write_string_id(ctx, expr->as.ident.name);
            // Note: resolution info is not serialized - it's recomputed after deserialization
            break;

        case EXPR_NULL:
            // No data needed
            break;

        case EXPR_BINARY:
            write_u8(ctx, (uint8_t)expr->as.binary.op);
            serialize_expr(ctx, expr->as.binary.left);
            serialize_expr(ctx, expr->as.binary.right);
            break;

        case EXPR_UNARY:
            write_u8(ctx, (uint8_t)expr->as.unary.op);
            serialize_expr(ctx, expr->as.unary.operand);
            break;

        case EXPR_TERNARY:
            serialize_expr(ctx, expr->as.ternary.condition);
            serialize_expr(ctx, expr->as.ternary.true_expr);
            serialize_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_CALL:
            serialize_expr(ctx, expr->as.call.func);
            write_u32(ctx, (uint32_t)expr->as.call.num_args);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                serialize_expr(ctx, expr->as.call.args[i]);
            }
            break;

        case EXPR_ASSIGN:
            write_string_id(ctx, expr->as.assign.name);
            serialize_expr(ctx, expr->as.assign.value);
            break;

        case EXPR_GET_PROPERTY:
            serialize_expr(ctx, expr->as.get_property.object);
            write_string_id(ctx, expr->as.get_property.property);
            break;

        case EXPR_SET_PROPERTY:
            serialize_expr(ctx, expr->as.set_property.object);
            write_string_id(ctx, expr->as.set_property.property);
            serialize_expr(ctx, expr->as.set_property.value);
            break;

        case EXPR_INDEX:
            serialize_expr(ctx, expr->as.index.object);
            serialize_expr(ctx, expr->as.index.index);
            break;

        case EXPR_INDEX_ASSIGN:
            serialize_expr(ctx, expr->as.index_assign.object);
            serialize_expr(ctx, expr->as.index_assign.index);
            serialize_expr(ctx, expr->as.index_assign.value);
            break;

        case EXPR_FUNCTION:
            write_u8(ctx, expr->as.function.is_async ? 1 : 0);
            write_u32(ctx, (uint32_t)expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                write_string_id(ctx, expr->as.function.param_names[i]);
                serialize_type(ctx, expr->as.function.param_types ? expr->as.function.param_types[i] : NULL);
                serialize_expr(ctx, expr->as.function.param_defaults ? expr->as.function.param_defaults[i] : NULL);
            }
            serialize_type(ctx, expr->as.function.return_type);
            serialize_stmt(ctx, expr->as.function.body);
            break;

        case EXPR_ARRAY_LITERAL:
            write_u32(ctx, (uint32_t)expr->as.array_literal.num_elements);
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                serialize_expr(ctx, expr->as.array_literal.elements[i]);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            write_u32(ctx, (uint32_t)expr->as.object_literal.num_fields);
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                write_string_id(ctx, expr->as.object_literal.field_names[i]);
                serialize_expr(ctx, expr->as.object_literal.field_values[i]);
            }
            break;

        case EXPR_PREFIX_INC:
            serialize_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_PREFIX_DEC:
            serialize_expr(ctx, expr->as.prefix_dec.operand);
            break;

        case EXPR_POSTFIX_INC:
            serialize_expr(ctx, expr->as.postfix_inc.operand);
            break;

        case EXPR_POSTFIX_DEC:
            serialize_expr(ctx, expr->as.postfix_dec.operand);
            break;

        case EXPR_AWAIT:
            serialize_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            write_u32(ctx, (uint32_t)expr->as.string_interpolation.num_parts);
            // Write num_parts + 1 string parts
            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                write_string_id(ctx, expr->as.string_interpolation.string_parts[i]);
            }
            // Write num_parts expression parts
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                serialize_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            serialize_expr(ctx, expr->as.optional_chain.object);
            write_u8(ctx, expr->as.optional_chain.is_property ? 1 : 0);
            write_u8(ctx, expr->as.optional_chain.is_call ? 1 : 0);
            if (expr->as.optional_chain.is_property) {
                write_string_id(ctx, expr->as.optional_chain.property);
            } else if (expr->as.optional_chain.is_call) {
                write_u32(ctx, (uint32_t)expr->as.optional_chain.num_args);
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    serialize_expr(ctx, expr->as.optional_chain.args[i]);
                }
            } else {
                serialize_expr(ctx, expr->as.optional_chain.index);
            }
            break;

        case EXPR_NULL_COALESCE:
            serialize_expr(ctx, expr->as.null_coalesce.left);
            serialize_expr(ctx, expr->as.null_coalesce.right);
            break;
    }
}

static Expr* deserialize_expr(DeserializeContext *ctx) {
    uint8_t type_byte = read_u8(ctx);
    if (type_byte == NULL_MARKER) return NULL;

    Expr *expr = malloc(sizeof(Expr));
    memset(expr, 0, sizeof(Expr));
    expr->type = (ExprType)type_byte;

    // Read line number if debug info present
    if (ctx->flags & HMLC_FLAG_DEBUG) {
        expr->line = (int)read_u32(ctx);
    } else {
        expr->line = 0;
    }

    switch (expr->type) {
        case EXPR_NUMBER:
            expr->as.number.is_float = read_u8(ctx);
            if (expr->as.number.is_float) {
                expr->as.number.float_value = read_f64(ctx);
            } else {
                expr->as.number.int_value = read_i64(ctx);
            }
            break;

        case EXPR_BOOL:
            expr->as.boolean = read_u8(ctx) ? 1 : 0;
            break;

        case EXPR_STRING:
            expr->as.string = read_string_id(ctx);
            break;

        case EXPR_RUNE:
            expr->as.rune = read_u32(ctx);
            break;

        case EXPR_IDENT:
            expr->as.ident.name = read_string_id(ctx);
            expr->as.ident.resolved.is_resolved = 0;  // Needs resolution after deserialization
            expr->as.ident.resolved.depth = 0;
            expr->as.ident.resolved.slot = 0;
            break;

        case EXPR_NULL:
            // No data needed
            break;

        case EXPR_BINARY:
            expr->as.binary.op = (BinaryOp)read_u8(ctx);
            expr->as.binary.left = deserialize_expr(ctx);
            expr->as.binary.right = deserialize_expr(ctx);
            break;

        case EXPR_UNARY:
            expr->as.unary.op = (UnaryOp)read_u8(ctx);
            expr->as.unary.operand = deserialize_expr(ctx);
            break;

        case EXPR_TERNARY:
            expr->as.ternary.condition = deserialize_expr(ctx);
            expr->as.ternary.true_expr = deserialize_expr(ctx);
            expr->as.ternary.false_expr = deserialize_expr(ctx);
            break;

        case EXPR_CALL:
            expr->as.call.func = deserialize_expr(ctx);
            expr->as.call.num_args = (int)read_u32(ctx);
            if (expr->as.call.num_args > 0) {
                expr->as.call.args = malloc(expr->as.call.num_args * sizeof(Expr*));
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    expr->as.call.args[i] = deserialize_expr(ctx);
                }
            } else {
                expr->as.call.args = NULL;
            }
            break;

        case EXPR_ASSIGN:
            expr->as.assign.name = read_string_id(ctx);
            expr->as.assign.value = deserialize_expr(ctx);
            break;

        case EXPR_GET_PROPERTY:
            expr->as.get_property.object = deserialize_expr(ctx);
            expr->as.get_property.property = read_string_id(ctx);
            break;

        case EXPR_SET_PROPERTY:
            expr->as.set_property.object = deserialize_expr(ctx);
            expr->as.set_property.property = read_string_id(ctx);
            expr->as.set_property.value = deserialize_expr(ctx);
            break;

        case EXPR_INDEX:
            expr->as.index.object = deserialize_expr(ctx);
            expr->as.index.index = deserialize_expr(ctx);
            break;

        case EXPR_INDEX_ASSIGN:
            expr->as.index_assign.object = deserialize_expr(ctx);
            expr->as.index_assign.index = deserialize_expr(ctx);
            expr->as.index_assign.value = deserialize_expr(ctx);
            break;

        case EXPR_FUNCTION: {
            expr->as.function.is_async = read_u8(ctx);
            expr->as.function.num_params = (int)read_u32(ctx);
            if (expr->as.function.num_params > 0) {
                expr->as.function.param_names = malloc(expr->as.function.num_params * sizeof(char*));
                expr->as.function.param_types = malloc(expr->as.function.num_params * sizeof(Type*));
                expr->as.function.param_defaults = malloc(expr->as.function.num_params * sizeof(Expr*));
                for (int i = 0; i < expr->as.function.num_params; i++) {
                    expr->as.function.param_names[i] = read_string_id(ctx);
                    expr->as.function.param_types[i] = deserialize_type(ctx);
                    expr->as.function.param_defaults[i] = deserialize_expr(ctx);
                }
            } else {
                expr->as.function.param_names = NULL;
                expr->as.function.param_types = NULL;
                expr->as.function.param_defaults = NULL;
            }
            expr->as.function.return_type = deserialize_type(ctx);
            expr->as.function.body = deserialize_stmt(ctx);
            break;
        }

        case EXPR_ARRAY_LITERAL:
            expr->as.array_literal.num_elements = (int)read_u32(ctx);
            if (expr->as.array_literal.num_elements > 0) {
                expr->as.array_literal.elements = malloc(expr->as.array_literal.num_elements * sizeof(Expr*));
                for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                    expr->as.array_literal.elements[i] = deserialize_expr(ctx);
                }
            } else {
                expr->as.array_literal.elements = NULL;
            }
            break;

        case EXPR_OBJECT_LITERAL:
            expr->as.object_literal.num_fields = (int)read_u32(ctx);
            if (expr->as.object_literal.num_fields > 0) {
                expr->as.object_literal.field_names = malloc(expr->as.object_literal.num_fields * sizeof(char*));
                expr->as.object_literal.field_values = malloc(expr->as.object_literal.num_fields * sizeof(Expr*));
                for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                    expr->as.object_literal.field_names[i] = read_string_id(ctx);
                    expr->as.object_literal.field_values[i] = deserialize_expr(ctx);
                }
            } else {
                expr->as.object_literal.field_names = NULL;
                expr->as.object_literal.field_values = NULL;
            }
            break;

        case EXPR_PREFIX_INC:
            expr->as.prefix_inc.operand = deserialize_expr(ctx);
            break;

        case EXPR_PREFIX_DEC:
            expr->as.prefix_dec.operand = deserialize_expr(ctx);
            break;

        case EXPR_POSTFIX_INC:
            expr->as.postfix_inc.operand = deserialize_expr(ctx);
            break;

        case EXPR_POSTFIX_DEC:
            expr->as.postfix_dec.operand = deserialize_expr(ctx);
            break;

        case EXPR_AWAIT:
            expr->as.await_expr.awaited_expr = deserialize_expr(ctx);
            break;

        case EXPR_STRING_INTERPOLATION: {
            expr->as.string_interpolation.num_parts = (int)read_u32(ctx);
            int n = expr->as.string_interpolation.num_parts;
            expr->as.string_interpolation.string_parts = malloc((n + 1) * sizeof(char*));
            expr->as.string_interpolation.expr_parts = malloc(n * sizeof(Expr*));
            for (int i = 0; i <= n; i++) {
                expr->as.string_interpolation.string_parts[i] = read_string_id(ctx);
            }
            for (int i = 0; i < n; i++) {
                expr->as.string_interpolation.expr_parts[i] = deserialize_expr(ctx);
            }
            break;
        }

        case EXPR_OPTIONAL_CHAIN: {
            expr->as.optional_chain.object = deserialize_expr(ctx);
            expr->as.optional_chain.is_property = read_u8(ctx);
            expr->as.optional_chain.is_call = read_u8(ctx);
            if (expr->as.optional_chain.is_property) {
                expr->as.optional_chain.property = read_string_id(ctx);
                expr->as.optional_chain.index = NULL;
                expr->as.optional_chain.args = NULL;
                expr->as.optional_chain.num_args = 0;
            } else if (expr->as.optional_chain.is_call) {
                expr->as.optional_chain.property = NULL;
                expr->as.optional_chain.index = NULL;
                expr->as.optional_chain.num_args = (int)read_u32(ctx);
                if (expr->as.optional_chain.num_args > 0) {
                    expr->as.optional_chain.args = malloc(expr->as.optional_chain.num_args * sizeof(Expr*));
                    for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                        expr->as.optional_chain.args[i] = deserialize_expr(ctx);
                    }
                } else {
                    expr->as.optional_chain.args = NULL;
                }
            } else {
                expr->as.optional_chain.property = NULL;
                expr->as.optional_chain.index = deserialize_expr(ctx);
                expr->as.optional_chain.args = NULL;
                expr->as.optional_chain.num_args = 0;
            }
            break;
        }

        case EXPR_NULL_COALESCE:
            expr->as.null_coalesce.left = deserialize_expr(ctx);
            expr->as.null_coalesce.right = deserialize_expr(ctx);
            break;
    }

    return expr;
}

// ========== STATEMENT SERIALIZATION ==========

static void serialize_stmt(SerializeContext *ctx, Stmt *stmt) {
    if (stmt == NULL) {
        write_u8(ctx, NULL_MARKER);
        return;
    }

    write_u8(ctx, (uint8_t)stmt->type);

    // Write line number if debug info requested
    if (ctx->flags & HMLC_FLAG_DEBUG) {
        write_u32(ctx, (uint32_t)stmt->line);
    }

    switch (stmt->type) {
        case STMT_LET:
            write_string_id(ctx, stmt->as.let.name);
            serialize_type(ctx, stmt->as.let.type_annotation);
            serialize_expr(ctx, stmt->as.let.value);
            break;

        case STMT_CONST:
            write_string_id(ctx, stmt->as.const_stmt.name);
            serialize_type(ctx, stmt->as.const_stmt.type_annotation);
            serialize_expr(ctx, stmt->as.const_stmt.value);
            break;

        case STMT_EXPR:
            serialize_expr(ctx, stmt->as.expr);
            break;

        case STMT_IF:
            serialize_expr(ctx, stmt->as.if_stmt.condition);
            serialize_stmt(ctx, stmt->as.if_stmt.then_branch);
            serialize_stmt(ctx, stmt->as.if_stmt.else_branch);
            break;

        case STMT_WHILE:
            serialize_expr(ctx, stmt->as.while_stmt.condition);
            serialize_stmt(ctx, stmt->as.while_stmt.body);
            break;

        case STMT_FOR:
            serialize_stmt(ctx, stmt->as.for_loop.initializer);
            serialize_expr(ctx, stmt->as.for_loop.condition);
            serialize_expr(ctx, stmt->as.for_loop.increment);
            serialize_stmt(ctx, stmt->as.for_loop.body);
            break;

        case STMT_FOR_IN:
            write_string_id(ctx, stmt->as.for_in.key_var);
            write_string_id(ctx, stmt->as.for_in.value_var);
            serialize_expr(ctx, stmt->as.for_in.iterable);
            serialize_stmt(ctx, stmt->as.for_in.body);
            break;

        case STMT_BREAK:
        case STMT_CONTINUE:
            // No data needed
            break;

        case STMT_BLOCK:
            write_u32(ctx, (uint32_t)stmt->as.block.count);
            for (int i = 0; i < stmt->as.block.count; i++) {
                serialize_stmt(ctx, stmt->as.block.statements[i]);
            }
            break;

        case STMT_RETURN:
            serialize_expr(ctx, stmt->as.return_stmt.value);
            break;

        case STMT_DEFINE_OBJECT:
            write_string_id(ctx, stmt->as.define_object.name);
            write_u32(ctx, (uint32_t)stmt->as.define_object.num_fields);
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                write_string_id(ctx, stmt->as.define_object.field_names[i]);
                serialize_type(ctx, stmt->as.define_object.field_types ? stmt->as.define_object.field_types[i] : NULL);
                write_u8(ctx, stmt->as.define_object.field_optional ? stmt->as.define_object.field_optional[i] : 0);
                serialize_expr(ctx, stmt->as.define_object.field_defaults ? stmt->as.define_object.field_defaults[i] : NULL);
            }
            break;

        case STMT_ENUM:
            write_string_id(ctx, stmt->as.enum_decl.name);
            write_u32(ctx, (uint32_t)stmt->as.enum_decl.num_variants);
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                write_string_id(ctx, stmt->as.enum_decl.variant_names[i]);
                serialize_expr(ctx, stmt->as.enum_decl.variant_values ? stmt->as.enum_decl.variant_values[i] : NULL);
            }
            break;

        case STMT_TRY:
            serialize_stmt(ctx, stmt->as.try_stmt.try_block);
            write_string_id(ctx, stmt->as.try_stmt.catch_param);
            serialize_stmt(ctx, stmt->as.try_stmt.catch_block);
            serialize_stmt(ctx, stmt->as.try_stmt.finally_block);
            break;

        case STMT_THROW:
            serialize_expr(ctx, stmt->as.throw_stmt.value);
            break;

        case STMT_SWITCH:
            serialize_expr(ctx, stmt->as.switch_stmt.expr);
            write_u32(ctx, (uint32_t)stmt->as.switch_stmt.num_cases);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                serialize_expr(ctx, stmt->as.switch_stmt.case_values ? stmt->as.switch_stmt.case_values[i] : NULL);
                serialize_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
            }
            break;

        case STMT_DEFER:
            serialize_expr(ctx, stmt->as.defer_stmt.call);
            break;

        case STMT_IMPORT:
            write_u8(ctx, stmt->as.import_stmt.is_namespace ? 1 : 0);
            write_string_id(ctx, stmt->as.import_stmt.namespace_name);
            write_string_id(ctx, stmt->as.import_stmt.module_path);
            write_u32(ctx, (uint32_t)stmt->as.import_stmt.num_imports);
            for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
                write_string_id(ctx, stmt->as.import_stmt.import_names ? stmt->as.import_stmt.import_names[i] : NULL);
                write_string_id(ctx, stmt->as.import_stmt.import_aliases ? stmt->as.import_stmt.import_aliases[i] : NULL);
            }
            break;

        case STMT_EXPORT:
            write_u8(ctx, stmt->as.export_stmt.is_declaration ? 1 : 0);
            write_u8(ctx, stmt->as.export_stmt.is_reexport ? 1 : 0);
            serialize_stmt(ctx, stmt->as.export_stmt.declaration);
            write_string_id(ctx, stmt->as.export_stmt.module_path);
            write_u32(ctx, (uint32_t)stmt->as.export_stmt.num_exports);
            for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                write_string_id(ctx, stmt->as.export_stmt.export_names ? stmt->as.export_stmt.export_names[i] : NULL);
                write_string_id(ctx, stmt->as.export_stmt.export_aliases ? stmt->as.export_stmt.export_aliases[i] : NULL);
            }
            break;

        case STMT_IMPORT_FFI:
            write_string_id(ctx, stmt->as.import_ffi.library_path);
            break;

        case STMT_EXTERN_FN:
            write_string_id(ctx, stmt->as.extern_fn.function_name);
            write_u32(ctx, (uint32_t)stmt->as.extern_fn.num_params);
            for (int i = 0; i < stmt->as.extern_fn.num_params; i++) {
                serialize_type(ctx, stmt->as.extern_fn.param_types[i]);
            }
            serialize_type(ctx, stmt->as.extern_fn.return_type);
            break;
    }
}

static Stmt* deserialize_stmt(DeserializeContext *ctx) {
    uint8_t type_byte = read_u8(ctx);
    if (type_byte == NULL_MARKER) return NULL;

    Stmt *stmt = malloc(sizeof(Stmt));
    memset(stmt, 0, sizeof(Stmt));
    stmt->type = (StmtType)type_byte;

    // Read line number if debug info present
    if (ctx->flags & HMLC_FLAG_DEBUG) {
        stmt->line = (int)read_u32(ctx);
    } else {
        stmt->line = 0;
    }

    switch (stmt->type) {
        case STMT_LET:
            stmt->as.let.name = read_string_id(ctx);
            stmt->as.let.type_annotation = deserialize_type(ctx);
            stmt->as.let.value = deserialize_expr(ctx);
            break;

        case STMT_CONST:
            stmt->as.const_stmt.name = read_string_id(ctx);
            stmt->as.const_stmt.type_annotation = deserialize_type(ctx);
            stmt->as.const_stmt.value = deserialize_expr(ctx);
            break;

        case STMT_EXPR:
            stmt->as.expr = deserialize_expr(ctx);
            break;

        case STMT_IF:
            stmt->as.if_stmt.condition = deserialize_expr(ctx);
            stmt->as.if_stmt.then_branch = deserialize_stmt(ctx);
            stmt->as.if_stmt.else_branch = deserialize_stmt(ctx);
            break;

        case STMT_WHILE:
            stmt->as.while_stmt.condition = deserialize_expr(ctx);
            stmt->as.while_stmt.body = deserialize_stmt(ctx);
            break;

        case STMT_FOR:
            stmt->as.for_loop.initializer = deserialize_stmt(ctx);
            stmt->as.for_loop.condition = deserialize_expr(ctx);
            stmt->as.for_loop.increment = deserialize_expr(ctx);
            stmt->as.for_loop.body = deserialize_stmt(ctx);
            break;

        case STMT_FOR_IN:
            stmt->as.for_in.key_var = read_string_id(ctx);
            stmt->as.for_in.value_var = read_string_id(ctx);
            stmt->as.for_in.iterable = deserialize_expr(ctx);
            stmt->as.for_in.body = deserialize_stmt(ctx);
            break;

        case STMT_BREAK:
        case STMT_CONTINUE:
            // No data needed
            break;

        case STMT_BLOCK:
            stmt->as.block.count = (int)read_u32(ctx);
            if (stmt->as.block.count > 0) {
                stmt->as.block.statements = malloc(stmt->as.block.count * sizeof(Stmt*));
                for (int i = 0; i < stmt->as.block.count; i++) {
                    stmt->as.block.statements[i] = deserialize_stmt(ctx);
                }
            } else {
                stmt->as.block.statements = NULL;
            }
            break;

        case STMT_RETURN:
            stmt->as.return_stmt.value = deserialize_expr(ctx);
            break;

        case STMT_DEFINE_OBJECT: {
            stmt->as.define_object.name = read_string_id(ctx);
            stmt->as.define_object.num_fields = (int)read_u32(ctx);
            int n = stmt->as.define_object.num_fields;
            if (n > 0) {
                stmt->as.define_object.field_names = malloc(n * sizeof(char*));
                stmt->as.define_object.field_types = malloc(n * sizeof(Type*));
                stmt->as.define_object.field_optional = malloc(n * sizeof(int));
                stmt->as.define_object.field_defaults = malloc(n * sizeof(Expr*));
                for (int i = 0; i < n; i++) {
                    stmt->as.define_object.field_names[i] = read_string_id(ctx);
                    stmt->as.define_object.field_types[i] = deserialize_type(ctx);
                    stmt->as.define_object.field_optional[i] = read_u8(ctx);
                    stmt->as.define_object.field_defaults[i] = deserialize_expr(ctx);
                }
            } else {
                stmt->as.define_object.field_names = NULL;
                stmt->as.define_object.field_types = NULL;
                stmt->as.define_object.field_optional = NULL;
                stmt->as.define_object.field_defaults = NULL;
            }
            break;
        }

        case STMT_ENUM: {
            stmt->as.enum_decl.name = read_string_id(ctx);
            stmt->as.enum_decl.num_variants = (int)read_u32(ctx);
            int n = stmt->as.enum_decl.num_variants;
            if (n > 0) {
                stmt->as.enum_decl.variant_names = malloc(n * sizeof(char*));
                stmt->as.enum_decl.variant_values = malloc(n * sizeof(Expr*));
                for (int i = 0; i < n; i++) {
                    stmt->as.enum_decl.variant_names[i] = read_string_id(ctx);
                    stmt->as.enum_decl.variant_values[i] = deserialize_expr(ctx);
                }
            } else {
                stmt->as.enum_decl.variant_names = NULL;
                stmt->as.enum_decl.variant_values = NULL;
            }
            break;
        }

        case STMT_TRY:
            stmt->as.try_stmt.try_block = deserialize_stmt(ctx);
            stmt->as.try_stmt.catch_param = read_string_id(ctx);
            stmt->as.try_stmt.catch_block = deserialize_stmt(ctx);
            stmt->as.try_stmt.finally_block = deserialize_stmt(ctx);
            break;

        case STMT_THROW:
            stmt->as.throw_stmt.value = deserialize_expr(ctx);
            break;

        case STMT_SWITCH: {
            stmt->as.switch_stmt.expr = deserialize_expr(ctx);
            stmt->as.switch_stmt.num_cases = (int)read_u32(ctx);
            int n = stmt->as.switch_stmt.num_cases;
            if (n > 0) {
                stmt->as.switch_stmt.case_values = malloc(n * sizeof(Expr*));
                stmt->as.switch_stmt.case_bodies = malloc(n * sizeof(Stmt*));
                for (int i = 0; i < n; i++) {
                    stmt->as.switch_stmt.case_values[i] = deserialize_expr(ctx);
                    stmt->as.switch_stmt.case_bodies[i] = deserialize_stmt(ctx);
                }
            } else {
                stmt->as.switch_stmt.case_values = NULL;
                stmt->as.switch_stmt.case_bodies = NULL;
            }
            break;
        }

        case STMT_DEFER:
            stmt->as.defer_stmt.call = deserialize_expr(ctx);
            break;

        case STMT_IMPORT: {
            stmt->as.import_stmt.is_namespace = read_u8(ctx);
            stmt->as.import_stmt.namespace_name = read_string_id(ctx);
            stmt->as.import_stmt.module_path = read_string_id(ctx);
            stmt->as.import_stmt.num_imports = (int)read_u32(ctx);
            int n = stmt->as.import_stmt.num_imports;
            if (n > 0) {
                stmt->as.import_stmt.import_names = malloc(n * sizeof(char*));
                stmt->as.import_stmt.import_aliases = malloc(n * sizeof(char*));
                for (int i = 0; i < n; i++) {
                    stmt->as.import_stmt.import_names[i] = read_string_id(ctx);
                    stmt->as.import_stmt.import_aliases[i] = read_string_id(ctx);
                }
            } else {
                stmt->as.import_stmt.import_names = NULL;
                stmt->as.import_stmt.import_aliases = NULL;
            }
            break;
        }

        case STMT_EXPORT: {
            stmt->as.export_stmt.is_declaration = read_u8(ctx);
            stmt->as.export_stmt.is_reexport = read_u8(ctx);
            stmt->as.export_stmt.declaration = deserialize_stmt(ctx);
            stmt->as.export_stmt.module_path = read_string_id(ctx);
            stmt->as.export_stmt.num_exports = (int)read_u32(ctx);
            int n = stmt->as.export_stmt.num_exports;
            if (n > 0) {
                stmt->as.export_stmt.export_names = malloc(n * sizeof(char*));
                stmt->as.export_stmt.export_aliases = malloc(n * sizeof(char*));
                for (int i = 0; i < n; i++) {
                    stmt->as.export_stmt.export_names[i] = read_string_id(ctx);
                    stmt->as.export_stmt.export_aliases[i] = read_string_id(ctx);
                }
            } else {
                stmt->as.export_stmt.export_names = NULL;
                stmt->as.export_stmt.export_aliases = NULL;
            }
            break;
        }

        case STMT_IMPORT_FFI:
            stmt->as.import_ffi.library_path = read_string_id(ctx);
            break;

        case STMT_EXTERN_FN: {
            stmt->as.extern_fn.function_name = read_string_id(ctx);
            stmt->as.extern_fn.num_params = (int)read_u32(ctx);
            int n = stmt->as.extern_fn.num_params;
            if (n > 0) {
                stmt->as.extern_fn.param_types = malloc(n * sizeof(Type*));
                for (int i = 0; i < n; i++) {
                    stmt->as.extern_fn.param_types[i] = deserialize_type(ctx);
                }
            } else {
                stmt->as.extern_fn.param_types = NULL;
            }
            stmt->as.extern_fn.return_type = deserialize_type(ctx);
            break;
        }
    }

    return stmt;
}

// ========== PUBLIC API IMPLEMENTATION ==========

uint8_t* ast_serialize(Stmt **statements, int stmt_count, uint16_t flags, size_t *out_size) {
    SerializeContext ctx;
    ctx_init(&ctx, flags);

    // First pass: collect all strings and serialize AST to temporary buffer
    // The string table will be built during serialization
    write_u32(&ctx, (uint32_t)stmt_count);
    for (int i = 0; i < stmt_count; i++) {
        serialize_stmt(&ctx, statements[i]);
    }

    // Now build the final output with header and string table
    uint8_t *ast_data = ctx.buffer;
    size_t ast_data_size = ctx.buffer_size;

    // Create new buffer for final output
    ctx.buffer = malloc(INITIAL_BUFFER_SIZE);
    ctx.buffer_size = 0;
    ctx.buffer_capacity = INITIAL_BUFFER_SIZE;

    // Write header
    write_u32(&ctx, HMLC_MAGIC);
    write_u16(&ctx, HMLC_VERSION);
    write_u16(&ctx, flags);
    write_u32(&ctx, ctx.strings.count);
    write_u32(&ctx, (uint32_t)stmt_count);
    write_u32(&ctx, 0);  // Checksum placeholder

    // Write string table
    for (uint32_t i = 0; i < ctx.strings.count; i++) {
        uint32_t len = ctx.strings.lengths[i];
        write_u32(&ctx, len);
        write_bytes(&ctx, ctx.strings.strings[i], len);
    }

    // Copy AST data (skip the stmt_count we wrote at the start)
    write_bytes(&ctx, ast_data + 4, ast_data_size - 4);

    free(ast_data);

    // Compute checksum of data after header and write it to the placeholder
    uint32_t checksum = compute_checksum(ctx.buffer + HEADER_SIZE, ctx.buffer_size - HEADER_SIZE);
    ctx.buffer[CHECKSUM_OFFSET] = checksum & 0xFF;
    ctx.buffer[CHECKSUM_OFFSET + 1] = (checksum >> 8) & 0xFF;
    ctx.buffer[CHECKSUM_OFFSET + 2] = (checksum >> 16) & 0xFF;
    ctx.buffer[CHECKSUM_OFFSET + 3] = (checksum >> 24) & 0xFF;

    *out_size = ctx.buffer_size;
    uint8_t *result = ctx.buffer;

    string_table_free(&ctx.strings);

    return result;
}

Stmt** ast_deserialize(const uint8_t *data, size_t data_size, int *out_count) {
    DeserializeContext ctx;
    dctx_init(&ctx, data, data_size);

    // Read and validate header
    uint32_t magic = read_u32(&ctx);
    if (magic != HMLC_MAGIC) {
        fprintf(stderr, "Error: Invalid .hmlc file (bad magic number)\n");
        return NULL;
    }

    uint16_t version = read_u16(&ctx);
    if (version > HMLC_VERSION) {
        fprintf(stderr, "Error: .hmlc file version %d is newer than supported version %d\n",
                version, HMLC_VERSION);
        return NULL;
    }

    ctx.flags = read_u16(&ctx);
    ctx.string_count = read_u32(&ctx);
    uint32_t stmt_count = read_u32(&ctx);
    uint32_t stored_checksum = read_u32(&ctx);

    // Validate checksum
    if (data_size > HEADER_SIZE) {
        uint32_t computed_checksum = compute_checksum(data + HEADER_SIZE, data_size - HEADER_SIZE);
        if (stored_checksum != computed_checksum) {
            fprintf(stderr, "Error: .hmlc file checksum mismatch (file may be corrupted)\n");
            return NULL;
        }
    }

    // Read string table
    ctx.strings = malloc(ctx.string_count * sizeof(char*));
    for (uint32_t i = 0; i < ctx.string_count; i++) {
        uint32_t len = read_u32(&ctx);
        char *str = malloc(len + 1);
        if (dctx_has_bytes(&ctx, len)) {
            memcpy(str, ctx.data + ctx.offset, len);
            ctx.offset += len;
        }
        str[len] = '\0';
        ctx.strings[i] = str;
    }

    // Deserialize statements
    Stmt **statements = malloc(stmt_count * sizeof(Stmt*));
    for (uint32_t i = 0; i < stmt_count; i++) {
        statements[i] = deserialize_stmt(&ctx);
    }

    *out_count = (int)stmt_count;

    dctx_free(&ctx);

    return statements;
}

int ast_serialize_to_file(const char *filename, Stmt **statements, int stmt_count, uint16_t flags) {
    size_t data_size;
    uint8_t *data = ast_serialize(statements, stmt_count, flags, &data_size);
    if (data == NULL) {
        return -1;
    }

    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        fprintf(stderr, "Error: Cannot open '%s' for writing\n", filename);
        free(data);
        return -1;
    }

    size_t written = fwrite(data, 1, data_size, f);
    fclose(f);
    free(data);

    if (written != data_size) {
        fprintf(stderr, "Error: Failed to write complete data to '%s'\n", filename);
        return -1;
    }

    return 0;
}

Stmt** ast_deserialize_from_file(const char *filename, int *out_count) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        fprintf(stderr, "Error: Cannot open '%s' for reading\n", filename);
        return NULL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < (long)sizeof(HmlcHeader)) {
        fprintf(stderr, "Error: '%s' is too small to be a valid .hmlc file\n", filename);
        fclose(f);
        return NULL;
    }

    // Read entire file
    uint8_t *data = malloc(file_size);
    size_t bytes_read = fread(data, 1, file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read complete file '%s'\n", filename);
        free(data);
        return NULL;
    }

    Stmt **result = ast_deserialize(data, file_size, out_count);
    free(data);

    return result;
}

int is_hmlc_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL) return 0;

    uint8_t magic_bytes[4];
    size_t read = fread(magic_bytes, 1, 4, f);
    fclose(f);

    if (read != 4) return 0;

    uint32_t magic = magic_bytes[0] |
                     ((uint32_t)magic_bytes[1] << 8) |
                     ((uint32_t)magic_bytes[2] << 16) |
                     ((uint32_t)magic_bytes[3] << 24);

    return magic == HMLC_MAGIC;
}

int is_hmlc_data(const uint8_t *data, size_t data_size) {
    if (data_size < 4) return 0;

    uint32_t magic = data[0] |
                     ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) |
                     ((uint32_t)data[3] << 24);

    return magic == HMLC_MAGIC;
}

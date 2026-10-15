#include "formatter_internal.h"

// ========== FORMATTER CONTEXT ==========


// Output any leading comments for a given source line
// Only outputs comments between last_source_line and source_line
void fmt_leading_comments(FmtCtx *ctx, int source_line) {
    if (!ctx->comments) return;

    int first_item = 1;  // Track if this is first item to be output
    int last_output_line = ctx->last_source_line;  // Track source line of last output item

    while (ctx->comments->next_idx < ctx->comments->count) {
        Comment *c = &ctx->comments->comments[ctx->comments->next_idx];

        // Only output comments that are:
        // 1. After last_source_line and before or on source_line
        // 2. Not trailing comments (those go after code)
        if (c->line > source_line) break;
        if (c->line <= ctx->last_source_line) {
            // Comment is from before our window - skip it
            ctx->comments->next_idx++;
            continue;
        }
        if (c->is_trailing) {
            if (c->line == source_line) {
                // Trailing comment for this statement - let fmt_trailing_comment handle it
                break;
            }
            // Trailing comment from an earlier line - skip it
            ctx->comments->next_idx++;
            continue;
        }

        // Check if we need a blank line before this comment
        if (!first_item || ctx->last_source_line > 0) {
            if (has_blank_line_between(ctx->blank_lines, last_output_line, c->line)) {
                fmt_newline(ctx);
                ctx->output_line++;
            }
        }

        fmt_indent(ctx);
        if (c->type == COMMENT_LINE) {
            buf_append(&ctx->buf, "// ");
            buf_append(&ctx->buf, c->text);
        } else {
            buf_append(&ctx->buf, "/* ");
            buf_append(&ctx->buf, c->text);
            buf_append(&ctx->buf, " */");
        }
        fmt_newline(ctx);
        ctx->output_line++;
        last_output_line = c->line;
        first_item = 0;
        ctx->comments->next_idx++;
    }

    // Check if we need a blank line before the statement itself
    if (ctx->last_source_line > 0 && source_line > ctx->last_source_line) {
        int check_from = (last_output_line > ctx->last_source_line) ? last_output_line : ctx->last_source_line;
        if (has_blank_line_between(ctx->blank_lines, check_from, source_line)) {
            // Only add blank line if we haven't just added one
            // Check if the last char in buffer is already a double newline
            if (ctx->buf.len < 2 ||
                !(ctx->buf.data[ctx->buf.len - 1] == '\n' && ctx->buf.data[ctx->buf.len - 2] == '\n')) {
                fmt_newline(ctx);
                ctx->output_line++;
            }
        }
    }

    // Advance blank line tracker
    advance_blank_lines_past(ctx->blank_lines, source_line);
}

// Output trailing comment for a given source line (if any)
void fmt_trailing_comment(FmtCtx *ctx, int source_line) {
    if (!ctx->comments) return;

    // Look for trailing comment on this line
    for (int i = ctx->comments->next_idx; i < ctx->comments->count; i++) {
        Comment *c = &ctx->comments->comments[i];

        if (c->line > source_line) break;
        if (c->line == source_line && c->is_trailing) {
            // Remove the newline we just added
            if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                ctx->buf.len--;
                ctx->buf.data[ctx->buf.len] = '\0';
            }

            buf_append(&ctx->buf, "  ");
            if (c->type == COMMENT_LINE) {
                buf_append(&ctx->buf, "// ");
                buf_append(&ctx->buf, c->text);
            } else {
                buf_append(&ctx->buf, "/* ");
                buf_append(&ctx->buf, c->text);
                buf_append(&ctx->buf, " */");
            }
            fmt_newline(ctx);

            // Mark as consumed by moving next_idx past it
            if (i == ctx->comments->next_idx) {
                ctx->comments->next_idx++;
            }
            return;
        }
    }
}

// Update column position after the buffer changes
void update_column(FmtCtx *ctx) {
    // Find last newline in buffer and count from there
    ctx->column = 0;
    for (size_t i = ctx->buf.len; i > 0; i--) {
        if (ctx->buf.data[i - 1] == '\n') {
            // Count from after the newline
            for (size_t j = i; j < ctx->buf.len; j++) {
                if (ctx->buf.data[j] == '\t') {
                    ctx->column = ((ctx->column / FMT_TAB_WIDTH) + 1) * FMT_TAB_WIDTH;
                } else {
                    ctx->column++;
                }
            }
            return;
        }
    }
    // No newline found - count from beginning
    for (size_t j = 0; j < ctx->buf.len; j++) {
        if (ctx->buf.data[j] == '\t') {
            ctx->column = ((ctx->column / FMT_TAB_WIDTH) + 1) * FMT_TAB_WIDTH;
        } else {
            ctx->column++;
        }
    }
}

void fmt_indent(FmtCtx *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        buf_append_char(&ctx->buf, '\t');
    }
    update_column(ctx);
}

void fmt_newline(FmtCtx *ctx) {
    buf_append_char(&ctx->buf, '\n');
    ctx->column = 0;
}

// Estimate the length of an expression (for line-breaking decisions)
int estimate_expr_len(Expr *expr) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_NUMBER:
            return expr->as.number.is_float ? 10 : 6;  // Rough estimate
        case EXPR_BOOL:
            return expr->as.boolean ? 4 : 5;
        case EXPR_STRING:
            return expr->as.string ? (int)strlen(expr->as.string) + 2 : 2;
        case EXPR_RUNE:
            return 3;
        case EXPR_IDENT:
            return expr->as.ident.name ? (int)strlen(expr->as.ident.name) : 0;
        case EXPR_NULL:
            return 4;
        case EXPR_BINARY:
            return estimate_expr_len(expr->as.binary.left) + 3 +
                   estimate_expr_len(expr->as.binary.right);
        case EXPR_UNARY:
            return 1 + estimate_expr_len(expr->as.unary.operand);
        case EXPR_CALL: {
            int len = estimate_expr_len(expr->as.call.func) + 2;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (i > 0) len += 2;
                len += estimate_expr_len(expr->as.call.args[i]);
            }
            return len;
        }
        case EXPR_ARRAY_LITERAL: {
            int len = 2;
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (i > 0) len += 2;
                len += estimate_expr_len(expr->as.array_literal.elements[i]);
            }
            return len;
        }
        case EXPR_OBJECT_LITERAL: {
            int len = 4;  // "{ " and " }"
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (i > 0) len += 2;
                if (expr->as.object_literal.field_names[i] == NULL) {
                    // Spread: ...expr
                    len += 3;  // "..."
                } else {
                    len += strlen(expr->as.object_literal.field_names[i]) + 2;
                }
                len += estimate_expr_len(expr->as.object_literal.field_values[i]);
            }
            return len;
        }
        case EXPR_GET_PROPERTY:
            return estimate_expr_len(expr->as.get_property.object) + 1 +
                   (expr->as.get_property.property ? (int)strlen(expr->as.get_property.property) : 0);
        case EXPR_SET_PROPERTY:
            return estimate_expr_len(expr->as.set_property.object) + 1 +
                   (expr->as.set_property.property ? (int)strlen(expr->as.set_property.property) : 0) +
                   3 + estimate_expr_len(expr->as.set_property.value);
        case EXPR_INDEX:
            return estimate_expr_len(expr->as.index.object) + 2 +
                   estimate_expr_len(expr->as.index.index);
        case EXPR_TERNARY:
            return estimate_expr_len(expr->as.ternary.condition) + 3 +
                   estimate_expr_len(expr->as.ternary.true_expr) + 3 +
                   estimate_expr_len(expr->as.ternary.false_expr);
        case EXPR_ASSIGN:
            return (expr->as.assign.name ? (int)strlen(expr->as.assign.name) : 0) + 3 +
                   estimate_expr_len(expr->as.assign.value);
        case EXPR_INDEX_ASSIGN:
            return estimate_expr_len(expr->as.index_assign.object) + 2 +
                   estimate_expr_len(expr->as.index_assign.index) + 4 +
                   estimate_expr_len(expr->as.index_assign.value);
        case EXPR_FUNCTION:
            // Functions are always complex - return large value to trigger line break
            return 50;
        case EXPR_PREFIX_INC:
            return 2 + estimate_expr_len(expr->as.prefix_inc.operand);
        case EXPR_PREFIX_DEC:
            return 2 + estimate_expr_len(expr->as.prefix_dec.operand);
        case EXPR_POSTFIX_INC:
            return estimate_expr_len(expr->as.postfix_inc.operand) + 2;
        case EXPR_POSTFIX_DEC:
            return estimate_expr_len(expr->as.postfix_dec.operand) + 2;
        case EXPR_AWAIT:
            return 6 + estimate_expr_len(expr->as.await_expr.awaited_expr);
        case EXPR_STRING_INTERPOLATION: {
            int len = 2;  // backticks
            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                if (expr->as.string_interpolation.string_parts[i]) {
                    len += strlen(expr->as.string_interpolation.string_parts[i]);
                }
                if (i < expr->as.string_interpolation.num_parts) {
                    len += 3;  // "${" and "}"
                    len += estimate_expr_len(expr->as.string_interpolation.expr_parts[i]);
                }
            }
            return len;
        }
        case EXPR_OPTIONAL_CHAIN: {
            int len = estimate_expr_len(expr->as.optional_chain.object) + 2;  // "?."
            if (expr->as.optional_chain.is_call) {
                len += 2;  // "()"
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    if (i > 0) len += 2;
                    len += estimate_expr_len(expr->as.optional_chain.args[i]);
                }
            } else if (expr->as.optional_chain.is_property) {
                len += expr->as.optional_chain.property ?
                       (int)strlen(expr->as.optional_chain.property) : 0;
            } else {
                len += 2 + estimate_expr_len(expr->as.optional_chain.index);  // "[idx]"
            }
            return len;
        }
        case EXPR_NULL_COALESCE:
            return estimate_expr_len(expr->as.null_coalesce.left) + 4 +
                   estimate_expr_len(expr->as.null_coalesce.right);
        default:
            return 10;  // Default estimate for any future expression types
    }
}

// Check if an expression is "complex" enough to warrant multiline formatting
// Returns 1 if the expression is a function call with arguments, nested array/object, etc.
int expr_is_complex(Expr *expr) {
    if (!expr) return 0;
    switch (expr->type) {
        case EXPR_CALL:
            // Calls with arguments are complex
            return expr->as.call.num_args > 0;
        case EXPR_ARRAY_LITERAL:
            // Non-empty arrays are complex
            return expr->as.array_literal.num_elements > 0;
        case EXPR_OBJECT_LITERAL:
            // Non-empty objects are complex
            return expr->as.object_literal.num_fields > 0;
        case EXPR_FUNCTION:
            return 1;
        case EXPR_TERNARY:
            return 1;
        default:
            return 0;
    }
}

// Count how many elements in an expression list are complex
int count_complex_elements(Expr **elements, int num_elements) {
    int count = 0;
    for (int i = 0; i < num_elements; i++) {
        if (expr_is_complex(elements[i])) {
            count++;
        }
    }
    return count;
}

// Estimate the length of a type annotation
int estimate_type_len(Type *type) {
    if (!type) return 0;
    switch (type->kind) {
        case TYPE_I8: return 2;
        case TYPE_I16: return 3;
        case TYPE_I32: return 3;
        case TYPE_I64: return 3;
        case TYPE_U8: return 2;
        case TYPE_U16: return 3;
        case TYPE_U32: return 3;
        case TYPE_U64: return 3;
        case TYPE_F32: return 3;
        case TYPE_F64: return 3;
        case TYPE_BOOL: return 4;
        case TYPE_STRING: return 6;
        case TYPE_RUNE: return 4;
        case TYPE_PTR: return 3;
        case TYPE_BUFFER: return 6;
        case TYPE_ARRAY:
            return 5 + (type->element_type ? 2 + estimate_type_len(type->element_type) : 0);
        case TYPE_NULL: return 4;
        case TYPE_CUSTOM_OBJECT:
            return type->type_name ? (int)strlen(type->type_name) : 0;
        case TYPE_GENERIC_OBJECT: return 6;
        case TYPE_ENUM:
            return type->type_name ? (int)strlen(type->type_name) : 0;
        case TYPE_VOID: return 4;
        case TYPE_INFER: return 0;
        case TYPE_PARAM:
            return type->type_name ? (int)strlen(type->type_name) : 0;
        case TYPE_SELF: return 4;
        case TYPE_COMPOUND: {
            int len = 0;
            for (int i = 0; i < type->num_compound_types; i++) {
                if (i > 0) len += 3;  // " & "
                len += estimate_type_len(type->compound_types[i]);
            }
            return len;
        }
        case TYPE_FUNCTION: {
            int len = type->fn_is_async ? 9 : 3;  // "async fn(" or "fn("
            for (int i = 0; i < type->fn_num_params; i++) {
                if (i > 0) len += 2;
                if (type->fn_param_types && type->fn_param_types[i]) {
                    len += estimate_type_len(type->fn_param_types[i]);
                }
            }
            len += 1;  // ")"
            if (type->fn_return_type && type->fn_return_type->kind != TYPE_INFER) {
                len += 2 + estimate_type_len(type->fn_return_type);
            }
            return len;
        }
        default: return 5;
    }
}

// Estimate the length of function parameters
int estimate_fn_params_len(Expr *fn) {
    if (!fn || fn->type != EXPR_FUNCTION) return 0;
    int len = 2;  // "()"
    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (i > 0) len += 2;  // ", "
        if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
            len += 4;  // "ref "
        }
        if (fn->as.function.param_names[i]) {
            len += strlen(fn->as.function.param_names[i]);
        }
        if (fn->as.function.param_types && fn->as.function.param_types[i] &&
            fn->as.function.param_types[i]->kind != TYPE_INFER) {
            len += 2;  // ": "
            len += estimate_type_len(fn->as.function.param_types[i]);
            if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
                len += 1;  // "?"
                len += 3 + estimate_expr_len(fn->as.function.param_defaults[i]);  // " = expr"
            }
        } else if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            len += 3 + estimate_expr_len(fn->as.function.param_defaults[i]);  // "?: expr"
        }
    }
    if (fn->as.function.rest_param) {
        if (fn->as.function.num_params > 0) len += 2;
        len += 3 + strlen(fn->as.function.rest_param);  // "...name"
        if (fn->as.function.rest_param_type) {
            len += 2 + estimate_type_len(fn->as.function.rest_param_type);
        }
    }
    return len;
}

// Estimate total import statement length
int estimate_import_len(Stmt *stmt) {
    if (!stmt || stmt->type != STMT_IMPORT) return 0;
    int len = 17;  // "import {  } from \"\";"
    len += strlen(stmt->as.import_stmt.module_path);
    for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
        if (i > 0) len += 2;  // ", "
        len += strlen(stmt->as.import_stmt.import_names[i]);
        if (stmt->as.import_stmt.import_aliases && stmt->as.import_stmt.import_aliases[i]) {
            len += 4 + strlen(stmt->as.import_stmt.import_aliases[i]);  // " as alias"
        }
    }
    return len;
}

// Count depth of chained method calls (obj.method().method2().method3())
int count_method_chain_depth(Expr *expr) {
    int depth = 0;
    while (expr) {
        if (expr->type == EXPR_CALL) {
            Expr *func = expr->as.call.func;
            if (func && func->type == EXPR_GET_PROPERTY) {
                depth++;
                expr = func->as.get_property.object;
            } else {
                break;
            }
        } else if (expr->type == EXPR_GET_PROPERTY) {
            expr = expr->as.get_property.object;
        } else {
            break;
        }
    }
    return depth;
}

// ========== TYPE FORMATTING ==========

void fmt_type(FmtCtx *ctx, Type *type) {
    if (!type) return;

    switch (type->kind) {
        case TYPE_I8:  buf_append(&ctx->buf, "i8"); break;
        case TYPE_I16: buf_append(&ctx->buf, "i16"); break;
        case TYPE_I32: buf_append(&ctx->buf, "i32"); break;
        case TYPE_I64: buf_append(&ctx->buf, "i64"); break;
        case TYPE_U8:  buf_append(&ctx->buf, "u8"); break;
        case TYPE_U16: buf_append(&ctx->buf, "u16"); break;
        case TYPE_U32: buf_append(&ctx->buf, "u32"); break;
        case TYPE_U64: buf_append(&ctx->buf, "u64"); break;
        case TYPE_F32: buf_append(&ctx->buf, "f32"); break;
        case TYPE_F64: buf_append(&ctx->buf, "f64"); break;
        case TYPE_BOOL: buf_append(&ctx->buf, "bool"); break;
        case TYPE_STRING: buf_append(&ctx->buf, "string"); break;
        case TYPE_RUNE: buf_append(&ctx->buf, "rune"); break;
        case TYPE_PTR: buf_append(&ctx->buf, "ptr"); break;
        case TYPE_BUFFER: buf_append(&ctx->buf, "buffer"); break;
        case TYPE_ARRAY:
            buf_append(&ctx->buf, "array");
            if (type->element_type) {
                buf_append(&ctx->buf, "<");
                fmt_type(ctx, type->element_type);
                buf_append(&ctx->buf, ">");
            }
            break;
        case TYPE_NULL: buf_append(&ctx->buf, "null"); break;
        case TYPE_CUSTOM_OBJECT:
            if (type->type_name) {
                buf_append(&ctx->buf, type->type_name);
            }
            break;
        case TYPE_GENERIC_OBJECT: buf_append(&ctx->buf, "object"); break;
        case TYPE_ENUM:
            if (type->type_name) {
                buf_append(&ctx->buf, type->type_name);
            }
            break;
        case TYPE_VOID: buf_append(&ctx->buf, "void"); break;
        case TYPE_INFER: break;  // No annotation
        case TYPE_PARAM:
            // Generic type parameter (e.g., T)
            if (type->type_name) {
                buf_append(&ctx->buf, type->type_name);
            }
            break;
        case TYPE_SELF:
            buf_append(&ctx->buf, "Self");
            break;
        case TYPE_COMPOUND:
            // Intersection type (A & B & C)
            for (int i = 0; i < type->num_compound_types; i++) {
                if (i > 0) buf_append(&ctx->buf, " & ");
                fmt_type(ctx, type->compound_types[i]);
            }
            break;
        case TYPE_FUNCTION:
            // Function type: fn(params): return_type
            if (type->fn_is_async) {
                buf_append(&ctx->buf, "async ");
            }
            buf_append(&ctx->buf, "fn(");
            for (int i = 0; i < type->fn_num_params; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                if (type->fn_param_is_const && type->fn_param_is_const[i]) {
                    buf_append(&ctx->buf, "const ");
                }
                if (type->fn_param_names && type->fn_param_names[i]) {
                    buf_append(&ctx->buf, type->fn_param_names[i]);
                    if (type->fn_param_optional && type->fn_param_optional[i]) {
                        buf_append_char(&ctx->buf, '?');
                    }
                    buf_append(&ctx->buf, ": ");
                }
                if (type->fn_param_types && type->fn_param_types[i]) {
                    fmt_type(ctx, type->fn_param_types[i]);
                }
            }
            buf_append_char(&ctx->buf, ')');
            // Output return type (including void - it was explicitly specified)
            if (type->fn_return_type && type->fn_return_type->kind != TYPE_INFER) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, type->fn_return_type);
            }
            break;
    }

    if (type->nullable) {
        buf_append_char(&ctx->buf, '?');
    }
}

// ========== ANNOTATION FORMATTING ==========

// Format annotations (e.g., @inline, @hot, @optimize("3"))
void fmt_annotations(FmtCtx *ctx, Annotation **annotations, int count) {
    if (!annotations || count == 0) return;

    for (int i = 0; i < count; i++) {
        Annotation *a = annotations[i];
        if (!a) continue;

        fmt_indent(ctx);
        buf_append_char(&ctx->buf, '@');
        buf_append(&ctx->buf, a->name);

        // Format arguments if present
        if (a->args && a->arg_count > 0) {
            buf_append_char(&ctx->buf, '(');
            for (int j = 0; j < a->arg_count; j++) {
                if (j > 0) buf_append(&ctx->buf, ", ");
                AnnotationArg *arg = &a->args[j];
                // Named argument
                if (arg->name) {
                    buf_append(&ctx->buf, arg->name);
                    buf_append(&ctx->buf, ": ");
                }
                // Value
                switch (arg->kind) {
                    case ANNOT_ARG_STRING:
                        buf_append_char(&ctx->buf, '"');
                        buf_append(&ctx->buf, arg->value.string_val);
                        buf_append_char(&ctx->buf, '"');
                        break;
                    case ANNOT_ARG_NUMBER:
                        buf_printf(&ctx->buf, "%g", arg->value.number_val);
                        break;
                    case ANNOT_ARG_IDENT:
                        buf_append(&ctx->buf, arg->value.ident_val);
                        break;
                }
            }
            buf_append_char(&ctx->buf, ')');
        }
        fmt_newline(ctx);
    }
}

// ========== FUNCTION PARAMETER FORMATTING ==========

// Format function parameters with optional line breaking
// fn_name is optional (NULL for anonymous functions)
// prefix is "fn " for regular or "async fn " for async
void fmt_fn_params(FmtCtx *ctx, Expr *fn, const char *fn_name, const char *prefix) {
    if (!fn || fn->type != EXPR_FUNCTION) return;

    buf_append(&ctx->buf, prefix);
    if (fn_name) {
        buf_append(&ctx->buf, fn_name);
    }

    update_column(ctx);
    int params_len = estimate_fn_params_len(fn);
    int should_break = (ctx->column + params_len > FMT_MAX_LINE_WIDTH) &&
                       (fn->as.function.num_params > 1);

    buf_append_char(&ctx->buf, '(');
    if (should_break) {
        fmt_newline(ctx);
        ctx->indent++;
    }

    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (should_break) {
            fmt_indent(ctx);
        } else if (i > 0) {
            buf_append(&ctx->buf, ", ");
        }
        if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
            buf_append(&ctx->buf, "ref ");
        }
        if (fn->as.function.param_is_const && fn->as.function.param_is_const[i]) {
            buf_append(&ctx->buf, "const ");
        }
        buf_append(&ctx->buf, fn->as.function.param_names[i]);
        if (fn->as.function.param_types && fn->as.function.param_types[i] &&
            fn->as.function.param_types[i]->kind != TYPE_INFER) {
            if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
                buf_append(&ctx->buf, "?");
            }
            buf_append(&ctx->buf, ": ");
            fmt_type(ctx, fn->as.function.param_types[i]);
        } else if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            buf_append(&ctx->buf, "?: ");
            fmt_expr(ctx, fn->as.function.param_defaults[i]);
        }
        if (fn->as.function.param_types && fn->as.function.param_types[i] &&
            fn->as.function.param_types[i]->kind != TYPE_INFER &&
            fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            buf_append(&ctx->buf, " = ");
            fmt_expr(ctx, fn->as.function.param_defaults[i]);
        }
        if (should_break) {
            buf_append(&ctx->buf, ",");
            fmt_newline(ctx);
        }
    }

    if (fn->as.function.rest_param) {
        if (should_break) {
            fmt_indent(ctx);
        } else if (fn->as.function.num_params > 0) {
            buf_append(&ctx->buf, ", ");
        }
        buf_append(&ctx->buf, "...");
        buf_append(&ctx->buf, fn->as.function.rest_param);
        if (fn->as.function.rest_param_type) {
            buf_append(&ctx->buf, ": ");
            fmt_type(ctx, fn->as.function.rest_param_type);
        }
        if (should_break) {
            buf_append(&ctx->buf, ",");
            fmt_newline(ctx);
        }
    }

    if (should_break) {
        ctx->indent--;
        fmt_indent(ctx);
    }
    buf_append_char(&ctx->buf, ')');

    if (fn->as.function.return_type && fn->as.function.return_type->kind != TYPE_INFER) {
        buf_append(&ctx->buf, ": ");
        fmt_type(ctx, fn->as.function.return_type);
    }
}


// ========== PUBLIC API ==========

// Read entire file into a string (caller must free)
// read_file() provided by shared/file_io.h

static char *resolve_format_path(const char *path) {
    // Don't use module resolution - it adds .hml extension
    // Just resolve to absolute path if possible
    char *absolute = realpath(path, NULL);
    if (absolute) {
        return absolute;
    }
    // If realpath fails, just use the original path
    return strdup(path);
}

// Write string to file
static int write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s' for writing\n", path);
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);

    if (written != len) {
        fprintf(stderr, "Error: Could not write to file '%s'\n", path);
        return -1;
    }

    return 0;
}

// Helper to find the end line of a statement (approximate)
int stmt_end_line(Stmt *stmt) {
    if (!stmt) return 0;
    // For most statements, use the starting line
    // For blocks, try to get a rough end line by looking at last statement
    if (stmt->type == STMT_BLOCK && stmt->as.block.count > 0) {
        Stmt *last = stmt->as.block.statements[stmt->as.block.count - 1];
        return stmt_end_line(last);
    }
    // For if statements with else, track the else branch
    if (stmt->type == STMT_IF && stmt->as.if_stmt.else_branch) {
        return stmt_end_line(stmt->as.if_stmt.else_branch);
    }
    // For while/for/loop, track the body
    if (stmt->type == STMT_WHILE && stmt->as.while_stmt.body) {
        return stmt_end_line(stmt->as.while_stmt.body);
    }
    if (stmt->type == STMT_LOOP && stmt->as.loop_stmt.body) {
        return stmt_end_line(stmt->as.loop_stmt.body);
    }
    if (stmt->type == STMT_FOR && stmt->as.for_loop.body) {
        return stmt_end_line(stmt->as.for_loop.body);
    }
    if (stmt->type == STMT_FOR_IN && stmt->as.for_in.body) {
        return stmt_end_line(stmt->as.for_in.body);
    }
    // For function declarations with body
    if (stmt->type == STMT_LET && stmt->as.let.value &&
        stmt->as.let.value->type == EXPR_FUNCTION &&
        stmt->as.let.value->as.function.body) {
        return stmt_end_line(stmt->as.let.value->as.function.body);
    }
    // Default: use line + some offset for single-line statements
    return stmt->line;
}

char *format_source(const char *source) {
    // Extract comments before parsing (lexer discards them)
    CommentList comments;
    comment_list_init(&comments);
    extract_comments(source, &comments);

    // Extract blank lines for preservation
    BlankLineList blank_lines;
    blank_line_list_init(&blank_lines);
    extract_blank_lines(source, &blank_lines);

    // Extract original literal text (preserves hex/bin/oct and separators)
    LiteralMap literals;
    literal_map_init(&literals);
    extract_literals(source, &literals);

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Format failed: parse errors\n");
        comment_list_free(&comments);
        blank_line_list_free(&blank_lines);
        literal_map_free(&literals);
        return NULL;
    }

    // Format
    FmtCtx ctx;
    buf_init(&ctx.buf);
    ctx.indent = 0;
    ctx.column = 0;
    ctx.comments = &comments;
    ctx.blank_lines = &blank_lines;
    ctx.literals = &literals;
    ctx.output_line = 1;
    ctx.last_source_line = 0;
    ctx.is_else_if = false;

    // Check if we have statement line numbers (parser may set them to 0)
    int have_line_info = 0;
    for (int i = 0; i < stmt_count; i++) {
        if (statements[i]->line > 0) {
            have_line_info = 1;
            break;
        }
    }

    // If no line info, output all comments at the start (for idempotency)
    if (!have_line_info) {
        for (int i = 0; i < comments.count; i++) {
            Comment *c = &comments.comments[i];
            fmt_indent(&ctx);
            if (c->type == COMMENT_LINE) {
                buf_append(&ctx.buf, "// ");
                buf_append(&ctx.buf, c->text);
            } else {
                buf_append(&ctx.buf, "/* ");
                buf_append(&ctx.buf, c->text);
                buf_append(&ctx.buf, " */");
            }
            fmt_newline(&ctx);
            ctx.output_line++;
        }
        comments.next_idx = comments.count;  // Mark all as consumed
    }

    for (int i = 0; i < stmt_count; i++) {
        // Output leading comments before this statement
        if (have_line_info && statements[i]->line > 0) {
            fmt_leading_comments(&ctx, statements[i]->line);
        }
        fmt_stmt(&ctx, statements[i]);
        // Output trailing comments after this statement
        if (have_line_info && statements[i]->line > 0) {
            fmt_trailing_comment(&ctx, statements[i]->line);
            // Update last_source_line to track where we are
            ctx.last_source_line = stmt_end_line(statements[i]);
        }
    }

    // Output any remaining comments at end of file
    while (comments.next_idx < comments.count) {
        Comment *c = &comments.comments[comments.next_idx];

        // Check for blank line before this remaining comment
        if (ctx.last_source_line > 0 &&
            has_blank_line_between(&blank_lines, ctx.last_source_line, c->line)) {
            if (ctx.buf.len < 2 ||
                !(ctx.buf.data[ctx.buf.len - 1] == '\n' && ctx.buf.data[ctx.buf.len - 2] == '\n')) {
                fmt_newline(&ctx);
            }
        }

        comments.next_idx++;
        fmt_indent(&ctx);
        if (c->type == COMMENT_LINE) {
            buf_append(&ctx.buf, "// ");
            buf_append(&ctx.buf, c->text);
        } else {
            buf_append(&ctx.buf, "/* ");
            buf_append(&ctx.buf, c->text);
            buf_append(&ctx.buf, " */");
        }
        fmt_newline(&ctx);
        ctx.last_source_line = c->line;
    }

    // Cleanup AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    // Cleanup comments, blank lines, and literal map
    comment_list_free(&comments);
    blank_line_list_free(&blank_lines);
    literal_map_free(&literals);

    // Ensure exactly one trailing newline at end of file (POSIX convention)
    while (ctx.buf.len > 0 && ctx.buf.data[ctx.buf.len - 1] == '\n') {
        ctx.buf.len--;
        ctx.buf.data[ctx.buf.len] = '\0';
    }
    buf_append_char(&ctx.buf, '\n');

    // Transfer ownership of buffer
    char *result = ctx.buf.data;
    // Don't call buf_free - we're returning the buffer

    return result;
}

int format_file(const char *path) {
    char *resolved_path = resolve_format_path(path);
    char *source = read_file(resolved_path);
    if (source == NULL) {
        free(resolved_path);
        return 1;
    }

    char *formatted = format_source(source);
    free(source);

    if (formatted == NULL) {
        free(resolved_path);
        return 1;
    }

    int result = write_file(resolved_path, formatted);
    free(formatted);
    free(resolved_path);

    return result;
}

int format_check(const char *path) {
    char *resolved_path = resolve_format_path(path);
    char *source = read_file(resolved_path);
    if (source == NULL) {
        free(resolved_path);
        return -1;
    }

    char *formatted = format_source(source);
    if (formatted == NULL) {
        free(source);
        free(resolved_path);
        return -1;
    }

    int result = strcmp(source, formatted) == 0 ? 0 : 1;

    free(source);
    free(formatted);
    free(resolved_path);

    return result;
}

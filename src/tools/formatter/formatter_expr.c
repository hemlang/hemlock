#include "formatter_internal.h"
#include <ctype.h>

// ========== EXPRESSION FORMATTING ==========

const char *binary_op_str(BinaryOp op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQUAL: return "==";
        case OP_NOT_EQUAL: return "!=";
        case OP_LESS: return "<";
        case OP_LESS_EQUAL: return "<=";
        case OP_GREATER: return ">";
        case OP_GREATER_EQUAL: return ">=";
        case OP_AND: return "&&";
        case OP_OR: return "||";
        case OP_BIT_AND: return "&";
        case OP_BIT_OR: return "|";
        case OP_BIT_XOR: return "^";
        case OP_BIT_LSHIFT: return "<<";
        case OP_BIT_RSHIFT: return ">>";
        default: return "?";
    }
}

int needs_parens(Expr *parent, Expr *child, int is_right) {
    if (parent->type != EXPR_BINARY || child->type != EXPR_BINARY) {
        return 0;
    }

    // Simple precedence check
    int parent_prec = 0, child_prec = 0;

    BinaryOp pop = parent->as.binary.op;
    BinaryOp cop = child->as.binary.op;

    // Higher number = higher precedence
    if (pop == OP_OR) parent_prec = 1;
    else if (pop == OP_AND) parent_prec = 2;
    else if (pop == OP_BIT_OR) parent_prec = 3;
    else if (pop == OP_BIT_XOR) parent_prec = 4;
    else if (pop == OP_BIT_AND) parent_prec = 5;
    else if (pop == OP_EQUAL || pop == OP_NOT_EQUAL) parent_prec = 6;
    else if (pop == OP_LESS || pop == OP_LESS_EQUAL || pop == OP_GREATER || pop == OP_GREATER_EQUAL) parent_prec = 7;
    else if (pop == OP_BIT_LSHIFT || pop == OP_BIT_RSHIFT) parent_prec = 8;
    else if (pop == OP_ADD || pop == OP_SUB) parent_prec = 9;
    else if (pop == OP_MUL || pop == OP_DIV || pop == OP_MOD) parent_prec = 10;

    if (cop == OP_OR) child_prec = 1;
    else if (cop == OP_AND) child_prec = 2;
    else if (cop == OP_BIT_OR) child_prec = 3;
    else if (cop == OP_BIT_XOR) child_prec = 4;
    else if (cop == OP_BIT_AND) child_prec = 5;
    else if (cop == OP_EQUAL || cop == OP_NOT_EQUAL) child_prec = 6;
    else if (cop == OP_LESS || cop == OP_LESS_EQUAL || cop == OP_GREATER || cop == OP_GREATER_EQUAL) child_prec = 7;
    else if (cop == OP_BIT_LSHIFT || cop == OP_BIT_RSHIFT) child_prec = 8;
    else if (cop == OP_ADD || cop == OP_SUB) child_prec = 9;
    else if (cop == OP_MUL || cop == OP_DIV || cop == OP_MOD) child_prec = 10;

    if (child_prec < parent_prec) return 1;
    if (child_prec == parent_prec && is_right) return 1;
    return 0;
}

// Escape a string for output
void fmt_escaped_string(FmtCtx *ctx, const char *s) {
    buf_append_char(&ctx->buf, '"');
    while (*s) {
        unsigned char c = *s;
        switch (c) {
            case '\n': buf_append(&ctx->buf, "\\n"); break;
            case '\r': buf_append(&ctx->buf, "\\r"); break;
            case '\t': buf_append(&ctx->buf, "\\t"); break;
            case '\\': buf_append(&ctx->buf, "\\\\"); break;
            case '"': buf_append(&ctx->buf, "\\\""); break;
            default:
                if (c < 32) {
                    buf_printf(&ctx->buf, "\\x%02x", c);
                } else {
                    buf_append_char(&ctx->buf, c);
                }
        }
        s++;
    }
    buf_append_char(&ctx->buf, '"');
}

// Escape a template string part for output (different escapes than regular strings)
void fmt_escaped_template_part(FmtCtx *ctx, const char *s) {
    while (*s) {
        unsigned char c = *s;
        // Check for ${ which needs escaping to prevent re-interpretation
        if (c == '$' && s[1] == '{') {
            buf_append(&ctx->buf, "\\${");
            s += 2;
            continue;
        }
        switch (c) {
            case '`':  buf_append(&ctx->buf, "\\`"); break;
            case '\\': buf_append(&ctx->buf, "\\\\"); break;
            case '\n': buf_append(&ctx->buf, "\\n"); break;
            case '\r': buf_append(&ctx->buf, "\\r"); break;
            case '\t': buf_append(&ctx->buf, "\\t"); break;
            default:
                if (c < 32) {
                    buf_printf(&ctx->buf, "\\x%02x", c);
                } else {
                    buf_append_char(&ctx->buf, c);
                }
        }
        s++;
    }
}

// Format a rune literal
void fmt_rune(FmtCtx *ctx, uint32_t codepoint) {
    buf_append_char(&ctx->buf, '\'');
    if (codepoint == '\'') {
        buf_append(&ctx->buf, "\\'");
    } else if (codepoint == '\\') {
        buf_append(&ctx->buf, "\\\\");
    } else if (codepoint == '\n') {
        buf_append(&ctx->buf, "\\n");
    } else if (codepoint == '\r') {
        buf_append(&ctx->buf, "\\r");
    } else if (codepoint == '\t') {
        buf_append(&ctx->buf, "\\t");
    } else if (codepoint < 32) {
        buf_printf(&ctx->buf, "\\x%02x", codepoint);
    } else if (codepoint < 128) {
        buf_append_char(&ctx->buf, (char)codepoint);
    } else if (codepoint < 0x10000) {
        // UTF-8 encode
        char utf8[5];
        if (codepoint < 0x80) {
            utf8[0] = codepoint;
            utf8[1] = '\0';
        } else if (codepoint < 0x800) {
            utf8[0] = 0xC0 | (codepoint >> 6);
            utf8[1] = 0x80 | (codepoint & 0x3F);
            utf8[2] = '\0';
        } else {
            utf8[0] = 0xE0 | (codepoint >> 12);
            utf8[1] = 0x80 | ((codepoint >> 6) & 0x3F);
            utf8[2] = 0x80 | (codepoint & 0x3F);
            utf8[3] = '\0';
        }
        buf_append(&ctx->buf, utf8);
    } else {
        // 4-byte UTF-8
        char utf8[5];
        utf8[0] = 0xF0 | (codepoint >> 18);
        utf8[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        utf8[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[3] = 0x80 | (codepoint & 0x3F);
        utf8[4] = '\0';
        buf_append(&ctx->buf, utf8);
    }
    buf_append_char(&ctx->buf, '\'');
}

// Format a pattern (for match expressions)
void fmt_pattern(FmtCtx *ctx, Pattern *pattern) {
    if (!pattern) return;

    switch (pattern->type) {
        case PATTERN_LITERAL:
            fmt_expr(ctx, pattern->as.literal);
            break;

        case PATTERN_WILDCARD:
            buf_append_char(&ctx->buf, '_');
            break;

        case PATTERN_BINDING:
            buf_append(&ctx->buf, pattern->as.binding.name);
            break;

        case PATTERN_TYPED:
            if (pattern->as.typed.name) {
                buf_append(&ctx->buf, pattern->as.typed.name);
            } else {
                buf_append_char(&ctx->buf, '_');
            }
            if (pattern->as.typed.type_annotation) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, pattern->as.typed.type_annotation);
            }
            break;

        case PATTERN_OR:
            for (int i = 0; i < pattern->as.or_pattern.num_alternatives; i++) {
                if (i > 0) buf_append(&ctx->buf, " | ");
                fmt_pattern(ctx, pattern->as.or_pattern.alternatives[i]);
            }
            break;

        case PATTERN_OBJECT:
            buf_append(&ctx->buf, "{ ");
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                ObjectFieldPattern *field = &pattern->as.object.fields[i];
                buf_append(&ctx->buf, field->name);
                if (field->pattern) {
                    buf_append(&ctx->buf, ": ");
                    fmt_pattern(ctx, field->pattern);
                }
            }
            if (pattern->as.object.has_rest) {
                if (pattern->as.object.num_fields > 0) buf_append(&ctx->buf, ", ");
                buf_append(&ctx->buf, "...");
                if (pattern->as.object.rest_name) {
                    buf_append(&ctx->buf, pattern->as.object.rest_name);
                }
            }
            buf_append(&ctx->buf, " }");
            break;

        case PATTERN_ARRAY:
            buf_append_char(&ctx->buf, '[');
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                ArrayElementPattern *elem = &pattern->as.array.elements[i];
                if (elem->is_rest) {
                    buf_append(&ctx->buf, "...");
                    if (elem->rest_name) {
                        buf_append(&ctx->buf, elem->rest_name);
                    }
                } else {
                    fmt_pattern(ctx, elem->pattern);
                }
            }
            buf_append_char(&ctx->buf, ']');
            break;
    }
}

void fmt_expr(FmtCtx *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_NUMBER: {
            // Try to use original literal text (preserves hex/bin/oct and separators)
            const char *original = literal_map_lookup(ctx->literals, expr->line, expr->column);
            if (original) {
                buf_append(&ctx->buf, original);
            } else if (expr->as.number.is_float) {
                buf_printf(&ctx->buf, "%g", expr->as.number.float_value);
            } else if (expr->as.number.is_u64) {
                buf_printf(&ctx->buf, "%" PRIu64, expr->as.number.uint_value);
            } else {
                buf_printf(&ctx->buf, "%ld", (long)expr->as.number.int_value);
            }
            break;
        }

        case EXPR_BOOL:
            buf_append(&ctx->buf, expr->as.boolean ? "true" : "false");
            break;

        case EXPR_STRING:
            fmt_escaped_string(ctx, expr->as.string);
            break;

        case EXPR_RUNE:
            fmt_rune(ctx, expr->as.rune);
            break;

        case EXPR_IDENT:
            buf_append(&ctx->buf, expr->as.ident.name);
            break;

        case EXPR_NULL:
            buf_append(&ctx->buf, "null");
            break;

        case EXPR_BINARY: {
            int left_parens = needs_parens(expr, expr->as.binary.left, 0);
            int right_parens = needs_parens(expr, expr->as.binary.right, 1);

            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            // Break if expression is too long (only for logical/comparison operators)
            BinaryOp op = expr->as.binary.op;
            int is_breakable_op = (op == OP_AND || op == OP_OR ||
                                   op == OP_EQUAL || op == OP_NOT_EQUAL ||
                                   op == OP_LESS || op == OP_LESS_EQUAL ||
                                   op == OP_GREATER || op == OP_GREATER_EQUAL);
            int should_break = is_breakable_op &&
                               (ctx->column + total_len > FMT_MAX_LINE_WIDTH);

            if (left_parens) buf_append_char(&ctx->buf, '(');
            fmt_expr(ctx, expr->as.binary.left);
            if (left_parens) buf_append_char(&ctx->buf, ')');

            buf_append_char(&ctx->buf, ' ');
            buf_append(&ctx->buf, binary_op_str(expr->as.binary.op));
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
                fmt_indent(ctx);
                ctx->indent--;
            } else {
                buf_append_char(&ctx->buf, ' ');
            }

            if (right_parens) buf_append_char(&ctx->buf, '(');
            fmt_expr(ctx, expr->as.binary.right);
            if (right_parens) buf_append_char(&ctx->buf, ')');
            break;
        }

        case EXPR_UNARY:
            switch (expr->as.unary.op) {
                case UNARY_NOT: buf_append_char(&ctx->buf, '!'); break;
                case UNARY_NEGATE: buf_append_char(&ctx->buf, '-'); break;
                case UNARY_BIT_NOT: buf_append_char(&ctx->buf, '~'); break;
            }
            fmt_expr(ctx, expr->as.unary.operand);
            break;

        case EXPR_TERNARY:
            fmt_expr(ctx, expr->as.ternary.condition);
            buf_append(&ctx->buf, " ? ");
            fmt_expr(ctx, expr->as.ternary.true_expr);
            buf_append(&ctx->buf, " : ");
            fmt_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_CALL: {
            fmt_expr(ctx, expr->as.call.func);
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int num_complex = count_complex_elements(expr->as.call.args, expr->as.call.num_args);
            // Break if: (line too long AND multiple args) OR (2+ complex args)
            int should_break = ((ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.call.num_args > 1)) ||
                               (num_complex >= 2);

            buf_append(&ctx->buf, "(");
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            }
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }
                // Output named argument prefix if present
                if (expr->as.call.arg_names && expr->as.call.arg_names[i]) {
                    buf_append(&ctx->buf, expr->as.call.arg_names[i]);
                    buf_append(&ctx->buf, ": ");
                }
                fmt_expr(ctx, expr->as.call.args[i]);
                if (should_break) {
                    // Remove trailing newline if present (from function body)
                    if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                        ctx->buf.len--;
                        ctx->buf.data[ctx->buf.len] = '\0';
                    }
                    buf_append(&ctx->buf, ",");  // Always add trailing comma in multiline
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            }
            buf_append(&ctx->buf, ")");
            break;
        }

        case EXPR_ASSIGN:
            buf_append(&ctx->buf, expr->as.assign.name);
            buf_append(&ctx->buf, " = ");
            fmt_expr(ctx, expr->as.assign.value);
            break;

        case EXPR_GET_PROPERTY: {
            // Check if this is part of a method chain that's too long
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int chain_depth = count_method_chain_depth(expr);
            int should_break = (chain_depth >= 2) &&
                               (ctx->column + total_len > FMT_MAX_LINE_WIDTH);

            fmt_expr(ctx, expr->as.get_property.object);
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
                fmt_indent(ctx);
                ctx->indent--;
            }
            buf_append_char(&ctx->buf, '.');
            buf_append(&ctx->buf, expr->as.get_property.property);
            break;
        }

        case EXPR_SET_PROPERTY:
            fmt_expr(ctx, expr->as.set_property.object);
            buf_append_char(&ctx->buf, '.');
            buf_append(&ctx->buf, expr->as.set_property.property);
            buf_append(&ctx->buf, " = ");
            fmt_expr(ctx, expr->as.set_property.value);
            break;

        case EXPR_INDEX:
            fmt_expr(ctx, expr->as.index.object);
            buf_append_char(&ctx->buf, '[');
            fmt_expr(ctx, expr->as.index.index);
            buf_append_char(&ctx->buf, ']');
            break;

        case EXPR_INDEX_ASSIGN:
            fmt_expr(ctx, expr->as.index_assign.object);
            buf_append_char(&ctx->buf, '[');
            fmt_expr(ctx, expr->as.index_assign.index);
            buf_append(&ctx->buf, "] = ");
            fmt_expr(ctx, expr->as.index_assign.value);
            break;

        case EXPR_FUNCTION: {
            const char *prefix = expr->as.function.is_async ? "async fn" : "fn";
            fmt_fn_params(ctx, expr, NULL, prefix);
            buf_append(&ctx->buf, " ");
            fmt_stmt(ctx, expr->as.function.body);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int num_complex = count_complex_elements(expr->as.array_literal.elements,
                                                     expr->as.array_literal.num_elements);
            // Break if: (line too long AND multiple elements) OR (2+ complex elements)
            int should_break = ((ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.array_literal.num_elements > 1)) ||
                               (num_complex >= 2);

            buf_append(&ctx->buf, "[");
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            }
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }
                fmt_expr(ctx, expr->as.array_literal.elements[i]);
                if (should_break) {
                    // Remove trailing newline if present (from function body)
                    if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                        ctx->buf.len--;
                        ctx->buf.data[ctx->buf.len] = '\0';
                    }
                    buf_append(&ctx->buf, ",");  // Always add trailing comma in multiline
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            }
            buf_append(&ctx->buf, "]");
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int num_complex = count_complex_elements(expr->as.object_literal.field_values,
                                                     expr->as.object_literal.num_fields);
            // Break if: (line too long AND multiple fields) OR (2+ complex values)
            int should_break = ((ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.object_literal.num_fields > 1)) ||
                               (num_complex >= 2);

            buf_append(&ctx->buf, "{");
            if (expr->as.object_literal.num_fields == 0) {
                buf_append(&ctx->buf, "}");
                break;
            }
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            } else {
                buf_append(&ctx->buf, " ");
            }
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }

                // Check for spread (field_names[i] is NULL for spread)
                if (expr->as.object_literal.field_names[i] == NULL) {
                    buf_append(&ctx->buf, "...");
                    fmt_expr(ctx, expr->as.object_literal.field_values[i]);
                } else {
                    // Check for shorthand: { name } where value is identifier with same name
                    int is_shorthand = 0;
                    Expr *val = expr->as.object_literal.field_values[i];
                    if (val && val->type == EXPR_IDENT && val->as.ident.name &&
                        strcmp(expr->as.object_literal.field_names[i], val->as.ident.name) == 0) {
                        is_shorthand = 1;
                    }

                    if (is_shorthand) {
                        buf_append(&ctx->buf, expr->as.object_literal.field_names[i]);
                    } else {
                        const char *name = expr->as.object_literal.field_names[i];
                        // Quote keys that aren't valid bare identifiers
                        // (e.g. "chat-mahou", "1st-place", "with space").
                        int needs_quoting = 0;
                        if (!name || !*name) {
                            needs_quoting = 1;
                        } else {
                            unsigned char first = (unsigned char)name[0];
                            if (!(isalpha(first) || first == '_')) needs_quoting = 1;
                            for (const char *p = name + 1; *p && !needs_quoting; p++) {
                                unsigned char c = (unsigned char)*p;
                                if (!(isalnum(c) || c == '_')) needs_quoting = 1;
                            }
                        }
                        if (needs_quoting) {
                            buf_append_char(&ctx->buf, '"');
                            buf_append(&ctx->buf, name);
                            buf_append_char(&ctx->buf, '"');
                        } else {
                            buf_append(&ctx->buf, name);
                        }
                        buf_append(&ctx->buf, ": ");
                        fmt_expr(ctx, expr->as.object_literal.field_values[i]);
                    }
                }
                if (should_break) {
                    // Remove trailing newline if present (from function body)
                    if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                        ctx->buf.len--;
                        ctx->buf.data[ctx->buf.len] = '\0';
                    }
                    buf_append(&ctx->buf, ",");  // Always add trailing comma in multiline
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            } else {
                buf_append(&ctx->buf, " ");
            }
            buf_append(&ctx->buf, "}");
            break;
        }

        case EXPR_PREFIX_INC:
            buf_append(&ctx->buf, "++");
            fmt_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_PREFIX_DEC:
            buf_append(&ctx->buf, "--");
            fmt_expr(ctx, expr->as.prefix_dec.operand);
            break;

        case EXPR_POSTFIX_INC:
            fmt_expr(ctx, expr->as.postfix_inc.operand);
            buf_append(&ctx->buf, "++");
            break;

        case EXPR_POSTFIX_DEC:
            fmt_expr(ctx, expr->as.postfix_dec.operand);
            buf_append(&ctx->buf, "--");
            break;

        case EXPR_AWAIT:
            buf_append(&ctx->buf, "await ");
            fmt_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            buf_append_char(&ctx->buf, '`');
            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                // String part (escape backticks, backslashes, and ${ sequences)
                if (expr->as.string_interpolation.string_parts[i]) {
                    fmt_escaped_template_part(ctx, expr->as.string_interpolation.string_parts[i]);
                }
                // Expression part (except after last string)
                if (i < expr->as.string_interpolation.num_parts) {
                    buf_append(&ctx->buf, "${");
                    fmt_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                    buf_append_char(&ctx->buf, '}');
                }
            }
            buf_append_char(&ctx->buf, '`');
            break;

        case EXPR_OPTIONAL_CHAIN:
            fmt_expr(ctx, expr->as.optional_chain.object);
            buf_append(&ctx->buf, "?");
            if (expr->as.optional_chain.is_call) {
                buf_append_char(&ctx->buf, '(');
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    fmt_expr(ctx, expr->as.optional_chain.args[i]);
                }
                buf_append_char(&ctx->buf, ')');
            } else if (expr->as.optional_chain.is_property) {
                buf_append_char(&ctx->buf, '.');
                buf_append(&ctx->buf, expr->as.optional_chain.property);
            } else {
                buf_append(&ctx->buf, ".[");
                fmt_expr(ctx, expr->as.optional_chain.index);
                buf_append_char(&ctx->buf, ']');
            }
            break;

        case EXPR_NULL_COALESCE:
            fmt_expr(ctx, expr->as.null_coalesce.left);
            buf_append(&ctx->buf, " ?? ");
            fmt_expr(ctx, expr->as.null_coalesce.right);
            break;

        case EXPR_MATCH:
            buf_append(&ctx->buf, "match (");
            fmt_expr(ctx, expr->as.match_expr.scrutinee);
            buf_append(&ctx->buf, ") {");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                fmt_indent(ctx);
                fmt_pattern(ctx, arm->pattern);
                if (arm->guard) {
                    buf_append(&ctx->buf, " if ");
                    fmt_expr(ctx, arm->guard);
                }
                buf_append(&ctx->buf, " => ");
                fmt_expr(ctx, arm->body);
                if (i < expr->as.match_expr.num_arms - 1) {
                    buf_append_char(&ctx->buf, ',');
                }
                fmt_newline(ctx);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append_char(&ctx->buf, '}');
            break;
    }
}

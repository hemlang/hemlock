#include "formatter_internal.h"

// ========== STATEMENT FORMATTING ==========

void fmt_stmt(FmtCtx *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET:
            // Check if this is a named function declaration (fn name(...) { ... })
            if (stmt->as.let.value && stmt->as.let.value->type == EXPR_FUNCTION) {
                // Output annotations before the function
                fmt_annotations(ctx, stmt->as.let.annotations, stmt->as.let.annotation_count);
                fmt_indent(ctx);
                Expr *fn = stmt->as.let.value;
                const char *prefix = fn->as.function.is_async ? "async fn " : "fn ";
                fmt_fn_params(ctx, fn, stmt->as.let.name, prefix);
                buf_append(&ctx->buf, " ");
                fmt_stmt(ctx, fn->as.function.body);
                // No semicolon after function body
            } else {
                fmt_indent(ctx);
                buf_append(&ctx->buf, "let ");
                buf_append(&ctx->buf, stmt->as.let.name);
                if (stmt->as.let.type_annotation && stmt->as.let.type_annotation->kind != TYPE_INFER) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, stmt->as.let.type_annotation);
                }
                if (stmt->as.let.value) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.let.value);
                }
                buf_append(&ctx->buf, ";");
                fmt_newline(ctx);
            }
            break;

        case STMT_CONST:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "const ");
            buf_append(&ctx->buf, stmt->as.const_stmt.name);
            if (stmt->as.const_stmt.type_annotation && stmt->as.const_stmt.type_annotation->kind != TYPE_INFER) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, stmt->as.const_stmt.type_annotation);
            }
            if (stmt->as.const_stmt.value) {
                buf_append(&ctx->buf, " = ");
                fmt_expr(ctx, stmt->as.const_stmt.value);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_EXPR:
            fmt_indent(ctx);
            fmt_expr(ctx, stmt->as.expr);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_IF:
            // Skip leading indent for else-if (follows "else " on same line)
            if (ctx->is_else_if) {
                ctx->is_else_if = false;
            } else {
                fmt_indent(ctx);
            }
            buf_append(&ctx->buf, "if (");
            fmt_expr(ctx, stmt->as.if_stmt.condition);
            buf_append(&ctx->buf, ") ");
            // Handle block vs single statement
            if (stmt->as.if_stmt.then_branch->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.if_stmt.then_branch);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.if_stmt.then_branch);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            if (stmt->as.if_stmt.else_branch) {
                // Remove trailing newline for else
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " else ");
                if (stmt->as.if_stmt.else_branch->type == STMT_IF) {
                    // else if - don't add braces, skip leading indent
                    ctx->is_else_if = true;
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                } else if (stmt->as.if_stmt.else_branch->type == STMT_BLOCK) {
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                } else {
                    buf_append(&ctx->buf, "{");
                    fmt_newline(ctx);
                    ctx->indent++;
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                    ctx->indent--;
                    fmt_indent(ctx);
                    buf_append(&ctx->buf, "}");
                    fmt_newline(ctx);
                }
            }
            break;

        case STMT_WHILE:
            fmt_indent(ctx);
            // Output label if present
            if (stmt->as.while_stmt.label) {
                buf_append(&ctx->buf, stmt->as.while_stmt.label);
                buf_append(&ctx->buf, ": ");
            }
            buf_append(&ctx->buf, "while (");
            fmt_expr(ctx, stmt->as.while_stmt.condition);
            buf_append(&ctx->buf, ") ");
            if (stmt->as.while_stmt.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.while_stmt.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.while_stmt.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_LOOP:
            fmt_indent(ctx);
            // Output label if present
            if (stmt->as.loop_stmt.label) {
                buf_append(&ctx->buf, stmt->as.loop_stmt.label);
                buf_append(&ctx->buf, ": ");
            }
            buf_append(&ctx->buf, "loop ");
            if (stmt->as.loop_stmt.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.loop_stmt.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.loop_stmt.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_FOR:
            fmt_indent(ctx);
            // Output label if present
            if (stmt->as.for_loop.label) {
                buf_append(&ctx->buf, stmt->as.for_loop.label);
                buf_append(&ctx->buf, ": ");
            }
            buf_append(&ctx->buf, "for (");
            // Initializer (without indent and newline)
            if (stmt->as.for_loop.initializer) {
                if (stmt->as.for_loop.initializer->type == STMT_LET) {
                    buf_append(&ctx->buf, "let ");
                    buf_append(&ctx->buf, stmt->as.for_loop.initializer->as.let.name);
                    if (stmt->as.for_loop.initializer->as.let.value) {
                        buf_append(&ctx->buf, " = ");
                        fmt_expr(ctx, stmt->as.for_loop.initializer->as.let.value);
                    }
                } else if (stmt->as.for_loop.initializer->type == STMT_EXPR) {
                    fmt_expr(ctx, stmt->as.for_loop.initializer->as.expr);
                }
            }
            buf_append(&ctx->buf, "; ");
            if (stmt->as.for_loop.condition) {
                fmt_expr(ctx, stmt->as.for_loop.condition);
            }
            buf_append(&ctx->buf, "; ");
            if (stmt->as.for_loop.increment) {
                fmt_expr(ctx, stmt->as.for_loop.increment);
            }
            buf_append(&ctx->buf, ") ");
            if (stmt->as.for_loop.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.for_loop.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.for_loop.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_FOR_IN:
            fmt_indent(ctx);
            // Output label if present
            if (stmt->as.for_in.label) {
                buf_append(&ctx->buf, stmt->as.for_in.label);
                buf_append(&ctx->buf, ": ");
            }
            buf_append(&ctx->buf, "for (");
            if (stmt->as.for_in.key_var) {
                buf_append(&ctx->buf, stmt->as.for_in.key_var);
                buf_append(&ctx->buf, ", ");
            }
            buf_append(&ctx->buf, stmt->as.for_in.value_var);
            buf_append(&ctx->buf, " in ");
            fmt_expr(ctx, stmt->as.for_in.iterable);
            buf_append(&ctx->buf, ") ");
            if (stmt->as.for_in.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.for_in.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.for_in.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_BREAK:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "break");
            if (stmt->as.break_stmt.label) {
                buf_append_char(&ctx->buf, ' ');
                buf_append(&ctx->buf, stmt->as.break_stmt.label);
            }
            buf_append_char(&ctx->buf, ';');
            fmt_newline(ctx);
            break;

        case STMT_CONTINUE:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "continue");
            if (stmt->as.continue_stmt.label) {
                buf_append_char(&ctx->buf, ' ');
                buf_append(&ctx->buf, stmt->as.continue_stmt.label);
            }
            buf_append_char(&ctx->buf, ';');
            fmt_newline(ctx);
            break;

        case STMT_BLOCK:
            buf_append(&ctx->buf, "{");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < stmt->as.block.count; i++) {
                Stmt *inner = stmt->as.block.statements[i];
                if (inner->line > 0) {
                    fmt_leading_comments(ctx, inner->line);
                }
                fmt_stmt(ctx, inner);
                if (inner->line > 0) {
                    fmt_trailing_comment(ctx, inner->line);
                }
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_RETURN:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "return");
            if (stmt->as.return_stmt.value) {
                buf_append_char(&ctx->buf, ' ');
                fmt_expr(ctx, stmt->as.return_stmt.value);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_DEFINE_OBJECT:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "define ");
            buf_append(&ctx->buf, stmt->as.define_object.name);
            // Type parameters
            if (stmt->as.define_object.num_type_params > 0) {
                buf_append_char(&ctx->buf, '<');
                for (int i = 0; i < stmt->as.define_object.num_type_params; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.define_object.type_params[i]);
                }
                buf_append_char(&ctx->buf, '>');
            }
            buf_append(&ctx->buf, " {");
            fmt_newline(ctx);
            ctx->indent++;
            // Fields
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                fmt_indent(ctx);
                buf_append(&ctx->buf, stmt->as.define_object.field_names[i]);
                if (stmt->as.define_object.field_optional && stmt->as.define_object.field_optional[i]) {
                    buf_append_char(&ctx->buf, '?');
                }
                if (stmt->as.define_object.field_types && stmt->as.define_object.field_types[i]) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, stmt->as.define_object.field_types[i]);
                }
                if (stmt->as.define_object.field_defaults && stmt->as.define_object.field_defaults[i]) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.define_object.field_defaults[i]);
                }
                buf_append_char(&ctx->buf, ',');  // Always add trailing comma
                fmt_newline(ctx);
            }
            // Method signatures
            for (int i = 0; i < stmt->as.define_object.num_methods; i++) {
                fmt_indent(ctx);
                buf_append(&ctx->buf, "fn ");
                buf_append(&ctx->buf, stmt->as.define_object.method_names[i]);
                if (stmt->as.define_object.method_optional && stmt->as.define_object.method_optional[i]) {
                    buf_append_char(&ctx->buf, '?');
                }
                // Format method type (function signature)
                if (stmt->as.define_object.method_types && stmt->as.define_object.method_types[i]) {
                    Type *mt = stmt->as.define_object.method_types[i];
                    if (mt->kind == TYPE_FUNCTION) {
                        buf_append_char(&ctx->buf, '(');
                        for (int j = 0; j < mt->fn_num_params; j++) {
                            if (j > 0) buf_append(&ctx->buf, ", ");
                            if (mt->fn_param_names && mt->fn_param_names[j]) {
                                buf_append(&ctx->buf, mt->fn_param_names[j]);
                                buf_append(&ctx->buf, ": ");
                            }
                            if (mt->fn_param_types && mt->fn_param_types[j]) {
                                fmt_type(ctx, mt->fn_param_types[j]);
                            }
                        }
                        buf_append_char(&ctx->buf, ')');
                        if (mt->fn_return_type && mt->fn_return_type->kind != TYPE_INFER) {
                            buf_append(&ctx->buf, ": ");
                            fmt_type(ctx, mt->fn_return_type);
                        }
                    }
                }
                buf_append_char(&ctx->buf, ',');
                fmt_newline(ctx);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_ENUM:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "enum ");
            buf_append(&ctx->buf, stmt->as.enum_decl.name);
            buf_append(&ctx->buf, " {");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                fmt_indent(ctx);
                buf_append(&ctx->buf, stmt->as.enum_decl.variant_names[i]);
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.enum_decl.variant_values[i]);
                }
                buf_append_char(&ctx->buf, ',');  // Always add trailing comma
                fmt_newline(ctx);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_TRY:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "try ");
            fmt_stmt(ctx, stmt->as.try_stmt.try_block);
            if (stmt->as.try_stmt.catch_block) {
                // Remove trailing newline for catch
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " catch");
                if (stmt->as.try_stmt.catch_param) {
                    buf_append(&ctx->buf, " (");
                    buf_append(&ctx->buf, stmt->as.try_stmt.catch_param);
                    buf_append_char(&ctx->buf, ')');
                }
                buf_append_char(&ctx->buf, ' ');
                fmt_stmt(ctx, stmt->as.try_stmt.catch_block);
            }
            if (stmt->as.try_stmt.finally_block) {
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " finally ");
                fmt_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            break;

        case STMT_THROW:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "throw ");
            fmt_expr(ctx, stmt->as.throw_stmt.value);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_SWITCH:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "switch (");
            fmt_expr(ctx, stmt->as.switch_stmt.expr);
            buf_append(&ctx->buf, ") {");
            fmt_newline(ctx);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                fmt_indent(ctx);
                if (stmt->as.switch_stmt.case_values[i]) {
                    buf_append(&ctx->buf, "case ");
                    fmt_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                    buf_append(&ctx->buf, ":");
                } else {
                    buf_append(&ctx->buf, "default:");
                }
                fmt_newline(ctx);
                if (stmt->as.switch_stmt.case_bodies[i]) {
                    ctx->indent++;
                    if (stmt->as.switch_stmt.case_bodies[i]->type == STMT_BLOCK) {
                        for (int j = 0; j < stmt->as.switch_stmt.case_bodies[i]->as.block.count; j++) {
                            fmt_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]->as.block.statements[j]);
                        }
                    } else {
                        fmt_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                    }
                    ctx->indent--;
                }
            }
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_DEFER:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "defer ");
            fmt_expr(ctx, stmt->as.defer_stmt.call);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_IMPORT:
            fmt_indent(ctx);
            if (stmt->as.import_stmt.is_namespace) {
                if (stmt->as.import_stmt.namespace_name) {
                    buf_append(&ctx->buf, "import * as ");
                    buf_append(&ctx->buf, stmt->as.import_stmt.namespace_name);
                } else {
                    // Star import: import * from "module"
                    buf_append(&ctx->buf, "import *");
                }
            } else if (stmt->as.import_stmt.num_imports == 0 &&
                       stmt->as.import_stmt.module_path &&
                       (stmt->as.import_stmt.module_path[0] == '@' ||
                        (strlen(stmt->as.import_stmt.module_path) > 4 &&
                         strcmp(stmt->as.import_stmt.module_path +
                                strlen(stmt->as.import_stmt.module_path) - 4, ".hml") == 0))) {
                // Side-effect import: import "module"; (no bindings).
                // Only paths that re-parse as source imports (".hml" suffix or
                // "@" prefix) may use the bare spelling; others keep the
                // `import { } from "path"` form so round-tripping is stable.
                buf_append(&ctx->buf, "import \"");
                buf_append(&ctx->buf, stmt->as.import_stmt.module_path);
                buf_append(&ctx->buf, "\";");
                fmt_newline(ctx);
                break;
            } else {
                int import_len = estimate_import_len(stmt);
                int should_break = (import_len > FMT_MAX_LINE_WIDTH) &&
                                   (stmt->as.import_stmt.num_imports > 2);

                buf_append(&ctx->buf, "import {");
                if (should_break) {
                    fmt_newline(ctx);
                    ctx->indent++;
                } else {
                    buf_append_char(&ctx->buf, ' ');
                }
                for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
                    if (should_break) {
                        fmt_indent(ctx);
                    } else if (i > 0) {
                        buf_append(&ctx->buf, ", ");
                    }
                    buf_append(&ctx->buf, stmt->as.import_stmt.import_names[i]);
                    if (stmt->as.import_stmt.import_aliases && stmt->as.import_stmt.import_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.import_stmt.import_aliases[i]);
                    }
                    if (should_break) {
                        buf_append(&ctx->buf, ",");
                        fmt_newline(ctx);
                    }
                }
                if (should_break) {
                    ctx->indent--;
                    fmt_indent(ctx);
                    buf_append_char(&ctx->buf, '}');
                } else {
                    buf_append(&ctx->buf, " }");
                }
            }
            buf_append(&ctx->buf, " from \"");
            buf_append(&ctx->buf, stmt->as.import_stmt.module_path);
            buf_append(&ctx->buf, "\";");
            fmt_newline(ctx);
            break;

        case STMT_EXPORT:
            fmt_indent(ctx);
            if (stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
                buf_append(&ctx->buf, "export ");
                // Format the declaration inline without indent
                int saved_indent = ctx->indent;
                ctx->indent = 0;
                // Handle function declarations specially
                Stmt *decl = stmt->as.export_stmt.declaration;
                if (decl->type == STMT_LET && decl->as.let.value &&
                    decl->as.let.value->type == EXPR_FUNCTION) {
                    // export fn name(...) { }
                    Expr *fn = decl->as.let.value;
                    const char *prefix = fn->as.function.is_async ? "async fn " : "fn ";
                    fmt_fn_params(ctx, fn, decl->as.let.name, prefix);
                    buf_append(&ctx->buf, " ");
                    fmt_stmt(ctx, fn->as.function.body);
                } else {
                    fmt_stmt(ctx, decl);
                }
                ctx->indent = saved_indent;
            } else if (stmt->as.export_stmt.is_reexport) {
                buf_append(&ctx->buf, "export { ");
                for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.export_stmt.export_names[i]);
                    if (stmt->as.export_stmt.export_aliases && stmt->as.export_stmt.export_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.export_stmt.export_aliases[i]);
                    }
                }
                buf_append(&ctx->buf, " } from \"");
                buf_append(&ctx->buf, stmt->as.export_stmt.module_path);
                buf_append(&ctx->buf, "\";");
                fmt_newline(ctx);
            } else {
                buf_append(&ctx->buf, "export { ");
                for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.export_stmt.export_names[i]);
                    if (stmt->as.export_stmt.export_aliases && stmt->as.export_stmt.export_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.export_stmt.export_aliases[i]);
                    }
                }
                buf_append(&ctx->buf, " };");
                fmt_newline(ctx);
            }
            break;

        case STMT_IMPORT_FFI:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "import \"");
            buf_append(&ctx->buf, stmt->as.import_ffi.library_path);
            buf_append(&ctx->buf, "\";");
            fmt_newline(ctx);
            break;

        case STMT_EXTERN_FN:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "extern fn ");
            buf_append(&ctx->buf, stmt->as.extern_fn.function_name);
            buf_append_char(&ctx->buf, '(');
            for (int i = 0; i < stmt->as.extern_fn.num_params; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                if (stmt->as.extern_fn.param_names && stmt->as.extern_fn.param_names[i]) {
                    buf_append(&ctx->buf, stmt->as.extern_fn.param_names[i]);
                    buf_append(&ctx->buf, ": ");
                }
                if (stmt->as.extern_fn.param_types && stmt->as.extern_fn.param_types[i]) {
                    fmt_type(ctx, stmt->as.extern_fn.param_types[i]);
                }
            }
            buf_append_char(&ctx->buf, ')');
            if (stmt->as.extern_fn.return_type) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, stmt->as.extern_fn.return_type);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_TYPE_ALIAS:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "type ");
            buf_append(&ctx->buf, stmt->as.type_alias.name);
            // Type parameters (e.g., <T, U>)
            if (stmt->as.type_alias.num_type_params > 0) {
                buf_append_char(&ctx->buf, '<');
                for (int i = 0; i < stmt->as.type_alias.num_type_params; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.type_alias.type_params[i]);
                }
                buf_append_char(&ctx->buf, '>');
            }
            buf_append(&ctx->buf, " = ");
            fmt_type(ctx, stmt->as.type_alias.aliased_type);
            buf_append_char(&ctx->buf, ';');
            fmt_newline(ctx);
            break;
    }
}

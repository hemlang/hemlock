/*
 * Hemlock Code Generator - Match Expression & Pattern Matching
 * Extracted from codegen_expr.c
 */

#include "codegen_internal.h"
#include "codegen_expr_internal.h"

// Forward declaration
char* codegen_expr(CodegenContext *ctx, Expr *expr);

// ========== PATTERN MATCHING CODE GENERATION ==========

// Helper to get HML type enum for a TypeKind
static const char* type_kind_to_hml_type_enum(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return "HML_VAL_I8";
        case TYPE_I16: return "HML_VAL_I16";
        case TYPE_I32: return "HML_VAL_I32";
        case TYPE_I64: return "HML_VAL_I64";
        case TYPE_U8: return "HML_VAL_U8";
        case TYPE_U16: return "HML_VAL_U16";
        case TYPE_U32: return "HML_VAL_U32";
        case TYPE_U64: return "HML_VAL_U64";
        case TYPE_F32: return "HML_VAL_F32";
        case TYPE_F64: return "HML_VAL_F64";
        case TYPE_BOOL: return "HML_VAL_BOOL";
        case TYPE_STRING: return "HML_VAL_STRING";
        case TYPE_RUNE: return "HML_VAL_RUNE";
        case TYPE_PTR: return "HML_VAL_PTR";
        case TYPE_BUFFER: return "HML_VAL_BUFFER";
        case TYPE_ARRAY: return "HML_VAL_ARRAY";
        case TYPE_NULL: return "HML_VAL_NULL";
        case TYPE_GENERIC_OBJECT: return "HML_VAL_OBJECT";
        case TYPE_CUSTOM_OBJECT: return "HML_VAL_OBJECT";
        case TYPE_FUNCTION: return "HML_VAL_FUNCTION";
        default: return NULL;
    }
}

// Generate code to match a pattern against a value
void codegen_pattern_match(CodegenContext *ctx, Pattern *pattern, const char *scrutinee, const char *fail_label) {
    if (!pattern) {
        codegen_writeln(ctx, "goto %s;", fail_label);
        return;
    }

    switch (pattern->type) {
        case PATTERN_WILDCARD:
            // Wildcard always matches - no code needed
            break;

        case PATTERN_LITERAL: {
            // Generate literal value and compare
            char *lit_val = codegen_expr(ctx, pattern->as.literal);
            codegen_writeln(ctx, "if (!hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", scrutinee, lit_val);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_release(&%s);", lit_val);
            codegen_writeln(ctx, "goto %s;", fail_label);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", lit_val);
            free(lit_val);
            break;
        }

        case PATTERN_BINDING: {
            // Bind the value to a local variable
            // Use the actual variable name so identifier lookup works
            // Clear any stale unboxable mark left by a prior same-named variable
            // (e.g. a previous `for (let name = 0; ...)` counter), otherwise
            // codegen_expr_ident would treat this binding as a native C
            // primitive and emit hml_val_i32(x) on an HmlValue.
            if (ctx->type_ctx) {
                type_check_clear_unboxable(ctx->type_ctx, pattern->as.binding.name);
            }
            codegen_add_local(ctx, pattern->as.binding.name);
            if (ctx->current_scope) {
                scope_add_var(ctx->current_scope, pattern->as.binding.name);
            }
            codegen_writeln(ctx, "HmlValue %s = %s;", pattern->as.binding.name, scrutinee);
            codegen_writeln(ctx, "hml_retain(&%s);", pattern->as.binding.name);
            break;
        }

        case PATTERN_TYPED: {
            // Check type first
            const char *type_enum = type_kind_to_hml_type_enum(pattern->as.typed.type_annotation->kind);
            if (type_enum) {
                codegen_writeln(ctx, "if (%s.type != %s) goto %s;", scrutinee, type_enum, fail_label);
            } else {
                // For complex types like functions, check multiple possibilities
                if (pattern->as.typed.type_annotation->kind == TYPE_FUNCTION) {
                    codegen_writeln(ctx, "if (%s.type != HML_VAL_FUNCTION && %s.type != HML_VAL_BUILTIN_FN && %s.type != HML_VAL_FFI_FUNCTION) goto %s;",
                                  scrutinee, scrutinee, scrutinee, fail_label);
                }
            }
            // Bind if name is provided
            if (pattern->as.typed.name) {
                // Clear stale unboxable mark so codegen_expr_ident doesn't
                // treat this HmlValue binding as a native C primitive.
                if (ctx->type_ctx) {
                    type_check_clear_unboxable(ctx->type_ctx, pattern->as.typed.name);
                }
                codegen_add_local(ctx, pattern->as.typed.name);
                if (ctx->current_scope) {
                    scope_add_var(ctx->current_scope, pattern->as.typed.name);
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", pattern->as.typed.name, scrutinee);
                codegen_writeln(ctx, "hml_retain(&%s);", pattern->as.typed.name);
            }
            break;
        }

        case PATTERN_OR: {
            // Try each alternative - if any matches, continue; if all fail, go to fail_label
            char *success_label = codegen_label(ctx);

            for (int i = 0; i < pattern->as.or_pattern.num_alternatives; i++) {
                char *next_alt_label = (i + 1 < pattern->as.or_pattern.num_alternatives) ?
                                       codegen_label(ctx) : NULL;
                const char *alt_fail = next_alt_label ? next_alt_label : fail_label;

                codegen_writeln(ctx, "// OR pattern alternative %d", i);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);

                // Try this alternative
                codegen_pattern_match(ctx, pattern->as.or_pattern.alternatives[i], scrutinee, alt_fail);

                // If we get here, this alternative matched
                codegen_writeln(ctx, "goto %s;", success_label);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");

                if (next_alt_label) {
                    codegen_writeln(ctx, "%s:;", next_alt_label);
                    free(next_alt_label);
                }
            }

            codegen_writeln(ctx, "%s:;", success_label);
            free(success_label);
            break;
        }

        case PATTERN_OBJECT: {
            // Check that value is an object
            codegen_writeln(ctx, "if (%s.type != HML_VAL_OBJECT) goto %s;", scrutinee, fail_label);

            // Match each field
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                ObjectFieldPattern *field = &pattern->as.object.fields[i];
                char *field_val = codegen_temp(ctx);

                // Get field from object
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
                              field_val, scrutinee, field->name);

                if (field->pattern) {
                    // Explicit pattern for this field
                    codegen_pattern_match(ctx, field->pattern, field_val, fail_label);
                } else {
                    // Shorthand: bind field value to field name
                    // Clear stale unboxable mark so codegen_expr_ident doesn't
                    // treat this HmlValue binding as a native C primitive.
                    if (ctx->type_ctx) {
                        type_check_clear_unboxable(ctx->type_ctx, field->name);
                    }
                    codegen_add_local(ctx, field->name);
                    if (ctx->current_scope) {
                        scope_add_var(ctx->current_scope, field->name);
                    }
                    codegen_writeln(ctx, "HmlValue %s = %s;", field->name, field_val);
                    codegen_writeln(ctx, "hml_retain(&%s);", field->name);
                }

                free(field_val);
            }

            // Handle rest pattern if present
            if (pattern->as.object.has_rest && pattern->as.object.rest_name) {
                const char *rest_name = pattern->as.object.rest_name;
                // Clear stale unboxable mark so codegen_expr_ident doesn't
                // treat this HmlValue binding as a native C primitive.
                if (ctx->type_ctx) {
                    type_check_clear_unboxable(ctx->type_ctx, rest_name);
                }
                codegen_add_local(ctx, rest_name);
                if (ctx->current_scope) {
                    scope_add_var(ctx->current_scope, rest_name);
                }

                // Create a new object with unmatched fields
                codegen_writeln(ctx, "HmlValue %s = hml_val_object();", rest_name);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "int _count = hml_object_num_fields(%s);", scrutinee);
                codegen_writeln(ctx, "for (int _i = 0; _i < _count; _i++) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _key_val = hml_object_key_at(%s, _i);", scrutinee);
                codegen_writeln(ctx, "const char *_key = _key_val.as.as_string->data;");

                // Skip matched fields
                codegen_writeln(ctx, "int _matched = 0;");
                for (int i = 0; i < pattern->as.object.num_fields; i++) {
                    codegen_writeln(ctx, "if (strcmp(_key, \"%s\") == 0) _matched = 1;",
                                  pattern->as.object.fields[i].name);
                }
                codegen_writeln(ctx, "if (!_matched) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _v = hml_object_get_field(%s, _key);", scrutinee);
                codegen_writeln(ctx, "hml_retain(&_v);");
                codegen_writeln(ctx, "hml_object_set_field(%s, _key, _v);", rest_name);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
            break;
        }

        case PATTERN_ARRAY: {
            // Check that value is an array
            codegen_writeln(ctx, "if (%s.type != HML_VAL_ARRAY) goto %s;", scrutinee, fail_label);

            // Find rest pattern position
            int rest_index = -1;
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                if (pattern->as.array.elements[i].is_rest) {
                    rest_index = i;
                    break;
                }
            }

            int required_before = rest_index >= 0 ? rest_index : pattern->as.array.num_elements;
            int required_after = rest_index >= 0 ? (pattern->as.array.num_elements - rest_index - 1) : 0;
            int min_required = required_before + required_after;

            // Check array length
            if (rest_index < 0) {
                // Without rest pattern, require exact length
                codegen_writeln(ctx, "if (hml_array_length(%s).as.as_i32 != %d) goto %s;",
                              scrutinee, min_required, fail_label);
            } else {
                // With rest pattern, require at least min_required
                codegen_writeln(ctx, "if (hml_array_length(%s).as.as_i32 < %d) goto %s;",
                              scrutinee, min_required, fail_label);
            }

            // Match elements before rest
            for (int i = 0; i < required_before; i++) {
                char *elem_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(%d));",
                              elem_val, scrutinee, i);
                codegen_pattern_match(ctx, pattern->as.array.elements[i].pattern, elem_val, fail_label);
                free(elem_val);
            }

            // Handle rest pattern
            if (rest_index >= 0) {
                ArrayElementPattern *rest_pat = &pattern->as.array.elements[rest_index];
                const char *rest_name = rest_pat->rest_name;
                // Clear stale unboxable mark so codegen_expr_ident doesn't
                // treat this HmlValue binding as a native C primitive.
                if (ctx->type_ctx) {
                    type_check_clear_unboxable(ctx->type_ctx, rest_name);
                }
                codegen_add_local(ctx, rest_name);
                if (ctx->current_scope) {
                    scope_add_var(ctx->current_scope, rest_name);
                }

                // Create array with rest elements
                codegen_writeln(ctx, "HmlValue %s = hml_val_array();", rest_name);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "int _len = hml_array_length(%s).as.as_i32;", scrutinee);
                codegen_writeln(ctx, "int _rest_count = _len - %d;", min_required);
                codegen_writeln(ctx, "for (int _i = 0; _i < _rest_count; _i++) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _elem = hml_array_get(%s, hml_val_i32(%d + _i));",
                              scrutinee, required_before);
                codegen_writeln(ctx, "hml_retain(&_elem);");
                codegen_writeln(ctx, "hml_array_push(%s, _elem);", rest_name);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");

                // Match elements after rest
                for (int i = 0; i < required_after; i++) {
                    int pat_idx = rest_index + 1 + i;
                    char *elem_val = codegen_temp(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(hml_array_length(%s).as.as_i32 - %d));",
                                  elem_val, scrutinee, scrutinee, required_after - i);
                    codegen_pattern_match(ctx, pattern->as.array.elements[pat_idx].pattern, elem_val, fail_label);
                    free(elem_val);
                }
            }
            break;
        }
    }
}

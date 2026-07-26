/*
 * Hemlock Code Generator - Match Expression & Pattern Matching
 * Extracted from codegen_expr.c
 */

#include "codegen_internal.h"
#include "codegen_expr_internal.h"

// Forward declaration
char* codegen_expr(CodegenContext *ctx, Expr *expr);

// ========== PATTERN MATCHING CODE GENERATION ==========

// When non-zero, binding patterns ASSIGN to already-declared variables
// instead of declaring them. Used inside OR-pattern alternatives: each
// alternative is emitted in its own C block, so a declaration there would
// be block-scoped and unreachable from the arm body. The OR pattern
// pre-declares every binding (initialized to null) before the
// alternatives and switches this flag on. Codegen is single-threaded.
static int g_pattern_bind_assign = 0;

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

// Register `name` in the binding list if not already present (dedup keeps
// the pre-declarations unique when alternatives bind the same variable).
static void pattern_binding_add(const char ***names, int *count, int *cap, const char *name) {
    if (!name) return;
    for (int i = 0; i < *count; i++) {
        if (strcmp((*names)[i], name) == 0) return;
    }
    if (*count >= *cap) {
        int new_cap = *cap ? *cap * 2 : 8;
        const char **grown = realloc((void *)*names, (size_t)new_cap * sizeof(char *));
        if (!grown) return;  // fail open: binding just not hoisted
        *names = grown;
        *cap = new_cap;
    }
    (*names)[(*count)++] = name;
}

// Collect every variable name a pattern binds (recursively), so OR patterns
// can pre-declare them ahead of the per-alternative blocks.
static void collect_pattern_bindings(Pattern *p, const char ***names, int *count, int *cap) {
    if (!p) return;
    switch (p->type) {
        case PATTERN_BINDING:
            pattern_binding_add(names, count, cap, p->as.binding.name);
            break;
        case PATTERN_TYPED:
            pattern_binding_add(names, count, cap, p->as.typed.name);
            break;
        case PATTERN_OR:
            for (int i = 0; i < p->as.or_pattern.num_alternatives; i++) {
                collect_pattern_bindings(p->as.or_pattern.alternatives[i], names, count, cap);
            }
            break;
        case PATTERN_OBJECT:
            for (int i = 0; i < p->as.object.num_fields; i++) {
                ObjectFieldPattern *field = &p->as.object.fields[i];
                if (field->pattern) {
                    collect_pattern_bindings(field->pattern, names, count, cap);
                } else {
                    pattern_binding_add(names, count, cap, field->name);
                }
            }
            if (p->as.object.has_rest && p->as.object.rest_name) {
                pattern_binding_add(names, count, cap, p->as.object.rest_name);
            }
            break;
        case PATTERN_ARRAY:
            for (int i = 0; i < p->as.array.num_elements; i++) {
                if (p->as.array.elements[i].is_rest) {
                    pattern_binding_add(names, count, cap, p->as.array.elements[i].rest_name);
                } else {
                    collect_pattern_bindings(p->as.array.elements[i].pattern, names, count, cap);
                }
            }
            break;
        default:
            break;
    }
}

// Emit a binding: declare-and-init normally, or release-and-assign when the
// variable was pre-declared by an enclosing OR pattern. The release covers
// a partial binding left behind by an earlier failed alternative.
static void emit_pattern_binding(CodegenContext *ctx, const char *name, const char *value) {
    if (g_pattern_bind_assign) {
        codegen_writeln(ctx, "hml_release(&%s);", name);
        codegen_writeln(ctx, "%s = %s;", name, value);
        codegen_writeln(ctx, "hml_retain(&%s);", name);
    } else {
        codegen_writeln(ctx, "HmlValue %s = %s;", name, value);
        codegen_writeln(ctx, "hml_retain(&%s);", name);
    }
}

// Register a pattern binding with the scope/local machinery (skipped in
// assign mode - the enclosing OR pattern already registered it).
static void register_pattern_binding(CodegenContext *ctx, const char *name) {
    if (g_pattern_bind_assign) return;
    // Clear any stale unboxable mark left by a prior same-named variable
    // (e.g. a previous `for (let name = 0; ...)` counter), otherwise
    // codegen_expr_ident would treat this binding as a native C
    // primitive and emit hml_val_i32(x) on an HmlValue.
    if (ctx->type_ctx) {
        type_check_clear_unboxable(ctx->type_ctx, name);
    }
    codegen_add_local(ctx, name);
    if (ctx->current_scope) {
        scope_add_var(ctx->current_scope, name);
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
            register_pattern_binding(ctx, pattern->as.binding.name);
            emit_pattern_binding(ctx, pattern->as.binding.name, scrutinee);
            break;
        }

        case PATTERN_TYPED: {
            // Check type first
            Type *annot = pattern->as.typed.type_annotation;
            const char *type_enum = type_kind_to_hml_type_enum(annot->kind);
            if (annot->kind == TYPE_CUSTOM_OBJECT && annot->type_name) {
                // A define-typed pattern (`p: Person`) matches only objects
                // tagged with that type name at runtime - a bare HML_VAL_OBJECT
                // check would match ANY object (interpreter parity:
                // type_matches_value compares obj->type_name).
                char *escaped_type = codegen_escape_string(annot->type_name);
                codegen_writeln(ctx,
                    "if (!(%s.type == HML_VAL_OBJECT && %s.as.as_object && %s.as.as_object->type_name && strcmp(%s.as.as_object->type_name, \"%s\") == 0)) goto %s;",
                    scrutinee, scrutinee, scrutinee, scrutinee, escaped_type, fail_label);
                free(escaped_type);
            } else if (type_enum) {
                codegen_writeln(ctx, "if (%s.type != %s) goto %s;", scrutinee, type_enum, fail_label);
            } else {
                // For complex types like functions, check multiple possibilities
                if (annot->kind == TYPE_FUNCTION) {
                    codegen_writeln(ctx, "if (%s.type != HML_VAL_FUNCTION && %s.type != HML_VAL_BUILTIN_FN && %s.type != HML_VAL_FFI_FUNCTION) goto %s;",
                                  scrutinee, scrutinee, scrutinee, fail_label);
                }
            }
            // Bind if name is provided
            if (pattern->as.typed.name) {
                register_pattern_binding(ctx, pattern->as.typed.name);
                emit_pattern_binding(ctx, pattern->as.typed.name, scrutinee);
            }
            break;
        }

        case PATTERN_OR: {
            // Try each alternative - if any matches, continue; if all fail, go to fail_label
            char *success_label = codegen_label(ctx);

            // Pre-declare every variable the alternatives bind: each
            // alternative below is emitted inside its own C block, so a
            // declaration there would be scoped to that block and the arm
            // body (emitted after this pattern) couldn't see it. Inside the
            // alternatives, bindings switch to assign mode. Nested OR
            // patterns skip this (assign mode already set by the outer OR).
            int outer_assign = g_pattern_bind_assign;
            const char **bound_names = NULL;
            int num_bound = 0, bound_cap = 0;
            if (!outer_assign) {
                collect_pattern_bindings(pattern, &bound_names, &num_bound, &bound_cap);
                for (int i = 0; i < num_bound; i++) {
                    register_pattern_binding(ctx, bound_names[i]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", bound_names[i]);
                }
            }
            g_pattern_bind_assign = 1;

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

            g_pattern_bind_assign = outer_assign;
            free((void *)bound_names);

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
                char *escaped_name = codegen_escape_string(field->name);

                // Get field from object
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
                              field_val, scrutinee, escaped_name);
                free(escaped_name);

                if (field->pattern) {
                    // Explicit pattern for this field. Route the sub-pattern's
                    // failure through a local label that releases the owned
                    // field value first - a direct goto to fail_label leaked
                    // it on every failed match.
                    char *field_fail = codegen_label(ctx);
                    char *field_ok = codegen_label(ctx);
                    codegen_pattern_match(ctx, field->pattern, field_val, field_fail);
                    codegen_writeln(ctx, "hml_release(&%s);", field_val);
                    codegen_writeln(ctx, "goto %s;", field_ok);
                    codegen_writeln(ctx, "%s:;", field_fail);
                    codegen_writeln(ctx, "hml_release(&%s);", field_val);
                    codegen_writeln(ctx, "goto %s;", fail_label);
                    codegen_writeln(ctx, "%s:;", field_ok);
                    free(field_fail);
                    free(field_ok);
                } else {
                    // Shorthand: bind field value to field name
                    register_pattern_binding(ctx, field->name);
                    emit_pattern_binding(ctx, field->name, field_val);
                    // hml_object_get_field returns a retained (owned) value.
                    // The binding took its own retain, so drop ours.
                    codegen_writeln(ctx, "hml_release(&%s);", field_val);
                }
                free(field_val);
            }

            // Handle rest pattern if present
            if (pattern->as.object.has_rest && pattern->as.object.rest_name) {
                const char *rest_name = pattern->as.object.rest_name;
                register_pattern_binding(ctx, rest_name);

                // Create a new object with unmatched fields
                if (g_pattern_bind_assign) {
                    codegen_writeln(ctx, "hml_release(&%s);", rest_name);
                    codegen_writeln(ctx, "%s = hml_val_object();", rest_name);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_object();", rest_name);
                }
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
                    char *escaped_name = codegen_escape_string(pattern->as.object.fields[i].name);
                    codegen_writeln(ctx, "if (strcmp(_key, \"%s\") == 0) _matched = 1;",
                                  escaped_name);
                    free(escaped_name);
                }
                codegen_writeln(ctx, "if (!_matched) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _v = hml_object_get_field(%s, _key);", scrutinee);
                // hml_object_set_field retains the value (and strdup's the key),
                // so release our owned references afterwards to avoid leaking the
                // copied field value and key string on every match.
                codegen_writeln(ctx, "hml_object_set_field(%s, _key, _v);", rest_name);
                codegen_writeln(ctx, "hml_release(&_v);");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release(&_key_val);");
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

            // Match elements before rest. As with object fields, sub-pattern
            // failure must release the owned element value before unwinding
            // to fail_label (a direct goto leaked it per failed match).
            for (int i = 0; i < required_before; i++) {
                char *elem_val = codegen_temp(ctx);
                char *elem_fail = codegen_label(ctx);
                char *elem_ok = codegen_label(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(%d));",
                              elem_val, scrutinee, i);
                codegen_pattern_match(ctx, pattern->as.array.elements[i].pattern, elem_val, elem_fail);
                codegen_writeln(ctx, "hml_release(&%s);", elem_val);
                codegen_writeln(ctx, "goto %s;", elem_ok);
                codegen_writeln(ctx, "%s:;", elem_fail);
                codegen_writeln(ctx, "hml_release(&%s);", elem_val);
                codegen_writeln(ctx, "goto %s;", fail_label);
                codegen_writeln(ctx, "%s:;", elem_ok);
                free(elem_fail);
                free(elem_ok);
                free(elem_val);
            }

            // Handle rest pattern
            if (rest_index >= 0) {
                ArrayElementPattern *rest_pat = &pattern->as.array.elements[rest_index];
                const char *rest_name = rest_pat->rest_name;
                register_pattern_binding(ctx, rest_name);

                // Create array with rest elements
                if (g_pattern_bind_assign) {
                    codegen_writeln(ctx, "hml_release(&%s);", rest_name);
                    codegen_writeln(ctx, "%s = hml_val_array();", rest_name);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_array();", rest_name);
                }
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "int _len = hml_array_length(%s).as.as_i32;", scrutinee);
                codegen_writeln(ctx, "int _rest_count = _len - %d;", min_required);
                codegen_writeln(ctx, "for (int _i = 0; _i < _rest_count; _i++) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _elem = hml_array_get(%s, hml_val_i32(%d + _i));",
                              scrutinee, required_before);
                // Both hml_array_get and hml_array_push retain; release our owned
                // reference from the get so rest elements aren't leaked.
                codegen_writeln(ctx, "hml_array_push(%s, _elem);", rest_name);
                codegen_writeln(ctx, "hml_release(&_elem);");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");

                // Match elements after rest
                for (int i = 0; i < required_after; i++) {
                    int pat_idx = rest_index + 1 + i;
                    char *elem_val = codegen_temp(ctx);
                    char *elem_fail = codegen_label(ctx);
                    char *elem_ok = codegen_label(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(hml_array_length(%s).as.as_i32 - %d));",
                                  elem_val, scrutinee, scrutinee, required_after - i);
                    codegen_pattern_match(ctx, pattern->as.array.elements[pat_idx].pattern, elem_val, elem_fail);
                    codegen_writeln(ctx, "hml_release(&%s);", elem_val);
                    codegen_writeln(ctx, "goto %s;", elem_ok);
                    codegen_writeln(ctx, "%s:;", elem_fail);
                    codegen_writeln(ctx, "hml_release(&%s);", elem_val);
                    codegen_writeln(ctx, "goto %s;", fail_label);
                    codegen_writeln(ctx, "%s:;", elem_ok);
                    free(elem_fail);
                    free(elem_ok);
                    free(elem_val);
                }
            }
            break;
        }
    }
}

/*
 * Hemlock Compiler - Pattern Matching Code Generation
 */

#include "codegen.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations
static void generate_pattern_match_internal(CodegenContext *ctx, Pattern *pattern,
                                            const char *value_var, const char *matched_var);
static void generate_pattern_bindings_internal(CodegenContext *ctx, Pattern *pattern,
                                               const char *value_var);
static void generate_pattern_release_internal(CodegenContext *ctx, Pattern *pattern);

// Generate C type name for a Hemlock type
static const char* type_to_hml_type(TypeKind kind) {
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
        default: return "HML_VAL_NULL";
    }
}

// Generate pattern matching check code
// Sets matched_var to 1 if pattern matches, 0 otherwise
void codegen_pattern_match(CodegenContext *ctx, Pattern *pattern,
                           const char *value_var, const char *matched_var) {
    generate_pattern_match_internal(ctx, pattern, value_var, matched_var);
}

static void generate_pattern_match_internal(CodegenContext *ctx, Pattern *pattern,
                                            const char *value_var, const char *matched_var) {
    if (!pattern) {
        codegen_writeln(ctx, "%s = 0;", matched_var);
        return;
    }

    switch (pattern->type) {
        case PATTERN_WILDCARD:
            // Wildcard always matches
            codegen_writeln(ctx, "%s = 1;", matched_var);
            break;

        case PATTERN_LITERAL: {
            // Generate literal expression and compare
            char *lit_val = codegen_expr(ctx, pattern->as.literal);
            codegen_writeln(ctx, "%s = hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s));",
                          matched_var, value_var, lit_val);
            codegen_writeln(ctx, "hml_release(&%s);", lit_val);
            free(lit_val);
            break;
        }

        case PATTERN_BINDING:
            // Binding always matches (we'll bind the value later)
            codegen_writeln(ctx, "%s = 1;", matched_var);
            break;

        case PATTERN_ARRAY: {
            int num_elements = pattern->as.array.num_elements;
            char *rest_name = pattern->as.array.rest_name;

            // Check if value is an array
            codegen_writeln(ctx, "if (%s.type != HML_VAL_ARRAY) {", value_var);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = 0;", matched_var);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            // Check array length
            if (rest_name) {
                codegen_writeln(ctx, "if (%s.as.as_array->length < %d) {", value_var, num_elements);
            } else {
                codegen_writeln(ctx, "if (%s.as.as_array->length != %d) {", value_var, num_elements);
            }
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = 0;", matched_var);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            // Initially assume match
            codegen_writeln(ctx, "%s = 1;", matched_var);

            // Check each element
            for (int i = 0; i < num_elements; i++) {
                char *elem_var = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(%d));",
                              elem_var, value_var, i);

                char *elem_matched = codegen_temp(ctx);
                codegen_writeln(ctx, "int %s = 0;", elem_matched);
                generate_pattern_match_internal(ctx, pattern->as.array.elements[i],
                                               elem_var, elem_matched);

                codegen_writeln(ctx, "hml_release(&%s);", elem_var);
                codegen_writeln(ctx, "if (!%s) %s = 0;", elem_matched, matched_var);
                free(elem_var);
                free(elem_matched);
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case PATTERN_OBJECT: {
            int num_fields = pattern->as.object.num_fields;

            // Check if value is an object
            codegen_writeln(ctx, "if (%s.type != HML_VAL_OBJECT) {", value_var);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = 0;", matched_var);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            // Initially assume match
            codegen_writeln(ctx, "%s = 1;", matched_var);

            // Check each field
            for (int i = 0; i < num_fields; i++) {
                char *field_name = pattern->as.object.field_names[i];
                char *field_var = codegen_temp(ctx);
                char *has_field = codegen_temp(ctx);

                codegen_writeln(ctx, "int %s = hml_object_has_field(%s, \"%s\");",
                              has_field, value_var, field_name);
                codegen_writeln(ctx, "if (!%s) {", has_field);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = 0;", matched_var);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);

                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
                              field_var, value_var, field_name);

                if (pattern->as.object.field_patterns[i]) {
                    char *field_matched = codegen_temp(ctx);
                    codegen_writeln(ctx, "int %s = 0;", field_matched);
                    generate_pattern_match_internal(ctx, pattern->as.object.field_patterns[i],
                                                   field_var, field_matched);
                    codegen_writeln(ctx, "if (!%s) %s = 0;", field_matched, matched_var);
                    free(field_matched);
                }

                codegen_writeln(ctx, "hml_release(&%s);", field_var);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                free(field_var);
                free(has_field);
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case PATTERN_RANGE: {
            // Generate range check
            char *start_val = codegen_expr(ctx, pattern->as.range.start);
            char *end_val = codegen_expr(ctx, pattern->as.range.end);

            codegen_writeln(ctx, "%s = (hml_to_bool(hml_binary_op(HML_OP_GREATER_EQUAL, %s, %s)) && "
                          "hml_to_bool(hml_binary_op(HML_OP_LESS_EQUAL, %s, %s)));",
                          matched_var, value_var, start_val, value_var, end_val);

            codegen_writeln(ctx, "hml_release(&%s);", start_val);
            codegen_writeln(ctx, "hml_release(&%s);", end_val);
            free(start_val);
            free(end_val);
            break;
        }

        case PATTERN_TYPE: {
            // Check if value has the specified type
            const char *type_check = type_to_hml_type(pattern->as.type_pattern.match_type->kind);

            if (pattern->as.type_pattern.match_type->kind == TYPE_CUSTOM_OBJECT) {
                // Check object type name
                codegen_writeln(ctx, "%s = (%s.type == HML_VAL_OBJECT && "
                              "%s.as.as_object->type_name && "
                              "strcmp(%s.as.as_object->type_name, \"%s\") == 0);",
                              matched_var, value_var, value_var, value_var,
                              pattern->as.type_pattern.match_type->type_name);
            } else {
                codegen_writeln(ctx, "%s = (%s.type == %s);",
                              matched_var, value_var, type_check);
            }
            break;
        }

        case PATTERN_OR: {
            // Try each pattern until one matches
            codegen_writeln(ctx, "%s = 0;", matched_var);
            for (int i = 0; i < pattern->as.or_pattern.num_patterns; i++) {
                char *or_matched = codegen_temp(ctx);
                codegen_writeln(ctx, "int %s = 0;", or_matched);
                generate_pattern_match_internal(ctx, pattern->as.or_pattern.patterns[i],
                                               value_var, or_matched);
                codegen_writeln(ctx, "if (%s) %s = 1;", or_matched, matched_var);
                free(or_matched);
            }
            break;
        }
    }
}

// Generate variable bindings for a matched pattern
void codegen_pattern_bindings(CodegenContext *ctx, Pattern *pattern, const char *value_var) {
    generate_pattern_bindings_internal(ctx, pattern, value_var);
}

static void generate_pattern_bindings_internal(CodegenContext *ctx, Pattern *pattern,
                                               const char *value_var) {
    if (!pattern) return;

    switch (pattern->type) {
        case PATTERN_WILDCARD:
        case PATTERN_LITERAL:
        case PATTERN_RANGE:
        case PATTERN_TYPE:
            // No bindings
            break;

        case PATTERN_BINDING: {
            // Bind value to variable
            char *safe_name = codegen_sanitize_ident(pattern->as.binding.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, value_var);
            codegen_writeln(ctx, "hml_retain(&%s);", safe_name);
            codegen_add_local(ctx, pattern->as.binding.name);
            free(safe_name);
            break;
        }

        case PATTERN_ARRAY: {
            // Bind each element
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                char *elem_var = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, hml_val_i32(%d));",
                              elem_var, value_var, i);
                generate_pattern_bindings_internal(ctx, pattern->as.array.elements[i], elem_var);
                // Don't release - the binding owns it now or it was a non-binding pattern
                if (pattern->as.array.elements[i]->type != PATTERN_BINDING) {
                    codegen_writeln(ctx, "hml_release(&%s);", elem_var);
                }
                free(elem_var);
            }

            // Bind rest if present
            if (pattern->as.array.rest_name) {
                char *safe_name = codegen_sanitize_ident(pattern->as.array.rest_name);
                codegen_writeln(ctx, "HmlValue %s = hml_val_array();", safe_name);
                codegen_writeln(ctx, "for (int _i = %d; _i < %s.as.as_array->length; _i++) {",
                              pattern->as.array.num_elements, value_var);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "HmlValue _elem = hml_array_get(%s, hml_val_i32(_i));", value_var);
                codegen_writeln(ctx, "hml_array_push(%s, _elem);", safe_name);
                codegen_writeln(ctx, "hml_release(&_elem);");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_add_local(ctx, pattern->as.array.rest_name);
                free(safe_name);
            }
            break;
        }

        case PATTERN_OBJECT: {
            // Bind each field
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                char *field_name = pattern->as.object.field_names[i];
                char *field_var = codegen_temp(ctx);

                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
                              field_var, value_var, field_name);
                generate_pattern_bindings_internal(ctx, pattern->as.object.field_patterns[i],
                                                  field_var);
                // Don't release - the binding owns it now or it was a non-binding pattern
                if (pattern->as.object.field_patterns[i]->type != PATTERN_BINDING) {
                    codegen_writeln(ctx, "hml_release(&%s);", field_var);
                }
                free(field_var);
            }

            // Note: rest bindings for objects are more complex and skipped for now
            break;
        }

        case PATTERN_OR:
            // For OR patterns, we only bind from the first pattern
            // (all patterns in an OR should bind the same variables)
            if (pattern->as.or_pattern.num_patterns > 0) {
                generate_pattern_bindings_internal(ctx, pattern->as.or_pattern.patterns[0],
                                                  value_var);
            }
            break;
    }
}

// Release pattern-bound variables
void codegen_pattern_release_bindings(CodegenContext *ctx, Pattern *pattern) {
    generate_pattern_release_internal(ctx, pattern);
}

static void generate_pattern_release_internal(CodegenContext *ctx, Pattern *pattern) {
    if (!pattern) return;

    switch (pattern->type) {
        case PATTERN_WILDCARD:
        case PATTERN_LITERAL:
        case PATTERN_RANGE:
        case PATTERN_TYPE:
            // No bindings to release
            break;

        case PATTERN_BINDING: {
            char *safe_name = codegen_sanitize_ident(pattern->as.binding.name);
            codegen_writeln(ctx, "hml_release(&%s);", safe_name);
            free(safe_name);
            break;
        }

        case PATTERN_ARRAY: {
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                generate_pattern_release_internal(ctx, pattern->as.array.elements[i]);
            }
            if (pattern->as.array.rest_name) {
                char *safe_name = codegen_sanitize_ident(pattern->as.array.rest_name);
                codegen_writeln(ctx, "hml_release(&%s);", safe_name);
                free(safe_name);
            }
            break;
        }

        case PATTERN_OBJECT: {
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                generate_pattern_release_internal(ctx, pattern->as.object.field_patterns[i]);
            }
            if (pattern->as.object.rest_name) {
                char *safe_name = codegen_sanitize_ident(pattern->as.object.rest_name);
                codegen_writeln(ctx, "hml_release(&%s);", safe_name);
                free(safe_name);
            }
            break;
        }

        case PATTERN_OR:
            if (pattern->as.or_pattern.num_patterns > 0) {
                generate_pattern_release_internal(ctx, pattern->as.or_pattern.patterns[0]);
            }
            break;
    }
}

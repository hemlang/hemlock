#include "annotations.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>

// Known annotations with their specifications
static const AnnotationSpec known_annotations[] = {
    // Safety (for Tricycle)
    {"safe",       ANNOT_TARGET_FN, 0, 0, 0},
    {"unsafe",     ANNOT_TARGET_FN, 0, 0, 0},
    {"trusted",    ANNOT_TARGET_FN, 0, 0, 0},

    // Compiler hints
    {"inline",     ANNOT_TARGET_FN, 0, 0, 0},
    {"noinline",   ANNOT_TARGET_FN, 0, 0, 0},
    {"cold",       ANNOT_TARGET_FN, 0, 0, 0},
    {"hot",        ANNOT_TARGET_FN, 0, 0, 0},
    {"pure",       ANNOT_TARGET_FN, 0, 0, 0},
    {"const",      ANNOT_TARGET_FN, 0, 0, 0},
    {"flatten",    ANNOT_TARGET_FN, 0, 0, 0},
    {"optimize",   ANNOT_TARGET_FN, 1, 1, 1},  // Requires 1 argument (level)
    {"warn_unused", ANNOT_TARGET_FN, 0, 0, 0},  // Warn if return value is ignored

    // Memory/FFI annotations
    {"aligned",    ANNOT_TARGET_LET, 1, 1, 1},     // Requires 1 argument (alignment)
    {"packed",     ANNOT_TARGET_DEFINE, 0, 0, 0},  // No struct padding
    {"section",    ANNOT_TARGET_FN | ANNOT_TARGET_LET, 1, 1, 1},  // Custom ELF section

    // Deprecation
    {"deprecated", ANNOT_TARGET_ALL, 1, 0, 2},

    // Testing
    {"test",       ANNOT_TARGET_FN, 1, 0, 1},
    {"skip",       ANNOT_TARGET_FN, 1, 0, 1},
    {"timeout",    ANNOT_TARGET_FN, 1, 1, 1},

    // Documentation
    {"author",     ANNOT_TARGET_ALL, 1, 1, 1},
    {"since",      ANNOT_TARGET_ALL, 1, 1, 1},
    {"see",        ANNOT_TARGET_ALL, 1, 1, 1},

    {NULL, 0, 0, 0, 0}  // Sentinel
};

const AnnotationSpec *annotation_get_spec(const char *name) {
    for (int i = 0; known_annotations[i].name != NULL; i++) {
        if (strcmp(known_annotations[i].name, name) == 0) {
            return &known_annotations[i];
        }
    }
    return NULL;
}

static const char *target_name(AnnotationTarget target) {
    switch (target) {
        case ANNOT_TARGET_FN: return "function";
        case ANNOT_TARGET_DEFINE: return "type definition";
        case ANNOT_TARGET_ENUM: return "enum";
        case ANNOT_TARGET_LET: return "variable";
        case ANNOT_TARGET_CONST: return "constant";
        default: return "declaration";
    }
}

static AnnotationTarget stmt_to_target(Stmt *stmt) {
    switch (stmt->type) {
        case STMT_LET:
            // Check if it's a function (let name = fn(...) { })
            if (stmt->as.let.value && stmt->as.let.value->type == EXPR_FUNCTION) {
                return ANNOT_TARGET_FN;
            }
            return ANNOT_TARGET_LET;
        case STMT_CONST:
            return ANNOT_TARGET_CONST;
        case STMT_DEFINE_OBJECT:
            return ANNOT_TARGET_DEFINE;
        case STMT_ENUM:
            return ANNOT_TARGET_ENUM;
        default:
            return 0;
    }
}

static Annotation **get_annotations_from_stmt(Stmt *stmt, int *count) {
    *count = 0;
    switch (stmt->type) {
        case STMT_LET:
            *count = stmt->as.let.annotation_count;
            return stmt->as.let.annotations;
        case STMT_CONST:
            *count = stmt->as.const_stmt.annotation_count;
            return stmt->as.const_stmt.annotations;
        case STMT_DEFINE_OBJECT:
            *count = stmt->as.define_object.annotation_count;
            return stmt->as.define_object.annotations;
        case STMT_ENUM:
            *count = stmt->as.enum_decl.annotation_count;
            return stmt->as.enum_decl.annotations;
        default:
            return NULL;
    }
}

int validate_annotations(Stmt *stmt) {
    int annotation_count = 0;
    Annotation **annotations = get_annotations_from_stmt(stmt, &annotation_count);

    if (!annotations || annotation_count == 0) {
        return 1;  // No annotations to validate
    }

    AnnotationTarget stmt_target = stmt_to_target(stmt);
    int has_error = 0;

    for (int i = 0; i < annotation_count; i++) {
        Annotation *a = annotations[i];
        const AnnotationSpec *spec = annotation_get_spec(a->name);

        // Check if annotation is known
        if (spec == NULL) {
            fprintf(stderr, "[line %d:%d] Warning: unknown annotation '@%s'\n",
                    a->line, a->column, a->name);
            continue;
        }

        // Check if annotation is valid on this target
        if (!(spec->valid_targets & stmt_target)) {
            fprintf(stderr, "[line %d:%d] Error: @%s is not valid on %s declarations\n",
                    a->line, a->column, a->name, target_name(stmt_target));
            has_error = 1;
        }

        // Check argument count
        if (!spec->allows_args && a->arg_count > 0) {
            fprintf(stderr, "[line %d:%d] Error: @%s does not accept arguments\n",
                    a->line, a->column, a->name);
            has_error = 1;
        } else if (spec->allows_args) {
            if (a->arg_count < spec->min_args) {
                fprintf(stderr, "[line %d:%d] Error: @%s expects at least %d argument(s), got %d\n",
                        a->line, a->column, a->name, spec->min_args, a->arg_count);
                has_error = 1;
            }
            if (spec->max_args != -1 && a->arg_count > spec->max_args) {
                fprintf(stderr, "[line %d:%d] Error: @%s expects at most %d argument(s), got %d\n",
                        a->line, a->column, a->name, spec->max_args, a->arg_count);
                has_error = 1;
            }
        }

        // Check for duplicates
        for (int j = 0; j < i; j++) {
            if (strcmp(annotations[j]->name, a->name) == 0) {
                fprintf(stderr, "[line %d:%d] Warning: duplicate annotation '@%s'\n",
                        a->line, a->column, a->name);
            }
        }
    }

    return !has_error;
}

int is_deprecated(Annotation **annotations, int count) {
    return annotation_has(annotations, count, "deprecated");
}

const char *get_deprecation_message(Annotation **annotations, int count) {
    Annotation *deprecated = annotation_get(annotations, count, "deprecated");
    if (!deprecated) {
        return NULL;
    }

    // Try to get the first argument (positional or named "message")
    const char *msg = annotation_get_string_arg(deprecated, NULL, NULL);
    if (msg) {
        return msg;
    }

    return "this declaration is deprecated";
}

int has_inline_annotation(Annotation **annotations, int count) {
    return annotation_has(annotations, count, "inline");
}

int has_noinline_annotation(Annotation **annotations, int count) {
    return annotation_has(annotations, count, "noinline");
}

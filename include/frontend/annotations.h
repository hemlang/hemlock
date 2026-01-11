#ifndef HEMLOCK_ANNOTATIONS_H
#define HEMLOCK_ANNOTATIONS_H

#include "frontend/ast.h"

// Declaration types that can have annotations
typedef enum {
    ANNOT_TARGET_FN       = 1 << 0,  // Functions
    ANNOT_TARGET_DEFINE   = 1 << 1,  // Type definitions
    ANNOT_TARGET_ENUM     = 1 << 2,  // Enums
    ANNOT_TARGET_LET      = 1 << 3,  // Variables (let)
    ANNOT_TARGET_CONST    = 1 << 4,  // Constants
    ANNOT_TARGET_ALL      = 0xFF,    // All declarations
} AnnotationTarget;

// Annotation specification
typedef struct {
    const char *name;
    int valid_targets;      // Bitmask of AnnotationTarget
    int allows_args;        // 1 if arguments are allowed
    int min_args;           // Minimum number of arguments
    int max_args;           // Maximum number of arguments (-1 for unlimited)
} AnnotationSpec;

// Get the spec for a known annotation (NULL if unknown)
const AnnotationSpec *annotation_get_spec(const char *name);

// Validate annotations on a statement
// Returns 1 if valid, 0 if errors were found
// Outputs warnings/errors to stderr
int validate_annotations(Stmt *stmt);

// Check if a declaration is deprecated (for warning on use)
int is_deprecated(Annotation **annotations, int count);

// Get deprecation message if present
const char *get_deprecation_message(Annotation **annotations, int count);

// Check for inline/noinline hints
int has_inline_annotation(Annotation **annotations, int count);
int has_noinline_annotation(Annotation **annotations, int count);

#endif // HEMLOCK_ANNOTATIONS_H

#ifndef HEMLOCK_NAME_RESOLVE_H
#define HEMLOCK_NAME_RESOLVE_H

/*
 * Hemlock name resolver
 *
 * Walks a whole program - the entry file plus every module it imports,
 * transitively - and reports identifiers that no binding in scope can
 * explain: builtins, module-level declarations, imports (named, renamed,
 * `* as ns`, `import *`), function parameters, closure captures, loop and
 * pattern bindings, catch parameters.
 *
 * Both backends and `hemlock check` run this pass. For the compiler it is a
 * hard gate: an identifier that reaches code generation unresolved becomes a
 * C symbol nobody defined, and the failure surfaces as a GCC error about
 * generated code instead of a Hemlock error about the user's source.
 *
 * The pass over-approximates the set of defined names on purpose: when it
 * cannot see all the bindings a scope might hold (an unreadable module, an
 * `import *` from a module that failed to load) it stays quiet for that scope
 * rather than guessing. Every name it does report is one that would fail at
 * runtime.
 */

#include "frontend/ast.h"

/*
 * One diagnostic, in source order. Usually an unresolved name; a module the
 * pass had to read for itself can also fail to parse, and that is reported
 * here too rather than printed, so `hemlock check` keeps every diagnostic on
 * stdout where tools can consume it.
 */
typedef struct NameDiag {
    char *file;              /* file the diagnostic belongs to (owned) */
    int line;                /* 1-based */
    int column;              /* 1-based, 0 when unknown */
    char *message;           /* rendered message (owned) */
    char *name;              /* the unresolved identifier, else NULL (owned) */
    char *hint;              /* suggestion, else NULL (owned) */
    struct NameDiag *next;
} NameDiag;

typedef struct NameResolveResult {
    NameDiag *diags;         /* linked list, source order per module */
    NameDiag *tail;
    int error_count;         /* number of diags */
    int load_error_count;    /* modules that could not be read or parsed */
    int entry_parse_failed;  /* the entry file itself would not parse: the
                              * caller parses it too and reports that better,
                              * so its diagnostics here are suppressed */
} NameResolveResult;

/*
 * Resolve `entry_path` and every module it imports.
 *
 * `entry_stmts`/`entry_count` pass in an AST already parsed for `entry_path`
 * (borrowed, never freed here); pass NULL/0 to have the resolver read and
 * parse the file itself. Returns NULL only if the entry file itself cannot be
 * read or parsed with entry_stmts == NULL.
 */
NameResolveResult *name_resolve_entry(const char *entry_path,
                                      Stmt **entry_stmts, int entry_count);

void name_resolve_result_free(NameResolveResult *result);

/*
 * Is `name` a global the runtime provides - a builtin function, a type
 * constant, a TYPEID_*, one of the __ prefixed names the stdlib uses? Reads
 * include/builtin_globals.h, the same list the interpreter registers from, so
 * a caller cannot disagree with the runtime about what exists.
 */
int name_resolve_is_builtin(const char *name);

/* "file:line: error: <message>". Caller frees. */
char *name_diag_format(const NameDiag *diag);

#endif /* HEMLOCK_NAME_RESOLVE_H */

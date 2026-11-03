/*
 * Hemlock name resolver - see include/frontend/name_resolve.h.
 *
 * Three phases over the module graph:
 *
 *   1. load    - parse the entry file and, transitively, every module it
 *                imports or re-exports from.
 *   2. exports - work out each module's export names, following re-export
 *                chains, so `import { x }` and `import *` can be given scopes.
 *   3. walk    - walk every module's AST with a scope chain and report each
 *                identifier that no scope explains.
 *
 * Declarations are hoisted within their scope: `fn a() { return b(); }` ahead
 * of `fn b()` is legal Hemlock and mutual recursion between top-level
 * functions has to keep working, so a scope is seeded with every name its
 * statements declare before any of them is walked. Use-before-assignment is a
 * separate question this pass deliberately does not answer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "frontend/name_resolve.h"
#include "frontend/parser.h"
#include "builtin_globals.h"
#include "modules.h"
#include "shared/file_io.h"

#ifndef _WIN32
#include <dirent.h>
#endif
#include <unistd.h>

#define NR_LIST_INIT_CAP      8
#define NR_MAX_HINT_DISTANCE  2   /* edit distance for "did you mean 'x'?" */
#define NR_MIN_HINT_LENGTH    3   /* shorter names typo into each other too easily */
#define NR_LOOSE_HINT_LENGTH  5   /* below this, only a one-edit difference counts:
                                   * at distance 2, "nope" reaches "open" */

/* ========================= name lists ========================= */

/* Borrowed-pointer string list. Every name stored here is owned by an AST
 * node or by an NModule's export array, both of which outlive the pass. */
typedef struct {
    const char **items;
    int count;
    int cap;
} NrNames;

static void nr_names_init(NrNames *l) {
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static void nr_names_free(NrNames *l) {
    free(l->items);
    nr_names_init(l);
}

static void nr_names_add(NrNames *l, const char *name) {
    if (!name) return;
    if (l->count >= l->cap) {
        int cap = l->cap ? l->cap * 2 : NR_LIST_INIT_CAP;
        const char **items = realloc(l->items, sizeof(const char *) * (size_t)cap);
        if (!items) return;   /* OOM: the name is simply not recorded */
        l->items = items;
        l->cap = cap;
    }
    l->items[l->count++] = name;
}

static int nr_names_has(const NrNames *l, const char *name) {
    for (int i = 0; i < l->count; i++) {
        if (strcmp(l->items[i], name) == 0) return 1;
    }
    return 0;
}

/* ========================= builtins ========================= */

#define NR_BUILTIN_NAME_FN(name, impl) name,
#define NR_BUILTIN_NAME_1(name) name,

static const char *nr_builtin_names[] = {
    HML_BUILTIN_FNS(NR_BUILTIN_NAME_FN)
    HML_BUILTIN_TYPE_CONSTS(NR_BUILTIN_NAME_FN)
    HML_BUILTIN_I32_CONSTS(NR_BUILTIN_NAME_FN)
    HML_BUILTIN_F64_CONSTS(NR_BUILTIN_NAME_FN)
    HML_BUILTIN_DYNAMIC_GLOBALS(NR_BUILTIN_NAME_1)
    HML_COMPILER_ONLY_BUILTINS(NR_BUILTIN_NAME_1)
    NULL
};

#undef NR_BUILTIN_NAME_FN
#undef NR_BUILTIN_NAME_1

int name_resolve_is_builtin(const char *name) {
    if (!name) return 0;
    for (int i = 0; nr_builtin_names[i]; i++) {
        if (strcmp(nr_builtin_names[i], name) == 0) return 1;
    }
    return 0;
}

/*
 * Names the language binds implicitly rather than through a declaration.
 * `self` is bound on every method call (object-literal method shorthand and
 * `define` method defaults), so it is in scope wherever a method body is.
 */
static int nr_is_implicit_name(const char *name) {
    return strcmp(name, "self") == 0;
}

/* ========================= scopes ========================= */

typedef struct NrScope {
    NrNames names;
    int open;                 /* 1: may hold bindings we cannot see */
    struct NrScope *parent;
} NrScope;

/* ========================= paths ========================= */

/* The path a module is known by: canonical where possible, as given if not
 * (realpath fails for a file that does not exist, and the caller's spelling
 * is the more useful thing to print in that case). */
static char *nr_canonical_path(const char *path) {
    char *real = realpath(path, NULL);
    return real ? real : strdup(path);
}

/*
 * How a module's path is printed. An absolute path is right for a module
 * outside the tree being built (the stdlib), but for one inside it the
 * relative path is what the user typed and what an editor can jump to, so
 * trim the working directory off when it is a prefix.
 */
static char *nr_display_path(const char *path) {
    if (path[0] != '/') return strdup(path);

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return strdup(path);

    char *cwd_real = realpath(cwd, NULL);
    const char *base = cwd_real ? cwd_real : cwd;
    size_t base_len = strlen(base);

    char *out = NULL;
    if (base_len > 0 && strncmp(path, base, base_len) == 0 && path[base_len] == '/') {
        out = strdup(path + base_len + 1);
    }
    free(cwd_real);
    return out ? out : strdup(path);
}

/* ========================= modules ========================= */

typedef struct NrModule NrModule;

/* An import or re-export edge, keeping the path as written for diagnostics. */
typedef struct {
    char *spec;               /* "@stdlib/time", "./lib.hml" (owned) */
    NrModule *target;         /* NULL when the path could not be resolved */
} NrEdge;

struct NrModule {
    char *path;               /* canonical path: the module-graph cache key */
    char *display_path;       /* what diagnostics print */
    Stmt **stmts;
    int num_stmts;
    int owns_stmts;           /* 0 for an AST handed in by the caller */

    NrEdge *edges;
    int num_edges;
    int edge_cap;

    char **exports;           /* owned strings */
    int num_exports;
    int export_cap;

    int load_failed;          /* unreadable or unparsable */
    int exports_state;        /* 0 pending, 1 in progress, 2 done */
    int walked;

    NrModule *next;
};

typedef struct {
    ModuleResolution *resolver;
    NrModule *modules;
    NameResolveResult *result;

    /* current walk position */
    const char *file;
    NrScope *scope;

    /* lazily built: every stdlib module's exports, for import hints */
    char **stdlib_names;      /* owned export name */
    char **stdlib_specs;      /* parallel: the "@stdlib/time" that exports it */
    int num_stdlib;
    int stdlib_cap;
    int stdlib_scanned;
} NrCtx;

/* ---------- module list ---------- */

static NrModule *nr_module_find(NrCtx *ctx, const char *path) {
    for (NrModule *m = ctx->modules; m; m = m->next) {
        if (strcmp(m->path, path) == 0) return m;
    }
    return NULL;
}

static NrModule *nr_module_new(NrCtx *ctx, const char *path) {
    NrModule *m = calloc(1, sizeof(NrModule));
    if (!m) return NULL;
    m->path = strdup(path);
    m->display_path = nr_display_path(path);
    if (!m->path || !m->display_path) {
        free(m->path);
        free(m->display_path);
        free(m);
        return NULL;
    }
    m->next = ctx->modules;
    ctx->modules = m;
    return m;
}

static void nr_module_add_edge(NrModule *m, const char *spec, NrModule *target) {
    if (m->num_edges >= m->edge_cap) {
        int cap = m->edge_cap ? m->edge_cap * 2 : NR_LIST_INIT_CAP;
        NrEdge *edges = realloc(m->edges, sizeof(NrEdge) * (size_t)cap);
        if (!edges) return;
        m->edges = edges;
        m->edge_cap = cap;
    }
    m->edges[m->num_edges].spec = spec ? strdup(spec) : NULL;
    m->edges[m->num_edges].target = target;
    m->num_edges++;
}

static NrModule *nr_module_edge_target(NrModule *m, const char *spec) {
    if (!spec) return NULL;
    for (int i = 0; i < m->num_edges; i++) {
        if (m->edges[i].spec && strcmp(m->edges[i].spec, spec) == 0) {
            return m->edges[i].target;
        }
    }
    return NULL;
}

static void nr_module_add_export(NrModule *m, const char *name) {
    if (!name) return;
    for (int i = 0; i < m->num_exports; i++) {
        if (strcmp(m->exports[i], name) == 0) return;
    }
    if (m->num_exports >= m->export_cap) {
        int cap = m->export_cap ? m->export_cap * 2 : NR_LIST_INIT_CAP;
        char **exports = realloc(m->exports, sizeof(char *) * (size_t)cap);
        if (!exports) return;
        m->exports = exports;
        m->export_cap = cap;
    }
    char *copy = strdup(name);
    if (!copy) return;
    m->exports[m->num_exports++] = copy;
}

/* ---------- diagnostics ---------- */

/* One-argument message builder: "cannot read imported module '%s'". */
static char *nr_format_hint(const char *fmt, const char *arg) {
    size_t len = strlen(fmt) + strlen(arg) + 1;
    char *out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, fmt, arg);
    return out;
}

/*
 * Append a diagnostic. Takes ownership of `message`, `name` and `hint`.
 *
 * Identical diagnostics at the same position are dropped: desugaring makes
 * one name appear several times at one spot (`x += 1` becomes `x = x + 1`,
 * so an undefined `x` is both the assignment target and an operand), and
 * reporting the same line four times tells the reader nothing new.
 */
static void nr_report(NrCtx *ctx, const char *file, int line, int column,
                      char *message, char *name, char *hint) {
    for (NameDiag *prev = ctx->result->diags; prev; prev = prev->next) {
        if (prev->line == line && prev->column == column &&
            strcmp(prev->file, file ? file : "<unknown>") == 0 &&
            strcmp(prev->message, message) == 0) {
            free(message);
            free(name);
            free(hint);
            return;
        }
    }

    NameDiag *d = calloc(1, sizeof(NameDiag));
    if (!d) {
        free(message);
        free(name);
        free(hint);
        return;
    }
    d->file = strdup(file ? file : "<unknown>");
    d->line = line;
    d->column = column;
    d->message = message;
    d->name = name;
    d->hint = hint;

    if (ctx->result->tail) {
        ctx->result->tail->next = d;
    } else {
        ctx->result->diags = d;
    }
    ctx->result->tail = d;
    ctx->result->error_count++;
}

/* ========================= phase 1: load ========================= */

/*
 * Read and parse a module the pass discovered for itself.
 *
 * Parsed here rather than through parse_file_to_ast() because that prints to
 * stderr, and `hemlock check` has to keep every diagnostic on stdout. Syntax
 * errors in an imported module are collected and reported like any other
 * diagnostic: before this pass existed, `hemlock check entry.hml` said nothing
 * at all about a module that would not parse.
 *
 * Returns 1 on a usable AST.
 */
static int nr_parse_module(NrCtx *ctx, NrModule *m) {
    char *source = read_file(m->path);
    if (!source) {
        char *message = nr_format_hint("cannot read imported module '%s'", m->display_path);
        if (message) nr_report(ctx, m->display_path, 1, 0, message, NULL, NULL);
        return 0;
    }

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    parser_enable_error_collection(&parser);

    int count = 0;
    Stmt **parsed = parse_program(&parser, &count);

    for (ParseError *pe = parser.errors; pe; pe = pe->next) {
        char *message = strdup(pe->message ? pe->message : "syntax error");
        if (message) {
            nr_report(ctx, m->display_path, pe->line, pe->column, message, NULL, NULL);
        }
    }
    int had_error = parser.had_error;
    parser_free_errors(&parser);
    free(source);

    if (!parsed || had_error) {
        if (parsed) {
            for (int i = 0; i < count; i++) stmt_free(parsed[i]);
            free(parsed);
        }
        return 0;
    }

    m->stmts = parsed;
    m->num_stmts = count;
    m->owns_stmts = 1;
    return 1;
}

static NrModule *nr_load(NrCtx *ctx, const char *path, Stmt **stmts, int num_stmts);

/* Resolve one import/re-export path and load its module. */
static void nr_load_edge(NrCtx *ctx, NrModule *m, const char *spec) {
    if (!spec) return;
    if (nr_module_edge_target(m, spec)) return;   /* already loaded */

    char *resolved = resolve_module_path(ctx->resolver, m->path, spec);
    if (!resolved) {
        /* Unresolvable import: the module system reports this itself when it
         * runs. Record the edge as unresolved so `import *` from it widens
         * the scope instead of making every name look undefined. */
        nr_module_add_edge(m, spec, NULL);
        return;
    }

    char *canonical = nr_canonical_path(resolved);
    free(resolved);
    if (!canonical) return;

    NrModule *target = nr_load(ctx, canonical, NULL, 0);
    free(canonical);
    nr_module_add_edge(m, spec, target);
}

static void nr_load_edges(NrCtx *ctx, NrModule *m) {
    for (int i = 0; i < m->num_stmts; i++) {
        Stmt *s = m->stmts[i];
        if (!s) continue;
        if (s->type == STMT_IMPORT) {
            nr_load_edge(ctx, m, s->as.import_stmt.module_path);
        } else if (s->type == STMT_EXPORT && s->as.export_stmt.is_reexport) {
            nr_load_edge(ctx, m, s->as.export_stmt.module_path);
        }
    }
}

static NrModule *nr_load(NrCtx *ctx, const char *path, Stmt **stmts, int num_stmts) {
    NrModule *existing = nr_module_find(ctx, path);
    if (existing) return existing;

    NrModule *m = nr_module_new(ctx, path);
    if (!m) return NULL;

    if (stmts) {
        m->stmts = stmts;
        m->num_stmts = num_stmts;
        m->owns_stmts = 0;
    } else if (!nr_parse_module(ctx, m)) {
        /* A module we cannot read or parse contributes no names. Importers of
         * it go `open` so its absence does not turn every name they use into
         * a false "undefined". */
        m->load_failed = 1;
        ctx->result->load_error_count++;
        return m;
    }

    nr_load_edges(ctx, m);
    return m;
}

/* ========================= phase 2: exports ========================= */

static const char *nr_decl_name(Stmt *decl) {
    if (!decl) return NULL;
    switch (decl->type) {
        case STMT_LET:           return decl->as.let.name;
        case STMT_CONST:         return decl->as.const_stmt.name;
        case STMT_EXTERN_FN:     return decl->as.extern_fn.function_name;
        case STMT_DEFINE_OBJECT: return decl->as.define_object.name;
        case STMT_ENUM:          return decl->as.enum_decl.name;
        case STMT_TYPE_ALIAS:    return decl->as.type_alias.name;
        default:                 return NULL;
    }
}

static void nr_collect_exports(NrCtx *ctx, NrModule *m) {
    if (m->exports_state == 2) return;
    if (m->exports_state == 1) return;   /* import cycle: use what we have */
    m->exports_state = 1;

    for (int i = 0; i < m->num_stmts; i++) {
        Stmt *s = m->stmts[i];
        if (!s || s->type != STMT_EXPORT) continue;

        if (s->as.export_stmt.is_declaration) {
            nr_module_add_export(m, nr_decl_name(s->as.export_stmt.declaration));
        } else if (s->as.export_stmt.is_reexport) {
            NrModule *src = nr_module_edge_target(m, s->as.export_stmt.module_path);
            if (src) nr_collect_exports(ctx, src);
            for (int j = 0; j < s->as.export_stmt.num_exports; j++) {
                const char *name = s->as.export_stmt.export_names[j];
                const char *alias = s->as.export_stmt.export_aliases[j];
                nr_module_add_export(m, alias ? alias : name);
            }
        } else {
            for (int j = 0; j < s->as.export_stmt.num_exports; j++) {
                const char *name = s->as.export_stmt.export_names[j];
                const char *alias = s->as.export_stmt.export_aliases[j];
                nr_module_add_export(m, alias ? alias : name);
            }
        }
    }

    m->exports_state = 2;
}

/* ========================= hints ========================= */

/* Levenshtein distance, capped: anything past `limit` is not a suggestion. */
static int nr_edit_distance(const char *a, const char *b, int limit) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > lb ? la - lb > (size_t)limit : lb - la > (size_t)limit) return limit + 1;
    if (lb > 128) return limit + 1;

    int prev[129], cur[129];
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int)i;
        int row_best = cur[0];
        for (size_t j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int best = del < ins ? del : ins;
            cur[j] = best < sub ? best : sub;
            if (cur[j] < row_best) row_best = cur[j];
        }
        if (row_best > limit) return limit + 1;
        memcpy(prev, cur, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

/* Parse every stdlib module once and remember which one exports what, so a
 * name that exists in the stdlib but was never imported can say where it
 * lives. Only reached once a name has already failed to resolve. */
static void nr_scan_stdlib(NrCtx *ctx) {
#ifndef _WIN32
    if (ctx->stdlib_scanned) return;
    ctx->stdlib_scanned = 1;

    const char *dir_path = ctx->resolver ? ctx->resolver->stdlib_path : NULL;
    if (!dir_path) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len <= 4 || strcmp(name + len - 4, ".hml") != 0) continue;

        char file[PATH_MAX];
        if (snprintf(file, sizeof(file), "%s/%s", dir_path, name) >= (int)sizeof(file)) {
            continue;
        }

        char *source = read_file(file);
        if (!source) continue;

        Lexer lexer;
        lexer_init(&lexer, source);
        Parser parser;
        parser_init(&parser, &lexer);
        int count = 0;
        Stmt **stmts = parse_program(&parser, &count);

        if (stmts && !parser.had_error) {
            char spec[PATH_MAX];
            snprintf(spec, sizeof(spec), "@stdlib/%.*s", (int)(len - 4), name);

            for (int i = 0; i < count; i++) {
                Stmt *s = stmts[i];
                if (!s || s->type != STMT_EXPORT) continue;

                for (int j = -1; j < s->as.export_stmt.num_exports; j++) {
                    const char *exported = NULL;
                    if (j < 0) {
                        if (!s->as.export_stmt.is_declaration) continue;
                        exported = nr_decl_name(s->as.export_stmt.declaration);
                    } else {
                        if (s->as.export_stmt.is_declaration) break;
                        const char *alias = s->as.export_stmt.export_aliases[j];
                        exported = alias ? alias : s->as.export_stmt.export_names[j];
                    }
                    if (!exported) continue;

                    if (ctx->num_stdlib >= ctx->stdlib_cap) {
                        int cap = ctx->stdlib_cap ? ctx->stdlib_cap * 2 : 128;
                        char **owned = realloc(ctx->stdlib_names, sizeof(char *) * (size_t)cap);
                        char **specs = realloc(ctx->stdlib_specs, sizeof(char *) * (size_t)cap);
                        if (owned) ctx->stdlib_names = owned;
                        if (specs) ctx->stdlib_specs = specs;
                        if (!owned || !specs) break;
                        ctx->stdlib_cap = cap;
                    }
                    char *name_copy = strdup(exported);
                    char *spec_copy = strdup(spec);
                    if (!name_copy || !spec_copy) {
                        free(name_copy);
                        free(spec_copy);
                        break;
                    }
                    ctx->stdlib_names[ctx->num_stdlib] = name_copy;
                    ctx->stdlib_specs[ctx->num_stdlib] = spec_copy;
                    ctx->num_stdlib++;
                }
            }
        }

        if (stmts) {
            for (int i = 0; i < count; i++) stmt_free(stmts[i]);
            free(stmts);
        }
        free(source);
    }
    closedir(dir);
#else
    (void)ctx;
#endif
}

/*
 * Why this name might be missing, best guess first:
 *   1. a module this file already imports exports it -> a missing import name
 *   2. a stdlib module exports it                    -> a missing import
 *   3. something spelled almost the same is in scope -> a typo
 */
static char *nr_build_hint(NrCtx *ctx, NrModule *m, const char *name) {
    for (int i = 0; i < m->num_edges; i++) {
        NrModule *target = m->edges[i].target;
        if (!target || !m->edges[i].spec) continue;
        for (int j = 0; j < target->num_exports; j++) {
            if (strcmp(target->exports[j], name) == 0) {
                return nr_format_hint("did you mean to import it from %s?",
                                      m->edges[i].spec);
            }
        }
    }

    nr_scan_stdlib(ctx);
    for (int i = 0; i < ctx->num_stdlib; i++) {
        if (strcmp(ctx->stdlib_names[i], name) == 0) {
            return nr_format_hint("did you mean to import it from %s?",
                                  ctx->stdlib_specs[i]);
        }
    }

    size_t name_len = strlen(name);
    if (name_len >= NR_MIN_HINT_LENGTH) {
        int limit = name_len < NR_LOOSE_HINT_LENGTH ? 1 : NR_MAX_HINT_DISTANCE;
        const char *best = NULL;
        int best_distance = limit + 1;
        for (NrScope *s = ctx->scope; s; s = s->parent) {
            for (int i = 0; i < s->names.count; i++) {
                int d = nr_edit_distance(name, s->names.items[i], limit);
                if (d < best_distance) {
                    best_distance = d;
                    best = s->names.items[i];
                }
            }
        }
        for (int i = 0; nr_builtin_names[i] && best_distance > 1; i++) {
            int d = nr_edit_distance(name, nr_builtin_names[i], limit);
            if (d < best_distance) {
                best_distance = d;
                best = nr_builtin_names[i];
            }
        }
        if (best && best_distance <= limit) {
            return nr_format_hint("did you mean '%s'?", best);
        }
    }

    return NULL;
}

/* ========================= phase 3: walk ========================= */

static void nr_walk_stmt(NrCtx *ctx, NrModule *m, Stmt *s);
static void nr_walk_expr(NrCtx *ctx, NrModule *m, Expr *e);

static void nr_push_scope(NrCtx *ctx, NrScope *scope, int open) {
    nr_names_init(&scope->names);
    scope->open = open;
    scope->parent = ctx->scope;
    ctx->scope = scope;
}

static void nr_pop_scope(NrCtx *ctx, NrScope *scope) {
    ctx->scope = scope->parent;
    nr_names_free(&scope->names);
}

static void nr_declare(NrCtx *ctx, const char *name) {
    if (name && ctx->scope) nr_names_add(&ctx->scope->names, name);
}

static int nr_in_scope(NrCtx *ctx, const char *name) {
    for (NrScope *s = ctx->scope; s; s = s->parent) {
        if (s->open) return 1;
        if (nr_names_has(&s->names, name)) return 1;
    }
    return 0;
}

static void nr_use(NrCtx *ctx, NrModule *m, const char *name, int line, int column) {
    if (!name) return;
    if (nr_in_scope(ctx, name)) return;
    if (name_resolve_is_builtin(name)) return;
    if (nr_is_implicit_name(name)) return;

    char *hint = nr_build_hint(ctx, m, name);
    size_t len = strlen("undefined name '' ()") + strlen(name) + (hint ? strlen(hint) : 0) + 1;
    char *message = malloc(len);
    if (!message) {
        free(hint);
        return;
    }
    if (hint) {
        snprintf(message, len, "undefined name '%s' (%s)", name, hint);
    } else {
        snprintf(message, len, "undefined name '%s'", name);
    }
    nr_report(ctx, ctx->file, line, column, message, strdup(name), hint);
}

/* ---------- pattern bindings ---------- */

static void nr_declare_pattern(NrCtx *ctx, Pattern *p) {
    if (!p) return;
    switch (p->type) {
        case PATTERN_BINDING:
        case PATTERN_WILDCARD:
            nr_declare(ctx, p->as.binding.name);
            break;
        case PATTERN_TYPED:
            nr_declare(ctx, p->as.typed.name);
            break;
        case PATTERN_OR:
            for (int i = 0; i < p->as.or_pattern.num_alternatives; i++) {
                nr_declare_pattern(ctx, p->as.or_pattern.alternatives[i]);
            }
            break;
        case PATTERN_OBJECT:
            for (int i = 0; i < p->as.object.num_fields; i++) {
                ObjectFieldPattern *f = &p->as.object.fields[i];
                if (f->pattern) {
                    nr_declare_pattern(ctx, f->pattern);
                } else {
                    nr_declare(ctx, f->name);   /* { x } binds x */
                }
            }
            if (p->as.object.has_rest) nr_declare(ctx, p->as.object.rest_name);
            break;
        case PATTERN_ARRAY:
            for (int i = 0; i < p->as.array.num_elements; i++) {
                ArrayElementPattern *el = &p->as.array.elements[i];
                if (el->is_rest) {
                    nr_declare(ctx, el->rest_name);
                } else {
                    nr_declare_pattern(ctx, el->pattern);
                }
            }
            break;
        case PATTERN_LITERAL:
            break;
    }
}

static void nr_walk_pattern_exprs(NrCtx *ctx, NrModule *m, Pattern *p) {
    if (!p) return;
    switch (p->type) {
        case PATTERN_LITERAL:
            nr_walk_expr(ctx, m, p->as.literal);
            break;
        case PATTERN_OR:
            for (int i = 0; i < p->as.or_pattern.num_alternatives; i++) {
                nr_walk_pattern_exprs(ctx, m, p->as.or_pattern.alternatives[i]);
            }
            break;
        case PATTERN_OBJECT:
            for (int i = 0; i < p->as.object.num_fields; i++) {
                nr_walk_pattern_exprs(ctx, m, p->as.object.fields[i].pattern);
            }
            break;
        case PATTERN_ARRAY:
            for (int i = 0; i < p->as.array.num_elements; i++) {
                if (!p->as.array.elements[i].is_rest) {
                    nr_walk_pattern_exprs(ctx, m, p->as.array.elements[i].pattern);
                }
            }
            break;
        default:
            break;
    }
}

/* ---------- hoisting ---------- */

/* Seed the current scope with every name `s` declares in it. Nested blocks
 * declare into their own scope and are not visited here. */
static void nr_hoist_stmt(NrCtx *ctx, Stmt *s) {
    if (!s) return;
    switch (s->type) {
        case STMT_LET:
            nr_declare(ctx, s->as.let.name);
            break;
        case STMT_CONST:
            nr_declare(ctx, s->as.const_stmt.name);
            break;
        case STMT_EXTERN_FN:
            nr_declare(ctx, s->as.extern_fn.function_name);
            break;
        case STMT_DEFINE_OBJECT:
            nr_declare(ctx, s->as.define_object.name);
            break;
        case STMT_ENUM:
            nr_declare(ctx, s->as.enum_decl.name);
            break;
        case STMT_TYPE_ALIAS:
            nr_declare(ctx, s->as.type_alias.name);
            break;
        case STMT_EXPORT:
            if (s->as.export_stmt.is_declaration) {
                nr_hoist_stmt(ctx, s->as.export_stmt.declaration);
            }
            break;
        default:
            break;
    }
}

static void nr_hoist_block(NrCtx *ctx, Stmt **stmts, int count) {
    for (int i = 0; i < count; i++) nr_hoist_stmt(ctx, stmts[i]);
}

/* ---------- expressions ---------- */

static void nr_walk_function(NrCtx *ctx, NrModule *m, Expr *fn) {
    /*
     * A `gpu fn` body is a different language: a statically typed subset with
     * its own builtins (global_id, barrier, ...) and its own validation pass
     * in the GPU frontend. Both the kernel AST and the lowered CPU-emulation
     * body are machine-generated from it, so neither is ours to check.
     */
    if (fn->as.function.is_gpu) return;

    /* Defaults are evaluated in the enclosing scope, before the parameters
     * exist. */
    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            nr_walk_expr(ctx, m, fn->as.function.param_defaults[i]);
        }
    }

    NrScope scope;
    nr_push_scope(ctx, &scope, 0);
    for (int i = 0; i < fn->as.function.num_params; i++) {
        nr_declare(ctx, fn->as.function.param_names[i]);
    }
    nr_declare(ctx, fn->as.function.rest_param);

    /* The body is a block statement; walk its statements in the parameter
     * scope so parameters and body locals share one scope, as they do at
     * runtime. */
    Stmt *body = fn->as.function.body;
    if (body && body->type == STMT_BLOCK) {
        nr_hoist_block(ctx, body->as.block.statements, body->as.block.count);
        for (int i = 0; i < body->as.block.count; i++) {
            nr_walk_stmt(ctx, m, body->as.block.statements[i]);
        }
    } else {
        nr_walk_stmt(ctx, m, body);
    }
    nr_pop_scope(ctx, &scope);
}

static void nr_walk_expr(NrCtx *ctx, NrModule *m, Expr *e) {
    if (!e) return;
    switch (e->type) {
        case EXPR_IDENT:
            nr_use(ctx, m, e->as.ident.name, e->line, e->column);
            break;

        case EXPR_ASSIGN:
            /* Hemlock has no implicit declaration: assigning to a name that
             * was never declared throws "Undefined variable" at runtime. */
            nr_use(ctx, m, e->as.assign.name, e->line, e->column);
            nr_walk_expr(ctx, m, e->as.assign.value);
            break;

        case EXPR_BINARY:
            nr_walk_expr(ctx, m, e->as.binary.left);
            nr_walk_expr(ctx, m, e->as.binary.right);
            break;

        case EXPR_UNARY:
            nr_walk_expr(ctx, m, e->as.unary.operand);
            break;

        case EXPR_TERNARY:
            nr_walk_expr(ctx, m, e->as.ternary.condition);
            nr_walk_expr(ctx, m, e->as.ternary.true_expr);
            nr_walk_expr(ctx, m, e->as.ternary.false_expr);
            break;

        case EXPR_CALL:
            nr_walk_expr(ctx, m, e->as.call.func);
            for (int i = 0; i < e->as.call.num_args; i++) {
                nr_walk_expr(ctx, m, e->as.call.args[i]);
            }
            break;

        case EXPR_GET_PROPERTY:
            nr_walk_expr(ctx, m, e->as.get_property.object);
            break;

        case EXPR_SET_PROPERTY:
            nr_walk_expr(ctx, m, e->as.set_property.object);
            nr_walk_expr(ctx, m, e->as.set_property.value);
            break;

        case EXPR_INDEX:
            nr_walk_expr(ctx, m, e->as.index.object);
            nr_walk_expr(ctx, m, e->as.index.index);
            break;

        case EXPR_INDEX_ASSIGN:
            nr_walk_expr(ctx, m, e->as.index_assign.object);
            nr_walk_expr(ctx, m, e->as.index_assign.index);
            nr_walk_expr(ctx, m, e->as.index_assign.value);
            break;

        case EXPR_FUNCTION:
            nr_walk_function(ctx, m, e);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < e->as.array_literal.num_elements; i++) {
                nr_walk_expr(ctx, m, e->as.array_literal.elements[i]);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < e->as.object_literal.num_fields; i++) {
                nr_walk_expr(ctx, m, e->as.object_literal.field_values[i]);
            }
            break;

        case EXPR_PREFIX_INC:  nr_walk_expr(ctx, m, e->as.prefix_inc.operand);  break;
        case EXPR_PREFIX_DEC:  nr_walk_expr(ctx, m, e->as.prefix_dec.operand);  break;
        case EXPR_POSTFIX_INC: nr_walk_expr(ctx, m, e->as.postfix_inc.operand); break;
        case EXPR_POSTFIX_DEC: nr_walk_expr(ctx, m, e->as.postfix_dec.operand); break;

        case EXPR_AWAIT:
            nr_walk_expr(ctx, m, e->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < e->as.string_interpolation.num_parts; i++) {
                nr_walk_expr(ctx, m, e->as.string_interpolation.expr_parts[i]);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            nr_walk_expr(ctx, m, e->as.optional_chain.object);
            nr_walk_expr(ctx, m, e->as.optional_chain.index);
            for (int i = 0; i < e->as.optional_chain.num_args; i++) {
                nr_walk_expr(ctx, m, e->as.optional_chain.args[i]);
            }
            break;

        case EXPR_NULL_COALESCE:
            nr_walk_expr(ctx, m, e->as.null_coalesce.left);
            nr_walk_expr(ctx, m, e->as.null_coalesce.right);
            break;

        case EXPR_MATCH:
            nr_walk_expr(ctx, m, e->as.match_expr.scrutinee);
            for (int i = 0; i < e->as.match_expr.num_arms; i++) {
                MatchArm *arm = &e->as.match_expr.arms[i];
                NrScope scope;
                nr_push_scope(ctx, &scope, 0);
                nr_declare_pattern(ctx, arm->pattern);
                nr_walk_pattern_exprs(ctx, m, arm->pattern);
                nr_walk_expr(ctx, m, arm->guard);
                nr_walk_expr(ctx, m, arm->body);
                nr_pop_scope(ctx, &scope);
            }
            break;

        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_NULL:
            break;
    }
}

/* ---------- statements ---------- */

static void nr_walk_block(NrCtx *ctx, NrModule *m, Stmt *block) {
    if (!block) return;
    if (block->type != STMT_BLOCK) {
        /* A single-statement body still gets its own scope. */
        NrScope scope;
        nr_push_scope(ctx, &scope, 0);
        nr_hoist_stmt(ctx, block);
        nr_walk_stmt(ctx, m, block);
        nr_pop_scope(ctx, &scope);
        return;
    }
    NrScope scope;
    nr_push_scope(ctx, &scope, 0);
    nr_hoist_block(ctx, block->as.block.statements, block->as.block.count);
    for (int i = 0; i < block->as.block.count; i++) {
        nr_walk_stmt(ctx, m, block->as.block.statements[i]);
    }
    nr_pop_scope(ctx, &scope);
}

static void nr_walk_stmt(NrCtx *ctx, NrModule *m, Stmt *s) {
    if (!s) return;
    switch (s->type) {
        case STMT_LET:
            /* The name is already hoisted, so a self-referential closure
             * (`let f = fn() { return f(); };`) resolves. */
            nr_walk_expr(ctx, m, s->as.let.value);
            break;

        case STMT_CONST:
            nr_walk_expr(ctx, m, s->as.const_stmt.value);
            break;

        case STMT_EXPR:
            nr_walk_expr(ctx, m, s->as.expr);
            break;

        case STMT_IF:
            nr_walk_expr(ctx, m, s->as.if_stmt.condition);
            nr_walk_block(ctx, m, s->as.if_stmt.then_branch);
            nr_walk_block(ctx, m, s->as.if_stmt.else_branch);
            break;

        case STMT_WHILE:
            nr_walk_expr(ctx, m, s->as.while_stmt.condition);
            nr_walk_block(ctx, m, s->as.while_stmt.body);
            break;

        case STMT_LOOP:
            nr_walk_block(ctx, m, s->as.loop_stmt.body);
            break;

        case STMT_FOR: {
            /* The initializer's binding is in scope for the condition,
             * increment and body. */
            NrScope scope;
            nr_push_scope(ctx, &scope, 0);
            nr_hoist_stmt(ctx, s->as.for_loop.initializer);
            nr_walk_stmt(ctx, m, s->as.for_loop.initializer);
            nr_walk_expr(ctx, m, s->as.for_loop.condition);
            nr_walk_expr(ctx, m, s->as.for_loop.increment);
            nr_walk_block(ctx, m, s->as.for_loop.body);
            nr_pop_scope(ctx, &scope);
            break;
        }

        case STMT_FOR_IN: {
            nr_walk_expr(ctx, m, s->as.for_in.iterable);
            NrScope scope;
            nr_push_scope(ctx, &scope, 0);
            nr_declare(ctx, s->as.for_in.key_var);
            nr_declare(ctx, s->as.for_in.value_var);
            nr_walk_block(ctx, m, s->as.for_in.body);
            nr_pop_scope(ctx, &scope);
            break;
        }

        case STMT_BLOCK:
            nr_walk_block(ctx, m, s);
            break;

        case STMT_RETURN:
            nr_walk_expr(ctx, m, s->as.return_stmt.value);
            break;

        case STMT_DEFINE_OBJECT:
            for (int i = 0; i < s->as.define_object.num_fields; i++) {
                if (s->as.define_object.field_defaults) {
                    nr_walk_expr(ctx, m, s->as.define_object.field_defaults[i]);
                }
            }
            for (int i = 0; i < s->as.define_object.num_methods; i++) {
                if (s->as.define_object.method_defaults) {
                    nr_walk_expr(ctx, m, s->as.define_object.method_defaults[i]);
                }
            }
            break;

        case STMT_ENUM:
            for (int i = 0; i < s->as.enum_decl.num_variants; i++) {
                if (s->as.enum_decl.variant_values) {
                    nr_walk_expr(ctx, m, s->as.enum_decl.variant_values[i]);
                }
            }
            break;

        case STMT_TRY: {
            nr_walk_block(ctx, m, s->as.try_stmt.try_block);
            if (s->as.try_stmt.catch_block) {
                NrScope scope;
                nr_push_scope(ctx, &scope, 0);
                nr_declare(ctx, s->as.try_stmt.catch_param);
                nr_walk_block(ctx, m, s->as.try_stmt.catch_block);
                nr_pop_scope(ctx, &scope);
            }
            nr_walk_block(ctx, m, s->as.try_stmt.finally_block);
            break;
        }

        case STMT_THROW:
            nr_walk_expr(ctx, m, s->as.throw_stmt.value);
            break;

        case STMT_SWITCH:
            nr_walk_expr(ctx, m, s->as.switch_stmt.expr);
            /* Each case body is its own scope at runtime, even unbraced: a
             * binding declared in one case is not live in the next, despite
             * the fall-through. */
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++) {
                nr_walk_expr(ctx, m, s->as.switch_stmt.case_values[i]);
                nr_walk_block(ctx, m, s->as.switch_stmt.case_bodies[i]);
            }
            break;

        case STMT_DEFER:
            nr_walk_expr(ctx, m, s->as.defer_stmt.call);
            break;

        case STMT_EXPORT:
            if (s->as.export_stmt.is_declaration) {
                nr_walk_stmt(ctx, m, s->as.export_stmt.declaration);
            }
            /* `export { a, b }` and re-exports name no new identifiers to
             * resolve here; a missing local is reported by the module system. */
            break;

        case STMT_IMPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
        case STMT_TYPE_ALIAS:
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
    }
}

/* ---------- module scope ---------- */

/* Seed a module's top-level scope with its imports. Returns 1 if the scope has
 * to stay open because some import's contents are unknown. */
static int nr_declare_imports(NrCtx *ctx, NrModule *m) {
    int open = 0;
    for (int i = 0; i < m->num_stmts; i++) {
        Stmt *s = m->stmts[i];
        if (!s || s->type != STMT_IMPORT) continue;

        NrModule *target = nr_module_edge_target(m, s->as.import_stmt.module_path);

        if (s->as.import_stmt.is_namespace) {
            if (s->as.import_stmt.namespace_name) {
                /* import * as ns from "mod" - one binding, `ns` */
                nr_declare(ctx, s->as.import_stmt.namespace_name);
            } else {
                /* import * from "mod" - every export lands in this scope */
                if (!target || target->load_failed) {
                    open = 1;
                } else {
                    nr_collect_exports(ctx, target);
                    for (int j = 0; j < target->num_exports; j++) {
                        nr_declare(ctx, target->exports[j]);
                    }
                }
            }
            continue;
        }

        for (int j = 0; j < s->as.import_stmt.num_imports; j++) {
            const char *name = s->as.import_stmt.import_names[j];
            const char *alias = s->as.import_stmt.import_aliases[j];
            nr_declare(ctx, alias ? alias : name);
        }
    }
    return open;
}

static void nr_walk_module(NrCtx *ctx, NrModule *m) {
    if (m->walked || m->load_failed) return;
    m->walked = 1;

    const char *saved_file = ctx->file;
    NrScope *saved_scope = ctx->scope;
    ctx->file = m->display_path;
    ctx->scope = NULL;

    NrScope module_scope;
    nr_push_scope(ctx, &module_scope, 0);
    module_scope.open = nr_declare_imports(ctx, m);
    nr_hoist_block(ctx, m->stmts, m->num_stmts);

    for (int i = 0; i < m->num_stmts; i++) {
        nr_walk_stmt(ctx, m, m->stmts[i]);
    }

    nr_pop_scope(ctx, &module_scope);
    ctx->file = saved_file;
    ctx->scope = saved_scope;
}

/* ========================= entry points ========================= */

static void nr_ctx_free(NrCtx *ctx) {
    NrModule *m = ctx->modules;
    while (m) {
        NrModule *next = m->next;
        if (m->owns_stmts && m->stmts) {
            for (int i = 0; i < m->num_stmts; i++) stmt_free(m->stmts[i]);
            free(m->stmts);
        }
        for (int i = 0; i < m->num_edges; i++) free(m->edges[i].spec);
        free(m->edges);
        for (int i = 0; i < m->num_exports; i++) free(m->exports[i]);
        free(m->exports);
        free(m->path);
        free(m->display_path);
        free(m);
        m = next;
    }
    for (int i = 0; i < ctx->num_stdlib; i++) {
        free(ctx->stdlib_names[i]);
        free(ctx->stdlib_specs[i]);
    }
    free(ctx->stdlib_names);
    free(ctx->stdlib_specs);
    if (ctx->resolver) module_resolution_free(ctx->resolver);
}

NameResolveResult *name_resolve_entry(const char *entry_path,
                                      Stmt **entry_stmts, int entry_count) {
    if (!entry_path) return NULL;

    NameResolveResult *result = calloc(1, sizeof(NameResolveResult));
    if (!result) return NULL;

    NrCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.result = result;
    ctx.resolver = module_resolution_new(NULL, entry_path);

    char *canonical = nr_canonical_path(entry_path);
    NrModule *entry = nr_load(&ctx, canonical ? canonical : entry_path,
                              entry_stmts, entry_count);
    free(canonical);

    if (!entry || entry->load_failed) {
        /*
         * The entry file itself would not parse. Every caller parses the entry
         * for its own reasons and reports that failure in its own format -
         * the interpreter with a source snippet and caret - so saying it again
         * here would both duplicate and downgrade it. Drop what this pass
         * collected and let the caller speak.
         */
        nr_ctx_free(&ctx);
        if (!entry) {
            name_resolve_result_free(result);
            return NULL;
        }
        NameDiag *d = result->diags;
        while (d) {
            NameDiag *next = d->next;
            free(d->file);
            free(d->message);
            free(d->name);
            free(d->hint);
            free(d);
            d = next;
        }
        result->diags = NULL;
        result->tail = NULL;
        result->error_count = 0;
        result->entry_parse_failed = 1;
        return result;
    }

    /* The entry file prints exactly as the caller spelled it. */
    free(entry->display_path);
    entry->display_path = strdup(entry_path);

    for (NrModule *m = ctx.modules; m; m = m->next) {
        nr_collect_exports(&ctx, m);
    }

    /* Entry first, then its modules: diagnostics come out nearest-first. */
    nr_walk_module(&ctx, entry);
    for (NrModule *m = ctx.modules; m; m = m->next) {
        nr_walk_module(&ctx, m);
    }

    nr_ctx_free(&ctx);
    return result;
}

void name_resolve_result_free(NameResolveResult *result) {
    if (!result) return;
    NameDiag *d = result->diags;
    while (d) {
        NameDiag *next = d->next;
        free(d->file);
        free(d->message);
        free(d->name);
        free(d->hint);
        free(d);
        d = next;
    }
    free(result);
}

char *name_diag_format(const NameDiag *diag) {
    if (!diag) return NULL;
    size_t len = strlen(diag->file) + strlen(diag->message) + 64;
    char *out = malloc(len);
    if (!out) return NULL;
    if (diag->column > 0) {
        snprintf(out, len, "%s:%d:%d: error: %s", diag->file, diag->line,
                 diag->column, diag->message);
    } else {
        snprintf(out, len, "%s:%d: error: %s", diag->file, diag->line,
                 diag->message);
    }
    return out;
}

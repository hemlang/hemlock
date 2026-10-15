/*
 * Hemlock Compiler - Borrow / Ownership Checker
 *
 * See include/compiler/borrow_check.h for the high-level description.
 *
 * Model
 * -----
 * A *resource* is created by an acquisition expression (alloc/buffer/open).
 * Every binding that names a resource points at it by integer id; several
 * bindings may alias one resource (Hemlock values are shared, not moved,
 * so `let q = p;` aliases by default). A resource carries a flow-sensitive
 * state that the walk updates:
 *
 *   OWNED        live and held
 *   FREED        released on every path reaching here
 *   MOVED        ownership moved out of the binding (strict mode)
 *   MAYBE_FREED  released on some-but-not-all paths (branch merge result)
 *
 * Releasing happens through free(x) / x.close(). Acquisition reused on an
 * existing binding (`x = alloc(...)`) revives it to OWNED.
 *
 * Branch handling clones the resource-state vector, analyses each arm, and
 * merges; arms that diverge (return/break/continue/throw) are dropped from
 * the merge so guarded-then-return frees don't create false positives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "compiler/borrow_check.h"
#include "../../include/hemlock_limits.h"

// ========== INTERNAL TYPES ==========

// How a path leaves its block. Distinguishing break/continue (which leave the
// enclosing loop/switch) from return/throw (which leave the function) lets the
// switch merge include cases that end in the normal `break`, while still
// excluding cases that return out of the function.
#define BC_EXIT_NONE     0
#define BC_EXIT_BREAK    1
#define BC_EXIT_CONTINUE 2
#define BC_EXIT_RETURN   3

typedef enum {
    BC_OWNED,
    BC_FREED,
    BC_MOVED,
    BC_MAYBE_FREED,
} BcState;

typedef struct {
    BcState state;
    int acquire_line;
    char *kind;          // "memory", "buffer", "file" — owned
    char *origin;        // first binding name — owned, for messages
    unsigned char deferred_free;  // a `defer free(x)` is pending for scope exit
    unsigned char escaped;        // returned / stored outward; suppresses leak
} BcResource;

typedef struct BcBinding {
    char *name;          // owned
    int resource_id;     // index into resources, or -1
    unsigned char moved; // strict mode: binding moved out
    struct BcBinding *next;
} BcBinding;

typedef struct BcScope {
    BcBinding *bindings;
    struct BcScope *parent;
} BcScope;

// Snapshot entry for branch merging (mutable resource state only).
typedef struct {
    BcState state;
    unsigned char deferred_free;
    unsigned char escaped;
} BcSnap;

// How a function treats each of its parameters, for interprocedural analysis.
#define BC_PARAM_BORROW   0   // read-only / not released by the callee
#define BC_PARAM_CONSUMED 1   // released on every normal path
#define BC_PARAM_MAYBE    2   // released on some paths only

typedef struct {
    char *name;              // owned — function name
    int num_params;
    unsigned char *consumed; // owned — BC_PARAM_* per parameter (NULL if none)
    char *returns_kind;      // owned — resource kind every return path yields
                             // fresh ("factory"), or NULL
} BcFnSummary;

// ========== ACCESSORS ==========

static BcResource *bc_resources(BorrowContext *ctx) {
    return (BcResource *)ctx->resources;
}

static BcResource *bc_res(BorrowContext *ctx, int id) {
    if (id < 0 || id >= ctx->num_resources) return NULL;
    return &bc_resources(ctx)[id];
}

// ========== DIAGNOSTICS ==========

static void bc_report(BorrowContext *ctx, int line, int is_error, const char *fmt, ...) {
    // While computing a function summary we re-walk the body purely to observe
    // how it treats its parameters; any diagnostics there are not real.
    if (ctx->summary_mode) return;

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (is_error) ctx->error_count++;
    else ctx->warning_count++;

    if (ctx->collect) {
        BorrowDiag *d = calloc(1, sizeof(BorrowDiag));
        if (!d) return;
        d->line = line;
        d->column = 0;
        d->end_column = 1;
        d->message = strdup(message);
        d->is_error = is_error;
        d->next = NULL;
        if (ctx->diags_tail) ctx->diags_tail->next = d;
        else ctx->diags = d;
        ctx->diags_tail = d;
    } else {
        fprintf(stderr, "%s:%d: %s: %s\n",
                ctx->filename ? ctx->filename : "<unknown>", line,
                is_error ? "error" : "warning", message);
    }
}

// Emit at the configured severity (warning, unless errors_are_fatal).
static void bc_warn(BorrowContext *ctx, int line, const char *fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    bc_report(ctx, line, ctx->errors_are_fatal, "%s", message);
}

// ========== RESOURCE / BINDING MANAGEMENT ==========

static int bc_new_resource(BorrowContext *ctx, const char *kind,
                           const char *origin, int line) {
    if (ctx->num_resources >= ctx->cap_resources) {
        int ncap = ctx->cap_resources ? ctx->cap_resources * 2 : 16;
        BcResource *n = realloc(ctx->resources, (size_t)ncap * sizeof(BcResource));
        if (!n) return -1;
        ctx->resources = n;
        ctx->cap_resources = ncap;
    }
    int id = ctx->num_resources++;
    BcResource *r = &bc_resources(ctx)[id];
    r->state = BC_OWNED;
    r->acquire_line = line;
    r->kind = strdup(kind ? kind : "resource");
    r->origin = strdup(origin ? origin : "<temp>");
    r->deferred_free = 0;
    r->escaped = 0;
    return id;
}

static void bc_push_scope(BorrowContext *ctx) {
    BcScope *s = calloc(1, sizeof(BcScope));
    if (!s) return;
    s->parent = (BcScope *)ctx->scope;
    ctx->scope = s;
    ctx->scope_depth++;
}

// Look up a binding by name across the scope chain.
static BcBinding *bc_lookup(BorrowContext *ctx, const char *name) {
    for (BcScope *s = (BcScope *)ctx->scope; s; s = s->parent) {
        for (BcBinding *b = s->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) return b;
        }
    }
    return NULL;
}

// Number of live (non-moved) bindings that refer to resource `rid` anywhere in
// the scope chain. Used to tell "reassigning the last reference" apart from
// reassigning one of several aliases.
static int bc_alias_count(BorrowContext *ctx, int rid) {
    int count = 0;
    for (BcScope *s = (BcScope *)ctx->scope; s; s = s->parent) {
        for (BcBinding *b = s->bindings; b; b = b->next) {
            if (b->resource_id == rid && !b->moved) count++;
        }
    }
    return count;
}

// Define a binding in the innermost scope (shadows outer ones).
static BcBinding *bc_define(BorrowContext *ctx, const char *name, int resource_id) {
    BcScope *s = (BcScope *)ctx->scope;
    if (!s) return NULL;
    BcBinding *b = calloc(1, sizeof(BcBinding));
    if (!b) return NULL;
    b->name = strdup(name);
    b->resource_id = resource_id;
    b->moved = 0;
    b->next = s->bindings;
    s->bindings = b;
    return b;
}

// Leak check on scope exit (strict mode only).
static void bc_pop_scope(BorrowContext *ctx) {
    BcScope *s = (BcScope *)ctx->scope;
    if (!s) return;
    BcBinding *b = s->bindings;
    while (b) {
        // A resource still OWNED (not freed, moved, escaped, or deferred) when
        // its binding leaves scope is a leak. This is independent of whether
        // the path diverged via return: escape (return p) is tracked separately
        // by bc_mark_escape, so a returning function with an unreleased local
        // is still a leak.
        //
        // MAYBE_FREED is a leak too, on the paths that did not free. The merge
        // already computes it, and a use in that state is reported as a possible
        // double free; saying nothing about it here left the two halves of the
        // same branch analysis disagreeing. `if (c) { free(p); }` leaked on !c
        // and the checker was silent, while the mirrored `if (c) { free(p); }
        // free(p);` has warned since cond_possible_double.
        if (ctx->strict && b->resource_id >= 0 && !b->moved) {
            BcResource *r = bc_res(ctx, b->resource_id);
            // Only complain once (about the originating binding) for live,
            // non-escaped, non-deferred resources.
            if (r && !r->escaped && !r->deferred_free &&
                strcmp(r->origin, b->name) == 0) {
                if (r->state == BC_OWNED) {
                    bc_warn(ctx, r->acquire_line,
                            "'%s' (%s) acquired here is never freed before it goes "
                            "out of scope (possible leak)", b->name, r->kind);
                } else if (r->state == BC_MAYBE_FREED) {
                    bc_warn(ctx, r->acquire_line,
                            "'%s' (%s) acquired here is not freed on some paths "
                            "before it goes out of scope (possible leak)",
                            b->name, r->kind);
                }
            }
        }
        BcBinding *next = b->next;
        free(b->name);
        free(b);
        b = next;
    }
    ctx->scope = s->parent;
    ctx->scope_depth--;
    free(s);
}

// ========== SNAPSHOT / MERGE ==========

static BcSnap *bc_snapshot(BorrowContext *ctx, int *out_n) {
    int n = ctx->num_resources;
    *out_n = n;
    if (n == 0) return NULL;
    BcSnap *snap = malloc((size_t)n * sizeof(BcSnap));
    if (!snap) return NULL;
    BcResource *res = bc_resources(ctx);
    for (int i = 0; i < n; i++) {
        snap[i].state = res[i].state;
        snap[i].deferred_free = res[i].deferred_free;
        snap[i].escaped = res[i].escaped;
    }
    return snap;
}

static void bc_restore(BorrowContext *ctx, BcSnap *snap, int n) {
    BcResource *res = bc_resources(ctx);
    for (int i = 0; i < n && i < ctx->num_resources; i++) {
        res[i].state = snap[i].state;
        res[i].deferred_free = snap[i].deferred_free;
        res[i].escaped = snap[i].escaped;
    }
}

static int bc_state_dead(BcState s) {
    return s == BC_FREED || s == BC_MOVED || s == BC_MAYBE_FREED;
}

static BcState bc_merge_state(BcState a, BcState b) {
    if (a == b) return a;
    if (bc_state_dead(a) && bc_state_dead(b)) {
        // both dead but disagree (e.g. FREED vs MOVED) -> conservative
        return BC_MAYBE_FREED;
    }
    // one live, one dead
    return BC_MAYBE_FREED;
}

// Merge two captured branch states into the live context. Diverged arms are
// passed as NULL so only the surviving path(s) contribute.
static void bc_merge_two(BorrowContext *ctx, BcSnap *a, int na, BcSnap *b, int nb) {
    BcResource *res = bc_resources(ctx);
    if (a && b) {
        for (int i = 0; i < ctx->num_resources; i++) {
            BcState sa = (i < na) ? a[i].state : res[i].state;
            BcState sb = (i < nb) ? b[i].state : res[i].state;
            res[i].state = bc_merge_state(sa, sb);
            res[i].deferred_free = ((i < na && a[i].deferred_free) ||
                                    (i < nb && b[i].deferred_free)) ? 1 : 0;
            res[i].escaped = ((i < na && a[i].escaped) ||
                              (i < nb && b[i].escaped)) ? 1 : 0;
        }
    } else if (a) {
        bc_restore(ctx, a, na);
    } else if (b) {
        bc_restore(ctx, b, nb);
    }
}

// ========== FORWARD DECLARATIONS ==========

static void bc_stmt(BorrowContext *ctx, Stmt *stmt);
static void bc_expr_use(BorrowContext *ctx, Expr *expr);
static void bc_analyze_function(BorrowContext *ctx, Expr *fn);
static const BcFnSummary *bc_find_summary(BorrowContext *ctx, const char *name);
static void bc_apply_call_consumption(BorrowContext *ctx, Expr *call);
static void bc_mark_escape(BorrowContext *ctx, Expr *value);
static const char *bc_value_acquire_kind(BorrowContext *ctx, Expr *e);

// ========== ACQUISITION / RELEASE RECOGNITION ==========

// Acquisition builtins and the resource kind each produces. A *resource* is
// anything obtained from one of these and released by an explicit operation;
// the kind drives both the diagnostic wording and the release-method match.
static const struct { const char *name; const char *kind; } BC_ACQUIRE[] = {
    {"alloc",          "memory"},       // raw memory      -> free()
    {"buffer",         "buffer"},       // safe buffer     -> free()
    {"open",           "file"},         // file handle     -> .close()
    {"channel",        "channel"},      // channel         -> .close()
    {"spawn",          "task"},         // async task      -> join()/detach()/await
    {"spawn_with",     "task"},
    {"ffi_open",       "ffi library"},  // dynamic FFI lib -> ffi_close()
    {"mmap_open",      "mapping"},      // memory mapping  -> mmap_close()
    {"mmap_open_anon", "mapping"},
};

// Function-call release builtins whose *first* argument is the released binding,
// paired with the canonical op verb used in diagnostics.
static const struct { const char *name; const char *op; } BC_RELEASE_FN[] = {
    {"free",       "free"},
    {"ffi_close",  "ffi_close"},
    {"mmap_close", "mmap_close"},
    {"join",       "join"},
    {"detach",     "detach"},
};

// If `e` acquires an owned resource, return its kind string, else NULL.
static const char *bc_acquire_kind(Expr *e) {
    if (!e || e->type != EXPR_CALL) return NULL;
    Expr *f = e->as.call.func;
    if (!f || f->type != EXPR_IDENT) return NULL;
    const char *name = f->as.ident.name;
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(BC_ACQUIRE) / sizeof(BC_ACQUIRE[0]); i++)
        if (strcmp(name, BC_ACQUIRE[i].name) == 0) return BC_ACQUIRE[i].kind;
    return NULL;
}

// Like bc_acquire_kind, but also recognises calls to user functions whose
// summary says every return path yields a fresh owned resource ("factories"),
// so `let p = make();` is tracked just like `let p = alloc(...);`.
static const char *bc_value_acquire_kind(BorrowContext *ctx, Expr *e) {
    const char *kind = bc_acquire_kind(e);
    if (kind) return kind;
    if (!e || e->type != EXPR_CALL) return NULL;
    Expr *f = e->as.call.func;
    if (!f || f->type != EXPR_IDENT || !f->as.ident.name) return NULL;
    const BcFnSummary *sum = bc_find_summary(ctx, f->as.ident.name);
    return sum ? sum->returns_kind : NULL;
}

// Is `op` the correct release operation for a resource of `kind`?
static int bc_op_matches_kind(const char *op, const char *kind) {
    if (!op || !kind) return 1;
    if (strcmp(op, "param") == 0 || strcmp(kind, "param") == 0)
        return 1;  // summary placeholder: any release "matches"
    if (strcmp(op, "free") == 0)
        return strcmp(kind, "memory") == 0 || strcmp(kind, "buffer") == 0;
    if (strcmp(op, "close") == 0)
        return strcmp(kind, "file") == 0 || strcmp(kind, "channel") == 0;
    if (strcmp(op, "join") == 0 || strcmp(op, "detach") == 0 ||
        strcmp(op, "await") == 0)
        return strcmp(kind, "task") == 0;
    if (strcmp(op, "ffi_close") == 0) return strcmp(kind, "ffi library") == 0;
    if (strcmp(op, "mmap_close") == 0) return strcmp(kind, "mapping") == 0;
    return 1;  // unknown op: don't second-guess
}

// Human-readable hint naming the correct release operation for `kind`.
static const char *bc_kind_release_hint(const char *kind) {
    if (!kind) return "the matching release";
    if (strcmp(kind, "memory") == 0 || strcmp(kind, "buffer") == 0)
        return "free()";
    if (strcmp(kind, "file") == 0 || strcmp(kind, "channel") == 0)
        return ".close()";
    if (strcmp(kind, "task") == 0) return "join(), detach() or await";
    if (strcmp(kind, "ffi library") == 0) return "ffi_close()";
    if (strcmp(kind, "mapping") == 0) return "mmap_close()";
    return "the matching release";
}

// If `e` is a recognised release form, return the released binding's name and
// op verb via out-params; return 1 on match.
//   free(x) / ffi_close(x) / mmap_close(x) / join(x) / detach(x)   (first arg)
//   x.close()                                                      (method)
static int bc_release_target(Expr *e, const char **out_name, const char **out_op) {
    if (!e || e->type != EXPR_CALL) return 0;
    Expr *f = e->as.call.func;
    if (!f) return 0;

    // Function-style release: <fn>(x, ...)
    if (f->type == EXPR_IDENT && f->as.ident.name &&
        e->as.call.num_args >= 1 && e->as.call.args[0] &&
        e->as.call.args[0]->type == EXPR_IDENT) {
        for (size_t i = 0; i < sizeof(BC_RELEASE_FN) / sizeof(BC_RELEASE_FN[0]); i++) {
            if (strcmp(f->as.ident.name, BC_RELEASE_FN[i].name) == 0) {
                *out_name = e->as.call.args[0]->as.ident.name;
                *out_op = BC_RELEASE_FN[i].op;
                return 1;
            }
        }
    }
    // Method-style release: x.close()
    if (f->type == EXPR_GET_PROPERTY && f->as.get_property.property &&
        strcmp(f->as.get_property.property, "close") == 0 &&
        f->as.get_property.object &&
        f->as.get_property.object->type == EXPR_IDENT) {
        *out_name = f->as.get_property.object->as.ident.name;
        *out_op = "close";
        return 1;
    }
    return 0;
}

// A call that definitely ends the current path (panic/exit).
static int bc_is_diverging_call(Expr *e) {
    if (!e || e->type != EXPR_CALL) return 0;
    Expr *f = e->as.call.func;
    if (!f || f->type != EXPR_IDENT || !f->as.ident.name) return 0;
    return strcmp(f->as.ident.name, "panic") == 0 ||
           strcmp(f->as.ident.name, "exit") == 0;
}

// ========== CORE CHECKS ==========

// Record a *use* of binding `name`: flag if its resource is dead.
static void bc_check_use(BorrowContext *ctx, const char *name, int line) {
    if (line <= 0) line = ctx->cur_line;
    BcBinding *b = bc_lookup(ctx, name);
    if (!b) return;
    if (b->moved) {
        bc_warn(ctx, line,
                "use of '%s' after it was moved", name);
        return;
    }
    BcResource *r = bc_res(ctx, b->resource_id);
    if (!r) return;
    if (r->state == BC_FREED) {
        bc_warn(ctx, line,
                "use of '%s' after it was freed (line %d)", name, r->acquire_line);
    } else if (r->state == BC_MOVED) {
        bc_warn(ctx, line, "use of '%s' after it was moved", name);
    }
    // MAYBE_FREED uses are deliberately not reported (avoid false positives).
}

// Release binding `name` via free()/close().
static void bc_release(BorrowContext *ctx, const char *name, const char *op, int line) {
    if (line <= 0) line = ctx->cur_line;
    BcBinding *b = bc_lookup(ctx, name);
    if (!b) return;
    if (b->moved) {
        bc_warn(ctx, line, "%s of '%s' after it was moved", op, name);
        return;
    }
    BcResource *r = bc_res(ctx, b->resource_id);
    if (!r) return;
    // Wrong release operation for this kind of resource (e.g. free() on a file).
    // The release almost certainly did not happen, so leave the state untouched
    // — that keeps any real leak / correct-release diagnostics intact.
    if (!bc_op_matches_kind(op, r->kind)) {
        bc_warn(ctx, line,
                "'%s' (%s) cannot be released with %s(); use %s",
                name, r->kind, op, bc_kind_release_hint(r->kind));
        return;
    }
    switch (r->state) {
        case BC_FREED:
            bc_warn(ctx, line,
                    "double free: '%s' was already freed (line %d)",
                    name, r->acquire_line);
            break;
        case BC_MAYBE_FREED:
            bc_warn(ctx, line,
                    "possible double free: '%s' may already be freed on some paths",
                    name);
            r->state = BC_FREED;
            break;
        case BC_MOVED:
            bc_warn(ctx, line, "%s of '%s' after it was moved", op, name);
            break;
        case BC_OWNED:
            if (r->deferred_free) {
                bc_warn(ctx, line,
                        "double free: '%s' is already scheduled to be freed via "
                        "defer", name);
            }
            r->state = BC_FREED;
            break;
    }
}

// ========== EXPRESSION WALK ==========

// Walk one conditionally-evaluated sub-expression (a ternary/match arm, or the
// short-circuited side of &&, ||, ??) as its own path, the expression-level
// analogue of bc_branch. Returns how the arm exited (BC_EXIT_NONE unless a
// diverging call such as panic()/exit() ended it).
static int bc_expr_arm(BorrowContext *ctx, Expr *e) {
    int saved_div = ctx->diverged;
    int saved_kind = ctx->exit_kind;
    ctx->diverged = 0;
    ctx->exit_kind = BC_EXIT_NONE;
    bc_expr_use(ctx, e);
    int kind = ctx->diverged ? ctx->exit_kind : BC_EXIT_NONE;
    ctx->diverged = saved_div;
    ctx->exit_kind = saved_kind;
    return kind;
}

// Does `e` mention identifier `name` anywhere? Used to keep a binding attached
// to its resource across self-referential reassignment (`p = p + 8;` derives a
// new pointer from the same allocation, it does not drop it).
static int bc_expr_mentions(Expr *e, const char *name) {
    if (!e || !name) return 0;
    switch (e->type) {
        case EXPR_IDENT:
            return e->as.ident.name && strcmp(e->as.ident.name, name) == 0;
        case EXPR_BINARY:
            return bc_expr_mentions(e->as.binary.left, name) ||
                   bc_expr_mentions(e->as.binary.right, name);
        case EXPR_UNARY:
            return bc_expr_mentions(e->as.unary.operand, name);
        case EXPR_TERNARY:
            return bc_expr_mentions(e->as.ternary.condition, name) ||
                   bc_expr_mentions(e->as.ternary.true_expr, name) ||
                   bc_expr_mentions(e->as.ternary.false_expr, name);
        case EXPR_CALL: {
            if (bc_expr_mentions(e->as.call.func, name)) return 1;
            for (int i = 0; i < e->as.call.num_args; i++)
                if (bc_expr_mentions(e->as.call.args[i], name)) return 1;
            return 0;
        }
        case EXPR_ASSIGN:
            return bc_expr_mentions(e->as.assign.value, name);
        case EXPR_GET_PROPERTY:
            return bc_expr_mentions(e->as.get_property.object, name);
        case EXPR_INDEX:
            return bc_expr_mentions(e->as.index.object, name) ||
                   bc_expr_mentions(e->as.index.index, name);
        case EXPR_NULL_COALESCE:
            return bc_expr_mentions(e->as.null_coalesce.left, name) ||
                   bc_expr_mentions(e->as.null_coalesce.right, name);
        default:
            return 0;
    }
}

// Walk an expression treating every identifier reference as a *use*. Special
// forms (acquisition, release, assignment) are handled by callers before they
// reach here, but nested calls are still walked so uses inside arguments count.
static void bc_expr_use(BorrowContext *ctx, Expr *expr) {
    if (!expr) return;
    switch (expr->type) {
        case EXPR_IDENT:
            bc_check_use(ctx, expr->as.ident.name, expr->line);
            break;
        case EXPR_BINARY:
            bc_expr_use(ctx, expr->as.binary.left);
            if (expr->as.binary.op == OP_AND || expr->as.binary.op == OP_OR) {
                // Short-circuit: the right operand may not run. Merge the
                // skipped path (pre-state) with the evaluated one.
                int n; BcSnap *snap = bc_snapshot(ctx, &n);
                bc_expr_arm(ctx, expr->as.binary.right);
                int cn; BcSnap *cur = bc_snapshot(ctx, &cn);
                bc_merge_two(ctx, snap, n, cur, cn);
                free(snap); free(cur);
            } else {
                bc_expr_use(ctx, expr->as.binary.right);
            }
            break;
        case EXPR_UNARY:
            bc_expr_use(ctx, expr->as.unary.operand);
            break;
        case EXPR_TERNARY: {
            // Exactly one arm runs: analyse each from the same entry state and
            // merge, exactly like an if/else statement.
            bc_expr_use(ctx, expr->as.ternary.condition);
            int n; BcSnap *snap = bc_snapshot(ctx, &n);
            int tdiv = bc_expr_arm(ctx, expr->as.ternary.true_expr);
            int tn; BcSnap *tstate = bc_snapshot(ctx, &tn);
            bc_restore(ctx, snap, n);
            int fdiv = bc_expr_arm(ctx, expr->as.ternary.false_expr);
            int en; BcSnap *fstate = bc_snapshot(ctx, &en);
            bc_merge_two(ctx, tdiv ? NULL : tstate, tn,
                              fdiv ? NULL : fstate, en);
            if (tdiv && fdiv) {
                ctx->diverged = 1;
                ctx->exit_kind = (tdiv == BC_EXIT_RETURN || fdiv == BC_EXIT_RETURN)
                                 ? BC_EXIT_RETURN : BC_EXIT_BREAK;
            }
            free(snap); free(tstate); free(fstate);
            break;
        }
        case EXPR_CALL: {
            // Release forms consume rather than use their target.
            const char *rname = NULL, *rop = NULL;
            if (bc_release_target(expr, &rname, &rop)) {
                if (rname) bc_release(ctx, rname, rop, expr->line);
                // Walk remaining args (skip the released target itself).
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    Expr *a = expr->as.call.args[i];
                    if (a && a->type == EXPR_IDENT && rname &&
                        a->as.ident.name && strcmp(a->as.ident.name, rname) == 0)
                        continue;
                    bc_expr_use(ctx, a);
                }
                break;
            }
            bc_expr_use(ctx, expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++)
                bc_expr_use(ctx, expr->as.call.args[i]);
            // Interprocedural: passing a resource to a parameter the callee
            // releases consumes it here. Done after the use-walk so passing an
            // already-dead resource is still reported as a use first.
            bc_apply_call_consumption(ctx, expr);
            if (bc_is_diverging_call(expr)) { ctx->diverged = 1; ctx->exit_kind = BC_EXIT_RETURN; }
            break;
        }
        case EXPR_ASSIGN:
            // `x = value`: RHS is a use; LHS target is handled at stmt level
            // only for the simple ident case. Here (nested) just walk value.
            bc_expr_use(ctx, expr->as.assign.value);
            break;
        case EXPR_GET_PROPERTY:
            bc_expr_use(ctx, expr->as.get_property.object);
            break;
        case EXPR_SET_PROPERTY:
            bc_expr_use(ctx, expr->as.set_property.object);
            bc_expr_use(ctx, expr->as.set_property.value);
            // Storing a resource into a field hands ownership to the container.
            bc_mark_escape(ctx, expr->as.set_property.value);
            break;
        case EXPR_INDEX:
            bc_expr_use(ctx, expr->as.index.object);
            bc_expr_use(ctx, expr->as.index.index);
            break;
        case EXPR_INDEX_ASSIGN:
            bc_expr_use(ctx, expr->as.index_assign.object);
            bc_expr_use(ctx, expr->as.index_assign.index);
            bc_expr_use(ctx, expr->as.index_assign.value);
            // Storing a resource into a slot hands ownership to the container.
            bc_mark_escape(ctx, expr->as.index_assign.value);
            break;
        case EXPR_FUNCTION:
            bc_analyze_function(ctx, expr);
            break;
        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                bc_expr_use(ctx, expr->as.array_literal.elements[i]);
                // Resource placed into a container: ownership moves to it.
                bc_mark_escape(ctx, expr->as.array_literal.elements[i]);
            }
            break;
        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                bc_expr_use(ctx, expr->as.object_literal.field_values[i]);
                bc_mark_escape(ctx, expr->as.object_literal.field_values[i]);
            }
            break;
        case EXPR_PREFIX_INC: bc_expr_use(ctx, expr->as.prefix_inc.operand); break;
        case EXPR_PREFIX_DEC: bc_expr_use(ctx, expr->as.prefix_dec.operand); break;
        case EXPR_POSTFIX_INC: bc_expr_use(ctx, expr->as.postfix_inc.operand); break;
        case EXPR_POSTFIX_DEC: bc_expr_use(ctx, expr->as.postfix_dec.operand); break;
        case EXPR_AWAIT: {
            // `await t` consumes the task `t` (like join). If the awaited
            // expression names a tracked task, treat it as a release; otherwise
            // it is an ordinary use of the awaited value.
            Expr *a = expr->as.await_expr.awaited_expr;
            if (a && a->type == EXPR_IDENT && a->as.ident.name) {
                BcBinding *b = bc_lookup(ctx, a->as.ident.name);
                BcResource *r = b ? bc_res(ctx, b->resource_id) : NULL;
                if (r && strcmp(r->kind, "task") == 0) {
                    bc_release(ctx, a->as.ident.name, "await", expr->line);
                    break;
                }
            }
            bc_expr_use(ctx, a);
            break;
        }
        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++)
                bc_expr_use(ctx, expr->as.string_interpolation.expr_parts[i]);
            break;
        case EXPR_OPTIONAL_CHAIN:
            bc_expr_use(ctx, expr->as.optional_chain.object);
            bc_expr_use(ctx, expr->as.optional_chain.index);
            for (int i = 0; i < expr->as.optional_chain.num_args; i++)
                bc_expr_use(ctx, expr->as.optional_chain.args[i]);
            break;
        case EXPR_NULL_COALESCE: {
            // The right operand only runs when the left is null: merge the
            // skipped path with the evaluated one.
            bc_expr_use(ctx, expr->as.null_coalesce.left);
            int n; BcSnap *snap = bc_snapshot(ctx, &n);
            bc_expr_arm(ctx, expr->as.null_coalesce.right);
            int cn; BcSnap *cur = bc_snapshot(ctx, &cn);
            bc_merge_two(ctx, snap, n, cur, cn);
            free(snap); free(cur);
            break;
        }
        case EXPR_MATCH: {
            // Arms are alternatives, not a sequence: analyse each from the
            // entry state and merge the survivors, like switch cases. The
            // entry state stays in the merge (a match need not be exhaustive).
            bc_expr_use(ctx, expr->as.match_expr.scrutinee);
            int n; BcSnap *snap = bc_snapshot(ctx, &n);
            BcSnap *acc = NULL; int accn = 0; int have_acc = 0;
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                bc_restore(ctx, snap, n);
                MatchArm *arm = &expr->as.match_expr.arms[i];
                if (arm->guard) bc_expr_use(ctx, arm->guard);
                int kind = arm->body ? bc_expr_arm(ctx, arm->body) : BC_EXIT_NONE;
                if (kind != BC_EXIT_NONE) continue;  // arm diverged (panic/exit)
                int cn; BcSnap *cs = bc_snapshot(ctx, &cn);
                if (!have_acc) { acc = cs; accn = cn; have_acc = 1; }
                else { bc_merge_two(ctx, acc, accn, cs, cn);
                       free(cs); free(acc); acc = bc_snapshot(ctx, &accn); }
            }
            bc_restore(ctx, snap, n);
            if (have_acc) { bc_merge_two(ctx, acc, accn, snap, n); free(acc); }
            free(snap);
            break;
        }
        default:
            break;  // literals, etc.
    }
}

// Mark a resource as having escaped (returned / stored outward).
static void bc_mark_escape(BorrowContext *ctx, Expr *value) {
    if (!value || value->type != EXPR_IDENT) return;
    BcBinding *b = bc_lookup(ctx, value->as.ident.name);
    if (!b || b->resource_id < 0) return;
    BcResource *r = bc_res(ctx, b->resource_id);
    if (r) r->escaped = 1;
}

// Bind `name` to whatever ownership `value` implies.
static void bc_bind_value(BorrowContext *ctx, const char *name, Expr *value) {
    const char *kind = bc_value_acquire_kind(ctx, value);
    if (kind) {
        // Fresh acquisition (builtin or factory function): walk the call's
        // argument expressions for uses (the call itself is the acquisition,
        // not a use), then apply any consumption the callee's summary implies.
        if (value->type == EXPR_CALL) {
            for (int i = 0; i < value->as.call.num_args; i++)
                bc_expr_use(ctx, value->as.call.args[i]);
            bc_apply_call_consumption(ctx, value);
        }
        int id = bc_new_resource(ctx, kind, name, value ? value->line : 0);
        bc_define(ctx, name, id);
        return;
    }
    // `let q = p;` where p owns a resource -> alias (default) or move (strict).
    if (value && value->type == EXPR_IDENT) {
        bc_check_use(ctx, value->as.ident.name, value->line);
        BcBinding *src = bc_lookup(ctx, value->as.ident.name);
        if (src && src->resource_id >= 0 && !src->moved) {
            bc_define(ctx, name, src->resource_id);
            if (ctx->strict) src->moved = 1;
            return;
        }
    }
    // Non-owning initializer: walk it for uses, bind as untracked.
    bc_expr_use(ctx, value);
    bc_define(ctx, name, -1);
}

// While summarising a function, record what a `return` yields. If every path
// returns a fresh owned resource of one kind, the function acts as an
// acquisition ("factory") at its call sites. Returning a parameter is
// excluded — the caller owns that value already.
static void bc_note_summary_return(BorrowContext *ctx, Expr *v) {
    const char *k = NULL;
    if (v) {
        k = bc_value_acquire_kind(ctx, v);
        if (!k && v->type == EXPR_IDENT && v->as.ident.name) {
            BcBinding *b = bc_lookup(ctx, v->as.ident.name);
            BcResource *r = b && !b->moved ? bc_res(ctx, b->resource_id) : NULL;
            if (r && r->state == BC_OWNED && strcmp(r->kind, "param") != 0)
                k = r->kind;
        }
    }
    if (k && (ctx->sum_ret_state == 0 ||
              (ctx->sum_ret_state == 1 &&
               strcmp(ctx->sum_ret_kind, k) == 0))) {
        ctx->sum_ret_kind = k;
        ctx->sum_ret_state = 1;
    } else {
        ctx->sum_ret_state = 2;
    }
}

// ========== STATEMENT WALK ==========

static void bc_block(BorrowContext *ctx, Stmt *block) {
    if (!block) return;
    bc_push_scope(ctx);
    if (block->type == STMT_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            bc_stmt(ctx, block->as.block.statements[i]);
            if (ctx->diverged) {
                // Remaining statements are unreachable on this path.
                for (i++; i < block->as.block.count; i++) { /* skip */ }
                break;
            }
        }
    } else {
        bc_stmt(ctx, block);
    }
    bc_pop_scope(ctx);
}

// Analyse `body` as an independent continuation. Returns how it exited
// (BC_EXIT_NONE if it fell through, otherwise BC_EXIT_BREAK/CONTINUE/RETURN).
static int bc_branch(BorrowContext *ctx, Stmt *body) {
    int saved_div = ctx->diverged;
    int saved_kind = ctx->exit_kind;
    ctx->diverged = 0;
    ctx->exit_kind = BC_EXIT_NONE;
    bc_block(ctx, body);
    int kind = ctx->diverged ? ctx->exit_kind : BC_EXIT_NONE;
    ctx->diverged = saved_div;
    ctx->exit_kind = saved_kind;
    return kind;
}

static void bc_stmt(BorrowContext *ctx, Stmt *stmt) {
    if (!stmt) return;
    if (stmt->line > 0) ctx->cur_line = stmt->line;
    switch (stmt->type) {
        case STMT_LET:
            bc_bind_value(ctx, stmt->as.let.name, stmt->as.let.value);
            break;
        case STMT_CONST:
            bc_bind_value(ctx, stmt->as.const_stmt.name, stmt->as.const_stmt.value);
            break;
        case STMT_EXPR: {
            Expr *e = stmt->as.expr;
            // Simple reassignment `x = value` at statement level.
            if (e && e->type == EXPR_ASSIGN) {
                BcBinding *b = bc_lookup(ctx, e->as.assign.name);
                Expr *v = e->as.assign.value;
                if (b) {
                    int line = e->line > 0 ? e->line : ctx->cur_line;
                    const char *kind = bc_value_acquire_kind(ctx, v);
                    // A value derived from the binding itself (`p = p + 8;`)
                    // keeps referring to the same resource: no drop, no rebind.
                    int self_ref = !kind && v &&
                                   bc_expr_mentions(v, e->as.assign.name);
                    // Strict: overwriting the last live reference to a
                    // still-owned resource loses it for good.
                    if (ctx->strict && !self_ref &&
                        b->resource_id >= 0 && !b->moved) {
                        BcResource *old = bc_res(ctx, b->resource_id);
                        if (old && old->state == BC_OWNED && !old->escaped &&
                            !old->deferred_free &&
                            bc_alias_count(ctx, b->resource_id) == 1) {
                            bc_warn(ctx, line,
                                    "reassigning '%s' drops the last reference "
                                    "to the %s acquired at line %d (possible "
                                    "leak)", e->as.assign.name, old->kind,
                                    old->acquire_line);
                        }
                    }
                    if (kind) {
                        // Re-acquire: revive binding to a fresh OWNED resource.
                        if (v->type == EXPR_CALL) {
                            for (int i = 0; i < v->as.call.num_args; i++)
                                bc_expr_use(ctx, v->as.call.args[i]);
                            bc_apply_call_consumption(ctx, v);
                        }
                        int id = bc_new_resource(ctx, kind, e->as.assign.name,
                                                 line);
                        b->resource_id = id;
                        b->moved = 0;
                        break;
                    }
                    if (self_ref) {
                        bc_expr_use(ctx, v);
                        break;
                    }
                    // `x = y` where y owns a resource: x now aliases (or, in
                    // strict mode, takes over) that resource.
                    if (v && v->type == EXPR_IDENT) {
                        bc_check_use(ctx, v->as.ident.name, v->line);
                        BcBinding *src = bc_lookup(ctx, v->as.ident.name);
                        if (src && src->resource_id >= 0 && !src->moved) {
                            b->resource_id = src->resource_id;
                            b->moved = 0;
                            if (ctx->strict && src != b) src->moved = 1;
                            break;
                        }
                    } else {
                        bc_expr_use(ctx, v);
                    }
                    // The binding now holds an untracked value; detach it from
                    // the old resource so later uses of the name are not
                    // misattributed to it.
                    b->resource_id = -1;
                    b->moved = 0;
                    break;
                }
            }
            // Strict: an acquisition whose result is discarded at statement
            // level can never be released.
            if (ctx->strict && e && e->type == EXPR_CALL) {
                const char *k = bc_value_acquire_kind(ctx, e);
                if (k && e->as.call.func && e->as.call.func->type == EXPR_IDENT) {
                    bc_warn(ctx, e->line,
                            "the %s returned by '%s' is discarded without being "
                            "released (possible leak)", k,
                            e->as.call.func->as.ident.name);
                }
            }
            bc_expr_use(ctx, e);
            break;
        }
        case STMT_RETURN:
            if (ctx->summary_mode)
                bc_note_summary_return(ctx, stmt->as.return_stmt.value);
            if (stmt->as.return_stmt.value) {
                bc_mark_escape(ctx, stmt->as.return_stmt.value);
                bc_expr_use(ctx, stmt->as.return_stmt.value);
            }
            ctx->diverged = 1;
            ctx->exit_kind = BC_EXIT_RETURN;
            break;
        case STMT_THROW:
            bc_expr_use(ctx, stmt->as.throw_stmt.value);
            ctx->diverged = 1;
            ctx->exit_kind = BC_EXIT_RETURN;
            break;
        case STMT_BREAK:
            ctx->diverged = 1;
            ctx->exit_kind = BC_EXIT_BREAK;
            break;
        case STMT_CONTINUE:
            ctx->diverged = 1;
            ctx->exit_kind = BC_EXIT_CONTINUE;
            break;
        case STMT_BLOCK:
            bc_block(ctx, stmt);
            break;
        case STMT_IF: {
            bc_expr_use(ctx, stmt->as.if_stmt.condition);
            int n; BcSnap *snap = bc_snapshot(ctx, &n);

            int then_div = bc_branch(ctx, stmt->as.if_stmt.then_branch);
            int tn; BcSnap *then_state = bc_snapshot(ctx, &tn);

            bc_restore(ctx, snap, n);
            int else_div = 0, en = 0; BcSnap *else_state = NULL;
            if (stmt->as.if_stmt.else_branch) {
                else_div = bc_branch(ctx, stmt->as.if_stmt.else_branch);
                else_state = bc_snapshot(ctx, &en);
            } else {
                // No else: the "else" path keeps the pre-if state.
                else_state = bc_snapshot(ctx, &en);
            }

            // Merge surviving (non-diverging) arms.
            BcSnap *a = then_div ? NULL : then_state;
            BcSnap *b = else_div ? NULL : else_state;
            bc_merge_two(ctx, a, tn, b, en);
            if (then_div && else_div) {
                // Both arms leave; the join is unreachable. Report the exit as
                // a function-leaving return only if both arms returned.
                ctx->diverged = 1;
                ctx->exit_kind = (then_div == BC_EXIT_RETURN || else_div == BC_EXIT_RETURN)
                                 ? BC_EXIT_RETURN : BC_EXIT_BREAK;
            }

            free(snap); free(then_state); free(else_state);
            break;
        }
        case STMT_WHILE: {
            bc_expr_use(ctx, stmt->as.while_stmt.condition);
            int n; BcSnap *entry = bc_snapshot(ctx, &n);
            bc_branch(ctx, stmt->as.while_stmt.body);
            // free-inside-loop: a resource OWNED at entry but FREED at body exit
            // would double-free on the next iteration.
            BcResource *res = bc_resources(ctx);
            for (int i = 0; i < n && i < ctx->num_resources; i++) {
                if (entry[i].state == BC_OWNED &&
                    (res[i].state == BC_FREED)) {
                    bc_warn(ctx, res[i].acquire_line,
                            "'%s' is freed inside a loop; a later iteration may "
                            "double free it", res[i].origin);
                }
            }
            // Body may run zero times: merge entry with post-body conservatively.
            int bn; BcSnap *post = bc_snapshot(ctx, &bn);
            bc_merge_two(ctx, entry, n, post, bn);
            free(entry); free(post);
            break;
        }
        case STMT_LOOP: {
            int n; BcSnap *entry = bc_snapshot(ctx, &n);
            bc_branch(ctx, stmt->as.loop_stmt.body);
            BcResource *res = bc_resources(ctx);
            for (int i = 0; i < n && i < ctx->num_resources; i++) {
                if (entry[i].state == BC_OWNED && res[i].state == BC_FREED) {
                    bc_warn(ctx, res[i].acquire_line,
                            "'%s' is freed inside a loop; a later iteration may "
                            "double free it", res[i].origin);
                }
            }
            int bn; BcSnap *post = bc_snapshot(ctx, &bn);
            bc_merge_two(ctx, entry, n, post, bn);
            free(entry); free(post);
            break;
        }
        case STMT_FOR: {
            bc_push_scope(ctx);
            if (stmt->as.for_loop.initializer) bc_stmt(ctx, stmt->as.for_loop.initializer);
            if (stmt->as.for_loop.condition) bc_expr_use(ctx, stmt->as.for_loop.condition);
            if (stmt->as.for_loop.increment) bc_expr_use(ctx, stmt->as.for_loop.increment);
            int n; BcSnap *entry = bc_snapshot(ctx, &n);
            bc_branch(ctx, stmt->as.for_loop.body);
            BcResource *res = bc_resources(ctx);
            for (int i = 0; i < n && i < ctx->num_resources; i++) {
                if (entry[i].state == BC_OWNED && res[i].state == BC_FREED) {
                    bc_warn(ctx, res[i].acquire_line,
                            "'%s' is freed inside a loop; a later iteration may "
                            "double free it", res[i].origin);
                }
            }
            int bn; BcSnap *post = bc_snapshot(ctx, &bn);
            bc_merge_two(ctx, entry, n, post, bn);
            free(entry); free(post);
            bc_pop_scope(ctx);
            break;
        }
        case STMT_FOR_IN: {
            bc_push_scope(ctx);
            bc_expr_use(ctx, stmt->as.for_in.iterable);
            int n; BcSnap *entry = bc_snapshot(ctx, &n);
            bc_branch(ctx, stmt->as.for_in.body);
            BcResource *res = bc_resources(ctx);
            for (int i = 0; i < n && i < ctx->num_resources; i++) {
                if (entry[i].state == BC_OWNED && res[i].state == BC_FREED) {
                    bc_warn(ctx, res[i].acquire_line,
                            "'%s' is freed inside a loop; a later iteration may "
                            "double free it", res[i].origin);
                }
            }
            int bn; BcSnap *post = bc_snapshot(ctx, &bn);
            bc_merge_two(ctx, entry, n, post, bn);
            free(entry); free(post);
            bc_pop_scope(ctx);
            break;
        }
        case STMT_SWITCH: {
            bc_expr_use(ctx, stmt->as.switch_stmt.expr);
            int n; BcSnap *snap = bc_snapshot(ctx, &n);
            // Analyse each case from the entry state, merge survivors.
            BcSnap *acc = NULL; int accn = 0; int have_acc = 0;
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                bc_restore(ctx, snap, n);
                if (stmt->as.switch_stmt.case_values[i])
                    bc_expr_use(ctx, stmt->as.switch_stmt.case_values[i]);
                int kind = bc_branch(ctx, stmt->as.switch_stmt.case_bodies[i]);
                // `break` (and fall-through) is the normal case exit: its state
                // contributes to the post-switch merge. Only a case that leaves
                // the function (return/throw) or the enclosing loop (continue)
                // is excluded.
                if (kind == BC_EXIT_RETURN || kind == BC_EXIT_CONTINUE) continue;
                int cn; BcSnap *cs = bc_snapshot(ctx, &cn);
                if (!have_acc) { acc = cs; accn = cn; have_acc = 1; }
                else { bc_merge_two(ctx, acc, accn, cs, cn);
                       free(cs); free(acc); acc = bc_snapshot(ctx, &accn); }
            }
            bc_restore(ctx, snap, n);
            if (have_acc) { bc_merge_two(ctx, acc, accn, snap, n); free(acc); }
            free(snap);
            break;
        }
        case STMT_TRY: {
            // try body may abort at any point; merge try-exit, catch-exit and
            // the pre-try state conservatively.
            int n; BcSnap *snap = bc_snapshot(ctx, &n);
            int try_div = bc_branch(ctx, stmt->as.try_stmt.try_block);
            int tn; BcSnap *try_state = bc_snapshot(ctx, &tn);

            bc_restore(ctx, snap, n);
            int catch_div = 0, cn = 0; BcSnap *catch_state = NULL;
            if (stmt->as.try_stmt.catch_block) {
                bc_push_scope(ctx);
                catch_div = bc_branch(ctx, stmt->as.try_stmt.catch_block);
                bc_pop_scope(ctx);
                catch_state = bc_snapshot(ctx, &cn);
            }
            BcSnap *a = try_div ? NULL : try_state;
            BcSnap *b = (stmt->as.try_stmt.catch_block && !catch_div) ? catch_state
                                                                      : snap;
            int bn = (stmt->as.try_stmt.catch_block && !catch_div) ? cn : n;
            bc_merge_two(ctx, a, tn, b, bn);

            if (stmt->as.try_stmt.finally_block)
                bc_branch(ctx, stmt->as.try_stmt.finally_block);

            free(snap); free(try_state); free(catch_state);
            break;
        }
        case STMT_DEFER: {
            // `defer free(x)` schedules a release for scope exit.
            Expr *call = stmt->as.defer_stmt.call;
            const char *rname = NULL, *rop = NULL;
            if (call && bc_release_target(call, &rname, &rop) && rname) {
                BcBinding *b = bc_lookup(ctx, rname);
                if (b) {
                    BcResource *r = bc_res(ctx, b->resource_id);
                    if (r) {
                        if (r->deferred_free)
                            bc_warn(ctx, stmt->line,
                                    "'%s' already has a deferred free scheduled",
                                    rname);
                        r->deferred_free = 1;
                    }
                }
            } else if (call) {
                bc_expr_use(ctx, call);
            }
            break;
        }
        case STMT_DEFINE_OBJECT:
            // Analyse method default bodies as independent functions.
            for (int i = 0; i < stmt->as.define_object.num_methods; i++) {
                Expr *m = stmt->as.define_object.method_defaults[i];
                if (m && m->type == EXPR_FUNCTION) bc_analyze_function(ctx, m);
            }
            break;
        case STMT_EXPORT:
            if (stmt->as.export_stmt.declaration)
                bc_stmt(ctx, stmt->as.export_stmt.declaration);
            break;
        default:
            break;  // enum/import/extern/type-alias: nothing to track
    }
}

// Analyse a function body in its own ownership universe.
static void bc_analyze_function(BorrowContext *ctx, Expr *fn) {
    if (!fn || fn->type != EXPR_FUNCTION || !fn->as.function.body) return;
    int saved_div = ctx->diverged;
    int saved_kind = ctx->exit_kind;
    // Shield any in-progress summary's return tracking from this nested body.
    const char *saved_rk = ctx->sum_ret_kind;
    int saved_rs = ctx->sum_ret_state;
    ctx->diverged = 0;
    ctx->exit_kind = BC_EXIT_NONE;
    int n; BcSnap *snap = bc_snapshot(ctx, &n);
    bc_push_scope(ctx);
    // Parameters are borrows from the caller; bind them as untracked so uses
    // inside the body don't trip ownership rules.
    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (fn->as.function.param_names[i])
            bc_define(ctx, fn->as.function.param_names[i], -1);
    }
    bc_block(ctx, fn->as.function.body);
    bc_pop_scope(ctx);
    // This body runs at an unknown time — perhaps never, perhaps repeatedly —
    // so its effect on *captured* outer resources must not bleed into the
    // sequential analysis of the code that merely defines it (a `destroy`
    // closure releasing a captured buffer must not make a sibling closure's
    // use of it a "use after free"). Restore the outer resources' state, with
    // two exceptions: escapes stay (a closure returning or storing a captured
    // resource still counts), and a resource the body released is marked
    // escaped — its lifecycle now belongs to the closure, so it is not a leak
    // when the defining scope ends. Resources created inside the body (locals)
    // sit beyond the snapshot and keep their final state.
    if (snap) {
        BcResource *res = bc_resources(ctx);
        for (int i = 0; i < n && i < ctx->num_resources; i++) {
            int released_here = bc_state_dead(res[i].state) &&
                                !bc_state_dead(snap[i].state);
            res[i].escaped = res[i].escaped || snap[i].escaped || released_here;
            res[i].state = snap[i].state;
            res[i].deferred_free = snap[i].deferred_free;
        }
        free(snap);
    }
    ctx->diverged = saved_div;
    ctx->exit_kind = saved_kind;
    ctx->sum_ret_kind = saved_rk;
    ctx->sum_ret_state = saved_rs;
}

// ========== INTERPROCEDURAL SUMMARIES ==========

static BcFnSummary *bc_summaries(BorrowContext *ctx) {
    return (BcFnSummary *)ctx->summaries;
}

static const BcFnSummary *bc_find_summary(BorrowContext *ctx, const char *name) {
    if (!name) return NULL;
    BcFnSummary *s = bc_summaries(ctx);
    for (int i = 0; i < ctx->num_summaries; i++)
        if (s[i].name && strcmp(s[i].name, name) == 0) return &s[i];
    return NULL;
}

// At a call site, transition resources passed to parameters the callee releases.
// Also active while building summaries: a wrapper that forwards its parameter
// into a consuming callee is then summarised as consuming too, and the fixpoint
// in bc_register_summaries propagates chains of any depth.
static void bc_apply_call_consumption(BorrowContext *ctx, Expr *call) {
    if (!call || call->type != EXPR_CALL) return;
    Expr *f = call->as.call.func;
    if (!f || f->type != EXPR_IDENT || !f->as.ident.name) return;
    const BcFnSummary *sum = bc_find_summary(ctx, f->as.ident.name);
    if (!sum) return;
    // Named arguments would break positional mapping: stay conservative.
    if (call->as.call.arg_names) {
        for (int i = 0; i < call->as.call.num_args; i++)
            if (call->as.call.arg_names[i]) return;
    }
    int n = call->as.call.num_args;
    if (n > sum->num_params) n = sum->num_params;
    for (int i = 0; i < n; i++) {
        if (sum->consumed[i] == BC_PARAM_BORROW) continue;
        Expr *a = call->as.call.args[i];
        if (!a || a->type != EXPR_IDENT || !a->as.ident.name) continue;
        BcBinding *b = bc_lookup(ctx, a->as.ident.name);
        if (!b || b->moved) continue;
        BcResource *r = bc_res(ctx, b->resource_id);
        if (!r || r->state != BC_OWNED) continue;
        r->state = (sum->consumed[i] == BC_PARAM_CONSUMED) ? BC_FREED
                                                           : BC_MAYBE_FREED;
    }
}

// Walk one function body with its parameters modelled as resources, then read
// back how each was left to derive a consumption summary, plus whether every
// return path yields a fresh owned resource (a "factory"). A summary is kept
// only if it says something (consumes a parameter or returns a resource).
// Returns 1 if the recorded summary is new or changed since the last pass —
// this drives the fixpoint loop in bc_register_summaries.
static int bc_register_summary(BorrowContext *ctx, const char *name, Expr *fn) {
    if (!name || !fn || fn->type != EXPR_FUNCTION || !fn->as.function.body)
        return 0;
    int np = fn->as.function.num_params;

    int base = ctx->num_resources;
    int saved_strict = ctx->strict;
    int saved_div = ctx->diverged, saved_kind = ctx->exit_kind;
    const char *saved_rk = ctx->sum_ret_kind;
    int saved_rs = ctx->sum_ret_state;
    ctx->summary_mode = 1;
    ctx->strict = 0;  // move/leak rules are irrelevant to consumption
    ctx->diverged = 0; ctx->exit_kind = BC_EXIT_NONE;
    ctx->sum_ret_kind = NULL; ctx->sum_ret_state = 0;

    bc_push_scope(ctx);
    int *ids = np > 0 ? calloc((size_t)np, sizeof(int)) : NULL;
    for (int i = 0; i < np; i++) {
        const char *pn = fn->as.function.param_names[i];
        if (!pn) { if (ids) ids[i] = -1; continue; }
        int id = bc_new_resource(ctx, "param", pn, fn->line);
        if (ids) ids[i] = id;
        bc_define(ctx, pn, id);
    }
    bc_block(ctx, fn->as.function.body);

    unsigned char *consumed = np > 0 ? calloc((size_t)np, sizeof(unsigned char))
                                     : NULL;
    int any = 0;
    for (int i = 0; consumed && ids && i < np; i++) {
        BcResource *r = bc_res(ctx, ids[i]);
        if (!r) continue;
        if (r->state == BC_FREED)       { consumed[i] = BC_PARAM_CONSUMED; any = 1; }
        else if (r->state == BC_MAYBE_FREED) { consumed[i] = BC_PARAM_MAYBE; any = 1; }
    }
    // Factory: every path must end in a return (ctx->diverged) that yields a
    // fresh owned resource of one consistent kind. Copy the kind now — it may
    // point into a temporary resource about to be discarded.
    char *ret_kind = NULL;
    if (ctx->sum_ret_state == 1 && ctx->diverged && ctx->sum_ret_kind &&
        strcmp(ctx->sum_ret_kind, "param") != 0)
        ret_kind = strdup(ctx->sum_ret_kind);

    bc_pop_scope(ctx);
    free(ids);

    // Discard the throwaway parameter/local resources.
    BcResource *res = bc_resources(ctx);
    for (int i = base; i < ctx->num_resources; i++) {
        free(res[i].kind);
        free(res[i].origin);
    }
    ctx->num_resources = base;

    ctx->summary_mode = 0;
    ctx->strict = saved_strict;
    ctx->diverged = saved_div; ctx->exit_kind = saved_kind;
    ctx->sum_ret_kind = saved_rk; ctx->sum_ret_state = saved_rs;

    if (!any && !ret_kind) { free(consumed); return 0; }

    // Later fixpoint pass: update the existing summary in place if it changed.
    BcFnSummary *existing = (BcFnSummary *)bc_find_summary(ctx, name);
    if (existing) {
        int changed = existing->num_params != np ||
                      (np > 0 && existing->consumed && consumed &&
                       memcmp(existing->consumed, consumed, (size_t)np) != 0) ||
                      (existing->returns_kind == NULL) != (ret_kind == NULL) ||
                      (ret_kind && existing->returns_kind &&
                       strcmp(ret_kind, existing->returns_kind) != 0);
        if (changed) {
            free(existing->consumed);
            free(existing->returns_kind);
            existing->num_params = np;
            existing->consumed = consumed;
            existing->returns_kind = ret_kind;
        } else {
            free(consumed);
            free(ret_kind);
        }
        return changed;
    }

    if (ctx->num_summaries >= ctx->cap_summaries) {
        int ncap = ctx->cap_summaries ? ctx->cap_summaries * 2 : 8;
        BcFnSummary *ns = realloc(ctx->summaries, (size_t)ncap * sizeof(BcFnSummary));
        if (!ns) { free(consumed); free(ret_kind); return 0; }
        ctx->summaries = ns;
        ctx->cap_summaries = ncap;
    }
    BcFnSummary *s = &bc_summaries(ctx)[ctx->num_summaries++];
    s->name = strdup(name);
    s->num_params = np;
    s->consumed = consumed;
    s->returns_kind = ret_kind;
    return 1;
}

// Pre-pass: register summaries for every top-level named function so that call
// sites can be checked regardless of declaration order. Iterated to a fixpoint
// so knowledge propagates through wrappers: a function that forwards its
// parameter into a consuming callee, or returns another factory's fresh
// resource, is only seen once its callee's summary exists. Summaries grow
// monotonically, so the loop converges; the pass cap bounds pathological input.
static void bc_register_summaries(BorrowContext *ctx, Stmt **stmts, int count) {
    for (int pass = 0; pass < HML_BORROW_SUMMARY_MAX_PASSES; pass++) {
        int changed = 0;
        for (int i = 0; i < count; i++) {
            Stmt *s = stmts[i];
            if (!s) continue;
            if (s->type == STMT_EXPORT && s->as.export_stmt.declaration)
                s = s->as.export_stmt.declaration;
            if (!s) continue;
            if (s->type == STMT_LET && s->as.let.value &&
                s->as.let.value->type == EXPR_FUNCTION)
                changed |= bc_register_summary(ctx, s->as.let.name, s->as.let.value);
            else if (s->type == STMT_CONST && s->as.const_stmt.value &&
                     s->as.const_stmt.value->type == EXPR_FUNCTION)
                changed |= bc_register_summary(ctx, s->as.const_stmt.name,
                                               s->as.const_stmt.value);
        }
        if (!changed) break;
    }
}

// ========== PUBLIC API ==========

BorrowContext *borrow_check_new(const char *filename) {
    BorrowContext *ctx = calloc(1, sizeof(BorrowContext));
    if (!ctx) return NULL;
    ctx->filename = filename;
    return ctx;
}

void borrow_check_enable_collection(BorrowContext *ctx, const char *source) {
    if (!ctx) return;
    ctx->collect = 1;
    ctx->source = source;
}

int borrow_check_program(BorrowContext *ctx, Stmt **stmts, int stmt_count) {
    if (!ctx || !stmts) return 0;
    bc_register_summaries(ctx, stmts, stmt_count);  // interprocedural pre-pass
    bc_push_scope(ctx);  // global scope
    for (int i = 0; i < stmt_count; i++) {
        ctx->diverged = 0;  // each top-level statement starts a fresh path
        ctx->exit_kind = BC_EXIT_NONE;
        bc_stmt(ctx, stmts[i]);
    }
    bc_pop_scope(ctx);
    return ctx->errors_are_fatal ? ctx->error_count : 0;
}

void borrow_check_free(BorrowContext *ctx) {
    if (!ctx) return;
    // Free any scopes still open (defensive).
    while (ctx->scope) {
        BcScope *s = (BcScope *)ctx->scope;
        BcBinding *b = s->bindings;
        while (b) { BcBinding *n = b->next; free(b->name); free(b); b = n; }
        ctx->scope = s->parent;
        free(s);
    }
    BcResource *res = bc_resources(ctx);
    for (int i = 0; i < ctx->num_resources; i++) {
        free(res[i].kind);
        free(res[i].origin);
    }
    free(ctx->resources);
    BcFnSummary *sums = bc_summaries(ctx);
    for (int i = 0; i < ctx->num_summaries; i++) {
        free(sums[i].name);
        free(sums[i].consumed);
        free(sums[i].returns_kind);
    }
    free(ctx->summaries);
    BorrowDiag *d = ctx->diags;
    while (d) { BorrowDiag *n = d->next; free(d->message); free(d); d = n; }
    free(ctx);
}

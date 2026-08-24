/*
 * Hemlock Compiler - Specialization Coverage Report
 *
 * Builds the report served by `hemlockc --coverage` and
 * `hemlock check --coverage`: for every numeric variable site, whether the
 * unboxing optimization gives it a native C type or leaves it boxed as an
 * HmlValue, and (for boxed sites) a stable HC21xx reason code plus a hint.
 *
 * The unboxed/boxed decision is ground truth: this pass runs the same
 * analysis codegen runs (type_check_analyze_block_for_unboxing per
 * function body, type_check_analyze_for_loop per loop) and reads the
 * resulting unboxable marks. Only the *reason* for a boxed site is
 * derived here, by replaying the analyzer's checks in the same order —
 * see type_unboxing.c, which stays the single source of truth for the
 * decisions themselves.
 */

#include "type_check_internal.h"
#include "compiler/type_coverage.h"

// ========== REASON CODE TABLES ==========

const char *coverage_reason_code(CoverageReason reason) {
    static const char *codes[] = {
        "",        // COVERAGE_OK
        "HC2101",  // COVERAGE_OTHER
        "HC2102",  // COVERAGE_UNTYPED
        "HC2103",  // COVERAGE_TOP_LEVEL
        "HC2104",  // COVERAGE_PARAMETER
        "HC2105",  // COVERAGE_ESCAPES
        "HC2106",  // COVERAGE_DYNAMIC_INIT
        "HC2107",  // COVERAGE_MIXED_ASSIGN
        "HC2108",  // COVERAGE_COMPLEX_LOOP
        "HC2109",  // COVERAGE_RUNE
        "HC2110",  // COVERAGE_UNSUPPORTED_TYPE
    };
    if (reason < 0 || reason > COVERAGE_UNSUPPORTED_TYPE) return "";
    return codes[reason];
}

const char *coverage_reason_text(CoverageReason reason) {
    switch (reason) {
        case COVERAGE_OK:
            return "unboxed";
        case COVERAGE_OTHER:
            return "not specialized by the current analysis";
        case COVERAGE_UNTYPED:
            return "no static numeric type could be inferred";
        case COVERAGE_TOP_LEVEL:
            return "top-level variables stay boxed";
        case COVERAGE_PARAMETER:
            return "parameters are always boxed";
        case COVERAGE_ESCAPES:
            return "escapes its scope (captured, stored, returned, or indexed)";
        case COVERAGE_DYNAMIC_INIT:
            return "initializer is not statically analyzable";
        case COVERAGE_MIXED_ASSIGN:
            return "a later assignment is not statically numeric";
        case COVERAGE_COMPLEX_LOOP:
            return "loop shape too complex to specialize";
        case COVERAGE_RUNE:
            return "runes keep their type tag";
        case COVERAGE_UNSUPPORTED_TYPE:
            return "inferred type has no unboxed form";
    }
    return "";
}

const char *coverage_reason_hint(CoverageReason reason) {
    switch (reason) {
        case COVERAGE_UNTYPED:
            return "add a numeric type annotation (e.g. `: i32`)";
        case COVERAGE_TOP_LEVEL:
            return "move hot code into a function";
        case COVERAGE_PARAMETER:
            return "copy the parameter into an annotated local at function entry";
        case COVERAGE_ESCAPES:
            return "keep hot arithmetic in a local that is not captured, "
                   "stored, or returned";
        case COVERAGE_MIXED_ASSIGN:
            return "keep assignments to the variable numeric, or annotate it";
        default:
            return NULL;
    }
}

const char *coverage_site_kind_name(CoverageSiteKind kind) {
    switch (kind) {
        case COVERAGE_SITE_TYPED_VAR:    return "typed_var";
        case COVERAGE_SITE_INFERRED_VAR: return "inferred_var";
        case COVERAGE_SITE_LOOP_COUNTER: return "loop_counter";
        case COVERAGE_SITE_ACCUMULATOR:  return "accumulator";
        case COVERAGE_SITE_PARAMETER:    return "parameter";
        case COVERAGE_SITE_TOP_LEVEL:    return "top_level";
    }
    return "unknown";
}

// ========== SITE RECORDING ==========

typedef struct {
    TypeCheckContext *ctx;
    CoverageReport *report;
    CoverageSite *tail;
} CoverageWalk;

static void coverage_record(CoverageWalk *w, const char *name, int line,
                            int column, CoverageSiteKind kind, int unboxed,
                            CheckedTypeKind native_type, CoverageReason reason,
                            const char *message) {
    CoverageSite *site = calloc(1, sizeof(CoverageSite));
    if (!site) return;
    site->name = strdup(name ? name : "");
    site->line = line;
    site->column = column;
    site->kind = kind;
    site->unboxed = unboxed;
    site->native_type = native_type;
    site->reason = reason;
    site->message = message ? strdup(message) : NULL;
    if (!site->name) {
        free(site->message);
        free(site);
        return;
    }

    if (w->tail) {
        w->tail->next = site;
    } else {
        w->report->sites = site;
    }
    w->tail = site;
    w->report->num_sites++;
    if (unboxed) w->report->num_unboxed++;
}

// A let is a coverage site when the unboxing analysis would consider it:
// a primitive type annotation, or an untyped declaration whose initializer
// is structurally numeric (literals, idents, arithmetic).
static int coverage_let_is_candidate(Stmt *stmt) {
    if (stmt->type != STMT_LET) return 0;
    if (stmt->as.let.type_annotation) {
        return type_check_can_unbox_annotation(stmt->as.let.type_annotation)
               != CHECKED_UNKNOWN;
    }
    return stmt->as.let.value && is_unboxable_expr(stmt->as.let.value);
}

// Does the variable escape in the statements following its declaration?
// Mirrors the check in type_check_analyze_typed_let / _inferred_let.
static int coverage_escapes_after(Stmt *block, int stmt_index,
                                  const char *name) {
    if (!block || block->type != STMT_BLOCK) return 0;
    for (int i = stmt_index + 1; i < block->as.block.count; i++) {
        if (variable_escapes_in_stmt_internal(block->as.block.statements[i],
                                              name)) {
            return 1;
        }
    }
    return 0;
}

static int coverage_mixed_assign_after(Stmt *block, int stmt_index,
                                       const char *name) {
    if (!block || block->type != STMT_BLOCK) return 0;
    for (int i = stmt_index + 1; i < block->as.block.count; i++) {
        if (has_incompatible_assignment_stmt(block->as.block.statements[i],
                                             name)) {
            return 1;
        }
    }
    return 0;
}

// ========== SITE CLASSIFICATION ==========
//
// Classification runs after the real analysis: a mark on the variable
// means codegen unboxes it; an unmarked candidate is boxed and the checks
// below (same order as the analyzer's) explain why.

static CoverageSiteKind coverage_kind_from_mark(TypeCheckContext *ctx,
                                                const char *name,
                                                CoverageSiteKind fallback) {
    if (type_check_is_loop_counter(ctx, name)) return COVERAGE_SITE_LOOP_COUNTER;
    if (type_check_is_accumulator(ctx, name)) return COVERAGE_SITE_ACCUMULATOR;
    return fallback;
}

static void coverage_classify_typed_let(CoverageWalk *w, Stmt *stmt,
                                        Stmt *block, int stmt_index) {
    const char *name = stmt->as.let.name;
    CheckedTypeKind annot =
        type_check_can_unbox_annotation(stmt->as.let.type_annotation);

    // Codegen's typed unboxing path requires an initializer even when the
    // analysis marked the variable (see codegen_stmt.c STMT_LET).
    if (!stmt->as.let.value) {
        coverage_record(w, name, stmt->line, stmt->column,
                        COVERAGE_SITE_TYPED_VAR, 0, annot,
                        COVERAGE_DYNAMIC_INIT, "declared without an initializer");
        return;
    }

    CheckedTypeKind marked = type_check_get_unboxable(w->ctx, name);
    if (marked != CHECKED_UNKNOWN) {
        coverage_record(w, name, stmt->line, stmt->column,
                        coverage_kind_from_mark(w->ctx, name,
                                                COVERAGE_SITE_TYPED_VAR),
                        1, marked, COVERAGE_OK, NULL);
        return;
    }

    CoverageReason reason;
    if (!is_unboxable_expr(stmt->as.let.value)) {
        reason = COVERAGE_DYNAMIC_INIT;
    } else if (coverage_escapes_after(block, stmt_index, name)) {
        reason = COVERAGE_ESCAPES;
    } else {
        reason = COVERAGE_OTHER;
    }
    coverage_record(w, name, stmt->line, stmt->column,
                    COVERAGE_SITE_TYPED_VAR, 0, annot, reason, NULL);
}

static void coverage_classify_inferred_let(CoverageWalk *w, Stmt *stmt,
                                           Stmt *block, int stmt_index) {
    const char *name = stmt->as.let.name;
    Expr *value = stmt->as.let.value;

    CheckedTypeKind marked = type_check_get_unboxable(w->ctx, name);
    if (marked != CHECKED_UNKNOWN) {
        coverage_record(w, name, stmt->line, stmt->column,
                        coverage_kind_from_mark(w->ctx, name,
                                                COVERAGE_SITE_INFERRED_VAR),
                        1, marked, COVERAGE_OK, NULL);
        return;
    }

    CoverageReason reason;
    CheckedTypeKind inferred = infer_expr_native_type(w->ctx, value);
    if (value->type == EXPR_RUNE) {
        reason = COVERAGE_RUNE;
    } else if (inferred == CHECKED_UNKNOWN) {
        reason = COVERAGE_UNTYPED;
    } else if (inferred != CHECKED_I32 && inferred != CHECKED_I64 &&
               inferred != CHECKED_F64 && inferred != CHECKED_BOOL) {
        // The inference path only unboxes i32/i64/f64/bool
        // (type_check_analyze_inferred_let).
        reason = COVERAGE_UNSUPPORTED_TYPE;
    } else if (coverage_escapes_after(block, stmt_index, name)) {
        reason = COVERAGE_ESCAPES;
    } else if (coverage_mixed_assign_after(block, stmt_index, name)) {
        reason = COVERAGE_MIXED_ASSIGN;
    } else {
        reason = COVERAGE_OTHER;
    }
    coverage_record(w, name, stmt->line, stmt->column,
                    COVERAGE_SITE_INFERRED_VAR, 0, inferred, reason, NULL);
}

static void coverage_classify_for_counter(CoverageWalk *w, Stmt *stmt) {
    Stmt *init = stmt->as.for_loop.initializer;
    if (!init || init->type != STMT_LET) return;

    const char *name = init->as.let.name;
    Expr *init_value = init->as.let.value;
    int line = init->line > 0 ? init->line : stmt->line;

    // Codegen's native-counter loop requires an i32 loop-counter mark
    // (see codegen_stmt.c STMT_FOR).
    CheckedTypeKind marked = type_check_get_unboxable(w->ctx, name);
    if (marked == CHECKED_I32 && type_check_is_loop_counter(w->ctx, name)) {
        coverage_record(w, name, line, init->column,
                        COVERAGE_SITE_LOOP_COUNTER, 1, CHECKED_I32,
                        COVERAGE_OK, NULL);
        return;
    }

    CoverageReason reason = COVERAGE_COMPLEX_LOOP;
    const char *message = NULL;
    if (!init_value || init_value->type != EXPR_NUMBER ||
        init_value->as.number.is_float || init_value->as.number.is_u64) {
        message = "counter initializer is not a constant integer";
    } else if (!is_simple_comparison(stmt->as.for_loop.condition, name)) {
        message = "loop condition is not a simple comparison on the counter";
    } else if (!is_simple_increment(stmt->as.for_loop.increment, name)) {
        message = "loop increment is not a simple constant step";
    } else if (variable_escapes_in_stmt_internal(stmt->as.for_loop.body,
                                                 name)) {
        reason = COVERAGE_ESCAPES;
    } else if (marked == CHECKED_I64) {
        message = "counter starts outside the i32 range";
    } else {
        reason = COVERAGE_OTHER;
    }
    coverage_record(w, name, line, init->column, COVERAGE_SITE_LOOP_COUNTER,
                    0, CHECKED_UNKNOWN, reason, message);
}

// ========== BODY WALK ==========
//
// Mirrors the traversal of type_check_analyze_block_for_unboxing so every
// let the analyzer saw becomes a site, and also descends into `loop`
// bodies (which the analyzer skips — candidates there classify as boxed).

static void coverage_classify_block(CoverageWalk *w, Stmt *block);

static void coverage_classify_stmt(CoverageWalk *w, Stmt *stmt,
                                   Stmt *block, int stmt_index) {
    switch (stmt->type) {
        case STMT_LET:
            if (!coverage_let_is_candidate(stmt)) break;
            if (stmt->as.let.type_annotation) {
                coverage_classify_typed_let(w, stmt, block, stmt_index);
            } else {
                coverage_classify_inferred_let(w, stmt, block, stmt_index);
            }
            break;
        case STMT_FOR:
            coverage_classify_for_counter(w, stmt);
            coverage_classify_block(w, stmt->as.for_loop.body);
            break;
        case STMT_FOR_IN:
            coverage_classify_block(w, stmt->as.for_in.body);
            break;
        case STMT_WHILE:
            coverage_classify_block(w, stmt->as.while_stmt.body);
            break;
        case STMT_LOOP:
            coverage_classify_block(w, stmt->as.loop_stmt.body);
            break;
        case STMT_IF:
            coverage_classify_block(w, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                coverage_classify_block(w, stmt->as.if_stmt.else_branch);
            }
            break;
        case STMT_BLOCK:
            coverage_classify_block(w, stmt);
            break;
        case STMT_TRY:
            coverage_classify_block(w, stmt->as.try_stmt.try_block);
            if (stmt->as.try_stmt.catch_block) {
                coverage_classify_block(w, stmt->as.try_stmt.catch_block);
            }
            if (stmt->as.try_stmt.finally_block) {
                coverage_classify_block(w, stmt->as.try_stmt.finally_block);
            }
            break;
        default:
            break;
    }
}

static void coverage_classify_block(CoverageWalk *w, Stmt *block) {
    if (!block) return;
    if (block->type == STMT_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            coverage_classify_stmt(w, block->as.block.statements[i], block, i);
        }
    } else {
        coverage_classify_stmt(w, block, NULL, 0);
    }
}

// ========== FUNCTION DISCOVERY ==========

static void coverage_analyze_function(CoverageWalk *w, const char *name,
                                      Expr *func);

// Find `let f = fn(...) {...}` declarations (named fn statements desugar
// to this) so nested function bodies get their own analysis pass, the way
// codegen analyzes each generated function separately.
static void coverage_find_functions(CoverageWalk *w, Stmt *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_LET:
            if (stmt->as.let.value &&
                stmt->as.let.value->type == EXPR_FUNCTION) {
                coverage_analyze_function(w, stmt->as.let.name,
                                          stmt->as.let.value);
            }
            break;
        case STMT_CONST:
            if (stmt->as.const_stmt.value &&
                stmt->as.const_stmt.value->type == EXPR_FUNCTION) {
                coverage_analyze_function(w, stmt->as.const_stmt.name,
                                          stmt->as.const_stmt.value);
            }
            break;
        case STMT_FOR:
            coverage_find_functions(w, stmt->as.for_loop.body);
            break;
        case STMT_FOR_IN:
            coverage_find_functions(w, stmt->as.for_in.body);
            break;
        case STMT_WHILE:
            coverage_find_functions(w, stmt->as.while_stmt.body);
            break;
        case STMT_LOOP:
            coverage_find_functions(w, stmt->as.loop_stmt.body);
            break;
        case STMT_IF:
            coverage_find_functions(w, stmt->as.if_stmt.then_branch);
            coverage_find_functions(w, stmt->as.if_stmt.else_branch);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                coverage_find_functions(w, stmt->as.block.statements[i]);
            }
            break;
        case STMT_TRY:
            coverage_find_functions(w, stmt->as.try_stmt.try_block);
            coverage_find_functions(w, stmt->as.try_stmt.catch_block);
            coverage_find_functions(w, stmt->as.try_stmt.finally_block);
            break;
        case STMT_EXPORT:
            if (stmt->as.export_stmt.is_declaration &&
                stmt->as.export_stmt.declaration) {
                coverage_find_functions(w, stmt->as.export_stmt.declaration);
            }
            break;
        default:
            break;
    }
}

static void coverage_analyze_function(CoverageWalk *w, const char *name,
                                      Expr *func) {
    (void)name;

    // Parameters with a primitive annotation are numeric sites that codegen
    // always keeps boxed (they arrive as HmlValue).
    for (int i = 0; i < func->as.function.num_params; i++) {
        Type *pt = func->as.function.param_types
                       ? func->as.function.param_types[i]
                       : NULL;
        CheckedTypeKind kind = type_check_can_unbox_annotation(pt);
        if (kind != CHECKED_UNKNOWN) {
            coverage_record(w, func->as.function.param_names[i], func->line, 0,
                            COVERAGE_SITE_PARAMETER, 0, kind,
                            COVERAGE_PARAMETER, NULL);
        }
    }

    // Same sequence as funcgen_generate_body: clear the previous function's
    // marks, analyze this body, then classify against the marks.
    type_check_clear_all_unboxable(w->ctx);
    type_check_analyze_block_for_unboxing(w->ctx, func->as.function.body);
    coverage_classify_block(w, func->as.function.body);

    // Nested functions get their own pass (clobbers this function's marks,
    // so classification above must already be done).
    coverage_find_functions(w, func->as.function.body);
}

// ========== TOP LEVEL ==========
//
// Codegen pre-declares every direct top-level let/const as a boxed main
// variable and never runs block analysis on top-level code, so candidates
// there are boxed except for-loop counters, which codegen analyzes inline
// at every nesting level.

static void coverage_walk_toplevel(CoverageWalk *w, Stmt *stmt);

static void coverage_walk_toplevel_block(CoverageWalk *w, Stmt *block) {
    if (!block) return;
    if (block->type == STMT_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            coverage_walk_toplevel(w, block->as.block.statements[i]);
        }
    } else {
        coverage_walk_toplevel(w, block);
    }
}

static void coverage_walk_toplevel(CoverageWalk *w, Stmt *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_LET:
            if (coverage_let_is_candidate(stmt)) {
                coverage_record(w, stmt->as.let.name, stmt->line, stmt->column,
                                COVERAGE_SITE_TOP_LEVEL, 0,
                                type_check_can_unbox_annotation(
                                    stmt->as.let.type_annotation),
                                COVERAGE_TOP_LEVEL, NULL);
            }
            break;
        case STMT_FOR:
            type_check_analyze_for_loop(w->ctx, stmt);
            coverage_classify_for_counter(w, stmt);
            coverage_walk_toplevel_block(w, stmt->as.for_loop.body);
            break;
        case STMT_FOR_IN:
            coverage_walk_toplevel_block(w, stmt->as.for_in.body);
            break;
        case STMT_WHILE:
            coverage_walk_toplevel_block(w, stmt->as.while_stmt.body);
            break;
        case STMT_LOOP:
            coverage_walk_toplevel_block(w, stmt->as.loop_stmt.body);
            break;
        case STMT_IF:
            coverage_walk_toplevel_block(w, stmt->as.if_stmt.then_branch);
            coverage_walk_toplevel_block(w, stmt->as.if_stmt.else_branch);
            break;
        case STMT_BLOCK:
            coverage_walk_toplevel_block(w, stmt);
            break;
        case STMT_TRY:
            coverage_walk_toplevel_block(w, stmt->as.try_stmt.try_block);
            coverage_walk_toplevel_block(w, stmt->as.try_stmt.catch_block);
            coverage_walk_toplevel_block(w, stmt->as.try_stmt.finally_block);
            break;
        case STMT_EXPORT:
            if (stmt->as.export_stmt.is_declaration &&
                stmt->as.export_stmt.declaration) {
                coverage_walk_toplevel(w, stmt->as.export_stmt.declaration);
            }
            break;
        default:
            break;
    }
}

// ========== DRIVER ==========

static int coverage_site_compare(const void *a, const void *b) {
    const CoverageSite *sa = *(const CoverageSite *const *)a;
    const CoverageSite *sb = *(const CoverageSite *const *)b;
    if (sa->line != sb->line) return sa->line - sb->line;
    return sa->column - sb->column;
}

CoverageReport *type_coverage_analyze(TypeCheckContext *ctx,
                                      Stmt **stmts, int stmt_count) {
    if (!ctx) return NULL;
    CoverageReport *report = calloc(1, sizeof(CoverageReport));
    if (!report) return NULL;

    CoverageWalk walk = { .ctx = ctx, .report = report, .tail = NULL };

    // Top-level sites first (uses inline loop analysis, no block analysis —
    // matching codegen's handling of main).
    type_check_clear_all_unboxable(ctx);
    for (int i = 0; i < stmt_count; i++) {
        coverage_walk_toplevel(&walk, stmts[i]);
    }

    // Then every function, each with a fresh analysis pass.
    for (int i = 0; i < stmt_count; i++) {
        coverage_find_functions(&walk, stmts[i]);
    }

    type_check_clear_all_unboxable(ctx);

    // Sort by source position (functions were visited after top level).
    if (report->num_sites > 1) {
        CoverageSite **arr = malloc(sizeof(CoverageSite *)
                                    * (size_t)report->num_sites);
        if (arr) {
            int n = 0;
            for (CoverageSite *s = report->sites; s; s = s->next) arr[n++] = s;
            qsort(arr, (size_t)n, sizeof(CoverageSite *),
                  coverage_site_compare);
            for (int j = 0; j < n - 1; j++) arr[j]->next = arr[j + 1];
            arr[n - 1]->next = NULL;
            report->sites = arr[0];
            free(arr);
        }
    }

    return report;
}

void coverage_report_free(CoverageReport *report) {
    if (!report) return;
    CoverageSite *site = report->sites;
    while (site) {
        CoverageSite *next = site->next;
        free(site->name);
        free(site->message);
        free(site);
        site = next;
    }
    free(report);
}

// ========== RENDERING ==========

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} CoverageBuf;

static void covbuf_put(CoverageBuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n + 1) cap *= 2;
        char *data = realloc(b->data, cap);
        if (!data) return;
        b->data = data;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void covbuf_puts(CoverageBuf *b, const char *s) {
    covbuf_put(b, s, strlen(s));
}

static void covbuf_printf(CoverageBuf *b, const char *fmt, ...) {
    char stack[HML_COVERAGE_LINE_BUFSIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(stack, sizeof(stack), fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(stack)) {
        covbuf_put(b, stack, (size_t)n);
        return;
    }
    char *heap = malloc((size_t)n + 1);
    if (!heap) return;
    va_start(args, fmt);
    vsnprintf(heap, (size_t)n + 1, fmt, args);
    va_end(args);
    covbuf_put(b, heap, (size_t)n);
    free(heap);
}

static void covbuf_json_string(CoverageBuf *b, const char *s) {
    covbuf_puts(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  covbuf_puts(b, "\\\""); break;
            case '\\': covbuf_puts(b, "\\\\"); break;
            case '\n': covbuf_puts(b, "\\n"); break;
            case '\r': covbuf_puts(b, "\\r"); break;
            case '\t': covbuf_puts(b, "\\t"); break;
            default:
                if (*p < 0x20) {
                    covbuf_printf(b, "\\u%04x", *p);
                } else {
                    covbuf_put(b, (const char *)p, 1);
                }
        }
    }
    covbuf_puts(b, "\"");
}

static const char *coverage_site_message(const CoverageSite *site) {
    return site->message ? site->message : coverage_reason_text(site->reason);
}

char *coverage_report_render_text(const CoverageReport *report,
                                  const char *filename) {
    CoverageBuf b = {0};
    int boxed = report->num_sites - report->num_unboxed;
    int pct = report->num_sites > 0
                  ? (report->num_unboxed * 100) / report->num_sites
                  : 100;

    covbuf_printf(&b, "Specialization coverage: %s\n", filename);
    covbuf_printf(&b,
                  "  %d numeric site%s: %d unboxed (%d%%), %d boxed\n",
                  report->num_sites, report->num_sites == 1 ? "" : "s",
                  report->num_unboxed, pct, boxed);

    if (report->num_sites > 0) {
        covbuf_puts(&b, "\n");
    }
    for (const CoverageSite *site = report->sites; site; site = site->next) {
        if (site->unboxed) {
            covbuf_printf(&b, "  %s:%d  unboxed  %s: %s (%s)\n",
                          filename, site->line, site->name,
                          checked_type_kind_name(site->native_type),
                          coverage_site_kind_name(site->kind));
        } else {
            covbuf_printf(&b, "  %s:%d  boxed    %s \xe2\x80\x94 %s [%s]\n",
                          filename, site->line, site->name,
                          coverage_site_message(site),
                          coverage_reason_code(site->reason));
            const char *hint = coverage_reason_hint(site->reason);
            if (hint) {
                covbuf_printf(&b, "%*shint: %s\n",
                              (int)(strlen(filename) + 14), "", hint);
            }
        }
    }
    return b.data ? b.data : strdup("");
}

char *coverage_report_render_json(const CoverageReport *report,
                                  const char *filename, int indent) {
    CoverageBuf b = {0};
    char pad[64];
    int p = indent < 0 ? 0 : (indent > 60 ? 60 : indent);
    memset(pad, ' ', sizeof(pad));
    pad[p] = '\0';

    double ratio = report->num_sites > 0
                       ? (double)report->num_unboxed / report->num_sites
                       : 1.0;

    covbuf_printf(&b, "%s{\n", pad);
    covbuf_printf(&b, "%s  \"version\": 1,\n", pad);
    covbuf_printf(&b, "%s  \"file\": ", pad);
    covbuf_json_string(&b, filename);
    covbuf_puts(&b, ",\n");
    covbuf_printf(&b,
                  "%s  \"summary\": {\"sites\": %d, \"unboxed\": %d, "
                  "\"boxed\": %d, \"ratio\": %.4f},\n",
                  pad, report->num_sites, report->num_unboxed,
                  report->num_sites - report->num_unboxed, ratio);
    covbuf_printf(&b, "%s  \"sites\": [", pad);

    int first = 1;
    for (const CoverageSite *site = report->sites; site; site = site->next) {
        covbuf_printf(&b, "%s\n%s    {\"name\": ", first ? "" : ",", pad);
        first = 0;
        covbuf_json_string(&b, site->name);
        covbuf_printf(&b, ", \"line\": %d, \"column\": %d, \"kind\": \"%s\", "
                          "\"decision\": \"%s\"",
                      site->line, site->column,
                      coverage_site_kind_name(site->kind),
                      site->unboxed ? "unboxed" : "boxed");
        if (site->unboxed) {
            covbuf_printf(&b, ", \"native_type\": \"%s\"",
                          checked_type_kind_name(site->native_type));
        } else {
            covbuf_printf(&b, ", \"reason\": \"%s\", \"message\": ",
                          coverage_reason_code(site->reason));
            covbuf_json_string(&b, coverage_site_message(site));
            const char *hint = coverage_reason_hint(site->reason);
            if (hint) {
                covbuf_puts(&b, ", \"hint\": ");
                covbuf_json_string(&b, hint);
            }
        }
        covbuf_puts(&b, "}");
    }
    covbuf_printf(&b, "%s%s  ]\n%s}", first ? "" : "\n", first ? "" : pad,
                  pad);
    return b.data ? b.data : strdup("");
}

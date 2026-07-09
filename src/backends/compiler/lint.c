/*
 * Hemlock Compiler - Static Lint / Diagnostics Pass
 *
 * See include/compiler/lint.h for the high-level description.
 *
 * The pass is a straightforward recursive walk of the AST. It carries no
 * dataflow state of its own: every check is a local, syntactic pattern that
 * the compiler can be certain about, which keeps the implementation small and
 * guarantees no false positives. Each check is documented at its site.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "compiler/lint.h"

/* ========== DIAGNOSTICS ========== */

static void lint_warn(LintContext *ctx, int line, const char *fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    int is_error = ctx->errors_are_fatal;
    if (is_error) ctx->error_count++;
    else ctx->warning_count++;

    if (ctx->collect) {
        LintDiag *d = calloc(1, sizeof(LintDiag));
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

/* Source line for an expression, falling back to the enclosing statement's
 * line when the node itself carries none (e.g. assignment expressions). */
static int lint_eline(LintContext *ctx, Expr *e) {
    return (e && e->line > 0) ? e->line : ctx->cur_line;
}

/* ========== SMALL PREDICATES ========== */

/* A constant boolean/numeric condition. Returns 1 and sets *truthy when the
 * expression is a literal whose truthiness is unambiguous. Strings and null
 * are deliberately excluded — their truthiness rules are easy to get subtly
 * wrong, and `if ("")` / `if (null)` are vanishingly rare in real code. */
static int lint_const_truth(Expr *e, int *truthy) {
    if (!e) return 0;
    switch (e->type) {
        case EXPR_BOOL:
            *truthy = e->as.boolean != 0;
            return 1;
        case EXPR_NUMBER:
            if (e->as.number.is_float) *truthy = e->as.number.float_value != 0.0;
            else if (e->as.number.is_u64) *truthy = e->as.number.uint_value != 0;
            else *truthy = e->as.number.int_value != 0;
            return 1;
        default:
            return 0;
    }
}

/* A literal integer zero (the only case where `%` definitely traps). */
static int lint_is_int_zero(Expr *e) {
    if (!e || e->type != EXPR_NUMBER || e->as.number.is_float) return 0;
    if (e->as.number.is_u64) return e->as.number.uint_value == 0;
    return e->as.number.int_value == 0;
}

/* A call to the builtin `panic(...)`, which never returns. */
static int lint_is_panic_call(Expr *e) {
    if (!e || e->type != EXPR_CALL) return 0;
    Expr *f = e->as.call.func;
    return f && f->type == EXPR_IDENT && f->as.ident.name &&
           strcmp(f->as.ident.name, "panic") == 0;
}

/*
 * Structural equality for *pure* expressions: identifiers, literals, property
 * reads, and indexing whose pieces are themselves pure. Anything containing a
 * call (or any node type not listed) compares unequal, so the self-assignment
 * check never fires on `a[next()] = a[next()]`, where the two sides may differ.
 */
static int lint_pure_equal(Expr *a, Expr *b) {
    if (!a || !b || a->type != b->type) return 0;
    switch (a->type) {
        case EXPR_IDENT:
            return a->as.ident.name && b->as.ident.name &&
                   strcmp(a->as.ident.name, b->as.ident.name) == 0;
        case EXPR_NUMBER:
            if (a->as.number.is_float || b->as.number.is_float)
                return a->as.number.is_float && b->as.number.is_float &&
                       a->as.number.float_value == b->as.number.float_value;
            return a->as.number.is_u64 == b->as.number.is_u64 &&
                   a->as.number.int_value == b->as.number.int_value &&
                   a->as.number.uint_value == b->as.number.uint_value;
        case EXPR_STRING:
            return a->as.string && b->as.string &&
                   strcmp(a->as.string, b->as.string) == 0;
        case EXPR_BOOL:
            return a->as.boolean == b->as.boolean;
        case EXPR_RUNE:
            return a->as.rune == b->as.rune;
        case EXPR_GET_PROPERTY:
            return a->as.get_property.property && b->as.get_property.property &&
                   strcmp(a->as.get_property.property, b->as.get_property.property) == 0 &&
                   lint_pure_equal(a->as.get_property.object, b->as.get_property.object);
        case EXPR_INDEX:
            return lint_pure_equal(a->as.index.object, b->as.index.object) &&
                   lint_pure_equal(a->as.index.index, b->as.index.index);
        default:
            return 0;
    }
}

/*
 * Does `s` contain a break that would exit a loop enclosing it? `depth` is
 * the number of loop/switch levels between that enclosing loop and `s` (0 =
 * directly inside it): an unlabeled break only escapes when depth is 0, since
 * deeper ones bind to the nested loop/switch instead. A labeled break escapes
 * when it names `label`, at any depth. Used to decide whether an infinite
 * loop can ever fall through to the statement after it.
 */
static int lint_has_escaping_break(Stmt *s, const char *label, int depth) {
    if (!s) return 0;
    switch (s->type) {
        case STMT_BREAK:
            if (s->as.break_stmt.label)
                return label && strcmp(s->as.break_stmt.label, label) == 0;
            return depth == 0;
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++)
                if (lint_has_escaping_break(s->as.block.statements[i], label, depth))
                    return 1;
            return 0;
        case STMT_IF:
            return lint_has_escaping_break(s->as.if_stmt.then_branch, label, depth) ||
                   lint_has_escaping_break(s->as.if_stmt.else_branch, label, depth);
        case STMT_TRY:
            return lint_has_escaping_break(s->as.try_stmt.try_block, label, depth) ||
                   lint_has_escaping_break(s->as.try_stmt.catch_block, label, depth) ||
                   lint_has_escaping_break(s->as.try_stmt.finally_block, label, depth);
        case STMT_WHILE:
            return lint_has_escaping_break(s->as.while_stmt.body, label, depth + 1);
        case STMT_LOOP:
            return lint_has_escaping_break(s->as.loop_stmt.body, label, depth + 1);
        case STMT_FOR:
            return lint_has_escaping_break(s->as.for_loop.body, label, depth + 1);
        case STMT_FOR_IN:
            return lint_has_escaping_break(s->as.for_in.body, label, depth + 1);
        case STMT_SWITCH:
            /* An unlabeled break inside a switch binds to the switch. */
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++)
                if (lint_has_escaping_break(s->as.switch_stmt.case_bodies[i], label, depth + 1))
                    return 1;
            return 0;
        default:
            return 0;
    }
}

/*
 * Does control unconditionally leave the program point after this statement
 * (so anything textually following it in the same block is unreachable)?
 * Only cases the compiler is certain about are claimed; conditional loops,
 * switch, and try are treated as falling through to avoid false reports.
 * An infinite loop (`loop`, or `while (true)`) falls through only via a
 * break bound to it — with none, everything after it is unreachable.
 */
static int lint_stmt_diverges(Stmt *s) {
    if (!s) return 0;
    switch (s->type) {
        case STMT_RETURN:
        case STMT_BREAK:
        case STMT_CONTINUE:
        case STMT_THROW:
            return 1;
        case STMT_EXPR:
            return lint_is_panic_call(s->as.expr);
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++)
                if (lint_stmt_diverges(s->as.block.statements[i])) return 1;
            return 0;
        case STMT_IF:
            return s->as.if_stmt.else_branch &&
                   lint_stmt_diverges(s->as.if_stmt.then_branch) &&
                   lint_stmt_diverges(s->as.if_stmt.else_branch);
        case STMT_LOOP:
            return !lint_has_escaping_break(s->as.loop_stmt.body,
                                            s->as.loop_stmt.label, 0);
        case STMT_WHILE: {
            int truthy;
            if (lint_const_truth(s->as.while_stmt.condition, &truthy) && truthy)
                return !lint_has_escaping_break(s->as.while_stmt.body,
                                                s->as.while_stmt.label, 0);
            return 0;
        }
        default:
            return 0;
    }
}

/* A human-readable name for the diverging statement, used in messages. */
static const char *lint_diverge_word(Stmt *s) {
    switch (s->type) {
        case STMT_RETURN:   return "return";
        case STMT_BREAK:    return "break";
        case STMT_CONTINUE: return "continue";
        case STMT_THROW:    return "throw";
        case STMT_EXPR:     return "panic()";
        case STMT_IF:       return "if/else that always diverges";
        case STMT_BLOCK:    return "block that always diverges";
        default:            return "diverging statement";
    }
}

/*
 * Declarations that are hoisted (visible regardless of textual position) and
 * therefore not "unreachable" even when they trail a diverging statement:
 * type/enum/object definitions, imports/exports, extern fns, and named
 * function bindings (`let f = fn ...`).
 */
static int lint_is_hoisted_decl(Stmt *s) {
    if (!s) return 1;
    switch (s->type) {
        case STMT_DEFINE_OBJECT:
        case STMT_ENUM:
        case STMT_TYPE_ALIAS:
        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
            return 1;
        case STMT_LET:
            return s->as.let.value && s->as.let.value->type == EXPR_FUNCTION;
        case STMT_CONST:
            return s->as.const_stmt.value && s->as.const_stmt.value->type == EXPR_FUNCTION;
        default:
            return 0;
    }
}

/* ========== READ COUNTING (for unused-variable detection) ========== */

static int lint_count_reads_expr(Expr *e, const char *name);
static int lint_count_reads_stmt(Stmt *s, const char *name);

static int lint_count_reads_expr(Expr *e, const char *name) {
    if (!e) return 0;
    int n = 0;
    switch (e->type) {
        case EXPR_IDENT:
            if (e->as.ident.name && strcmp(e->as.ident.name, name) == 0) n++;
            break;
        case EXPR_BINARY:
            n += lint_count_reads_expr(e->as.binary.left, name);
            n += lint_count_reads_expr(e->as.binary.right, name);
            break;
        case EXPR_UNARY:
            n += lint_count_reads_expr(e->as.unary.operand, name);
            break;
        case EXPR_TERNARY:
            n += lint_count_reads_expr(e->as.ternary.condition, name);
            n += lint_count_reads_expr(e->as.ternary.true_expr, name);
            n += lint_count_reads_expr(e->as.ternary.false_expr, name);
            break;
        case EXPR_CALL:
            n += lint_count_reads_expr(e->as.call.func, name);
            for (int i = 0; i < e->as.call.num_args; i++)
                n += lint_count_reads_expr(e->as.call.args[i], name);
            break;
        case EXPR_ASSIGN:
            /* The assignment target is a write, but the value is a read, and
             * the target name itself counts as a use of the binding. */
            if (e->as.assign.name && strcmp(e->as.assign.name, name) == 0) n++;
            n += lint_count_reads_expr(e->as.assign.value, name);
            break;
        case EXPR_GET_PROPERTY:
            n += lint_count_reads_expr(e->as.get_property.object, name);
            break;
        case EXPR_SET_PROPERTY:
            n += lint_count_reads_expr(e->as.set_property.object, name);
            n += lint_count_reads_expr(e->as.set_property.value, name);
            break;
        case EXPR_INDEX:
            n += lint_count_reads_expr(e->as.index.object, name);
            n += lint_count_reads_expr(e->as.index.index, name);
            break;
        case EXPR_INDEX_ASSIGN:
            n += lint_count_reads_expr(e->as.index_assign.object, name);
            n += lint_count_reads_expr(e->as.index_assign.index, name);
            n += lint_count_reads_expr(e->as.index_assign.value, name);
            break;
        case EXPR_FUNCTION:
            n += lint_count_reads_stmt(e->as.function.body, name);
            break;
        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < e->as.array_literal.num_elements; i++)
                n += lint_count_reads_expr(e->as.array_literal.elements[i], name);
            break;
        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < e->as.object_literal.num_fields; i++)
                n += lint_count_reads_expr(e->as.object_literal.field_values[i], name);
            break;
        case EXPR_PREFIX_INC:  n += lint_count_reads_expr(e->as.prefix_inc.operand, name); break;
        case EXPR_PREFIX_DEC:  n += lint_count_reads_expr(e->as.prefix_dec.operand, name); break;
        case EXPR_POSTFIX_INC: n += lint_count_reads_expr(e->as.postfix_inc.operand, name); break;
        case EXPR_POSTFIX_DEC: n += lint_count_reads_expr(e->as.postfix_dec.operand, name); break;
        case EXPR_AWAIT:
            n += lint_count_reads_expr(e->as.await_expr.awaited_expr, name);
            break;
        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < e->as.string_interpolation.num_parts; i++)
                n += lint_count_reads_expr(e->as.string_interpolation.expr_parts[i], name);
            break;
        case EXPR_OPTIONAL_CHAIN:
            n += lint_count_reads_expr(e->as.optional_chain.object, name);
            n += lint_count_reads_expr(e->as.optional_chain.index, name);
            for (int i = 0; i < e->as.optional_chain.num_args; i++)
                n += lint_count_reads_expr(e->as.optional_chain.args[i], name);
            break;
        case EXPR_NULL_COALESCE:
            n += lint_count_reads_expr(e->as.null_coalesce.left, name);
            n += lint_count_reads_expr(e->as.null_coalesce.right, name);
            break;
        case EXPR_MATCH:
            n += lint_count_reads_expr(e->as.match_expr.scrutinee, name);
            for (int i = 0; i < e->as.match_expr.num_arms; i++) {
                n += lint_count_reads_expr(e->as.match_expr.arms[i].guard, name);
                n += lint_count_reads_expr(e->as.match_expr.arms[i].body, name);
            }
            break;
        default:
            break;
    }
    return n;
}

static int lint_count_reads_stmt(Stmt *s, const char *name) {
    if (!s) return 0;
    int n = 0;
    switch (s->type) {
        case STMT_LET:    n += lint_count_reads_expr(s->as.let.value, name); break;
        case STMT_CONST:  n += lint_count_reads_expr(s->as.const_stmt.value, name); break;
        case STMT_EXPR:   n += lint_count_reads_expr(s->as.expr, name); break;
        case STMT_IF:
            n += lint_count_reads_expr(s->as.if_stmt.condition, name);
            n += lint_count_reads_stmt(s->as.if_stmt.then_branch, name);
            n += lint_count_reads_stmt(s->as.if_stmt.else_branch, name);
            break;
        case STMT_WHILE:
            n += lint_count_reads_expr(s->as.while_stmt.condition, name);
            n += lint_count_reads_stmt(s->as.while_stmt.body, name);
            break;
        case STMT_LOOP:
            n += lint_count_reads_stmt(s->as.loop_stmt.body, name);
            break;
        case STMT_FOR:
            n += lint_count_reads_stmt(s->as.for_loop.initializer, name);
            n += lint_count_reads_expr(s->as.for_loop.condition, name);
            n += lint_count_reads_expr(s->as.for_loop.increment, name);
            n += lint_count_reads_stmt(s->as.for_loop.body, name);
            break;
        case STMT_FOR_IN:
            n += lint_count_reads_expr(s->as.for_in.iterable, name);
            n += lint_count_reads_stmt(s->as.for_in.body, name);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++)
                n += lint_count_reads_stmt(s->as.block.statements[i], name);
            break;
        case STMT_RETURN: n += lint_count_reads_expr(s->as.return_stmt.value, name); break;
        case STMT_THROW:  n += lint_count_reads_expr(s->as.throw_stmt.value, name); break;
        case STMT_TRY:
            n += lint_count_reads_stmt(s->as.try_stmt.try_block, name);
            n += lint_count_reads_stmt(s->as.try_stmt.catch_block, name);
            n += lint_count_reads_stmt(s->as.try_stmt.finally_block, name);
            break;
        case STMT_SWITCH:
            n += lint_count_reads_expr(s->as.switch_stmt.expr, name);
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++) {
                n += lint_count_reads_expr(s->as.switch_stmt.case_values[i], name);
                n += lint_count_reads_stmt(s->as.switch_stmt.case_bodies[i], name);
            }
            break;
        case STMT_DEFER:  n += lint_count_reads_expr(s->as.defer_stmt.call, name); break;
        case STMT_EXPORT:
            if (s->as.export_stmt.declaration)
                n += lint_count_reads_stmt(s->as.export_stmt.declaration, name);
            break;
        case STMT_DEFINE_OBJECT:
            for (int i = 0; i < s->as.define_object.num_fields; i++)
                n += lint_count_reads_expr(s->as.define_object.field_defaults[i], name);
            for (int i = 0; i < s->as.define_object.num_methods; i++)
                n += lint_count_reads_expr(s->as.define_object.method_defaults[i], name);
            break;
        case STMT_ENUM:
            for (int i = 0; i < s->as.enum_decl.num_variants; i++)
                n += lint_count_reads_expr(s->as.enum_decl.variant_values[i], name);
            break;
        default:
            break;
    }
    return n;
}

/* ========== CHECKS ========== */

/* self-assignment: `x = x`, `obj.f = obj.f`, `a[i] = a[i]`. */
static void lint_check_self_assign(LintContext *ctx, Expr *e) {
    switch (e->type) {
        case EXPR_ASSIGN: {
            Expr *v = e->as.assign.value;
            if (v && v->type == EXPR_IDENT && v->as.ident.name && e->as.assign.name &&
                strcmp(v->as.ident.name, e->as.assign.name) == 0)
                lint_warn(ctx, lint_eline(ctx, e),
                          "self-assignment: '%s = %s' has no effect",
                          e->as.assign.name, e->as.assign.name);
            break;
        }
        case EXPR_SET_PROPERTY: {
            Expr *v = e->as.set_property.value;
            if (v && v->type == EXPR_GET_PROPERTY && v->as.get_property.property &&
                e->as.set_property.property &&
                strcmp(v->as.get_property.property, e->as.set_property.property) == 0 &&
                lint_pure_equal(v->as.get_property.object, e->as.set_property.object))
                lint_warn(ctx, lint_eline(ctx, e),
                          "self-assignment: assigning property '.%s' to itself has no effect",
                          e->as.set_property.property);
            break;
        }
        case EXPR_INDEX_ASSIGN: {
            Expr *v = e->as.index_assign.value;
            if (v && v->type == EXPR_INDEX &&
                lint_pure_equal(v->as.index.object, e->as.index_assign.object) &&
                lint_pure_equal(v->as.index.index, e->as.index_assign.index))
                lint_warn(ctx, lint_eline(ctx, e),
                          "self-assignment: assigning an element to itself has no effect");
            break;
        }
        default:
            break;
    }
}

/* A bare literal (its self-comparison is constant-folder territory, and
 * language tests exercise operators on literals deliberately). */
static int lint_is_literal(Expr *e) {
    if (!e) return 0;
    switch (e->type) {
        case EXPR_NUMBER:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_RUNE:
            return 1;
        default:
            return 0;
    }
}

/*
 * Comparison/combination of a variable with itself: `x < x` / `x > x` are
 * always false, `x && x` / `x || x` are redundant. Only fires when both sides
 * are pure and structurally identical (lint_pure_equal), so `a[next()] <
 * a[next()]` is left alone, and not when both sides are bare literals (`1 <
 * 1` is the constant folder's job and appears deliberately in operator
 * tests).
 *
 * `==`, `!=`, `<=`, `>=` are deliberately NOT flagged: for floats, every
 * comparison with NaN is false, so `x == x` / `x <= x` are NOT always true —
 * `x != x` is the canonical NaN check — and the linter cannot see types.
 * `<` and `>` are safe: false for NaN too, so "always false" always holds.
 */
static void lint_check_self_compare(LintContext *ctx, Expr *e) {
    if (!lint_pure_equal(e->as.binary.left, e->as.binary.right)) return;
    if (lint_is_literal(e->as.binary.left)) return;
    switch (e->as.binary.op) {
        case OP_LESS:
        case OP_GREATER:
            lint_warn(ctx, lint_eline(ctx, e),
                      "comparison of an expression with itself is always false");
            break;
        case OP_AND:
        case OP_OR:
            lint_warn(ctx, lint_eline(ctx, e),
                      "logical operator with identical operands is redundant");
            break;
        default:
            break;
    }
}

/* A constant `if`/`while` condition: the branch/loop is dead or redundant. */
static void lint_check_const_condition(LintContext *ctx, Expr *cond,
                                       int has_else, int is_while, int line) {
    int truthy;
    if (!lint_const_truth(cond, &truthy)) return;
    if (is_while) {
        if (!truthy)
            lint_warn(ctx, line,
                      "loop condition is always false; the loop body never executes");
        /* `while (true)` is a deliberate idiom — say nothing. */
    } else {
        if (!truthy)
            lint_warn(ctx, line,
                      "condition is always false; this branch never executes");
        else if (has_else)
            lint_warn(ctx, line,
                      "condition is always true; the else branch never executes");
    }
}

/* Duplicate field names in an object literal: `{ a: 1, a: 2 }` — the later
 * value silently wins, so the first is a certain mistake. Spread entries
 * (field_names[i] == NULL) are skipped. */
static void lint_check_dup_fields(LintContext *ctx, Expr *e) {
    int n = e->as.object_literal.num_fields;
    for (int i = 0; i < n; i++) {
        const char *name = e->as.object_literal.field_names[i];
        if (!name) continue;  /* spread */
        for (int j = i + 1; j < n; j++) {
            const char *other = e->as.object_literal.field_names[j];
            if (other && strcmp(name, other) == 0) {
                lint_warn(ctx, lint_eline(ctx, e),
                          "duplicate field '%s' in object literal; the later value wins",
                          name);
                break;  /* one report per name */
            }
        }
    }
}

/* A literal usable for exact duplicate-case comparison. */
static int lint_is_case_literal(Expr *e) {
    if (!e) return 0;
    switch (e->type) {
        case EXPR_NUMBER:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_RUNE:
            return 1;
        default:
            return 0;
    }
}

/* Duplicate literal case values in a switch: the second case can never be
 * reached. Only literal values are compared — identifiers or computed cases
 * are left alone since their values are not known here. */
static void lint_check_dup_cases(LintContext *ctx, Stmt *s) {
    int n = s->as.switch_stmt.num_cases;
    for (int i = 0; i < n; i++) {
        Expr *a = s->as.switch_stmt.case_values[i];
        if (!lint_is_case_literal(a)) continue;  /* default or computed */
        for (int j = i + 1; j < n; j++) {
            Expr *b = s->as.switch_stmt.case_values[j];
            if (b && lint_is_case_literal(b) && lint_pure_equal(a, b)) {
                lint_warn(ctx, b->line > 0 ? b->line : s->line,
                          "duplicate case value in switch; this case is unreachable"
                          " (first occurrence on line %d)",
                          a->line > 0 ? a->line : s->line);
                break;  /* one report per value */
            }
        }
    }
}

/* ========== RECURSIVE WALK ========== */

static void lint_expr(LintContext *ctx, Expr *e);
static void lint_stmt(LintContext *ctx, Stmt *s);
static void lint_stmt_list(LintContext *ctx, Stmt **stmts, int count);

static void lint_expr(LintContext *ctx, Expr *e) {
    if (!e) return;
    switch (e->type) {
        case EXPR_BINARY:
            /* modulo by a literal zero traps at runtime. */
            if (e->as.binary.op == OP_MOD && lint_is_int_zero(e->as.binary.right))
                lint_warn(ctx, lint_eline(ctx, e),
                          "modulo by zero: this operation traps at runtime");
            lint_check_self_compare(ctx, e);
            lint_expr(ctx, e->as.binary.left);
            lint_expr(ctx, e->as.binary.right);
            break;
        case EXPR_UNARY:
            lint_expr(ctx, e->as.unary.operand);
            break;
        case EXPR_TERNARY:
            lint_expr(ctx, e->as.ternary.condition);
            lint_expr(ctx, e->as.ternary.true_expr);
            lint_expr(ctx, e->as.ternary.false_expr);
            break;
        case EXPR_CALL:
            lint_expr(ctx, e->as.call.func);
            for (int i = 0; i < e->as.call.num_args; i++)
                lint_expr(ctx, e->as.call.args[i]);
            break;
        case EXPR_ASSIGN:
            lint_check_self_assign(ctx, e);
            lint_expr(ctx, e->as.assign.value);
            break;
        case EXPR_GET_PROPERTY:
            lint_expr(ctx, e->as.get_property.object);
            break;
        case EXPR_SET_PROPERTY:
            lint_check_self_assign(ctx, e);
            lint_expr(ctx, e->as.set_property.object);
            lint_expr(ctx, e->as.set_property.value);
            break;
        case EXPR_INDEX:
            lint_expr(ctx, e->as.index.object);
            lint_expr(ctx, e->as.index.index);
            break;
        case EXPR_INDEX_ASSIGN:
            lint_check_self_assign(ctx, e);
            lint_expr(ctx, e->as.index_assign.object);
            lint_expr(ctx, e->as.index_assign.index);
            lint_expr(ctx, e->as.index_assign.value);
            break;
        case EXPR_FUNCTION:
            lint_stmt(ctx, e->as.function.body);
            break;
        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < e->as.array_literal.num_elements; i++)
                lint_expr(ctx, e->as.array_literal.elements[i]);
            break;
        case EXPR_OBJECT_LITERAL:
            lint_check_dup_fields(ctx, e);
            for (int i = 0; i < e->as.object_literal.num_fields; i++)
                lint_expr(ctx, e->as.object_literal.field_values[i]);
            break;
        case EXPR_PREFIX_INC:  lint_expr(ctx, e->as.prefix_inc.operand); break;
        case EXPR_PREFIX_DEC:  lint_expr(ctx, e->as.prefix_dec.operand); break;
        case EXPR_POSTFIX_INC: lint_expr(ctx, e->as.postfix_inc.operand); break;
        case EXPR_POSTFIX_DEC: lint_expr(ctx, e->as.postfix_dec.operand); break;
        case EXPR_AWAIT:
            lint_expr(ctx, e->as.await_expr.awaited_expr);
            break;
        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < e->as.string_interpolation.num_parts; i++)
                lint_expr(ctx, e->as.string_interpolation.expr_parts[i]);
            break;
        case EXPR_OPTIONAL_CHAIN:
            lint_expr(ctx, e->as.optional_chain.object);
            lint_expr(ctx, e->as.optional_chain.index);
            for (int i = 0; i < e->as.optional_chain.num_args; i++)
                lint_expr(ctx, e->as.optional_chain.args[i]);
            break;
        case EXPR_NULL_COALESCE:
            lint_expr(ctx, e->as.null_coalesce.left);
            lint_expr(ctx, e->as.null_coalesce.right);
            break;
        case EXPR_MATCH:
            lint_expr(ctx, e->as.match_expr.scrutinee);
            for (int i = 0; i < e->as.match_expr.num_arms; i++) {
                lint_expr(ctx, e->as.match_expr.arms[i].guard);
                lint_expr(ctx, e->as.match_expr.arms[i].body);
            }
            break;
        default:
            break;
    }
}

/* unused variables: a let/const, declared directly in this scope, that is
 * never read anywhere in the scope. Strict mode only. Names beginning with
 * '_' and function bindings are exempt by convention. */
static void lint_check_unused(LintContext *ctx, Stmt **stmts, int count) {
    for (int i = 0; i < count; i++) {
        Stmt *s = stmts[i];
        const char *name = NULL;
        Expr *val = NULL;
        int line = s ? s->line : 0;
        if (s && s->type == STMT_LET) { name = s->as.let.name; val = s->as.let.value; }
        else if (s && s->type == STMT_CONST) { name = s->as.const_stmt.name; val = s->as.const_stmt.value; }
        else continue;

        if (!name || name[0] == '_') continue;
        if (val && val->type == EXPR_FUNCTION) continue;

        int reads = 0;
        for (int j = 0; j < count && reads == 0; j++) {
            if (j == i) continue;   /* the declaration itself is not a use */
            reads += lint_count_reads_stmt(stmts[j], name);
        }
        if (reads == 0)
            lint_warn(ctx, line, "variable '%s' is declared but never used", name);
    }
}

/* Report the first unreachable statement in a block (one warning per block). */
static void lint_check_unreachable(LintContext *ctx, Stmt **stmts, int count) {
    int diverged_at = -1;
    for (int i = 0; i < count; i++) {
        Stmt *s = stmts[i];
        if (diverged_at >= 0 && !lint_is_hoisted_decl(s)) {
            Stmt *d = stmts[diverged_at];
            if (d->type == STMT_LOOP || d->type == STMT_WHILE)
                lint_warn(ctx, s->line,
                          "unreachable code: the infinite loop on line %d has no break and never falls through",
                          d->line);
            else
                lint_warn(ctx, s->line,
                          "unreachable code: control always leaves via the %s on line %d",
                          lint_diverge_word(d), d->line);
            return;
        }
        if (diverged_at < 0 && lint_stmt_diverges(s)) diverged_at = i;
    }
}

static void lint_stmt_list(LintContext *ctx, Stmt **stmts, int count) {
    if (!stmts) return;
    lint_check_unreachable(ctx, stmts, count);
    if (ctx->strict) lint_check_unused(ctx, stmts, count);
    for (int i = 0; i < count; i++) lint_stmt(ctx, stmts[i]);
}

static void lint_stmt(LintContext *ctx, Stmt *s) {
    if (!s) return;
    if (s->line > 0) ctx->cur_line = s->line;
    switch (s->type) {
        case STMT_LET:   lint_expr(ctx, s->as.let.value); break;
        case STMT_CONST: lint_expr(ctx, s->as.const_stmt.value); break;
        case STMT_EXPR:  lint_expr(ctx, s->as.expr); break;
        case STMT_IF:
            lint_expr(ctx, s->as.if_stmt.condition);
            lint_check_const_condition(ctx, s->as.if_stmt.condition,
                                       s->as.if_stmt.else_branch != NULL, 0, s->line);
            lint_stmt(ctx, s->as.if_stmt.then_branch);
            lint_stmt(ctx, s->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            lint_expr(ctx, s->as.while_stmt.condition);
            lint_check_const_condition(ctx, s->as.while_stmt.condition, 0, 1, s->line);
            lint_stmt(ctx, s->as.while_stmt.body);
            break;
        case STMT_LOOP:
            lint_stmt(ctx, s->as.loop_stmt.body);
            break;
        case STMT_FOR:
            lint_stmt(ctx, s->as.for_loop.initializer);
            lint_expr(ctx, s->as.for_loop.condition);
            lint_expr(ctx, s->as.for_loop.increment);
            lint_stmt(ctx, s->as.for_loop.body);
            break;
        case STMT_FOR_IN:
            lint_expr(ctx, s->as.for_in.iterable);
            lint_stmt(ctx, s->as.for_in.body);
            break;
        case STMT_BLOCK:
            lint_stmt_list(ctx, s->as.block.statements, s->as.block.count);
            break;
        case STMT_RETURN: lint_expr(ctx, s->as.return_stmt.value); break;
        case STMT_THROW:  lint_expr(ctx, s->as.throw_stmt.value); break;
        case STMT_TRY:
            lint_stmt(ctx, s->as.try_stmt.try_block);
            lint_stmt(ctx, s->as.try_stmt.catch_block);
            lint_stmt(ctx, s->as.try_stmt.finally_block);
            break;
        case STMT_SWITCH:
            lint_expr(ctx, s->as.switch_stmt.expr);
            lint_check_dup_cases(ctx, s);
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++) {
                lint_expr(ctx, s->as.switch_stmt.case_values[i]);
                lint_stmt(ctx, s->as.switch_stmt.case_bodies[i]);
            }
            break;
        case STMT_DEFER:  lint_expr(ctx, s->as.defer_stmt.call); break;
        case STMT_EXPORT:
            if (s->as.export_stmt.declaration)
                lint_stmt(ctx, s->as.export_stmt.declaration);
            break;
        case STMT_DEFINE_OBJECT:
            for (int i = 0; i < s->as.define_object.num_fields; i++)
                lint_expr(ctx, s->as.define_object.field_defaults[i]);
            for (int i = 0; i < s->as.define_object.num_methods; i++)
                lint_expr(ctx, s->as.define_object.method_defaults[i]);
            break;
        case STMT_ENUM:
            for (int i = 0; i < s->as.enum_decl.num_variants; i++)
                lint_expr(ctx, s->as.enum_decl.variant_values[i]);
            break;
        default:
            break;
    }
}

/* ========== PUBLIC API ========== */

LintContext *lint_new(const char *filename) {
    LintContext *ctx = calloc(1, sizeof(LintContext));
    if (!ctx) return NULL;
    ctx->filename = filename;
    return ctx;
}

void lint_free(LintContext *ctx) {
    if (!ctx) return;
    LintDiag *d = ctx->diags;
    while (d) {
        LintDiag *next = d->next;
        free(d->message);
        free(d);
        d = next;
    }
    free(ctx);
}

void lint_enable_collection(LintContext *ctx, const char *source) {
    if (!ctx) return;
    ctx->collect = 1;
    ctx->source = source;
}

int lint_program(LintContext *ctx, Stmt **stmts, int stmt_count) {
    if (!ctx || !stmts) return 0;
    lint_stmt_list(ctx, stmts, stmt_count);
    return ctx->error_count;
}

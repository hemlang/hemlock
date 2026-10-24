/*
 * Hemlock Parser - gpu fn kernels
 *
 * `gpu fn name(...) { ... }` declares a device kernel. `gpu` is a contextual
 * keyword (an identifier followed by `fn`), so existing code using `gpu` as a
 * name is unaffected.
 *
 * Kernel bodies are a statically typed subset of Hemlock with no runtime on
 * the device: numeric scalars, bool, and gpu_buffer<T> parameters; control
 * flow; arithmetic; indexing; calls to plain named functions (device
 * intrinsics such as global_id(0), or other kernels). The subset is checked
 * here, in the shared frontend, so every backend and `hemlock check` reject
 * the same programs.
 *
 * The host-visible binding is a stub function that throws when called; the
 * validated kernel AST hangs off it as `gpu_kernel` for the formatter,
 * serializer, and (later) device code generation.
 */

#include "internal.h"

static int is_scalar_kind(TypeKind k) {
    switch (k) {
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_F32: case TYPE_F64: case TYPE_BOOL:
            return 1;
        default:
            return 0;
    }
}

static int is_scalar_type(Type *t) {
    return t && !t->nullable && is_scalar_kind(t->kind);
}

// gpu_buffer<T> with T a non-bool numeric scalar
static int is_gpu_buffer_type(Type *t) {
    if (!t || t->nullable || t->kind != TYPE_CUSTOM_OBJECT || !t->type_name) return 0;
    if (strcmp(t->type_name, HML_GPU_BUFFER_TYPE) != 0) return 0;
    if (t->num_type_args != 1) return 0;
    Type *elem = t->type_args[0];
    return is_scalar_type(elem) && elem->kind != TYPE_BOOL;
}

typedef struct {
    Parser *p;
    const char *kernel_name;
    int failed;
    // Position of the innermost statement being checked: the fallback for
    // expressions the parser leaves without a line (some literals).
    int stmt_line;
    int stmt_column;
} KernelCheck;

static void kernel_error(KernelCheck *kc, int line, int column, const char *fmt, const char *detail) {
    if (kc->failed) return;
    kc->failed = 1;
    if (line <= 0) {
        line = kc->stmt_line;
        column = kc->stmt_column;
    }

    char msg[256];
    char body[192];
    snprintf(body, sizeof(body), fmt, detail ? detail : "");
    snprintf(msg, sizeof(msg), "gpu fn '%s': %s", kc->kernel_name, body);

    Token t;
    memset(&t, 0, sizeof(t));
    t.type = TOK_ERROR;
    t.line = line;
    t.column = column > 0 ? column : 1;
    error_at(kc->p, &t, msg);
    // A kernel-subset violation is semantic: the token stream is still in
    // sync, so clear panic mode instead of letting the top-level loop
    // synchronize past the next (valid) statement.
    kc->p->panic_mode = 0;
}

static void check_expr(KernelCheck *kc, Expr *e);
static void check_stmt(KernelCheck *kc, Stmt *s);

static void check_expr(KernelCheck *kc, Expr *e) {
    if (!e || kc->failed) return;
    switch (e->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_IDENT:
            return;
        case EXPR_BINARY:
            check_expr(kc, e->as.binary.left);
            check_expr(kc, e->as.binary.right);
            return;
        case EXPR_UNARY:
            check_expr(kc, e->as.unary.operand);
            return;
        case EXPR_TERNARY:
            check_expr(kc, e->as.ternary.condition);
            check_expr(kc, e->as.ternary.true_expr);
            check_expr(kc, e->as.ternary.false_expr);
            return;
        case EXPR_CALL: {
            Expr *callee = e->as.call.func;
            if (!callee || callee->type != EXPR_IDENT) {
                kernel_error(kc, e->line, e->column,
                             "only calls to named functions are allowed (no method calls)%s", NULL);
                return;
            }
            if (strcmp(callee->as.ident.name, kc->kernel_name) == 0) {
                kernel_error(kc, e->line, e->column, "recursion is not allowed in kernels%s", NULL);
                return;
            }
            for (int i = 0; i < e->as.call.num_args; i++) {
                if (e->as.call.arg_names && e->as.call.arg_names[i]) {
                    kernel_error(kc, e->line, e->column, "named arguments are not allowed in kernels%s", NULL);
                    return;
                }
                check_expr(kc, e->as.call.args[i]);
            }
            return;
        }
        case EXPR_ASSIGN:
            check_expr(kc, e->as.assign.value);
            return;
        case EXPR_INDEX:
            check_expr(kc, e->as.index.object);
            check_expr(kc, e->as.index.index);
            return;
        case EXPR_INDEX_ASSIGN:
            check_expr(kc, e->as.index_assign.object);
            check_expr(kc, e->as.index_assign.index);
            check_expr(kc, e->as.index_assign.value);
            return;
        case EXPR_PREFIX_INC:  check_expr(kc, e->as.prefix_inc.operand);  return;
        case EXPR_PREFIX_DEC:  check_expr(kc, e->as.prefix_dec.operand);  return;
        case EXPR_POSTFIX_INC: check_expr(kc, e->as.postfix_inc.operand); return;
        case EXPR_POSTFIX_DEC: check_expr(kc, e->as.postfix_dec.operand); return;

        case EXPR_STRING:
        case EXPR_STRING_INTERPOLATION:
            kernel_error(kc, e->line, e->column, "strings are not allowed in kernels%s", NULL);
            return;
        case EXPR_RUNE:
            kernel_error(kc, e->line, e->column, "runes are not allowed in kernels (use a u32)%s", NULL);
            return;
        case EXPR_NULL:
        case EXPR_OPTIONAL_CHAIN:
        case EXPR_NULL_COALESCE:
            kernel_error(kc, e->line, e->column, "null is not allowed in kernels%s", NULL);
            return;
        case EXPR_GET_PROPERTY:
        case EXPR_SET_PROPERTY:
            kernel_error(kc, e->line, e->column, "property access is not allowed in kernels%s", NULL);
            return;
        case EXPR_FUNCTION:
            kernel_error(kc, e->line, e->column, "closures are not allowed in kernels%s", NULL);
            return;
        case EXPR_ARRAY_LITERAL:
        case EXPR_OBJECT_LITERAL:
            kernel_error(kc, e->line, e->column, "array and object literals are not allowed in kernels%s", NULL);
            return;
        case EXPR_AWAIT:
            kernel_error(kc, e->line, e->column, "await is not allowed in kernels%s", NULL);
            return;
        case EXPR_MATCH:
            kernel_error(kc, e->line, e->column, "match is not supported in kernels yet%s", NULL);
            return;
    }
    kernel_error(kc, e->line, e->column, "expression is not allowed in kernels%s", NULL);
}

static void check_local_decl(KernelCheck *kc, Stmt *s, const char *name, Type *type, Expr *value) {
    if (!type || type->kind == TYPE_INFER) {
        kernel_error(kc, s->line, s->column,
                     "local '%s' needs a type annotation (kernels are fully typed)", name);
        return;
    }
    if (!is_scalar_type(type)) {
        kernel_error(kc, s->line, s->column,
                     "local '%s' must be a numeric or bool scalar", name);
        return;
    }
    check_expr(kc, value);
}

static void check_stmt(KernelCheck *kc, Stmt *s) {
    if (!s || kc->failed) return;
    if (s->line > 0) {
        kc->stmt_line = s->line;
        kc->stmt_column = s->column;
    }
    switch (s->type) {
        case STMT_LET:
            if (s->as.let.value && s->as.let.value->type == EXPR_FUNCTION) {
                kernel_error(kc, s->line, s->column, "nested functions are not allowed in kernels%s", NULL);
                return;
            }
            check_local_decl(kc, s, s->as.let.name, s->as.let.type_annotation, s->as.let.value);
            return;
        case STMT_CONST:
            check_local_decl(kc, s, s->as.const_stmt.name, s->as.const_stmt.type_annotation,
                             s->as.const_stmt.value);
            return;
        case STMT_EXPR:
            check_expr(kc, s->as.expr);
            return;
        case STMT_IF:
            check_expr(kc, s->as.if_stmt.condition);
            check_stmt(kc, s->as.if_stmt.then_branch);
            check_stmt(kc, s->as.if_stmt.else_branch);
            return;
        case STMT_WHILE:
            check_expr(kc, s->as.while_stmt.condition);
            check_stmt(kc, s->as.while_stmt.body);
            return;
        case STMT_LOOP:
            check_stmt(kc, s->as.loop_stmt.body);
            return;
        case STMT_FOR:
            check_stmt(kc, s->as.for_loop.initializer);
            check_expr(kc, s->as.for_loop.condition);
            check_expr(kc, s->as.for_loop.increment);
            check_stmt(kc, s->as.for_loop.body);
            return;
        case STMT_BREAK:
        case STMT_CONTINUE:
            return;
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++) {
                check_stmt(kc, s->as.block.statements[i]);
            }
            return;
        case STMT_RETURN:
            if (s->as.return_stmt.value) {
                kernel_error(kc, s->line, s->column, "kernels cannot return a value (write to a gpu_buffer)%s", NULL);
            }
            return;
        case STMT_SWITCH:
            check_expr(kc, s->as.switch_stmt.expr);
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++) {
                check_expr(kc, s->as.switch_stmt.case_values[i]);
                check_stmt(kc, s->as.switch_stmt.case_bodies[i]);
            }
            return;
        case STMT_FOR_IN:
            kernel_error(kc, s->line, s->column, "for-in is not allowed in kernels (use an indexed for loop)%s", NULL);
            return;
        case STMT_TRY:
        case STMT_THROW:
            kernel_error(kc, s->line, s->column, "exceptions are not allowed in kernels%s", NULL);
            return;
        case STMT_DEFER:
            kernel_error(kc, s->line, s->column, "defer is not allowed in kernels%s", NULL);
            return;
        default:
            kernel_error(kc, s->line, s->column, "declarations are not allowed inside kernels%s", NULL);
            return;
    }
}

int validate_gpu_kernel(Parser *p, const char *name, Expr *fn, int line) {
    KernelCheck kc = { p, name, 0, line, 1 };

    if (fn->as.function.rest_param) {
        kernel_error(&kc, line, 0, "rest parameters are not allowed in kernels%s", NULL);
    }
    for (int i = 0; i < fn->as.function.num_params && !kc.failed; i++) {
        const char *pname = fn->as.function.param_names[i];
        Type *pt = fn->as.function.param_types[i];
        if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
            kernel_error(&kc, line, 0, "parameter '%s' cannot be ref (use a gpu_buffer)", pname);
        } else if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            kernel_error(&kc, line, 0, "parameter '%s' cannot have a default value", pname);
        } else if (!pt) {
            kernel_error(&kc, line, 0, "parameter '%s' needs a type annotation (kernels are fully typed)", pname);
        } else if (!is_scalar_type(pt) && !is_gpu_buffer_type(pt)) {
            kernel_error(&kc, line, 0,
                         "parameter '%s' must be a numeric/bool scalar or gpu_buffer<T> of a numeric T", pname);
        }
    }
    Type *rt = fn->as.function.return_type;
    if (!kc.failed && rt && rt->kind != TYPE_VOID) {
        kernel_error(&kc, line, 0, "kernels must return void (write results to a gpu_buffer)%s", NULL);
    }
    if (!kc.failed) {
        check_stmt(&kc, fn->as.function.body);
    }
    return !kc.failed;
}

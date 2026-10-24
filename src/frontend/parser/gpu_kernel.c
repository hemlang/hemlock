/*
 * Hemlock Parser - gpu fn kernels
 *
 * `gpu fn name(...) { ... }` declares a device kernel. `gpu` is a contextual
 * keyword (an identifier followed by `fn`), so existing code using `gpu` as a
 * name is unaffected.
 *
 * Kernel bodies are a statically typed subset of Hemlock with no runtime on
 * the device: numeric scalars, bool, and gpu_buffer<T> parameters; control
 * flow; arithmetic; buffer indexing; calls to device intrinsics such as
 * global_id(0). The subset is checked here, in the shared frontend, so every
 * backend and `hemlock check` reject the same programs.
 *
 * The declared name binds to a kernel descriptor object:
 *
 *   { kind: "gpu_kernel", name: "saxpy",
 *     params: [{ name: "n", type: "u32", buffer: false }, ...],
 *     emulate: fn(__hml_wi, n: u32, ...) { ...lowered body... } }
 *
 * `emulate` is the kernel lowered to ordinary Hemlock for CPU emulation:
 * intrinsic calls become calls on the work-item context `__hml_wi`, and
 * buffer accesses become bounds-checked __hml_wi.load()/store() calls.
 * @stdlib/gpu_emu drives it over the N-D range. Because the lowering is plain
 * Hemlock produced by the frontend, both backends run emulation identically.
 *
 * The validated kernel AST hangs off `emulate` as `gpu_kernel` for the
 * formatter, the serializer, and (later) device code generation.
 */

#include "gpu_internal.h"
#include <stdarg.h>

// Device intrinsics a kernel may call, with their arity. Thread-index
// intrinsics return u32; math intrinsics keep their argument's type.
// Conversions are Hemlock's own conversion builtins and lower to direct
// calls; everything else lowers to a call on the work-item context.
static const struct {
    const char *name;
    int arity;
    int is_conversion;
} GPU_INTRINSICS[] = {
    { "global_id", 1, 0 }, { "local_id", 1, 0 }, { "group_id", 1, 0 },
    { "global_size", 1, 0 }, { "local_size", 1, 0 }, { "num_groups", 1, 0 },
    { "work_dim", 0, 0 },
    { "min", 2, 0 }, { "max", 2, 0 }, { "abs", 1, 0 }, { "sqrt", 1, 0 },
    { "floor", 1, 0 }, { "ceil", 1, 0 }, { "exp", 1, 0 }, { "log", 1, 0 },
    { "sin", 1, 0 }, { "cos", 1, 0 }, { "pow", 2, 0 },
    { "barrier", 0, 0 },
    { "i8", 1, 1 }, { "i16", 1, 1 }, { "i32", 1, 1 }, { "i64", 1, 1 },
    { "u8", 1, 1 }, { "u16", 1, 1 }, { "u32", 1, 1 }, { "u64", 1, 1 },
    { "f32", 1, 1 }, { "f64", 1, 1 },
};

static int intrinsic_index(const char *name) {
    for (size_t i = 0; i < sizeof(GPU_INTRINSICS) / sizeof(GPU_INTRINSICS[0]); i++) {
        if (strcmp(GPU_INTRINSICS[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int intrinsic_arity(const char *name) {
    int i = intrinsic_index(name);
    return i < 0 ? -1 : GPU_INTRINSICS[i].arity;
}

static int is_conversion_intrinsic(const char *name) {
    int i = intrinsic_index(name);
    return i >= 0 && GPU_INTRINSICS[i].is_conversion;
}

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

static const char *scalar_kind_name(TypeKind k) {
    switch (k) {
        case TYPE_I8:  return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8:  return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        default: return "?";
    }
}

static int is_scalar_type(Type *t) {
    return t && !t->nullable && is_scalar_kind(t->kind);
}

// gpu_buffer<T> / gpu_local<T> with T a non-bool numeric scalar
static int is_memory_type(Type *t, const char *type_name) {
    if (!t || t->nullable || t->kind != TYPE_CUSTOM_OBJECT || !t->type_name) return 0;
    if (strcmp(t->type_name, type_name) != 0) return 0;
    if (t->num_type_args != 1) return 0;
    Type *elem = t->type_args[0];
    return is_scalar_type(elem) && elem->kind != TYPE_BOOL;
}

static int is_gpu_local_type(Type *t) { return is_memory_type(t, HML_GPU_LOCAL_TYPE); }

// Indexable kernel memory: global (gpu_buffer<T>) or work-group local (gpu_local<T>)
static int is_gpu_buffer_type(Type *t) {
    return is_memory_type(t, HML_GPU_BUFFER_TYPE) || is_gpu_local_type(t);
}

// The gpu_buffer<T>/gpu_local<T> type of parameter `name`, or NULL if it is not one
static Type *buffer_param(Expr *fn, const char *name) {
    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (strcmp(fn->as.function.param_names[i], name) == 0) {
            Type *t = fn->as.function.param_types ? fn->as.function.param_types[i] : NULL;
            return is_gpu_buffer_type(t) ? t : NULL;
        }
    }
    return NULL;
}

// "gpu_buffer" or "gpu_local", for messages about parameter `name`
static const char *memory_kind(Expr *fn, const char *name) {
    return is_gpu_local_type(buffer_param(fn, name)) ? HML_GPU_LOCAL_TYPE : HML_GPU_BUFFER_TYPE;
}

static int is_reserved_name(const char *name) {
    return strncmp(name, HML_GPU_RESERVED_PREFIX, strlen(HML_GPU_RESERVED_PREFIX)) == 0;
}

// Exported for gpu_codegen.c
int gpu_is_scalar_type(Type *t) { return is_scalar_type(t); }
int gpu_is_buffer_type(Type *t) { return is_gpu_buffer_type(t); }
int gpu_is_local_type(Type *t) { return is_gpu_local_type(t); }
Type *gpu_buffer_param(Expr *fn, const char *name) { return buffer_param(fn, name); }
const char *gpu_scalar_kind_name(TypeKind k) { return scalar_kind_name(k); }
int gpu_intrinsic_arity(const char *name) { return intrinsic_arity(name); }
int gpu_is_conversion_intrinsic(const char *name) { return is_conversion_intrinsic(name); }

// ========== VALIDATION ==========


void kernel_error(KernelCheck *kc, int line, int column, const char *fmt, ...) {
    if (kc->failed) return;
    kc->failed = 1;
    if (!kc->p) return;  // regenerating a validated kernel: nothing to report
    if (line <= 0) {
        line = kc->stmt_line;
        column = kc->stmt_column;
    }

    char body[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    char msg[256];
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

// Name a kernel may read or assign as a scalar
static void check_scalar_name(KernelCheck *kc, Expr *e, const char *name) {
    if (is_reserved_name(name)) {
        kernel_error(kc, e->line, e->column, "names starting with '%s' are reserved", HML_GPU_RESERVED_PREFIX);
    } else if (buffer_param(kc->fn, name)) {
        kernel_error(kc, e->line, e->column, "%s '%s' can only be indexed (%s[i])", memory_kind(kc->fn, name), name, name);
    }
}

// The object of an index expression must be a gpu_buffer parameter
static void check_buffer_base(KernelCheck *kc, Expr *e, Expr *object) {
    if (!object || object->type != EXPR_IDENT || !buffer_param(kc->fn, object->as.ident.name)) {
        kernel_error(kc, e->line, e->column, "only gpu_buffer and gpu_local parameters can be indexed");
    }
}

static void check_incdec(KernelCheck *kc, Expr *e, Expr *operand) {
    if (!operand || operand->type != EXPR_IDENT) {
        kernel_error(kc, e->line, e->column, "++/-- only apply to scalar variables (use x[i] += 1)");
        return;
    }
    check_scalar_name(kc, e, operand->as.ident.name);
}

static void check_expr(KernelCheck *kc, Expr *e) {
    if (!e || kc->failed) return;
    switch (e->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
            return;
        case EXPR_IDENT:
            check_scalar_name(kc, e, e->as.ident.name);
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
                kernel_error(kc, e->line, e->column, "only calls to device intrinsics are allowed (no method calls)");
                return;
            }
            const char *name = callee->as.ident.name;
            if (strcmp(name, kc->kernel_name) == 0) {
                kernel_error(kc, e->line, e->column, "recursion is not allowed in kernels");
                return;
            }
            int arity = intrinsic_arity(name);
            if (arity < 0) {
                kernel_error(kc, e->line, e->column, "unknown device function '%s'", name);
                return;
            }
            if (e->as.call.num_args != arity) {
                kernel_error(kc, e->line, e->column, "%s() takes %d argument%s, got %d",
                             name, arity, arity == 1 ? "" : "s", e->as.call.num_args);
                return;
            }
            for (int i = 0; i < e->as.call.num_args; i++) {
                if (e->as.call.arg_names && e->as.call.arg_names[i]) {
                    kernel_error(kc, e->line, e->column, "named arguments are not allowed in kernels");
                    return;
                }
                check_expr(kc, e->as.call.args[i]);
            }
            return;
        }
        case EXPR_ASSIGN:
            if (buffer_param(kc->fn, e->as.assign.name)) {
                kernel_error(kc, e->line, e->column, "cannot assign to %s '%s'",
                             memory_kind(kc->fn, e->as.assign.name), e->as.assign.name);
                return;
            }
            check_scalar_name(kc, e, e->as.assign.name);
            check_expr(kc, e->as.assign.value);
            return;
        case EXPR_INDEX:
            check_buffer_base(kc, e, e->as.index.object);
            check_expr(kc, e->as.index.index);
            return;
        case EXPR_INDEX_ASSIGN:
            check_buffer_base(kc, e, e->as.index_assign.object);
            check_expr(kc, e->as.index_assign.index);
            check_expr(kc, e->as.index_assign.value);
            return;
        case EXPR_PREFIX_INC:  check_incdec(kc, e, e->as.prefix_inc.operand);  return;
        case EXPR_PREFIX_DEC:  check_incdec(kc, e, e->as.prefix_dec.operand);  return;
        case EXPR_POSTFIX_INC: check_incdec(kc, e, e->as.postfix_inc.operand); return;
        case EXPR_POSTFIX_DEC: check_incdec(kc, e, e->as.postfix_dec.operand); return;

        case EXPR_STRING:
        case EXPR_STRING_INTERPOLATION:
            kernel_error(kc, e->line, e->column, "strings are not allowed in kernels");
            return;
        case EXPR_RUNE:
            kernel_error(kc, e->line, e->column, "runes are not allowed in kernels (use a u32)");
            return;
        case EXPR_NULL:
        case EXPR_OPTIONAL_CHAIN:
        case EXPR_NULL_COALESCE:
            kernel_error(kc, e->line, e->column, "null is not allowed in kernels");
            return;
        case EXPR_GET_PROPERTY:
        case EXPR_SET_PROPERTY:
            kernel_error(kc, e->line, e->column, "property access is not allowed in kernels");
            return;
        case EXPR_FUNCTION:
            kernel_error(kc, e->line, e->column, "closures are not allowed in kernels");
            return;
        case EXPR_ARRAY_LITERAL:
        case EXPR_OBJECT_LITERAL:
            kernel_error(kc, e->line, e->column, "array and object literals are not allowed in kernels");
            return;
        case EXPR_AWAIT:
            kernel_error(kc, e->line, e->column, "await is not allowed in kernels");
            return;
        case EXPR_MATCH:
            kernel_error(kc, e->line, e->column, "match is not supported in kernels yet");
            return;
    }
    kernel_error(kc, e->line, e->column, "expression is not allowed in kernels");
}

static void check_local_decl(KernelCheck *kc, Stmt *s, const char *name, Type *type, Expr *value) {
    if (is_reserved_name(name)) {
        kernel_error(kc, s->line, s->column, "names starting with '%s' are reserved", HML_GPU_RESERVED_PREFIX);
        return;
    }
    if (buffer_param(kc->fn, name)) {
        kernel_error(kc, s->line, s->column, "local '%s' shadows a %s parameter", name, memory_kind(kc->fn, name));
        return;
    }
    if (!type || type->kind == TYPE_INFER) {
        kernel_error(kc, s->line, s->column,
                     "local '%s' needs a type annotation (kernels are fully typed)", name);
        return;
    }
    if (!is_scalar_type(type)) {
        kernel_error(kc, s->line, s->column, "local '%s' must be a numeric or bool scalar", name);
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
                kernel_error(kc, s->line, s->column, "nested functions are not allowed in kernels");
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
                kernel_error(kc, s->line, s->column, "kernels cannot return a value (write to a gpu_buffer)");
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
            kernel_error(kc, s->line, s->column, "for-in is not allowed in kernels (use an indexed for loop)");
            return;
        case STMT_TRY:
        case STMT_THROW:
            kernel_error(kc, s->line, s->column, "exceptions are not allowed in kernels");
            return;
        case STMT_DEFER:
            kernel_error(kc, s->line, s->column, "defer is not allowed in kernels");
            return;
        default:
            kernel_error(kc, s->line, s->column, "declarations are not allowed inside kernels");
            return;
    }
}

int validate_gpu_kernel(Parser *p, const char *name, Expr *fn, int line) {
    KernelCheck kc = { p, name, fn, 0, line, 1 };

    if (fn->as.function.rest_param) {
        kernel_error(&kc, line, 0, "rest parameters are not allowed in kernels");
    }
    for (int i = 0; i < fn->as.function.num_params && !kc.failed; i++) {
        const char *pname = fn->as.function.param_names[i];
        Type *pt = fn->as.function.param_types[i];
        if (is_reserved_name(pname)) {
            kernel_error(&kc, line, 0, "names starting with '%s' are reserved", HML_GPU_RESERVED_PREFIX);
        } else if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
            kernel_error(&kc, line, 0, "parameter '%s' cannot be ref (use a gpu_buffer)", pname);
        } else if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
            kernel_error(&kc, line, 0, "parameter '%s' cannot have a default value", pname);
        } else if (!pt) {
            kernel_error(&kc, line, 0, "parameter '%s' needs a type annotation (kernels are fully typed)", pname);
        } else if (!is_scalar_type(pt) && !is_gpu_buffer_type(pt)) {
            kernel_error(&kc, line, 0,
                         "parameter '%s' must be a numeric/bool scalar, or gpu_buffer<T>/gpu_local<T> of a numeric T", pname);
        }
    }
    Type *rt = fn->as.function.return_type;
    if (!kc.failed && rt && rt->kind != TYPE_VOID) {
        kernel_error(&kc, line, 0, "kernels must return void (write results to a gpu_buffer)");
    }
    if (!kc.failed) {
        check_stmt(&kc, fn->as.function.body);
    }
    if (!kc.failed) {
        // Static typing (operand types, conversions, scopes) lives in the
        // code generator; run it here so type errors are parse errors
        GpuDeviceCode dc;
        if (gpu_codegen(&kc, name, fn, &dc)) {
            gpu_device_code_free(&dc);
        }
    }
    return !kc.failed;
}

// ========== LOWERING (kernel -> emulation function) ==========
//
// Builds a fresh AST; the kernel AST is left untouched. Only validated
// kernels run, but the lowering must not crash on an invalid one (the
// static checker keeps parsing past errors), so unknown nodes become
// null / empty blocks.

static Expr *at_expr(Expr *n, const Expr *src) {
    n->line = src->line;
    n->column = src->column;
    return n;
}

static Stmt *at_stmt(Stmt *n, const Stmt *src) {
    n->line = src->line;
    n->column = src->column;
    return n;
}

// __hml_wi.<method>(args...)
static Expr *wi_call(const char *method, Expr **args, int num_args, const Expr *src) {
    Expr *callee = at_expr(expr_get_property(at_expr(expr_ident(HML_GPU_WI_PARAM), src), method), src);
    return at_expr(expr_call(callee, args, NULL, num_args), src);
}

static Expr *lower_expr(Expr *fn, Expr *e);
static Stmt *lower_stmt(Expr *fn, Stmt *s);

// __hml_wi.load(buf, index, "T", "buf") / __hml_wi.store(buf, index, value, "T", "buf")
static Expr *lower_buffer_access(Expr *fn, Expr *e, Expr *object, Expr *index, Expr *value) {
    const char *buf = object->as.ident.name;
    Type *bt = buffer_param(fn, buf);
    const char *elem = bt ? scalar_kind_name(bt->type_args[0]->kind) : "?";
    int n = value ? 5 : 4;
    Expr **args = malloc(sizeof(Expr*) * n);
    int k = 0;
    args[k++] = at_expr(expr_ident(buf), e);
    args[k++] = lower_expr(fn, index);
    if (value) args[k++] = lower_expr(fn, value);
    args[k++] = at_expr(expr_string(elem), e);
    args[k++] = at_expr(expr_string(buf), e);
    return wi_call(value ? "store" : "load", args, n, e);
}

static Expr *lower_ident_operand(Expr *operand, const Expr *src) {
    if (operand && operand->type == EXPR_IDENT) {
        return at_expr(expr_ident(operand->as.ident.name), operand);
    }
    return at_expr(expr_null(), src);
}

static Expr *lower_expr(Expr *fn, Expr *e) {
    if (!e) return NULL;
    switch (e->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
            return at_expr(expr_clone(e), e);
        case EXPR_IDENT:
            return at_expr(expr_ident(e->as.ident.name), e);
        case EXPR_BINARY:
            return at_expr(expr_binary(lower_expr(fn, e->as.binary.left), e->as.binary.op,
                                       lower_expr(fn, e->as.binary.right)), e);
        case EXPR_UNARY:
            return at_expr(expr_unary(e->as.unary.op, lower_expr(fn, e->as.unary.operand)), e);
        case EXPR_TERNARY:
            return at_expr(expr_ternary(lower_expr(fn, e->as.ternary.condition),
                                        lower_expr(fn, e->as.ternary.true_expr),
                                        lower_expr(fn, e->as.ternary.false_expr)), e);
        case EXPR_CALL: {
            Expr *callee = e->as.call.func;
            if (!callee || callee->type != EXPR_IDENT) break;
            int n = e->as.call.num_args;
            Expr **args = n > 0 ? malloc(sizeof(Expr*) * n) : NULL;
            for (int i = 0; i < n; i++) {
                args[i] = lower_expr(fn, e->as.call.args[i]);
            }
            if (is_conversion_intrinsic(callee->as.ident.name)) {
                return at_expr(expr_call(at_expr(expr_ident(callee->as.ident.name), e), args, NULL, n), e);
            }
            if (strcmp(callee->as.ident.name, "barrier") == 0) {
                // the source position identifies the barrier for divergence checks
                free(args);
                args = malloc(sizeof(Expr*) * 2);
                args[0] = at_expr(expr_number_int(e->line), e);
                args[1] = at_expr(expr_number_int(e->column), e);
                return wi_call("barrier", args, 2, e);
            }
            return wi_call(callee->as.ident.name, args, n, e);
        }
        case EXPR_ASSIGN:
            return at_expr(expr_assign(e->as.assign.name, lower_expr(fn, e->as.assign.value)), e);
        case EXPR_INDEX:
            if (!e->as.index.object || e->as.index.object->type != EXPR_IDENT) break;
            return lower_buffer_access(fn, e, e->as.index.object, e->as.index.index, NULL);
        case EXPR_INDEX_ASSIGN:
            if (!e->as.index_assign.object || e->as.index_assign.object->type != EXPR_IDENT) break;
            return lower_buffer_access(fn, e, e->as.index_assign.object, e->as.index_assign.index,
                                       e->as.index_assign.value);
        case EXPR_PREFIX_INC:
            return at_expr(expr_prefix_inc(lower_ident_operand(e->as.prefix_inc.operand, e)), e);
        case EXPR_PREFIX_DEC:
            return at_expr(expr_prefix_dec(lower_ident_operand(e->as.prefix_dec.operand, e)), e);
        case EXPR_POSTFIX_INC:
            return at_expr(expr_postfix_inc(lower_ident_operand(e->as.postfix_inc.operand, e)), e);
        case EXPR_POSTFIX_DEC:
            return at_expr(expr_postfix_dec(lower_ident_operand(e->as.postfix_dec.operand, e)), e);
        default:
            break;
    }
    return at_expr(expr_null(), e);
}

static Type *lower_scalar_type(Type *t) {
    return type_new(is_scalar_type(t) ? t->kind : TYPE_INFER);
}

static Stmt *lower_stmt(Expr *fn, Stmt *s) {
    if (!s) return NULL;
    switch (s->type) {
        case STMT_LET:
            return at_stmt(stmt_let_typed(s->as.let.name, lower_scalar_type(s->as.let.type_annotation),
                                          lower_expr(fn, s->as.let.value)), s);
        case STMT_CONST:
            return at_stmt(stmt_const_typed(s->as.const_stmt.name,
                                            lower_scalar_type(s->as.const_stmt.type_annotation),
                                            lower_expr(fn, s->as.const_stmt.value)), s);
        case STMT_EXPR:
            return at_stmt(stmt_expr(lower_expr(fn, s->as.expr)), s);
        case STMT_IF:
            return at_stmt(stmt_if(lower_expr(fn, s->as.if_stmt.condition),
                                   lower_stmt(fn, s->as.if_stmt.then_branch),
                                   lower_stmt(fn, s->as.if_stmt.else_branch)), s);
        case STMT_WHILE:
            return at_stmt(stmt_while_labeled(s->as.while_stmt.label,
                                              lower_expr(fn, s->as.while_stmt.condition),
                                              lower_stmt(fn, s->as.while_stmt.body)), s);
        case STMT_LOOP:
            return at_stmt(stmt_loop_labeled(s->as.loop_stmt.label,
                                             lower_stmt(fn, s->as.loop_stmt.body)), s);
        case STMT_FOR:
            return at_stmt(stmt_for_labeled(s->as.for_loop.label,
                                            lower_stmt(fn, s->as.for_loop.initializer),
                                            lower_expr(fn, s->as.for_loop.condition),
                                            lower_expr(fn, s->as.for_loop.increment),
                                            lower_stmt(fn, s->as.for_loop.body)), s);
        case STMT_BREAK:
            return at_stmt(stmt_break_labeled(s->as.break_stmt.label), s);
        case STMT_CONTINUE:
            return at_stmt(stmt_continue_labeled(s->as.continue_stmt.label), s);
        case STMT_BLOCK: {
            int n = s->as.block.count;
            Stmt **stmts = n > 0 ? malloc(sizeof(Stmt*) * n) : NULL;
            for (int i = 0; i < n; i++) {
                stmts[i] = lower_stmt(fn, s->as.block.statements[i]);
            }
            return at_stmt(stmt_block(stmts, n), s);
        }
        case STMT_RETURN:
            return at_stmt(stmt_return(NULL), s);
        case STMT_SWITCH: {
            int n = s->as.switch_stmt.num_cases;
            Expr **values = n > 0 ? malloc(sizeof(Expr*) * n) : NULL;
            Stmt **bodies = n > 0 ? malloc(sizeof(Stmt*) * n) : NULL;
            for (int i = 0; i < n; i++) {
                values[i] = lower_expr(fn, s->as.switch_stmt.case_values[i]);
                bodies[i] = lower_stmt(fn, s->as.switch_stmt.case_bodies[i]);
            }
            return at_stmt(stmt_switch(lower_expr(fn, s->as.switch_stmt.expr), values, bodies, n), s);
        }
        default:
            return at_stmt(stmt_block(NULL, 0), s);
    }
}

// fn(__hml_wi, <kernel params, buffers untyped>) { <lowered body> }
// Takes ownership of `kernel`, which stays attached as gpu_kernel.
Expr *expr_gpu_emulate_fn(const char *name, Expr *kernel) {
    int nk = kernel->as.function.num_params;
    int n = nk + 1;
    char **names = malloc(sizeof(char*) * n);
    Type **types = malloc(sizeof(Type*) * n);
    Expr **defaults = calloc(n, sizeof(Expr*));
    int *is_ref = calloc(n, sizeof(int));
    int *is_const = calloc(n, sizeof(int));

    names[0] = strdup(HML_GPU_WI_PARAM);
    types[0] = NULL;
    for (int i = 0; i < nk; i++) {
        Type *pt = kernel->as.function.param_types ? kernel->as.function.param_types[i] : NULL;
        names[i + 1] = strdup(kernel->as.function.param_names[i]);
        types[i + 1] = is_scalar_type(pt) ? type_new(pt->kind) : NULL;
        is_const[i + 1] = kernel->as.function.param_is_const ? kernel->as.function.param_is_const[i] : 0;
    }

    Stmt *body = lower_stmt(kernel, kernel->as.function.body);
    if (!body) body = stmt_block(NULL, 0);
    Expr *emu = expr_function(0, names, types, defaults, is_ref, is_const, n, NULL, NULL, NULL, body);
    emu->line = kernel->line;
    emu->as.function.is_gpu = 1;
    emu->as.function.gpu_kernel = kernel;
    emu->as.function.gpu_name = strdup(name);
    return emu;
}

// ========== DESCRIPTOR ==========

static Expr *field_obj(int num, const char **names, Expr **values) {
    char **fnames = malloc(sizeof(char*) * num);
    Expr **fvalues = malloc(sizeof(Expr*) * num);
    for (int i = 0; i < num; i++) {
        fnames[i] = strdup(names[i]);
        fvalues[i] = values[i];
    }
    return expr_object_literal(fnames, fvalues, num);
}

// { source, entry, sites: [{ msg, detail, buf, line }...], uses_f64,
//   f64_reason, uses_groups, site_bits, err_slots }: the kernel compiled to OpenCL C for
// @stdlib/gpu, or null if it does not type-check (already reported).
static Expr *device_code_object(const char *name, Expr *kernel) {
    KernelCheck kc = { NULL, name, kernel, 0, kernel->line, 1 };
    GpuDeviceCode dc;
    if (!gpu_codegen(&kc, name, kernel, &dc)) {
        return expr_null();
    }
    Expr **sites = dc.num_sites > 0 ? malloc(sizeof(Expr*) * dc.num_sites) : NULL;
    for (int i = 0; i < dc.num_sites; i++) {
        const char *skeys[] = { "msg", "detail", "buf", "line" };
        Expr *svals[] = {
            expr_string(dc.sites[i].msg),
            expr_string(dc.sites[i].detail),
            dc.sites[i].buf ? expr_string(dc.sites[i].buf) : expr_null(),
            expr_number_int(dc.sites[i].line),
        };
        sites[i] = field_obj(4, skeys, svals);
    }
    const char *keys[] = { "source", "entry", "sites", "uses_f64", "f64_reason", "uses_groups",
                           "uses_barrier", "site_bits", "err_slots", "key_local_bits" };
    Expr *vals[] = {
        expr_string(dc.source),
        expr_string(dc.entry),
        expr_array_literal(sites, dc.num_sites),
        expr_bool(dc.uses_f64),
        dc.f64_reason ? expr_string(dc.f64_reason) : expr_null(),
        expr_bool(dc.uses_groups),
        expr_bool(dc.uses_barrier),
        expr_number_int(HML_GPU_SITE_BITS),
        expr_number_int(HML_GPU_ERR_SLOTS),
        expr_number_int(HML_GPU_KEY_LOCAL_BITS),
    };
    Expr *obj = field_obj(10, keys, vals);
    gpu_device_code_free(&dc);
    return obj;
}

// { kind, name, params: [{ name, type, buffer, local }...], device, emulate }
// Takes ownership of `kernel`.
Expr *expr_gpu_kernel_descriptor(const char *name, Expr *kernel) {
    int nk = kernel->as.function.num_params;
    Expr **params = nk > 0 ? malloc(sizeof(Expr*) * nk) : NULL;
    for (int i = 0; i < nk; i++) {
        Type *pt = kernel->as.function.param_types ? kernel->as.function.param_types[i] : NULL;
        int is_mem = is_gpu_buffer_type(pt);
        int is_local = is_gpu_local_type(pt);
        const char *tname = is_mem ? scalar_kind_name(pt->type_args[0]->kind)
                          : (is_scalar_type(pt) ? scalar_kind_name(pt->kind) : "?");
        const char *keys[] = { "name", "type", "buffer", "local" };
        Expr *vals[] = {
            expr_string(kernel->as.function.param_names[i]),
            expr_string(tname),
            expr_bool(is_mem && !is_local),
            expr_bool(is_local),
        };
        params[i] = field_obj(4, keys, vals);
    }

    const char *keys[] = { "kind", "name", "params", "device", HML_GPU_EMULATE_FIELD };
    Expr *vals[] = {
        expr_string(HML_GPU_KERNEL_KIND),
        expr_string(name),
        expr_array_literal(params, nk),
        device_code_object(name, kernel),
        NULL,
    };
    vals[4] = expr_gpu_emulate_fn(name, kernel);
    Expr *desc = field_obj(5, keys, vals);
    desc->line = vals[4]->line;
    return desc;
}

// The kernel AST behind a `gpu fn` binding's value, or NULL if `value` is
// not a kernel descriptor.
Expr *gpu_kernel_of(Expr *value) {
    if (!value || value->type != EXPR_OBJECT_LITERAL) return NULL;
    for (int i = 0; i < value->as.object_literal.num_fields; i++) {
        Expr *f = value->as.object_literal.field_values[i];
        const char *k = value->as.object_literal.field_names[i];
        if (k && f && f->type == EXPR_FUNCTION && f->as.function.is_gpu &&
            strcmp(k, HML_GPU_EMULATE_FIELD) == 0) {
            return f->as.function.gpu_kernel;
        }
    }
    return NULL;
}

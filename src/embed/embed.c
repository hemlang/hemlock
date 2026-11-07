/*
 * Hemlock embedding API (include/hemlock_embed.h)
 *
 * A thin layer over the interpreter: an HmlVM is a global Environment with
 * the builtins plus an ExecutionContext and a ModuleCache; a script is a
 * Module the VM owns (not in the cache: two scripts may share a path when a
 * host reloads one); an instance is that Module's body run into a fresh
 * child of the global environment by execute_module_into().
 *
 * Calls from the host go through a parsed call expression,
 * `__hml_embed_fn(__hml_embed_a0, ...)`, evaluated in a scratch environment
 * that binds those names. That reuses the interpreter's own call path, so
 * default parameters, rest parameters, typed parameters, `defer` and stack
 * traces behave exactly as they do for a call written in Hemlock.
 */

#include "hemlock_embed.h"
#include "hemlock_alloc.h"
#include "hemlock_limits.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "frontend.h"
#include "frontend/module.h"
#include "frontend/name_resolve.h"
#include "interpreter.h"
#include "interpreter/internal.h"
#include "interpreter/runtime/expressions_internal.h"

extern void ffi_init(void);

/* ========== VM registry (natives find their VM by index) ========== */

typedef struct {
    char *name;
    HmlNativeFn fn;
    void *userdata;
} HmlNative;

struct HmlVM {
    int id;                        /* index in g_vms */
    Environment *global_env;
    ExecutionContext *ctx;
    ModuleCache *cache;
    int name_check;

    HmlNative *natives;
    int num_natives;
    int natives_capacity;

    char *handle_wrapper;          /* global fn name, or NULL */

    /* Prelude modules run by hml_vm_eval: their ASTs back the functions
       they defined, so they live as long as the VM. */
    Module **preludes;
    int num_preludes;

    /* Parsed `__hml_embed_fn(a0, ..., aN-1)` per arity (lazily built) */
    Stmt *call_stmts[HML_EMBED_MAX_CALL_ARGS + 1];

    /* Last error */
    char *error;
    char *error_file;
    int error_line;
    int error_column;
};

/* A top-level `fn` / `let` / `const` (possibly `export`ed) */
typedef struct {
    char *name;
    int line;
    int exported;
    int is_const;
    Expr *init;          /* borrowed from the module AST; NULL if none */
    Type *annotation;    /* borrowed; NULL if none */
    char *type_name;     /* rendered annotation, or NULL */
} HmlDecl;

struct HmlScript {
    HmlVM *vm;
    int ref_count;
    Module *module;

    HmlDecl *fns;        /* init is the EXPR_FUNCTION */
    int num_fns;
    HmlDecl *vars;
    int num_vars;
};

struct HmlInstance {
    HmlScript *script;
    Environment *env;
};

static HmlVM *g_vms[HML_EMBED_MAX_VMS];
static int g_ffi_initialized = 0;

#define HML_EMBED_FN_NAME  "__hml_embed_fn"
#define HML_EMBED_ARG_FMT  "__hml_embed_a%d"
#define HML_EMBED_HOST_FN  "__hml_embed_host"
#define HML_EMBED_HANDLE_FIELD "__handle"

/* ========== Errors ========== */

static void vm_clear_error(HmlVM *vm) {
    free(vm->error);
    free(vm->error_file);
    vm->error = NULL;
    vm->error_file = NULL;
    vm->error_line = 0;
    vm->error_column = 0;
}

static void vm_set_error_at(HmlVM *vm, const char *file, int line, int column, const char *fmt, ...) {
    char buf[HML_EMBED_ERROR_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    vm_clear_error(vm);
    vm->error = hml_xstrdup(buf);
    vm->error_file = file ? hml_xstrdup(file) : NULL;
    vm->error_line = line;
    vm->error_column = column;
}

#define vm_set_error(vm, ...) vm_set_error_at((vm), NULL, 0, 0, __VA_ARGS__)

/* Sources the embedding synthesizes (prelude, native wrappers, call
   expressions) are named "<...>"; messages should point past them */
static int is_internal_source(const char *path) {
    if (!path) {
        return 1;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return base[0] == '<';
}

/* Take a pending exception off the context into vm->error, leaving the
   context reusable. `call_depth` is the call-stack depth to unwind to;
   `fallback_file` names the error when no frame does. */
static void vm_capture_exception_in(HmlVM *vm, int call_depth, const char *fallback_file) {
    ExecutionContext *ctx = vm->ctx;
    const char *file = NULL;
    int line = 0;
    int column = 0;
    /* The innermost frame in user code. Skipped: frames in the embedding's
       own sources ("<...>" files) and calls of internal functions
       ("__"-prefixed, like a host prelude's `__godot_get`), whose call
       sites are host code. Frame file names are interned by the
       interpreter, so they stay valid after the module that pushed them. */
    for (int i = ctx->call_stack.count - 1; i >= call_depth && i >= 0; i--) {
        CallFrame *f = &ctx->call_stack.frames[i];
        if (f->line <= 0 || (f->source_file && is_internal_source(f->source_file)) ||
            (f->function_name && strncmp(f->function_name, "__", 2) == 0)) {
            continue;
        }
        file = f->source_file;
        line = f->line;
        column = f->column;
        break;
    }
    if (!file) {
        file = fallback_file;
    }

    char *msg = value_to_string(ctx->exception_state.exception_value);
    vm_set_error_at(vm, file, line, column, "%s", msg ? msg : "(unknown error)");
    free(msg);

    VALUE_RELEASE(ctx->exception_state.exception_value);
    ctx->exception_state.exception_value = val_null();
    ctx->exception_state.is_throwing = 0;
    ctx->return_state.is_returning = 0;
    ctx->loop_state.is_breaking = 0;
    ctx->loop_state.is_continuing = 0;
    if (ctx->call_stack.count > call_depth) {
        ctx->call_stack.count = call_depth;
    }
}

#define vm_capture_exception(vm, depth) vm_capture_exception_in((vm), (depth), NULL)

/* Run with `path` as the interpreter's current source file (frames and
   messages name it), restoring the previous one after */
typedef struct {
    const char *saved;
} SourceScope;

static SourceScope source_enter(const char *path) {
    SourceScope scope = { get_current_source_file() };
    set_current_source_file(path);
    return scope;
}

static void source_leave(SourceScope scope) {
    set_current_source_file(scope.saved);  /* interned: still valid */
}

const char *hml_vm_last_error(const HmlVM *vm) { return vm && vm->error ? vm->error : ""; }
int hml_vm_last_error_line(const HmlVM *vm) { return vm ? vm->error_line : 0; }
int hml_vm_last_error_column(const HmlVM *vm) { return vm ? vm->error_column : 0; }
const char *hml_vm_last_error_file(const HmlVM *vm) { return vm ? vm->error_file : NULL; }

/* ========== Host values ========== */

HmlEmbedValue hml_embed_null(void) {
    HmlEmbedValue v;
    memset(&v, 0, sizeof(v));
    v.kind = HML_EMBED_NULL;
    return v;
}

HmlEmbedValue hml_embed_bool(int b) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_BOOL;
    v.as.b = b ? 1 : 0;
    return v;
}

HmlEmbedValue hml_embed_int(int64_t i) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_INT;
    v.as.i = i;
    return v;
}

HmlEmbedValue hml_embed_float(double f) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_FLOAT;
    v.as.f = f;
    return v;
}

HmlEmbedValue hml_embed_handle(uint64_t handle) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_HANDLE;
    v.as.handle = handle;
    return v;
}

HmlEmbedValue hml_embed_string(const char *data, size_t len) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_STRING;
    v.as.s.data = hml_xmalloc(len + 1);
    if (len) {
        memcpy(v.as.s.data, data, len);
    }
    v.as.s.data[len] = '\0';
    v.as.s.len = len;
    return v;
}

HmlEmbedValue hml_embed_cstring(const char *data) {
    return hml_embed_string(data ? data : "", data ? strlen(data) : 0);
}

HmlEmbedValue hml_embed_array(int reserve) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_ARRAY;
    if (reserve > 0) {
        v.as.arr.items = hml_xcalloc((size_t)reserve, sizeof(HmlEmbedValue));
    }
    v.as.arr.count = 0;
    return v;
}

void hml_embed_array_push(HmlEmbedValue *array, HmlEmbedValue item) {
    if (!array || array->kind != HML_EMBED_ARRAY) {
        hml_embed_value_free(&item);
        return;
    }
    /* The struct carries no capacity (it is a plain tree), so grow exactly */
    int count = array->as.arr.count;
    array->as.arr.items = hml_xrealloc(array->as.arr.items, sizeof(HmlEmbedValue) * (size_t)(count + 1));
    array->as.arr.items[count] = item;
    array->as.arr.count = count + 1;
}

HmlEmbedValue hml_embed_object(const char *type_name, int reserve) {
    HmlEmbedValue v = hml_embed_null();
    v.kind = HML_EMBED_OBJECT;
    v.as.obj.type_name = type_name ? hml_xstrdup(type_name) : NULL;
    if (reserve > 0) {
        v.as.obj.keys = hml_xcalloc((size_t)reserve, sizeof(char *));
        v.as.obj.values = hml_xcalloc((size_t)reserve, sizeof(HmlEmbedValue));
    }
    return v;
}

void hml_embed_object_set(HmlEmbedValue *object, const char *key, HmlEmbedValue item) {
    if (!object || object->kind != HML_EMBED_OBJECT || !key) {
        hml_embed_value_free(&item);
        return;
    }
    for (int i = 0; i < object->as.obj.count; i++) {
        if (strcmp(object->as.obj.keys[i], key) == 0) {
            hml_embed_value_free(&object->as.obj.values[i]);
            object->as.obj.values[i] = item;
            return;
        }
    }
    int count = object->as.obj.count;
    object->as.obj.keys = hml_xrealloc((void *)object->as.obj.keys, sizeof(char *) * (size_t)(count + 1));
    object->as.obj.values = hml_xrealloc(object->as.obj.values, sizeof(HmlEmbedValue) * (size_t)(count + 1));
    object->as.obj.keys[count] = hml_xstrdup(key);
    object->as.obj.values[count] = item;
    object->as.obj.count = count + 1;
}

const HmlEmbedValue *hml_embed_object_get(const HmlEmbedValue *object, const char *key) {
    if (!object || object->kind != HML_EMBED_OBJECT || !key) {
        return NULL;
    }
    for (int i = 0; i < object->as.obj.count; i++) {
        if (strcmp(object->as.obj.keys[i], key) == 0) {
            return &object->as.obj.values[i];
        }
    }
    return NULL;
}

HmlEmbedValue hml_embed_value_copy(const HmlEmbedValue *v) {
    if (!v) {
        return hml_embed_null();
    }
    HmlEmbedValue out = *v;
    switch (v->kind) {
        case HML_EMBED_STRING:
        case HML_EMBED_OPAQUE:
            out = hml_embed_string(v->as.s.data, v->as.s.len);
            out.kind = v->kind;
            break;
        case HML_EMBED_ARRAY:
            out = hml_embed_array(v->as.arr.count);
            for (int i = 0; i < v->as.arr.count; i++) {
                out.as.arr.items[i] = hml_embed_value_copy(&v->as.arr.items[i]);
            }
            out.as.arr.count = v->as.arr.count;
            break;
        case HML_EMBED_OBJECT:
            out = hml_embed_object(v->as.obj.type_name, v->as.obj.count);
            for (int i = 0; i < v->as.obj.count; i++) {
                out.as.obj.keys[i] = hml_xstrdup(v->as.obj.keys[i]);
                out.as.obj.values[i] = hml_embed_value_copy(&v->as.obj.values[i]);
            }
            out.as.obj.count = v->as.obj.count;
            break;
        default:
            break;
    }
    return out;
}

void hml_embed_value_free(HmlEmbedValue *v) {
    if (!v) {
        return;
    }
    switch (v->kind) {
        case HML_EMBED_STRING:
        case HML_EMBED_OPAQUE:
            free(v->as.s.data);
            break;
        case HML_EMBED_ARRAY:
            for (int i = 0; i < v->as.arr.count; i++) {
                hml_embed_value_free(&v->as.arr.items[i]);
            }
            free(v->as.arr.items);
            break;
        case HML_EMBED_OBJECT:
            for (int i = 0; i < v->as.obj.count; i++) {
                free(v->as.obj.keys[i]);
                hml_embed_value_free(&v->as.obj.values[i]);
            }
            free((void *)v->as.obj.keys);
            free(v->as.obj.values);
            free(v->as.obj.type_name);
            break;
        default:
            break;
    }
    *v = hml_embed_null();
}

/* ========== Conversion: Hemlock -> host ========== */

static HmlEmbedValue to_host(HmlVM *vm, Value val, int depth);

static HmlEmbedValue opaque_of(Value val) {
    const char *name = get_value_type_name(val);
    HmlEmbedValue v = hml_embed_cstring(name ? name : "value");
    v.kind = HML_EMBED_OPAQUE;
    return v;
}

static HmlEmbedValue to_host(HmlVM *vm, Value val, int depth) {
    if (depth > HML_EMBED_MAX_VALUE_DEPTH) {
        return opaque_of(val);
    }
    switch (val.type) {
        case VAL_NULL: return hml_embed_null();
        case VAL_BOOL: return hml_embed_bool(val.as.as_bool);
        case VAL_I8:   return hml_embed_int(val.as.as_i8);
        case VAL_I16:  return hml_embed_int(val.as.as_i16);
        case VAL_I32:  return hml_embed_int(val.as.as_i32);
        case VAL_I64:  return hml_embed_int(val.as.as_i64);
        case VAL_U8:   return hml_embed_int(val.as.as_u8);
        case VAL_U16:  return hml_embed_int(val.as.as_u16);
        case VAL_U32:  return hml_embed_int(val.as.as_u32);
        case VAL_U64:
            return hml_embed_int(val.as.as_u64 > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)val.as.as_u64);
        case VAL_RUNE: return hml_embed_int((int64_t)val.as.as_rune);
        case VAL_F32:  return hml_embed_float(val.as.as_f32);
        case VAL_F64:  return hml_embed_float(val.as.as_f64);
        case VAL_STRING: {
            String *s = val.as.as_string;
            return hml_embed_string(s->data, (size_t)s->length);
        }
        case VAL_ARRAY: {
            Array *arr = val.as.as_array;
            HmlEmbedValue out = hml_embed_array(arr->length);
            for (int i = 0; i < arr->length; i++) {
                out.as.arr.items[i] = to_host(vm, arr->elements[i], depth + 1);
            }
            out.as.arr.count = arr->length;
            return out;
        }
        case VAL_OBJECT: {
            Object *obj = val.as.as_object;
            int idx = object_lookup_field(obj, HML_EMBED_HANDLE_FIELD);
            if (idx >= 0 && obj->fields[idx].value.type == VAL_U64) {
                return hml_embed_handle(obj->fields[idx].value.as.as_u64);
            }
            HmlEmbedValue out = hml_embed_object(obj->type_name, obj->num_fields);
            for (int i = 0; i < obj->num_fields; i++) {
                out.as.obj.keys[i] = hml_xstrdup(obj->fields[i].name);
                out.as.obj.values[i] = to_host(vm, obj->fields[i].value, depth + 1);
            }
            out.as.obj.count = obj->num_fields;
            return out;
        }
        default:
            return opaque_of(val);
    }
}

/* ========== Conversion: host -> Hemlock ========== */

static Value call_value(HmlVM *vm, Environment *scope, Value fn, Value *args, int num_args);

/* Returns an owned Value (refcount 1 for heap values). Leaves an exception
   pending in vm->ctx if the handle wrapper raised. */
static Value to_hemlock(HmlVM *vm, const HmlEmbedValue *v, int depth) {
    if (!v || depth > HML_EMBED_MAX_VALUE_DEPTH) {
        return val_null();
    }
    switch (v->kind) {
        case HML_EMBED_NULL:
        case HML_EMBED_OPAQUE:
            return val_null();
        case HML_EMBED_BOOL:
            return val_bool(v->as.b);
        case HML_EMBED_INT:
            /* Same inference as an integer literal */
            if (v->as.i >= INT32_MIN && v->as.i <= INT32_MAX) {
                return val_i32((int32_t)v->as.i);
            }
            return val_i64(v->as.i);
        case HML_EMBED_FLOAT:
            return val_f64(v->as.f);
        case HML_EMBED_STRING: {
            size_t len = v->as.s.len;
            if (len > (size_t)INT_MAX - 1) {
                len = (size_t)INT_MAX - 1;
            }
            char *copy = hml_xmalloc(len + 1);
            memcpy(copy, v->as.s.data, len);
            copy[len] = '\0';
            return val_string_take(copy, (int)len, (int)len + 1);
        }
        case HML_EMBED_ARRAY: {
            Array *arr = array_new();
            for (int i = 0; i < v->as.arr.count; i++) {
                Value item = to_hemlock(vm, &v->as.arr.items[i], depth + 1);
                array_push(arr, item);   /* retains */
                VALUE_RELEASE(item);
            }
            return val_array(arr);
        }
        case HML_EMBED_OBJECT: {
            int n = v->as.obj.count;
            Object *obj = object_new(v->as.obj.type_name, n > 0 ? n : 1);
            for (int i = 0; i < n; i++) {
                const char *key = v->as.obj.keys[i];
                Value item = to_hemlock(vm, &v->as.obj.values[i], depth + 1);
                int existing = object_lookup_field(obj, key);
                if (existing >= 0) {
                    VALUE_RELEASE(obj->fields[existing].value);
                    obj->fields[existing].value = item;
                    continue;
                }
                if (obj->num_fields >= obj->capacity) {
                    object_grow_fields(obj, obj->capacity * 2);
                }
                int idx = obj->num_fields;
                obj->fields[idx].name = hml_xstrdup(key);
                obj->fields[idx].value = item;   /* object owns it */
                obj->num_fields++;
                object_hash_insert(obj, key, idx);
            }
            return val_object(obj);
        }
        case HML_EMBED_HANDLE: {
            Value h = val_u64(v->as.handle);
            if (vm->handle_wrapper) {
                Value fn = env_get(vm->global_env, vm->handle_wrapper, vm->ctx);
                if (vm->ctx->exception_state.is_throwing) {
                    return val_null();
                }
                Value wrapped = call_value(vm, vm->global_env, fn, &h, 1);
                VALUE_RELEASE(fn);
                return wrapped;
            }
            Object *obj = object_new(NULL, 1);
            obj->fields[0].name = hml_xstrdup(HML_EMBED_HANDLE_FIELD);
            obj->fields[0].value = h;
            obj->num_fields = 1;
            object_hash_insert(obj, HML_EMBED_HANDLE_FIELD, 0);
            return val_object(obj);
        }
    }
    return val_null();
}

/* ========== Calling Hemlock functions ========== */

static Stmt *parse_one(const char *source) {
    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    parser_enable_error_collection(&parser);
    int count = 0;
    Stmt **stmts = parse_program(&parser, &count);
    int ok = !parser.had_error && count == 1 && stmts[0]->type == STMT_EXPR;
    parser_free_errors(&parser);
    if (!ok) {
        for (int i = 0; i < count; i++) {
            stmt_free(stmts[i]);
        }
        free((void *)stmts);
        return NULL;
    }
    Stmt *stmt = stmts[0];
    free((void *)stmts);
    resolve_program(&stmt, 1);
    return stmt;
}

static Stmt *call_stmt_for(HmlVM *vm, int num_args) {
    if (vm->call_stmts[num_args]) {
        return vm->call_stmts[num_args];
    }
    /* "__hml_embed_fn(__hml_embed_a0, __hml_embed_a1);" */
    size_t cap = 64 + (size_t)num_args * 24;
    char *src = hml_xmalloc(cap);
    size_t len = (size_t)snprintf(src, cap, "%s(", HML_EMBED_FN_NAME);
    for (int i = 0; i < num_args; i++) {
        len += (size_t)snprintf(src + len, cap - len, i ? ", " HML_EMBED_ARG_FMT : HML_EMBED_ARG_FMT, i);
    }
    snprintf(src + len, cap - len, ");");
    vm->call_stmts[num_args] = parse_one(src);
    free(src);
    return vm->call_stmts[num_args];
}

/* Call `fn` with `args` (borrowed) from a scratch scope under `scope`.
   Returns an owned result; leaves any exception pending. */
static Value call_value(HmlVM *vm, Environment *scope, Value fn, Value *args, int num_args) {
    if (num_args > HML_EMBED_MAX_CALL_ARGS) {
        runtime_error(vm->ctx, "embedding: too many arguments (%d, max %d)",
                      num_args, HML_EMBED_MAX_CALL_ARGS);
        return val_null();
    }
    Stmt *stmt = call_stmt_for(vm, num_args);
    if (!stmt) {
        runtime_error(vm->ctx, "embedding: could not build call expression");
        return val_null();
    }

    Environment *call_env = env_new(scope);
    env_define(call_env, HML_EMBED_FN_NAME, fn, 1, vm->ctx);
    char name[32];
    for (int i = 0; i < num_args; i++) {
        snprintf(name, sizeof(name), HML_EMBED_ARG_FMT, i);
        env_define(call_env, name, args[i], 0, vm->ctx);
    }
    Value result = eval_expr(stmt->as.expr, call_env, vm->ctx);
    env_release(call_env);
    if (vm->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(result);
        return val_null();
    }
    return result;
}

/* ========== Host natives ========== */

/* __hml_embed_host(vm_id, native_index, args_array) */
static Value builtin_embed_host(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3 || args[2].type != VAL_ARRAY) {
        runtime_error(ctx, HML_EMBED_HOST_FN "() is internal to the embedding API");
        return val_null();
    }
    int64_t vm_id = value_to_int64(args[0]);
    int64_t index = value_to_int64(args[1]);
    HmlVM *vm = (vm_id >= 0 && vm_id < HML_EMBED_MAX_VMS) ? g_vms[vm_id] : NULL;
    if (!vm || index < 0 || index >= vm->num_natives) {
        runtime_error(ctx, "embedding: stale host function");
        return val_null();
    }
    HmlNative *native = &vm->natives[index];

    Array *arr = args[2].as.as_array;
    HmlEmbedValue *host_args = arr->length ? hml_xcalloc((size_t)arr->length, sizeof(HmlEmbedValue)) : NULL;
    for (int i = 0; i < arr->length; i++) {
        host_args[i] = to_host(vm, arr->elements[i], 0);
    }

    HmlEmbedValue result = hml_embed_null();
    char error[HML_EMBED_ERROR_SIZE];
    error[0] = '\0';
    int failed = native->fn(native->userdata, host_args, arr->length, &result, error, sizeof(error));

    for (int i = 0; i < arr->length; i++) {
        hml_embed_value_free(&host_args[i]);
    }
    free(host_args);

    if (failed) {
        hml_embed_value_free(&result);
        /* Thrown as a plain string, like `throw "..."`: runtime_error()
           would prefix the line of the internal wrapper */
        ctx->exception_state.exception_value = val_string(error[0] ? error : native->name);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    Value out = to_hemlock(vm, &result, 0);
    hml_embed_value_free(&result);
    return out;
}

/* ========== Parsing / module construction ========== */

static void module_free_owned(Module *module) {
    if (!module) {
        return;
    }
    for (int i = 0; i < module->num_statements; i++) {
        stmt_free(module->statements[i]);
    }
    free((void *)module->statements);
    for (int i = 0; i < module->num_exports; i++) {
        free(module->export_names[i]);
    }
    free((void *)module->export_names);
    if (module->exports_env) {
        env_release(module->exports_env);
    }
    free(module->absolute_path);
    free(module);
}

static char *absolute_script_path(const char *path) {
    if (path && path[0] == '/') {
        return hml_xstrdup(path);
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '\0';
    }
    size_t need = strlen(cwd) + 2 + strlen(path ? path : "<embedded>.hml");
    char *out = hml_xmalloc(need);
    snprintf(out, need, "%s/%s", cwd, path ? path : "<embedded>.hml");
    return out;
}

/* Parse, name-check and load the imports of `source`. NULL on failure with
   vm->error set and *status written. */
static Module *build_module(HmlVM *vm, const char *source, const char *path,
                            int name_check, HmlEmbedStatus *status) {
    char *abs_path = absolute_script_path(path);

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    parser_enable_error_collection(&parser);
    int count = 0;
    Stmt **stmts = parse_program(&parser, &count);
    if (parser.had_error) {
        ParseError *e = parser.errors;
        vm_set_error_at(vm, abs_path, e ? e->line : 0, e ? e->column : 0,
                        "%s", e && e->message ? e->message : "parse error");
        parser_free_errors(&parser);
        for (int i = 0; i < count; i++) {
            stmt_free(stmts[i]);
        }
        free((void *)stmts);
        free(abs_path);
        *status = HML_EMBED_PARSE_ERROR;
        return NULL;
    }
    parser_free_errors(&parser);

    if (name_check) {
        NameResolveResult *names = name_resolve_entry(abs_path, stmts, count);
        if (names && names->error_count > 0 && !names->entry_parse_failed) {
            NameDiag *d = names->diags;
            vm_set_error_at(vm, d->file ? d->file : abs_path, d->line, d->column, "%s%s%s",
                            d->message, d->hint ? " " : "", d->hint ? d->hint : "");
            name_resolve_result_free(names);
            for (int i = 0; i < count; i++) {
                stmt_free(stmts[i]);
            }
            free((void *)stmts);
            free(abs_path);
            *status = HML_EMBED_NAME_ERROR;
            return NULL;
        }
        name_resolve_result_free(names);
    }

    resolve_program(stmts, count);
    optimize_program(stmts, count);

    Module *module = hml_xcalloc(1, sizeof(Module));
    module->absolute_path = abs_path;
    module->state = MODULE_LOADED;
    module->statements = stmts;
    module->num_statements = count;
    module->export_capacity = 1;
    module->export_names = hml_xcalloc(1, sizeof(char *));

    if (module_load_dependencies(vm->cache, module, vm->ctx) != 0) {
        vm_set_error_at(vm, abs_path, 0, 0, "failed to load an imported module of '%s'", abs_path);
        module_free_owned(module);
        *status = HML_EMBED_LOAD_ERROR;
        return NULL;
    }

    *status = HML_EMBED_OK;
    return module;
}

/* ========== VM ========== */

void hml_vm_config_init(HmlVMConfig *config) {
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->api_version = HML_EMBED_API_VERSION;
    config->name_check = 1;
}

HmlVM *hml_vm_new(const HmlVMConfig *config) {
    HmlVMConfig defaults;
    if (!config) {
        hml_vm_config_init(&defaults);
        config = &defaults;
    }
    if (config->api_version != HML_EMBED_API_VERSION) {
        return NULL;
    }

    int slot = -1;
    for (int i = 0; i < HML_EMBED_MAX_VMS; i++) {
        if (!g_vms[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL;
    }

    if (!g_ffi_initialized) {
        ffi_init();
        g_ffi_initialized = 1;
    }
    if (config->stdlib_path) {
        module_set_stdlib_override(config->stdlib_path);
    }

    HmlVM *vm = hml_xcalloc(1, sizeof(HmlVM));
    vm->id = slot;
    vm->name_check = config->name_check;
    vm->global_env = env_new(NULL);
    vm->ctx = exec_context_new();
    if (config->max_stack_depth > 0) {
        vm->ctx->max_stack_depth = config->max_stack_depth;
    }
    register_builtins(vm->global_env, 0, NULL, vm->ctx);
    env_define(vm->global_env, HML_EMBED_HOST_FN, val_builtin_fn(builtin_embed_host), 1, vm->ctx);

    char cwd[PATH_MAX];
    const char *base = config->base_dir;
    if (!base) {
        base = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
    }
    vm->cache = module_cache_new(base);
    vm->cache->no_exit_on_error = 1;

    g_vms[slot] = vm;
    return vm;
}

void hml_vm_free(HmlVM *vm) {
    if (!vm) {
        return;
    }
    g_vms[vm->id] = NULL;

    env_break_cycles(vm->global_env);
    env_release(vm->global_env);
    module_cache_free(vm->cache);
    for (int i = 0; i < vm->num_preludes; i++) {
        module_free_owned(vm->preludes[i]);
    }
    free((void *)vm->preludes);
    for (int i = 0; i <= HML_EMBED_MAX_CALL_ARGS; i++) {
        if (vm->call_stmts[i]) {
            stmt_free(vm->call_stmts[i]);
        }
    }
    exec_context_free(vm->ctx);
    for (int i = 0; i < vm->num_natives; i++) {
        free(vm->natives[i].name);
    }
    free(vm->natives);
    free(vm->handle_wrapper);
    vm_clear_error(vm);
    free(vm);
}

void hml_vm_declare_global(HmlVM *vm, const char *name) {
    (void)vm;
    name_resolve_add_host_global(name);
}

static int valid_identifier(const char *name) {
    if (!name || !*name) {
        return 0;
    }
    for (const char *p = name; *p; p++) {
        int ok = (*p == '_') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (p != name && *p >= '0' && *p <= '9');
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

HmlEmbedStatus hml_vm_define_native(HmlVM *vm, const char *name, HmlNativeFn fn, void *userdata) {
    if (!vm || !fn || !valid_identifier(name)) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    vm_clear_error(vm);

    int index = -1;
    for (int i = 0; i < vm->num_natives; i++) {
        if (strcmp(vm->natives[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (vm->num_natives == vm->natives_capacity) {
            vm->natives_capacity = vm->natives_capacity ? vm->natives_capacity * 2 : 8;
            vm->natives = hml_xrealloc(vm->natives, sizeof(HmlNative) * (size_t)vm->natives_capacity);
        }
        index = vm->num_natives++;
        vm->natives[index].name = hml_xstrdup(name);
    }
    vm->natives[index].fn = fn;
    vm->natives[index].userdata = userdata;

    /* A Hemlock wrapper that packs its arguments and names this VM + slot */
    char src[HML_EMBED_ERROR_SIZE];
    snprintf(src, sizeof(src), "fn %s(...args) { return " HML_EMBED_HOST_FN "(%d, %d, args); }",
             name, vm->id, index);
    name_resolve_add_host_global(name);
    return hml_vm_eval(vm, src, "<native>");
}

HmlEmbedStatus hml_vm_define_global(HmlVM *vm, const char *name, const HmlEmbedValue *value) {
    if (!vm || !valid_identifier(name)) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    vm_clear_error(vm);
    int depth = vm->ctx->call_stack.count;
    Value v = to_hemlock(vm, value, 0);
    if (vm->ctx->exception_state.is_throwing) {
        vm_capture_exception(vm, depth);
        return HML_EMBED_RUNTIME_ERROR;
    }
    env_define(vm->global_env, name, v, 0, vm->ctx);
    VALUE_RELEASE(v);
    name_resolve_add_host_global(name);
    return HML_EMBED_OK;
}

void hml_vm_set_handle_wrapper(HmlVM *vm, const char *function_name) {
    if (!vm) {
        return;
    }
    free(vm->handle_wrapper);
    vm->handle_wrapper = function_name ? hml_xstrdup(function_name) : NULL;
}

static void declare_env_names(Environment *env) {
    for (int i = 0; i < env->count; i++) {
        if (env->names[i]) {
            name_resolve_add_host_global(env->names[i]);
        }
    }
}

HmlEmbedStatus hml_vm_eval(HmlVM *vm, const char *source, const char *name) {
    if (!vm || !source) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    vm_clear_error(vm);
    HmlEmbedStatus status;
    /* Preludes are host code: they may use globals not declared yet */
    Module *module = build_module(vm, source, name ? name : "<prelude>.hml", 0, &status);
    if (!module) {
        return status;
    }
    vm->preludes = hml_xrealloc((void *)vm->preludes, sizeof(Module *) * (size_t)(vm->num_preludes + 1));
    vm->preludes[vm->num_preludes++] = module;

    int depth = vm->ctx->call_stack.count;
    if (execute_module_into(module, vm->cache, vm->global_env, vm->global_env, vm->ctx) != 0 ||
        vm->ctx->exception_state.is_throwing) {
        vm_capture_exception_in(vm, depth, module->absolute_path);
        return HML_EMBED_RUNTIME_ERROR;
    }
    declare_env_names(vm->global_env);
    return HML_EMBED_OK;
}

/* ========== Scripts ========== */

static int required_params(Expr *fn) {
    int n = 0;
    for (int i = 0; i < fn->as.function.num_params; i++) {
        if (!fn->as.function.param_defaults || !fn->as.function.param_defaults[i]) {
            n++;
        }
    }
    return n;
}

/* Annotation as written for the simple cases a host can map ("f64",
   "string", "array<i32>", "Vector2"); NULL for none or anything fancier. */
static char *render_type(Type *t) {
    if (!t) {
        return NULL;
    }
    static const char *const scalar[] = {
        [TYPE_I8] = "i8", [TYPE_I16] = "i16", [TYPE_I32] = "i32", [TYPE_I64] = "i64",
        [TYPE_U8] = "u8", [TYPE_U16] = "u16", [TYPE_U32] = "u32", [TYPE_U64] = "u64",
        [TYPE_F32] = "f32", [TYPE_F64] = "f64", [TYPE_BOOL] = "bool", [TYPE_STRING] = "string",
        [TYPE_RUNE] = "rune", [TYPE_PTR] = "ptr", [TYPE_BUFFER] = "buffer", [TYPE_NULL] = "null",
        [TYPE_GENERIC_OBJECT] = "object",
    };
    char buf[HML_EMBED_ERROR_SIZE];
    if ((size_t)t->kind < sizeof(scalar) / sizeof(scalar[0]) && scalar[t->kind]) {
        snprintf(buf, sizeof(buf), "%s%s", scalar[t->kind], t->nullable ? "?" : "");
    } else if ((t->kind == TYPE_CUSTOM_OBJECT || t->kind == TYPE_ENUM) && t->type_name) {
        snprintf(buf, sizeof(buf), "%s%s", t->type_name, t->nullable ? "?" : "");
    } else if (t->kind == TYPE_ARRAY) {
        char *elem = render_type(t->element_type);
        snprintf(buf, sizeof(buf), elem ? "array<%s>%s" : "array%s%s", elem ? elem : "", t->nullable ? "?" : "");
        free(elem);
    } else {
        return NULL;
    }
    return hml_xstrdup(buf);
}

static void decl_free(HmlDecl *d) {
    free(d->name);
    free(d->type_name);
}

static void script_add_decl(HmlScript *s, Stmt *decl, int exported, int line) {
    HmlDecl d;
    memset(&d, 0, sizeof(d));
    if (decl->type == STMT_LET) {
        d.name = decl->as.let.name;
        d.init = decl->as.let.value;
        d.annotation = decl->as.let.type_annotation;
    } else if (decl->type == STMT_CONST) {
        d.name = decl->as.const_stmt.name;
        d.init = decl->as.const_stmt.value;
        d.annotation = decl->as.const_stmt.type_annotation;
        d.is_const = 1;
    } else {
        return;
    }
    d.name = hml_xstrdup(d.name);
    d.line = decl->line > 0 ? decl->line : line;
    d.exported = exported;
    d.type_name = render_type(d.annotation);

    if (d.init && d.init->type == EXPR_FUNCTION) {
        s->fns = hml_xrealloc(s->fns, sizeof(HmlDecl) * (size_t)(s->num_fns + 1));
        s->fns[s->num_fns++] = d;
    } else {
        s->vars = hml_xrealloc(s->vars, sizeof(HmlDecl) * (size_t)(s->num_vars + 1));
        s->vars[s->num_vars++] = d;
    }
}

HmlScript *hml_script_compile(HmlVM *vm, const char *source, const char *path, HmlEmbedStatus *status) {
    HmlEmbedStatus local;
    if (!status) {
        status = &local;
    }
    if (!vm || !source) {
        *status = HML_EMBED_BAD_ARGUMENT;
        return NULL;
    }
    vm_clear_error(vm);
    Module *module = build_module(vm, source, path, vm->name_check, status);
    if (!module) {
        return NULL;
    }

    HmlScript *s = hml_xcalloc(1, sizeof(HmlScript));
    s->vm = vm;
    s->ref_count = 1;
    s->module = module;
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            script_add_decl(s, stmt->as.export_stmt.declaration, 1, stmt->line);
        } else {
            script_add_decl(s, stmt, 0, stmt->line);
        }
    }
    return s;
}

void hml_script_retain(HmlScript *script) {
    if (script) {
        script->ref_count++;
    }
}

void hml_script_release(HmlScript *script) {
    if (!script || --script->ref_count > 0) {
        return;
    }
    for (int i = 0; i < script->num_fns; i++) {
        decl_free(&script->fns[i]);
    }
    for (int i = 0; i < script->num_vars; i++) {
        decl_free(&script->vars[i]);
    }
    free(script->fns);
    free(script->vars);
    module_free_owned(script->module);
    free(script);
}

#define FN_AT(s, i) (((s) && (i) >= 0 && (i) < (s)->num_fns) ? &(s)->fns[i] : NULL)
#define VAR_AT(s, i) (((s) && (i) >= 0 && (i) < (s)->num_vars) ? &(s)->vars[i] : NULL)

int hml_script_function_count(const HmlScript *s) { return s ? s->num_fns : 0; }
const char *hml_script_function_name(const HmlScript *s, int i) {
    const HmlDecl *d = FN_AT(s, i);
    return d ? d->name : NULL;
}
int hml_script_function_arity(const HmlScript *s, int i) {
    const HmlDecl *d = FN_AT(s, i);
    return d ? required_params(d->init) : 0;
}
int hml_script_function_param_count(const HmlScript *s, int i) {
    const HmlDecl *d = FN_AT(s, i);
    return d ? d->init->as.function.num_params : 0;
}
const char *hml_script_function_param_name(const HmlScript *s, int i, int param) {
    const HmlDecl *d = FN_AT(s, i);
    if (!d || param < 0 || param >= d->init->as.function.num_params) {
        return NULL;
    }
    return d->init->as.function.param_names[param];
}
int hml_script_function_has_rest(const HmlScript *s, int i) {
    const HmlDecl *d = FN_AT(s, i);
    return d && d->init->as.function.rest_param != NULL;
}
int hml_script_function_line(const HmlScript *s, int i) {
    const HmlDecl *d = FN_AT(s, i);
    return d ? d->line : 0;
}
int hml_script_variable_count(const HmlScript *s) { return s ? s->num_vars : 0; }
const char *hml_script_variable_name(const HmlScript *s, int i) {
    const HmlDecl *d = VAR_AT(s, i);
    return d ? d->name : NULL;
}
int hml_script_variable_exported(const HmlScript *s, int i) {
    const HmlDecl *d = VAR_AT(s, i);
    return d ? d->exported : 0;
}
int hml_script_variable_is_const(const HmlScript *s, int i) {
    const HmlDecl *d = VAR_AT(s, i);
    return d ? d->is_const : 0;
}
int hml_script_variable_line(const HmlScript *s, int i) {
    const HmlDecl *d = VAR_AT(s, i);
    return d ? d->line : 0;
}
const char *hml_script_variable_type(const HmlScript *s, int i) {
    const HmlDecl *d = VAR_AT(s, i);
    return d ? d->type_name : NULL;
}

/* Evaluate a literal initializer without running anything. 0 if `e` is
   not made of literals only. */
static int literal_value(const Expr *e, int want_float, HmlEmbedValue *out, int depth) {
    if (!e || depth > HML_EMBED_MAX_VALUE_DEPTH) {
        return 0;
    }
    switch (e->type) {
        case EXPR_NUMBER:
            if (e->as.number.is_float || want_float) {
                *out = hml_embed_float(e->as.number.is_float ? e->as.number.float_value
                                       : e->as.number.is_u64 ? (double)e->as.number.uint_value
                                       : (double)e->as.number.int_value);
            } else if (e->as.number.is_u64) {
                *out = hml_embed_int(e->as.number.uint_value > (uint64_t)INT64_MAX
                                     ? INT64_MAX : (int64_t)e->as.number.uint_value);
            } else {
                *out = hml_embed_int(e->as.number.int_value);
            }
            return 1;
        case EXPR_BOOL:
            *out = hml_embed_bool(e->as.boolean);
            return 1;
        case EXPR_STRING:
            *out = hml_embed_cstring(e->as.string);
            return 1;
        case EXPR_RUNE:
            *out = hml_embed_int(e->as.rune);
            return 1;
        case EXPR_NULL:
            *out = hml_embed_null();
            return 1;
        case EXPR_UNARY:
            if (e->as.unary.op == UNARY_NEGATE && e->as.unary.operand &&
                e->as.unary.operand->type == EXPR_NUMBER &&
                literal_value(e->as.unary.operand, want_float, out, depth + 1)) {
                if (out->kind == HML_EMBED_FLOAT) {
                    out->as.f = -out->as.f;
                } else {
                    out->as.i = -out->as.i;
                }
                return 1;
            }
            return 0;
        case EXPR_ARRAY_LITERAL: {
            HmlEmbedValue arr = hml_embed_array(0);
            for (int i = 0; i < e->as.array_literal.num_elements; i++) {
                HmlEmbedValue item;
                if (!literal_value(e->as.array_literal.elements[i], 0, &item, depth + 1)) {
                    hml_embed_value_free(&arr);
                    return 0;
                }
                hml_embed_array_push(&arr, item);
            }
            *out = arr;
            return 1;
        }
        case EXPR_OBJECT_LITERAL: {
            HmlEmbedValue obj = hml_embed_object(NULL, 0);
            for (int i = 0; i < e->as.object_literal.num_fields; i++) {
                HmlEmbedValue item;
                if (!e->as.object_literal.field_names[i] ||
                    !literal_value(e->as.object_literal.field_values[i], 0, &item, depth + 1)) {
                    hml_embed_value_free(&obj);
                    return 0;
                }
                hml_embed_object_set(&obj, e->as.object_literal.field_names[i], item);
            }
            *out = obj;
            return 1;
        }
        default:
            return 0;
    }
}

HmlEmbedStatus hml_script_variable_default(const HmlScript *s, int i, HmlEmbedValue *out) {
    if (out) {
        *out = hml_embed_null();
    }
    const HmlDecl *d = VAR_AT(s, i);
    if (!d || !out) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    if (!d->init) {
        return HML_EMBED_OK;   /* `let x;` is null */
    }
    int want_float = d->annotation && (d->annotation->kind == TYPE_F32 || d->annotation->kind == TYPE_F64);
    return literal_value(d->init, want_float, out, 0) ? HML_EMBED_OK : HML_EMBED_NOT_FOUND;
}

int hml_script_find_variable(const HmlScript *s, const char *name) {
    if (!s || !name) {
        return -1;
    }
    for (int i = 0; i < s->num_vars; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int hml_script_find_function(const HmlScript *s, const char *name) {
    if (!s || !name) {
        return -1;
    }
    for (int i = 0; i < s->num_fns; i++) {
        if (strcmp(s->fns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int hml_script_has_function(const HmlScript *s, const char *name) {
    return hml_script_find_function(s, name) >= 0;
}

/* ========== Instances ========== */

/* Index of `name` bound directly in `env` (not its parents), or -1 */
static int env_own_index(Environment *env, const char *name) {
    for (int i = env->count - 1; i >= 0; i--) {
        if (env->names[i] && strcmp(env->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

HmlInstance *hml_instance_new(HmlScript *script, const char *const *names,
                              const HmlEmbedValue *values, int num_predefined,
                              HmlEmbedStatus *status) {
    HmlEmbedStatus local;
    if (!status) {
        status = &local;
    }
    if (!script || (num_predefined > 0 && (!names || !values))) {
        *status = HML_EMBED_BAD_ARGUMENT;
        return NULL;
    }
    HmlVM *vm = script->vm;
    vm_clear_error(vm);
    int depth = vm->ctx->call_stack.count;

    Environment *env = env_new(vm->global_env);
    for (int i = 0; i < num_predefined; i++) {
        if (!valid_identifier(names[i])) {
            env_release(env);
            vm_set_error(vm, "invalid predefined name");
            *status = HML_EMBED_BAD_ARGUMENT;
            return NULL;
        }
        Value v = to_hemlock(vm, &values[i], 0);
        if (vm->ctx->exception_state.is_throwing) {
            VALUE_RELEASE(v);
            vm_capture_exception(vm, depth);
            env_release(env);
            *status = HML_EMBED_RUNTIME_ERROR;
            return NULL;
        }
        env_define(env, names[i], v, 1, vm->ctx);
        VALUE_RELEASE(v);
    }

    if (execute_module_into(script->module, vm->cache, vm->global_env, env, vm->ctx) != 0 ||
        vm->ctx->exception_state.is_throwing) {
        vm_capture_exception_in(vm, depth, script->module->absolute_path);
        env_break_cycles(env);
        env_release(env);
        *status = HML_EMBED_RUNTIME_ERROR;
        return NULL;
    }

    HmlInstance *inst = hml_xcalloc(1, sizeof(HmlInstance));
    inst->script = script;
    inst->env = env;
    hml_script_retain(script);
    *status = HML_EMBED_OK;
    return inst;
}

void hml_instance_free(HmlInstance *inst) {
    if (!inst) {
        return;
    }
    env_break_cycles(inst->env);
    env_release(inst->env);
    hml_script_release(inst->script);
    free(inst);
}

HmlScript *hml_instance_script(HmlInstance *inst) {
    return inst ? inst->script : NULL;
}

int hml_instance_has_function(HmlInstance *inst, const char *name) {
    if (!inst || !name) {
        return 0;
    }
    int idx = env_own_index(inst->env, name);
    if (idx < 0) {
        return 0;
    }
    ValueType t = inst->env->values[idx].type;
    return t == VAL_FUNCTION || t == VAL_BUILTIN_FN || t == VAL_FFI_FUNCTION;
}

HmlEmbedStatus hml_instance_call(HmlInstance *inst, const char *name,
                                 const HmlEmbedValue *args, int num_args,
                                 HmlEmbedValue *result) {
    if (result) {
        *result = hml_embed_null();
    }
    if (!inst || !name || num_args < 0 || (num_args > 0 && !args)) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    HmlVM *vm = inst->script->vm;
    vm_clear_error(vm);
    if (!hml_instance_has_function(inst, name)) {
        vm_set_error(vm, "no function '%s' in this script", name);
        return HML_EMBED_NOT_FOUND;
    }
    if (num_args > HML_EMBED_MAX_CALL_ARGS) {
        vm_set_error(vm, "too many arguments (%d, max %d)", num_args, HML_EMBED_MAX_CALL_ARGS);
        return HML_EMBED_BAD_ARGUMENT;
    }

    int depth = vm->ctx->call_stack.count;
    Value hargs[HML_EMBED_MAX_CALL_ARGS];
    int converted = 0;
    for (; converted < num_args; converted++) {
        hargs[converted] = to_hemlock(vm, &args[converted], 0);
        if (vm->ctx->exception_state.is_throwing) {
            VALUE_RELEASE(hargs[converted]);
            break;
        }
    }

    Value out = val_null();
    if (!vm->ctx->exception_state.is_throwing) {
        Value fn = inst->env->values[env_own_index(inst->env, name)];
        VALUE_RETAIN(fn);
        SourceScope scope = source_enter(inst->script->module->absolute_path);
        out = call_value(vm, inst->env, fn, hargs, num_args);
        source_leave(scope);
        VALUE_RELEASE(fn);
    }
    for (int i = 0; i < converted; i++) {
        VALUE_RELEASE(hargs[i]);
    }

    if (vm->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(out);
        vm_capture_exception_in(vm, depth, inst->script->module->absolute_path);
        return HML_EMBED_RUNTIME_ERROR;
    }
    if (result) {
        *result = to_host(vm, out, 0);
    }
    VALUE_RELEASE(out);
    return HML_EMBED_OK;
}

HmlEmbedStatus hml_instance_get(HmlInstance *inst, const char *name, HmlEmbedValue *out) {
    if (out) {
        *out = hml_embed_null();
    }
    if (!inst || !name || !out) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    HmlVM *vm = inst->script->vm;
    vm_clear_error(vm);
    int idx = env_own_index(inst->env, name);
    if (idx < 0) {
        vm_set_error(vm, "no variable '%s' in this script", name);
        return HML_EMBED_NOT_FOUND;
    }
    *out = to_host(vm, inst->env->values[idx], 0);
    return HML_EMBED_OK;
}

HmlEmbedStatus hml_instance_set(HmlInstance *inst, const char *name, const HmlEmbedValue *value) {
    if (!inst || !name || !value) {
        return HML_EMBED_BAD_ARGUMENT;
    }
    HmlVM *vm = inst->script->vm;
    vm_clear_error(vm);
    int idx = env_own_index(inst->env, name);
    if (idx < 0) {
        vm_set_error(vm, "no variable '%s' in this script", name);
        return HML_EMBED_NOT_FOUND;
    }
    int depth = vm->ctx->call_stack.count;
    Value v = to_hemlock(vm, value, 0);
    if (!vm->ctx->exception_state.is_throwing) {
        /* `name = v;` semantics: const check, declared-type conversion.
           Takes ownership of v and hands back the stored value. */
        Value stored = env_assign_resolved(inst->env, 0, idx, v, vm->ctx);
        VALUE_RELEASE(stored);
    } else {
        VALUE_RELEASE(v);
    }
    if (vm->ctx->exception_state.is_throwing) {
        vm_capture_exception_in(vm, depth, inst->script->module->absolute_path);
        return HML_EMBED_RUNTIME_ERROR;
    }
    return HML_EMBED_OK;
}

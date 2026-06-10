/*
 * Hemlock Runtime Library - Function Call Builtins
 *
 * This file implements:
 * - Exception handling (throw, try/catch)
 * - Defer support
 * - Function calls (hml_call_function, hml_call_method)
 * - Call stack tracking
 */

#include "builtins_internal.h"

// ========== EXCEPTION HANDLING ==========

HmlExceptionContext* hml_exception_push(void) {
    HmlExceptionContext *ctx = malloc(sizeof(HmlExceptionContext));
    ctx->is_active = 1;
    ctx->exception_value = hml_val_null();
    ctx->prev = g_exception_stack;
    g_exception_stack = ctx;
    return ctx;
}

void hml_exception_pop(void) {
    if (g_exception_stack) {
        HmlExceptionContext *ctx = g_exception_stack;
        g_exception_stack = ctx->prev;
        hml_release(&ctx->exception_value);
        free(ctx);
    }
}

void hml_throw(HmlValue exception_value) {
    if (!g_exception_stack || !g_exception_stack->is_active) {
        // Uncaught exception
        fprintf(stderr, "Uncaught exception: ");
        print_value_to(stderr, exception_value);
        fprintf(stderr, "\n");
        exit(1);
    }

    g_exception_stack->exception_value = exception_value;
    hml_retain(&g_exception_stack->exception_value);
    longjmp(g_exception_stack->exception_buf, 1);
}

HmlValue hml_exception_get_value(void) {
    if (g_exception_stack) {
        HmlValue v = g_exception_stack->exception_value;
        hml_retain(&v);
        return v;
    }
    return hml_val_null();
}

// Runtime error helper - throws catchable exception with formatted message
void hml_runtime_error(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    HmlValue error_msg = hml_val_string(buffer);
    hml_throw(error_msg);
}

// ========== ENV-FIRST BUILTIN WRAPPERS ==========
// When a builtin is used as a first-class value (e.g. the stdlib re-export
// `export let poll = __poll;` then called through the `poll` binding), the
// compiler wraps it as an HML_VAL_FUNCTION and hml_call_function dispatches it
// as f(env, args...). User functions have that env-first C signature, but these
// raw builtins do not — so the old wraps pointed straight at e.g. hml_poll and
// every argument shifted by one (env was passed as the first HmlValue, the real
// first arg as the second, ...). That mis-typed the first arg, producing errors
// like "poll() expects array as first argument". On Linux the resulting uncaught
// exception exited 0 (silently masked); on macOS it aborts (SIGABRT) — surfacing
// the bug. These thin shims give each builtin the env-first signature the
// function-value path expects. See codegen_expr_ident.c.
HmlValue hml_builtin_poll(HmlClosureEnv *env, HmlValue fds, HmlValue timeout) {
    (void)env; return hml_poll(fds, timeout);
}
HmlValue hml_builtin_open(HmlClosureEnv *env, HmlValue path, HmlValue mode) {
    (void)env; return hml_open(path, mode);
}
HmlValue hml_builtin_raise(HmlClosureEnv *env, HmlValue signum) {
    (void)env; return hml_raise(signum);
}
HmlValue hml_builtin_signal(HmlClosureEnv *env, HmlValue signum, HmlValue handler) {
    (void)env; return hml_signal(signum, handler);
}
HmlValue hml_builtin_string_concat_many(HmlClosureEnv *env, HmlValue arr) {
    (void)env; return hml_string_concat_many(arr);
}
HmlValue hml_builtin_task_debug_info(HmlClosureEnv *env, HmlValue task) {
    (void)env; hml_task_debug_info(task); return hml_val_null();
}

// ========== DEFER SUPPORT ==========

void hml_defer_push(HmlDeferFn fn, void *arg) {
    DeferEntry *entry = malloc(sizeof(DeferEntry));
    entry->fn = fn;
    entry->arg = arg;
    entry->next = g_defer_stack;
    g_defer_stack = entry;
}

void hml_defer_pop_and_execute(void) {
    if (g_defer_stack) {
        DeferEntry *entry = g_defer_stack;
        g_defer_stack = entry->next;
        entry->fn(entry->arg);
        free(entry);
    }
}

void hml_defer_execute_all(void) {
    while (g_defer_stack) {
        hml_defer_pop_and_execute();
    }
}

// Per-function defer frames: record the stack mark at function entry, drain
// only entries pushed after the mark on exit. This keeps a callee's return
// from running its caller's pending defers.
void* hml_defer_frame_begin(void) {
    return g_defer_stack;
}

void hml_defer_execute_frame(void *mark) {
    while (g_defer_stack && g_defer_stack != (DeferEntry *)mark) {
        hml_defer_pop_and_execute();
    }
}

// Helper for deferring HmlValue function calls
static void hml_defer_call_wrapper(void *arg) {
    HmlValue *fn_ptr = (HmlValue *)arg;
    HmlValue result = hml_call_function(*fn_ptr, NULL, 0);
    hml_release(&result);
    hml_release(fn_ptr);
    free(fn_ptr);
}

void hml_defer_push_call(HmlValue fn) {
    HmlValue *fn_copy = malloc(sizeof(HmlValue));
    *fn_copy = fn;
    hml_retain(fn_copy);
    hml_defer_push(hml_defer_call_wrapper, fn_copy);
}

// Structure to hold a deferred call with arguments
typedef struct {
    HmlValue fn;
    HmlValue *args;
    int num_args;
} HmlDeferCallWithArgs;

// Helper for deferring HmlValue function calls with arguments
static void hml_defer_call_with_args_wrapper(void *arg) {
    HmlDeferCallWithArgs *call = (HmlDeferCallWithArgs *)arg;
    HmlValue result = hml_call_function(call->fn, call->args, call->num_args);
    hml_release(&result);
    // Release all args
    for (int i = 0; i < call->num_args; i++) {
        hml_release(&call->args[i]);
    }
    hml_release(&call->fn);
    free(call->args);
    free(call);
}

void hml_defer_push_call_with_args(HmlValue fn, HmlValue *args, int num_args) {
    HmlDeferCallWithArgs *call = malloc(sizeof(HmlDeferCallWithArgs));
    call->fn = fn;
    hml_retain(&call->fn);
    call->num_args = num_args;
    call->args = malloc(sizeof(HmlValue) * num_args);
    for (int i = 0; i < num_args; i++) {
        call->args[i] = args[i];
        hml_retain(&call->args[i]);
    }
    hml_defer_push(hml_defer_call_with_args_wrapper, call);
}

// ========== FUNCTION CALLS ==========

// Pre-created null value for fast padding (avoids repeated function calls)
static const HmlValue HML_NULL_VAL = { .type = HML_VAL_NULL };

// Function pointer typedefs for dispatch (declared once for reuse)
typedef HmlValue (*HmlFn0)(HmlClosureEnv*);
typedef HmlValue (*HmlFn1)(HmlClosureEnv*, HmlValue);
typedef HmlValue (*HmlFn2)(HmlClosureEnv*, HmlValue, HmlValue);
typedef HmlValue (*HmlFn3)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn4)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn5)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn6)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn7)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn8)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);

// Hot path: dispatch function call with optimized branching
__attribute__((hot))
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args) {
    // Validate args pointer if we have arguments
    if (__builtin_expect(num_args > 0 && args == NULL, 0)) {
        hml_runtime_error("Function called with NULL args array");
    }

    // Fast path: builtin functions (common for stdlib)
    if (__builtin_expect(fn.type == HML_VAL_BUILTIN_FN, 0)) {
        return fn.as.as_builtin_fn(args, num_args);
    }

    // Main path: user-defined functions
    if (__builtin_expect(fn.type == HML_VAL_FUNCTION && fn.as.as_function != NULL, 1)) {
        HmlFunction *func = fn.as.as_function;
        void *fn_ptr = func->fn_ptr;

        // Null check (rare error case)
        if (__builtin_expect(fn_ptr == NULL, 0)) {
            hml_runtime_error("Function pointer is NULL");
        }

        int num_params = func->num_params;
        int num_required = func->num_required;
        int has_rest_param = func->has_rest_param;

        // Arity check (error cases are rare)
        const char *fn_name = func->name ? func->name : "<anonymous>";
        if (__builtin_expect(num_args < num_required, 0)) {
            if (has_rest_param) {
                hml_runtime_error("Function '%s' expects at least %d arguments, got %d", fn_name, num_required, num_args);
            } else {
                hml_runtime_error("Function '%s' expects %d arguments, got %d", fn_name, num_required, num_args);
            }
        }
        // Only check max args if no rest param
        if (__builtin_expect(!has_rest_param && num_args > num_params, 0)) {
            hml_runtime_error("Function '%s' expects %d arguments, got %d", fn_name, num_params, num_args);
        }

        HmlClosureEnv *env = (HmlClosureEnv*)func->closure_env;

        // Handle rest parameter: collect extra args into array
        // Function actually takes num_params + 1 params (last is rest array)
        if (has_rest_param) {
            HmlValue rest_array = hml_val_array();
            if (args != NULL) {
                for (int i = num_params; i < num_args; i++) {
                    hml_array_push(rest_array, args[i]);
                }
            }

            // Prepare padded args with rest array as last param
            HmlValue padded_args[8];
            int total_params = num_params + 1;  // Regular params + rest array

            // Copy provided args up to num_params
            int copy_count = (num_args < num_params) ? num_args : num_params;
            if (args != NULL) {
                for (int i = 0; i < copy_count; i++) {
                    padded_args[i] = args[i];
                }
            }
            // Fill remaining regular params with null
            for (int i = num_args; i < num_params; i++) {
                padded_args[i] = HML_NULL_VAL;
            }
            // Add rest array as last param
            padded_args[num_params] = rest_array;

            HmlValue result;
            switch (total_params) {
                case 1: result = ((HmlFn1)fn_ptr)(env, padded_args[0]); break;
                case 2: result = ((HmlFn2)fn_ptr)(env, padded_args[0], padded_args[1]); break;
                case 3: result = ((HmlFn3)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2]); break;
                case 4: result = ((HmlFn4)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3]); break;
                case 5: result = ((HmlFn5)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4]); break;
                case 6: result = ((HmlFn6)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5]); break;
                case 7: result = ((HmlFn7)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6]); break;
                case 8: result = ((HmlFn8)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6], padded_args[7]); break;
                default:
                    hml_runtime_error("Functions with more than 7 regular parameters + rest not supported");
                    result = hml_val_null();
            }
            hml_release(&rest_array);
            return result;
        }

        // Fast paths for common arities (0-3 params cover ~90% of functions)
        // Avoid padded_args array entirely when num_args == num_params
        if (__builtin_expect(num_args == num_params, 1)) {
            switch (num_params) {
                case 0: return ((HmlFn0)fn_ptr)(env);
                case 1: return ((HmlFn1)fn_ptr)(env, args[0]);
                case 2: return ((HmlFn2)fn_ptr)(env, args[0], args[1]);
                case 3: return ((HmlFn3)fn_ptr)(env, args[0], args[1], args[2]);
                case 4: return ((HmlFn4)fn_ptr)(env, args[0], args[1], args[2], args[3]);
                case 5: return ((HmlFn5)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4]);
                case 6: return ((HmlFn6)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5]);
                case 7: return ((HmlFn7)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
                case 8: return ((HmlFn8)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            }
        }

        // Slow path: need to pad args with nulls for optional parameters
        HmlValue padded_args[8];

        // Copy provided args (use memcpy for larger copies)
        if (num_args <= 4) {
            for (int i = 0; i < num_args; i++) {
                padded_args[i] = args[i];
            }
        } else {
            memcpy(padded_args, args, num_args * sizeof(HmlValue));
        }

        // Fill remaining with null (use static null value)
        for (int i = num_args; i < num_params; i++) {
            padded_args[i] = HML_NULL_VAL;
        }

        switch (num_params) {
            case 1: return ((HmlFn1)fn_ptr)(env, padded_args[0]);
            case 2: return ((HmlFn2)fn_ptr)(env, padded_args[0], padded_args[1]);
            case 3: return ((HmlFn3)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2]);
            case 4: return ((HmlFn4)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3]);
            case 5: return ((HmlFn5)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4]);
            case 6: return ((HmlFn6)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5]);
            case 7: return ((HmlFn7)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6]);
            case 8: return ((HmlFn8)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6], padded_args[7]);
            default:
                hml_runtime_error("Functions with more than 8 arguments not supported");
        }
    }

    hml_runtime_error("Cannot call non-function value (type: %s)", hml_typeof_str(fn));
}

// Call a function with named arguments - reorders args to match parameter names
HmlValue hml_call_function_named(HmlValue fn, HmlValue *args, const char **arg_names, int num_args) {
    // If no named arguments or not a function, fall back to regular call
    if (arg_names == NULL || fn.type != HML_VAL_FUNCTION) {
        return hml_call_function(fn, args, num_args);
    }

    HmlFunction *func = fn.as.as_function;

    // Check if function has parameter names
    if (func->param_names == NULL) {
        hml_runtime_error("Cannot use named arguments with function '%s' (no parameter info)",
                         func->name ? func->name : "<anonymous>");
    }

    // Allocate reordered args array
    HmlValue reordered[8];  // Stack allocation for up to 8 params
    HmlValue *reordered_args = (func->num_params <= 8) ? reordered : malloc(sizeof(HmlValue) * func->num_params);

    // Initialize to null
    for (int i = 0; i < func->num_params; i++) {
        reordered_args[i] = HML_NULL_VAL;
    }

    // Track which args have been used
    int used[8] = {0};  // Assumes max 8 args for simplicity
    int *arg_used = (num_args <= 8) ? used : calloc(num_args, sizeof(int));

    // First pass: place named arguments
    for (int i = 0; i < num_args; i++) {
        if (arg_names[i] != NULL) {
            // Find the parameter with this name
            int found = 0;
            for (int j = 0; j < func->num_params; j++) {
                if (func->param_names[j] && strcmp(func->param_names[j], arg_names[i]) == 0) {
                    if (reordered_args[j].type != HML_VAL_NULL) {
                        if (reordered_args != reordered) free(reordered_args);
                        if (arg_used != used) free(arg_used);
                        hml_runtime_error("Duplicate argument for parameter '%s'", func->param_names[j]);
                    }
                    reordered_args[j] = args[i];
                    arg_used[i] = 1;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (reordered_args != reordered) free(reordered_args);
                if (arg_used != used) free(arg_used);
                hml_runtime_error("Unknown parameter name '%s'", arg_names[i]);
            }
        }
    }

    // Second pass: place positional arguments in remaining slots
    int next_slot = 0;
    for (int i = 0; i < num_args; i++) {
        if (!arg_used[i]) {
            // Find next unfilled slot
            while (next_slot < func->num_params && reordered_args[next_slot].type != HML_VAL_NULL) {
                next_slot++;
            }
            if (next_slot < func->num_params) {
                reordered_args[next_slot] = args[i];
                next_slot++;
            }
        }
    }

    if (arg_used != used) free(arg_used);

    // Call with reordered args
    HmlValue result = hml_call_function(fn, reordered_args, func->num_params);

    if (reordered_args != reordered) free(reordered_args);

    return result;
}

// Thread-local self for method calls
__thread HmlValue hml_self = {0};

HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args) {
    // Handle string methods
    if (obj.type == HML_VAL_STRING) {
        if (strcmp(method, "chars") == 0 && num_args == 0) {
            return hml_string_chars(obj);
        }
        if (strcmp(method, "bytes") == 0 && num_args == 0) {
            return hml_string_bytes(obj);
        }
        if (strcmp(method, "to_bytes") == 0 && num_args == 0) {
            return hml_string_to_bytes(obj);
        }
        if (strcmp(method, "substr") == 0 && num_args == 2) {
            return hml_string_substr(obj, args[0], args[1]);
        }
        if (strcmp(method, "substr") == 0 && num_args == 1) {
            return hml_string_substr_from(obj, args[0]);
        }
        if (strcmp(method, "slice") == 0 && num_args == 2) {
            return hml_string_slice(obj, args[0], args[1]);
        }
        if (strcmp(method, "slice") == 0 && num_args == 1) {
            return hml_string_slice(obj, args[0], hml_string_length(obj));
        }
        if (strcmp(method, "find") == 0 && num_args == 1) {
            return hml_string_find(obj, args[0]);
        }
        if (strcmp(method, "rfind") == 0 && num_args == 1) {
            return hml_string_rfind(obj, args[0]);
        }
        if (strcmp(method, "contains") == 0 && num_args == 1) {
            return hml_string_contains(obj, args[0]);
        }
        if (strcmp(method, "split") == 0 && num_args == 1) {
            return hml_string_split(obj, args[0]);
        }
        if (strcmp(method, "trim") == 0 && num_args == 0) {
            return hml_string_trim(obj);
        }
        if ((strcmp(method, "to_upper") == 0 || strcmp(method, "upper") == 0) && num_args == 0) {
            return hml_string_to_upper(obj);
        }
        if ((strcmp(method, "to_lower") == 0 || strcmp(method, "lower") == 0) && num_args == 0) {
            return hml_string_to_lower(obj);
        }
        if (strcmp(method, "starts_with") == 0 && num_args == 1) {
            return hml_string_starts_with(obj, args[0]);
        }
        if (strcmp(method, "ends_with") == 0 && num_args == 1) {
            return hml_string_ends_with(obj, args[0]);
        }
        if (strcmp(method, "replace") == 0 && num_args == 2) {
            return hml_string_replace(obj, args[0], args[1]);
        }
        if (strcmp(method, "replace_all") == 0 && num_args == 2) {
            return hml_string_replace_all(obj, args[0], args[1]);
        }
        if (strcmp(method, "repeat") == 0 && num_args == 1) {
            return hml_string_repeat(obj, args[0]);
        }
        if (strcmp(method, "char_at") == 0 && num_args == 1) {
            return hml_string_char_at(obj, args[0]);
        }
        if (strcmp(method, "byte_at") == 0 && num_args == 1) {
            return hml_string_byte_at(obj, args[0]);
        }
        if (strcmp(method, "deserialize") == 0 && num_args == 0) {
            return hml_deserialize(obj);
        }
        hml_runtime_error("String has no method '%s'", method);
    }

    // Handle array methods
    if (obj.type == HML_VAL_ARRAY) {
        if (strcmp(method, "push") == 0 && num_args == 1) {
            hml_array_push(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "pop") == 0 && num_args == 0) {
            return hml_array_pop(obj);
        }
        if (strcmp(method, "shift") == 0 && num_args == 0) {
            return hml_array_shift(obj);
        }
        if (strcmp(method, "unshift") == 0 && num_args == 1) {
            hml_array_unshift(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "insert") == 0 && num_args == 2) {
            hml_array_insert(obj, args[0], args[1]);
            return hml_val_null();
        }
        if (strcmp(method, "remove") == 0 && num_args == 1) {
            return hml_array_remove(obj, args[0]);
        }
        if (strcmp(method, "find") == 0 && num_args == 1) {
            return hml_array_find(obj, args[0]);
        }
        if (strcmp(method, "contains") == 0 && num_args == 1) {
            return hml_array_contains(obj, args[0]);
        }
        if (strcmp(method, "slice") == 0 && num_args == 2) {
            return hml_array_slice(obj, args[0], args[1]);
        }
        if (strcmp(method, "join") == 0 && num_args == 1) {
            return hml_array_join(obj, args[0]);
        }
        if (strcmp(method, "concat") == 0 && num_args == 1) {
            return hml_array_concat(obj, args[0]);
        }
        if (strcmp(method, "reverse") == 0 && num_args == 0) {
            hml_array_reverse(obj);
            return hml_val_null();
        }
        if (strcmp(method, "first") == 0 && num_args == 0) {
            return hml_array_first(obj);
        }
        if (strcmp(method, "last") == 0 && num_args == 0) {
            return hml_array_last(obj);
        }
        if (strcmp(method, "clear") == 0 && num_args == 0) {
            hml_array_clear(obj);
            return hml_val_null();
        }
        if (strcmp(method, "map") == 0 && num_args == 1) {
            return hml_array_map(obj, args[0]);
        }
        if (strcmp(method, "filter") == 0 && num_args == 1) {
            return hml_array_filter(obj, args[0]);
        }
        if ((strcmp(method, "reduce") == 0) && (num_args == 1 || num_args == 2)) {
            HmlValue initial = (num_args == 2) ? args[1] : hml_val_null();
            return hml_array_reduce(obj, args[0], initial);
        }
        hml_runtime_error("Array has no method '%s'", method);
    }

    // Handle file methods
    if (obj.type == HML_VAL_FILE) {
        if (strcmp(method, "read") == 0 && (num_args == 0 || num_args == 1)) {
            HmlValue size = (num_args == 1) ? args[0] : hml_val_i32(0);
            return hml_file_read(obj, size);
        }
        if (strcmp(method, "read_bytes") == 0 && num_args == 1) {
            return hml_file_read_bytes(obj, args[0]);
        }
        if (strcmp(method, "read_binary") == 0 && (num_args == 0 || num_args == 1)) {
            HmlValue size = (num_args == 1) ? args[0] : hml_val_i32(-1);
            return hml_file_read_binary(obj, size);
        }
        if (strcmp(method, "write") == 0 && num_args == 1) {
            return hml_file_write(obj, args[0]);
        }
        if (strcmp(method, "write_bytes") == 0 && num_args == 1) {
            return hml_file_write_bytes(obj, args[0]);
        }
        if (strcmp(method, "seek") == 0 && num_args == 1) {
            return hml_file_seek(obj, args[0]);
        }
        if (strcmp(method, "tell") == 0 && num_args == 0) {
            return hml_file_tell(obj);
        }
        if (strcmp(method, "close") == 0 && num_args == 0) {
            hml_file_close(obj);
            return hml_val_null();
        }
        hml_runtime_error("File has no method '%s'", method);
    }

    // Handle channel methods
    if (obj.type == HML_VAL_CHANNEL) {
        if (strcmp(method, "send") == 0 && num_args == 1) {
            hml_channel_send(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "recv") == 0 && num_args == 0) {
            return hml_channel_recv(obj);
        }
        if (strcmp(method, "recv_timeout") == 0 && num_args == 1) {
            return hml_channel_recv_timeout(obj, args[0]);
        }
        if (strcmp(method, "send_timeout") == 0 && num_args == 2) {
            return hml_channel_send_timeout(obj, args[0], args[1]);
        }
        if (strcmp(method, "close") == 0 && num_args == 0) {
            hml_channel_close(obj);
            return hml_val_null();
        }
        hml_runtime_error("Channel has no method '%s'", method);
    }

    // Handle socket methods
    if (obj.type == HML_VAL_SOCKET) {
        if (strcmp(method, "bind") == 0 && num_args == 2) {
            hml_socket_bind(obj, args[0], args[1]);
            return hml_val_null();
        }
        if (strcmp(method, "listen") == 0 && num_args == 1) {
            hml_socket_listen(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "accept") == 0 && num_args == 0) {
            return hml_socket_accept(obj);
        }
        if (strcmp(method, "connect") == 0 && num_args == 2) {
            hml_socket_connect(obj, args[0], args[1]);
            return hml_val_null();
        }
        if (strcmp(method, "send") == 0 && num_args == 1) {
            return hml_socket_send(obj, args[0]);
        }
        if (strcmp(method, "recv") == 0 && num_args == 1) {
            return hml_socket_recv(obj, args[0]);
        }
        if (strcmp(method, "sendto") == 0 && num_args == 3) {
            return hml_socket_sendto(obj, args[0], args[1], args[2]);
        }
        if (strcmp(method, "recvfrom") == 0 && num_args == 1) {
            return hml_socket_recvfrom(obj, args[0]);
        }
        if (strcmp(method, "setsockopt") == 0 && num_args == 3) {
            hml_socket_setsockopt(obj, args[0], args[1], args[2]);
            return hml_val_null();
        }
        if (strcmp(method, "set_timeout") == 0 && num_args == 1) {
            hml_socket_set_timeout(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "set_nonblocking") == 0 && num_args == 1) {
            hml_socket_set_nonblocking(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "close") == 0 && num_args == 0) {
            hml_socket_close(obj);
            return hml_val_null();
        }
        hml_runtime_error("Socket has no method '%s'", method);
    }

    // Handle object methods
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Cannot call method '%s' on non-object (type: %s)",
                method, hml_typeof_str(obj));
    }

    // Get the method function from the object
    HmlValue fn = hml_object_get_field(obj, method);
    if (fn.type == HML_VAL_NULL) {
        // Fallback to built-in object methods if no custom method exists
        if (strcmp(method, "keys") == 0 && num_args == 0) {
            return hml_object_keys(obj);
        }
        if (strcmp(method, "has") == 0 && num_args == 1) {
            if (args[0].type == HML_VAL_STRING) {
                return hml_val_bool(hml_object_has_field(obj, args[0].as.as_string->data));
            }
            char key_buf[64];
            if (hml_value_coerce_to_key(args[0], key_buf, sizeof(key_buf))) {
                return hml_val_bool(hml_object_has_field(obj, key_buf));
            }
            hml_runtime_error("Object.has() requires string, number, bool, or rune argument");
        }
        if (strcmp(method, "delete") == 0 && num_args == 1) {
            if (args[0].type == HML_VAL_STRING) {
                return hml_val_bool(hml_object_delete_field(obj, args[0].as.as_string->data));
            }
            char key_buf[64];
            if (hml_value_coerce_to_key(args[0], key_buf, sizeof(key_buf))) {
                return hml_val_bool(hml_object_delete_field(obj, key_buf));
            }
            hml_runtime_error("Object.delete() requires string, number, bool, or rune argument");
        }
        hml_runtime_error("Object has no method '%s'", method);
    }

    // Save previous self and set new one
    HmlValue prev_self = hml_self;
    hml_self = obj;
    hml_retain(&hml_self);

    // Call the method
    HmlValue result = hml_call_function(fn, args, num_args);

    // Restore previous self
    hml_release(&hml_self);
    hml_self = prev_self;

    hml_release(&fn);
    return result;
}

// ========== CALL STACK TRACKING ==========

// Thread-local call depth counter for stack overflow detection
// Exposed globally for inline macro access (hml_g_call_depth)
__thread int hml_g_call_depth = 0;

// Thread-local maximum call depth (can be modified at runtime)
// Initialized to default value, can be changed via set_stack_limit()
__thread int hml_g_max_call_depth = HML_MAX_CALL_DEPTH;

// Get the current stack limit
HmlValue hml_get_stack_limit(void) {
    return hml_val_i32(hml_g_max_call_depth);
}

// Set the stack limit (returns the old limit)
HmlValue hml_set_stack_limit(HmlValue limit) {
    int old_limit = hml_g_max_call_depth;
    int new_limit = hml_to_i32(limit);
    if (new_limit <= 0) {
        hml_runtime_error("set_stack_limit() expects a positive integer");
    }
    hml_g_max_call_depth = new_limit;
    return hml_val_i32(old_limit);
}

// Builtin wrapper versions (for function references)
HmlValue hml_builtin_get_stack_limit(HmlClosureEnv *env) {
    (void)env;
    return hml_get_stack_limit();
}

HmlValue hml_builtin_set_stack_limit(HmlClosureEnv *env, HmlValue limit) {
    (void)env;
    return hml_set_stack_limit(limit);
}

// Function versions for backwards compatibility (macros are faster)
void hml_call_enter(void) {
    HML_CALL_ENTER();
}

void hml_call_exit(void) {
    HML_CALL_EXIT();
}


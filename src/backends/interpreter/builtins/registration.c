#include "internal.h"
#include "hemlock_limits.h"
#include "builtin_globals.h"
#ifndef _WIN32
#include <poll.h>
#include <sys/mman.h>
#endif

// Structure to hold builtin function info
typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinInfo;

// The builtin tables live in include/builtin_globals.h so that the frontend
// name resolver sees exactly the names registered here (see that header).
#define HML_REG_FN(name, cfn) {name, cfn},
static BuiltinInfo builtins[] = {
    HML_BUILTIN_FNS(HML_REG_FN)
    {NULL, NULL}  // Sentinel
};
#undef HML_REG_FN

Value val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx) {
    // All three constant tables come from include/builtin_globals.h, the same
    // lists the frontend name resolver reads. Type constants are registered
    // first so sizeof()/talloc() see them before any same-named builtin.
    //
    // NOTE: signal/socket/poll/mmap constants are exposed to stdlib only under
    // their __ prefixed names; the public spellings live in @stdlib/signal,
    // @stdlib/net and @stdlib/mmap. Math (sin, sqrt, ...), environment
    // (getenv, ...) and random (rand, seed) functions are likewise not global:
    // users import them from @stdlib/math, @stdlib/env and @stdlib/random.
#define HML_REG_TYPE(name, kind) env_set(env, name, val_type(kind), ctx);
    HML_BUILTIN_TYPE_CONSTS(HML_REG_TYPE)
#undef HML_REG_TYPE

#define HML_REG_I32(name, value) env_set(env, name, val_i32(value), ctx);
    HML_BUILTIN_I32_CONSTS(HML_REG_I32)
#undef HML_REG_I32

#define HML_REG_F64(name, value) env_set(env, name, val_f64(value), ctx);
    HML_BUILTIN_F64_CONSTS(HML_REG_F64)
#undef HML_REG_F64

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn), ctx);
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        Value str_val = val_string(argv[i]);
        array_push(args_array, str_val);
        value_release(str_val);  // array_push retains, so release our reference
    }
    env_set(env, "args", val_array(args_array), ctx);
    // Release caller's reference - env now owns the array
    array_release(args_array);
}

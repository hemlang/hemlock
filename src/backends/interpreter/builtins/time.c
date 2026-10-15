#include "internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Forward declarations for helper functions
static Value get_object_field(Object *obj, const char *name);
static void set_object_field(Object *obj, const char *name, Value value);

// Note: value_to_int64() is now defined globally in types.c

Value builtin_now(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "now() expects no arguments"); return val_null();
    }
    return val_i64((int64_t)time(NULL));
}

Value builtin_time_ms(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "time_ms() expects no arguments"); return val_null();
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return val_i64(ms);
}

Value builtin_sleep(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "sleep() expects 1 argument (seconds)"); return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "sleep() argument must be numeric"); return val_null();
    }
    double seconds = value_to_float(args[0]);
    if (seconds < 0) {
        runtime_error(ctx, "sleep() argument must be non-negative"); return val_null();
    }
    // Use nanosleep for more precise sleep
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * HML_NANOSECONDS_PER_SECOND);
    nanosleep(&req, NULL);
    return val_null();
}

// ========== FRAME LOOP ==========
//
// main_loop(callback, fps) drives per-frame work without sleep(). See
// runtime/src/builtins_mainloop.c for the full rationale: in a browser the
// sleep()-based game loop forces Emscripten Asyncify onto the whole module,
// and main_loop() exists so that a frame loop does not have to pay for it.
//
// The interpreter is native everywhere except the `make wasm-interpreter`
// playground build, so the loop below is the paced native one; the WASM
// interpreter hands the callback to the browser's frame scheduler instead
// (a blocking loop there would wedge the tab).

static Value g_main_loop_callback;
static int g_main_loop_active = 0;
static volatile int g_main_loop_stopping = 0;
static ExecutionContext *g_main_loop_ctx = NULL;

// Invoke a zero-argument Hemlock function value. Mirrors the callback path
// used by the array methods (see io/array_methods.c).
static Value main_loop_invoke(Value func, ExecutionContext *ctx) {
    Function *fn = func.as.as_function;
    if (fn->num_params != 0) {
        runtime_error(ctx, "main_loop() callback must take no arguments (takes %d)",
                      fn->num_params);
        return val_null();
    }

    Environment *call_env = env_new(fn->closure_env);
    ctx->return_state.is_returning = 0;
    eval_stmt(fn->body, call_env, ctx);
    Value result = ctx->return_state.is_returning ? ctx->return_state.return_value : val_null();
    ctx->return_state.is_returning = 0;
    env_release(call_env);
    return result;
}

static void main_loop_clear(void) {
    if (!g_main_loop_active) return;
    g_main_loop_active = 0;
    g_main_loop_stopping = 0;
    g_main_loop_ctx = NULL;
    VALUE_RELEASE(g_main_loop_callback);
    g_main_loop_callback = val_null();
}

#ifndef __EMSCRIPTEN__
// Only the native pacing loop needs a clock; the browser schedules frames.
static double main_loop_now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}
#endif

#ifdef __EMSCRIPTEN__
static void main_loop_tick(void *unused) {
    (void)unused;
    if (!g_main_loop_active || !g_main_loop_ctx) return;
    main_loop_invoke(g_main_loop_callback, g_main_loop_ctx);
}
#endif

Value builtin_main_loop(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "main_loop() expects 2 arguments (callback, fps)"); return val_null();
    }
    if (args[0].type != VAL_FUNCTION) {
        runtime_error(ctx, "main_loop() first argument must be a function"); return val_null();
    }
    if (!is_numeric(args[1])) {
        runtime_error(ctx, "main_loop() second argument must be numeric (fps)"); return val_null();
    }
    if (g_main_loop_active) {
        runtime_error(ctx, "main_loop() is already running (call main_loop_stop() first)");
        return val_null();
    }

    int64_t target_fps = value_to_int64(args[1]);
    if (target_fps < 0) {
        runtime_error(ctx, "main_loop() fps must be >= 0 (0 = let the host pick the rate)");
        return val_null();
    }

    g_main_loop_callback = args[0];
    VALUE_RETAIN(g_main_loop_callback);
    g_main_loop_active = 1;
    g_main_loop_stopping = 0;
    g_main_loop_ctx = ctx;

#ifdef __EMSCRIPTEN__
    // Non-blocking: the callback keeps running on the browser's frame clock
    // after this call returns, so the playground's eval() can finish. fps 0
    // means requestAnimationFrame.
    emscripten_set_main_loop_arg(main_loop_tick, NULL, (int)target_fps, 0);
#else
    double frame_ms = target_fps > 0 ? 1000.0 / (double)target_fps : 0.0;
    while (!g_main_loop_stopping) {
        double frame_start = main_loop_now_ms();

        main_loop_invoke(g_main_loop_callback, ctx);
        if (ctx->exception_state.is_throwing) break;

        if (g_main_loop_stopping) break;

        if (frame_ms > 0.0) {
            double remaining = frame_ms - (main_loop_now_ms() - frame_start);
            if (remaining > 0.0) {
                struct timespec req;
                req.tv_sec = (time_t)(remaining / 1000.0);
                req.tv_nsec = (long)((remaining - (double)req.tv_sec * 1000.0) * 1000000.0);
                nanosleep(&req, NULL);
            }
        }
    }
    main_loop_clear();
#endif
    return val_null();
}

Value builtin_main_loop_stop(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "main_loop_stop() expects no arguments"); return val_null();
    }
    if (!g_main_loop_active) return val_null();

    g_main_loop_stopping = 1;
#ifdef __EMSCRIPTEN__
    // Nothing will return to the native loop body under Emscripten, so the
    // teardown has to happen here.
    emscripten_cancel_main_loop();
    main_loop_clear();
#endif
    return val_null();
}

Value builtin_clock(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "clock() expects no arguments"); return val_null();
    }
    // Returns CPU time in seconds as f64
    return val_f64((double)clock() / CLOCKS_PER_SEC);
}

// Convert Unix timestamp to local time components
Value builtin_localtime(Value *args, int num_args, ExecutionContext *ctx) {
    time_t timestamp;
    struct tm *tm_info;
    Object *obj;

    if (num_args != 1) {
        runtime_error(ctx, "localtime() expects 1 argument (timestamp)"); return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "localtime() argument must be numeric"); return val_null();
    }

    timestamp = (time_t)value_to_int64(args[0]);
    tm_info = localtime(&timestamp);

    if (!tm_info) {
        runtime_error(ctx, "localtime() failed to convert timestamp"); return val_null();
    }

    // Create object with time components
    obj = object_new(NULL, 16);
    set_object_field(obj, "year", val_i32(tm_info->tm_year + 1900));
    set_object_field(obj, "month", val_i32(tm_info->tm_mon + 1));  // 1-12
    set_object_field(obj, "day", val_i32(tm_info->tm_mday));
    set_object_field(obj, "hour", val_i32(tm_info->tm_hour));
    set_object_field(obj, "minute", val_i32(tm_info->tm_min));
    set_object_field(obj, "second", val_i32(tm_info->tm_sec));
    set_object_field(obj, "weekday", val_i32(tm_info->tm_wday));  // 0=Sunday
    set_object_field(obj, "yearday", val_i32(tm_info->tm_yday + 1));  // 1-366
    set_object_field(obj, "isdst", val_bool(tm_info->tm_isdst > 0));

    return val_object(obj);
}

// Convert Unix timestamp to UTC time components
Value builtin_gmtime(Value *args, int num_args, ExecutionContext *ctx) {
    time_t timestamp;
    struct tm *tm_info;
    Object *obj;

    if (num_args != 1) {
        runtime_error(ctx, "gmtime() expects 1 argument (timestamp)"); return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "gmtime() argument must be numeric"); return val_null();
    }

    timestamp = (time_t)value_to_int64(args[0]);
    tm_info = gmtime(&timestamp);

    if (!tm_info) {
        runtime_error(ctx, "gmtime() failed to convert timestamp"); return val_null();
    }

    // Create object with time components
    obj = object_new(NULL, 16);
    set_object_field(obj, "year", val_i32(tm_info->tm_year + 1900));
    set_object_field(obj, "month", val_i32(tm_info->tm_mon + 1));  // 1-12
    set_object_field(obj, "day", val_i32(tm_info->tm_mday));
    set_object_field(obj, "hour", val_i32(tm_info->tm_hour));
    set_object_field(obj, "minute", val_i32(tm_info->tm_min));
    set_object_field(obj, "second", val_i32(tm_info->tm_sec));
    set_object_field(obj, "weekday", val_i32(tm_info->tm_wday));  // 0=Sunday
    set_object_field(obj, "yearday", val_i32(tm_info->tm_yday + 1));  // 1-366
    set_object_field(obj, "isdst", val_bool(0));  // UTC has no DST

    return val_object(obj);
}

// Helper function to get field from object
static Value get_object_field(Object *obj, const char *name) {
    int i;
    for (i = 0; i < obj->num_fields; i++) {
        if (strcmp(obj->fields[i].name, name) == 0) {
            return obj->fields[i].value;
        }
    }
    return val_null();
}

// Helper function to set field on object
static void set_object_field(Object *obj, const char *name, Value value) {
    if (obj->num_fields >= obj->capacity) {
        int new_capacity = obj->capacity * 2;
        FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
        if (!new_fields) {
            return;  // Keep original data on allocation failure
        }
    }
    obj->fields[obj->num_fields].name = strdup(name);
    obj->fields[obj->num_fields].value = value;
    obj->num_fields++;
}

// Convert time components to Unix timestamp
Value builtin_mktime(Value *args, int num_args, ExecutionContext *ctx) {
    Object *obj;
    struct tm tm_info = {0};
    Value year_val, month_val, day_val, hour_val, minute_val, second_val;
    time_t timestamp;

    if (num_args != 1) {
        runtime_error(ctx, "mktime() expects 1 argument (time components object)"); return val_null();
    }
    if (args[0].type != VAL_OBJECT) {
        runtime_error(ctx, "mktime() argument must be an object"); return val_null();
    }

    obj = args[0].as.as_object;

    // Extract fields from object
    year_val = get_object_field(obj, "year");
    month_val = get_object_field(obj, "month");
    day_val = get_object_field(obj, "day");
    hour_val = get_object_field(obj, "hour");
    minute_val = get_object_field(obj, "minute");
    second_val = get_object_field(obj, "second");

    if (year_val.type == VAL_NULL || month_val.type == VAL_NULL ||
        day_val.type == VAL_NULL) {
        runtime_error(ctx, "mktime() requires year, month, and day fields"); return val_null();
    }

    tm_info.tm_year = value_to_int(year_val) - 1900;
    tm_info.tm_mon = value_to_int(month_val) - 1;  // 0-11
    tm_info.tm_mday = value_to_int(day_val);
    tm_info.tm_hour = hour_val.type != VAL_NULL ? value_to_int(hour_val) : 0;
    tm_info.tm_min = minute_val.type != VAL_NULL ? value_to_int(minute_val) : 0;
    tm_info.tm_sec = second_val.type != VAL_NULL ? value_to_int(second_val) : 0;
    tm_info.tm_isdst = -1;  // Auto-determine DST

    timestamp = mktime(&tm_info);
    if (timestamp == -1) {
        runtime_error(ctx, "mktime() failed to convert time components"); return val_null();
    }

    return val_i64((int64_t)timestamp);
}

// Format date/time using strftime
Value builtin_strftime(Value *args, int num_args, ExecutionContext *ctx) {
    const char *format;
    Object *obj;
    struct tm tm_info = {0};
    Value year_val, month_val, day_val, hour_val, minute_val, second_val;
    Value weekday_val, yearday_val;
    char buffer[256];
    size_t len;

    if (num_args != 2) {
        runtime_error(ctx, "strftime() expects 2 arguments (format, time components)"); return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "strftime() format argument must be a string"); return val_null();
    }
    if (args[1].type != VAL_OBJECT) {
        runtime_error(ctx, "strftime() time components argument must be an object"); return val_null();
    }

    format = args[0].as.as_string->data;
    obj = args[1].as.as_object;

    // Extract fields from object
    year_val = get_object_field(obj, "year");
    month_val = get_object_field(obj, "month");
    day_val = get_object_field(obj, "day");
    hour_val = get_object_field(obj, "hour");
    minute_val = get_object_field(obj, "minute");
    second_val = get_object_field(obj, "second");
    weekday_val = get_object_field(obj, "weekday");
    yearday_val = get_object_field(obj, "yearday");

    if (year_val.type == VAL_NULL || month_val.type == VAL_NULL ||
        day_val.type == VAL_NULL) {
        runtime_error(ctx, "strftime() requires year, month, and day fields"); return val_null();
    }

    tm_info.tm_year = value_to_int(year_val) - 1900;
    tm_info.tm_mon = value_to_int(month_val) - 1;  // 0-11
    tm_info.tm_mday = value_to_int(day_val);
    tm_info.tm_hour = hour_val.type != VAL_NULL ? value_to_int(hour_val) : 0;
    tm_info.tm_min = minute_val.type != VAL_NULL ? value_to_int(minute_val) : 0;
    tm_info.tm_sec = second_val.type != VAL_NULL ? value_to_int(second_val) : 0;
    tm_info.tm_wday = weekday_val.type != VAL_NULL ? value_to_int(weekday_val) : 0;
    tm_info.tm_yday = yearday_val.type != VAL_NULL ? value_to_int(yearday_val) - 1 : 0;
    tm_info.tm_isdst = -1;

    // Format the time
    len = strftime(buffer, sizeof(buffer), format, &tm_info);
    if (len == 0) {
        runtime_error(ctx, "strftime() formatting failed"); return val_null();
    }

    return val_string(buffer);
}

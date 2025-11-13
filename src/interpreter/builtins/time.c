#include "internal.h"

Value builtin_now(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: now() expects no arguments\n");
        exit(1);
    }
    return val_i64((int64_t)time(NULL));
}

Value builtin_time_ms(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: time_ms() expects no arguments\n");
        exit(1);
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return val_i64(ms);
}

Value builtin_sleep(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sleep() expects 1 argument (seconds)\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sleep() argument must be numeric\n");
        exit(1);
    }
    double seconds = value_to_float(args[0]);
    if (seconds < 0) {
        fprintf(stderr, "Runtime error: sleep() argument must be non-negative\n");
        exit(1);
    }
    // Use nanosleep for more precise sleep
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1000000000);
    nanosleep(&req, NULL);
    return val_null();
}

Value builtin_clock(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: clock() expects no arguments\n");
        exit(1);
    }
    // Returns CPU time in seconds as f64
    return val_f64((double)clock() / CLOCKS_PER_SEC);
}

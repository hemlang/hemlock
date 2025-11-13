#include "internal.h"

Value builtin_sin(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sin() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sin() argument must be numeric\n");
        exit(1);
    }
    return val_f64(sin(value_to_float(args[0])));
}

Value builtin_cos(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: cos() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: cos() argument must be numeric\n");
        exit(1);
    }
    return val_f64(cos(value_to_float(args[0])));
}

Value builtin_tan(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: tan() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: tan() argument must be numeric\n");
        exit(1);
    }
    return val_f64(tan(value_to_float(args[0])));
}

Value builtin_asin(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: asin() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: asin() argument must be numeric\n");
        exit(1);
    }
    return val_f64(asin(value_to_float(args[0])));
}

Value builtin_acos(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: acos() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: acos() argument must be numeric\n");
        exit(1);
    }
    return val_f64(acos(value_to_float(args[0])));
}

Value builtin_atan(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: atan() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: atan() argument must be numeric\n");
        exit(1);
    }
    return val_f64(atan(value_to_float(args[0])));
}

Value builtin_atan2(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: atan2() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: atan2() arguments must be numeric\n");
        exit(1);
    }
    return val_f64(atan2(value_to_float(args[0]), value_to_float(args[1])));
}

Value builtin_sqrt(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sqrt() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sqrt() argument must be numeric\n");
        exit(1);
    }
    return val_f64(sqrt(value_to_float(args[0])));
}

Value builtin_pow(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: pow() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: pow() arguments must be numeric\n");
        exit(1);
    }
    return val_f64(pow(value_to_float(args[0]), value_to_float(args[1])));
}

Value builtin_exp(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: exp() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: exp() argument must be numeric\n");
        exit(1);
    }
    return val_f64(exp(value_to_float(args[0])));
}

Value builtin_log(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log(value_to_float(args[0])));
}

Value builtin_log10(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log10() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log10() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log10(value_to_float(args[0])));
}

Value builtin_log2(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log2() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log2() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log2(value_to_float(args[0])));
}

Value builtin_floor(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: floor() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: floor() argument must be numeric\n");
        exit(1);
    }
    return val_f64(floor(value_to_float(args[0])));
}

Value builtin_ceil(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: ceil() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: ceil() argument must be numeric\n");
        exit(1);
    }
    return val_f64(ceil(value_to_float(args[0])));
}

Value builtin_round(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: round() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: round() argument must be numeric\n");
        exit(1);
    }
    return val_f64(round(value_to_float(args[0])));
}

Value builtin_trunc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: trunc() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: trunc() argument must be numeric\n");
        exit(1);
    }
    return val_f64(trunc(value_to_float(args[0])));
}

Value builtin_abs(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: abs() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: abs() argument must be numeric\n");
        exit(1);
    }
    return val_f64(fabs(value_to_float(args[0])));
}

Value builtin_min(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: min() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: min() arguments must be numeric\n");
        exit(1);
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a < b ? a : b);
}

Value builtin_max(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: max() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: max() arguments must be numeric\n");
        exit(1);
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a > b ? a : b);
}

Value builtin_clamp(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: clamp() expects 3 arguments (value, min, max)\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1]) || !is_numeric(args[2])) {
        fprintf(stderr, "Runtime error: clamp() arguments must be numeric\n");
        exit(1);
    }
    double value = value_to_float(args[0]);
    double min_val = value_to_float(args[1]);
    double max_val = value_to_float(args[2]);
    if (value < min_val) return val_f64(min_val);
    if (value > max_val) return val_f64(max_val);
    return val_f64(value);
}

Value builtin_rand(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: rand() expects no arguments\n");
        exit(1);
    }
    // Return random float between 0.0 and 1.0
    return val_f64((double)rand() / RAND_MAX);
}

Value builtin_rand_range(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: rand_range() expects 2 arguments (min, max)\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: rand_range() arguments must be numeric\n");
        exit(1);
    }
    double min_val = value_to_float(args[0]);
    double max_val = value_to_float(args[1]);
    double range = max_val - min_val;
    return val_f64(min_val + (range * rand() / RAND_MAX));
}

Value builtin_seed(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: seed() expects 1 argument\n");
        exit(1);
    }
    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: seed() argument must be an integer\n");
        exit(1);
    }
    srand((unsigned int)value_to_int(args[0]));
    return val_null();
}

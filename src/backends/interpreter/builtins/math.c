#include "internal.h"

Value builtin_sin(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "sin() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "sin() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(sin(value_to_float(args[0])));
}

Value builtin_cos(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "cos() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "cos() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(cos(value_to_float(args[0])));
}

Value builtin_tan(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "tan() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "tan() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(tan(value_to_float(args[0])));
}

Value builtin_asin(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "asin() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "asin() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(asin(value_to_float(args[0])));
}

Value builtin_acos(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "acos() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "acos() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(acos(value_to_float(args[0])));
}

Value builtin_atan(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "atan() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "atan() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(atan(value_to_float(args[0])));
}

Value builtin_atan2(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atan2() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "atan2() arguments must be numeric");
        return val_null();
    }
    return val_f64(atan2(value_to_float(args[0]), value_to_float(args[1])));
}

Value builtin_sqrt(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "sqrt() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "sqrt() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(sqrt(value_to_float(args[0])));
}

Value builtin_pow(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "pow() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "pow() arguments must be numeric");
        return val_null();
    }
    return val_f64(pow(value_to_float(args[0]), value_to_float(args[1])));
}

Value builtin_exp(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "exp() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "exp() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(exp(value_to_float(args[0])));
}

Value builtin_log(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "log() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "log() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(log(value_to_float(args[0])));
}

Value builtin_log10(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "log10() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "log10() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(log10(value_to_float(args[0])));
}

Value builtin_log2(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "log2() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "log2() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(log2(value_to_float(args[0])));
}

Value builtin_floor(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "floor() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "floor() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(floor(value_to_float(args[0])));
}

Value builtin_ceil(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ceil() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "ceil() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(ceil(value_to_float(args[0])));
}

Value builtin_round(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "round() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "round() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(round(value_to_float(args[0])));
}

Value builtin_trunc(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "trunc() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "trunc() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(trunc(value_to_float(args[0])));
}

Value builtin_floori(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "floori() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "floori() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_i64((int64_t)floor(value_to_float(args[0])));
}

Value builtin_ceili(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ceili() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "ceili() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_i64((int64_t)ceil(value_to_float(args[0])));
}

Value builtin_roundi(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "roundi() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "roundi() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_i64((int64_t)round(value_to_float(args[0])));
}

Value builtin_trunci(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "trunci() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "trunci() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_i64((int64_t)trunc(value_to_float(args[0])));
}

Value builtin_div(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "div() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "div() arguments must be numeric");
        return val_null();
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    if (b == 0.0) {
        runtime_error(ctx, "div(): division by zero");
        return val_null();
    }
    return val_f64(a / b);
}

Value builtin_divi(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "divi() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "divi() arguments must be numeric");
        return val_null();
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    if (b == 0.0) {
        runtime_error(ctx, "divi(): division by zero");
        return val_null();
    }
    return val_i64((int64_t)trunc(a / b));
}

Value builtin_abs(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "abs() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0])) {
        runtime_error(ctx, "abs() argument must be numeric, got %s", value_type_name(args[0].type));
        return val_null();
    }
    return val_f64(fabs(value_to_float(args[0])));
}

Value builtin_min(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "min() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "min() arguments must be numeric");
        return val_null();
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a < b ? a : b);
}

Value builtin_max(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "max() expects 2 arguments, got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "max() arguments must be numeric");
        return val_null();
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a > b ? a : b);
}

Value builtin_clamp(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "clamp() expects 3 arguments (value, min, max), got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1]) || !is_numeric(args[2])) {
        runtime_error(ctx, "clamp() arguments must be numeric");
        return val_null();
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
    if (num_args != 0) {
        runtime_error(ctx, "rand() expects no arguments, got %d", num_args);
        return val_null();
    }
    // Return random float between 0.0 and 1.0
    return val_f64((double)rand() / RAND_MAX);
}

Value builtin_rand_range(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "rand_range() expects 2 arguments (min, max), got %d", num_args);
        return val_null();
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        runtime_error(ctx, "rand_range() arguments must be numeric");
        return val_null();
    }
    double min_val = value_to_float(args[0]);
    double max_val = value_to_float(args[1]);
    double range = max_val - min_val;
    return val_f64(min_val + (range * rand() / RAND_MAX));
}

Value builtin_seed(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "seed() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (!is_integer(args[0])) {
        runtime_error(ctx, "seed() argument must be an integer, got %s", value_type_name(args[0].type));
        return val_null();
    }
    srand((unsigned int)value_to_int(args[0]));
    return val_null();
}

#include "internal.h"

// Forward declarations for helper functions
static Value get_object_field(Object *obj, const char *name);
static void set_object_field(Object *obj, const char *name, Value value);

// Helper to convert Value to int64_t
static int64_t value_to_int64(Value val) {
    switch (val.type) {
        case VAL_I8: return (int64_t)val.as.as_i8;
        case VAL_I16: return (int64_t)val.as.as_i16;
        case VAL_I32: return (int64_t)val.as.as_i32;
        case VAL_I64: return val.as.as_i64;
        case VAL_U8: return (int64_t)val.as.as_u8;
        case VAL_U16: return (int64_t)val.as.as_u16;
        case VAL_U32: return (int64_t)val.as.as_u32;
        case VAL_U64: return (int64_t)val.as.as_u64;
        case VAL_F32: return (int64_t)val.as.as_f32;
        case VAL_F64: return (int64_t)val.as.as_f64;
        default: return 0;
    }
}

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

// Convert Unix timestamp to local time components
Value builtin_localtime(Value *args, int num_args, ExecutionContext *ctx) {
    time_t timestamp;
    struct tm *tm_info;
    Object *obj;

    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: localtime() expects 1 argument (timestamp)\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: localtime() argument must be numeric\n");
        exit(1);
    }

    timestamp = (time_t)value_to_int64(args[0]);
    tm_info = localtime(&timestamp);

    if (!tm_info) {
        fprintf(stderr, "Runtime error: localtime() failed to convert timestamp\n");
        exit(1);
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

    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: gmtime() expects 1 argument (timestamp)\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: gmtime() argument must be numeric\n");
        exit(1);
    }

    timestamp = (time_t)value_to_int64(args[0]);
    tm_info = gmtime(&timestamp);

    if (!tm_info) {
        fprintf(stderr, "Runtime error: gmtime() failed to convert timestamp\n");
        exit(1);
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
        if (strcmp(obj->field_names[i], name) == 0) {
            return obj->field_values[i];
        }
    }
    return val_null();
}

// Helper function to set field on object
static void set_object_field(Object *obj, const char *name, Value value) {
    if (obj->num_fields >= obj->capacity) {
        obj->capacity *= 2;
        obj->field_names = realloc(obj->field_names, obj->capacity * sizeof(char *));
        obj->field_values = realloc(obj->field_values, obj->capacity * sizeof(Value));
    }
    obj->field_names[obj->num_fields] = strdup(name);
    obj->field_values[obj->num_fields] = value;
    obj->num_fields++;
}

// Convert time components to Unix timestamp
Value builtin_mktime(Value *args, int num_args, ExecutionContext *ctx) {
    Object *obj;
    struct tm tm_info = {0};
    Value year_val, month_val, day_val, hour_val, minute_val, second_val;
    time_t timestamp;

    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: mktime() expects 1 argument (time components object)\n");
        exit(1);
    }
    if (args[0].type != VAL_OBJECT) {
        fprintf(stderr, "Runtime error: mktime() argument must be an object\n");
        exit(1);
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
        fprintf(stderr, "Runtime error: mktime() requires year, month, and day fields\n");
        exit(1);
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
        fprintf(stderr, "Runtime error: mktime() failed to convert time components\n");
        exit(1);
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

    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: strftime() expects 2 arguments (format, time components)\n");
        exit(1);
    }
    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: strftime() format argument must be a string\n");
        exit(1);
    }
    if (args[1].type != VAL_OBJECT) {
        fprintf(stderr, "Runtime error: strftime() time components argument must be an object\n");
        exit(1);
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
        fprintf(stderr, "Runtime error: strftime() requires year, month, and day fields\n");
        exit(1);
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
        fprintf(stderr, "Runtime error: strftime() formatting failed\n");
        exit(1);
    }

    return val_string(buffer);
}

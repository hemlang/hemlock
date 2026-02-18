/*
 * Hemlock Runtime Library - Time Operations
 *
 * Time and datetime functions: now, sleep, localtime, strftime, etc.
 */

#include "builtins_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ========== CORE TIME FUNCTIONS ==========

HmlValue hml_now(void) {
    // time(NULL) works on all platforms including Emscripten (libc)
    return hml_val_i64((int64_t)time(NULL));
}

HmlValue hml_time_ms(void) {
#ifdef __EMSCRIPTEN__
    // emscripten_get_now() returns high-resolution milliseconds (double)
    double ms = emscripten_get_now();
    return hml_val_i64((int64_t)ms);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return hml_val_i64((int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

HmlValue hml_clock(void) {
    // clock() works on all platforms including Emscripten (libc)
    return hml_val_f64((double)clock() / CLOCKS_PER_SEC);
}

void hml_sleep(HmlValue seconds) {
    double secs = hml_to_f64(seconds);
#ifdef __EMSCRIPTEN__
    emscripten_sleep((unsigned int)(secs * 1000));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
}

// ========== DATETIME FUNCTIONS ==========

// Convert Unix timestamp to local time components
HmlValue hml_localtime(HmlValue timestamp) {
    time_t ts = (time_t)hml_to_i64(timestamp);
    struct tm *tm_info = localtime(&ts);

    if (!tm_info) {
        fprintf(stderr, "Error: localtime() failed to convert timestamp\n");
        exit(1);
    }

    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "year", hml_val_i32(tm_info->tm_year + 1900));
    hml_object_set_field(obj, "month", hml_val_i32(tm_info->tm_mon + 1));  // 1-12
    hml_object_set_field(obj, "day", hml_val_i32(tm_info->tm_mday));
    hml_object_set_field(obj, "hour", hml_val_i32(tm_info->tm_hour));
    hml_object_set_field(obj, "minute", hml_val_i32(tm_info->tm_min));
    hml_object_set_field(obj, "second", hml_val_i32(tm_info->tm_sec));
    hml_object_set_field(obj, "weekday", hml_val_i32(tm_info->tm_wday));  // 0=Sunday
    hml_object_set_field(obj, "yearday", hml_val_i32(tm_info->tm_yday + 1));  // 1-366
    hml_object_set_field(obj, "isdst", hml_val_bool(tm_info->tm_isdst > 0));

    return obj;
}

// Convert Unix timestamp to UTC time components
HmlValue hml_gmtime(HmlValue timestamp) {
    time_t ts = (time_t)hml_to_i64(timestamp);
    struct tm *tm_info = gmtime(&ts);

    if (!tm_info) {
        fprintf(stderr, "Error: gmtime() failed to convert timestamp\n");
        exit(1);
    }

    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "year", hml_val_i32(tm_info->tm_year + 1900));
    hml_object_set_field(obj, "month", hml_val_i32(tm_info->tm_mon + 1));  // 1-12
    hml_object_set_field(obj, "day", hml_val_i32(tm_info->tm_mday));
    hml_object_set_field(obj, "hour", hml_val_i32(tm_info->tm_hour));
    hml_object_set_field(obj, "minute", hml_val_i32(tm_info->tm_min));
    hml_object_set_field(obj, "second", hml_val_i32(tm_info->tm_sec));
    hml_object_set_field(obj, "weekday", hml_val_i32(tm_info->tm_wday));  // 0=Sunday
    hml_object_set_field(obj, "yearday", hml_val_i32(tm_info->tm_yday + 1));  // 1-366
    hml_object_set_field(obj, "isdst", hml_val_bool(0));  // UTC has no DST

    return obj;
}

// Convert time components to Unix timestamp
HmlValue hml_mktime(HmlValue time_obj) {
    if (time_obj.type != HML_VAL_OBJECT || !time_obj.as.as_object) {
        fprintf(stderr, "Error: mktime() requires an object argument\n");
        exit(1);
    }

    struct tm tm_info = {0};

    HmlValue year = hml_object_get_field(time_obj, "year");
    HmlValue month = hml_object_get_field(time_obj, "month");
    HmlValue day = hml_object_get_field(time_obj, "day");
    HmlValue hour = hml_object_get_field(time_obj, "hour");
    HmlValue minute = hml_object_get_field(time_obj, "minute");
    HmlValue second = hml_object_get_field(time_obj, "second");

    if (year.type == HML_VAL_NULL || month.type == HML_VAL_NULL || day.type == HML_VAL_NULL) {
        fprintf(stderr, "Error: mktime() requires year, month, and day fields\n");
        exit(1);
    }

    tm_info.tm_year = hml_to_i32(year) - 1900;
    tm_info.tm_mon = hml_to_i32(month) - 1;  // 0-11
    tm_info.tm_mday = hml_to_i32(day);
    tm_info.tm_hour = hour.type != HML_VAL_NULL ? hml_to_i32(hour) : 0;
    tm_info.tm_min = minute.type != HML_VAL_NULL ? hml_to_i32(minute) : 0;
    tm_info.tm_sec = second.type != HML_VAL_NULL ? hml_to_i32(second) : 0;
    tm_info.tm_isdst = -1;  // Auto-determine DST

    time_t timestamp = mktime(&tm_info);
    if (timestamp == -1) {
        fprintf(stderr, "Error: mktime() failed to convert time components\n");
        exit(1);
    }

    return hml_val_i64((int64_t)timestamp);
}

// Format date/time using strftime
HmlValue hml_strftime(HmlValue format, HmlValue time_obj) {
    if (format.type != HML_VAL_STRING || !format.as.as_string) {
        fprintf(stderr, "Error: strftime() format must be a string\n");
        exit(1);
    }
    if (time_obj.type != HML_VAL_OBJECT || !time_obj.as.as_object) {
        fprintf(stderr, "Error: strftime() time components must be an object\n");
        exit(1);
    }

    struct tm tm_info = {0};

    HmlValue year = hml_object_get_field(time_obj, "year");
    HmlValue month = hml_object_get_field(time_obj, "month");
    HmlValue day = hml_object_get_field(time_obj, "day");
    HmlValue hour = hml_object_get_field(time_obj, "hour");
    HmlValue minute = hml_object_get_field(time_obj, "minute");
    HmlValue second = hml_object_get_field(time_obj, "second");
    HmlValue weekday = hml_object_get_field(time_obj, "weekday");
    HmlValue yearday = hml_object_get_field(time_obj, "yearday");

    if (year.type == HML_VAL_NULL || month.type == HML_VAL_NULL || day.type == HML_VAL_NULL) {
        fprintf(stderr, "Error: strftime() requires year, month, and day fields\n");
        exit(1);
    }

    tm_info.tm_year = hml_to_i32(year) - 1900;
    tm_info.tm_mon = hml_to_i32(month) - 1;  // 0-11
    tm_info.tm_mday = hml_to_i32(day);
    tm_info.tm_hour = hour.type != HML_VAL_NULL ? hml_to_i32(hour) : 0;
    tm_info.tm_min = minute.type != HML_VAL_NULL ? hml_to_i32(minute) : 0;
    tm_info.tm_sec = second.type != HML_VAL_NULL ? hml_to_i32(second) : 0;
    tm_info.tm_wday = weekday.type != HML_VAL_NULL ? hml_to_i32(weekday) : 0;
    tm_info.tm_yday = yearday.type != HML_VAL_NULL ? hml_to_i32(yearday) - 1 : 0;
    tm_info.tm_isdst = -1;

    char buffer[256];
    size_t len = strftime(buffer, sizeof(buffer), format.as.as_string->data, &tm_info);
    if (len == 0) {
        fprintf(stderr, "Error: strftime() formatting failed\n");
        exit(1);
    }

    return hml_val_string(buffer);
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_now(HmlClosureEnv *env) {
    (void)env;
    return hml_now();
}

HmlValue hml_builtin_time_ms(HmlClosureEnv *env) {
    (void)env;
    return hml_time_ms();
}

HmlValue hml_builtin_clock(HmlClosureEnv *env) {
    (void)env;
    return hml_clock();
}

HmlValue hml_builtin_sleep(HmlClosureEnv *env, HmlValue seconds) {
    (void)env;
    hml_sleep(seconds);
    return hml_val_null();
}

HmlValue hml_builtin_localtime(HmlClosureEnv *env, HmlValue timestamp) {
    (void)env;
    return hml_localtime(timestamp);
}

HmlValue hml_builtin_gmtime(HmlClosureEnv *env, HmlValue timestamp) {
    (void)env;
    return hml_gmtime(timestamp);
}

HmlValue hml_builtin_mktime(HmlClosureEnv *env, HmlValue time_obj) {
    (void)env;
    return hml_mktime(time_obj);
}

HmlValue hml_builtin_strftime(HmlClosureEnv *env, HmlValue format, HmlValue time_obj) {
    (void)env;
    return hml_strftime(format, time_obj);
}

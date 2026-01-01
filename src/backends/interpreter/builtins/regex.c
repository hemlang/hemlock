/*
 * Hemlock Interpreter - Regex Builtins
 *
 * POSIX regex functions for static linking compatibility.
 */

#include "internal.h"
#include <regex.h>

// ========== REGEX BUILTINS ==========

/**
 * __regex_compile(pattern: string, flags: i32) -> ptr
 *
 * Compiles a regex pattern and returns a pointer to the compiled regex_t.
 * Returns null on failure.
 */
Value builtin_regex_compile(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    if (num_args < 1) {
        runtime_error(ctx, "regex_compile requires at least 1 argument");
    }

    Value pattern = args[0];
    if (pattern.type != VAL_STRING) {
        runtime_error(ctx, "regex_compile: pattern must be a string");
    }

    int cflags = REG_EXTENDED;  // Default to extended regex
    if (num_args >= 2 && args[1].type != VAL_NULL) {
        cflags = (int)value_to_int64(args[1]);
    }

    // Allocate regex_t
    regex_t *preg = (regex_t *)malloc(sizeof(regex_t));
    if (!preg) {
        runtime_error(ctx, "regex_compile: failed to allocate memory");
    }

    // Compile the pattern
    int result = regcomp(preg, pattern.as.as_string->data, cflags);
    if (result != 0) {
        free(preg);
        return val_null();  // Return null on compilation failure
    }

    // Return pointer to compiled regex
    return val_ptr(preg);
}

/**
 * __regex_test(preg: ptr, text: string, eflags: i32) -> bool
 *
 * Tests if the text matches the compiled regex.
 */
Value builtin_regex_test(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    if (num_args < 2) {
        runtime_error(ctx, "regex_test requires at least 2 arguments");
    }

    Value preg_val = args[0];
    Value text = args[1];

    if (preg_val.type != VAL_PTR || preg_val.as.as_ptr == NULL) {
        runtime_error(ctx, "regex_test: invalid regex pointer");
    }
    if (text.type != VAL_STRING) {
        runtime_error(ctx, "regex_test: text must be a string");
    }

    int flags = 0;
    if (num_args >= 3 && args[2].type != VAL_NULL) {
        flags = (int)value_to_int64(args[2]);
    }

    regex_t *regex = (regex_t *)preg_val.as.as_ptr;
    int result = regexec(regex, text.as.as_string->data, 0, NULL, flags);

    return val_bool(result == 0);
}

/**
 * __regex_match(preg: ptr, text: string, max_matches: i32) -> array
 *
 * Finds matches in the text and returns an array of match objects.
 */
Value builtin_regex_match(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 2) {
        runtime_error(ctx, "regex_match requires at least 2 arguments");
    }

    Value preg_val = args[0];
    Value text = args[1];

    if (preg_val.type != VAL_PTR || preg_val.as.as_ptr == NULL) {
        runtime_error(ctx, "regex_match: invalid regex pointer");
    }
    if (text.type != VAL_STRING) {
        runtime_error(ctx, "regex_match: text must be a string");
    }

    int nmatch = 10;  // Default max matches
    if (num_args >= 3 && args[2].type != VAL_NULL) {
        nmatch = (int)value_to_int64(args[2]);
        if (nmatch <= 0) nmatch = 10;
        if (nmatch > 100) nmatch = 100;  // Cap at 100
    }

    regex_t *regex = (regex_t *)preg_val.as.as_ptr;
    const char *text_data = text.as.as_string->data;
    regmatch_t *pmatch = (regmatch_t *)malloc(nmatch * sizeof(regmatch_t));
    if (!pmatch) {
        runtime_error(ctx, "regex_match: failed to allocate memory");
        return val_null();
    }

    Array *result = array_new();

    int exec_result = regexec(regex, text_data, nmatch, pmatch, 0);
    if (exec_result == 0) {
        // Add all valid matches to the array
        for (int i = 0; i < nmatch; i++) {
            if (pmatch[i].rm_so == -1) break;  // No more matches

            // Create match object with 3 fields
            Object *match = object_new(NULL, 3);

            // Extract matched text
            int len = pmatch[i].rm_eo - pmatch[i].rm_so;
            char *matched = (char *)malloc(len + 1);
            if (matched) {
                strncpy(matched, text_data + pmatch[i].rm_so, len);
                matched[len] = '\0';
            }

            // Set fields
            match->field_names[0] = strdup("start");
            match->field_values[0] = val_i32((int32_t)pmatch[i].rm_so);
            match->num_fields++;

            match->field_names[1] = strdup("end");
            match->field_values[1] = val_i32((int32_t)pmatch[i].rm_eo);
            match->num_fields++;

            match->field_names[2] = strdup("text");
            match->field_values[2] = matched ? val_string(matched) : val_null();
            match->num_fields++;

            if (matched) free(matched);

            array_push(result, val_object(match));
        }
    }

    free(pmatch);
    return val_array(result);
}

/**
 * __regex_free(preg: ptr) -> null
 *
 * Frees a compiled regex.
 */
Value builtin_regex_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    if (num_args < 1) {
        return val_null();
    }

    Value preg_val = args[0];
    if (preg_val.type != VAL_PTR || preg_val.as.as_ptr == NULL) {
        return val_null();  // Already freed or invalid
    }

    regex_t *regex = (regex_t *)preg_val.as.as_ptr;
    regfree(regex);
    free(regex);

    return val_null();
}

/**
 * __regex_error(errcode: i32, preg: ptr) -> string
 *
 * Returns an error message for a regex error code.
 */
Value builtin_regex_error(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    if (num_args < 1) {
        return val_string("Unknown error");
    }

    int code = (int)value_to_int64(args[0]);
    regex_t *regex = NULL;

    if (num_args >= 2 && args[1].type == VAL_PTR && args[1].as.as_ptr != NULL) {
        regex = (regex_t *)args[1].as.as_ptr;
    }

    char errbuf[256];
    regerror(code, regex, errbuf, sizeof(errbuf));

    return val_string(errbuf);
}

/**
 * __regex_replace(preg: ptr, text: string, replacement: string) -> string
 *
 * Replaces the first match with the replacement string.
 */
Value builtin_regex_replace(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 3) {
        runtime_error(ctx, "regex_replace requires 3 arguments");
    }

    Value preg_val = args[0];
    Value text = args[1];
    Value replacement = args[2];

    if (preg_val.type != VAL_PTR || preg_val.as.as_ptr == NULL) {
        runtime_error(ctx, "regex_replace: invalid regex pointer");
    }
    if (text.type != VAL_STRING) {
        runtime_error(ctx, "regex_replace: text must be a string");
    }
    if (replacement.type != VAL_STRING) {
        runtime_error(ctx, "regex_replace: replacement must be a string");
    }

    regex_t *regex = (regex_t *)preg_val.as.as_ptr;
    const char *text_data = text.as.as_string->data;
    const char *repl_data = replacement.as.as_string->data;
    regmatch_t pmatch[1];

    int result = regexec(regex, text_data, 1, pmatch, 0);
    if (result != 0) {
        // No match, return original string
        return text;
    }

    // Build result string
    size_t prefix_len = pmatch[0].rm_so;
    size_t suffix_start = pmatch[0].rm_eo;
    size_t suffix_len = strlen(text_data) - suffix_start;
    size_t repl_len = strlen(repl_data);

    char *new_str = (char *)malloc(prefix_len + repl_len + suffix_len + 1);
    if (!new_str) {
        runtime_error(ctx, "regex_replace: failed to allocate memory");
        return val_null();
    }

    memcpy(new_str, text_data, prefix_len);
    memcpy(new_str + prefix_len, repl_data, repl_len);
    memcpy(new_str + prefix_len + repl_len, text_data + suffix_start, suffix_len);
    new_str[prefix_len + repl_len + suffix_len] = '\0';

    Value result_val = val_string(new_str);
    free(new_str);
    return result_val;
}

/**
 * __regex_replace_all(preg: ptr, text: string, replacement: string) -> string
 *
 * Replaces all matches with the replacement string.
 */
Value builtin_regex_replace_all(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 3) {
        runtime_error(ctx, "regex_replace_all requires 3 arguments");
    }

    Value preg_val = args[0];
    Value text = args[1];
    Value replacement = args[2];

    if (preg_val.type != VAL_PTR || preg_val.as.as_ptr == NULL) {
        runtime_error(ctx, "regex_replace_all: invalid regex pointer");
    }
    if (text.type != VAL_STRING) {
        runtime_error(ctx, "regex_replace_all: text must be a string");
    }
    if (replacement.type != VAL_STRING) {
        runtime_error(ctx, "regex_replace_all: replacement must be a string");
    }

    regex_t *regex = (regex_t *)preg_val.as.as_ptr;
    const char *src = text.as.as_string->data;
    const char *repl = replacement.as.as_string->data;
    size_t repl_len = strlen(repl);

    // First pass: count matches and calculate result size
    size_t result_size = 0;
    size_t match_count = 0;
    const char *p = src;
    regmatch_t pmatch[1];

    while (*p && regexec(regex, p, 1, pmatch, (p == src) ? 0 : REG_NOTBOL) == 0) {
        // Prevent infinite loop on zero-length matches
        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (p[pmatch[0].rm_eo] == '\0') break;
            result_size += 1;  // Copy one char and continue
            p += pmatch[0].rm_eo + 1;
            continue;
        }

        result_size += pmatch[0].rm_so;  // Prefix
        result_size += repl_len;          // Replacement
        match_count++;
        p += pmatch[0].rm_eo;
    }
    result_size += strlen(p);  // Remaining suffix

    if (match_count == 0) {
        return text;  // No matches
    }

    // Second pass: build result
    char *result = (char *)malloc(result_size + 1);
    if (!result) {
        runtime_error(ctx, "regex_replace_all: failed to allocate memory");
        return val_null();
    }

    char *dst = result;
    p = src;

    while (*p && regexec(regex, p, 1, pmatch, (p == src) ? 0 : REG_NOTBOL) == 0) {
        // Prevent infinite loop on zero-length matches
        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (p[pmatch[0].rm_eo] == '\0') break;
            *dst++ = p[pmatch[0].rm_eo];
            p += pmatch[0].rm_eo + 1;
            continue;
        }

        // Copy prefix
        memcpy(dst, p, pmatch[0].rm_so);
        dst += pmatch[0].rm_so;

        // Copy replacement
        memcpy(dst, repl, repl_len);
        dst += repl_len;

        p += pmatch[0].rm_eo;
    }

    // Copy remaining suffix
    strcpy(dst, p);

    Value result_val = val_string(result);
    free(result);
    return result_val;
}

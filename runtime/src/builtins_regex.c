/*
 * Hemlock Runtime Library - Regex Builtins
 *
 * POSIX regex functions exposed as builtins for static linking compatibility.
 * This allows regex to work without dlopen/FFI.
 */

#include "builtins_internal.h"
#include <regex.h>

// ========== REGEX CONSTANTS ==========

// These match POSIX regex.h values
#define HML_REG_EXTENDED  1
#define HML_REG_ICASE     2
#define HML_REG_NOSUB     4
#define HML_REG_NEWLINE   8

#define HML_REG_NOTBOL    1
#define HML_REG_NOTEOL    2

// ========== REGEX FUNCTIONS ==========

/**
 * regex_compile(pattern: string, flags: i32) -> ptr
 *
 * Compiles a regex pattern and returns a pointer to the compiled regex_t.
 * Returns null on failure.
 */
HmlValue hml_regex_compile(HmlValue pattern, HmlValue flags) {
    if (pattern.type != HML_VAL_STRING || !pattern.as.as_string) {
        hml_runtime_error("regex_compile: pattern must be a string");
    }

    int cflags = REG_EXTENDED;  // Default to extended regex
    if (flags.type != HML_VAL_NULL) {
        cflags = (int)hml_to_i64(flags);
    }

    // Allocate regex_t
    regex_t *preg = (regex_t *)malloc(sizeof(regex_t));
    if (!preg) {
        hml_runtime_error("regex_compile: failed to allocate memory");
    }

    // Compile the pattern
    int result = regcomp(preg, pattern.as.as_string->data, cflags);
    if (result != 0) {
        free(preg);
        return hml_val_null();  // Return null on compilation failure
    }

    // Return pointer to compiled regex
    return hml_val_ptr(preg);
}

/**
 * regex_test(preg: ptr, text: string, eflags: i32) -> bool
 *
 * Tests if the text matches the compiled regex.
 */
HmlValue hml_regex_test(HmlValue preg, HmlValue text, HmlValue eflags) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        hml_runtime_error("regex_test: invalid regex pointer");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("regex_test: text must be a string");
    }

    int flags = 0;
    if (eflags.type != HML_VAL_NULL) {
        flags = (int)hml_to_i64(eflags);
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
    int result = regexec(regex, text.as.as_string->data, 0, NULL, flags);

    return hml_val_bool(result == 0);
}

/**
 * regex_match(preg: ptr, text: string, max_matches: i32) -> array
 *
 * Finds matches in the text and returns an array of match objects.
 * Each match object has: { start: i32, end: i32, text: string }
 */
HmlValue hml_regex_match(HmlValue preg, HmlValue text, HmlValue max_matches) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        hml_runtime_error("regex_match: invalid regex pointer");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("regex_match: text must be a string");
    }

    int nmatch = 10;  // Default max matches
    if (max_matches.type != HML_VAL_NULL) {
        nmatch = (int)hml_to_i64(max_matches);
        if (nmatch <= 0) nmatch = 10;
        if (nmatch > 100) nmatch = 100;  // Cap at 100
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
    const char *text_data = text.as.as_string->data;
    regmatch_t *pmatch = (regmatch_t *)malloc(nmatch * sizeof(regmatch_t));
    if (!pmatch) {
        hml_runtime_error("regex_match: failed to allocate memory");
    }

    HmlValue result = hml_val_array();

    int exec_result = regexec(regex, text_data, nmatch, pmatch, 0);
    if (exec_result == 0) {
        // Add all valid matches to the array
        for (int i = 0; i < nmatch; i++) {
            if (pmatch[i].rm_so == -1) break;  // No more matches

            HmlValue match = hml_val_object();
            hml_object_set_field(match, "start", hml_val_i32((int32_t)pmatch[i].rm_so));
            hml_object_set_field(match, "end", hml_val_i32((int32_t)pmatch[i].rm_eo));

            // Extract matched text
            int len = pmatch[i].rm_eo - pmatch[i].rm_so;
            char *matched = (char *)malloc(len + 1);
            if (matched) {
                strncpy(matched, text_data + pmatch[i].rm_so, len);
                matched[len] = '\0';
                hml_object_set_field(match, "text", hml_val_string(matched));
                free(matched);
            }

            hml_array_push(result, match);
        }
    }

    free(pmatch);
    return result;
}

/**
 * regex_free(preg: ptr) -> null
 *
 * Frees a compiled regex.
 */
HmlValue hml_regex_free(HmlValue preg) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        return hml_val_null();  // Already freed or invalid
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
    regfree(regex);
    free(regex);

    return hml_val_null();
}

/**
 * regex_error(errcode: i32, preg: ptr) -> string
 *
 * Returns an error message for a regex error code.
 */
HmlValue hml_regex_error(HmlValue errcode, HmlValue preg) {
    int code = (int)hml_to_i64(errcode);
    regex_t *regex = NULL;

    if (preg.type == HML_VAL_PTR && preg.as.as_ptr != NULL) {
        regex = (regex_t *)preg.as.as_ptr;
    }

    char errbuf[256];
    regerror(code, regex, errbuf, sizeof(errbuf));

    return hml_val_string(errbuf);
}

/**
 * regex_replace(preg: ptr, text: string, replacement: string) -> string
 *
 * Replaces the first match with the replacement string.
 */
HmlValue hml_regex_replace(HmlValue preg, HmlValue text, HmlValue replacement) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        hml_runtime_error("regex_replace: invalid regex pointer");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("regex_replace: text must be a string");
    }
    if (replacement.type != HML_VAL_STRING || !replacement.as.as_string) {
        hml_runtime_error("regex_replace: replacement must be a string");
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
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
        hml_runtime_error("regex_replace: failed to allocate memory");
    }

    memcpy(new_str, text_data, prefix_len);
    memcpy(new_str + prefix_len, repl_data, repl_len);
    memcpy(new_str + prefix_len + repl_len, text_data + suffix_start, suffix_len);
    new_str[prefix_len + repl_len + suffix_len] = '\0';

    HmlValue result_val = hml_val_string(new_str);
    free(new_str);
    return result_val;
}

/**
 * regex_replace_all(preg: ptr, text: string, replacement: string) -> string
 *
 * Replaces all matches with the replacement string.
 */
HmlValue hml_regex_replace_all(HmlValue preg, HmlValue text, HmlValue replacement) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        hml_runtime_error("regex_replace_all: invalid regex pointer");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("regex_replace_all: text must be a string");
    }
    if (replacement.type != HML_VAL_STRING || !replacement.as.as_string) {
        hml_runtime_error("regex_replace_all: replacement must be a string");
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
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
        hml_runtime_error("regex_replace_all: failed to allocate memory");
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

    HmlValue result_val = hml_val_string(result);
    free(result);
    return result_val;
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_regex_compile(HmlClosureEnv *env, HmlValue pattern, HmlValue flags) {
    (void)env;
    return hml_regex_compile(pattern, flags);
}

HmlValue hml_builtin_regex_test(HmlClosureEnv *env, HmlValue preg, HmlValue text, HmlValue eflags) {
    (void)env;
    return hml_regex_test(preg, text, eflags);
}

HmlValue hml_builtin_regex_match(HmlClosureEnv *env, HmlValue preg, HmlValue text, HmlValue max_matches) {
    (void)env;
    return hml_regex_match(preg, text, max_matches);
}

HmlValue hml_builtin_regex_free(HmlClosureEnv *env, HmlValue preg) {
    (void)env;
    return hml_regex_free(preg);
}

HmlValue hml_builtin_regex_error(HmlClosureEnv *env, HmlValue errcode, HmlValue preg) {
    (void)env;
    return hml_regex_error(errcode, preg);
}

HmlValue hml_builtin_regex_replace(HmlClosureEnv *env, HmlValue preg, HmlValue text, HmlValue replacement) {
    (void)env;
    return hml_regex_replace(preg, text, replacement);
}

HmlValue hml_builtin_regex_replace_all(HmlClosureEnv *env, HmlValue preg, HmlValue text, HmlValue replacement) {
    (void)env;
    return hml_regex_replace_all(preg, text, replacement);
}

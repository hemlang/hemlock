/*
 * Hemlock Runtime Library - Regex Builtins
 *
 * POSIX regex functions exposed as builtins for static linking compatibility.
 * This allows regex to work without dlopen/FFI.
 */

#include "builtins_internal.h"

// On Windows <regex.h> resolves to the bundled musl/TRE engine
// (src/shared/regex_win32, on the include path for MinGW builds), so the
// same POSIX implementation compiles on every platform.
#include <regex.h>
#include "regex_flags.h"

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
        cflags = hml_regex_native_cflags(hml_to_i64(flags));
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
        flags = hml_regex_native_eflags(hml_to_i64(eflags));
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
HmlValue hml_regex_match(HmlValue preg, HmlValue text, HmlValue max_matches_val) {
    if (preg.type != HML_VAL_PTR || preg.as.as_ptr == NULL) {
        hml_runtime_error("regex_match: invalid regex pointer");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("regex_match: text must be a string");
    }

    // max_matches: null or <= 0 means no limit
    int64_t max_matches = 0;
    if (max_matches_val.type != HML_VAL_NULL) {
        max_matches = hml_to_i64(max_matches_val);
    }

    regex_t *regex = (regex_t *)preg.as.as_ptr;
    const char *text_data = text.as.as_string->data;
    size_t text_len = (size_t)text.as.as_string->length;

    HmlValue result = hml_val_array();

    // Successive whole-pattern matches (not the capture groups of one match)
    size_t pos = 0, so = 0, eo = 0;
    int64_t found = 0;
    while ((max_matches <= 0 || found < max_matches) &&
           hml_regex_next_match(regex, text_data, text_len, &pos, &so, &eo)) {
        HmlValue match = hml_val_object();
        hml_object_set_field(match, "start", hml_val_i32((int32_t)so));
        hml_object_set_field(match, "end", hml_val_i32((int32_t)eo));
        char *matched = strndup(text_data + so, eo - so);
        if (!matched) {
            hml_release(&match);
            hml_release(&result);
            hml_runtime_error("regex_match: failed to allocate memory");
        }
        hml_object_set_field_owned(match, "text", hml_val_string_owned(matched, (int)(eo - so), (int)(eo - so) + 1));
        hml_array_push(result, match);
        hml_release(&match);
        found++;
    }

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

    int result = hml_regexec_positions(regex, text_data, 1, pmatch, 0);
    if (result != 0) {
        // No match, return a retained copy of the original string
        hml_retain(&text);
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

    while (*p && hml_regexec_positions(regex, p, 1, pmatch, (p == src) ? 0 : REG_NOTBOL) == 0) {
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
        // No matches - return a retained copy of the original string
        hml_retain(&text);
        return text;
    }

    // Second pass: build result
    char *result = (char *)malloc(result_size + 1);
    if (!result) {
        hml_runtime_error("regex_replace_all: failed to allocate memory");
    }

    char *dst = result;
    p = src;

    while (*p && hml_regexec_positions(regex, p, 1, pmatch, (p == src) ? 0 : REG_NOTBOL) == 0) {
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

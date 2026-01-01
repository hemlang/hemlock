/*
 * Hemlock Runtime Library - Builtins Implementation
 *
 * This file implements the core builtin functions:
 * print, typeof, assert, panic, and operations.
 */

#include "../include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <limits.h>
#include <dlfcn.h>
#include <ffi.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>

#ifdef HML_HAVE_ZLIB
#include <zlib.h>
#endif

// OpenSSL for cryptographic functions
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

// ========== GLOBAL STATE ==========

static int g_argc = 0;
static char **g_argv = NULL;
static HmlExceptionContext *g_exception_stack = NULL;

// Defer stack
typedef struct DeferEntry {
    HmlDeferFn fn;
    void *arg;
    struct DeferEntry *next;
} DeferEntry;

static DeferEntry *g_defer_stack = NULL;

// ========== RUNTIME INITIALIZATION ==========

void hml_runtime_init(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    g_exception_stack = NULL;
    g_defer_stack = NULL;
}

void hml_runtime_cleanup(void) {
    // Execute remaining defers
    hml_defer_execute_all();

    // Clear exception stack
    while (g_exception_stack) {
        hml_exception_pop();
    }
}

HmlValue hml_get_args(void) {
    HmlValue arr = hml_val_array();

    // For compiled binaries, argv[0] is the program name which becomes args[0]
    // This matches interpreter behavior where args[0] is the script filename
    for (int i = 0; i < g_argc; i++) {
        HmlValue str = hml_val_string(g_argv[i]);
        hml_array_push(arr, str);
    }

    return arr;
}

// ========== UTF-8 ENCODING ==========

// Encode a Unicode codepoint to UTF-8, returns the number of bytes written
static int utf8_encode_rune(uint32_t codepoint, char *out) {
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
}

// ========== PRINT IMPLEMENTATION ==========

// Helper to print a value to a file
static void print_value_to(FILE *out, HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            fprintf(out, "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            fprintf(out, "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            fprintf(out, "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            fprintf(out, "%ld", val.as.as_i64);
            break;
        case HML_VAL_U8:
            fprintf(out, "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            fprintf(out, "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            fprintf(out, "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            fprintf(out, "%lu", val.as.as_u64);
            break;
        case HML_VAL_F32:
            fprintf(out, "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            fprintf(out, "%g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            fprintf(out, "%s", val.as.as_bool ? "true" : "false");
            break;
        case HML_VAL_STRING:
            if (val.as.as_string) {
                fprintf(out, "%s", val.as.as_string->data);
            }
            break;
        case HML_VAL_RUNE: {
            // Print rune as character if printable, otherwise as U+XXXX (match interpreter behavior)
            uint32_t r = val.as.as_rune;
            if (r >= 32 && r < 127) {
                fprintf(out, "'%c'", (char)r);
            } else {
                fprintf(out, "U+%04X", r);
            }
            break;
        }
        case HML_VAL_NULL:
            fprintf(out, "null");
            break;
        case HML_VAL_PTR:
            // Match interpreter behavior: print 0x... instead of ptr<0x...>
            fprintf(out, "%p", val.as.as_ptr);
            break;
        case HML_VAL_BUFFER:
            if (val.as.as_buffer) {
                fprintf(out, "<buffer %p length=%d capacity=%d>",
                    (void*)val.as.as_buffer->data, val.as.as_buffer->length, val.as.as_buffer->capacity);
            } else {
                fprintf(out, "buffer[null]");
            }
            break;
        case HML_VAL_ARRAY:
            if (val.as.as_array) {
                fprintf(out, "[");
                for (int i = 0; i < val.as.as_array->length; i++) {
                    if (i > 0) fprintf(out, ", ");
                    // Print all elements consistently (no special quotes for strings)
                    print_value_to(out, val.as.as_array->elements[i]);
                }
                fprintf(out, "]");
            } else {
                fprintf(out, "[]");
            }
            break;
        case HML_VAL_OBJECT:
            // Match interpreter behavior: print <object> instead of JSON
            fprintf(out, "<object>");
            break;
        case HML_VAL_FUNCTION:
            fprintf(out, "<function>");
            break;
        case HML_VAL_BUILTIN_FN:
            fprintf(out, "<builtin>");
            break;
        case HML_VAL_TASK:
            fprintf(out, "<task>");
            break;
        case HML_VAL_CHANNEL:
            fprintf(out, "<channel>");
            break;
        case HML_VAL_FILE:
            fprintf(out, "<file>");
            break;
        default:
            fprintf(out, "<unknown>");
            break;
    }
}

void hml_print(HmlValue val) {
    print_value_to(stdout, val);
    printf("\n");
    fflush(stdout);
}

void hml_eprint(HmlValue val) {
    print_value_to(stderr, val);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// I/O builtins as first-class functions
HmlValue hml_builtin_print(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_print(val);
    return hml_val_null();
}

HmlValue hml_builtin_println(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_print(val);
    return hml_val_null();
}

HmlValue hml_builtin_eprint(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_eprint(val);
    return hml_val_null();
}

// ========== VALUE COMPARISON ==========

int hml_values_equal(HmlValue left, HmlValue right) {
    // Null comparison
    if (left.type == HML_VAL_NULL || right.type == HML_VAL_NULL) {
        return (left.type == HML_VAL_NULL && right.type == HML_VAL_NULL);
    }

    // Boolean comparison
    if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
        return (left.as.as_bool == right.as.as_bool);
    }

    // String comparison
    if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
        if (!left.as.as_string || !right.as.as_string) return 0;
        return (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
    }

    // Numeric comparison
    if (hml_is_numeric(left) && hml_is_numeric(right)) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        return (l == r);
    }

    // Reference equality for arrays/objects
    if (left.type == HML_VAL_ARRAY && right.type == HML_VAL_ARRAY) {
        return (left.as.as_array == right.as.as_array);
    }
    if (left.type == HML_VAL_OBJECT && right.type == HML_VAL_OBJECT) {
        return (left.as.as_object == right.as.as_object);
    }

    // Different types are not equal
    return 0;
}

// ========== TYPE CHECKING ==========

const char* hml_typeof(HmlValue val) {
    return hml_typeof_str(val);
}

void hml_check_type(HmlValue val, HmlValueType expected, const char *var_name) {
    if (val.type != expected) {
        hml_runtime_error("Type mismatch for '%s': expected %s, got %s",
                var_name, hml_type_name(expected), hml_typeof_str(val));
    }
}

// Helper to check if a value is an integer type
int hml_is_integer_type(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
            return 1;
        default:
            return 0;
    }
}

// Helper to check if a value is a float type
int hml_is_float_type(HmlValue val) {
    return val.type == HML_VAL_F32 || val.type == HML_VAL_F64;
}

// Helper to extract int64 from any numeric value
int64_t hml_val_to_int64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return val.as.as_i8;
        case HML_VAL_I16: return val.as.as_i16;
        case HML_VAL_I32: return val.as.as_i32;
        case HML_VAL_I64: return val.as.as_i64;
        case HML_VAL_U8:  return val.as.as_u8;
        case HML_VAL_U16: return val.as.as_u16;
        case HML_VAL_U32: return val.as.as_u32;
        case HML_VAL_U64: return (int64_t)val.as.as_u64;
        case HML_VAL_F32: return (int64_t)val.as.as_f32;
        case HML_VAL_F64: return (int64_t)val.as.as_f64;
        case HML_VAL_BOOL: return val.as.as_bool ? 1 : 0;
        case HML_VAL_RUNE: return val.as.as_rune;
        default: return 0;
    }
}

// Helper to extract double from any numeric value
double hml_val_to_double(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return (double)val.as.as_i8;
        case HML_VAL_I16: return (double)val.as.as_i16;
        case HML_VAL_I32: return (double)val.as.as_i32;
        case HML_VAL_I64: return (double)val.as.as_i64;
        case HML_VAL_U8:  return (double)val.as.as_u8;
        case HML_VAL_U16: return (double)val.as.as_u16;
        case HML_VAL_U32: return (double)val.as.as_u32;
        case HML_VAL_U64: return (double)val.as.as_u64;
        case HML_VAL_F32: return (double)val.as.as_f32;
        case HML_VAL_F64: return val.as.as_f64;
        default: return 0.0;
    }
}

// Helper to check if a type is numeric (for type annotations)
static int hml_is_numeric_target_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
        case HML_VAL_F32: case HML_VAL_F64:
            return 1;
        default:
            return 0;
    }
}

HmlValue hml_convert_to_type(HmlValue val, HmlValueType target_type) {
    // If already the target type, return as-is
    if (val.type == target_type) {
        return val;
    }

    // Extract source value
    int64_t int_val = 0;
    double float_val = 0.0;
    int is_source_float = hml_is_float_type(val);

    if (hml_is_integer_type(val) || val.type == HML_VAL_BOOL || val.type == HML_VAL_RUNE) {
        int_val = hml_val_to_int64(val);
    } else if (is_source_float) {
        float_val = hml_val_to_double(val);
    } else if (val.type == HML_VAL_STRING && target_type == HML_VAL_STRING) {
        return val;
    } else if (val.type == HML_VAL_STRING && target_type == HML_VAL_BOOL) {
        // String to bool via type annotation is not allowed
        // Use explicit conversion: bool("true") or bool("false")
        hml_runtime_error("Cannot convert string to bool via type annotation. Use bool(\"...\") instead.");
        return hml_val_null();
    } else if (val.type == HML_VAL_STRING && hml_is_numeric_target_type(target_type)) {
        // String to numeric via type annotation is not allowed
        // Use explicit conversion: i32("42"), f64("3.14"), etc.
        hml_runtime_error("Cannot convert string to %s via type annotation. Use %s(\"...\") instead.",
                hml_type_name(target_type), hml_type_name(target_type));
        return hml_val_null();
    } else if (val.type == HML_VAL_NULL && target_type == HML_VAL_NULL) {
        return val;
    } else {
        hml_runtime_error("Cannot convert %s to %s",
                hml_type_name(val.type), hml_type_name(target_type));
        return hml_val_null();  // Never reached, but silences compiler warning
    }

    switch (target_type) {
        case HML_VAL_I8:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -128 || int_val > 127) {
                hml_runtime_error("Value %ld out of range for i8 [-128, 127]", int_val);
            }
            return hml_val_i8((int8_t)int_val);

        case HML_VAL_I16:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -32768 || int_val > 32767) {
                hml_runtime_error("Value %ld out of range for i16 [-32768, 32767]", int_val);
            }
            return hml_val_i16((int16_t)int_val);

        case HML_VAL_I32:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -2147483648LL || int_val > 2147483647LL) {
                hml_runtime_error("Value %ld out of range for i32 [-2147483648, 2147483647]", int_val);
            }
            return hml_val_i32((int32_t)int_val);

        case HML_VAL_I64:
            if (is_source_float) int_val = (int64_t)float_val;
            return hml_val_i64(int_val);

        case HML_VAL_U8:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 255) {
                hml_runtime_error("Value %ld out of range for u8 [0, 255]", int_val);
            }
            return hml_val_u8((uint8_t)int_val);

        case HML_VAL_U16:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 65535) {
                hml_runtime_error("Value %ld out of range for u16 [0, 65535]", int_val);
            }
            return hml_val_u16((uint16_t)int_val);

        case HML_VAL_U32:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 4294967295LL) {
                hml_runtime_error("Value %ld out of range for u32 [0, 4294967295]", int_val);
            }
            return hml_val_u32((uint32_t)int_val);

        case HML_VAL_U64:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0) {
                hml_runtime_error("Value %ld out of range for u64 [0, 18446744073709551615]", int_val);
            }
            return hml_val_u64((uint64_t)int_val);

        case HML_VAL_F32:
            if (is_source_float) {
                return hml_val_f32((float)float_val);
            } else {
                return hml_val_f32((float)int_val);
            }

        case HML_VAL_F64:
            if (is_source_float) {
                return hml_val_f64(float_val);
            } else {
                return hml_val_f64((double)int_val);
            }

        case HML_VAL_RUNE:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 0x10FFFF) {
                hml_runtime_error("Value %ld out of range for rune [0, 0x10FFFF]", int_val);
            }
            return hml_val_rune((uint32_t)int_val);

        case HML_VAL_BOOL:
            // String -> bool is handled above with early return
            // Allow conversion from numeric types to bool (0 = false, non-zero = true)
            if (is_source_float) {
                return hml_val_bool(float_val != 0.0);
            }
            return hml_val_bool(int_val != 0);

        case HML_VAL_STRING:
            // Allow conversion from rune to string (match interpreter behavior)
            if (val.type == HML_VAL_RUNE) {
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode_rune(val.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';
                return hml_val_string(rune_bytes);
            }
            // Allow conversion from bool to string
            if (val.type == HML_VAL_BOOL) {
                return hml_val_string(val.as.as_bool ? "true" : "false");
            }
            // Allow conversion from numeric types to string
            if (hml_is_integer_type(val)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%ld", hml_val_to_int64(val));
                return hml_val_string(buf);
            }
            if (hml_is_float_type(val)) {
                char buf[64];
                double fval = hml_val_to_double(val);
                // Use %g to avoid trailing zeros, but ensure we get enough precision
                snprintf(buf, sizeof(buf), "%.17g", fval);
                return hml_val_string(buf);
            }
            hml_runtime_error("Cannot convert %s to string", hml_type_name(val.type));
            return hml_val_null();

        default:
            // For other types, return as-is
            return val;
    }
}

// Parse a value to a target type (for type constructors like i32("42"))
// This function ALLOWS string parsing, unlike hml_convert_to_type
HmlValue hml_parse_string_to_type(HmlValue val, HmlValueType target_type) {
    // If already the target type, return as-is
    if (val.type == target_type) {
        return val;
    }

    // Handle string parsing for type constructors
    if (val.type == HML_VAL_STRING && target_type == HML_VAL_BOOL) {
        // String to bool: check for "true" or "false"
        HmlString *str = val.as.as_string;
        if (str && str->length == 4 &&
            str->data[0] == 't' && str->data[1] == 'r' &&
            str->data[2] == 'u' && str->data[3] == 'e') {
            return hml_val_bool(1);
        } else if (str && str->length == 5 &&
            str->data[0] == 'f' && str->data[1] == 'a' &&
            str->data[2] == 'l' && str->data[3] == 's' && str->data[4] == 'e') {
            return hml_val_bool(0);
        }
        hml_runtime_error("Cannot parse string as bool (expected 'true' or 'false')");
        return hml_val_null();
    } else if (val.type == HML_VAL_STRING && hml_is_numeric_target_type(target_type)) {
        // String to numeric conversion - parse the string
        HmlString *str = val.as.as_string;
        int64_t int_val = 0;
        double float_val = 0.0;
        int is_float = 0;

        if (str && str->length > 0) {
            // Create null-terminated copy for parsing
            char *cstr = malloc(str->length + 1);
            memcpy(cstr, str->data, str->length);
            cstr[str->length] = '\0';

            // Try to parse as number
            char *endptr;

            // Check for float (contains '.' or 'e'/'E')
            int has_decimal = 0;
            for (int64_t i = 0; i < str->length; i++) {
                if (cstr[i] == '.' || cstr[i] == 'e' || cstr[i] == 'E') {
                    has_decimal = 1;
                    break;
                }
            }

            if (has_decimal) {
                float_val = strtod(cstr, &endptr);
                if (endptr == cstr || *endptr != '\0') {
                    hml_runtime_error("Cannot parse '%s' as number", cstr);
                }
                is_float = 1;
            } else {
                int_val = strtoll(cstr, &endptr, 0);  // base 0 supports hex, octal
                if (endptr == cstr || *endptr != '\0') {
                    hml_runtime_error("Cannot parse '%s' as integer", cstr);
                }
            }
            free(cstr);
        } else {
            hml_runtime_error("Cannot convert empty string to number");
        }

        // Now convert to target type with range checking
        switch (target_type) {
            case HML_VAL_I8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -128 || int_val > 127) {
                    hml_runtime_error("Value %ld out of range for i8 [-128, 127]", int_val);
                }
                return hml_val_i8((int8_t)int_val);

            case HML_VAL_I16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -32768 || int_val > 32767) {
                    hml_runtime_error("Value %ld out of range for i16 [-32768, 32767]", int_val);
                }
                return hml_val_i16((int16_t)int_val);

            case HML_VAL_I32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -2147483648LL || int_val > 2147483647LL) {
                    hml_runtime_error("Value %ld out of range for i32", int_val);
                }
                return hml_val_i32((int32_t)int_val);

            case HML_VAL_I64:
                if (is_float) int_val = (int64_t)float_val;
                return hml_val_i64(int_val);

            case HML_VAL_U8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 255) {
                    hml_runtime_error("Value %ld out of range for u8 [0, 255]", int_val);
                }
                return hml_val_u8((uint8_t)int_val);

            case HML_VAL_U16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 65535) {
                    hml_runtime_error("Value %ld out of range for u16 [0, 65535]", int_val);
                }
                return hml_val_u16((uint16_t)int_val);

            case HML_VAL_U32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 4294967295LL) {
                    hml_runtime_error("Value %ld out of range for u32", int_val);
                }
                return hml_val_u32((uint32_t)int_val);

            case HML_VAL_U64:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0) {
                    hml_runtime_error("Value %ld out of range for u64", int_val);
                }
                return hml_val_u64((uint64_t)int_val);

            case HML_VAL_F32:
                if (is_float) {
                    return hml_val_f32((float)float_val);
                } else {
                    return hml_val_f32((float)int_val);
                }

            case HML_VAL_F64:
                if (is_float) {
                    return hml_val_f64(float_val);
                } else {
                    return hml_val_f64((double)int_val);
                }

            default:
                break;
        }
    }

    // For non-string values, fall back to regular hml_convert_to_type
    return hml_convert_to_type(val, target_type);
}

// ========== ASSERTIONS ==========

void hml_assert(HmlValue condition, HmlValue message) {
    if (!hml_to_bool(condition)) {
        // Throw catchable exception (match interpreter behavior)
        HmlValue exception_msg;
        if (message.type == HML_VAL_STRING && message.as.as_string) {
            exception_msg = message;
        } else {
            exception_msg = hml_val_string("assertion failed");
        }
        hml_throw(exception_msg);
    }
}

void hml_panic(HmlValue message) {
    fprintf(stderr, "panic: ");
    print_value_to(stderr, message);
    fprintf(stderr, "\n");
    exit(1);
}

// ========== COMMAND EXECUTION ==========

// SECURITY WARNING: exec() uses popen() which passes commands through a shell.
// This is vulnerable to command injection if the command string contains untrusted input.
// For safe command execution, use exec_argv() instead which bypasses the shell.
HmlValue hml_exec(HmlValue command) {
    if (command.type != HML_VAL_STRING || !command.as.as_string) {
        hml_runtime_error("exec() argument must be a string");
    }

    HmlString *cmd_str = command.as.as_string;

    // SECURITY: Warn about potentially dangerous shell metacharacters
    const char *dangerous_chars = ";|&$`\\\"'<>(){}[]!#";
    for (int64_t i = 0; i < cmd_str->length; i++) {
        for (const char *dc = dangerous_chars; *dc; dc++) {
            if (cmd_str->data[i] == *dc) {
                fprintf(stderr, "Warning: exec() command contains shell metacharacter '%c'. "
                        "Consider using exec_argv() for safer command execution.\n", *dc);
                goto done_warning;
            }
        }
    }
done_warning:
    ;  // Empty statement required after label (C99/C11)

    char *ccmd = malloc(cmd_str->length + 1);
    if (!ccmd) {
        hml_runtime_error("exec() memory allocation failed");
    }
    memcpy(ccmd, cmd_str->data, cmd_str->length);
    ccmd[cmd_str->length] = '\0';

    // Open pipe to read command output (uses shell - vulnerable to injection)
    FILE *pipe = popen(ccmd, "r");
    if (!pipe) {
        fprintf(stderr, "Runtime error: Failed to execute command '%s': %s\n", ccmd, strerror(errno));
        free(ccmd);
        exit(1);
    }

    // Read output into buffer
    char *output_buffer = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output_buffer = malloc(output_capacity);
    if (!output_buffer) {
        fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
        pclose(pipe);
        free(ccmd);
        exit(1);
    }

    char chunk[4096];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        // Grow buffer if needed
        while (output_size + bytes_read > output_capacity) {
            // SECURITY: Check for overflow before doubling capacity
            if (output_capacity > SIZE_MAX / 2) {
                fprintf(stderr, "Runtime error: exec() output too large\n");
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                exit(1);
            }
            output_capacity *= 2;
            char *new_buffer = realloc(output_buffer, output_capacity);
            if (!new_buffer) {
                fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                exit(1);
            }
            output_buffer = new_buffer;
        }
        memcpy(output_buffer + output_size, chunk, bytes_read);
        output_size += bytes_read;
    }

    // Get exit code
    int status = pclose(pipe);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    free(ccmd);

    // Ensure string is null-terminated
    if (output_size >= output_capacity) {
        output_capacity = output_size + 1;
        char *new_buffer = realloc(output_buffer, output_capacity);
        if (!new_buffer) {
            fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
            free(output_buffer);
            exit(1);
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Create result object with output and exit_code
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "output", hml_val_string(output_buffer));
    hml_object_set_field(result, "exit_code", hml_val_i32(exit_code));
    free(output_buffer);

    return result;
}

// exec_argv() - Safe command execution without shell interpretation
// Takes an array of strings: [program, arg1, arg2, ...]
// Uses fork/execvp directly, preventing shell injection attacks
HmlValue hml_exec_argv(HmlValue args_array) {
    if (args_array.type != HML_VAL_ARRAY || !args_array.as.as_array) {
        hml_runtime_error("exec_argv() argument must be an array of strings");
    }

    HmlArray *arr = args_array.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("exec_argv() array must not be empty");
    }

    // Build argv array for execvp
    char **argv = malloc((arr->length + 1) * sizeof(char*));
    if (!argv) {
        hml_runtime_error("exec_argv() memory allocation failed");
    }

    for (int64_t i = 0; i < arr->length; i++) {
        HmlValue elem = arr->elements[i];
        if (elem.type != HML_VAL_STRING || !elem.as.as_string) {
            for (int64_t j = 0; j < i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec_argv() array elements must be strings");
        }
        HmlString *s = elem.as.as_string;
        argv[i] = malloc(s->length + 1);
        if (!argv[i]) {
            for (int64_t j = 0; j < i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec_argv() memory allocation failed");
        }
        memcpy(argv[i], s->data, s->length);
        argv[i][s->length] = '\0';
    }
    argv[arr->length] = NULL;

    // Create pipes for stdout
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        hml_runtime_error("exec_argv() pipe creation failed: %s", strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        hml_runtime_error("exec_argv() fork failed: %s", strerror(errno));
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);  // Close read end
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);

        execvp(argv[0], argv);
        // If execvp returns, it failed
        fprintf(stderr, "exec_argv() failed to execute '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    // Parent process
    close(stdout_pipe[1]);  // Close write end

    // Free argv in parent (child has its own copy after fork)
    for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
    free(argv);

    // Read output from child
    char *output_buffer = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output_buffer = malloc(output_capacity);
    if (!output_buffer) {
        close(stdout_pipe[0]);
        hml_runtime_error("exec_argv() memory allocation failed");
    }

    char chunk[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(stdout_pipe[0], chunk, sizeof(chunk))) > 0) {
        // Check for overflow before doubling capacity
        while (output_size + (size_t)bytes_read > output_capacity) {
            if (output_capacity > SIZE_MAX / 2) {
                free(output_buffer);
                close(stdout_pipe[0]);
                hml_runtime_error("exec_argv() output too large");
            }
            output_capacity *= 2;
            char *new_buffer = realloc(output_buffer, output_capacity);
            if (!new_buffer) {
                free(output_buffer);
                close(stdout_pipe[0]);
                hml_runtime_error("exec_argv() memory allocation failed");
            }
            output_buffer = new_buffer;
        }
        memcpy(output_buffer + output_size, chunk, (size_t)bytes_read);
        output_size += (size_t)bytes_read;
    }
    close(stdout_pipe[0]);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Ensure null termination
    if (output_size >= output_capacity) {
        char *new_buffer = realloc(output_buffer, output_size + 1);
        if (!new_buffer) {
            free(output_buffer);
            hml_runtime_error("exec_argv() memory allocation failed");
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Create result object
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "output", hml_val_string(output_buffer));
    hml_object_set_field(result, "exit_code", hml_val_i32(exit_code));
    free(output_buffer);

    return result;
}

// Math operations moved to builtins_math.c

// Time builtin wrappers moved to builtins_time.c

// Env builtin wrappers
HmlValue hml_builtin_getenv(HmlClosureEnv *env, HmlValue name) {
    (void)env;
    return hml_getenv(name);
}

HmlValue hml_builtin_setenv(HmlClosureEnv *env, HmlValue name, HmlValue value) {
    (void)env;
    hml_setenv(name, value);
    return hml_val_null();
}

HmlValue hml_builtin_exit(HmlClosureEnv *env, HmlValue code) {
    (void)env;
    hml_exit(code);
    return hml_val_null();  // Never reached
}

HmlValue hml_builtin_get_pid(HmlClosureEnv *env) {
    (void)env;
    return hml_get_pid();
}

HmlValue hml_builtin_exec(HmlClosureEnv *env, HmlValue command) {
    (void)env;
    return hml_exec(command);
}

HmlValue hml_builtin_exec_argv(HmlClosureEnv *env, HmlValue args_array) {
    (void)env;
    return hml_exec_argv(args_array);
}

// Process ID builtins
HmlValue hml_getppid(void) {
    return hml_val_i32((int32_t)getppid());
}

HmlValue hml_getuid(void) {
    return hml_val_i32((int32_t)getuid());
}

HmlValue hml_geteuid(void) {
    return hml_val_i32((int32_t)geteuid());
}

HmlValue hml_getgid(void) {
    return hml_val_i32((int32_t)getgid());
}

HmlValue hml_getegid(void) {
    return hml_val_i32((int32_t)getegid());
}

HmlValue hml_unsetenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return hml_val_null();
    }
    unsetenv(name.as.as_string->data);
    return hml_val_null();
}

HmlValue hml_kill(HmlValue pid, HmlValue sig) {
    int p = hml_to_i32(pid);
    int s = hml_to_i32(sig);
    int result = kill(p, s);
    return hml_val_i32(result);
}

HmlValue hml_fork(void) {
    pid_t pid = fork();
    return hml_val_i32((int32_t)pid);
}

HmlValue hml_wait(void) {
    int status;
    pid_t pid = wait(&status);
    // Return object with pid and status
    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "pid", hml_val_i32((int32_t)pid));
    hml_object_set_field(obj, "status", hml_val_i32(status));
    return obj;
}

HmlValue hml_waitpid(HmlValue pid, HmlValue options) {
    int status;
    pid_t result = waitpid(hml_to_i32(pid), &status, hml_to_i32(options));
    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "pid", hml_val_i32((int32_t)result));
    hml_object_set_field(obj, "status", hml_val_i32(status));
    return obj;
}

void hml_abort(void) {
    abort();
}

// Process builtin wrappers
HmlValue hml_builtin_getppid(HmlClosureEnv *env) {
    (void)env;
    return hml_getppid();
}

HmlValue hml_builtin_getuid(HmlClosureEnv *env) {
    (void)env;
    return hml_getuid();
}

HmlValue hml_builtin_geteuid(HmlClosureEnv *env) {
    (void)env;
    return hml_geteuid();
}

HmlValue hml_builtin_getgid(HmlClosureEnv *env) {
    (void)env;
    return hml_getgid();
}

HmlValue hml_builtin_getegid(HmlClosureEnv *env) {
    (void)env;
    return hml_getegid();
}

HmlValue hml_builtin_unsetenv(HmlClosureEnv *env, HmlValue name) {
    (void)env;
    return hml_unsetenv(name);
}

HmlValue hml_builtin_kill(HmlClosureEnv *env, HmlValue pid, HmlValue sig) {
    (void)env;
    return hml_kill(pid, sig);
}

HmlValue hml_builtin_fork(HmlClosureEnv *env) {
    (void)env;
    return hml_fork();
}

HmlValue hml_builtin_wait(HmlClosureEnv *env) {
    (void)env;
    return hml_wait();
}

HmlValue hml_builtin_waitpid(HmlClosureEnv *env, HmlValue pid, HmlValue options) {
    (void)env;
    return hml_waitpid(pid, options);
}

HmlValue hml_builtin_abort(HmlClosureEnv *env) {
    (void)env;
    hml_abort();
    return hml_val_null();  // Never reached
}

// Time and datetime operations moved to builtins_time.c

// ========== ENVIRONMENT OPERATIONS ==========

HmlValue hml_getenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return hml_val_null();
    }
    char *value = getenv(name.as.as_string->data);
    if (!value) {
        return hml_val_null();
    }
    return hml_val_string(value);
}

void hml_setenv(HmlValue name, HmlValue value) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return;
    }
    if (value.type != HML_VAL_STRING || !value.as.as_string) {
        return;
    }
    setenv(name.as.as_string->data, value.as.as_string->data, 1);
}

void hml_exit(HmlValue code) {
    exit(hml_to_i32(code));
}

HmlValue hml_get_pid(void) {
    return hml_val_i32((int32_t)getpid());
}

// ========== I/O OPERATIONS ==========

HmlValue hml_read_line(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, stdin);
    if (read == -1) {
        free(line);
        return hml_val_null();
    }
    // Remove trailing newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
        read--;
    }
    HmlValue result = hml_val_string(line);
    free(line);
    return result;
}

// ========== TYPE OPERATIONS ==========

HmlValue hml_sizeof(HmlValue type_name) {
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        return hml_val_i32(0);
    }
    const char *name = type_name.as.as_string->data;
    if (strcmp(name, "i8") == 0 || strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return hml_val_i32(1);
    if (strcmp(name, "i16") == 0 || strcmp(name, "u16") == 0) return hml_val_i32(2);
    if (strcmp(name, "i32") == 0 || strcmp(name, "u32") == 0 || strcmp(name, "integer") == 0) return hml_val_i32(4);
    if (strcmp(name, "i64") == 0 || strcmp(name, "u64") == 0) return hml_val_i32(8);
    if (strcmp(name, "f32") == 0) return hml_val_i32(4);
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return hml_val_i32(8);
    if (strcmp(name, "bool") == 0) return hml_val_i32(1);
    if (strcmp(name, "ptr") == 0) return hml_val_i32(8);
    if (strcmp(name, "rune") == 0) return hml_val_i32(4);
    return hml_val_i32(0);
}

// ========== BINARY OPERATIONS ==========

// Type promotion table (higher number = higher priority)
static int type_priority(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8:   return 1;
        case HML_VAL_U8:   return 2;
        case HML_VAL_I16:  return 3;
        case HML_VAL_U16:  return 4;
        case HML_VAL_I32:  return 5;
        case HML_VAL_RUNE: return 5;  // Runes promote like i32
        case HML_VAL_U32:  return 6;
        case HML_VAL_I64:  return 7;
        case HML_VAL_U64:  return 8;
        case HML_VAL_F32:  return 9;
        case HML_VAL_F64:  return 10;
        default:          return 0;
    }
}

static HmlValueType promote_types(HmlValueType a, HmlValueType b) {
    // If either is f64, result is f64
    if (a == HML_VAL_F64 || b == HML_VAL_F64) return HML_VAL_F64;

    // f32 with i64/u64 should promote to f64 to preserve precision
    // (f32 has only 24-bit mantissa, i64/u64 need 53+ bits)
    if (a == HML_VAL_F32 || b == HML_VAL_F32) {
        HmlValueType other = (a == HML_VAL_F32) ? b : a;
        if (other == HML_VAL_I64 || other == HML_VAL_U64) {
            return HML_VAL_F64;
        }
        return HML_VAL_F32;
    }

    // Runes promote to i32 when combined with other types
    if (a == HML_VAL_RUNE && b == HML_VAL_RUNE) return HML_VAL_I32;
    if (a == HML_VAL_RUNE) return (type_priority(HML_VAL_I32) >= type_priority(b)) ? HML_VAL_I32 : b;
    if (b == HML_VAL_RUNE) return (type_priority(HML_VAL_I32) >= type_priority(a)) ? HML_VAL_I32 : a;

    // Otherwise, higher priority wins
    return (type_priority(a) >= type_priority(b)) ? a : b;
}

// Create an integer result value with the correct type
static HmlValue make_int_result(HmlValueType result_type, int64_t value) {
    switch (result_type) {
        case HML_VAL_I8:  return hml_val_i8((int8_t)value);
        case HML_VAL_I16: return hml_val_i16((int16_t)value);
        case HML_VAL_I32: return hml_val_i32((int32_t)value);
        case HML_VAL_I64: return hml_val_i64(value);
        case HML_VAL_U8:  return hml_val_u8((uint8_t)value);
        case HML_VAL_U16: return hml_val_u16((uint16_t)value);
        case HML_VAL_U32: return hml_val_u32((uint32_t)value);
        case HML_VAL_U64: return hml_val_u64((uint64_t)value);
        default:          return hml_val_i64(value);
    }
}

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right) {
    // Division always uses float regardless of operand types
    if (op == HML_OP_DIV) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        if (r == 0.0) hml_runtime_error("Division by zero");
        return hml_val_f64(l / r);
    }

    // FAST PATH: i32 operations (most common case)
    if (left.type == HML_VAL_I32 && right.type == HML_VAL_I32) {
        int32_t l = left.as.as_i32;
        int32_t r = right.as.as_i32;
        switch (op) {
            case HML_OP_ADD: return hml_val_i32(l + r);
            case HML_OP_SUB: return hml_val_i32(l - r);
            case HML_OP_MUL: return hml_val_i32(l * r);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i32(l % r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            case HML_OP_BIT_AND: return hml_val_i32(l & r);
            case HML_OP_BIT_OR: return hml_val_i32(l | r);
            case HML_OP_BIT_XOR: return hml_val_i32(l ^ r);
            case HML_OP_LSHIFT: return hml_val_i32(l << r);
            case HML_OP_RSHIFT: return hml_val_i32(l >> r);
            default: break;
        }
    }

    // FAST PATH: i64 operations
    if (left.type == HML_VAL_I64 && right.type == HML_VAL_I64) {
        int64_t l = left.as.as_i64;
        int64_t r = right.as.as_i64;
        switch (op) {
            case HML_OP_ADD: return hml_val_i64(l + r);
            case HML_OP_SUB: return hml_val_i64(l - r);
            case HML_OP_MUL: return hml_val_i64(l * r);
            case HML_OP_DIV:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i64(l / r);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i64(l % r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            case HML_OP_BIT_AND: return hml_val_i64(l & r);
            case HML_OP_BIT_OR: return hml_val_i64(l | r);
            case HML_OP_BIT_XOR: return hml_val_i64(l ^ r);
            case HML_OP_LSHIFT: return hml_val_i64(l << r);
            case HML_OP_RSHIFT: return hml_val_i64(l >> r);
            default: break;
        }
    }

    // FAST PATH: f64 operations
    if (left.type == HML_VAL_F64 && right.type == HML_VAL_F64) {
        double l = left.as.as_f64;
        double r = right.as.as_f64;
        switch (op) {
            case HML_OP_ADD: return hml_val_f64(l + r);
            case HML_OP_SUB: return hml_val_f64(l - r);
            case HML_OP_MUL: return hml_val_f64(l * r);
            case HML_OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                return hml_val_f64(l / r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            default: break;
        }
    }

    // String concatenation
    if (op == HML_OP_ADD && (left.type == HML_VAL_STRING || right.type == HML_VAL_STRING)) {
        return hml_string_concat(left, right);
    }

    // Boolean operations
    if (op == HML_OP_AND) {
        return hml_val_bool(hml_to_bool(left) && hml_to_bool(right));
    }
    if (op == HML_OP_OR) {
        return hml_val_bool(hml_to_bool(left) || hml_to_bool(right));
    }

    // Equality/inequality work on all types
    if (op == HML_OP_EQUAL || op == HML_OP_NOT_EQUAL) {
        int equal = 0;
        if (left.type == HML_VAL_NULL || right.type == HML_VAL_NULL) {
            equal = (left.type == HML_VAL_NULL && right.type == HML_VAL_NULL);
        } else if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
            equal = (left.as.as_bool == right.as.as_bool);
        } else if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
            equal = (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
        } else if (left.type == HML_VAL_RUNE && right.type == HML_VAL_RUNE) {
            equal = (left.as.as_rune == right.as.as_rune);
        } else if (left.type == HML_VAL_PTR && right.type == HML_VAL_PTR) {
            equal = (left.as.as_ptr == right.as.as_ptr);
        } else if (hml_is_numeric(left) && hml_is_numeric(right)) {
            double l = hml_to_f64(left);
            double r = hml_to_f64(right);
            equal = (l == r);
        } else {
            equal = 0;  // Different types are not equal
        }
        return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
    }

    // Rune comparison operations (ordering)
    if (left.type == HML_VAL_RUNE && right.type == HML_VAL_RUNE) {
        uint32_t l = left.as.as_rune;
        uint32_t r = right.as.as_rune;
        switch (op) {
            case HML_OP_LESS:          return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(l <= r);
            case HML_OP_GREATER:       return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                hml_runtime_error("Invalid operation for rune type");
        }
    }

    // String comparison operations (ordering)
    if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
        int cmp = strcmp(left.as.as_string->data, right.as.as_string->data);
        switch (op) {
            case HML_OP_LESS:          return hml_val_bool(cmp < 0);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(cmp <= 0);
            case HML_OP_GREATER:       return hml_val_bool(cmp > 0);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(cmp >= 0);
            default:
                hml_runtime_error("Invalid operation for string type");
        }
    }

    // Pointer arithmetic: ptr + int or ptr - int
    if (left.type == HML_VAL_PTR && hml_is_numeric(right)) {
        int64_t offset = hml_to_i64(right);
        switch (op) {
            case HML_OP_ADD:
                return hml_val_ptr((char*)left.as.as_ptr + offset);
            case HML_OP_SUB:
                return hml_val_ptr((char*)left.as.as_ptr - offset);
            default:
                hml_runtime_error("Invalid operation for pointer type");
        }
    }

    // Pointer comparisons (both null and non-null)
    if (left.type == HML_VAL_PTR && right.type == HML_VAL_PTR) {
        void *lp = left.as.as_ptr;
        void *rp = right.as.as_ptr;
        switch (op) {
            case HML_OP_EQUAL:         return hml_val_bool(lp == rp);
            case HML_OP_NOT_EQUAL:     return hml_val_bool(lp != rp);
            case HML_OP_LESS:          return hml_val_bool(lp < rp);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(lp <= rp);
            case HML_OP_GREATER:       return hml_val_bool(lp > rp);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(lp >= rp);
            default:
                hml_runtime_error("Invalid operation for pointer type");
        }
    }

    // Numeric operations
    if (!hml_is_numeric(left) || !hml_is_numeric(right)) {
        hml_runtime_error("Cannot perform numeric operation on non-numeric types");
    }

    HmlValueType result_type = promote_types(left.type, right.type);

    // Float operations
    if (result_type == HML_VAL_F64 || result_type == HML_VAL_F32) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        double result;

        switch (op) {
            case HML_OP_ADD:      result = l + r; break;
            case HML_OP_SUB:      result = l - r; break;
            case HML_OP_MUL:      result = l * r; break;
            case HML_OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                result = l / r;
                break;
            case HML_OP_MOD:
                // IEEE 754: fmod with zero returns NaN
                result = fmod(l, r);
                break;
            case HML_OP_LESS:         return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
            case HML_OP_GREATER:      return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                hml_runtime_error("Invalid operation for floats");
        }
        // Return f32 or f64 based on the promoted type
        if (result_type == HML_VAL_F32) {
            return hml_val_f32((float)result);
        }
        return hml_val_f64(result);
    }

    // Integer operations
    int64_t l = hml_to_i64(left);
    int64_t r = hml_to_i64(right);

    switch (op) {
        case HML_OP_ADD:
            return make_int_result(result_type, l + r);
        case HML_OP_SUB:
            return make_int_result(result_type, l - r);
        case HML_OP_MUL:
            return make_int_result(result_type, l * r);
        case HML_OP_DIV:
            if (r == 0) {
                hml_runtime_error("Division by zero");
            }
            return make_int_result(result_type, l / r);
        case HML_OP_MOD:
            if (r == 0) {
                hml_runtime_error("Division by zero");
            }
            return make_int_result(result_type, l % r);
        case HML_OP_LESS:         return hml_val_bool(l < r);
        case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
        case HML_OP_GREATER:      return hml_val_bool(l > r);
        case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
        case HML_OP_BIT_AND:
            return make_int_result(result_type, l & r);
        case HML_OP_BIT_OR:
            return make_int_result(result_type, l | r);
        case HML_OP_BIT_XOR:
            return make_int_result(result_type, l ^ r);
        case HML_OP_LSHIFT:
            return make_int_result(result_type, l << r);
        case HML_OP_RSHIFT:
            return make_int_result(result_type, l >> r);
        default:
            break;
    }

    hml_runtime_error("Unknown binary operation");
}

// ========== UNARY OPERATIONS ==========

HmlValue hml_unary_op(HmlUnaryOp op, HmlValue operand) {
    switch (op) {
        case HML_UNARY_NOT:
            return hml_val_bool(!hml_to_bool(operand));

        case HML_UNARY_NEGATE:
            if (!hml_is_numeric(operand)) {
                hml_runtime_error("Cannot negate non-numeric type");
            }
            if (operand.type == HML_VAL_F64) {
                return hml_val_f64(-operand.as.as_f64);
            } else if (operand.type == HML_VAL_F32) {
                return hml_val_f32(-operand.as.as_f32);
            } else if (operand.type == HML_VAL_I64) {
                return hml_val_i64(-operand.as.as_i64);
            } else {
                return hml_val_i32(-hml_to_i32(operand));
            }

        case HML_UNARY_BIT_NOT:
            if (!hml_is_integer(operand)) {
                hml_runtime_error("Bitwise NOT requires integer type");
            }
            // Preserve the original type
            switch (operand.type) {
                case HML_VAL_I8:  return hml_val_i8(~operand.as.as_i8);
                case HML_VAL_I16: return hml_val_i16(~operand.as.as_i16);
                case HML_VAL_I32: return hml_val_i32(~operand.as.as_i32);
                case HML_VAL_I64: return hml_val_i64(~operand.as.as_i64);
                case HML_VAL_U8:  return hml_val_u8(~operand.as.as_u8);
                case HML_VAL_U16: return hml_val_u16(~operand.as.as_u16);
                case HML_VAL_U32: return hml_val_u32(~operand.as.as_u32);
                case HML_VAL_U64: return hml_val_u64(~operand.as.as_u64);
                default: return hml_val_i32(~hml_to_i32(operand));
            }
    }

    return hml_val_null();
}

// ========== STRING OPERATIONS ==========

// Helper: Encode a Unicode codepoint to UTF-8, returns number of bytes written
static int encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

// OPTIMIZATION: In-place string append for pattern "x = x + y"
// If the left string has refcount == 1, we can mutate it in place
// This turns O(n²) repeated concatenation into O(n) amortized
HmlValue hml_string_append_inplace(HmlValue *dest, HmlValue src) {
    // Only works if dest is a string with refcount 1
    if (dest->type != HML_VAL_STRING || !dest->as.as_string) {
        // Fall back to regular concat
        HmlValue result = hml_string_concat(*dest, src);
        hml_release(dest);
        *dest = result;
        return result;
    }

    HmlString *sd = dest->as.as_string;

    // If refcount > 1, we can't mutate - fall back to concat
    if (sd->ref_count > 1) {
        HmlValue result = hml_string_concat(*dest, src);
        hml_release(dest);
        *dest = result;
        return result;
    }

    // FAST PATH: Appending a single rune (common in character-by-character string building)
    if (src.type == HML_VAL_RUNE) {
        char utf8_buf[4];
        int char_len = encode_utf8(src.as.as_rune, utf8_buf);
        int new_len = sd->length + char_len;

        // Grow capacity if needed
        if (new_len + 1 > sd->capacity) {
            int new_capacity = sd->capacity * 2;
            if (new_capacity < new_len + 1) new_capacity = new_len + 1;
            if (new_capacity < 32) new_capacity = 32;

            char *new_data = realloc(sd->data, new_capacity);
            if (!new_data) {
                HmlValue result = hml_string_concat(*dest, src);
                hml_release(dest);
                *dest = result;
                return result;
            }
            sd->data = new_data;
            sd->capacity = new_capacity;
        }

        memcpy(sd->data + sd->length, utf8_buf, char_len);
        sd->length = new_len;
        sd->data[new_len] = '\0';
        sd->char_length = -1;
        return *dest;
    }

    // Get source string
    HmlValue str_src = (src.type == HML_VAL_STRING) ? src : hml_to_string(src);
    HmlString *ss = str_src.as.as_string;
    int src_len = ss ? ss->length : 0;

    if (src_len == 0) {
        if (src.type != HML_VAL_STRING) hml_release(&str_src);
        return *dest;
    }

    int new_len = sd->length + src_len;

    // Grow capacity if needed (with 2x growth factor for amortized O(1))
    if (new_len + 1 > sd->capacity) {
        int new_capacity = sd->capacity * 2;
        if (new_capacity < new_len + 1) new_capacity = new_len + 1;
        if (new_capacity < 32) new_capacity = 32;  // Minimum capacity

        char *new_data = realloc(sd->data, new_capacity);
        if (!new_data) {
            // Allocation failed - fall back to regular concat
            if (src.type != HML_VAL_STRING) hml_release(&str_src);
            HmlValue result = hml_string_concat(*dest, src);
            hml_release(dest);
            *dest = result;
            return result;
        }
        sd->data = new_data;
        sd->capacity = new_capacity;
    }

    // Append in place
    memcpy(sd->data + sd->length, ss->data, src_len);
    sd->length = new_len;
    sd->data[new_len] = '\0';
    sd->char_length = -1;  // Invalidate cached char length

    if (src.type != HML_VAL_STRING) hml_release(&str_src);

    // Return the same value (refcount unchanged since we're mutating in place)
    return *dest;
}

HmlValue hml_string_concat(HmlValue a, HmlValue b) {
    // FAST PATH: Both are already strings - avoid hml_to_string overhead
    if (a.type == HML_VAL_STRING && b.type == HML_VAL_STRING &&
        a.as.as_string && b.as.as_string) {
        HmlString *sa = a.as.as_string;
        HmlString *sb = b.as.as_string;
        int total = sa->length + sb->length;

        char *result = malloc(total + 1);
        memcpy(result, sa->data, sa->length);
        memcpy(result + sa->length, sb->data, sb->length);
        result[total] = '\0';

        return hml_val_string_owned(result, total, total + 1);
    }

    // Convert both to strings
    HmlValue str_a = hml_to_string(a);
    HmlValue str_b = hml_to_string(b);

    const char *s1 = hml_to_string_ptr(str_a);
    const char *s2 = hml_to_string_ptr(str_b);

    if (!s1) s1 = "";
    if (!s2) s2 = "";

    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int total = len1 + len2;

    char *result = malloc(total + 1);
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2);
    result[total] = '\0';

    // Release converted strings if they were newly created
    hml_release(&str_a);
    hml_release(&str_b);

    return hml_val_string_owned(result, total, total + 1);
}

HmlValue hml_to_string(HmlValue val) {
    if (val.type == HML_VAL_STRING) {
        hml_retain(&val);
        return val;
    }

    char buffer[256];
    switch (val.type) {
        case HML_VAL_I8:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            snprintf(buffer, sizeof(buffer), "%ld", val.as.as_i64);
            break;
        case HML_VAL_U8:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            snprintf(buffer, sizeof(buffer), "%lu", val.as.as_u64);
            break;
        case HML_VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            return hml_val_string(val.as.as_bool ? "true" : "false");
        case HML_VAL_NULL:
            return hml_val_string("null");
        case HML_VAL_RUNE:
            // Encode rune to UTF-8
            if (val.as.as_rune < 0x80) {
                buffer[0] = (char)val.as.as_rune;
                buffer[1] = '\0';
            } else if (val.as.as_rune < 0x800) {
                buffer[0] = (char)(0xC0 | (val.as.as_rune >> 6));
                buffer[1] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[2] = '\0';
            } else if (val.as.as_rune < 0x10000) {
                buffer[0] = (char)(0xE0 | (val.as.as_rune >> 12));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[2] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[3] = '\0';
            } else {
                buffer[0] = (char)(0xF0 | (val.as.as_rune >> 18));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 12) & 0x3F));
                buffer[2] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[3] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[4] = '\0';
            }
            return hml_val_string(buffer);
        default:
            return hml_val_string("<value>");
    }

    return hml_val_string(buffer);
}

// ========== STRING METHODS ==========
// Moved to builtins_string.c:
// - hml_string_length, hml_string_byte_length, hml_string_char_at, hml_string_byte_at
// - hml_string_substr, hml_string_slice, hml_string_find, hml_string_contains
// - hml_string_split, hml_string_trim, hml_string_to_upper, hml_string_to_lower
// - hml_string_starts_with, hml_string_ends_with, hml_string_replace, hml_string_replace_all
// - hml_string_repeat, hml_string_concat3/4/5, hml_string_concat_many
// - UTF-8 helpers, hml_string_index_assign, hml_string_char_count
// - hml_string_rune_at, hml_string_chars, hml_string_bytes, hml_string_to_bytes
// - hml_buffer_get, hml_buffer_set, hml_buffer_length, hml_buffer_capacity

// ========== POINTER INDEX OPERATIONS ==========

HmlValue hml_ptr_get(HmlValue ptr, HmlValue index) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("Pointer index requires pointer");
    }
    void *p = ptr.as.as_ptr;
    if (p == NULL) {
        hml_runtime_error("Cannot index into null pointer");
    }
    int idx = hml_to_i32(index);
    // Return the byte as u8 (matching interpreter behavior)
    return hml_val_u8(((unsigned char *)p)[idx]);
}

void hml_ptr_set(HmlValue ptr, HmlValue index, HmlValue val) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("Pointer index assignment requires pointer");
    }
    void *p = ptr.as.as_ptr;
    if (p == NULL) {
        hml_runtime_error("Cannot index into null pointer");
    }
    int idx = hml_to_i32(index);
    // Treat as byte array (matching interpreter behavior)
    ((unsigned char *)p)[idx] = (unsigned char)hml_to_i32(val);
}

// ========== FFI CALLBACK OPERATIONS ==========

// Forward declaration
HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue param_types, HmlValue return_type);
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr);

// Create an FFI callback that wraps a Hemlock function
// Delegates to hml_builtin_callback which has the full implementation
HmlValue hml_callback_create(HmlValue fn, HmlValue arg_types, HmlValue ret_type) {
    return hml_builtin_callback(NULL, fn, arg_types, ret_type);
}

// Free an FFI callback
void hml_callback_free(HmlValue callback) {
    hml_builtin_callback_free(NULL, callback);
}

// ========== MEMORY OPERATIONS ==========

HmlValue hml_alloc(int32_t size) {
    if (size <= 0) {
        hml_runtime_error("alloc() requires positive size");
    }
    void *ptr = malloc(size);
    if (!ptr) {
        return hml_val_null();
    }
    return hml_val_ptr(ptr);
}

void hml_free(HmlValue ptr_or_buffer) {
    if (ptr_or_buffer.type == HML_VAL_PTR) {
        if (ptr_or_buffer.as.as_ptr) {
            free(ptr_or_buffer.as.as_ptr);
        }
    } else if (ptr_or_buffer.type == HML_VAL_BUFFER) {
        if (ptr_or_buffer.as.as_buffer) {
            if (ptr_or_buffer.as.as_buffer->data) {
                free(ptr_or_buffer.as.as_buffer->data);
            }
            free(ptr_or_buffer.as.as_buffer);
        }
    } else if (ptr_or_buffer.type == HML_VAL_ARRAY) {
        if (ptr_or_buffer.as.as_array) {
            HmlArray *arr = ptr_or_buffer.as.as_array;
            // Release all elements
            for (int i = 0; i < arr->length; i++) {
                hml_release(&arr->elements[i]);
            }
            free(arr->elements);
            free(arr);
        }
    } else if (ptr_or_buffer.type == HML_VAL_OBJECT) {
        if (ptr_or_buffer.as.as_object) {
            HmlObject *obj = ptr_or_buffer.as.as_object;
            // Release all field values and free names
            for (int i = 0; i < obj->num_fields; i++) {
                hml_release(&obj->field_values[i]);
                free(obj->field_names[i]);
            }
            free(obj->field_names);
            free(obj->field_values);
            if (obj->type_name) free(obj->type_name);
            free(obj);
        }
    } else if (ptr_or_buffer.type == HML_VAL_NULL) {
        // free(null) is a safe no-op (like C's free(NULL))
    } else {
        hml_runtime_error("free() requires pointer, buffer, object, or array");
    }
}

HmlValue hml_realloc(HmlValue ptr, int32_t new_size) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("realloc() requires pointer");
    }
    if (new_size <= 0) {
        hml_runtime_error("realloc() requires positive size");
    }
    void *new_ptr = realloc(ptr.as.as_ptr, new_size);
    if (!new_ptr) {
        return hml_val_null();
    }
    return hml_val_ptr(new_ptr);
}

void hml_memset(HmlValue ptr, uint8_t byte_val, int32_t size) {
    if (ptr.type == HML_VAL_PTR) {
        memset(ptr.as.as_ptr, byte_val, size);
    } else if (ptr.type == HML_VAL_BUFFER) {
        memset(ptr.as.as_buffer->data, byte_val, size);
    } else {
        hml_runtime_error("memset() requires pointer or buffer");
    }
}

void hml_memcpy(HmlValue dest, HmlValue src, int32_t size) {
    void *dest_ptr = NULL;
    void *src_ptr = NULL;

    if (dest.type == HML_VAL_PTR) {
        dest_ptr = dest.as.as_ptr;
    } else if (dest.type == HML_VAL_BUFFER) {
        dest_ptr = dest.as.as_buffer->data;
    } else {
        hml_runtime_error("memcpy() dest requires pointer or buffer");
    }

    if (src.type == HML_VAL_PTR) {
        src_ptr = src.as.as_ptr;
    } else if (src.type == HML_VAL_BUFFER) {
        src_ptr = src.as.as_buffer->data;
    } else {
        hml_runtime_error("memcpy() src requires pointer or buffer");
    }

    memcpy(dest_ptr, src_ptr, size);
}

int32_t hml_sizeof_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8:    return 1;
        case HML_VAL_U8:    return 1;
        case HML_VAL_I16:   return 2;
        case HML_VAL_U16:   return 2;
        case HML_VAL_I32:   return 4;
        case HML_VAL_U32:   return 4;
        case HML_VAL_I64:   return 8;
        case HML_VAL_U64:   return 8;
        case HML_VAL_F32:   return 4;
        case HML_VAL_F64:   return 8;
        case HML_VAL_BOOL:  return 1;
        case HML_VAL_PTR:   return 8;
        case HML_VAL_RUNE:  return 4;
        default:            return 0;
    }
}

// Helper to convert string type name to HmlValueType
static HmlValueType hml_type_from_string(const char *name) {
    if (strcmp(name, "i8") == 0) return HML_VAL_I8;
    if (strcmp(name, "i16") == 0) return HML_VAL_I16;
    if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) return HML_VAL_I32;
    if (strcmp(name, "i64") == 0) return HML_VAL_I64;
    if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return HML_VAL_U8;
    if (strcmp(name, "u16") == 0) return HML_VAL_U16;
    if (strcmp(name, "u32") == 0) return HML_VAL_U32;
    if (strcmp(name, "u64") == 0) return HML_VAL_U64;
    if (strcmp(name, "f32") == 0) return HML_VAL_F32;
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return HML_VAL_F64;
    if (strcmp(name, "bool") == 0) return HML_VAL_BOOL;
    if (strcmp(name, "ptr") == 0) return HML_VAL_PTR;
    if (strcmp(name, "rune") == 0) return HML_VAL_RUNE;
    return HML_VAL_NULL;  // Unknown type
}

HmlValue hml_talloc(HmlValue type_name, HmlValue count) {
    // Type name must be a string
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        hml_runtime_error("talloc() first argument must be a type name string");
    }

    // Count must be an integer
    if (!hml_is_integer(count)) {
        hml_runtime_error("talloc() second argument must be an integer count");
    }

    int32_t n = hml_to_i32(count);
    if (n <= 0) {
        hml_runtime_error("talloc() count must be positive");
    }

    HmlValueType elem_type = hml_type_from_string(type_name.as.as_string->data);
    if (elem_type == HML_VAL_NULL) {
        hml_runtime_error("talloc() unknown type '%s'", type_name.as.as_string->data);
    }

    int32_t elem_size = hml_sizeof_type(elem_type);
    if (elem_size == 0) {
        hml_runtime_error("talloc() type '%s' has no known size", type_name.as.as_string->data);
    }

    size_t total_size = (size_t)elem_size * (size_t)n;
    void *ptr = malloc(total_size);
    if (!ptr) {
        return hml_val_null();
    }

    return hml_val_ptr(ptr);
}

HmlValue hml_builtin_talloc(HmlClosureEnv *env, HmlValue type_name, HmlValue count) {
    (void)env;
    return hml_talloc(type_name, count);
}

// ========== ARRAY OPERATIONS ==========
// Moved to builtins_array.c:
// - Basic: push, pop, shift, unshift, insert, remove
// - Access: get, set, length, first, last, clear
// - Search: find, contains
// - Transform: slice, join, concat, reverse
// - Higher-order: map, filter, reduce
// - Typed arrays: validate and set element type constraints

// ========== OBJECT OPERATIONS ==========

HmlValue hml_object_get_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property access requires object (trying to get '%s' from type %s)",
                field, hml_typeof_str(obj));
    }

    HmlObject *o = obj.as.as_object;
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            HmlValue result = o->field_values[i];
            hml_retain(&result);
            return result;
        }
    }

    return hml_val_null();  // Field not found
}

// Get field from object - throws error if field not found (for strict property access)
HmlValue hml_object_get_field_required(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property access requires object (trying to get '%s' from type %s)",
                field, hml_typeof_str(obj));
    }

    HmlObject *o = obj.as.as_object;
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            HmlValue result = o->field_values[i];
            hml_retain(&result);
            return result;
        }
    }

    hml_runtime_error("Object has no field '%s' (use ?. for optional access)", field);
    return hml_val_null();  // Unreachable but needed for compiler
}

void hml_object_set_field(HmlValue obj, const char *field, HmlValue val) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property assignment requires object");
    }

    HmlObject *o = obj.as.as_object;

    // Check if field exists
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            hml_release(&o->field_values[i]);
            o->field_values[i] = val;
            hml_retain(&o->field_values[i]);
            return;
        }
    }

    // Add new field
    if (o->num_fields >= o->capacity) {
        int new_cap = (o->capacity == 0) ? 4 : o->capacity * 2;
        o->field_names = realloc(o->field_names, new_cap * sizeof(char*));
        o->field_values = realloc(o->field_values, new_cap * sizeof(HmlValue));
        o->capacity = new_cap;
    }

    o->field_names[o->num_fields] = strdup(field);
    o->field_values[o->num_fields] = val;
    hml_retain(&o->field_values[o->num_fields]);
    o->num_fields++;
}

int hml_object_has_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }

    HmlObject *o = obj.as.as_object;
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            return 1;
        }
    }
    return 0;
}

// Delete a field from object, returns 1 if deleted, 0 if not found
int hml_object_delete_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }

    HmlObject *o = obj.as.as_object;
    int found_index = -1;

    // Find the field
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return 0;  // Not found
    }

    // Release the value and free the field name
    hml_release(&o->field_values[found_index]);
    free(o->field_names[found_index]);

    // Shift remaining fields down
    for (int i = found_index; i < o->num_fields - 1; i++) {
        o->field_names[i] = o->field_names[i + 1];
        o->field_values[i] = o->field_values[i + 1];
    }

    o->num_fields--;
    return 1;  // Deleted
}

// Get number of fields in object
int hml_object_num_fields(HmlValue obj) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }
    return obj.as.as_object->num_fields;
}

// Get field name at index
HmlValue hml_object_key_at(HmlValue obj, int index) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object key access requires object");
    }
    HmlObject *o = obj.as.as_object;
    if (index < 0 || index >= o->num_fields) {
        hml_runtime_error("Object key index out of bounds");
    }
    return hml_val_string(o->field_names[index]);
}

// Get field value at index
HmlValue hml_object_value_at(HmlValue obj, int index) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object value access requires object");
    }
    HmlObject *o = obj.as.as_object;
    if (index < 0 || index >= o->num_fields) {
        hml_runtime_error("Object value index out of bounds");
    }
    HmlValue result = o->field_values[index];
    hml_retain(&result);
    return result;
}

// Get all keys of an object as an array
HmlValue hml_object_keys(HmlValue obj) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object has no method 'keys'");
    }
    HmlObject *o = obj.as.as_object;

    // Create a new array to hold the keys
    HmlValue arr = hml_val_array();

    // Add each field name to the array
    for (int i = 0; i < o->num_fields; i++) {
        hml_array_push(arr, hml_val_string(o->field_names[i]));
    }

    return arr;
}

// ========== OPTIMIZED SERIALIZATION (JSON) ==========
// Moved to builtins_serialization.c:
// - hml_serialize() - Convert values to JSON strings
// - hml_deserialize() - Parse JSON strings to values
// - JSON buffer, visited set, parser helpers

// ========== EXCEPTION HANDLING ==========

HmlExceptionContext* hml_exception_push(void) {
    HmlExceptionContext *ctx = malloc(sizeof(HmlExceptionContext));
    ctx->is_active = 1;
    ctx->exception_value = hml_val_null();
    ctx->prev = g_exception_stack;
    g_exception_stack = ctx;
    return ctx;
}

void hml_exception_pop(void) {
    if (g_exception_stack) {
        HmlExceptionContext *ctx = g_exception_stack;
        g_exception_stack = ctx->prev;
        hml_release(&ctx->exception_value);
        free(ctx);
    }
}

void hml_throw(HmlValue exception_value) {
    if (!g_exception_stack || !g_exception_stack->is_active) {
        // Uncaught exception
        fprintf(stderr, "Uncaught exception: ");
        print_value_to(stderr, exception_value);
        fprintf(stderr, "\n");
        exit(1);
    }

    g_exception_stack->exception_value = exception_value;
    hml_retain(&g_exception_stack->exception_value);
    longjmp(g_exception_stack->exception_buf, 1);
}

HmlValue hml_exception_get_value(void) {
    if (g_exception_stack) {
        HmlValue v = g_exception_stack->exception_value;
        hml_retain(&v);
        return v;
    }
    return hml_val_null();
}

// Runtime error helper - throws catchable exception with formatted message
void hml_runtime_error(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    HmlValue error_msg = hml_val_string(buffer);
    hml_throw(error_msg);
}

// ========== DEFER SUPPORT ==========

void hml_defer_push(HmlDeferFn fn, void *arg) {
    DeferEntry *entry = malloc(sizeof(DeferEntry));
    entry->fn = fn;
    entry->arg = arg;
    entry->next = g_defer_stack;
    g_defer_stack = entry;
}

void hml_defer_pop_and_execute(void) {
    if (g_defer_stack) {
        DeferEntry *entry = g_defer_stack;
        g_defer_stack = entry->next;
        entry->fn(entry->arg);
        free(entry);
    }
}

void hml_defer_execute_all(void) {
    while (g_defer_stack) {
        hml_defer_pop_and_execute();
    }
}

// Helper for deferring HmlValue function calls
static void hml_defer_call_wrapper(void *arg) {
    HmlValue *fn_ptr = (HmlValue *)arg;
    HmlValue result = hml_call_function(*fn_ptr, NULL, 0);
    hml_release(&result);
    hml_release(fn_ptr);
    free(fn_ptr);
}

void hml_defer_push_call(HmlValue fn) {
    HmlValue *fn_copy = malloc(sizeof(HmlValue));
    *fn_copy = fn;
    hml_retain(fn_copy);
    hml_defer_push(hml_defer_call_wrapper, fn_copy);
}

// Structure to hold a deferred call with arguments
typedef struct {
    HmlValue fn;
    HmlValue *args;
    int num_args;
} HmlDeferCallWithArgs;

// Helper for deferring HmlValue function calls with arguments
static void hml_defer_call_with_args_wrapper(void *arg) {
    HmlDeferCallWithArgs *call = (HmlDeferCallWithArgs *)arg;
    HmlValue result = hml_call_function(call->fn, call->args, call->num_args);
    hml_release(&result);
    // Release all args
    for (int i = 0; i < call->num_args; i++) {
        hml_release(&call->args[i]);
    }
    hml_release(&call->fn);
    free(call->args);
    free(call);
}

void hml_defer_push_call_with_args(HmlValue fn, HmlValue *args, int num_args) {
    HmlDeferCallWithArgs *call = malloc(sizeof(HmlDeferCallWithArgs));
    call->fn = fn;
    hml_retain(&call->fn);
    call->num_args = num_args;
    call->args = malloc(sizeof(HmlValue) * num_args);
    for (int i = 0; i < num_args; i++) {
        call->args[i] = args[i];
        hml_retain(&call->args[i]);
    }
    hml_defer_push(hml_defer_call_with_args_wrapper, call);
}

// ========== FUNCTION CALLS ==========

// Pre-created null value for fast padding (avoids repeated function calls)
static const HmlValue HML_NULL_VAL = { .type = HML_VAL_NULL };

// Function pointer typedefs for dispatch (declared once for reuse)
typedef HmlValue (*HmlFn0)(HmlClosureEnv*);
typedef HmlValue (*HmlFn1)(HmlClosureEnv*, HmlValue);
typedef HmlValue (*HmlFn2)(HmlClosureEnv*, HmlValue, HmlValue);
typedef HmlValue (*HmlFn3)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn4)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn5)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn6)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn7)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
typedef HmlValue (*HmlFn8)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);

// Hot path: dispatch function call with optimized branching
__attribute__((hot))
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args) {
    // Validate args pointer if we have arguments
    if (__builtin_expect(num_args > 0 && args == NULL, 0)) {
        hml_runtime_error("Function called with NULL args array");
    }

    // Fast path: builtin functions (common for stdlib)
    if (__builtin_expect(fn.type == HML_VAL_BUILTIN_FN, 0)) {
        return fn.as.as_builtin_fn(args, num_args);
    }

    // Main path: user-defined functions
    if (__builtin_expect(fn.type == HML_VAL_FUNCTION && fn.as.as_function != NULL, 1)) {
        HmlFunction *func = fn.as.as_function;
        void *fn_ptr = func->fn_ptr;

        // Null check (rare error case)
        if (__builtin_expect(fn_ptr == NULL, 0)) {
            hml_runtime_error("Function pointer is NULL");
        }

        int num_params = func->num_params;
        int num_required = func->num_required;
        int has_rest_param = func->has_rest_param;

        // Arity check (error cases are rare)
        if (__builtin_expect(num_args < num_required, 0)) {
            if (has_rest_param) {
                hml_runtime_error("Function expects at least %d arguments, got %d", num_required, num_args);
            } else {
                hml_runtime_error("Function expects %d arguments, got %d", num_required, num_args);
            }
        }
        // Only check max args if no rest param
        if (__builtin_expect(!has_rest_param && num_args > num_params, 0)) {
            hml_runtime_error("Function expects %d arguments, got %d", num_params, num_args);
        }

        HmlClosureEnv *env = (HmlClosureEnv*)func->closure_env;

        // Handle rest parameter: collect extra args into array
        // Function actually takes num_params + 1 params (last is rest array)
        if (has_rest_param) {
            HmlValue rest_array = hml_val_array();
            if (args != NULL) {
                for (int i = num_params; i < num_args; i++) {
                    hml_array_push(rest_array, args[i]);
                }
            }

            // Prepare padded args with rest array as last param
            HmlValue padded_args[8];
            int total_params = num_params + 1;  // Regular params + rest array

            // Copy provided args up to num_params
            int copy_count = (num_args < num_params) ? num_args : num_params;
            if (args != NULL) {
                for (int i = 0; i < copy_count; i++) {
                    padded_args[i] = args[i];
                }
            }
            // Fill remaining regular params with null
            for (int i = num_args; i < num_params; i++) {
                padded_args[i] = HML_NULL_VAL;
            }
            // Add rest array as last param
            padded_args[num_params] = rest_array;

            HmlValue result;
            switch (total_params) {
                case 1: result = ((HmlFn1)fn_ptr)(env, padded_args[0]); break;
                case 2: result = ((HmlFn2)fn_ptr)(env, padded_args[0], padded_args[1]); break;
                case 3: result = ((HmlFn3)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2]); break;
                case 4: result = ((HmlFn4)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3]); break;
                case 5: result = ((HmlFn5)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4]); break;
                case 6: result = ((HmlFn6)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5]); break;
                case 7: result = ((HmlFn7)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6]); break;
                case 8: result = ((HmlFn8)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6], padded_args[7]); break;
                default:
                    hml_runtime_error("Functions with more than 7 regular parameters + rest not supported");
                    result = hml_val_null();
            }
            hml_release(&rest_array);
            return result;
        }

        // Fast paths for common arities (0-3 params cover ~90% of functions)
        // Avoid padded_args array entirely when num_args == num_params
        if (__builtin_expect(num_args == num_params, 1)) {
            switch (num_params) {
                case 0: return ((HmlFn0)fn_ptr)(env);
                case 1: return ((HmlFn1)fn_ptr)(env, args[0]);
                case 2: return ((HmlFn2)fn_ptr)(env, args[0], args[1]);
                case 3: return ((HmlFn3)fn_ptr)(env, args[0], args[1], args[2]);
                case 4: return ((HmlFn4)fn_ptr)(env, args[0], args[1], args[2], args[3]);
                case 5: return ((HmlFn5)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4]);
                case 6: return ((HmlFn6)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5]);
                case 7: return ((HmlFn7)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
                case 8: return ((HmlFn8)fn_ptr)(env, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            }
        }

        // Slow path: need to pad args with nulls for optional parameters
        HmlValue padded_args[8];

        // Copy provided args (use memcpy for larger copies)
        if (num_args <= 4) {
            for (int i = 0; i < num_args; i++) {
                padded_args[i] = args[i];
            }
        } else {
            memcpy(padded_args, args, num_args * sizeof(HmlValue));
        }

        // Fill remaining with null (use static null value)
        for (int i = num_args; i < num_params; i++) {
            padded_args[i] = HML_NULL_VAL;
        }

        switch (num_params) {
            case 1: return ((HmlFn1)fn_ptr)(env, padded_args[0]);
            case 2: return ((HmlFn2)fn_ptr)(env, padded_args[0], padded_args[1]);
            case 3: return ((HmlFn3)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2]);
            case 4: return ((HmlFn4)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3]);
            case 5: return ((HmlFn5)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4]);
            case 6: return ((HmlFn6)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5]);
            case 7: return ((HmlFn7)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6]);
            case 8: return ((HmlFn8)fn_ptr)(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4], padded_args[5], padded_args[6], padded_args[7]);
            default:
                hml_runtime_error("Functions with more than 8 arguments not supported");
        }
    }

    hml_runtime_error("Cannot call non-function value (type: %s)", hml_typeof_str(fn));
}

// Thread-local self for method calls
__thread HmlValue hml_self = {0};

HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args) {
    // Handle string methods
    if (obj.type == HML_VAL_STRING) {
        if (strcmp(method, "chars") == 0 && num_args == 0) {
            return hml_string_chars(obj);
        }
        if (strcmp(method, "bytes") == 0 && num_args == 0) {
            return hml_string_bytes(obj);
        }
        if (strcmp(method, "to_bytes") == 0 && num_args == 0) {
            return hml_string_to_bytes(obj);
        }
        if (strcmp(method, "substr") == 0 && num_args == 2) {
            return hml_string_substr(obj, args[0], args[1]);
        }
        if (strcmp(method, "slice") == 0 && num_args == 2) {
            return hml_string_slice(obj, args[0], args[1]);
        }
        if (strcmp(method, "find") == 0 && num_args == 1) {
            return hml_string_find(obj, args[0]);
        }
        if (strcmp(method, "contains") == 0 && num_args == 1) {
            return hml_string_contains(obj, args[0]);
        }
        if (strcmp(method, "split") == 0 && num_args == 1) {
            return hml_string_split(obj, args[0]);
        }
        if (strcmp(method, "trim") == 0 && num_args == 0) {
            return hml_string_trim(obj);
        }
        if (strcmp(method, "to_upper") == 0 && num_args == 0) {
            return hml_string_to_upper(obj);
        }
        if (strcmp(method, "to_lower") == 0 && num_args == 0) {
            return hml_string_to_lower(obj);
        }
        if (strcmp(method, "starts_with") == 0 && num_args == 1) {
            return hml_string_starts_with(obj, args[0]);
        }
        if (strcmp(method, "ends_with") == 0 && num_args == 1) {
            return hml_string_ends_with(obj, args[0]);
        }
        if (strcmp(method, "replace") == 0 && num_args == 2) {
            return hml_string_replace(obj, args[0], args[1]);
        }
        if (strcmp(method, "replace_all") == 0 && num_args == 2) {
            return hml_string_replace_all(obj, args[0], args[1]);
        }
        if (strcmp(method, "repeat") == 0 && num_args == 1) {
            return hml_string_repeat(obj, args[0]);
        }
        if (strcmp(method, "char_at") == 0 && num_args == 1) {
            return hml_string_char_at(obj, args[0]);
        }
        if (strcmp(method, "byte_at") == 0 && num_args == 1) {
            return hml_string_byte_at(obj, args[0]);
        }
        hml_runtime_error("String has no method '%s'", method);
    }

    // Handle array methods
    if (obj.type == HML_VAL_ARRAY) {
        if (strcmp(method, "push") == 0 && num_args == 1) {
            hml_array_push(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "pop") == 0 && num_args == 0) {
            return hml_array_pop(obj);
        }
        if (strcmp(method, "shift") == 0 && num_args == 0) {
            return hml_array_shift(obj);
        }
        if (strcmp(method, "unshift") == 0 && num_args == 1) {
            hml_array_unshift(obj, args[0]);
            return hml_val_null();
        }
        if (strcmp(method, "insert") == 0 && num_args == 2) {
            hml_array_insert(obj, args[0], args[1]);
            return hml_val_null();
        }
        if (strcmp(method, "remove") == 0 && num_args == 1) {
            return hml_array_remove(obj, args[0]);
        }
        if (strcmp(method, "find") == 0 && num_args == 1) {
            return hml_array_find(obj, args[0]);
        }
        if (strcmp(method, "contains") == 0 && num_args == 1) {
            return hml_array_contains(obj, args[0]);
        }
        if (strcmp(method, "slice") == 0 && num_args == 2) {
            return hml_array_slice(obj, args[0], args[1]);
        }
        if (strcmp(method, "join") == 0 && num_args == 1) {
            return hml_array_join(obj, args[0]);
        }
        if (strcmp(method, "concat") == 0 && num_args == 1) {
            return hml_array_concat(obj, args[0]);
        }
        if (strcmp(method, "reverse") == 0 && num_args == 0) {
            hml_array_reverse(obj);
            return hml_val_null();
        }
        if (strcmp(method, "first") == 0 && num_args == 0) {
            return hml_array_first(obj);
        }
        if (strcmp(method, "last") == 0 && num_args == 0) {
            return hml_array_last(obj);
        }
        if (strcmp(method, "clear") == 0 && num_args == 0) {
            hml_array_clear(obj);
            return hml_val_null();
        }
        if (strcmp(method, "map") == 0 && num_args == 1) {
            return hml_array_map(obj, args[0]);
        }
        if (strcmp(method, "filter") == 0 && num_args == 1) {
            return hml_array_filter(obj, args[0]);
        }
        if ((strcmp(method, "reduce") == 0) && (num_args == 1 || num_args == 2)) {
            HmlValue initial = (num_args == 2) ? args[1] : hml_val_null();
            return hml_array_reduce(obj, args[0], initial);
        }
        hml_runtime_error("Array has no method '%s'", method);
    }

    // Handle object methods
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Cannot call method '%s' on non-object (type: %s)",
                method, hml_typeof_str(obj));
    }

    // Get the method function from the object
    HmlValue fn = hml_object_get_field(obj, method);
    if (fn.type == HML_VAL_NULL) {
        // Fallback to built-in object methods if no custom method exists
        if (strcmp(method, "keys") == 0 && num_args == 0) {
            return hml_object_keys(obj);
        }
        if (strcmp(method, "has") == 0 && num_args == 1) {
            if (args[0].type != HML_VAL_STRING) {
                hml_runtime_error("Object.has() requires string argument");
            }
            return hml_val_bool(hml_object_has_field(obj, args[0].as.as_string->data));
        }
        if (strcmp(method, "delete") == 0 && num_args == 1) {
            if (args[0].type != HML_VAL_STRING) {
                hml_runtime_error("Object.delete() requires string argument");
            }
            return hml_val_bool(hml_object_delete_field(obj, args[0].as.as_string->data));
        }
        hml_runtime_error("Object has no method '%s'", method);
    }

    // Save previous self and set new one
    HmlValue prev_self = hml_self;
    hml_self = obj;
    hml_retain(&hml_self);

    // Call the method
    HmlValue result = hml_call_function(fn, args, num_args);

    // Restore previous self
    hml_release(&hml_self);
    hml_self = prev_self;

    hml_release(&fn);
    return result;
}

// ========== FILE I/O ==========

HmlValue hml_open(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING) {
        fprintf(stderr, "Error: open() expects string path\n");
        exit(1);
    }

    const char *path_str = path.as.as_string->data;
    const char *mode_str = "r";

    if (mode.type == HML_VAL_STRING) {
        mode_str = mode.as.as_string->data;
    }

    FILE *fp = fopen(path_str, mode_str);
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s'\n", path_str);
        exit(1);
    }

    HmlFileHandle *fh = malloc(sizeof(HmlFileHandle));
    fh->fp = fp;
    fh->path = strdup(path_str);
    fh->mode = strdup(mode_str);
    fh->closed = 0;

    HmlValue result;
    result.type = HML_VAL_FILE;
    result.as.as_file = fh;
    return result;
}

HmlValue hml_file_read(HmlValue file, HmlValue size) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: read() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot read from closed file '%s'\n", fh->path);
        exit(1);
    }

    int32_t read_size = 0;
    if (size.type == HML_VAL_I32) {
        read_size = size.as.as_i32;
    } else if (size.type == HML_VAL_I64) {
        read_size = (int32_t)size.as.as_i64;
    }

    if (read_size <= 0) {
        return hml_file_read_all(file);
    }

    char *buffer = malloc(read_size + 1);
    size_t bytes_read = fread(buffer, 1, read_size, (FILE*)fh->fp);
    buffer[bytes_read] = '\0';

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_file_read_all(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: read() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot read from closed file '%s'\n", fh->path);
        exit(1);
    }

    FILE *fp = (FILE*)fh->fp;
    long start_pos = ftell(fp);

    // Check if stream is seekable (regular file vs pipe/stdin)
    int is_seekable = (start_pos != -1 && fseek(fp, 0, SEEK_END) == 0);

    if (is_seekable) {
        // Seekable stream: get size and read in one go
        long end_pos = ftell(fp);
        fseek(fp, start_pos, SEEK_SET);

        long size = end_pos - start_pos;
        if (size <= 0) {
            return hml_val_string("");
        }

        char *buffer = malloc(size + 1);
        if (!buffer) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit(1);
        }
        size_t bytes_read = fread(buffer, 1, size, fp);
        buffer[bytes_read] = '\0';

        HmlValue result = hml_val_string(buffer);
        free(buffer);
        return result;
    } else {
        // Non-seekable stream (stdin, pipe, socket): read in chunks
        size_t capacity = 4096;
        size_t total_read = 0;
        char *buffer = malloc(capacity);
        if (!buffer) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit(1);
        }

        while (1) {
            // Ensure we have room to read
            if (total_read + 4096 > capacity) {
                capacity *= 2;
                char *new_buffer = realloc(buffer, capacity);
                if (!new_buffer) {
                    free(buffer);
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    exit(1);
                }
                buffer = new_buffer;
            }

            size_t bytes = fread(buffer + total_read, 1, 4096, fp);
            total_read += bytes;

            if (bytes < 4096) {
                // EOF or error
                if (ferror(fp)) {
                    free(buffer);
                    fprintf(stderr, "Error: Read error on file '%s'\n", fh->path);
                    exit(1);
                }
                break;  // EOF reached
            }
        }

        buffer[total_read] = '\0';

        HmlValue result = hml_val_string(buffer);
        free(buffer);
        return result;
    }
}

HmlValue hml_file_write(HmlValue file, HmlValue data) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: write() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot write to closed file '%s'\n", fh->path);
        exit(1);
    }

    const char *str = "";
    if (data.type == HML_VAL_STRING) {
        str = data.as.as_string->data;
    }

    size_t bytes_written = fwrite(str, 1, strlen(str), (FILE*)fh->fp);
    return hml_val_i32((int32_t)bytes_written);
}

HmlValue hml_file_seek(HmlValue file, HmlValue position) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: seek() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot seek in closed file '%s'\n", fh->path);
        exit(1);
    }

    long pos = 0;
    if (position.type == HML_VAL_I32) {
        pos = position.as.as_i32;
    } else if (position.type == HML_VAL_I64) {
        pos = (long)position.as.as_i64;
    }

    fseek((FILE*)fh->fp, pos, SEEK_SET);
    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

HmlValue hml_file_tell(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: tell() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot tell position in closed file '%s'\n", fh->path);
        exit(1);
    }

    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

void hml_file_close(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        return;
    }

    HmlFileHandle *fh = file.as.as_file;
    if (!fh->closed) {
        fclose((FILE*)fh->fp);
        fh->closed = 1;
    }
}

// ========== SYSTEM INFO OPERATIONS ==========

HmlValue hml_platform(void) {
#ifdef __linux__
    return hml_val_string("linux");
#elif defined(__APPLE__)
    return hml_val_string("macos");
#elif defined(_WIN32) || defined(_WIN64)
    return hml_val_string("windows");
#else
    return hml_val_string("unknown");
#endif
}

HmlValue hml_arch(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: arch() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.machine);
}

HmlValue hml_hostname(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        fprintf(stderr, "Error: hostname() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(hostname);
}

HmlValue hml_username(void) {
    // Try getlogin_r first
    char username[256];
    if (getlogin_r(username, sizeof(username)) == 0) {
        return hml_val_string(username);
    }

    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_name != NULL) {
        return hml_val_string(pw->pw_name);
    }

    // Fall back to environment variable
    char *env_user = getenv("USER");
    if (env_user != NULL) {
        return hml_val_string(env_user);
    }

    fprintf(stderr, "Error: username() failed: could not determine username\n");
    exit(1);
}

HmlValue hml_homedir(void) {
    // Try HOME environment variable first
    char *home = getenv("HOME");
    if (home != NULL) {
        return hml_val_string(home);
    }

    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return hml_val_string(pw->pw_dir);
    }

    fprintf(stderr, "Error: homedir() failed: could not determine home directory\n");
    exit(1);
}

HmlValue hml_cpu_count(void) {
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) {
        nprocs = 1;  // Default to 1 if we can't determine
    }
    return hml_val_i32((int32_t)nprocs);
}

HmlValue hml_total_memory(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: total_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64((int64_t)info.totalram * (int64_t)info.mem_unit);
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    int64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) != 0) {
        fprintf(stderr, "Error: total_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64(memsize);
#else
    // Fallback: use sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages < 0 || page_size < 0) {
        fprintf(stderr, "Error: total_memory() failed: could not determine memory\n");
        exit(1);
    }
    return hml_val_i64((int64_t)pages * (int64_t)page_size);
#endif
}

HmlValue hml_free_memory(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: free_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    int64_t free_mem = (int64_t)info.freeram * (int64_t)info.mem_unit;
    int64_t buffers = (int64_t)info.bufferram * (int64_t)info.mem_unit;
    return hml_val_i64(free_mem + buffers);
#elif defined(__APPLE__)
    // Use vm_statistics to get free memory on macOS
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    host_page_size(mach_host_self(), &page_size);
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stat, &count) != KERN_SUCCESS) {
        // Fallback: return 10% of total memory
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        int64_t memsize;
        size_t len = sizeof(memsize);
        sysctl(mib, 2, &memsize, &len, NULL, 0);
        return hml_val_i64(memsize / 10);
    }
    // Free + inactive pages
    int64_t free_mem = (int64_t)(vm_stat.free_count + vm_stat.inactive_count) * (int64_t)page_size;
    return hml_val_i64(free_mem);
#else
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (avail_pages < 0 || page_size < 0) {
        fprintf(stderr, "Error: free_memory() failed: could not determine free memory\n");
        exit(1);
    }
    return hml_val_i64((int64_t)avail_pages * (int64_t)page_size);
#endif
}

HmlValue hml_os_version(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: os_version() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.release);
}

HmlValue hml_os_name(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: os_name() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.sysname);
}

HmlValue hml_tmpdir(void) {
    char *tmpdir = getenv("TMPDIR");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    tmpdir = getenv("TMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    tmpdir = getenv("TEMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    return hml_val_string("/tmp");
}

HmlValue hml_uptime(void) {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: uptime() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64((int64_t)info.uptime);
#elif defined(__APPLE__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) != 0) {
        fprintf(stderr, "Error: uptime() failed: %s\n", strerror(errno));
        exit(1);
    }
    time_t now = time(NULL);
    return hml_val_i64((int64_t)(now - boottime.tv_sec));
#else
    fprintf(stderr, "Error: uptime() not supported on this platform\n");
    exit(1);
#endif
}

// System info builtin wrappers
HmlValue hml_builtin_platform(HmlClosureEnv *env) {
    (void)env;
    return hml_platform();
}

HmlValue hml_builtin_arch(HmlClosureEnv *env) {
    (void)env;
    return hml_arch();
}

HmlValue hml_builtin_hostname(HmlClosureEnv *env) {
    (void)env;
    return hml_hostname();
}

HmlValue hml_builtin_username(HmlClosureEnv *env) {
    (void)env;
    return hml_username();
}

HmlValue hml_builtin_homedir(HmlClosureEnv *env) {
    (void)env;
    return hml_homedir();
}

HmlValue hml_builtin_cpu_count(HmlClosureEnv *env) {
    (void)env;
    return hml_cpu_count();
}

HmlValue hml_builtin_total_memory(HmlClosureEnv *env) {
    (void)env;
    return hml_total_memory();
}

HmlValue hml_builtin_free_memory(HmlClosureEnv *env) {
    (void)env;
    return hml_free_memory();
}

HmlValue hml_builtin_os_version(HmlClosureEnv *env) {
    (void)env;
    return hml_os_version();
}

HmlValue hml_builtin_os_name(HmlClosureEnv *env) {
    (void)env;
    return hml_os_name();
}

HmlValue hml_builtin_tmpdir(HmlClosureEnv *env) {
    (void)env;
    return hml_tmpdir();
}

HmlValue hml_builtin_uptime(HmlClosureEnv *env) {
    (void)env;
    return hml_uptime();
}

// ========== FILESYSTEM OPERATIONS ==========

HmlValue hml_exists(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    return hml_val_bool(stat(path.as.as_string->data, &st) == 0);
}

HmlValue hml_read_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: read_file() requires a string path\n");
        exit(1);
    }

    FILE *fp = fopen(path.as.as_string->data, "r");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Error: read_file() memory allocation failed\n");
        exit(1);
    }

    size_t read_size = fread(buffer, 1, size, fp);
    buffer[read_size] = '\0';
    fclose(fp);

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_write_file(HmlValue path, HmlValue content) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: write_file() requires a string path\n");
        exit(1);
    }
    if (content.type != HML_VAL_STRING || !content.as.as_string) {
        fprintf(stderr, "Error: write_file() requires string content\n");
        exit(1);
    }

    FILE *fp = fopen(path.as.as_string->data, "w");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fwrite(content.as.as_string->data, 1, content.as.as_string->length, fp);
    fclose(fp);
    return hml_val_null();
}

HmlValue hml_append_file(HmlValue path, HmlValue content) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: append_file() requires a string path\n");
        exit(1);
    }
    if (content.type != HML_VAL_STRING || !content.as.as_string) {
        fprintf(stderr, "Error: append_file() requires string content\n");
        exit(1);
    }

    FILE *fp = fopen(path.as.as_string->data, "a");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fwrite(content.as.as_string->data, 1, content.as.as_string->length, fp);
    fclose(fp);
    return hml_val_null();
}

HmlValue hml_remove_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: remove_file() requires a string path\n");
        exit(1);
    }

    if (unlink(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to remove '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_rename_file(HmlValue old_path, HmlValue new_path) {
    if (old_path.type != HML_VAL_STRING || !old_path.as.as_string) {
        fprintf(stderr, "Error: rename() requires string old_path\n");
        exit(1);
    }
    if (new_path.type != HML_VAL_STRING || !new_path.as.as_string) {
        fprintf(stderr, "Error: rename() requires string new_path\n");
        exit(1);
    }

    if (rename(old_path.as.as_string->data, new_path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to rename '%s' to '%s': %s\n",
            old_path.as.as_string->data, new_path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_copy_file(HmlValue src_path, HmlValue dest_path) {
    if (src_path.type != HML_VAL_STRING || !src_path.as.as_string) {
        fprintf(stderr, "Error: copy_file() requires string src_path\n");
        exit(1);
    }
    if (dest_path.type != HML_VAL_STRING || !dest_path.as.as_string) {
        fprintf(stderr, "Error: copy_file() requires string dest_path\n");
        exit(1);
    }

    FILE *src_fp = fopen(src_path.as.as_string->data, "rb");
    if (!src_fp) {
        fprintf(stderr, "Error: Failed to open source '%s': %s\n",
            src_path.as.as_string->data, strerror(errno));
        exit(1);
    }

    FILE *dest_fp = fopen(dest_path.as.as_string->data, "wb");
    if (!dest_fp) {
        fclose(src_fp);
        fprintf(stderr, "Error: Failed to open destination '%s': %s\n",
            dest_path.as.as_string->data, strerror(errno));
        exit(1);
    }

    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
        if (fwrite(buffer, 1, n, dest_fp) != n) {
            fclose(src_fp);
            fclose(dest_fp);
            fprintf(stderr, "Error: Failed to write to '%s': %s\n",
                dest_path.as.as_string->data, strerror(errno));
            exit(1);
        }
    }

    fclose(src_fp);
    fclose(dest_fp);
    return hml_val_null();
}

HmlValue hml_is_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        return hml_val_bool(0);
    }
    return hml_val_bool(S_ISREG(st.st_mode));
}

HmlValue hml_is_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        return hml_val_bool(0);
    }
    return hml_val_bool(S_ISDIR(st.st_mode));
}

HmlValue hml_file_stat(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: file_stat() requires a string path\n");
        exit(1);
    }

    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        fprintf(stderr, "Error: Failed to stat '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }

    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "size", hml_val_i64(st.st_size));
    hml_object_set_field(obj, "atime", hml_val_i64(st.st_atime));
    hml_object_set_field(obj, "mtime", hml_val_i64(st.st_mtime));
    hml_object_set_field(obj, "ctime", hml_val_i64(st.st_ctime));
    hml_object_set_field(obj, "mode", hml_val_u32(st.st_mode));
    hml_object_set_field(obj, "is_file", hml_val_bool(S_ISREG(st.st_mode)));
    hml_object_set_field(obj, "is_dir", hml_val_bool(S_ISDIR(st.st_mode)));
    return obj;
}

// ========== DIRECTORY OPERATIONS ==========

HmlValue hml_make_dir(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: make_dir() requires a string path\n");
        exit(1);
    }

    uint32_t dir_mode = 0755;  // Default mode
    if (mode.type == HML_VAL_U32) {
        dir_mode = mode.as.as_u32;
    } else if (mode.type == HML_VAL_I32) {
        dir_mode = (uint32_t)mode.as.as_i32;
    }

    if (mkdir(path.as.as_string->data, dir_mode) != 0) {
        fprintf(stderr, "Error: Failed to create directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_remove_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: remove_dir() requires a string path\n");
        exit(1);
    }

    if (rmdir(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to remove directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_list_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: list_dir() requires a string path\n");
        exit(1);
    }

    DIR *dir = opendir(path.as.as_string->data);
    if (!dir) {
        fprintf(stderr, "Error: Failed to open directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }

    HmlValue arr = hml_val_array();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        hml_array_push(arr, hml_val_string(entry->d_name));
    }

    closedir(dir);
    return arr;
}

HmlValue hml_cwd(void) {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        fprintf(stderr, "Error: Failed to get current directory: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(buffer);
}

HmlValue hml_chdir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: chdir() requires a string path\n");
        exit(1);
    }

    if (chdir(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to change directory to '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_absolute_path(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: absolute_path() requires a string path\n");
        exit(1);
    }

    char buffer[PATH_MAX];
    if (realpath(path.as.as_string->data, buffer) == NULL) {
        fprintf(stderr, "Error: Failed to resolve path '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_string(buffer);
}

// ========== FILESYSTEM BUILTIN WRAPPERS ==========

HmlValue hml_builtin_exists(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_exists(path);
}

HmlValue hml_builtin_read_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_read_file(path);
}

HmlValue hml_builtin_write_file(HmlClosureEnv *env, HmlValue path, HmlValue content) {
    (void)env;
    return hml_write_file(path, content);
}

HmlValue hml_builtin_append_file(HmlClosureEnv *env, HmlValue path, HmlValue content) {
    (void)env;
    return hml_append_file(path, content);
}

HmlValue hml_builtin_remove_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_remove_file(path);
}

HmlValue hml_builtin_rename(HmlClosureEnv *env, HmlValue old_path, HmlValue new_path) {
    (void)env;
    return hml_rename_file(old_path, new_path);
}

HmlValue hml_builtin_copy_file(HmlClosureEnv *env, HmlValue src, HmlValue dest) {
    (void)env;
    return hml_copy_file(src, dest);
}

HmlValue hml_builtin_is_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_is_file(path);
}

HmlValue hml_builtin_is_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_is_dir(path);
}

HmlValue hml_builtin_file_stat(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_file_stat(path);
}

HmlValue hml_builtin_make_dir(HmlClosureEnv *env, HmlValue path, HmlValue mode) {
    (void)env;
    return hml_make_dir(path, mode);
}

HmlValue hml_builtin_remove_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_remove_dir(path);
}

HmlValue hml_builtin_list_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_list_dir(path);
}

HmlValue hml_builtin_cwd(HmlClosureEnv *env) {
    (void)env;
    return hml_cwd();
}

HmlValue hml_builtin_chdir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_chdir(path);
}

HmlValue hml_builtin_absolute_path(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_absolute_path(path);
}

// Async/concurrency operations moved to builtins_async.c

// ========== CALL STACK TRACKING ==========

// Thread-local call depth counter for stack overflow detection
// Exposed globally for inline macro access (hml_g_call_depth)
__thread int hml_g_call_depth = 0;

// Thread-local maximum call depth (can be modified at runtime)
// Initialized to default value, can be changed via set_stack_limit()
__thread int hml_g_max_call_depth = HML_MAX_CALL_DEPTH;

// Get the current stack limit
HmlValue hml_get_stack_limit(void) {
    return hml_val_i32(hml_g_max_call_depth);
}

// Set the stack limit (returns the old limit)
HmlValue hml_set_stack_limit(HmlValue limit) {
    int old_limit = hml_g_max_call_depth;
    int new_limit = hml_to_i32(limit);
    if (new_limit <= 0) {
        hml_runtime_error("set_stack_limit() expects a positive integer");
    }
    hml_g_max_call_depth = new_limit;
    return hml_val_i32(old_limit);
}

// Builtin wrapper versions (for function references)
HmlValue hml_builtin_get_stack_limit(HmlClosureEnv *env) {
    (void)env;
    return hml_get_stack_limit();
}

HmlValue hml_builtin_set_stack_limit(HmlClosureEnv *env, HmlValue limit) {
    (void)env;
    return hml_set_stack_limit(limit);
}

// Function versions for backwards compatibility (macros are faster)
void hml_call_enter(void) {
    HML_CALL_ENTER();
}

void hml_call_exit(void) {
    HML_CALL_EXIT();
}

// ========== SIGNAL HANDLING ==========

#include <signal.h>
#include <errno.h>

// Global signal handler table (signal number -> Hemlock function value)
static HmlValue g_signal_handlers[HML_MAX_SIGNAL];
static int g_signal_handlers_initialized = 0;

static void init_signal_handlers(void) {
    if (g_signal_handlers_initialized) return;
    for (int i = 0; i < HML_MAX_SIGNAL; i++) {
        g_signal_handlers[i] = hml_val_null();
    }
    g_signal_handlers_initialized = 1;
}

// C signal handler that invokes Hemlock function values
static void hml_c_signal_handler(int signum) {
    if (signum < 0 || signum >= HML_MAX_SIGNAL) return;

    HmlValue handler = g_signal_handlers[signum];
    if (handler.type == HML_VAL_NULL) return;

    if (handler.type == HML_VAL_FUNCTION) {
        // Call the function with signal number as argument
        HmlValue sig_arg = hml_val_i32(signum);
        hml_call_function(handler, &sig_arg, 1);
    }
}

HmlValue hml_signal(HmlValue signum, HmlValue handler) {
    init_signal_handlers();

    // Validate signum
    if (signum.type != HML_VAL_I32) {
        hml_runtime_error("signal() signum must be an integer");
    }

    int sig = signum.as.as_i32;
    if (sig < 0 || sig >= HML_MAX_SIGNAL) {
        hml_runtime_error("signal() signum %d out of range [0, %d)", sig, HML_MAX_SIGNAL);
    }

    // Validate handler is function or null
    if (handler.type != HML_VAL_NULL && handler.type != HML_VAL_FUNCTION) {
        hml_runtime_error("signal() handler must be a function or null");
    }

    // Get previous handler for return
    HmlValue prev = g_signal_handlers[sig];
    hml_retain(&prev);

    // Release old handler and store new one
    hml_release(&g_signal_handlers[sig]);
    g_signal_handlers[sig] = handler;
    hml_retain(&g_signal_handlers[sig]);

    // Install or reset C signal handler
    struct sigaction sa;
    if (handler.type != HML_VAL_NULL) {
        sa.sa_handler = hml_c_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(sig, &sa, NULL) != 0) {
            hml_runtime_error("signal() failed for signal %d: %s", sig, strerror(errno));
        }
    } else {
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(sig, &sa, NULL) != 0) {
            hml_runtime_error("signal() failed to reset signal %d: %s", sig, strerror(errno));
        }
    }

    return prev;
}

HmlValue hml_raise(HmlValue signum) {
    if (signum.type != HML_VAL_I32) {
        hml_runtime_error("raise() signum must be an integer");
    }

    int sig = signum.as.as_i32;
    if (sig < 0 || sig >= HML_MAX_SIGNAL) {
        hml_runtime_error("raise() signum %d out of range [0, %d)", sig, HML_MAX_SIGNAL);
    }

    if (raise(sig) != 0) {
        hml_runtime_error("raise() failed for signal %d: %s", sig, strerror(errno));
    }

    return hml_val_null();
}

// ========== TYPE DEFINITIONS (DUCK TYPING) ==========

// Type registry
static HmlTypeDef *g_type_registry = NULL;
static int g_type_count = 0;
static int g_type_capacity = 0;

void hml_register_type(const char *name, HmlTypeField *fields, int num_fields) {
    // Initialize registry if needed
    if (g_type_registry == NULL) {
        g_type_capacity = 16;
        g_type_registry = malloc(sizeof(HmlTypeDef) * g_type_capacity);
    }

    // Grow if needed
    if (g_type_count >= g_type_capacity) {
        g_type_capacity *= 2;
        g_type_registry = realloc(g_type_registry, sizeof(HmlTypeDef) * g_type_capacity);
    }

    // Add type definition
    HmlTypeDef *type = &g_type_registry[g_type_count++];
    type->name = strdup(name);
    type->num_fields = num_fields;
    type->fields = malloc(sizeof(HmlTypeField) * num_fields);

    for (int i = 0; i < num_fields; i++) {
        type->fields[i].name = strdup(fields[i].name);
        type->fields[i].type_kind = fields[i].type_kind;
        type->fields[i].is_optional = fields[i].is_optional;
        type->fields[i].default_value = fields[i].default_value;
        hml_retain(&type->fields[i].default_value);
    }
}

HmlTypeDef* hml_lookup_type(const char *name) {
    for (int i = 0; i < g_type_count; i++) {
        if (strcmp(g_type_registry[i].name, name) == 0) {
            return &g_type_registry[i];
        }
    }
    return NULL;
}

HmlValue hml_validate_object_type(HmlValue obj, const char *type_name) {
    if (obj.type != HML_VAL_OBJECT) {
        fprintf(stderr, "Error: Expected object for type '%s', got %s\n",
                type_name, hml_typeof(obj));
        exit(1);
    }

    HmlTypeDef *type = hml_lookup_type(type_name);
    if (!type) {
        fprintf(stderr, "Error: Unknown type '%s'\n", type_name);
        exit(1);
    }

    HmlObject *o = obj.as.as_object;

    // Check each required field
    for (int i = 0; i < type->num_fields; i++) {
        HmlTypeField *field = &type->fields[i];

        // Find field in object
        int found = 0;
        for (int j = 0; j < o->num_fields; j++) {
            if (strcmp(o->field_names[j], field->name) == 0) {
                found = 1;
                // Type check if field has a specific type
                if (field->type_kind >= 0) {
                    HmlValue val = o->field_values[j];
                    int type_ok = 0;

                    switch (field->type_kind) {
                        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
                        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
                            type_ok = (val.type >= HML_VAL_I8 && val.type <= HML_VAL_U64);
                            break;
                        case HML_VAL_F32: case HML_VAL_F64:
                            type_ok = (val.type == HML_VAL_F32 || val.type == HML_VAL_F64);
                            break;
                        case HML_VAL_BOOL:
                            type_ok = (val.type == HML_VAL_BOOL);
                            break;
                        case HML_VAL_STRING:
                            type_ok = (val.type == HML_VAL_STRING);
                            break;
                        default:
                            type_ok = 1;  // Accept any type
                            break;
                    }

                    if (!type_ok) {
                        fprintf(stderr, "Error: Field '%s' has wrong type for '%s'\n",
                                field->name, type_name);
                        exit(1);
                    }
                }
                break;
            }
        }

        if (!found) {
            if (field->is_optional) {
                // Add default value
                hml_object_set_field(obj, field->name, field->default_value);
            } else {
                fprintf(stderr, "Error: Object missing required field '%s' for type '%s'\n",
                        field->name, type_name);
                exit(1);
            }
        }
    }

    // Set the object's type name
    if (o->type_name) free(o->type_name);
    o->type_name = strdup(type_name);

    return obj;
}

// FFI (Foreign Function Interface) operations moved to builtins_ffi.c

// ========== ADDITIONAL POINTER HELPERS FOR ALL TYPES ==========

// Builtin: ptr_deref_i8(ptr) -> i8
HmlValue hml_builtin_ptr_deref_i8(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i8() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i8() cannot dereference null pointer");
    }
    return hml_val_i8(*(int8_t*)p);
}

// Builtin: ptr_deref_i16(ptr) -> i16
HmlValue hml_builtin_ptr_deref_i16(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i16() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i16() cannot dereference null pointer");
    }
    return hml_val_i16(*(int16_t*)p);
}

// Builtin: ptr_deref_i64(ptr) -> i64
HmlValue hml_builtin_ptr_deref_i64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i64() cannot dereference null pointer");
    }
    return hml_val_i64(*(int64_t*)p);
}

// Builtin: ptr_deref_u8(ptr) -> u8
HmlValue hml_builtin_ptr_deref_u8(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u8() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u8() cannot dereference null pointer");
    }
    return hml_val_u8(*(uint8_t*)p);
}

// Builtin: ptr_deref_u16(ptr) -> u16
HmlValue hml_builtin_ptr_deref_u16(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u16() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u16() cannot dereference null pointer");
    }
    return hml_val_u16(*(uint16_t*)p);
}

// Builtin: ptr_deref_u32(ptr) -> u32
HmlValue hml_builtin_ptr_deref_u32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u32() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u32() cannot dereference null pointer");
    }
    return hml_val_u32(*(uint32_t*)p);
}

// Builtin: ptr_deref_u64(ptr) -> u64
HmlValue hml_builtin_ptr_deref_u64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u64() cannot dereference null pointer");
    }
    return hml_val_u64(*(uint64_t*)p);
}

// Builtin: ptr_deref_f32(ptr) -> f32
HmlValue hml_builtin_ptr_deref_f32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_f32() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_f32() cannot dereference null pointer");
    }
    return hml_val_f32(*(float*)p);
}

// Builtin: ptr_deref_f64(ptr) -> f64
HmlValue hml_builtin_ptr_deref_f64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_f64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_f64() cannot dereference null pointer");
    }
    return hml_val_f64(*(double*)p);
}

// Builtin: ptr_deref_ptr(ptr) -> ptr (pointer-to-pointer)
HmlValue hml_builtin_ptr_deref_ptr(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_ptr() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_ptr() cannot dereference null pointer");
    }
    return hml_val_ptr(*(void**)p);
}

// Builtin: ptr_write_i8(ptr, value)
HmlValue hml_builtin_ptr_write_i8(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i8() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i8() cannot write to null pointer");
    }
    *(int8_t*)p = (int8_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_i16(ptr, value)
HmlValue hml_builtin_ptr_write_i16(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i16() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i16() cannot write to null pointer");
    }
    *(int16_t*)p = (int16_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_i64(ptr, value)
HmlValue hml_builtin_ptr_write_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i64() cannot write to null pointer");
    }
    *(int64_t*)p = hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_u8(ptr, value)
HmlValue hml_builtin_ptr_write_u8(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u8() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u8() cannot write to null pointer");
    }
    *(uint8_t*)p = (uint8_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_u16(ptr, value)
HmlValue hml_builtin_ptr_write_u16(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u16() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u16() cannot write to null pointer");
    }
    *(uint16_t*)p = (uint16_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_u32(ptr, value)
HmlValue hml_builtin_ptr_write_u32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u32() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u32() cannot write to null pointer");
    }
    *(uint32_t*)p = (uint32_t)hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_u64(ptr, value)
HmlValue hml_builtin_ptr_write_u64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u64() cannot write to null pointer");
    }
    *(uint64_t*)p = (uint64_t)hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_f32(ptr, value)
HmlValue hml_builtin_ptr_write_f32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_f32() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_f32() cannot write to null pointer");
    }
    *(float*)p = (float)hml_to_f64(value);
    return hml_val_null();
}

// Builtin: ptr_write_f64(ptr, value)
HmlValue hml_builtin_ptr_write_f64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_f64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_f64() cannot write to null pointer");
    }
    *(double*)p = hml_to_f64(value);
    return hml_val_null();
}

// Builtin: ptr_write_ptr(ptr, value)
HmlValue hml_builtin_ptr_write_ptr(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_ptr() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_ptr() cannot write to null pointer");
    }
    if (value.type == HML_VAL_NULL) {
        *(void**)p = NULL;
    } else if (value.type == HML_VAL_PTR) {
        *(void**)p = value.as.as_ptr;
    } else {
        hml_runtime_error("ptr_write_ptr() second argument must be a ptr or null");
    }
    return hml_val_null();
}

// Builtin: ffi_sizeof(type_name) -> i32
HmlValue hml_builtin_ffi_sizeof(HmlClosureEnv *env, HmlValue type_name) {
    (void)env;
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        hml_runtime_error("ffi_sizeof() argument must be a type name string");
    }
    const char *name = type_name.as.as_string->data;

    if (strcmp(name, "i8") == 0) return hml_val_i32(sizeof(int8_t));
    if (strcmp(name, "i16") == 0) return hml_val_i32(sizeof(int16_t));
    if (strcmp(name, "i32") == 0) return hml_val_i32(sizeof(int32_t));
    if (strcmp(name, "i64") == 0) return hml_val_i32(sizeof(int64_t));
    if (strcmp(name, "u8") == 0) return hml_val_i32(sizeof(uint8_t));
    if (strcmp(name, "u16") == 0) return hml_val_i32(sizeof(uint16_t));
    if (strcmp(name, "u32") == 0) return hml_val_i32(sizeof(uint32_t));
    if (strcmp(name, "u64") == 0) return hml_val_i32(sizeof(uint64_t));
    if (strcmp(name, "f32") == 0) return hml_val_i32(sizeof(float));
    if (strcmp(name, "f64") == 0) return hml_val_i32(sizeof(double));
    if (strcmp(name, "ptr") == 0) return hml_val_i32(sizeof(void*));
    if (strcmp(name, "size_t") == 0 || strcmp(name, "usize") == 0) return hml_val_i32(sizeof(size_t));
    if (strcmp(name, "intptr_t") == 0 || strcmp(name, "isize") == 0) return hml_val_i32(sizeof(intptr_t));

    hml_runtime_error("ffi_sizeof(): unknown type '%s'", name);
    return hml_val_null();  // Unreachable
}

// Builtin: ptr_to_buffer(ptr, size) -> buffer
HmlValue hml_builtin_ptr_to_buffer(HmlClosureEnv *env, HmlValue ptr, HmlValue size) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_to_buffer() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_to_buffer() cannot read from null pointer");
    }
    int32_t sz = hml_to_i32(size);
    if (sz <= 0) {
        hml_runtime_error("ptr_to_buffer() size must be positive");
    }

    // Create a new buffer and copy data from the pointer
    HmlValue buf = hml_val_buffer(sz);
    memcpy(buf.as.as_buffer->data, p, sz);
    return buf;
}

// Builtin: buffer_ptr(buffer) -> ptr
HmlValue hml_builtin_buffer_ptr(HmlClosureEnv *env, HmlValue buf) {
    (void)env;
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("buffer_ptr() argument must be a buffer");
    }
    return hml_val_ptr(buf.as.as_buffer->data);
}

// Builtin: ptr_null() -> ptr
HmlValue hml_builtin_ptr_null(HmlClosureEnv *env) {
    (void)env;
    return hml_val_ptr(NULL);
}

// Compression and cryptographic operations moved to builtins_crypto.c

// ========== INTERNAL HELPER OPERATIONS ==========

// Read a u32 value from a pointer
HmlValue hml_read_u32(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_u32() requires a pointer");
    }
    uint32_t *ptr = (uint32_t*)ptr_val.as.as_ptr;
    return hml_val_u32(*ptr);
}

// Read a u64 value from a pointer
HmlValue hml_read_u64(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_u64() requires a pointer");
    }
    uint64_t *ptr = (uint64_t*)ptr_val.as.as_ptr;
    return hml_val_u64(*ptr);
}

// Read a pointer value from a pointer (double indirection)
HmlValue hml_read_ptr(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__read_ptr() requires a pointer");
    }
    void **pptr = (void**)ptr_val.as.as_ptr;
    return hml_val_ptr(*pptr);
}

// Get the last error string (strerror(errno))
HmlValue hml_strerror(void) {
    return hml_val_string(strerror(errno));
}

// Get the name field from a dirent structure
HmlValue hml_dirent_name(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__dirent_name() requires a pointer");
    }
    struct dirent *entry = (struct dirent*)ptr_val.as.as_ptr;
    return hml_val_string(entry->d_name);
}

// Convert a Hemlock string to a C string (returns allocated ptr)
HmlValue hml_string_to_cstr(HmlValue str_val) {
    if (str_val.type != HML_VAL_STRING) {
        hml_runtime_error("__string_to_cstr() requires a string");
    }
    HmlString *str = str_val.as.as_string;
    char *cstr = malloc(str->length + 1);
    if (!cstr) {
        hml_runtime_error("__string_to_cstr() memory allocation failed");
    }
    memcpy(cstr, str->data, str->length);
    cstr[str->length] = '\0';
    return hml_val_ptr(cstr);
}

// Convert a C string (ptr) to a Hemlock string
HmlValue hml_cstr_to_string(HmlValue ptr_val) {
    if (ptr_val.type != HML_VAL_PTR) {
        hml_runtime_error("__cstr_to_string() requires a pointer");
    }
    char *cstr = (char*)ptr_val.as.as_ptr;
    if (!cstr) {
        return hml_val_string("");
    }
    return hml_val_string(cstr);
}

// Forward declaration for hml_string_from_bytes wrapper
HmlValue hml_builtin_string_from_bytes(HmlClosureEnv *env, HmlValue arg);

// Convert an array of bytes or buffer to a UTF-8 string (wrapper for direct calls)
HmlValue hml_string_from_bytes(HmlValue arg) {
    return hml_builtin_string_from_bytes(NULL, arg);
}

// Wrapper functions for internal helpers
HmlValue hml_builtin_read_u32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u32(ptr);
}

HmlValue hml_builtin_read_u64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u64(ptr);
}

HmlValue hml_builtin_read_ptr(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_ptr(ptr);
}

HmlValue hml_builtin_strerror(HmlClosureEnv *env) {
    (void)env;
    return hml_strerror();
}

HmlValue hml_builtin_dirent_name(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_dirent_name(ptr);
}

HmlValue hml_builtin_string_to_cstr(HmlClosureEnv *env, HmlValue str) {
    (void)env;
    return hml_string_to_cstr(str);
}

HmlValue hml_builtin_cstr_to_string(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_cstr_to_string(ptr);
}

// Convert an array of bytes or buffer to a UTF-8 string
HmlValue hml_builtin_string_from_bytes(HmlClosureEnv *env, HmlValue arg) {
    (void)env;

    char *data = NULL;
    int length = 0;

    if (arg.type == HML_VAL_BUFFER) {
        // Handle buffer input
        HmlBuffer *buf = arg.as.as_buffer;
        if (!buf || !buf->data) {
            return hml_val_string("");
        }
        length = buf->length;
        data = malloc(length + 1);
        if (!data) {
            hml_runtime_error("__string_from_bytes() memory allocation failed");
        }
        memcpy(data, buf->data, length);
        data[length] = '\0';
    } else if (arg.type == HML_VAL_ARRAY) {
        // Handle array input - each element should be an integer byte value
        HmlArray *arr = arg.as.as_array;
        if (!arr || arr->length == 0) {
            return hml_val_string("");
        }
        length = arr->length;
        data = malloc(length + 1);
        if (!data) {
            hml_runtime_error("__string_from_bytes() memory allocation failed");
        }

        for (int i = 0; i < arr->length; i++) {
            HmlValue elem = arr->elements[i];
            int byte_val = 0;

            // Accept any integer type
            switch (elem.type) {
                case HML_VAL_I8:
                    byte_val = (unsigned char)elem.as.as_i8;
                    break;
                case HML_VAL_I16:
                    byte_val = elem.as.as_i16 & 0xFF;
                    break;
                case HML_VAL_I32:
                    byte_val = elem.as.as_i32 & 0xFF;
                    break;
                case HML_VAL_I64:
                    byte_val = (int)(elem.as.as_i64 & 0xFF);
                    break;
                case HML_VAL_U8:
                    byte_val = elem.as.as_u8;
                    break;
                case HML_VAL_U16:
                    byte_val = elem.as.as_u16 & 0xFF;
                    break;
                case HML_VAL_U32:
                    byte_val = elem.as.as_u32 & 0xFF;
                    break;
                case HML_VAL_U64:
                    byte_val = (int)(elem.as.as_u64 & 0xFF);
                    break;
                default:
                    free(data);
                    hml_runtime_error("__string_from_bytes() array element at index %d is not an integer", i);
            }

            data[i] = (char)byte_val;
        }
        data[length] = '\0';
    } else {
        hml_runtime_error("__string_from_bytes() requires array or buffer argument");
    }

    // Create string from the data - use hml_val_string_owned which takes ownership of data
    return hml_val_string_owned(data, length, length + 1);
}

// Socket and networking operations moved to builtins_socket.c

// ========== HTTP/WEBSOCKET SUPPORT ==========
// Requires libwebsockets

#ifdef HML_HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>

// HTTP response structure
typedef struct {
    char *body;
    size_t body_len;
    size_t body_capacity;
    int status_code;
    int complete;
    int failed;
    char *redirect_url;
    char *headers;
} hml_http_response_t;

// HTTP callback
static int hml_http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len) {
    hml_http_response_t *resp = (hml_http_response_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            // Add custom headers to the HTTP request
            {
                unsigned char **p = (unsigned char **)in;
                unsigned char *end = (*p) + len;

                // Add User-Agent header (required by GitHub API)
                const char *ua = "User-Agent: hemlock/1.0\r\n";
                size_t ua_len = strlen(ua);
                if (end - *p >= (int)ua_len) {
                    memcpy(*p, ua, ua_len);
                    *p += ua_len;
                }

                // Add Accept header for JSON APIs
                const char *accept = "Accept: application/json\r\n";
                size_t accept_len = strlen(accept);
                if (end - *p >= (int)accept_len) {
                    memcpy(*p, accept, accept_len);
                    *p += accept_len;
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            if (resp) {
                resp->failed = 1;
                resp->complete = 1;
            }
            break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            if (resp) {
                resp->status_code = lws_http_client_http_response(wsi);

                // Capture response headers
                {
                    char headers_buf[8192];
                    size_t headers_len = 0;
                    char value[1024];
                    int vlen;

                    struct { enum lws_token_indexes token; const char *name; } header_list[] = {
                        { WSI_TOKEN_HTTP_CONTENT_TYPE, "Content-Type" },
                        { WSI_TOKEN_HTTP_CONTENT_LENGTH, "Content-Length" },
                        { WSI_TOKEN_HTTP_CACHE_CONTROL, "Cache-Control" },
                        { WSI_TOKEN_HTTP_DATE, "Date" },
                        { WSI_TOKEN_HTTP_ETAG, "ETag" },
                        { WSI_TOKEN_HTTP_LAST_MODIFIED, "Last-Modified" },
                        { WSI_TOKEN_HTTP_LOCATION, "Location" },
                        { WSI_TOKEN_HTTP_SERVER, "Server" },
                        { WSI_TOKEN_HTTP_SET_COOKIE, "Set-Cookie" },
                        { WSI_TOKEN_HTTP_TRANSFER_ENCODING, "Transfer-Encoding" },
                        { WSI_TOKEN_HTTP_WWW_AUTHENTICATE, "WWW-Authenticate" },
                        { WSI_TOKEN_HTTP_ACCESS_CONTROL_ALLOW_ORIGIN, "Access-Control-Allow-Origin" },
                    };

                    for (size_t i = 0; i < sizeof(header_list)/sizeof(header_list[0]); i++) {
                        vlen = lws_hdr_copy(wsi, value, sizeof(value), header_list[i].token);
                        if (vlen > 0) {
                            value[vlen] = '\0';
                            int written = snprintf(headers_buf + headers_len,
                                                   sizeof(headers_buf) - headers_len,
                                                   "%s: %s\r\n", header_list[i].name, value);
                            if (written > 0 && (size_t)written < sizeof(headers_buf) - headers_len) {
                                headers_len += written;
                            }
                        }
                    }

                    if (headers_len > 0) {
                        resp->headers = strndup(headers_buf, headers_len);
                    }
                }

                // Capture Location header for redirects (3xx responses)
                if (resp->status_code >= 300 && resp->status_code < 400) {
                    char location[1024];
                    int loc_len = lws_hdr_copy(wsi, location, sizeof(location), WSI_TOKEN_HTTP_LOCATION);
                    if (loc_len > 0) {
                        location[loc_len] = '\0';
                        resp->redirect_url = strdup(location);
                        resp->complete = 1;
                    }
                }
            }
            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
            // This callback tells us there's data available - we must consume it
            {
                char buffer[4096 + LWS_PRE];
                char *px = buffer + LWS_PRE;
                int lenx = sizeof(buffer) - LWS_PRE;

                if (lws_http_client_read(wsi, &px, &lenx) < 0)
                    return -1;
            }
            return 0;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            // Accumulate response body - this is called after lws_http_client_read
            if (resp) {
                if (resp->body_len + len >= resp->body_capacity) {
                    resp->body_capacity = (resp->body_len + len + 1) * 2;
                    char *new_body = realloc(resp->body, resp->body_capacity);
                    if (!new_body) {
                        resp->failed = 1;
                        resp->complete = 1;
                        return -1;
                    }
                    resp->body = new_body;
                }
                memcpy(resp->body + resp->body_len, in, len);
                resp->body_len += len;
                resp->body[resp->body_len] = '\0';
            }
            return 0;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            if (resp) {
                resp->complete = 1;
            }
            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            if (resp) {
                resp->complete = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// Parse URL into components
static int hml_parse_url(const char *url, char *host, int *port, char *path, int *ssl) {
    *ssl = 0;
    *port = 80;
    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "https://", 8) == 0) {
        *ssl = 1;
        *port = 443;
        const char *rest = url + 8;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else if (strncmp(url, "http://", 7) == 0) {
        const char *rest = url + 7;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else {
        return -1;
    }

    return 0;
}

// HTTP GET
HmlValue hml_lws_http_get(HmlValue url_val) {
    if (url_val.type != HML_VAL_STRING || !url_val.as.as_string) {
        hml_runtime_error("__lws_http_get() expects string URL");
    }

    const char *url = url_val.as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;  // 16KB for large headers (e.g., GitHub API)

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to create libwebsockets context");
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "GET";
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    // Disable automatic redirects - we'll handle them at the hemlock layer
    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
        free(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// HTTP POST
HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post() expects string arguments");
    }

    const char *url = url_val.as.as_string->data;
    (void)body_val;  // Not fully implemented yet
    (void)content_type_val;
    
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols post_protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = post_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to create libwebsockets context");
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "POST";
    connect_info.protocol = post_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
        free(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Generic HTTP request with configurable method
HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request() expects string arguments");
    }

    const char *method = method_val.as.as_string->data;
    const char *url = url_val.as.as_string->data;
    (void)body_val;  // Not fully implemented yet
    (void)content_type_val;

    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols req_protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = req_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to create libwebsockets context");
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = method;
    connect_info.protocol = req_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
        free(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Get response status code
HmlValue hml_lws_response_status(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    return hml_val_i32(resp ? resp->status_code : 0);
}

// Get response body
HmlValue hml_lws_response_body(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->body) {
        return hml_val_string("");
    }
    return hml_val_string(resp->body);
}

// Get response headers
HmlValue hml_lws_response_headers(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->headers) {
        return hml_val_string("");
    }
    return hml_val_string(resp->headers);
}

// Free response
HmlValue hml_lws_response_free(HmlValue resp_val) {
    if (resp_val.type == HML_VAL_PTR) {
        hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
        if (resp) {
            if (resp->body) free(resp->body);
            if (resp->redirect_url) free(resp->redirect_url);
            if (resp->headers) free(resp->headers);
            free(resp);
        }
    }
    return hml_val_null();
}

// Get redirect URL from response (if any)
HmlValue hml_lws_response_redirect(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->redirect_url) {
        return hml_val_null();
    }
    return hml_val_string(resp->redirect_url);
}

// Get response body as binary buffer (preserves null bytes)
HmlValue hml_lws_response_body_binary(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_buffer(0);
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->body || resp->body_len == 0) {
        return hml_val_buffer(0);
    }
    // Create buffer and copy data (preserves binary data including null bytes)
    HmlValue buf = hml_val_buffer(resp->body_len);
    if (buf.type == HML_VAL_BUFFER && buf.as.as_buffer) {
        memcpy(buf.as.as_buffer->data, resp->body, resp->body_len);
    }
    return buf;
}

// Builtin wrappers
HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_http_get(url);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_status(resp);
}

HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body(resp);
}

HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_headers(resp);
}

HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_free(resp);
}

HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_redirect(resp);
}

HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body_binary(resp);
}

// ========== WEBSOCKET SUPPORT ==========

// WebSocket message structure
typedef struct hml_ws_message {
    unsigned char *data;
    size_t len;
    int is_binary;
    struct hml_ws_message *next;
} hml_ws_message_t;

// WebSocket connection structure
typedef struct {
    struct lws_context *context;
    struct lws *wsi;
    hml_ws_message_t *msg_queue_head;
    hml_ws_message_t *msg_queue_tail;
    int closed;
    int failed;
    int established;
    char *send_buffer;
    size_t send_len;
    int send_pending;
    pthread_t service_thread;
    volatile int shutdown;
    int has_own_thread;
    int owns_memory;
} hml_ws_connection_t;

// WebSocket server structure
typedef struct {
    struct lws_context *context;
    struct lws *pending_wsi;
    hml_ws_connection_t *pending_conn;
    int port;
    int closed;
    pthread_t service_thread;
    volatile int shutdown;
    pthread_mutex_t pending_mutex;
} hml_ws_server_t;

// Forward declarations for callbacks
static int hml_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len);
static int hml_ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len);

// Service thread for WebSocket clients
static void* hml_ws_service_thread(void *arg) {
    hml_ws_connection_t *conn = (hml_ws_connection_t *)arg;
    while (!conn->shutdown) {
        lws_service(conn->context, 50);
    }
    return NULL;
}

// Service thread for WebSocket servers
static void* hml_ws_server_service_thread(void *arg) {
    hml_ws_server_t *server = (hml_ws_server_t *)arg;
    while (!server->shutdown) {
        lws_service(server->context, 50);
    }
    return NULL;
}

// WebSocket client callback
static int hml_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    hml_ws_connection_t *conn = (hml_ws_connection_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (conn) {
                conn->wsi = wsi;
                conn->established = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (conn) {
                hml_ws_message_t *msg = malloc(sizeof(hml_ws_message_t));
                if (!msg) break;

                msg->len = len;
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
            }
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (conn && conn->send_pending && conn->send_buffer) {
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, LWS_WRITE_TEXT);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
            }
            break;

        case LWS_CALLBACK_CLOSED:
            if (conn) {
                conn->closed = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            if (conn) {
                conn->failed = 1;
                conn->closed = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// WebSocket server callback
static int hml_ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len) {
    hml_ws_server_t *server = (hml_ws_server_t *)lws_context_user(lws_get_context(wsi));
    hml_ws_connection_t *conn = (hml_ws_connection_t *)user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            if (conn) {
                conn->wsi = wsi;
                conn->context = lws_get_context(wsi);
                conn->shutdown = 0;
                conn->has_own_thread = 0;
                conn->owns_memory = 0;
            }
            if (server) {
                pthread_mutex_lock(&server->pending_mutex);
                if (!server->pending_wsi) {
                    server->pending_wsi = wsi;
                    server->pending_conn = conn;
                }
                pthread_mutex_unlock(&server->pending_mutex);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            if (conn) {
                hml_ws_message_t *msg = malloc(sizeof(hml_ws_message_t));
                if (!msg) break;

                msg->len = len;
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
            }
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (conn && conn->send_pending && conn->send_buffer) {
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, LWS_WRITE_TEXT);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
            }
            break;

        case LWS_CALLBACK_CLOSED:
            if (conn) {
                conn->closed = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// Parse WebSocket URL
static int hml_parse_ws_url(const char *url, char *host, int *port, char *path, int *ssl) {
    *ssl = 0;
    *port = 80;
    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "wss://", 6) == 0) {
        *ssl = 1;
        *port = 443;
        const char *rest = url + 6;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else if (strncmp(url, "ws://", 5) == 0) {
        const char *rest = url + 5;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else {
        return -1;
    }

    return 0;
}

// __lws_ws_connect(url: string): ptr
HmlValue hml_lws_ws_connect(HmlValue url_val) {
    if (url_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_ws_connect() expects string URL");
    }

    const char *url = url_val.as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_ws_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid WebSocket URL (must start with ws:// or wss://)");
    }

    hml_ws_connection_t *conn = calloc(1, sizeof(hml_ws_connection_t));
    if (!conn) {
        hml_runtime_error("Failed to allocate WebSocket connection");
    }
    conn->owns_memory = 1;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols ws_protocols[] = {
        { "ws", hml_ws_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = ws_protocols;

    conn->context = lws_create_context(&info);
    if (!conn->context) {
        free(conn);
        hml_runtime_error("Failed to create libwebsockets context");
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = conn->context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.protocol = ws_protocols[0].name;
    connect_info.userdata = conn;
    connect_info.pwsi = &conn->wsi;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection = LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(conn->context);
        free(conn);
        hml_runtime_error("Failed to connect WebSocket");
    }

    // Wait for connection (timeout 10 seconds)
    int timeout = 100;
    while (timeout-- > 0 && !conn->closed && !conn->failed && !conn->established) {
        lws_service(conn->context, 100);
    }

    if (conn->failed || conn->closed || !conn->established) {
        lws_context_destroy(conn->context);
        free(conn);
        hml_runtime_error("WebSocket connection failed or timed out");
    }

    // Start service thread
    conn->shutdown = 0;
    conn->has_own_thread = 1;
    if (pthread_create(&conn->service_thread, NULL, hml_ws_service_thread, conn) != 0) {
        lws_context_destroy(conn->context);
        free(conn);
        hml_runtime_error("Failed to create WebSocket service thread");
    }

    return hml_val_ptr(conn);
}

// __lws_ws_send_text(conn: ptr, text: string): i32
HmlValue hml_lws_ws_send_text(HmlValue conn_val, HmlValue text_val) {
    if (conn_val.type != HML_VAL_PTR || text_val.type != HML_VAL_STRING) {
        return hml_val_i32(-1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_i32(-1);
    }

    const char *text = text_val.as.as_string->data;
    size_t len = strlen(text);

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return hml_val_i32(-1);
    }

    memcpy(buf + LWS_PRE, text, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);

    if (written < 0) {
        return hml_val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return hml_val_i32(0);
}

// __lws_ws_send_binary(conn: ptr, buffer: buffer): i32
HmlValue hml_lws_ws_send_binary(HmlValue conn_val, HmlValue buffer_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_i32(-1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_i32(-1);
    }

    if (buffer_val.type != HML_VAL_BUFFER) {
        return hml_val_i32(-1);
    }

    HmlBuffer *hbuf = buffer_val.as.as_buffer;
    size_t len = hbuf->length;

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return hml_val_i32(-1);
    }

    memcpy(buf + LWS_PRE, hbuf->data, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    free(buf);

    if (written < 0) {
        return hml_val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return hml_val_i32(0);
}

// __lws_ws_recv(conn: ptr, timeout_ms: i32): ptr
HmlValue hml_lws_ws_recv(HmlValue conn_val, HmlValue timeout_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_null();
    }

    int timeout_ms = hml_to_i32(timeout_val);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        if (conn->msg_queue_head) {
            hml_ws_message_t *msg = conn->msg_queue_head;
            conn->msg_queue_head = msg->next;
            if (!conn->msg_queue_head) {
                conn->msg_queue_tail = NULL;
            }
            msg->next = NULL;
            return hml_val_ptr(msg);
        }

        usleep(10000);  // 10ms sleep
        if (conn->closed) return hml_val_null();
        if (iterations > 0) iterations--;
    }

    return hml_val_null();
}

// __lws_msg_type(msg: ptr): i32
HmlValue hml_lws_msg_type(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg) {
        return hml_val_i32(0);
    }

    return hml_val_i32(msg->is_binary ? 2 : 1);
}

// __lws_msg_text(msg: ptr): string
HmlValue hml_lws_msg_text(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg || !msg->data) {
        return hml_val_string("");
    }

    return hml_val_string((const char *)msg->data);
}

// __lws_msg_len(msg: ptr): i32
HmlValue hml_lws_msg_len(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg) {
        return hml_val_i32(0);
    }

    return hml_val_i32((int32_t)msg->len);
}

// __lws_msg_free(msg: ptr): null
HmlValue hml_lws_msg_free(HmlValue msg_val) {
    if (msg_val.type == HML_VAL_PTR) {
        hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
        if (msg) {
            if (msg->data) free(msg->data);
            free(msg);
        }
    }
    return hml_val_null();
}

// __lws_ws_close(conn: ptr): null
HmlValue hml_lws_ws_close(HmlValue conn_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (conn) {
        conn->closed = 1;
        conn->shutdown = 1;

        if (conn->has_own_thread) {
            pthread_join(conn->service_thread, NULL);
        }

        hml_ws_message_t *msg = conn->msg_queue_head;
        while (msg) {
            hml_ws_message_t *next = msg->next;
            if (msg->data) free(msg->data);
            free(msg);
            msg = next;
        }

        if (conn->send_buffer) {
            free(conn->send_buffer);
        }

        if (conn->has_own_thread && conn->context) {
            lws_context_destroy(conn->context);
        }

        if (conn->owns_memory) {
            free(conn);
        }
    }

    return hml_val_null();
}

// __lws_ws_is_closed(conn: ptr): i32
HmlValue hml_lws_ws_is_closed(HmlValue conn_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_i32(1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    return hml_val_i32(conn ? conn->closed : 1);
}

// __lws_ws_server_create(host: string, port: i32): ptr
HmlValue hml_lws_ws_server_create(HmlValue host_val, HmlValue port_val) {
    if (host_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_ws_server_create() expects string host");
    }

    const char *host = host_val.as.as_string->data;
    int port = hml_to_i32(port_val);

    hml_ws_server_t *server = calloc(1, sizeof(hml_ws_server_t));
    if (!server) {
        hml_runtime_error("Failed to allocate WebSocket server");
    }

    server->port = port;
    pthread_mutex_init(&server->pending_mutex, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.iface = host;
    info.user = server;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    static const struct lws_protocols server_protocols[] = {
        { "ws", hml_ws_server_callback, sizeof(hml_ws_connection_t), 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = server_protocols;

    server->context = lws_create_context(&info);
    if (!server->context) {
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        hml_runtime_error("Failed to create WebSocket server context");
    }

    server->shutdown = 0;
    if (pthread_create(&server->service_thread, NULL, hml_ws_server_service_thread, server) != 0) {
        lws_context_destroy(server->context);
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        hml_runtime_error("Failed to create WebSocket server thread");
    }

    return hml_val_ptr(server);
}

// __lws_ws_server_accept(server: ptr, timeout_ms: i32): ptr
HmlValue hml_lws_ws_server_accept(HmlValue server_val, HmlValue timeout_val) {
    if (server_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_server_t *server = (hml_ws_server_t *)server_val.as.as_ptr;
    if (!server || server->closed) {
        return hml_val_null();
    }

    int timeout_ms = hml_to_i32(timeout_val);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        pthread_mutex_lock(&server->pending_mutex);
        hml_ws_connection_t *conn = NULL;
        if (server->pending_wsi) {
            conn = server->pending_conn;
            server->pending_wsi = NULL;
            server->pending_conn = NULL;
        }
        pthread_mutex_unlock(&server->pending_mutex);

        if (conn) {
            return hml_val_ptr(conn);
        }

        usleep(10000);  // 10ms sleep
        if (iterations > 0) iterations--;
    }

    return hml_val_null();
}

// __lws_ws_server_close(server: ptr): null
HmlValue hml_lws_ws_server_close(HmlValue server_val) {
    if (server_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_server_t *server = (hml_ws_server_t *)server_val.as.as_ptr;
    if (server) {
        server->closed = 1;
        server->shutdown = 1;
        pthread_join(server->service_thread, NULL);
        pthread_mutex_destroy(&server->pending_mutex);
        if (server->context) {
            lws_context_destroy(server->context);
        }
        free(server);
    }

    return hml_val_null();
}

// WebSocket builtin wrappers
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_ws_connect(url);
}

HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue conn, HmlValue text) {
    (void)env;
    return hml_lws_ws_send_text(conn, text);
}

HmlValue hml_builtin_lws_ws_send_binary(HmlClosureEnv *env, HmlValue conn, HmlValue buffer) {
    (void)env;
    return hml_lws_ws_send_binary(conn, buffer);
}

HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue conn, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_recv(conn, timeout_ms);
}

HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_close(conn);
}

HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_is_closed(conn);
}

HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_type(msg);
}

HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_text(msg);
}

HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_len(msg);
}

HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_free(msg);
}

HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue host, HmlValue port) {
    (void)env;
    return hml_lws_ws_server_create(host, port);
}

HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue server, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_server_accept(server, timeout_ms);
}

HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue server) {
    (void)env;
    return hml_lws_ws_server_close(server);
}

#else  // !HML_HAVE_LIBWEBSOCKETS

// Stub implementations
HmlValue hml_lws_http_get(HmlValue url_val) {
    (void)url_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_status(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_body(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_headers(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_free(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_null();
}

HmlValue hml_lws_response_redirect(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_null();
}

HmlValue hml_lws_response_body_binary(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_buffer(0);
}

HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_http_get(url);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_status(resp);
}

HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body(resp);
}

HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_headers(resp);
}

HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_free(resp);
}

HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_redirect(resp);
}

HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body_binary(resp);
}

// WebSocket stub implementations
HmlValue hml_lws_ws_connect(HmlValue url_val) {
    (void)url_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_send_text(HmlValue conn_val, HmlValue text_val) {
    (void)conn_val; (void)text_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_send_binary(HmlValue conn_val, HmlValue buffer_val) {
    (void)conn_val; (void)buffer_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_recv(HmlValue conn_val, HmlValue timeout_val) {
    (void)conn_val; (void)timeout_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_close(HmlValue conn_val) {
    (void)conn_val;
    return hml_val_null();
}

HmlValue hml_lws_ws_is_closed(HmlValue conn_val) {
    (void)conn_val;
    return hml_val_i32(1);
}

HmlValue hml_lws_msg_type(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_i32(0);
}

HmlValue hml_lws_msg_text(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_string("");
}

HmlValue hml_lws_msg_len(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_i32(0);
}

HmlValue hml_lws_msg_free(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_null();
}

HmlValue hml_lws_ws_server_create(HmlValue host_val, HmlValue port_val) {
    (void)host_val; (void)port_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_server_accept(HmlValue server_val, HmlValue timeout_val) {
    (void)server_val; (void)timeout_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_server_close(HmlValue server_val) {
    (void)server_val;
    return hml_val_null();
}

// WebSocket builtin wrapper stubs
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_ws_connect(url);
}

HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue conn, HmlValue text) {
    (void)env;
    return hml_lws_ws_send_text(conn, text);
}

HmlValue hml_builtin_lws_ws_send_binary(HmlClosureEnv *env, HmlValue conn, HmlValue buffer) {
    (void)env;
    return hml_lws_ws_send_binary(conn, buffer);
}

HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue conn, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_recv(conn, timeout_ms);
}

HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_close(conn);
}

HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_is_closed(conn);
}

HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_type(msg);
}

HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_text(msg);
}

HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_len(msg);
}

HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_free(msg);
}

HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue host, HmlValue port) {
    (void)env;
    return hml_lws_ws_server_create(host, port);
}

HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue server, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_server_accept(server, timeout_ms);
}

HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue server) {
    (void)env;
    return hml_lws_ws_server_close(server);
}

#endif  // HML_HAVE_LIBWEBSOCKETS

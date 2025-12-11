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

// OpenSSL for cryptographic hash functions
#include <openssl/sha.h>
#include <openssl/md5.h>

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
static int hml_is_integer_type(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
            return 1;
        default:
            return 0;
    }
}

// Helper to check if a value is a float type
static int hml_is_float_type(HmlValue val) {
    return val.type == HML_VAL_F32 || val.type == HML_VAL_F64;
}

// Helper to extract int64 from any numeric value
static int64_t hml_val_to_int64(HmlValue val) {
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
static double hml_val_to_double(HmlValue val) {
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
            return hml_val_bool(int_val != 0 || float_val != 0.0);

        case HML_VAL_STRING:
            // Allow conversion from rune to string (match interpreter behavior)
            if (val.type == HML_VAL_RUNE) {
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode_rune(val.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';
                return hml_val_string(rune_bytes);
            }
            hml_runtime_error("Cannot convert %s to string", hml_type_name(val.type));
            return hml_val_null();

        default:
            // For other types, return as-is
            return val;
    }
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

HmlValue hml_exec(HmlValue command) {
    if (command.type != HML_VAL_STRING || !command.as.as_string) {
        hml_runtime_error("exec() argument must be a string");
    }

    HmlString *cmd_str = command.as.as_string;
    char *ccmd = malloc(cmd_str->length + 1);
    if (!ccmd) {
        hml_runtime_error("exec() memory allocation failed");
    }
    memcpy(ccmd, cmd_str->data, cmd_str->length);
    ccmd[cmd_str->length] = '\0';

    // Open pipe to read command output
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

// ========== MATH OPERATIONS ==========

HmlValue hml_sqrt(HmlValue x) {
    return hml_val_f64(sqrt(hml_to_f64(x)));
}

HmlValue hml_sin(HmlValue x) {
    return hml_val_f64(sin(hml_to_f64(x)));
}

HmlValue hml_cos(HmlValue x) {
    return hml_val_f64(cos(hml_to_f64(x)));
}

HmlValue hml_tan(HmlValue x) {
    return hml_val_f64(tan(hml_to_f64(x)));
}

HmlValue hml_asin(HmlValue x) {
    return hml_val_f64(asin(hml_to_f64(x)));
}

HmlValue hml_acos(HmlValue x) {
    return hml_val_f64(acos(hml_to_f64(x)));
}

HmlValue hml_atan(HmlValue x) {
    return hml_val_f64(atan(hml_to_f64(x)));
}

HmlValue hml_floor(HmlValue x) {
    return hml_val_f64(floor(hml_to_f64(x)));
}

HmlValue hml_ceil(HmlValue x) {
    return hml_val_f64(ceil(hml_to_f64(x)));
}

HmlValue hml_round(HmlValue x) {
    return hml_val_f64(round(hml_to_f64(x)));
}

HmlValue hml_trunc(HmlValue x) {
    return hml_val_f64(trunc(hml_to_f64(x)));
}

HmlValue hml_abs(HmlValue x) {
    double val = hml_to_f64(x);
    return hml_val_f64(val < 0 ? -val : val);
}

HmlValue hml_pow(HmlValue base, HmlValue exp) {
    return hml_val_f64(pow(hml_to_f64(base), hml_to_f64(exp)));
}

HmlValue hml_exp(HmlValue x) {
    return hml_val_f64(exp(hml_to_f64(x)));
}

HmlValue hml_log(HmlValue x) {
    return hml_val_f64(log(hml_to_f64(x)));
}

HmlValue hml_log10(HmlValue x) {
    return hml_val_f64(log10(hml_to_f64(x)));
}

HmlValue hml_log2(HmlValue x) {
    return hml_val_f64(log2(hml_to_f64(x)));
}

HmlValue hml_atan2(HmlValue y, HmlValue x) {
    return hml_val_f64(atan2(hml_to_f64(y), hml_to_f64(x)));
}

HmlValue hml_min(HmlValue a, HmlValue b) {
    double va = hml_to_f64(a);
    double vb = hml_to_f64(b);
    return hml_val_f64(va < vb ? va : vb);
}

HmlValue hml_max(HmlValue a, HmlValue b) {
    double va = hml_to_f64(a);
    double vb = hml_to_f64(b);
    return hml_val_f64(va > vb ? va : vb);
}

HmlValue hml_clamp(HmlValue x, HmlValue min_val, HmlValue max_val) {
    double v = hml_to_f64(x);
    double lo = hml_to_f64(min_val);
    double hi = hml_to_f64(max_val);
    if (v < lo) return hml_val_f64(lo);
    if (v > hi) return hml_val_f64(hi);
    return hml_val_f64(v);
}

static int g_rand_seeded = 0;

HmlValue hml_rand(void) {
    if (!g_rand_seeded) {
        srand((unsigned int)time(NULL));
        g_rand_seeded = 1;
    }
    return hml_val_f64((double)rand() / RAND_MAX);
}

HmlValue hml_rand_range(HmlValue min_val, HmlValue max_val) {
    if (!g_rand_seeded) {
        srand((unsigned int)time(NULL));
        g_rand_seeded = 1;
    }
    double lo = hml_to_f64(min_val);
    double hi = hml_to_f64(max_val);
    double r = (double)rand() / RAND_MAX;
    return hml_val_f64(lo + r * (hi - lo));
}

HmlValue hml_seed_val(HmlValue seed) {
    srand((unsigned int)hml_to_i32(seed));
    g_rand_seeded = 1;
    return hml_val_null();
}

void hml_seed(HmlValue seed) {
    srand((unsigned int)hml_to_i32(seed));
    g_rand_seeded = 1;
}

// ========== BUILTIN WRAPPERS FOR COMPILER ==========
// These match the compiler's function calling convention: (HmlClosureEnv*, HmlValue args...)

HmlValue hml_builtin_sin(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_sin(x);
}

HmlValue hml_builtin_cos(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_cos(x);
}

HmlValue hml_builtin_tan(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_tan(x);
}

HmlValue hml_builtin_asin(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_asin(x);
}

HmlValue hml_builtin_acos(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_acos(x);
}

HmlValue hml_builtin_atan(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_atan(x);
}

HmlValue hml_builtin_atan2(HmlClosureEnv *env, HmlValue y, HmlValue x) {
    (void)env;
    return hml_atan2(y, x);
}

HmlValue hml_builtin_sqrt(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_sqrt(x);
}

HmlValue hml_builtin_pow(HmlClosureEnv *env, HmlValue base, HmlValue exp) {
    (void)env;
    return hml_pow(base, exp);
}

HmlValue hml_builtin_exp(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_exp(x);
}

HmlValue hml_builtin_log(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log(x);
}

HmlValue hml_builtin_log10(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log10(x);
}

HmlValue hml_builtin_log2(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_log2(x);
}

HmlValue hml_builtin_floor(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_floor(x);
}

HmlValue hml_builtin_ceil(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_ceil(x);
}

HmlValue hml_builtin_round(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_round(x);
}

HmlValue hml_builtin_trunc(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_trunc(x);
}

HmlValue hml_builtin_abs(HmlClosureEnv *env, HmlValue x) {
    (void)env;
    return hml_abs(x);
}

HmlValue hml_builtin_min(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_min(a, b);
}

HmlValue hml_builtin_max(HmlClosureEnv *env, HmlValue a, HmlValue b) {
    (void)env;
    return hml_max(a, b);
}

HmlValue hml_builtin_clamp(HmlClosureEnv *env, HmlValue x, HmlValue lo, HmlValue hi) {
    (void)env;
    return hml_clamp(x, lo, hi);
}

HmlValue hml_builtin_rand(HmlClosureEnv *env) {
    (void)env;
    return hml_rand();
}

HmlValue hml_builtin_rand_range(HmlClosureEnv *env, HmlValue min_val, HmlValue max_val) {
    (void)env;
    return hml_rand_range(min_val, max_val);
}

HmlValue hml_builtin_seed(HmlClosureEnv *env, HmlValue seed) {
    (void)env;
    return hml_seed_val(seed);
}

// Time builtin wrappers
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

// ========== TIME OPERATIONS ==========

HmlValue hml_now(void) {
    return hml_val_i64((int64_t)time(NULL));
}

HmlValue hml_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return hml_val_i64((int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

HmlValue hml_clock(void) {
    return hml_val_f64((double)clock() / CLOCKS_PER_SEC);
}

void hml_sleep(HmlValue seconds) {
    double secs = hml_to_f64(seconds);
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

// ========== DATETIME OPERATIONS ==========

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

// Datetime builtin wrappers
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
    // Float always wins
    if (a == HML_VAL_F64 || b == HML_VAL_F64) return HML_VAL_F64;
    if (a == HML_VAL_F32 || b == HML_VAL_F32) return HML_VAL_F32;

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
    // FAST PATH: i32 operations (most common case)
    if (left.type == HML_VAL_I32 && right.type == HML_VAL_I32) {
        int32_t l = left.as.as_i32;
        int32_t r = right.as.as_i32;
        switch (op) {
            case HML_OP_ADD: return hml_val_i32(l + r);
            case HML_OP_SUB: return hml_val_i32(l - r);
            case HML_OP_MUL: return hml_val_i32(l * r);
            case HML_OP_DIV:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i32(l / r);
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
                if (r == 0.0) hml_runtime_error("Division by zero");
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
                if (r == 0.0) {
                    hml_runtime_error("Division by zero");
                }
                result = l / r;
                break;
            case HML_OP_LESS:         return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
            case HML_OP_GREATER:      return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                hml_runtime_error("Invalid operation for floats");
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

HmlValue hml_string_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    HmlString *s = str.as.as_string;
    // For now, return byte length (UTF-8 codepoint counting can be added later)
    return hml_val_i32(s->length);
}

HmlValue hml_string_byte_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    return hml_val_i32(str.as.as_string->length);
}

HmlValue hml_string_char_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    if (idx < 0 || idx >= s->length) {
        return hml_val_null();
    }
    // Return as rune (byte value for ASCII)
    return hml_val_rune((uint32_t)(unsigned char)s->data[idx]);
}

HmlValue hml_string_byte_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    if (idx < 0 || idx >= s->length) {
        return hml_val_null();
    }
    return hml_val_u8((uint8_t)s->data[idx]);
}

HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t len = hml_to_i32(length);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= s->length) return hml_val_string("");
    if (len < 0) len = 0;
    if (start_idx + len > s->length) len = s->length - start_idx;

    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t end_idx = hml_to_i32(end);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx > s->length) start_idx = s->length;
    if (end_idx < start_idx) end_idx = start_idx;
    if (end_idx > s->length) end_idx = s->length;

    int len = end_idx - start_idx;
    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_find(HmlValue str, HmlValue needle) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        needle.type != HML_VAL_STRING || !needle.as.as_string) {
        return hml_val_i32(-1);
    }
    HmlString *s = str.as.as_string;
    HmlString *n = needle.as.as_string;

    if (n->length == 0) return hml_val_i32(0);
    if (n->length > s->length) return hml_val_i32(-1);

    for (int i = 0; i <= s->length - n->length; i++) {
        if (memcmp(s->data + i, n->data, n->length) == 0) {
            return hml_val_i32(i);
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_string_contains(HmlValue str, HmlValue needle) {
    HmlValue pos = hml_string_find(str, needle);
    return hml_val_bool(pos.as.as_i32 >= 0);
}

HmlValue hml_string_split(HmlValue str, HmlValue delimiter) {
    HmlValue result = hml_val_array();

    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        delimiter.type != HML_VAL_STRING || !delimiter.as.as_string) {
        return result;
    }

    HmlString *s = str.as.as_string;
    HmlString *d = delimiter.as.as_string;

    if (d->length == 0) {
        // Split into individual characters
        for (int i = 0; i < s->length; i++) {
            char *c = malloc(2);
            c[0] = s->data[i];
            c[1] = '\0';
            HmlValue part_val = hml_val_string_owned(c, 1, 2);
            hml_array_push(result, part_val);
        }
        return result;
    }

    int start = 0;
    for (int i = 0; i <= s->length - d->length; i++) {
        if (memcmp(s->data + i, d->data, d->length) == 0) {
            int len = i - start;
            char *part = malloc(len + 1);
            memcpy(part, s->data + start, len);
            part[len] = '\0';
            HmlValue part_val = hml_val_string_owned(part, len, len + 1);
            hml_array_push(result, part_val);
            i += d->length - 1;
            start = i + 1;
        }
    }

    // Add remaining part
    int len = s->length - start;
    char *part = malloc(len + 1);
    memcpy(part, s->data + start, len);
    part[len] = '\0';
    HmlValue part_val = hml_val_string_owned(part, len, len + 1);
    hml_array_push(result, part_val);

    return result;
}

HmlValue hml_string_trim(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int start = 0;
    int end = s->length - 1;

    while (start < s->length && (s->data[start] == ' ' || s->data[start] == '\t' ||
                                  s->data[start] == '\n' || s->data[start] == '\r')) {
        start++;
    }

    while (end >= start && (s->data[end] == ' ' || s->data[end] == '\t' ||
                            s->data[end] == '\n' || s->data[end] == '\r')) {
        end--;
    }

    int len = end - start + 1;
    if (len <= 0) return hml_val_string("");

    char *result = malloc(len + 1);
    memcpy(result, s->data + start, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_to_upper(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'a' && c <= 'z') {
            result[i] = c - 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_to_lower(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'A' && c <= 'Z') {
            result[i] = c + 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_starts_with(HmlValue str, HmlValue prefix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        prefix.type != HML_VAL_STRING || !prefix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = prefix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    return hml_val_bool(memcmp(s->data, p->data, p->length) == 0);
}

HmlValue hml_string_ends_with(HmlValue str, HmlValue suffix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        suffix.type != HML_VAL_STRING || !suffix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = suffix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    int offset = s->length - p->length;
    return hml_val_bool(memcmp(s->data + offset, p->data, p->length) == 0);
}

HmlValue hml_string_replace(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Find first occurrence
    int pos = -1;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            pos = i;
            break;
        }
    }

    if (pos == -1) {
        hml_retain(&str);
        return str;
    }

    int new_len = s->length - o->length + n->length;
    char *result = malloc(new_len + 1);
    memcpy(result, s->data, pos);
    memcpy(result + pos, n->data, n->length);
    memcpy(result + pos + n->length, s->data + pos + o->length, s->length - pos - o->length);
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

HmlValue hml_string_replace_all(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Count occurrences
    int count = 0;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            count++;
            i += o->length - 1;
        }
    }

    if (count == 0) {
        hml_retain(&str);
        return str;
    }

    int new_len = s->length - (count * o->length) + (count * n->length);
    char *result = malloc(new_len + 1);
    int result_pos = 0;

    for (int i = 0; i < s->length; ) {
        if (i <= s->length - o->length && memcmp(s->data + i, o->data, o->length) == 0) {
            memcpy(result + result_pos, n->data, n->length);
            result_pos += n->length;
            i += o->length;
        } else {
            result[result_pos++] = s->data[i++];
        }
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

HmlValue hml_string_repeat(HmlValue str, HmlValue count) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t n = hml_to_i32(count);
    if (n <= 0) return hml_val_string("");

    int new_len = s->length * n;
    char *result = malloc(new_len + 1);
    for (int i = 0; i < n; i++) {
        memcpy(result + i * s->length, s->data, s->length);
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

// OPTIMIZATION: Concatenate 3 strings in a single allocation
HmlValue hml_string_concat3(HmlValue a, HmlValue b, HmlValue c) {
    // Convert all to strings if needed
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int total = len_a + len_b + len_c;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); pos += len_c; }
    result[total] = '\0';

    // Release converted strings
    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);

    return hml_val_string_owned(result, total, total + 1);
}

// OPTIMIZATION: Concatenate 4 strings in a single allocation
HmlValue hml_string_concat4(HmlValue a, HmlValue b, HmlValue c, HmlValue d) {
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string(d);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;
    HmlString *sd = str_d.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int len_d = sd ? sd->length : 0;
    int total = len_a + len_b + len_c + len_d;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); pos += len_c; }
    if (sd) { memcpy(result + pos, sd->data, len_d); pos += len_d; }
    result[total] = '\0';

    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);
    if (d.type != HML_VAL_STRING) hml_release(&str_d);

    return hml_val_string_owned(result, total, total + 1);
}

// OPTIMIZATION: Concatenate 5 strings in a single allocation
HmlValue hml_string_concat5(HmlValue a, HmlValue b, HmlValue c, HmlValue d, HmlValue e) {
    HmlValue str_a = (a.type == HML_VAL_STRING) ? a : hml_to_string(a);
    HmlValue str_b = (b.type == HML_VAL_STRING) ? b : hml_to_string(b);
    HmlValue str_c = (c.type == HML_VAL_STRING) ? c : hml_to_string(c);
    HmlValue str_d = (d.type == HML_VAL_STRING) ? d : hml_to_string(d);
    HmlValue str_e = (e.type == HML_VAL_STRING) ? e : hml_to_string(e);

    HmlString *sa = str_a.as.as_string;
    HmlString *sb = str_b.as.as_string;
    HmlString *sc = str_c.as.as_string;
    HmlString *sd = str_d.as.as_string;
    HmlString *se = str_e.as.as_string;

    int len_a = sa ? sa->length : 0;
    int len_b = sb ? sb->length : 0;
    int len_c = sc ? sc->length : 0;
    int len_d = sd ? sd->length : 0;
    int len_e = se ? se->length : 0;
    int total = len_a + len_b + len_c + len_d + len_e;

    char *result = malloc(total + 1);
    int pos = 0;
    if (sa) { memcpy(result + pos, sa->data, len_a); pos += len_a; }
    if (sb) { memcpy(result + pos, sb->data, len_b); pos += len_b; }
    if (sc) { memcpy(result + pos, sc->data, len_c); pos += len_c; }
    if (sd) { memcpy(result + pos, sd->data, len_d); pos += len_d; }
    if (se) { memcpy(result + pos, se->data, len_e); pos += len_e; }
    result[total] = '\0';

    if (a.type != HML_VAL_STRING) hml_release(&str_a);
    if (b.type != HML_VAL_STRING) hml_release(&str_b);
    if (c.type != HML_VAL_STRING) hml_release(&str_c);
    if (d.type != HML_VAL_STRING) hml_release(&str_d);
    if (e.type != HML_VAL_STRING) hml_release(&str_e);

    return hml_val_string_owned(result, total, total + 1);
}

// Concatenate an array of strings into a single string
HmlValue hml_string_concat_many(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("string_concat_many() requires array argument");
    }

    HmlArray *a = arr.as.as_array;

    // Handle empty array
    if (a->length == 0) {
        return hml_val_string("");
    }

    // Calculate total length needed
    int total_len = 0;
    for (int i = 0; i < a->length; i++) {
        if (a->elements[i].type == HML_VAL_STRING && a->elements[i].as.as_string) {
            total_len += a->elements[i].as.as_string->length;
        }
    }

    // Allocate and build result
    char *result = malloc(total_len + 1);
    int pos = 0;
    for (int i = 0; i < a->length; i++) {
        if (a->elements[i].type == HML_VAL_STRING && a->elements[i].as.as_string) {
            HmlString *s = a->elements[i].as.as_string;
            memcpy(result + pos, s->data, s->length);
            pos += s->length;
        }
    }
    result[total_len] = '\0';

    return hml_val_string_owned(result, total_len, total_len + 1);
}

// String indexing (returns char at position as rune)
HmlValue hml_string_index(HmlValue str, HmlValue index) {
    return hml_string_char_at(str, index);
}

void hml_string_index_assign(HmlValue str, HmlValue index, HmlValue val) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("String index assignment requires string");
    }

    // Accept both rune and integer types (convert integer to rune)
    uint32_t rune_val;
    if (val.type == HML_VAL_RUNE) {
        rune_val = val.as.as_rune;
    } else if (hml_is_integer_type(val)) {
        int64_t int_val = hml_val_to_int64(val);
        if (int_val < 0 || int_val > 0x10FFFF) {
            hml_runtime_error("Integer value %ld out of range for rune [0, 0x10FFFF]", int_val);
        }
        rune_val = (uint32_t)int_val;
    } else {
        hml_runtime_error("String index assignment requires rune or integer value");
    }

    int idx = hml_to_i32(index);
    HmlString *s = str.as.as_string;

    if (idx < 0 || idx >= s->length) {
        hml_runtime_error("String index %d out of bounds", idx);
    }

    // For simplicity, only support single-byte characters in assignment
    // Full UTF-8 would require resizing the string
    if (rune_val < 128) {
        s->data[idx] = (char)rune_val;
    } else {
        hml_runtime_error("String assignment of multi-byte runes not yet supported");
    }
}

// UTF-8 helper: get byte length of character starting at given byte
static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;        // ASCII
    if ((c & 0xE0) == 0xC0) return 2;     // 2-byte
    if ((c & 0xF0) == 0xE0) return 3;     // 3-byte
    if ((c & 0xF8) == 0xF0) return 4;     // 4-byte
    return 1;  // Invalid, treat as single byte
}

// UTF-8 helper: decode codepoint at position
static uint32_t utf8_decode_char(const char *s, int *bytes_read) {
    unsigned char c = (unsigned char)s[0];
    uint32_t codepoint;
    int len = utf8_char_len(c);

    if (len == 1) {
        codepoint = c;
    } else if (len == 2) {
        codepoint = ((c & 0x1F) << 6) | (s[1] & 0x3F);
    } else if (len == 3) {
        codepoint = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    } else {
        codepoint = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                    ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }

    *bytes_read = len;
    return codepoint;
}

// Count UTF-8 codepoints in a string
HmlValue hml_string_char_count(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    HmlString *s = str.as.as_string;

    // Use cached char_length if available
    if (s->char_length >= 0) {
        return hml_val_i32(s->char_length);
    }

    // Count UTF-8 codepoints
    int count = 0;
    int byte_pos = 0;
    while (byte_pos < s->length) {
        byte_pos += utf8_char_len((unsigned char)s->data[byte_pos]);
        count++;
    }

    // Cache the result
    s->char_length = count;
    return hml_val_i32(count);
}

// Get rune at character index (UTF-8 aware)
HmlValue hml_string_rune_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);

    if (idx < 0) {
        return hml_val_null();
    }

    // Navigate to the character index
    int byte_pos = 0;
    int char_idx = 0;
    while (byte_pos < s->length && char_idx < idx) {
        byte_pos += utf8_char_len((unsigned char)s->data[byte_pos]);
        char_idx++;
    }

    // Check if we found the character
    if (byte_pos >= s->length) {
        return hml_val_null();
    }

    // Decode the UTF-8 codepoint at this position
    int bytes_read;
    uint32_t codepoint = utf8_decode_char(s->data + byte_pos, &bytes_read);
    return hml_val_rune(codepoint);
}

// Convert string to array of runes (codepoints)
HmlValue hml_string_chars(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("chars() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlValue arr = hml_val_array();

    int byte_pos = 0;
    while (byte_pos < s->length) {
        int bytes_read;
        uint32_t codepoint = utf8_decode_char(s->data + byte_pos, &bytes_read);
        HmlValue rune = hml_val_rune(codepoint);
        hml_array_push(arr, rune);
        byte_pos += bytes_read;
    }

    return arr;
}

// Convert string to array of bytes (u8 values)
HmlValue hml_string_bytes(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("bytes() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlValue arr = hml_val_array();

    for (int i = 0; i < s->length; i++) {
        HmlValue byte = hml_val_u8((unsigned char)s->data[i]);
        hml_array_push(arr, byte);
    }

    return arr;
}

// Convert string to buffer (raw bytes)
HmlValue hml_string_to_bytes(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        hml_runtime_error("to_bytes() requires string");
    }

    HmlString *s = str.as.as_string;
    HmlBuffer *buf = malloc(sizeof(HmlBuffer));
    buf->data = malloc(s->length);
    memcpy(buf->data, s->data, s->length);
    buf->length = s->length;
    buf->capacity = s->length;
    buf->ref_count = 1;

    return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
}

// Buffer indexing
HmlValue hml_buffer_get(HmlValue buf, HmlValue index) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("Buffer index requires buffer");
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        hml_runtime_error("Buffer index %d out of bounds (length %d)", idx, b->length);
    }

    uint8_t *data = (uint8_t *)b->data;
    return hml_val_u8(data[idx]);
}

void hml_buffer_set(HmlValue buf, HmlValue index, HmlValue val) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("Buffer index assignment requires buffer");
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        hml_runtime_error("Buffer index %d out of bounds (length %d)", idx, b->length);
    }

    uint8_t *data = (uint8_t *)b->data;
    data[idx] = (uint8_t)hml_to_i32(val);
}

HmlValue hml_buffer_length(HmlValue buf) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("length requires buffer");
    }
    return hml_val_i32(buf.as.as_buffer->length);
}

HmlValue hml_buffer_capacity(HmlValue buf) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("capacity requires buffer");
    }
    return hml_val_i32(buf.as.as_buffer->capacity);
}

// ========== FFI CALLBACK OPERATIONS ==========

// Create an FFI callback that wraps a Hemlock function
// This is a stub implementation - full FFI callbacks require libffi closure support
HmlValue hml_callback_create(HmlValue fn, HmlValue arg_types, HmlValue ret_type) {
    (void)fn;
    (void)arg_types;
    (void)ret_type;
    // TODO: Implement proper FFI callback using ffi_prep_closure_loc
    // For now, return null to indicate callbacks are not supported in compiled mode
    hml_runtime_error("FFI callbacks not yet supported in compiled mode");
    return hml_val_null();
}

// Free an FFI callback
void hml_callback_free(HmlValue callback) {
    (void)callback;
    // No-op for stub implementation
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

void hml_array_push(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("push() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    a->elements[a->length] = val;
    hml_retain(&a->elements[a->length]);
    a->length++;
}

HmlValue hml_array_get(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Index access requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        hml_runtime_error("Array index %d out of bounds (length %d)", idx, a->length);
    }

    HmlValue result = a->elements[idx];
    // OPTIMIZATION: Skip retain for primitive types
    hml_retain_if_needed(&result);
    return result;
}

void hml_array_set(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Index assignment requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    if (idx < 0) {
        hml_runtime_error("Negative array index not supported");
    }

    // Extend array if needed, filling with nulls (match interpreter behavior)
    while (idx >= a->length) {
        // Grow capacity if needed
        if (a->length >= a->capacity) {
            int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
            a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
            a->capacity = new_cap;
        }
        a->elements[a->length] = hml_val_null();
        a->length++;
    }

    hml_release(&a->elements[idx]);
    a->elements[idx] = val;
    hml_retain(&a->elements[idx]);
}

HmlValue hml_array_length(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        return hml_val_i32(0);
    }
    return hml_val_i32(arr.as.as_array->length);
}

HmlValue hml_array_pop(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("pop() requires array");
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[a->length - 1];
    // Don't release - we're transferring ownership
    a->length--;
    return result;
}

HmlValue hml_array_shift(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("shift() requires array");
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[0];
    // Shift elements left
    for (int i = 0; i < a->length - 1; i++) {
        a->elements[i] = a->elements[i + 1];
    }
    a->length--;
    return result;
}

void hml_array_unshift(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("unshift() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    // Shift elements right
    for (int i = a->length; i > 0; i--) {
        a->elements[i] = a->elements[i - 1];
    }

    a->elements[0] = val;
    hml_retain(&a->elements[0]);
    a->length++;
}

void hml_array_insert(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("insert() requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    if (idx < 0 || idx > a->length) {
        hml_runtime_error("insert index %d out of bounds (length %d)", idx, a->length);
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    // Shift elements right from idx
    for (int i = a->length; i > idx; i--) {
        a->elements[i] = a->elements[i - 1];
    }

    a->elements[idx] = val;
    hml_retain(&a->elements[idx]);
    a->length++;
}

HmlValue hml_array_remove(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("remove() requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        hml_runtime_error("remove index %d out of bounds (length %d)", idx, a->length);
    }

    HmlValue result = a->elements[idx];
    // Shift elements left
    for (int i = idx; i < a->length - 1; i++) {
        a->elements[i] = a->elements[i + 1];
    }
    a->length--;
    return result;
}

HmlValue hml_array_find(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("find() requires array");
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        if (hml_values_equal(a->elements[i], val)) {
            return hml_val_i32(i);
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_array_contains(HmlValue arr, HmlValue val) {
    HmlValue idx = hml_array_find(arr, val);
    return hml_val_bool(idx.as.as_i32 >= 0);
}

HmlValue hml_array_slice(HmlValue arr, HmlValue start, HmlValue end) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("slice() requires array");
    }

    HmlArray *a = arr.as.as_array;
    int s = hml_to_i32(start);
    int e = hml_to_i32(end);

    // Clamp values
    if (s < 0) s = 0;
    if (e > a->length) e = a->length;
    if (s > e) s = e;

    int new_len = e - s;
    HmlArray *result = malloc(sizeof(HmlArray));
    result->ref_count = 1;
    result->length = new_len;
    result->capacity = (new_len > 0) ? new_len : 1;
    result->elements = malloc(result->capacity * sizeof(HmlValue));
    result->element_type = HML_VAL_NULL;
    atomic_store(&result->freed, 0);  // Not freed

    for (int i = 0; i < new_len; i++) {
        result->elements[i] = a->elements[s + i];
        hml_retain(&result->elements[i]);
    }

    HmlValue val;
    val.type = HML_VAL_ARRAY;
    val.as.as_array = result;
    return val;
}

HmlValue hml_array_join(HmlValue arr, HmlValue delimiter) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("join() requires array");
    }
    if (delimiter.type != HML_VAL_STRING) {
        hml_runtime_error("join() requires string delimiter");
    }

    HmlArray *a = arr.as.as_array;
    const char *delim = delimiter.as.as_string->data;
    int delim_len = delimiter.as.as_string->length;

    if (a->length == 0) {
        return hml_val_string("");
    }

    // Calculate total length
    int total_len = 0;
    for (int i = 0; i < a->length; i++) {
        HmlValue str = hml_to_string(a->elements[i]);
        total_len += str.as.as_string->length;
        if (i < a->length - 1) {
            total_len += delim_len;
        }
        hml_release(&str);
    }

    char *result = malloc(total_len + 1);
    int pos = 0;
    for (int i = 0; i < a->length; i++) {
        HmlValue str = hml_to_string(a->elements[i]);
        memcpy(result + pos, str.as.as_string->data, str.as.as_string->length);
        pos += str.as.as_string->length;
        hml_release(&str);
        if (i < a->length - 1) {
            memcpy(result + pos, delim, delim_len);
            pos += delim_len;
        }
    }
    result[total_len] = '\0';

    return hml_val_string_owned(result, total_len, total_len + 1);
}

HmlValue hml_array_concat(HmlValue arr1, HmlValue arr2) {
    if (arr1.type != HML_VAL_ARRAY || !arr1.as.as_array) {
        hml_runtime_error("concat() requires array");
    }
    if (arr2.type != HML_VAL_ARRAY || !arr2.as.as_array) {
        hml_runtime_error("concat() requires array argument");
    }

    HmlArray *a1 = arr1.as.as_array;
    HmlArray *a2 = arr2.as.as_array;
    int new_len = a1->length + a2->length;

    HmlArray *result = malloc(sizeof(HmlArray));
    result->ref_count = 1;
    result->length = new_len;
    result->capacity = (new_len > 0) ? new_len : 1;
    result->elements = malloc(result->capacity * sizeof(HmlValue));
    result->element_type = HML_VAL_NULL;
    atomic_store(&result->freed, 0);  // Not freed

    for (int i = 0; i < a1->length; i++) {
        result->elements[i] = a1->elements[i];
        hml_retain(&result->elements[i]);
    }
    for (int i = 0; i < a2->length; i++) {
        result->elements[a1->length + i] = a2->elements[i];
        hml_retain(&result->elements[a1->length + i]);
    }

    HmlValue val;
    val.type = HML_VAL_ARRAY;
    val.as.as_array = result;
    return val;
}

void hml_array_reverse(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("reverse() requires array");
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length / 2; i++) {
        HmlValue tmp = a->elements[i];
        a->elements[i] = a->elements[a->length - 1 - i];
        a->elements[a->length - 1 - i] = tmp;
    }
}

HmlValue hml_array_first(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("first() requires array");
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[0];
    hml_retain(&result);
    return result;
}

HmlValue hml_array_last(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("last() requires array");
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[a->length - 1];
    hml_retain(&result);
    return result;
}

void hml_array_clear(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("clear() requires array");
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        hml_release(&a->elements[i]);
    }
    a->length = 0;
}

// ========== TYPED ARRAY SUPPORT ==========

void hml_array_set_element_type(HmlValue arr, HmlValueType element_type) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("cannot set element type on non-array");
    }
    arr.as.as_array->element_type = element_type;
}

// Helper: Check if value type matches expected element type
static int hml_type_matches(HmlValue val, HmlValueType expected) {
    if (expected == HML_VAL_NULL) return 1;  // Untyped, accept anything
    return val.type == expected;
}

// Validate and set element type constraint on array
HmlValue hml_validate_typed_array(HmlValue arr, HmlValueType element_type) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Expected array");
    }

    HmlArray *a = arr.as.as_array;

    // If HML_VAL_NULL, it's an untyped array - no constraint
    if (element_type == HML_VAL_NULL) {
        return arr;
    }

    // Validate all existing elements match the type constraint
    for (int i = 0; i < a->length; i++) {
        if (!hml_type_matches(a->elements[i], element_type)) {
            hml_runtime_error("Type mismatch in typed array - expected element of specific type");
        }
    }

    // Set the element type constraint
    a->element_type = element_type;
    return arr;
}

// ========== HIGHER-ORDER ARRAY FUNCTIONS ==========

HmlValue hml_array_map(HmlValue arr, HmlValue callback) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("map() requires array");
    }

    HmlArray *a = arr.as.as_array;
    HmlValue result = hml_val_array();

    for (int i = 0; i < a->length; i++) {
        HmlValue args[1] = { a->elements[i] };
        HmlValue mapped = hml_call_function(callback, args, 1);
        hml_array_push(result, mapped);
        hml_release(&mapped);
    }

    return result;
}

HmlValue hml_array_filter(HmlValue arr, HmlValue predicate) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("filter() requires array");
    }

    HmlArray *a = arr.as.as_array;
    HmlValue result = hml_val_array();

    for (int i = 0; i < a->length; i++) {
        HmlValue args[1] = { a->elements[i] };
        HmlValue keep = hml_call_function(predicate, args, 1);
        if (hml_to_bool(keep)) {
            HmlValue elem = a->elements[i];
            hml_retain(&elem);
            hml_array_push(result, elem);
            hml_release(&elem);
        }
        hml_release(&keep);
    }

    return result;
}

HmlValue hml_array_reduce(HmlValue arr, HmlValue reducer, HmlValue initial) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("reduce() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Handle empty array
    if (a->length == 0) {
        if (initial.type == HML_VAL_NULL) {
            hml_runtime_error("reduce() of empty array with no initial value");
        }
        hml_retain(&initial);
        return initial;
    }

    // Determine starting accumulator and index
    HmlValue acc;
    int start_idx;
    if (initial.type == HML_VAL_NULL) {
        // No initial value - use first element
        acc = a->elements[0];
        hml_retain(&acc);
        start_idx = 1;
    } else {
        acc = initial;
        hml_retain(&acc);
        start_idx = 0;
    }

    // Reduce
    for (int i = start_idx; i < a->length; i++) {
        HmlValue args[2] = { acc, a->elements[i] };
        HmlValue new_acc = hml_call_function(reducer, args, 2);
        hml_release(&acc);
        acc = new_acc;
    }

    return acc;
}

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

// Visited set for cycle detection
typedef struct HmlVisitedSet {
    void **items;
    int count;
    int capacity;
} HmlVisitedSet;

static void visited_init(HmlVisitedSet *set) {
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void visited_free(HmlVisitedSet *set) {
    free(set->items);
}

static int visited_contains(HmlVisitedSet *set, void *ptr) {
    for (int i = 0; i < set->count; i++) {
        if (set->items[i] == ptr) return 1;
    }
    return 0;
}

static void visited_add(HmlVisitedSet *set, void *ptr) {
    if (set->count >= set->capacity) {
        int new_cap = (set->capacity == 0) ? 16 : set->capacity * 2;
        set->items = realloc(set->items, new_cap * sizeof(void*));
        set->capacity = new_cap;
    }
    set->items[set->count++] = ptr;
}

// JSON StringBuilder - accumulates output in a single growing buffer
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} HmlJsonBuffer;

static inline void hjbuf_init(HmlJsonBuffer *buf, size_t initial_capacity) {
    buf->capacity = initial_capacity > 64 ? initial_capacity : 64;
    buf->data = malloc(buf->capacity);
    buf->len = 0;
}

static inline void hjbuf_ensure(HmlJsonBuffer *buf, size_t additional) {
    size_t needed = buf->len + additional;
    if (needed > buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        if (new_cap < needed) new_cap = needed;
        buf->data = realloc(buf->data, new_cap);
        buf->capacity = new_cap;
    }
}

static inline void hjbuf_append_char(HmlJsonBuffer *buf, char c) {
    hjbuf_ensure(buf, 1);
    buf->data[buf->len++] = c;
}

static inline void hjbuf_append_str(HmlJsonBuffer *buf, const char *s, size_t len) {
    hjbuf_ensure(buf, len);
    memcpy(buf->data + buf->len, s, len);
    buf->len += len;
}

// Fast integer to string - write directly to buffer
static inline void hjbuf_append_i64(HmlJsonBuffer *buf, int64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);
    int negative = val < 0;
    if (negative) val = -val;

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    if (negative) *--p = '-';
    hjbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

static inline void hjbuf_append_u64(HmlJsonBuffer *buf, uint64_t val) {
    char tmp[24];
    char *p = tmp + sizeof(tmp);

    do {
        *--p = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    hjbuf_append_str(buf, p, (tmp + sizeof(tmp)) - p);
}

// Append escaped JSON string directly to buffer
static inline void hjbuf_append_escaped_string(HmlJsonBuffer *buf, const char *str, size_t str_len) {
    hjbuf_append_char(buf, '"');
    hjbuf_ensure(buf, str_len * 2);

    const char *end = str + str_len;
    while (str < end) {
        unsigned char c = *str++;
        switch (c) {
            case '"':  buf->data[buf->len++] = '\\'; buf->data[buf->len++] = '"'; break;
            case '\\': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = '\\'; break;
            case '\n': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'n'; break;
            case '\r': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'r'; break;
            case '\t': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 't'; break;
            case '\b': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'b'; break;
            case '\f': buf->data[buf->len++] = '\\'; buf->data[buf->len++] = 'f'; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    hjbuf_ensure(buf, 6);
                    buf->data[buf->len++] = '\\';
                    buf->data[buf->len++] = 'u';
                    buf->data[buf->len++] = '0';
                    buf->data[buf->len++] = '0';
                    buf->data[buf->len++] = hex[c >> 4];
                    buf->data[buf->len++] = hex[c & 0xF];
                } else {
                    buf->data[buf->len++] = c;
                }
                break;
        }
    }
    hjbuf_append_char(buf, '"');
}

// Forward declaration
static int serialize_to_buffer_impl(HmlValue val, HmlJsonBuffer *buf, HmlVisitedSet *visited);

static int serialize_to_buffer_impl(HmlValue val, HmlJsonBuffer *buf, HmlVisitedSet *visited) {
    char tmp[32];

    switch (val.type) {
        case HML_VAL_I8:
            hjbuf_append_i64(buf, val.as.as_i8);
            return 1;
        case HML_VAL_I16:
            hjbuf_append_i64(buf, val.as.as_i16);
            return 1;
        case HML_VAL_I32:
            hjbuf_append_i64(buf, val.as.as_i32);
            return 1;
        case HML_VAL_I64:
            hjbuf_append_i64(buf, val.as.as_i64);
            return 1;
        case HML_VAL_U8:
            hjbuf_append_u64(buf, val.as.as_u8);
            return 1;
        case HML_VAL_U16:
            hjbuf_append_u64(buf, val.as.as_u16);
            return 1;
        case HML_VAL_U32:
            hjbuf_append_u64(buf, val.as.as_u32);
            return 1;
        case HML_VAL_U64:
            hjbuf_append_u64(buf, val.as.as_u64);
            return 1;
        case HML_VAL_F32: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f32);
            hjbuf_append_str(buf, tmp, len);
            return 1;
        }
        case HML_VAL_F64: {
            int len = snprintf(tmp, sizeof(tmp), "%g", val.as.as_f64);
            hjbuf_append_str(buf, tmp, len);
            return 1;
        }
        case HML_VAL_BOOL:
            if (val.as.as_bool) {
                hjbuf_append_str(buf, "true", 4);
            } else {
                hjbuf_append_str(buf, "false", 5);
            }
            return 1;
        case HML_VAL_STRING: {
            HmlString *s = val.as.as_string;
            hjbuf_append_escaped_string(buf, s->data, s->length);
            return 1;
        }
        case HML_VAL_NULL:
            hjbuf_append_str(buf, "null", 4);
            return 1;
        case HML_VAL_OBJECT: {
            HmlObject *obj = val.as.as_object;
            if (!obj) {
                hjbuf_append_str(buf, "null", 4);
                return 1;
            }

            if (visited_contains(visited, obj)) {
                hml_runtime_error("serialize() detected circular reference");
            }
            visited_add(visited, obj);

            hjbuf_append_char(buf, '{');

            for (int i = 0; i < obj->num_fields; i++) {
                if (i > 0) hjbuf_append_char(buf, ',');

                const char *name = obj->field_names[i];
                hjbuf_append_escaped_string(buf, name, strlen(name));
                hjbuf_append_char(buf, ':');

                if (!serialize_to_buffer_impl(obj->field_values[i], buf, visited)) {
                    return 0;
                }
            }

            hjbuf_append_char(buf, '}');
            return 1;
        }
        case HML_VAL_ARRAY: {
            HmlArray *arr = val.as.as_array;
            if (!arr) {
                hjbuf_append_str(buf, "null", 4);
                return 1;
            }

            if (visited_contains(visited, arr)) {
                hml_runtime_error("serialize() detected circular reference");
            }
            visited_add(visited, arr);

            hjbuf_append_char(buf, '[');

            for (int i = 0; i < arr->length; i++) {
                if (i > 0) hjbuf_append_char(buf, ',');

                if (!serialize_to_buffer_impl(arr->elements[i], buf, visited)) {
                    return 0;
                }
            }

            hjbuf_append_char(buf, ']');
            return 1;
        }
        default:
            hml_runtime_error("Cannot serialize value of this type");
    }
    return 0;
}

HmlValue hml_serialize(HmlValue val) {
    HmlVisitedSet visited;
    visited_init(&visited);

    HmlJsonBuffer buf;
    hjbuf_init(&buf, 256);

    serialize_to_buffer_impl(val, &buf, &visited);

    visited_free(&visited);

    hjbuf_append_char(&buf, '\0');
    return hml_val_string_owned(buf.data, buf.len - 1, buf.capacity);
}

// JSON Parser state
typedef struct {
    const char *input;
    int pos;
} HmlJSONParser;

// Optimized whitespace skip
static inline void json_skip_whitespace(HmlJSONParser *p) {
    const char *s = p->input + p->pos;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    p->pos = s - p->input;
}

// Forward declarations
static HmlValue json_parse_value(HmlJSONParser *p);
static HmlValue json_parse_string(HmlJSONParser *p);
static HmlValue json_parse_number(HmlJSONParser *p);
static HmlValue json_parse_object(HmlJSONParser *p);
static HmlValue json_parse_array(HmlJSONParser *p);

// Optimized string parsing with fast path for strings without escapes
static HmlValue json_parse_string(HmlJSONParser *p) {
    if (p->input[p->pos] != '"') {
        hml_runtime_error("Expected '\"' in JSON");
    }
    p->pos++;  // skip opening quote

    const char *start = p->input + p->pos;
    const char *s = start;

    // Fast scan for end quote and check for escapes
    int has_escapes = 0;
    while (*s != '"' && *s != '\0') {
        if (*s == '\\') {
            has_escapes = 1;
            s++;
            if (*s) s++;
        } else {
            s++;
        }
    }

    if (*s != '"') {
        hml_runtime_error("Unterminated string in JSON");
    }

    size_t raw_len = s - start;

    if (!has_escapes) {
        // Fast path: direct copy
        char *buf = malloc(raw_len + 1);
        memcpy(buf, start, raw_len);
        buf[raw_len] = '\0';
        p->pos += raw_len + 1;
        return hml_val_string_owned(buf, raw_len, raw_len + 1);
    }

    // Slow path: handle escapes
    char *buf = malloc(raw_len + 1);
    char *out = buf;
    s = start;

    while (*s != '"') {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case 'n': *out++ = '\n'; break;
                case 'r': *out++ = '\r'; break;
                case 't': *out++ = '\t'; break;
                case 'b': *out++ = '\b'; break;
                case 'f': *out++ = '\f'; break;
                case '"': *out++ = '"'; break;
                case '\\': *out++ = '\\'; break;
                case '/': *out++ = '/'; break;
                case 'u': {
                    s++;
                    if (s[0] && s[1] && s[2] && s[3]) {
                        int codepoint = 0;
                        for (int i = 0; i < 4; i++) {
                            char c = s[i];
                            codepoint <<= 4;
                            if (c >= '0' && c <= '9') codepoint |= c - '0';
                            else if (c >= 'a' && c <= 'f') codepoint |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') codepoint |= c - 'A' + 10;
                        }
                        if (codepoint < 0x80) {
                            *out++ = codepoint;
                        } else if (codepoint < 0x800) {
                            *out++ = 0xC0 | (codepoint >> 6);
                            *out++ = 0x80 | (codepoint & 0x3F);
                        } else {
                            *out++ = 0xE0 | (codepoint >> 12);
                            *out++ = 0x80 | ((codepoint >> 6) & 0x3F);
                            *out++ = 0x80 | (codepoint & 0x3F);
                        }
                        s += 3;
                    }
                    break;
                }
                default:
                    free(buf);
                    hml_runtime_error("Invalid escape sequence in JSON");
            }
            s++;
        } else {
            *out++ = *s++;
        }
    }

    *out = '\0';
    p->pos = (s - p->input) + 1;
    size_t len = out - buf;
    return hml_val_string_owned(buf, len, raw_len + 1);
}

// Optimized number parsing - no allocation
static HmlValue json_parse_number(HmlJSONParser *p) {
    const char *s = p->input + p->pos;
    int negative = 0;

    if (*s == '-') {
        negative = 1;
        s++;
    }

    int64_t int_val = 0;
    while (*s >= '0' && *s <= '9') {
        int_val = int_val * 10 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double frac = 0.0;
        double divisor = 10.0;
        while (*s >= '0' && *s <= '9') {
            frac += (*s - '0') / divisor;
            divisor *= 10.0;
            s++;
        }

        if (*s == 'e' || *s == 'E') {
            s++;
            int exp_negative = 0;
            if (*s == '-') { exp_negative = 1; s++; }
            else if (*s == '+') { s++; }

            int exp = 0;
            while (*s >= '0' && *s <= '9') {
                exp = exp * 10 + (*s - '0');
                s++;
            }

            double multiplier = 1.0;
            for (int i = 0; i < exp; i++) multiplier *= 10.0;
            if (exp_negative) {
                frac = (int_val + frac) / multiplier;
                int_val = 0;
            } else {
                frac = (int_val + frac) * multiplier;
                int_val = 0;
            }
        }

        p->pos = s - p->input;
        double result = int_val + frac;
        return hml_val_f64(negative ? -result : result);
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_negative = 0;
        if (*s == '-') { exp_negative = 1; s++; }
        else if (*s == '+') { s++; }

        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }

        p->pos = s - p->input;
        double result = (double)int_val;
        double multiplier = 1.0;
        for (int i = 0; i < exp; i++) multiplier *= 10.0;
        result = exp_negative ? result / multiplier : result * multiplier;
        return hml_val_f64(negative ? -result : result);
    }

    p->pos = s - p->input;

    if (int_val <= 2147483647) {
        return hml_val_i32(negative ? -(int32_t)int_val : (int32_t)int_val);
    }
    return hml_val_i64(negative ? -int_val : int_val);
}

static HmlValue json_parse_object(HmlJSONParser *p) {
    if (p->input[p->pos] != '{') {
        hml_runtime_error("Expected '{' in JSON");
    }
    p->pos++;

    HmlValue obj = hml_val_object();

    json_skip_whitespace(p);

    if (p->input[p->pos] == '}') {
        p->pos++;
        return obj;
    }

    while (p->input[p->pos] != '}' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        HmlValue name_val = json_parse_string(p);
        char *field_name = strdup(name_val.as.as_string->data);
        hml_release(&name_val);

        json_skip_whitespace(p);

        if (p->input[p->pos] != ':') {
            free(field_name);
            hml_runtime_error("Expected ':' in JSON object");
        }
        p->pos++;

        json_skip_whitespace(p);

        HmlValue field_value = json_parse_value(p);
        hml_object_set_field(obj, field_name, field_value);
        hml_release(&field_value);
        free(field_name);

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != '}') {
            hml_runtime_error("Expected ',' or '}' in JSON object");
        }
    }

    if (p->input[p->pos] != '}') {
        hml_runtime_error("Unterminated object in JSON");
    }
    p->pos++;

    return obj;
}

static HmlValue json_parse_array(HmlJSONParser *p) {
    if (p->input[p->pos] != '[') {
        hml_runtime_error("Expected '[' in JSON");
    }
    p->pos++;

    HmlValue arr = hml_val_array();

    json_skip_whitespace(p);

    if (p->input[p->pos] == ']') {
        p->pos++;
        return arr;
    }

    while (p->input[p->pos] != ']' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        HmlValue elem = json_parse_value(p);
        hml_array_push(arr, elem);
        hml_release(&elem);

        json_skip_whitespace(p);

        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != ']') {
            hml_runtime_error("Expected ',' or ']' in JSON array");
        }
    }

    if (p->input[p->pos] != ']') {
        hml_runtime_error("Unterminated array in JSON");
    }
    p->pos++;

    return arr;
}

// Optimized json_parse_value with direct character comparisons
static HmlValue json_parse_value(HmlJSONParser *p) {
    json_skip_whitespace(p);

    const char *s = p->input + p->pos;
    char c = *s;

    switch (c) {
        case '"':
            return json_parse_string(p);
        case '{':
            return json_parse_object(p);
        case '[':
            return json_parse_array(p);
        case 't':
            if (s[1] == 'r' && s[2] == 'u' && s[3] == 'e') {
                p->pos += 4;
                return hml_val_bool(1);
            }
            break;
        case 'f':
            if (s[1] == 'a' && s[2] == 'l' && s[3] == 's' && s[4] == 'e') {
                p->pos += 5;
                return hml_val_bool(0);
            }
            break;
        case 'n':
            if (s[1] == 'u' && s[2] == 'l' && s[3] == 'l') {
                p->pos += 4;
                return hml_val_null();
            }
            break;
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return json_parse_number(p);
    }

    hml_runtime_error("Unexpected character '%c' in JSON", c);
}

HmlValue hml_deserialize(HmlValue json_str) {
    if (json_str.type != HML_VAL_STRING || !json_str.as.as_string) {
        hml_runtime_error("deserialize() requires string argument");
    }

    HmlJSONParser parser = {
        .input = json_str.as.as_string->data,
        .pos = 0
    };

    return json_parse_value(&parser);
}

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

        // Arity check (error cases are rare)
        if (__builtin_expect(num_args < num_required, 0)) {
            hml_runtime_error("Function expects %d arguments, got %d", num_required, num_args);
        }
        if (__builtin_expect(num_args > num_params, 0)) {
            hml_runtime_error("Function expects %d arguments, got %d", num_params, num_args);
        }

        HmlClosureEnv *env = (HmlClosureEnv*)func->closure_env;

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

    fseek(fp, 0, SEEK_END);
    long end_pos = ftell(fp);
    fseek(fp, start_pos, SEEK_SET);

    long size = end_pos - start_pos;
    char *buffer = malloc(size + 1);
    size_t bytes_read = fread(buffer, 1, size, fp);
    buffer[bytes_read] = '\0';

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
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

// ========== ASYNC / CONCURRENCY ==========

#include <pthread.h>
#include <stdatomic.h>

static atomic_int g_next_task_id = 1;

// Thread wrapper function
static void* task_thread_wrapper(void* arg) {
    HmlTask *task = (HmlTask*)arg;

    // Mark as running
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->state = HML_TASK_RUNNING;
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Get function info
    HmlFunction *fn = task->function.as.as_function;
    void *fn_ptr = fn->fn_ptr;
    void *closure_env = fn->closure_env;

    // Call function with arguments based on num_args
    HmlValue result;
    typedef HmlValue (*Fn0)(void*);
    typedef HmlValue (*Fn1)(void*, HmlValue);
    typedef HmlValue (*Fn2)(void*, HmlValue, HmlValue);
    typedef HmlValue (*Fn3)(void*, HmlValue, HmlValue, HmlValue);
    typedef HmlValue (*Fn4)(void*, HmlValue, HmlValue, HmlValue, HmlValue);
    typedef HmlValue (*Fn5)(void*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);

    switch (task->num_args) {
        case 0:
            result = ((Fn0)fn_ptr)(closure_env);
            break;
        case 1:
            result = ((Fn1)fn_ptr)(closure_env, task->args[0]);
            break;
        case 2:
            result = ((Fn2)fn_ptr)(closure_env, task->args[0], task->args[1]);
            break;
        case 3:
            result = ((Fn3)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2]);
            break;
        case 4:
            result = ((Fn4)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2], task->args[3]);
            break;
        case 5:
            result = ((Fn5)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2], task->args[3], task->args[4]);
            break;
        default:
            result = hml_val_null();
            break;
    }

    // Store result and mark as completed
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->result = result;
    task->state = HML_TASK_COMPLETED;
    pthread_cond_signal((pthread_cond_t*)task->cond);
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    return NULL;
}

HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args) {
    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("spawn() expects a function");
    }

    // Create task
    HmlTask *task = malloc(sizeof(HmlTask));
    task->id = atomic_fetch_add(&g_next_task_id, 1);
    task->state = HML_TASK_READY;
    task->result = hml_val_null();
    task->joined = 0;
    task->detached = 0;
    task->ref_count = 1;

    // Store function and args
    task->function = fn;
    hml_retain(&task->function);
    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            task->args[i] = args[i];
            hml_retain(&task->args[i]);
        }
    } else {
        task->args = NULL;
    }

    // Initialize mutex and condition variable
    task->mutex = malloc(sizeof(pthread_mutex_t));
    task->cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)task->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)task->cond, NULL);

    // Create thread
    task->thread = malloc(sizeof(pthread_t));
    pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);

    // Return task value
    HmlValue result;
    result.type = HML_VAL_TASK;
    result.as.as_task = task;
    return result;
}

HmlValue hml_join(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("join() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        hml_runtime_error("task handle already joined");
    }

    if (task->detached) {
        hml_runtime_error("cannot join detached task");
    }

    // Wait for task to complete
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    while (task->state != HML_TASK_COMPLETED) {
        pthread_cond_wait((pthread_cond_t*)task->cond, (pthread_mutex_t*)task->mutex);
    }
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Join the thread
    pthread_join(*(pthread_t*)task->thread, NULL);
    task->joined = 1;

    // Return result (retained)
    HmlValue result = task->result;
    hml_retain(&result);
    return result;
}

void hml_detach(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("detach() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        hml_runtime_error("cannot detach already joined task");
    }

    if (task->detached) {
        return; // Already detached
    }

    task->detached = 1;
    pthread_detach(*(pthread_t*)task->thread);
}

// task_debug_info(task) - Print debug information about a task
void hml_task_debug_info(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("task_debug_info() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    // Lock mutex to safely read task state
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);

    printf("=== Task Debug Info ===\n");
    printf("Task ID: %d\n", task->id);
    printf("State: ");
    switch (task->state) {
        case HML_TASK_READY: printf("READY\n"); break;
        case HML_TASK_RUNNING: printf("RUNNING\n"); break;
        case HML_TASK_COMPLETED: printf("COMPLETED\n"); break;
        default: printf("UNKNOWN\n"); break;
    }
    printf("Joined: %s\n", task->joined ? "true" : "false");
    printf("Detached: %s\n", task->detached ? "true" : "false");
    printf("Ref Count: %d\n", task->ref_count);
    printf("Has Result: %s\n", task->result.type != HML_VAL_NULL ? "true" : "false");
    printf("======================\n");

    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);
}

// Channel functions
HmlValue hml_channel(int32_t capacity) {
    HmlChannel *ch = malloc(sizeof(HmlChannel));
    ch->capacity = capacity;
    ch->buffer = malloc(sizeof(HmlValue) * capacity);
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->ref_count = 1;

    ch->mutex = malloc(sizeof(pthread_mutex_t));
    ch->not_empty = malloc(sizeof(pthread_cond_t));
    ch->not_full = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);

    HmlValue result;
    result.type = HML_VAL_CHANNEL;
    result.as.as_channel = ch;
    return result;
}

void hml_channel_send(HmlValue channel, HmlValue value) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("send() expects a channel");
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Wait while buffer is full
    while (ch->count == ch->capacity && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_full, (pthread_mutex_t*)ch->mutex);
    }

    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal((pthread_cond_t*)ch->not_empty);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
}

HmlValue hml_channel_recv(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("recv() expects a channel");
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_empty, (pthread_mutex_t*)ch->mutex);
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

    return value;
}

// channel.recv_timeout(timeout_ms) - receive with timeout, returns null on timeout
HmlValue hml_channel_recv_timeout(HmlValue channel, HmlValue timeout_val) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("recv_timeout() expects a channel");
    }

    int timeout_ms = hml_to_i32(timeout_val);
    HmlChannel *ch = channel.as.as_channel;

    // Calculate deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        int rc = pthread_cond_timedwait((pthread_cond_t*)ch->not_empty,
                                        (pthread_mutex_t*)ch->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_null();  // Timeout
        }
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

    return value;
}

// channel.send_timeout(value, timeout_ms) - send with timeout, returns bool (true if sent)
HmlValue hml_channel_send_timeout(HmlValue channel, HmlValue value, HmlValue timeout_val) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("send_timeout() expects a channel");
    }

    int timeout_ms = hml_to_i32(timeout_val);
    HmlChannel *ch = channel.as.as_channel;

    // Calculate deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Check if channel is closed
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Wait while buffer is full
    while (ch->count >= ch->capacity && !ch->closed) {
        int rc = pthread_cond_timedwait((pthread_cond_t*)ch->not_full,
                                        (pthread_mutex_t*)ch->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_bool(0);  // Timeout - send failed
        }
    }

    // Check again if closed after waking up
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal((pthread_cond_t*)ch->not_empty);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

    return hml_val_bool(1);  // Success
}

void hml_channel_close(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        return;
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);
    ch->closed = 1;
    pthread_cond_broadcast((pthread_cond_t*)ch->not_empty);
    pthread_cond_broadcast((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
}

// select(channels, timeout_ms?) - wait on multiple channels
HmlValue hml_select(HmlValue channels, HmlValue timeout) {
    if (channels.type != HML_VAL_ARRAY) {
        hml_runtime_error("select() expects array of channels as first argument");
    }

    HmlArray *arr = channels.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("select() requires at least one channel");
    }

    // Validate all elements are channels
    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != HML_VAL_CHANNEL) {
            hml_runtime_error("select() array must contain only channels");
        }
    }

    // Get timeout in milliseconds (-1 means infinite)
    int timeout_ms = -1;
    if (timeout.type != HML_VAL_NULL) {
        timeout_ms = hml_to_i32(timeout);
    }

    // Calculate deadline
    struct timespec deadline;
    struct timespec *deadline_ptr = NULL;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }
        deadline_ptr = &deadline;
    }

    // Polling loop
    while (1) {
        // Check each channel for available data
        for (int i = 0; i < arr->length; i++) {
            HmlChannel *ch = arr->elements[i].as.as_channel;
            pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;

            pthread_mutex_lock(mutex);

            // Check if channel has data
            if (ch->count > 0) {
                // Read the value
                HmlValue msg = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;

                // Signal that buffer is not full
                pthread_cond_signal((pthread_cond_t*)ch->not_full);
                pthread_mutex_unlock(mutex);

                // Create result object { channel, value }
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", msg);
                return result;
            }

            // Check if channel is closed and empty
            if (ch->closed) {
                pthread_mutex_unlock(mutex);
                // Return object with null value for closed channel
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", hml_val_null());
                return result;
            }

            pthread_mutex_unlock(mutex);
        }

        // Check timeout
        if (deadline_ptr) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline_ptr->tv_sec ||
                (now.tv_sec == deadline_ptr->tv_sec && now.tv_nsec >= deadline_ptr->tv_nsec)) {
                return hml_val_null();  // Timeout
            }
        }

        // Sleep briefly before polling again (1ms)
        usleep(1000);
    }
}

// Helper to get fd from a socket, file, or object with fd field
static int hml_get_fd_from_value(HmlValue val) {
    if (val.type == HML_VAL_SOCKET) {
        HmlSocket *s = val.as.as_socket;
        return s ? s->fd : -1;
    }
    if (val.type == HML_VAL_FILE) {
        HmlFileHandle *f = val.as.as_file;
        if (f && f->fp) {
            return fileno(f->fp);
        }
        return -1;
    }
    if (val.type == HML_VAL_OBJECT) {
        HmlValue fd_val = hml_object_get_field(val, "fd");
        if (hml_is_integer(fd_val)) {
            return hml_to_i32(fd_val);
        }
    }
    return -1;
}

// poll(fds, timeout_ms) - wait for I/O events on file descriptors
HmlValue hml_poll(HmlValue fds, HmlValue timeout) {
    if (fds.type != HML_VAL_ARRAY) {
        hml_runtime_error("poll() expects array as first argument");
    }

    HmlArray *arr = fds.as.as_array;
    int timeout_ms = hml_to_i32(timeout);

    if (arr->length == 0) {
        // Return empty array
        HmlValue result = hml_val_array();
        return result;
    }

    // Build pollfd array
    struct pollfd *pfds = malloc(sizeof(struct pollfd) * arr->length);
    if (!pfds) {
        hml_runtime_error("poll() memory allocation failed");
    }

    // Store original fd values for return
    HmlValue *original_fds = malloc(sizeof(HmlValue) * arr->length);
    if (!original_fds) {
        free(pfds);
        hml_runtime_error("poll() memory allocation failed");
    }

    for (int i = 0; i < arr->length; i++) {
        HmlValue item = arr->elements[i];

        if (item.type != HML_VAL_OBJECT) {
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() array elements must be objects with 'fd' and 'events'");
        }

        // Get fd field
        HmlValue fd_val = hml_object_get_field(item, "fd");
        HmlValue events_val = hml_object_get_field(item, "events");

        int fd = hml_get_fd_from_value(fd_val);
        if (fd < 0) {
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() fd must be a socket or file");
        }

        if (!hml_is_integer(events_val)) {
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() events must be an integer");
        }

        pfds[i].fd = fd;
        pfds[i].events = (short)hml_to_i32(events_val);
        pfds[i].revents = 0;
        original_fds[i] = fd_val;
        hml_retain(&original_fds[i]);
    }

    // Call poll
    int result = poll(pfds, arr->length, timeout_ms);

    if (result < 0) {
        for (int i = 0; i < arr->length; i++) {
            hml_release(&original_fds[i]);
        }
        free(pfds);
        free(original_fds);
        hml_runtime_error("poll() failed");
    }

    // Build result array with fds that have events
    HmlValue result_arr = hml_val_array();

    for (int i = 0; i < arr->length; i++) {
        if (pfds[i].revents != 0) {
            HmlValue obj = hml_val_object();
            hml_object_set_field(obj, "fd", original_fds[i]);
            hml_object_set_field(obj, "revents", hml_val_i32(pfds[i].revents));
            hml_array_push(result_arr, obj);
            hml_release(&obj);
        }
        hml_release(&original_fds[i]);
    }

    free(pfds);
    free(original_fds);
    return result_arr;
}

// ========== CALL STACK TRACKING ==========

// Thread-local call depth counter for stack overflow detection
// Exposed globally for inline macro access (hml_g_call_depth)
__thread int hml_g_call_depth = 0;

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

// ========== SOCKET OPERATIONS ==========

// socket_create(domain, type, protocol) -> socket
HmlValue hml_socket_create(HmlValue domain, HmlValue sock_type, HmlValue protocol) {
    int d = hml_to_i32(domain);
    int t = hml_to_i32(sock_type);
    int p = hml_to_i32(protocol);

    int fd = socket(d, t, p);
    if (fd < 0) {
        hml_runtime_error("Failed to create socket: %s", strerror(errno));
    }

    HmlSocket *sock = malloc(sizeof(HmlSocket));
    sock->fd = fd;
    sock->address = NULL;
    sock->port = 0;
    sock->domain = d;
    sock->type = t;
    sock->closed = 0;
    sock->listening = 0;

    return hml_val_socket(sock);
}

// socket.bind(address, port)
void hml_socket_bind(HmlValue socket_val, HmlValue address, HmlValue port) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("bind() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot bind closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);

    if (strcmp(addr_str, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        hml_runtime_error("Invalid IP address: %s", addr_str);
    }

    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Runtime error: Failed to bind socket to %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    if (sock->address) free(sock->address);
    sock->address = strdup(addr_str);
    sock->port = p;
}

// socket.listen(backlog)
void hml_socket_listen(HmlValue socket_val, HmlValue backlog) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("listen() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot listen on closed socket");
    }

    int bl = hml_to_i32(backlog);
    if (listen(sock->fd, bl) < 0) {
        hml_runtime_error("Failed to listen on socket: %s", strerror(errno));
    }

    sock->listening = 1;
}

// socket.accept() -> socket
HmlValue hml_socket_accept(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("accept() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot accept on closed socket");
    }

    if (!sock->listening) {
        hml_runtime_error("Socket must be listening before accept()");
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(sock->fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        hml_runtime_error("Failed to accept connection: %s", strerror(errno));
    }

    HmlSocket *client_sock = malloc(sizeof(HmlSocket));
    client_sock->fd = client_fd;
    client_sock->domain = sock->domain;
    client_sock->type = sock->type;
    client_sock->closed = 0;
    client_sock->listening = 0;

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
    client_sock->address = strdup(addr_str);
    client_sock->port = ntohs(client_addr.sin_port);

    return hml_val_socket(client_sock);
}

// socket.connect(address, port)
void hml_socket_connect(HmlValue socket_val, HmlValue address, HmlValue port) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("connect() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot connect closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    struct hostent *host = gethostbyname(addr_str);
    if (!host) {
        hml_runtime_error("Failed to resolve hostname '%s'", addr_str);
    }

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(p);
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);

    if (connect(sock->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Runtime error: Failed to connect to %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    if (sock->address) free(sock->address);
    sock->address = strdup(addr_str);
    sock->port = p;
}

// socket.send(data) -> i32 (bytes sent)
HmlValue hml_socket_send(HmlValue socket_val, HmlValue data) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("send() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot send on closed socket");
    }

    const void *buf;
    size_t len;

    if (data.type == HML_VAL_STRING && data.as.as_string) {
        buf = data.as.as_string->data;
        len = data.as.as_string->length;
    } else if (data.type == HML_VAL_BUFFER && data.as.as_buffer) {
        buf = data.as.as_buffer->data;
        len = data.as.as_buffer->length;
    } else {
        hml_runtime_error("send() expects string or buffer");
    }

    ssize_t sent = send(sock->fd, buf, len, 0);
    if (sent < 0) {
        hml_runtime_error("Failed to send data: %s", strerror(errno));
    }

    return hml_val_i32((int32_t)sent);
}

// socket.recv(size) -> buffer
HmlValue hml_socket_recv(HmlValue socket_val, HmlValue size) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("recv() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot recv on closed socket");
    }

    int sz = hml_to_i32(size);
    if (sz <= 0) {
        return hml_val_buffer(0);
    }

    void *buf = malloc(sz);
    ssize_t received = recv(sock->fd, buf, sz, 0);
    if (received < 0) {
        free(buf);
        hml_runtime_error("Failed to receive data: %s", strerror(errno));
    }

    HmlBuffer *hbuf = malloc(sizeof(HmlBuffer));
    hbuf->data = buf;
    hbuf->length = (int)received;
    hbuf->capacity = sz;
    hbuf->ref_count = 1;
    atomic_store(&hbuf->freed, 0);  // Not freed

    HmlValue result;
    result.type = HML_VAL_BUFFER;
    result.as.as_buffer = hbuf;
    return result;
}

// socket.sendto(address, port, data) -> i32
HmlValue hml_socket_sendto(HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("sendto() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot sendto on closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    const void *buf;
    size_t len;

    if (data.type == HML_VAL_STRING && data.as.as_string) {
        buf = data.as.as_string->data;
        len = data.as.as_string->length;
    } else if (data.type == HML_VAL_BUFFER && data.as.as_buffer) {
        buf = data.as.as_buffer->data;
        len = data.as.as_buffer->length;
    } else {
        hml_runtime_error("sendto() data must be string or buffer");
    }

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(p);

    if (inet_pton(AF_INET, addr_str, &dest_addr.sin_addr) != 1) {
        hml_runtime_error("Invalid IP address: %s", addr_str);
    }

    ssize_t sent = sendto(sock->fd, buf, len, 0,
            (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        fprintf(stderr, "Runtime error: Failed to sendto %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    return hml_val_i32((int32_t)sent);
}

// socket.recvfrom(size) -> { data: buffer, address: string, port: i32 }
HmlValue hml_socket_recvfrom(HmlValue socket_val, HmlValue size) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("recvfrom() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot recvfrom on closed socket");
    }

    int sz = hml_to_i32(size);
    if (sz <= 0) {
        hml_runtime_error("recvfrom() size must be positive");
    }

    void *buf = malloc(sz);
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    ssize_t received = recvfrom(sock->fd, buf, sz, 0,
            (struct sockaddr *)&src_addr, &addr_len);

    if (received < 0) {
        free(buf);
        hml_runtime_error("Failed to recvfrom: %s", strerror(errno));
    }

    // Create buffer for data
    HmlBuffer *hbuf = malloc(sizeof(HmlBuffer));
    hbuf->data = buf;
    hbuf->length = (int)received;
    hbuf->capacity = sz;
    hbuf->ref_count = 1;
    atomic_store(&hbuf->freed, 0);  // Not freed

    // Get source address and port
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr, addr_str, sizeof(addr_str));
    int src_port = ntohs(src_addr.sin_port);

    // Create result object { data, address, port }
    HmlValue result = hml_val_object();
    HmlValue data_val;
    data_val.type = HML_VAL_BUFFER;
    data_val.as.as_buffer = hbuf;

    hml_object_set_field(result, "data", data_val);
    hml_object_set_field(result, "address", hml_val_string(addr_str));
    hml_object_set_field(result, "port", hml_val_i32(src_port));

    return result;
}

// socket.setsockopt(level, option, value)
void hml_socket_setsockopt(HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("setsockopt() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot setsockopt on closed socket");
    }

    int lvl = hml_to_i32(level);
    int opt = hml_to_i32(option);
    int val = hml_to_i32(value);

    if (setsockopt(sock->fd, lvl, opt, &val, sizeof(val)) < 0) {
        hml_runtime_error("Failed to set socket option: %s", strerror(errno));
    }
}

// socket.set_timeout(seconds)
void hml_socket_set_timeout(HmlValue socket_val, HmlValue seconds_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("set_timeout() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot set_timeout on closed socket");
    }

    double seconds = hml_to_f64(seconds_val);

    struct timeval timeout;
    timeout.tv_sec = (long)seconds;
    timeout.tv_usec = (long)((seconds - timeout.tv_sec) * 1000000);

    // Set both recv and send timeouts
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        hml_runtime_error("Failed to set receive timeout: %s", strerror(errno));
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        hml_runtime_error("Failed to set send timeout: %s", strerror(errno));
    }
}

// socket.set_nonblocking(enable: bool)
void hml_socket_set_nonblocking(HmlValue socket_val, HmlValue enable_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("set_nonblocking() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot set_nonblocking on closed socket");
    }

    int enable = hml_to_bool(enable_val);

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        hml_runtime_error("Failed to get socket flags: %s", strerror(errno));
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        hml_runtime_error("Failed to set socket flags: %s", strerror(errno));
    }

    sock->nonblocking = enable;
}

// socket.close()
void hml_socket_close(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("close() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    // Idempotent - safe to call multiple times
    if (!sock->closed && sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
        sock->closed = 1;
    }
}

// Socket property getters
HmlValue hml_socket_get_fd(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_i32(-1);
    }
    return hml_val_i32(socket_val.as.as_socket->fd);
}

HmlValue hml_socket_get_address(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_null();
    }
    HmlSocket *sock = socket_val.as.as_socket;
    if (sock->address) {
        return hml_val_string(sock->address);
    }
    return hml_val_null();
}

HmlValue hml_socket_get_port(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_i32(0);
    }
    return hml_val_i32(socket_val.as.as_socket->port);
}

HmlValue hml_socket_get_closed(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_bool(1);
    }
    return hml_val_bool(socket_val.as.as_socket->closed);
}

// ========== FFI (Foreign Function Interface) ==========

HmlValue hml_ffi_load(const char *path) {
    void *handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        hml_runtime_error("Failed to load library '%s': %s", path, dlerror());
    }
    return hml_val_ptr(handle);
}

void hml_ffi_close(HmlValue lib) {
    if (lib.type == HML_VAL_PTR && lib.as.as_ptr) {
        dlclose(lib.as.as_ptr);
    }
}

void* hml_ffi_sym(HmlValue lib, const char *name) {
    if (lib.type != HML_VAL_PTR || !lib.as.as_ptr) {
        hml_runtime_error("ffi_sym requires library handle");
    }
    dlerror(); // Clear errors
    void *sym = dlsym(lib.as.as_ptr, name);
    char *error = dlerror();
    if (error) {
        hml_runtime_error("Failed to find symbol '%s': %s", name, error);
    }
    return sym;
}

// Convert HmlFFIType to libffi type
static ffi_type* hml_ffi_type_to_ffi(HmlFFIType type) {
    switch (type) {
        case HML_FFI_VOID:   return &ffi_type_void;
        case HML_FFI_I8:     return &ffi_type_sint8;
        case HML_FFI_I16:    return &ffi_type_sint16;
        case HML_FFI_I32:    return &ffi_type_sint32;
        case HML_FFI_I64:    return &ffi_type_sint64;
        case HML_FFI_U8:     return &ffi_type_uint8;
        case HML_FFI_U16:    return &ffi_type_uint16;
        case HML_FFI_U32:    return &ffi_type_uint32;
        case HML_FFI_U64:    return &ffi_type_uint64;
        case HML_FFI_F32:    return &ffi_type_float;
        case HML_FFI_F64:    return &ffi_type_double;
        case HML_FFI_PTR:    return &ffi_type_pointer;
        case HML_FFI_STRING: return &ffi_type_pointer;
        default:
            hml_runtime_error("Unknown FFI type: %d", type);
    }
}

// Convert HmlValue to C value for FFI call
static void hml_value_to_ffi(HmlValue val, HmlFFIType type, void *out) {
    switch (type) {
        case HML_FFI_I8:     *(int8_t*)out = (int8_t)hml_to_i32(val); break;
        case HML_FFI_I16:    *(int16_t*)out = (int16_t)hml_to_i32(val); break;
        case HML_FFI_I32:    *(int32_t*)out = hml_to_i32(val); break;
        case HML_FFI_I64:    *(int64_t*)out = hml_to_i64(val); break;
        case HML_FFI_U8:     *(uint8_t*)out = (uint8_t)hml_to_i32(val); break;
        case HML_FFI_U16:    *(uint16_t*)out = (uint16_t)hml_to_i32(val); break;
        case HML_FFI_U32:    *(uint32_t*)out = (uint32_t)hml_to_i32(val); break;
        case HML_FFI_U64:    *(uint64_t*)out = (uint64_t)hml_to_i64(val); break;
        case HML_FFI_F32:    *(float*)out = (float)hml_to_f64(val); break;
        case HML_FFI_F64:    *(double*)out = hml_to_f64(val); break;
        case HML_FFI_PTR:
            if (val.type == HML_VAL_PTR) *(void**)out = val.as.as_ptr;
            else if (val.type == HML_VAL_BUFFER) *(void**)out = val.as.as_buffer->data;
            else *(void**)out = NULL;
            break;
        case HML_FFI_STRING:
            if (val.type == HML_VAL_STRING && val.as.as_string)
                *(char**)out = val.as.as_string->data;
            else
                *(char**)out = NULL;
            break;
        default:
            hml_runtime_error("Cannot convert to FFI type: %d", type);
    }
}

// Convert C value to HmlValue after FFI call
static HmlValue hml_ffi_to_value(void *result, HmlFFIType type) {
    switch (type) {
        case HML_FFI_VOID:   return hml_val_null();
        case HML_FFI_I8:     return hml_val_i32(*(int8_t*)result);
        case HML_FFI_I16:    return hml_val_i32(*(int16_t*)result);
        case HML_FFI_I32:    return hml_val_i32(*(int32_t*)result);
        case HML_FFI_I64:    return hml_val_i64(*(int64_t*)result);
        case HML_FFI_U8:     return hml_val_u8(*(uint8_t*)result);
        case HML_FFI_U16:    return hml_val_u16(*(uint16_t*)result);
        case HML_FFI_U32:    return hml_val_u32(*(uint32_t*)result);
        case HML_FFI_U64:    return hml_val_u64(*(uint64_t*)result);
        case HML_FFI_F32:    return hml_val_f32(*(float*)result);
        case HML_FFI_F64:    return hml_val_f64(*(double*)result);
        case HML_FFI_PTR:    return hml_val_ptr(*(void**)result);
        case HML_FFI_STRING: {
            char *s = *(char**)result;
            if (s) return hml_val_string(s);
            return hml_val_null();
        }
        default:
            hml_runtime_error("Cannot convert from FFI type: %d", type);
    }
}

HmlValue hml_ffi_call(void *func_ptr, HmlValue *args, int num_args, HmlFFIType *types) {
    if (!func_ptr) {
        hml_runtime_error("FFI call with null function pointer");
    }

    // types[0] is return type, types[1..] are arg types
    HmlFFIType return_type = types[0];

    // Prepare libffi call interface
    ffi_cif cif;
    ffi_type **arg_types = NULL;
    void **arg_values = NULL;
    void **arg_storage = NULL;

    if (num_args > 0) {
        arg_types = malloc(num_args * sizeof(ffi_type*));
        arg_values = malloc(num_args * sizeof(void*));
        arg_storage = malloc(num_args * sizeof(void*));

        for (int i = 0; i < num_args; i++) {
            arg_types[i] = hml_ffi_type_to_ffi(types[i + 1]);
            arg_storage[i] = malloc(8); // Max size for any arg
            hml_value_to_ffi(args[i], types[i + 1], arg_storage[i]);
            arg_values[i] = arg_storage[i];
        }
    }

    ffi_type *ret_type = hml_ffi_type_to_ffi(return_type);
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_args, ret_type, arg_types);

    if (status != FFI_OK) {
        hml_runtime_error("Failed to prepare FFI call");
    }

    // Call the function
    union {
        int8_t i8; int16_t i16; int32_t i32; int64_t i64;
        uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64;
        float f32; double f64;
        void *ptr;
    } result;

    ffi_call(&cif, func_ptr, &result, arg_values);

    // Convert result
    HmlValue ret = hml_ffi_to_value(&result, return_type);

    // Cleanup
    if (num_args > 0) {
        for (int i = 0; i < num_args; i++) {
            free(arg_storage[i]);
        }
        free(arg_types);
        free(arg_values);
        free(arg_storage);
    }

    return ret;
}

// ========== FFI CALLBACKS ==========

// Structure for FFI callback handle
struct HmlFFICallback {
    void *closure;           // ffi_closure (opaque)
    void *code_ptr;          // C-callable function pointer
    ffi_cif cif;             // Call interface
    ffi_type **arg_types;    // libffi argument types
    ffi_type *return_type;   // libffi return type
    HmlValue hemlock_fn;     // The Hemlock function to invoke
    HmlFFIType *param_types; // Hemlock parameter types
    HmlFFIType ret_type;     // Hemlock return type
    int num_params;
    int id;                  // Unique callback ID
};

// Global callback tracking
static HmlFFICallback **g_callbacks = NULL;
static int g_num_callbacks = 0;
static int g_callbacks_capacity = 0;
static int g_next_callback_id = 1;
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declaration
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args);

// Convert C value to HmlValue for callback arguments
static HmlValue hml_ffi_ptr_to_value(void *ptr, HmlFFIType type) {
    switch (type) {
        case HML_FFI_I8:     return hml_val_i32(*(int8_t*)ptr);
        case HML_FFI_I16:    return hml_val_i32(*(int16_t*)ptr);
        case HML_FFI_I32:    return hml_val_i32(*(int32_t*)ptr);
        case HML_FFI_I64:    return hml_val_i64(*(int64_t*)ptr);
        case HML_FFI_U8:     return hml_val_u32(*(uint8_t*)ptr);
        case HML_FFI_U16:    return hml_val_u32(*(uint16_t*)ptr);
        case HML_FFI_U32:    return hml_val_u32(*(uint32_t*)ptr);
        case HML_FFI_U64:    return hml_val_u64(*(uint64_t*)ptr);
        case HML_FFI_F32:    return hml_val_f64(*(float*)ptr);
        case HML_FFI_F64:    return hml_val_f64(*(double*)ptr);
        case HML_FFI_PTR:    return hml_val_ptr(*(void**)ptr);
        case HML_FFI_STRING: return hml_val_string(*(char**)ptr);
        default:             return hml_val_null();
    }
}

// Convert HmlValue to C storage for callback return
static void hml_value_to_ffi_storage(HmlValue val, HmlFFIType type, void *storage) {
    switch (type) {
        case HML_FFI_VOID:
            break;
        case HML_FFI_I8:
            *(int8_t*)storage = (int8_t)hml_to_i32(val);
            break;
        case HML_FFI_I16:
            *(int16_t*)storage = (int16_t)hml_to_i32(val);
            break;
        case HML_FFI_I32:
            *(int32_t*)storage = hml_to_i32(val);
            break;
        case HML_FFI_I64:
            *(int64_t*)storage = hml_to_i64(val);
            break;
        case HML_FFI_U8:
            *(uint8_t*)storage = (uint8_t)hml_to_i32(val);
            break;
        case HML_FFI_U16:
            *(uint16_t*)storage = (uint16_t)hml_to_i32(val);
            break;
        case HML_FFI_U32:
            *(uint32_t*)storage = (uint32_t)hml_to_i64(val);
            break;
        case HML_FFI_U64:
            *(uint64_t*)storage = (uint64_t)hml_to_i64(val);
            break;
        case HML_FFI_F32:
            *(float*)storage = (float)hml_to_f64(val);
            break;
        case HML_FFI_F64:
            *(double*)storage = hml_to_f64(val);
            break;
        case HML_FFI_PTR:
            *(void**)storage = (val.type == HML_VAL_PTR) ? val.as.as_ptr : NULL;
            break;
        case HML_FFI_STRING:
            *(char**)storage = (val.type == HML_VAL_STRING && val.as.as_string)
                              ? val.as.as_string->data : NULL;
            break;
    }
}

// Universal callback handler - this is called by libffi when C code invokes the callback
static void hml_ffi_callback_handler(ffi_cif *cif, void *ret, void **args, void *user_data) {
    (void)cif;  // Unused parameter
    HmlFFICallback *cb = (HmlFFICallback *)user_data;

    // Lock to ensure thread-safety
    pthread_mutex_lock(&g_callback_mutex);

    // Convert C arguments to Hemlock values
    HmlValue *hemlock_args = NULL;
    if (cb->num_params > 0) {
        hemlock_args = malloc(cb->num_params * sizeof(HmlValue));
        for (int i = 0; i < cb->num_params; i++) {
            hemlock_args[i] = hml_ffi_ptr_to_value(args[i], cb->param_types[i]);
        }
    }

    // Call the Hemlock function
    HmlValue result = hml_call_function(cb->hemlock_fn, hemlock_args, cb->num_params);

    // Handle return value
    if (cb->ret_type != HML_FFI_VOID) {
        hml_value_to_ffi_storage(result, cb->ret_type, ret);
    }

    // Cleanup
    hml_release(&result);
    if (hemlock_args) {
        for (int i = 0; i < cb->num_params; i++) {
            hml_release(&hemlock_args[i]);
        }
        free(hemlock_args);
    }

    pthread_mutex_unlock(&g_callback_mutex);
}

// Create a C-callable function pointer from a Hemlock function
HmlFFICallback* hml_ffi_callback_create(HmlValue fn, HmlFFIType *param_types, int num_params, HmlFFIType return_type) {
    if (fn.type != HML_VAL_FUNCTION || !fn.as.as_function) {
        hml_runtime_error("callback() requires a function");
    }

    HmlFFICallback *cb = malloc(sizeof(HmlFFICallback));
    if (!cb) {
        hml_runtime_error("Failed to allocate FFI callback");
    }

    cb->hemlock_fn = fn;
    hml_retain(&cb->hemlock_fn);
    cb->num_params = num_params;
    cb->ret_type = return_type;
    cb->id = g_next_callback_id++;

    // Copy parameter types
    cb->param_types = NULL;
    if (num_params > 0) {
        cb->param_types = malloc(sizeof(HmlFFIType) * num_params);
        for (int i = 0; i < num_params; i++) {
            cb->param_types[i] = param_types[i];
        }
    }

    // Build libffi type arrays
    cb->arg_types = NULL;
    if (num_params > 0) {
        cb->arg_types = malloc(sizeof(ffi_type*) * num_params);
        for (int i = 0; i < num_params; i++) {
            cb->arg_types[i] = hml_ffi_type_to_ffi(param_types[i]);
        }
    }

    cb->return_type = hml_ffi_type_to_ffi(return_type);

    // Prepare the CIF (Call Interface)
    ffi_status status = ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, num_params, cb->return_type, cb->arg_types);
    if (status != FFI_OK) {
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to prepare FFI callback interface");
    }

    // Allocate the closure
    void *code_ptr;
    ffi_closure *closure = ffi_closure_alloc(sizeof(ffi_closure), &code_ptr);
    if (!closure) {
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to allocate FFI closure");
    }

    cb->closure = closure;
    cb->code_ptr = code_ptr;

    // Prepare the closure with our handler
    status = ffi_prep_closure_loc(closure, &cb->cif, hml_ffi_callback_handler, cb, code_ptr);
    if (status != FFI_OK) {
        ffi_closure_free(closure);
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to prepare FFI closure");
    }

    // Track the callback
    pthread_mutex_lock(&g_callback_mutex);
    if (g_num_callbacks >= g_callbacks_capacity) {
        g_callbacks_capacity = g_callbacks_capacity == 0 ? 8 : g_callbacks_capacity * 2;
        g_callbacks = realloc(g_callbacks, sizeof(HmlFFICallback*) * g_callbacks_capacity);
    }
    g_callbacks[g_num_callbacks++] = cb;
    pthread_mutex_unlock(&g_callback_mutex);

    return cb;
}

// Get the C-callable function pointer from a callback handle
void* hml_ffi_callback_ptr(HmlFFICallback *cb) {
    return cb ? cb->code_ptr : NULL;
}

// Free a callback handle
void hml_ffi_callback_free(HmlFFICallback *cb) {
    if (!cb) return;

    // Remove from tracking list
    pthread_mutex_lock(&g_callback_mutex);
    for (int i = 0; i < g_num_callbacks; i++) {
        if (g_callbacks[i] == cb) {
            for (int j = i; j < g_num_callbacks - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_num_callbacks--;
            break;
        }
    }
    pthread_mutex_unlock(&g_callback_mutex);

    // Free the closure
    if (cb->closure) {
        ffi_closure_free(cb->closure);
    }

    // Release the Hemlock function
    hml_release(&cb->hemlock_fn);

    // Free type arrays
    free(cb->param_types);
    free(cb->arg_types);
    free(cb);
}

// Free a callback by its code pointer
int hml_ffi_callback_free_by_ptr(void *ptr) {
    if (!ptr) return 0;

    pthread_mutex_lock(&g_callback_mutex);
    for (int i = 0; i < g_num_callbacks; i++) {
        HmlFFICallback *cb = g_callbacks[i];
        if (cb && cb->code_ptr == ptr) {
            // Remove from list
            for (int j = i; j < g_num_callbacks - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_num_callbacks--;
            pthread_mutex_unlock(&g_callback_mutex);

            // Free the callback
            if (cb->closure) {
                ffi_closure_free(cb->closure);
            }
            hml_release(&cb->hemlock_fn);
            free(cb->param_types);
            free(cb->arg_types);
            free(cb);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_callback_mutex);
    return 0;
}

// Helper: convert type name string to HmlFFIType
static HmlFFIType hml_string_to_ffi_type(const char *name) {
    if (!name) return HML_FFI_VOID;
    if (strcmp(name, "void") == 0) return HML_FFI_VOID;
    if (strcmp(name, "i8") == 0) return HML_FFI_I8;
    if (strcmp(name, "i16") == 0) return HML_FFI_I16;
    if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) return HML_FFI_I32;
    if (strcmp(name, "i64") == 0) return HML_FFI_I64;
    if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return HML_FFI_U8;
    if (strcmp(name, "u16") == 0) return HML_FFI_U16;
    if (strcmp(name, "u32") == 0) return HML_FFI_U32;
    if (strcmp(name, "u64") == 0) return HML_FFI_U64;
    if (strcmp(name, "f32") == 0) return HML_FFI_F32;
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return HML_FFI_F64;
    if (strcmp(name, "ptr") == 0) return HML_FFI_PTR;
    if (strcmp(name, "string") == 0) return HML_FFI_STRING;
    return HML_FFI_I32; // Default
}

// Builtin wrapper: callback(fn, param_types, return_type) -> ptr
HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue param_types, HmlValue return_type) {
    (void)env;

    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("callback() first argument must be a function");
    }

    if (param_types.type != HML_VAL_ARRAY || !param_types.as.as_array) {
        hml_runtime_error("callback() second argument must be an array of type names");
    }

    HmlArray *params_arr = param_types.as.as_array;
    int num_params = params_arr->length;

    // Build parameter types
    HmlFFIType *types = NULL;
    if (num_params > 0) {
        types = malloc(sizeof(HmlFFIType) * num_params);
        for (int i = 0; i < num_params; i++) {
            HmlValue type_val = params_arr->elements[i];
            if (type_val.type != HML_VAL_STRING || !type_val.as.as_string) {
                free(types);
                hml_runtime_error("callback() param_types must contain type name strings");
            }
            types[i] = hml_string_to_ffi_type(type_val.as.as_string->data);
        }
    }

    // Get return type
    HmlFFIType ret_type = HML_FFI_VOID;
    if (return_type.type == HML_VAL_STRING && return_type.as.as_string) {
        ret_type = hml_string_to_ffi_type(return_type.as.as_string->data);
    }

    // Create the callback
    HmlFFICallback *cb = hml_ffi_callback_create(fn, types, num_params, ret_type);
    free(types);

    // Return the C-callable function pointer
    return hml_val_ptr(hml_ffi_callback_ptr(cb));
}

// Builtin wrapper: callback_free(ptr)
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("callback_free() argument must be a ptr");
    }

    int success = hml_ffi_callback_free_by_ptr(ptr.as.as_ptr);
    if (!success) {
        hml_runtime_error("callback_free(): pointer is not a valid callback");
    }

    return hml_val_null();
}

// Builtin: ptr_deref_i32(ptr) -> i32
HmlValue hml_builtin_ptr_deref_i32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i32() argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i32() cannot dereference null pointer");
    }

    return hml_val_i32(*(int32_t*)p);
}

// Builtin: ptr_write_i32(ptr, value)
HmlValue hml_builtin_ptr_write_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i32() first argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i32() cannot write to null pointer");
    }

    *(int32_t*)p = hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_offset(ptr, offset, element_size) -> ptr
HmlValue hml_builtin_ptr_offset(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue element_size) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_offset() first argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    int64_t off = hml_to_i64(offset);
    int64_t elem_size = hml_to_i64(element_size);

    char *base = (char*)p;
    return hml_val_ptr(base + (off * elem_size));
}

// Builtin: ptr_read_i32(ptr) -> i32  (dereference pointer-to-pointer, for qsort)
HmlValue hml_builtin_ptr_read_i32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_read_i32() argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_read_i32() cannot read from null pointer");
    }

    // Read through pointer-to-pointer (qsort passes ptr to element)
    int32_t *actual_ptr = *(int32_t**)p;
    return hml_val_i32(*actual_ptr);
}

// ========== COMPRESSION OPERATIONS ==========

#ifdef HML_HAVE_ZLIB

// zlib_compress(data: string, level: i32) -> buffer
HmlValue hml_zlib_compress(HmlValue data, HmlValue level_val) {
    if (data.type != HML_VAL_STRING || !data.as.as_string) {
        hml_runtime_error("zlib_compress() first argument must be string");
    }

    int level = (int)level_val.as.as_i32;
    if (level < -1 || level > 9) {
        hml_runtime_error("zlib_compress() level must be -1 to 9");
    }

    HmlString *str = data.as.as_string;

    // Handle empty input
    if (str->length == 0) {
        HmlValue buf = hml_val_buffer(1);
        buf.as.as_buffer->length = 0;
        return buf;
    }

    // Calculate maximum compressed size
    uLong source_len = str->length;
    uLong dest_len = compressBound(source_len);

    // Allocate destination buffer
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        hml_runtime_error("zlib_compress() memory allocation failed");
    }

    // Compress
    int result = compress2(dest, &dest_len, (const Bytef *)str->data, source_len, level);

    if (result != Z_OK) {
        free(dest);
        hml_runtime_error("zlib_compress() compression failed");
    }

    // Create buffer with compressed data
    HmlValue buf = hml_val_buffer((int32_t)dest_len);
    memcpy(buf.as.as_buffer->data, dest, dest_len);
    free(dest);

    return buf;
}

// zlib_decompress(data: buffer, max_size: i64) -> string
HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size_val) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("zlib_decompress() first argument must be buffer");
    }

    size_t max_size = (size_t)max_size_val.as.as_i64;
    HmlBuffer *buf = data.as.as_buffer;

    // Handle empty input
    if (buf->length == 0) {
        return hml_val_string("");
    }

    // Allocate destination buffer
    uLong dest_len = max_size;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        hml_runtime_error("zlib_decompress() memory allocation failed");
    }

    // Decompress
    int result = uncompress(dest, &dest_len, (const Bytef *)buf->data, buf->length);

    if (result != Z_OK) {
        free(dest);
        hml_runtime_error("zlib_decompress() decompression failed");
    }

    // Create string from decompressed data
    char *result_str = malloc(dest_len + 1);
    if (!result_str) {
        free(dest);
        hml_runtime_error("zlib_decompress() memory allocation failed");
    }
    memcpy(result_str, dest, dest_len);
    result_str[dest_len] = '\0';
    free(dest);

    HmlValue ret = hml_val_string(result_str);
    free(result_str);
    return ret;
}

// gzip_compress(data: string, level: i32) -> buffer
HmlValue hml_gzip_compress(HmlValue data, HmlValue level_val) {
    if (data.type != HML_VAL_STRING || !data.as.as_string) {
        hml_runtime_error("gzip_compress() first argument must be string");
    }

    int level = (int)level_val.as.as_i32;
    if (level < -1 || level > 9) {
        hml_runtime_error("gzip_compress() level must be -1 to 9");
    }

    HmlString *str = data.as.as_string;

    // Handle empty input - gzip still produces header/trailer
    if (str->length == 0) {
        unsigned char empty_gzip[] = {
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        HmlValue buf = hml_val_buffer(sizeof(empty_gzip));
        memcpy(buf.as.as_buffer->data, empty_gzip, sizeof(empty_gzip));
        return buf;
    }

    // Initialize z_stream for gzip compression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        hml_runtime_error("gzip_compress() initialization failed");
    }

    // Calculate output buffer size
    uLong dest_len = compressBound(str->length) + 18;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        deflateEnd(&strm);
        hml_runtime_error("gzip_compress() memory allocation failed");
    }

    // Set input/output
    strm.next_in = (Bytef *)str->data;
    strm.avail_in = str->length;
    strm.next_out = dest;
    strm.avail_out = dest_len;

    // Compress all at once
    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        free(dest);
        deflateEnd(&strm);
        hml_runtime_error("gzip_compress() compression failed");
    }

    size_t output_len = strm.total_out;
    deflateEnd(&strm);

    HmlValue buf = hml_val_buffer((int32_t)output_len);
    memcpy(buf.as.as_buffer->data, dest, output_len);
    free(dest);

    return buf;
}

// gzip_decompress(data: buffer, max_size: i64) -> string
HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size_val) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("gzip_decompress() first argument must be buffer");
    }

    size_t max_size = (size_t)max_size_val.as.as_i64;
    HmlBuffer *buf = data.as.as_buffer;

    // Handle empty input
    if (buf->length == 0) {
        hml_runtime_error("gzip_decompress() requires non-empty input");
    }

    // Verify gzip magic bytes
    unsigned char *buf_data = (unsigned char *)buf->data;
    if (buf->length < 10 || buf_data[0] != 0x1f || buf_data[1] != 0x8b) {
        hml_runtime_error("gzip_decompress() invalid gzip data");
    }

    // Initialize z_stream for gzip decompression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        hml_runtime_error("gzip_decompress() initialization failed");
    }

    // Allocate destination buffer
    Bytef *dest = malloc(max_size);
    if (!dest) {
        inflateEnd(&strm);
        hml_runtime_error("gzip_decompress() memory allocation failed");
    }

    // Set input/output
    strm.next_in = (Bytef *)buf->data;
    strm.avail_in = buf->length;
    strm.next_out = dest;
    strm.avail_out = max_size;

    // Decompress
    ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        free(dest);
        inflateEnd(&strm);
        hml_runtime_error("gzip_decompress() decompression failed");
    }

    size_t output_len = strm.total_out;
    inflateEnd(&strm);

    // Create string from decompressed data
    char *result_str = malloc(output_len + 1);
    if (!result_str) {
        free(dest);
        hml_runtime_error("gzip_decompress() memory allocation failed");
    }
    memcpy(result_str, dest, output_len);
    result_str[output_len] = '\0';
    free(dest);

    HmlValue result = hml_val_string(result_str);
    free(result_str);
    return result;
}

// zlib_compress_bound(source_len: i64) -> i64
HmlValue hml_zlib_compress_bound(HmlValue source_len_val) {
    uLong source_len = (uLong)source_len_val.as.as_i64;
    uLong bound = compressBound(source_len);
    return hml_val_i64((int64_t)bound);
}

// crc32(data: buffer) -> u32
HmlValue hml_crc32_val(HmlValue data) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("crc32() argument must be buffer");
    }

    HmlBuffer *buf = data.as.as_buffer;
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)buf->data, buf->length);

    return hml_val_u32((uint32_t)crc);
}

// adler32(data: buffer) -> u32
HmlValue hml_adler32_val(HmlValue data) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("adler32() argument must be buffer");
    }

    HmlBuffer *buf = data.as.as_buffer;
    uLong adler = adler32(0L, Z_NULL, 0);
    adler = adler32(adler, (const Bytef *)buf->data, buf->length);

    return hml_val_u32((uint32_t)adler);
}

// Compression builtin wrappers
HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_zlib_compress(data, level);
}

HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_zlib_decompress(data, max_size);
}

HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_gzip_compress(data, level);
}

HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_gzip_decompress(data, max_size);
}

HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len) {
    (void)env;
    return hml_zlib_compress_bound(source_len);
}

HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_crc32_val(data);
}

HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_adler32_val(data);
}

#else /* !HML_HAVE_ZLIB */

// Stub implementations when zlib is not available
HmlValue hml_zlib_compress(HmlValue data, HmlValue level_val) {
    (void)data; (void)level_val;
    hml_runtime_error("zlib_compress() not available - zlib not installed");
}

HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size_val) {
    (void)data; (void)max_size_val;
    hml_runtime_error("zlib_decompress() not available - zlib not installed");
}

HmlValue hml_gzip_compress(HmlValue data, HmlValue level_val) {
    (void)data; (void)level_val;
    hml_runtime_error("gzip_compress() not available - zlib not installed");
}

HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size_val) {
    (void)data; (void)max_size_val;
    hml_runtime_error("gzip_decompress() not available - zlib not installed");
}

HmlValue hml_zlib_compress_bound(HmlValue source_len_val) {
    (void)source_len_val;
    hml_runtime_error("zlib_compress_bound() not available - zlib not installed");
}

HmlValue hml_crc32_val(HmlValue data) {
    (void)data;
    hml_runtime_error("crc32() not available - zlib not installed");
}

HmlValue hml_adler32_val(HmlValue data) {
    (void)data;
    hml_runtime_error("adler32() not available - zlib not installed");
}

HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_zlib_compress(data, level);
}

HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_zlib_decompress(data, max_size);
}

HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_gzip_compress(data, level);
}

HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_gzip_decompress(data, max_size);
}

HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len) {
    (void)env;
    return hml_zlib_compress_bound(source_len);
}

HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_crc32_val(data);
}

HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_adler32_val(data);
}

#endif /* HML_HAVE_ZLIB */

// ========== CRYPTOGRAPHIC HASH FUNCTIONS (OpenSSL) ==========

// Helper: Convert bytes to hexadecimal string
static HmlValue bytes_to_hex_string(const unsigned char *bytes, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    char *hex = malloc(len * 2 + 1);
    if (!hex) return hml_val_string("");

    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';

    HmlValue result = hml_val_string(hex);
    free(hex);
    return result;
}

// SHA-256 hash - returns hex string
HmlValue hml_hash_sha256(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha256() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, SHA256_DIGEST_LENGTH);
}

// SHA-512 hash - returns hex string
HmlValue hml_hash_sha512(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha512() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, SHA512_DIGEST_LENGTH);
}

// MD5 hash - returns hex string
HmlValue hml_hash_md5(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("md5() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, MD5_DIGEST_LENGTH);
}

// Builtin wrappers for function-as-value usage
HmlValue hml_builtin_hash_sha256(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_sha256(input);
}

HmlValue hml_builtin_hash_sha512(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_sha512(input);
}

HmlValue hml_builtin_hash_md5(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_md5(input);
}

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

// Wrapper functions for internal helpers
HmlValue hml_builtin_read_u32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u32(ptr);
}

HmlValue hml_builtin_read_u64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_read_u64(ptr);
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

// ========== DNS/NETWORKING OPERATIONS ==========

#include <netdb.h>
#include <arpa/inet.h>

// Resolve a hostname to IP address string
HmlValue hml_dns_resolve(HmlValue hostname_val) {
    if (hostname_val.type != HML_VAL_STRING) {
        hml_runtime_error("dns_resolve() requires a string hostname");
    }

    HmlString *str = hostname_val.as.as_string;
    // Create null-terminated string
    char *hostname = malloc(str->length + 1);
    if (!hostname) {
        hml_runtime_error("dns_resolve() memory allocation failed");
    }
    memcpy(hostname, str->data, str->length);
    hostname[str->length] = '\0';

    struct hostent *host = gethostbyname(hostname);
    free(hostname);

    if (!host) {
        hml_runtime_error("Failed to resolve hostname");
    }

    // Return first IPv4 address
    char *ip = inet_ntoa(*(struct in_addr *)host->h_addr_list[0]);
    return hml_val_string(ip);
}

// Wrapper function for dns_resolve
HmlValue hml_builtin_dns_resolve(HmlClosureEnv *env, HmlValue hostname) {
    (void)env;
    return hml_dns_resolve(hostname);
}

// ========== SOCKET BUILTIN WRAPPERS ==========

HmlValue hml_builtin_socket_create(HmlClosureEnv *env, HmlValue domain, HmlValue sock_type, HmlValue protocol) {
    (void)env;
    return hml_socket_create(domain, sock_type, protocol);
}

HmlValue hml_builtin_socket_bind(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port) {
    (void)env;
    hml_socket_bind(socket_val, address, port);
    return hml_val_null();
}

HmlValue hml_builtin_socket_listen(HmlClosureEnv *env, HmlValue socket_val, HmlValue backlog) {
    (void)env;
    hml_socket_listen(socket_val, backlog);
    return hml_val_null();
}

HmlValue hml_builtin_socket_accept(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_accept(socket_val);
}

HmlValue hml_builtin_socket_connect(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port) {
    (void)env;
    hml_socket_connect(socket_val, address, port);
    return hml_val_null();
}

HmlValue hml_builtin_socket_close(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    hml_socket_close(socket_val);
    return hml_val_null();
}

HmlValue hml_builtin_socket_send(HmlClosureEnv *env, HmlValue socket_val, HmlValue data) {
    (void)env;
    return hml_socket_send(socket_val, data);
}

HmlValue hml_builtin_socket_recv(HmlClosureEnv *env, HmlValue socket_val, HmlValue size) {
    (void)env;
    return hml_socket_recv(socket_val, size);
}

HmlValue hml_builtin_socket_sendto(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data) {
    (void)env;
    return hml_socket_sendto(socket_val, address, port, data);
}

HmlValue hml_builtin_socket_recvfrom(HmlClosureEnv *env, HmlValue socket_val, HmlValue size) {
    (void)env;
    return hml_socket_recvfrom(socket_val, size);
}

HmlValue hml_builtin_socket_setsockopt(HmlClosureEnv *env, HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value) {
    (void)env;
    hml_socket_setsockopt(socket_val, level, option, value);
    return hml_val_null();
}

HmlValue hml_builtin_socket_get_fd(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_fd(socket_val);
}

HmlValue hml_builtin_socket_get_address(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_address(socket_val);
}

HmlValue hml_builtin_socket_get_port(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_port(socket_val);
}

HmlValue hml_builtin_socket_get_closed(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_closed(socket_val);
}

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
    strcpy(path, "/");

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
        connect_info.ssl_connection |= LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
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
        connect_info.ssl_connection |= LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
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

// Get response headers (not implemented yet)
HmlValue hml_lws_response_headers(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_string("");
}

// Free response
HmlValue hml_lws_response_free(HmlValue resp_val) {
    if (resp_val.type == HML_VAL_PTR) {
        hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
        if (resp) {
            if (resp->body) free(resp->body);
            if (resp->redirect_url) free(resp->redirect_url);
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
    strcpy(path, "/");

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
        connect_info.ssl_connection = LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
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

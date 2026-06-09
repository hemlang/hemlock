/*
 * Hemlock Compiler (hemlockc)
 *
 * Compiles Hemlock source code to C, then optionally invokes
 * the C compiler to produce an executable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include "frontend.h"
#include "shared/file_io.h"
#include "../../include/version.h"
#include "../../include/hemlock_limits.h"
#include "codegen.h"
#include "compiler/type_check.h"

#define HEMLOCK_BUILD_DATE __DATE__
#define HEMLOCK_BUILD_TIME __TIME__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>

// Well-known Homebrew paths by architecture
// Apple Silicon (arm64): /opt/homebrew/opt/<package>
// Intel (x86_64): /usr/local/opt/<package>
static const char* get_homebrew_prefix(void) {
#if defined(__arm64__) || defined(__aarch64__)
    return "/opt/homebrew/opt";
#else
    return "/usr/local/opt";
#endif
}

// Get library path using: 1) env var, 2) well-known path, 3) brew --prefix (slow fallback)
// Returns path in static buffer, or empty string if not found
static const char* get_macos_lib_path(const char *env_var, const char *package_name) {
    static char path_buf[PATH_MAX];

    // 1. Check environment variable override (fastest)
    const char *env_path = getenv(env_var);
    if (env_path && env_path[0]) {
        snprintf(path_buf, sizeof(path_buf), "%s", env_path);
        // Verify path exists
        if (access(path_buf, R_OK) == 0) {
            return path_buf;
        }
    }

    // 2. Try well-known Homebrew path (fast - just stat)
    snprintf(path_buf, sizeof(path_buf), "%s/%s", get_homebrew_prefix(), package_name);
    if (access(path_buf, R_OK) == 0) {
        return path_buf;
    }

    // 3. Fallback to brew --prefix (slow - spawns Ruby)
    char brew_cmd[256];
    snprintf(brew_cmd, sizeof(brew_cmd), "brew --prefix %s 2>/dev/null", package_name);
    FILE *fp = popen(brew_cmd, "r");
    if (fp) {
        if (fgets(path_buf, sizeof(path_buf), fp)) {
            path_buf[strcspn(path_buf, "\n")] = 0;
            pclose(fp);
            if (access(path_buf, R_OK) == 0) {
                return path_buf;
            }
        } else {
            pclose(fp);
        }
    }

    path_buf[0] = '\0';
    return path_buf;
}
#endif

// Get directory containing the hemlockc executable (cross-platform)
static char* get_self_dir(void) {
    static char path[PATH_MAX];

#ifdef __APPLE__
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return NULL;
    }
    // Resolve symlinks
    char *real = realpath(path, NULL);
    if (real) {
        strncpy(path, real, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        free(real);
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) {
        return NULL;
    }
    path[len] = '\0';
#else
    // Fallback: try /proc/self/exe anyway
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) {
        return NULL;
    }
    path[len] = '\0';
#endif

    // Find last slash and truncate to get directory
    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    return path;
}

static const char* get_temp_dir(void) {
    const char *env_dir = NULL;
    const char *env_vars[] = {"TMPDIR", "TEMP", "TMP"};

    for (size_t i = 0; i < sizeof(env_vars) / sizeof(env_vars[0]); i++) {
        env_dir = getenv(env_vars[i]);
        if (env_dir && env_dir[0] && access(env_dir, W_OK) == 0) {
            return env_dir;
        }
    }

#ifdef P_tmpdir
    if (access(P_tmpdir, W_OK) == 0) {
        return P_tmpdir;
    }
#endif

    return "/tmp";
}

static char* make_temp_c_filename(void) {
    const char *temp_dir = get_temp_dir();
    char template_path[PATH_MAX];
    size_t dir_len = strlen(temp_dir);
    const char *slash = (dir_len > 0 && temp_dir[dir_len - 1] == '/') ? "" : "/";

    if (snprintf(template_path, sizeof(template_path),
                 "%s%shemlock_XXXXXX.c", temp_dir, slash) >= (int)sizeof(template_path)) {
        return NULL;
    }

    int fd = mkstemps(template_path, 2);
    if (fd < 0) {
        return NULL;
    }
    close(fd);

    return strdup(template_path);
}

// Standard install locations for runtime library
#ifndef HEMLOCK_LIBDIR
#define HEMLOCK_LIBDIR "/usr/local/lib/hemlock"
#endif

// Find the runtime library, checking multiple locations
// Returns a static buffer with the path, or NULL if not found
static const char* find_runtime_path(void) {
    static char found_path[PATH_MAX];
    char check_path[PATH_MAX];

    // List of directories to check (in order of priority)
    const char *search_dirs[5];
    int num_dirs = 0;

    // 1. Directory containing hemlockc (for development builds)
    const char *self_dir = get_self_dir();
    char sibling_libdir[PATH_MAX] = {0};
    if (self_dir) {
        search_dirs[num_dirs++] = self_dir;

        // 2. install.sh layout: <prefix>/bin/hemlockc + <prefix>/lib/hemlock/
        int written = snprintf(sibling_libdir, sizeof(sibling_libdir),
                               "%s/../lib/hemlock", self_dir);
        if (written >= 0 && (size_t)written < sizeof(sibling_libdir)) {
            search_dirs[num_dirs++] = sibling_libdir;
        }
    }

    // 3. Standard install location
    search_dirs[num_dirs++] = HEMLOCK_LIBDIR;

    // 4. Current directory (fallback)
    search_dirs[num_dirs++] = ".";

    // Check each directory for libhemlock_runtime.a
    for (int i = 0; i < num_dirs; i++) {
        snprintf(check_path, sizeof(check_path), "%s/libhemlock_runtime.a", search_dirs[i]);
        if (access(check_path, R_OK) == 0) {
            char *resolved = realpath(search_dirs[i], NULL);
            if (resolved) {
                strncpy(found_path, resolved, sizeof(found_path) - 1);
                found_path[sizeof(found_path) - 1] = '\0';
                free(resolved);
            } else {
                strncpy(found_path, search_dirs[i], sizeof(found_path) - 1);
                found_path[sizeof(found_path) - 1] = '\0';
            }
            return found_path;
        }
    }

    // Not found - return self_dir anyway (will fail at link time with clear error)
    if (self_dir) {
        return self_dir;
    }
    return ".";
}

// Command-line options
typedef struct {
    const char *input_file;
    const char *output_file;
    const char *c_output;        // C source output (for -c option)
    int emit_c_only;             // Only emit C, don't compile
    int verbose;                 // Verbose output
    int keep_c;                  // Keep generated C file
    int optimize;                // Optimization level (0, 1, 2, 3)
    const char *cc;              // C compiler to use
    const char *runtime_path;    // Path to runtime library
    int type_check;              // Enable compile-time type checking (default: on)
    int strict_types;            // Enable strict type checking (warn on implicit any)
    int check_only;              // Only type check, don't compile
    int static_link;             // Static link all libraries for standalone binary
    int stack_check;             // Enable stack overflow checking (default: on)
    int sandbox;                 // Enable sandbox mode (restrict FFI, network, process, file writes)
    const char *sandbox_root;    // Optional sandbox root directory for file access
    int target_wasm;             // Target WebAssembly via Emscripten (--target wasm)
    int wasm_threads;            // Enable pthreads in WASM (Web Workers + SharedArrayBuffer)
} Options;

static void print_usage(const char *progname) {
    fprintf(stderr, "Hemlock Compiler v%s\n\n", HEMLOCK_VERSION);
    fprintf(stderr, "Usage: %s [options] <input.hml>\n\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>       Output executable name (default: a.out)\n");
    fprintf(stderr, "  -c              Emit C code only (don't compile)\n");
    fprintf(stderr, "  --emit-c <f>    Write generated C to file\n");
    fprintf(stderr, "  -k, --keep-c    Keep generated C file after compilation\n");
    fprintf(stderr, "  -O<level>       Optimization level (0-3, default: 3)\n");
#ifdef __APPLE__
    fprintf(stderr, "  --cc <path>     C compiler to use (default: clang)\n");
#else
    fprintf(stderr, "  --cc <path>     C compiler to use (default: gcc)\n");
#endif
    fprintf(stderr, "  --runtime <p>   Path to runtime library\n");
    fprintf(stderr, "  --check         Type check only, don't compile\n");
    fprintf(stderr, "  --no-type-check Disable type checking (less safe, fewer optimizations)\n");
    fprintf(stderr, "  --strict-types  Strict type checking (warn on implicit any)\n");
    fprintf(stderr, "  --no-stack-check  Disable stack overflow checking (faster, but no protection)\n");
    fprintf(stderr, "  --static        Static-link the volatile native libs (default on Linux)\n");
    fprintf(stderr, "  --dynamic       Dynamic-link instead (default on macOS; needed for runtime FFI)\n");
    fprintf(stderr, "  --sandbox [DIR] Enable sandbox mode (restrict FFI, network, process, file writes)\n");
    fprintf(stderr, "                  If DIR provided, restricts file reads to that directory\n");
    fprintf(stderr, "  --target wasm   Compile for WebAssembly via Emscripten (emcc)\n");
    fprintf(stderr, "  --threads       Enable threading in WASM (Web Workers + SharedArrayBuffer)\n");
    fprintf(stderr, "                  Requires: --target wasm, browser COOP/COEP headers\n");
    fprintf(stderr, "  -v, --verbose   Verbose output\n");
    fprintf(stderr, "  -h, --help      Show this help message\n");
    fprintf(stderr, "  --version       Show version\n");
}

static Options parse_args(int argc, char **argv) {
    Options opts = {
        .input_file = NULL,
        .output_file = "a.out",
        .c_output = NULL,
        .emit_c_only = 0,
        .verbose = 0,
        .keep_c = 0,
        .optimize = 3,           // Default to -O3 for best performance
#ifdef __APPLE__
        .cc = "clang",           // Use clang on macOS (better ARM64 optimization)
#else
        .cc = "gcc",
#endif
        .runtime_path = NULL,
        .type_check = 1,         // Type checking ON by default
        .strict_types = 0,
        .check_only = 0,
        .static_link = 0,
        .stack_check = 1,        // Stack overflow checking ON by default
        .sandbox = 0,
        .sandbox_root = NULL,
        .target_wasm = 0,
        .wasm_threads = 0
    };

    // Static-by-default on Linux (since 2.6.0): the host's shared libraries —
    // libwebsockets above all — are not ABI-stable across distro versions
    // (its lws_context_creation_info struct reordered fields between 4.0 and
    // 4.3), so a dynamically-linked binary built on one distro crashes on
    // another. Static-linking the volatile native libs makes binaries
    // self-contained and portable. macOS stays dynamic: its system libraries
    // are versioned consistently, brew static archives are awkward, and the
    // user explicitly accepts dynamic there. `--dynamic` opts out (required
    // for runtime FFI: ffi_open/ffi_bind).
#ifndef __APPLE__
    opts.static_link = 1;
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("hemlockc %s (built %s %s)\n", HEMLOCK_VERSION, HEMLOCK_BUILD_DATE, HEMLOCK_BUILD_TIME);
            exit(0);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.output_file = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            opts.emit_c_only = 1;
        } else if (strcmp(argv[i], "--emit-c") == 0 && i + 1 < argc) {
            opts.c_output = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep-c") == 0) {
            opts.keep_c = 1;
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opts.optimize = (int)strtol(argv[i] + 2, NULL, 10);
            if (opts.optimize < 0) opts.optimize = 0;
            if (opts.optimize > 3) opts.optimize = 3;
        } else if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) {
            opts.cc = argv[++i];
        } else if (strcmp(argv[i], "--runtime") == 0 && i + 1 < argc) {
            opts.runtime_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = 1;
        } else if (strcmp(argv[i], "--check") == 0) {
            opts.check_only = 1;
            opts.type_check = 1;  // --check implies type checking
        } else if (strcmp(argv[i], "--no-type-check") == 0) {
            opts.type_check = 0;
        } else if (strcmp(argv[i], "--strict-types") == 0) {
            opts.type_check = 1;  // Implies type checking
            opts.strict_types = 1;
        } else if (strcmp(argv[i], "--static") == 0) {
            opts.static_link = 1;   // default since 2.6.0; kept as an explicit no-op
        } else if (strcmp(argv[i], "--dynamic") == 0) {
            // Opt out of the 2.6.0 static-by-default linking (e.g. when a
            // program needs runtime FFI, ffi_open/ffi_bind, which static
            // linking can't support). On Linux this restores dynamic linking
            // of libwebsockets/openssl/etc.
            opts.static_link = 0;
        } else if (strcmp(argv[i], "--no-stack-check") == 0) {
            opts.stack_check = 0;
        } else if (strcmp(argv[i], "--sandbox") == 0) {
            opts.sandbox = 1;
            // Check if next argument is an optional directory (not a flag and not a .hml file)
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *next = argv[i + 1];
                size_t len = strlen(next);
                // If it doesn't end with .hml, treat it as sandbox root
                if (len < 4 || strcmp(next + len - 4, ".hml") != 0) {
                    opts.sandbox_root = next;
                    i++;  // Skip the directory argument
                }
            }
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "wasm") == 0) {
                opts.target_wasm = 1;
                opts.cc = "emcc";  // Use Emscripten compiler
            } else {
                fprintf(stderr, "Unknown target: %s (supported: wasm)\n", argv[i]);
                exit(1);
            }
        } else if (strcmp(argv[i], "--threads") == 0) {
            opts.wasm_threads = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        } else {
            if (opts.input_file != NULL) {
                fprintf(stderr, "Multiple input files not supported\n");
                exit(1);
            }
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file == NULL) {
        fprintf(stderr, "No input file specified\n");
        print_usage(argv[0]);
        exit(1);
    }

    // Validate --threads requires --target wasm
    if (opts.wasm_threads && !opts.target_wasm) {
        fprintf(stderr, "Error: --threads requires --target wasm\n");
        exit(1);
    }

    return opts;
}

// Read entire file into a string
// read_file() provided by shared/file_io.h

// Generate C output filename from input filename
static char* make_c_filename(const char *input) {
    // Find the last '/' or use start of string
    const char *base = strrchr(input, '/');
    base = base ? base + 1 : input;

    // Find .hml extension
    const char *ext = strstr(base, ".hml");

    char *result;
    if (ext) {
        size_t base_len = (size_t)(ext - base);
        size_t alloc_size = base_len + 3;  // base + ".c" + null
        result = malloc(alloc_size);
        if (!result) {
            fprintf(stderr, "error: Failed to allocate output filename\n");
            return NULL;
        }
        // Safe: use memcpy + explicit null termination instead of strncpy/strcat
        memcpy(result, base, base_len);
        memcpy(result + base_len, ".c", 3);  // includes null terminator
    } else {
        size_t len = strlen(base) + 3;
        result = malloc(len);
        if (!result) {
            fprintf(stderr, "error: Failed to allocate output filename\n");
            return NULL;
        }
        snprintf(result, len, "%s.c", base);
    }

    return result;
}

// Quote a string for safe inclusion in a shell command line: wrap in single
// quotes and escape embedded single quotes as '\''. Prevents filenames or
// paths containing shell metacharacters from being interpreted by system().
// Returns a malloc'd string, or NULL on allocation failure.
static char* shell_quote(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len * 4 + 3);  // worst case: every char is a quote
    if (!out) {
        return NULL;
    }
    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = s[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

// Invoke the C compiler
static int compile_c(const Options *opts, const char *c_file) {
    // Build command
    char cmd[4096];
    char opt_flag[4];
    snprintf(opt_flag, sizeof(opt_flag), "-O%d", opts->optimize);

    // Determine runtime path
    // Priority: --runtime flag > auto-detect (self dir, install dir, cwd)
    const char *runtime_path = opts->runtime_path;
    if (!runtime_path) {
        runtime_path = find_runtime_path();
    }

    // Build link command
    // Check if -lz is linkable (same check as runtime Makefile)
    char zlib_flag[8] = "";
    if (system("echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null") == 0) {
        memcpy(zlib_flag, " -lz", 5);  // Safe: 5 bytes including null, fits in 8-byte buffer
    }

    // Platform-specific library paths
    // On macOS, use well-known Homebrew paths (fast) instead of `brew --prefix` (slow)
    // Override with env vars: HEMLOCK_LIBFFI_PATH, HEMLOCK_LWS_PATH, HEMLOCK_OPENSSL_PATH
    char extra_lib_paths[512] = "";
#ifdef __APPLE__
    size_t extra_lib_len = 0;  // SECURITY: Track length to prevent overflow

    // Get libffi path (fast: uses well-known paths, falls back to brew only if needed)
    // Paths are shell-quoted: they come from env vars / brew output and are
    // later embedded in system() command lines.
    const char *libffi_path = get_macos_lib_path("HEMLOCK_LIBFFI_PATH", "libffi");
    if (libffi_path[0]) {
        char tmp[640];
        char *q = shell_quote(libffi_path);
        int n = q ? snprintf(tmp, sizeof(tmp), " -L%s/lib", q) : -1;
        free(q);
        if (n > 0 && (size_t)n < sizeof(tmp) &&
            extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
            strcat(extra_lib_paths + extra_lib_len, tmp);
            extra_lib_len += (size_t)n;
        }
    }

    // Get libwebsockets path
    const char *lws_path = get_macos_lib_path("HEMLOCK_LWS_PATH", "libwebsockets");
    if (lws_path[0]) {
        char tmp[640];
        char *q = shell_quote(lws_path);
        int n = q ? snprintf(tmp, sizeof(tmp), " -L%s/lib", q) : -1;
        free(q);
        if (n > 0 && (size_t)n < sizeof(tmp) &&
            extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
            strcat(extra_lib_paths + extra_lib_len, tmp);
            extra_lib_len += (size_t)n;
        }
    }

    // Get OpenSSL path
    const char *ssl_path = get_macos_lib_path("HEMLOCK_OPENSSL_PATH", "openssl@3");
    if (ssl_path[0]) {
        char tmp[640];
        char *q = shell_quote(ssl_path);
        int n = q ? snprintf(tmp, sizeof(tmp), " -L%s/lib", q) : -1;
        free(q);
        if (n > 0 && (size_t)n < sizeof(tmp) &&
            extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
            strcat(extra_lib_paths + extra_lib_len, tmp);
        }
    }
#endif

    // Check if -lwebsockets is linkable (with extra paths)
    char websockets_flag[16] = "";
    char ws_test_cmd[1024];
    snprintf(ws_test_cmd, sizeof(ws_test_cmd),
        "echo 'int main(){return 0;}' | gcc -x c - %s -lwebsockets -o /dev/null 2>/dev/null",
        extra_lib_paths);
    if (system(ws_test_cmd) == 0) {
        memcpy(websockets_flag, " -lwebsockets", 14);  // Safe: 14 bytes including null, fits in 16-byte buffer
    }

    // OpenSSL/libcrypto is required - the runtime links against it for hash functions
    // The runtime always needs libcrypto, so always link it
    // On Linux, use --no-as-needed to ensure the library is linked even if not directly referenced
#ifdef __APPLE__
    char crypto_flag[64] = " -lcrypto";
#else
    char crypto_flag[64] = " -Wl,--no-as-needed -lcrypto";
#endif

    // Determine include path - check for both development and installed layouts
    // Development: runtime_path/runtime/include
    // Installed: runtime_path/include
    char include_path[PATH_MAX];
    char dev_include[PATH_MAX];
    char install_include[PATH_MAX];
    int n1 = snprintf(dev_include, sizeof(dev_include), "%s/runtime/include", runtime_path);
    int n2 = snprintf(install_include, sizeof(install_include), "%s/include", runtime_path);
    if (n1 >= (int)sizeof(dev_include) || n2 >= (int)sizeof(install_include)) {
        fprintf(stderr, "Warning: runtime path too long, may be truncated\n");
    }

    if (access(dev_include, R_OK) == 0) {
        strncpy(include_path, dev_include, sizeof(include_path) - 1);
    } else {
        strncpy(include_path, install_include, sizeof(include_path) - 1);
    }
    include_path[sizeof(include_path) - 1] = '\0';

    // Shell-quote user-controlled paths before embedding them in the
    // system() command line: a filename like `foo$(cmd).hml` or an -o
    // argument containing shell metacharacters must not execute commands.
    char runtime_lib[PATH_MAX + 32];
    snprintf(runtime_lib, sizeof(runtime_lib), "%s/libhemlock_runtime.a", runtime_path);
    char *q_c = shell_quote(c_file);
    char *q_inc = shell_quote(include_path);
    char *q_runtime_lib = shell_quote(runtime_lib);
    char *q_out = NULL;  // set per-branch (WASM may rewrite the output name)
    char *q_wasm_runtime = NULL;
    if (!q_c || !q_inc || !q_runtime_lib) {
        free(q_c);
        free(q_inc);
        free(q_runtime_lib);
        fprintf(stderr, "error: Out of memory building compiler command\n");
        return 1;
    }

    // Build the linker command
    int n;
    if (opts->target_wasm) {
        // WASM build via Emscripten
        // Determine WASM runtime library path
        char wasm_runtime[PATH_MAX + 64];
        if (opts->wasm_threads) {
            // Threaded build uses separate runtime with pthread support
            snprintf(wasm_runtime, sizeof(wasm_runtime),
                     "%s/libhemlock_runtime_wasm_threaded.a", runtime_path);
            if (access(wasm_runtime, R_OK) != 0) {
                snprintf(wasm_runtime, sizeof(wasm_runtime),
                         "%s/runtime/build/libhemlock_runtime_wasm_threaded.a", runtime_path);
            }
        } else {
            snprintf(wasm_runtime, sizeof(wasm_runtime),
                     "%s/libhemlock_runtime_wasm.a", runtime_path);
            // Fall back to native runtime if WASM runtime not built yet
            if (access(wasm_runtime, R_OK) != 0) {
                snprintf(wasm_runtime, sizeof(wasm_runtime),
                         "%s/runtime/build/libhemlock_runtime_wasm.a", runtime_path);
            }
        }

        // Determine output file extension
        const char *out_file = opts->output_file;
        char js_output[PATH_MAX];
        // If output doesn't end in .js or .html, append .js
        size_t out_len = strlen(out_file);
        if (out_len < 3 ||
            (strcmp(out_file + out_len - 3, ".js") != 0 &&
             (out_len < 5 || strcmp(out_file + out_len - 5, ".html") != 0))) {
            snprintf(js_output, sizeof(js_output), "%s.js", out_file);
            out_file = js_output;
        }

        q_out = shell_quote(out_file);
        q_wasm_runtime = shell_quote(wasm_runtime);
        if (!q_out || !q_wasm_runtime) {
            free(q_out);
            free(q_wasm_runtime);
            free(q_c);
            free(q_inc);
            free(q_runtime_lib);
            fprintf(stderr, "error: Out of memory building compiler command\n");
            return 1;
        }

        if (opts->wasm_threads) {
            // Threaded WASM: pthreads mapped to Web Workers via SharedArrayBuffer
            // -pthread: enable pthreads support (Web Workers)
            // -sPROXY_TO_PTHREAD: run main() in a worker so it can block
            // -sPTHREAD_POOL_SIZE=4: pre-create worker pool for faster spawn
            // -sASYNCIFY: enable async unwinding for sleep() on main thread
            n = snprintf(cmd, sizeof(cmd),
                "emcc %s -o %s %s -I%s %s "
                "-pthread "
                "-sWASM=1 -sEXPORTED_FUNCTIONS=\"['_main']\" "
                "-sEXPORTED_RUNTIME_METHODS=\"['ccall','cwrap']\" "
                "-sALLOW_MEMORY_GROWTH=1 "
                "-sSTACK_SIZE=1048576 "
                "-sPTHREAD_POOL_SIZE=4 "
                "-sPROXY_TO_PTHREAD "
                "-D__HEMLOCK_WASM__=1",
                opt_flag, q_out, q_c,
                q_inc, q_wasm_runtime);
        } else {
            n = snprintf(cmd, sizeof(cmd),
                "emcc %s -o %s %s -I%s %s "
                "-sWASM=1 -sEXPORTED_FUNCTIONS=\"['_main']\" "
                "-sEXPORTED_RUNTIME_METHODS=\"['ccall','cwrap']\" "
                "-sALLOW_MEMORY_GROWTH=1 "
                "-sSTACK_SIZE=1048576 "
                "-D__HEMLOCK_WASM__=1",
                opt_flag, q_out, q_c,
                q_inc, q_wasm_runtime);
        }
    } else if (opts->static_link) {
        // Hybrid static/dynamic linking:
        // - Static: libffi, libz, libssl, libcrypto, libwebsockets
        // - Dynamic: glibc, libcap, libuv, libev (no static libs available on Ubuntu)
        //
        // This matches how hemlock/hemlockc themselves are built for release.
        // We use -Wl,-Bstatic and -Wl,-Bdynamic to selectively link libraries.
        //
        // Note: -ldl is omitted because runtime FFI (ffi_open/ffi_bind) is not
        // expected to work reliably with static linking. Compile-time FFI
        // (extern fn) still works via libffi.
        if (opts->verbose) {
            printf("Static linking enabled - hybrid static/dynamic binary\n");
            printf("Note: Runtime FFI (ffi_open/ffi_bind) disabled in static builds\n");
        }
#ifdef __APPLE__
        // macOS: Can't use -static, use .a files directly or fall back to dynamic
        // System frameworks are always dynamic on macOS
        q_out = shell_quote(opts->output_file);
        n = q_out ? snprintf(cmd, sizeof(cmd),
            "%s %s -rdynamic -o %s %s -I%s %s%s -lm -lpthread -lffi%s%s%s",
            opts->cc, opt_flag, q_out, q_c,
            q_inc, q_runtime_lib, extra_lib_paths, zlib_flag, websockets_flag, crypto_flag)
            : (int)sizeof(cmd);
#else
        // Linux: Hybrid static/dynamic linking
        // Static: libffi, libz; Dynamic: glibc libs (lm, lpthread)
        // If websockets available, add it statically with its dynamic dependencies
        q_out = shell_quote(opts->output_file);
        if (websockets_flag[0]) {
            // libwebsockets requires: libssl, libcrypto (static), libcap, libuv, libev (dynamic)
            n = q_out ? snprintf(cmd, sizeof(cmd),
                "%s %s -rdynamic -o %s %s -I%s %s%s "
                "-Wl,-Bstatic -lffi%s -lwebsockets -lssl -lcrypto "
                "-Wl,-Bdynamic -lcap -luv -lev -lm -lpthread",
                opts->cc, opt_flag, q_out, q_c,
                q_inc, q_runtime_lib, extra_lib_paths, zlib_flag)
                : (int)sizeof(cmd);
        } else {
            // No websockets, just static link libffi, libz, libssl, libcrypto
            n = q_out ? snprintf(cmd, sizeof(cmd),
                "%s %s -rdynamic -o %s %s -I%s %s%s "
                "-Wl,-Bstatic -lffi%s -lssl -lcrypto "
                "-Wl,-Bdynamic -lm -lpthread",
                opts->cc, opt_flag, q_out, q_c,
                q_inc, q_runtime_lib, extra_lib_paths, zlib_flag)
                : (int)sizeof(cmd);
        }
#endif
    } else {
        // Dynamic linking (default): link against shared libraries
        q_out = shell_quote(opts->output_file);
        n = q_out ? snprintf(cmd, sizeof(cmd),
            "%s %s -rdynamic -o %s %s -I%s %s%s -lm -lpthread -lffi -ldl%s%s%s",
            opts->cc, opt_flag, q_out, q_c,
            q_inc, q_runtime_lib, extra_lib_paths, zlib_flag, websockets_flag, crypto_flag)
            : (int)sizeof(cmd);
    }

    free(q_out);
    free(q_wasm_runtime);
    free(q_c);
    free(q_inc);
    free(q_runtime_lib);

    if (n >= (int)sizeof(cmd)) {
        fprintf(stderr, "error: Compiler command too long (truncated)\n");
        return 1;
    }

    if (opts->verbose) {
        printf("Running: %s\n", cmd);
    }

    int status = system(cmd);
    return WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    Options opts = parse_args(argc, argv);

    // Read input file
    char *source = read_file(opts.input_file);
    if (!source) {
        return 1;
    }

    // Parse
    if (opts.verbose) {
        printf("Parsing %s...\n", opts.input_file);
    }

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        free(source);
        return 1;
    }

    if (opts.verbose) {
        printf("Parsed %d statements\n", stmt_count);
    }

    // Resolve variables (compute depth/slot indices - shared with interpreter)
    if (opts.verbose) {
        printf("Resolving variables...\n");
    }
    resolve_program(statements, stmt_count);

    // Optimize AST (constant folding, boolean simplification, strength reduction)
    // This runs before type checking to simplify patterns for analysis
    if (opts.verbose) {
        printf("Optimizing AST...\n");
    }
    OptimizationStats opt_stats = optimize_program(statements, stmt_count);
    if (opts.verbose && (opt_stats.constants_folded > 0 || opt_stats.literals_folded > 0 ||
                         opt_stats.booleans_simplified > 0 || opt_stats.strength_reductions > 0 ||
                         opt_stats.dead_code_eliminated > 0)) {
        printf("  Folded %d constants (%d literals), simplified %d booleans, %d strength reductions, %d dead-code eliminations\n",
               opt_stats.constants_folded, opt_stats.literals_folded,
               opt_stats.booleans_simplified, opt_stats.strength_reductions,
               opt_stats.dead_code_eliminated);
    }

    // Type check (if enabled - on by default)
    // type_ctx is kept for codegen optimization hints
    TypeCheckContext *type_ctx = NULL;
    if (opts.type_check) {
        if (opts.verbose) {
            printf("Type checking...\n");
        }

        type_ctx = type_check_new(opts.input_file);
        type_ctx->warn_implicit_any = opts.strict_types;
        int type_errors = type_check_program(type_ctx, statements, stmt_count);

        if (type_errors > 0) {
            fprintf(stderr, "%d type error%s found\n",
                    type_errors, type_errors > 1 ? "s" : "");
            type_check_free(type_ctx);
            for (int i = 0; i < stmt_count; i++) {
                stmt_free(statements[i]);
            }
            free(statements);
            free(source);
            return 1;
        }

        if (opts.verbose) {
            printf("Type checking passed\n");
        }

        // If --check flag was used, exit after type checking
        if (opts.check_only) {
            if (!opts.verbose) {
                printf("%s: no type errors\n", opts.input_file);
            }
            type_check_free(type_ctx);
            for (int i = 0; i < stmt_count; i++) {
                stmt_free(statements[i]);
            }
            free(statements);
            free(source);
            return 0;
        }
        // Note: type_ctx is kept alive for codegen optimization hints
    }

    // Determine C output file
    char *c_file;
    int c_file_allocated = 0;
    if (opts.c_output) {
        c_file = strdup(opts.c_output);
        c_file_allocated = 1;
    } else if (opts.emit_c_only) {
        // When -c is used with -o, use the output file as C output
        if (strcmp(opts.output_file, "a.out") != 0) {
            c_file = strdup(opts.output_file);
            c_file_allocated = 1;
        } else {
            c_file = make_c_filename(opts.input_file);
            c_file_allocated = 1;
        }
    } else {
        // Generate temp file
        c_file = make_temp_c_filename();
        if (!c_file) {
            fprintf(stderr, "error: Could not create temporary file\n");
            free(source);
            return 1;
        }
        c_file_allocated = 1;
    }

    // Open output file
    FILE *output = fopen(c_file, "w");
    if (!output) {
        fprintf(stderr, "error: Could not open output file '%s'\n", c_file);
        free(source);
        if (c_file_allocated) free(c_file);
        return 1;
    }

    // Generate C code
    if (opts.verbose) {
        printf("Generating C code to %s...\n", c_file);
    }

    // Initialize module cache for import support
    CompilerModuleCache *module_cache = compiler_module_cache_new(opts.input_file);

    CodegenContext *ctx = codegen_new(output);
    codegen_set_module_cache(ctx, module_cache);
    ctx->type_ctx = type_ctx;  // Pass type context for unboxing hints
    ctx->stack_check = opts.stack_check;  // Pass stack check setting
    // Note: ctx->optimize is already set in codegen_new() based on optimization level
    // Don't override it here - the type context is just for unboxing hints

    // Configure WASM target
    ctx->target_wasm = opts.target_wasm;

    // Configure sandbox if enabled
    if (opts.sandbox) {
        // Default sandbox flags: FFI, network, process, file write
        ctx->sandbox_flags = HML_SANDBOX_RESTRICT_FFI |
                            HML_SANDBOX_RESTRICT_NETWORK |
                            HML_SANDBOX_RESTRICT_PROCESS |
                            HML_SANDBOX_RESTRICT_FILE_WRITE;
        if (opts.sandbox_root) {
            ctx->sandbox_flags |= HML_SANDBOX_RESTRICT_FILE_READ;
            ctx->sandbox_root = opts.sandbox_root;
        }
    }

    codegen_program(ctx, statements, stmt_count);

    // Check for compilation errors
    int had_errors = ctx->error_count > 0;
    if (had_errors) {
        fprintf(stderr, "%d error%s generated\n", ctx->error_count, ctx->error_count > 1 ? "s" : "");
    }

    codegen_free(ctx);
    if (type_ctx) type_check_free(type_ctx);
    compiler_module_cache_free(module_cache);
    fclose(output);

    // If there were errors, cleanup and exit
    if (had_errors) {
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
        free(source);
        if (!opts.keep_c && !opts.c_output) {
            unlink(c_file);
        }
        if (c_file_allocated) free(c_file);
        return 1;
    }

    // Cleanup AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
    free(source);

    if (opts.emit_c_only) {
        if (opts.verbose) {
            printf("C code written to %s\n", c_file);
        }
        if (c_file_allocated) free(c_file);
        return 0;
    }

    // Compile C code
    if (opts.verbose) {
        printf("Compiling C code...\n");
    }

    int result = compile_c(&opts, c_file);

    // Cleanup temp file
    if (!opts.keep_c && !opts.c_output) {
        if (opts.verbose) {
            printf("Removing temporary file %s\n", c_file);
        }
        unlink(c_file);
    }

    if (c_file_allocated) free(c_file);

    if (result == 0) {
        if (opts.verbose) {
            printf("Successfully compiled to %s\n", opts.output_file);
        }
    } else {
        fprintf(stderr, "C compilation failed with status %d\n", result);
    }

    return result;
}

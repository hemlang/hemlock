#include "internal.h"
#ifndef _WIN32
#include <sys/utsname.h>
#include <pwd.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#endif

// Get platform name (linux, macos, windows)
Value builtin_platform(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "platform() expects no arguments"); return val_null();
    }

#ifdef __linux__
    return val_string("linux");
#elif defined(__APPLE__)
    return val_string("macos");
#elif defined(_WIN32) || defined(_WIN64)
    return val_string("windows");
#else
    return val_string("unknown");
#endif
}

// Get CPU architecture (x86_64, aarch64, etc.)
Value builtin_arch(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "arch() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return val_string("x86_64");
        case PROCESSOR_ARCHITECTURE_ARM64: return val_string("aarch64");
        case PROCESSOR_ARCHITECTURE_INTEL: return val_string("i686");
        default:                           return val_string("unknown");
    }
#else
    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "arch() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.machine);
#endif
}

// Get system hostname
Value builtin_hostname(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "hostname() expects no arguments"); return val_null();
    }

    hml_platform_init();  // gethostname needs WSAStartup on Windows
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "hostname() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(hostname);
}

// Get current username
Value builtin_username(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "username() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    char username[256];
    DWORD username_len = sizeof(username);
    if (GetUserNameA(username, &username_len)) {
        return val_string(username);
    }

    // Fall back to environment variable
    char *env_user = getenv("USERNAME");
    if (env_user != NULL) {
        return val_string(env_user);
    }
#else
    // Try getlogin_r first
    char username[256];
    if (getlogin_r(username, sizeof(username)) == 0) {
        return val_string(username);
    }

    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_name != NULL) {
        return val_string(pw->pw_name);
    }

    // Fall back to environment variable
    char *env_user = getenv("USER");
    if (env_user != NULL) {
        return val_string(env_user);
    }
#endif

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "username() failed: could not determine username");
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// Get home directory
Value builtin_homedir(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "homedir() expects no arguments"); return val_null();
    }

    // Try HOME environment variable first
    char *home = getenv("HOME");
    if (home != NULL) {
        return val_string(home);
    }

#ifdef _WIN32
    home = getenv("USERPROFILE");
    if (home != NULL) {
        return val_string(home);
    }
#else
    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return val_string(pw->pw_dir);
    }
#endif

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "homedir() failed: could not determine home directory");
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// Get number of CPU cores
Value builtin_cpu_count(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "cpu_count() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    long nprocs = (long)info.dwNumberOfProcessors;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (nprocs < 1) {
        nprocs = 1;  // Default to 1 if we can't determine
    }

    return val_i32((int32_t)nprocs);
}

// Get total system memory in bytes
Value builtin_total_memory(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "total_memory() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "total_memory() failed: error %lu", (unsigned long)GetLastError());
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64((int64_t)status.ullTotalPhys);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "total_memory() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64((int64_t)info.totalram * (int64_t)info.mem_unit);
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    int64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "total_memory() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64(memsize);
#else
    // Fallback: use sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages < 0 || page_size < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "total_memory() failed: could not determine memory");
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64((int64_t)pages * (int64_t)page_size);
#endif
}

// Get free system memory in bytes
Value builtin_free_memory(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "free_memory() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "free_memory() failed: error %lu", (unsigned long)GetLastError());
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64((int64_t)status.ullAvailPhys);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "free_memory() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    // On Linux, freeram doesn't include buffers/cache, so we add them for "available" memory
    int64_t free_mem = (int64_t)info.freeram * (int64_t)info.mem_unit;
    int64_t buffers = (int64_t)info.bufferram * (int64_t)info.mem_unit;
    return val_i64(free_mem + buffers);
#elif defined(__APPLE__)
    // On macOS, use vm_statistics to get free memory
    mach_port_t host_port = mach_host_self();
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_stat) / sizeof(integer_t);
    
    if (host_page_size(host_port, &page_size) != KERN_SUCCESS) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "free_memory() failed: could not get page size");
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    
    if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) != KERN_SUCCESS) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "free_memory() failed: could not get VM statistics");
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    
    // Calculate free memory: free pages + inactive pages (can be reclaimed)
    int64_t free_memory = (int64_t)(vm_stat.free_count + vm_stat.inactive_count) * (int64_t)page_size;
    return val_i64(free_memory);
#else
    // Fallback: use sysconf for available pages if _SC_AVPHYS_PAGES exists
    #ifdef _SC_AVPHYS_PAGES
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (avail_pages >= 0 && page_size >= 0) {
        return val_i64((int64_t)avail_pages * (int64_t)page_size);
    }
    #endif
    
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "free_memory() failed: could not determine free memory");
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();
#endif
}

// Get OS kernel version string
Value builtin_os_version(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "os_version() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    // GetVersionEx lies on Win 8.1+ without a manifest, but it is the only
    // stable public API; the value is informational anyway
    OSVERSIONINFOA info;
    info.dwOSVersionInfoSize = sizeof(info);
    if (!GetVersionExA(&info)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "os_version() failed: error %lu", (unsigned long)GetLastError());
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    char version[64];
    snprintf(version, sizeof(version), "%lu.%lu.%lu",
             (unsigned long)info.dwMajorVersion,
             (unsigned long)info.dwMinorVersion,
             (unsigned long)info.dwBuildNumber);
    return val_string(version);
#else
    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "os_version() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.release);
#endif
}

// Get OS name (detailed, e.g., "Linux", "Darwin")
Value builtin_os_name(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "os_name() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    return val_string("Windows");
#else
    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "os_name() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.sysname);
#endif
}

// Get temporary directory path
Value builtin_tmpdir(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "tmpdir() expects no arguments"); return val_null();
    }

    // Check TMPDIR environment variable first
    char *tmpdir = getenv("TMPDIR");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return val_string(tmpdir);
    }

    // Check TMP environment variable
    tmpdir = getenv("TMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return val_string(tmpdir);
    }

    // Check TEMP environment variable
    tmpdir = getenv("TEMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return val_string(tmpdir);
    }

#ifdef _WIN32
    char temp_path[MAX_PATH + 1];
    DWORD len = GetTempPathA(sizeof(temp_path), temp_path);
    if (len > 0 && len < sizeof(temp_path)) {
        return val_string(temp_path);
    }
#endif

    // Default to /tmp on Unix-like systems
    return val_string("/tmp");
}

// Get uptime in seconds (system boot time)
Value builtin_uptime(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "uptime() expects no arguments"); return val_null();
    }

#ifdef _WIN32
    return val_i64((int64_t)(GetTickCount64() / 1000));
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "uptime() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    return val_i64((int64_t)info.uptime);
#elif defined(__APPLE__)
    // On macOS, use sysctl to get boot time
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "uptime() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    time_t now = time(NULL);
    return val_i64((int64_t)(now - boottime.tv_sec));
#else
    // Fallback: not supported
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "uptime() not supported on this platform");
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();
#endif
}

// ============================================================================
// TERMINAL CONTROL BUILTINS (term_core.c — POSIX termios / Windows console)
// ============================================================================

#include "term_core.h"

// __term_is_tty() -> bool
Value builtin_term_is_tty(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "__term_is_tty() expects no arguments");
        return val_null();
    }
    return val_bool(hml_termcore_is_tty());
}

// __term_raw(enable: bool) -> bool (true on success)
Value builtin_term_raw(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1 || args[0].type != VAL_BOOL) {
        runtime_error(ctx, "__term_raw() expects 1 bool argument");
        return val_null();
    }
    return val_bool(hml_termcore_raw(args[0].as.as_bool) == 0);
}

// __term_read_byte(timeout_ms: i32) -> i32 (-1 on timeout/EOF; timeout < 0 blocks)
Value builtin_term_read_byte(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1 || !is_integer(args[0])) {
        runtime_error(ctx, "__term_read_byte() expects 1 integer argument (timeout ms)");
        return val_null();
    }
    return val_i32(hml_termcore_read_byte(value_to_int(args[0])));
}

// __term_size() -> { rows, cols } | null
Value builtin_term_size(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "__term_size() expects no arguments");
        return val_null();
    }

    int rows = 0, cols = 0;
    if (hml_termcore_size(&rows, &cols) != 0) {
        return val_null();
    }

    Object *result = object_new(NULL, 2);
    if (!result) {
        runtime_error(ctx, "__term_size() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("rows");
    if (!result->fields[0].name) {
        object_free(result);
        runtime_error(ctx, "__term_size() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_i32(rows);
    result->num_fields++;
    result->fields[1].name = strdup("cols");
    if (!result->fields[1].name) {
        object_free(result);
        runtime_error(ctx, "__term_size() memory allocation failed");
        return val_null();
    }
    result->fields[1].value = val_i32(cols);
    result->num_fields++;
    return val_object(result);
}

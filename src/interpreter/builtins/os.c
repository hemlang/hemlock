#include "internal.h"
#include <sys/utsname.h>
#include <pwd.h>

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
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: platform() expects no arguments\n");
        exit(1);
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
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: arch() expects no arguments\n");
        exit(1);
    }

    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "arch() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.machine);
}

// Get system hostname
Value builtin_hostname(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: hostname() expects no arguments\n");
        exit(1);
    }

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
        fprintf(stderr, "Runtime error: username() expects no arguments\n");
        exit(1);
    }

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
        fprintf(stderr, "Runtime error: homedir() expects no arguments\n");
        exit(1);
    }

    // Try HOME environment variable first
    char *home = getenv("HOME");
    if (home != NULL) {
        return val_string(home);
    }

    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return val_string(pw->pw_dir);
    }

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "homedir() failed: could not determine home directory");
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// Get number of CPU cores
Value builtin_cpu_count(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: cpu_count() expects no arguments\n");
        exit(1);
    }

    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) {
        nprocs = 1;  // Default to 1 if we can't determine
    }

    return val_i32((int32_t)nprocs);
}

// Get total system memory in bytes
Value builtin_total_memory(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: total_memory() expects no arguments\n");
        exit(1);
    }

#ifdef __linux__
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
        fprintf(stderr, "Runtime error: free_memory() expects no arguments\n");
        exit(1);
    }

#ifdef __linux__
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
        fprintf(stderr, "Runtime error: os_version() expects no arguments\n");
        exit(1);
    }

    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "os_version() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.release);
}

// Get OS name (detailed, e.g., "Linux", "Darwin")
Value builtin_os_name(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: os_name() expects no arguments\n");
        exit(1);
    }

    struct utsname info;
    if (uname(&info) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "os_name() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(info.sysname);
}

// Get temporary directory path
Value builtin_tmpdir(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: tmpdir() expects no arguments\n");
        exit(1);
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

    // Default to /tmp on Unix-like systems
    return val_string("/tmp");
}

// Get uptime in seconds (system boot time)
Value builtin_uptime(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: uptime() expects no arguments\n");
        exit(1);
    }

#ifdef __linux__
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

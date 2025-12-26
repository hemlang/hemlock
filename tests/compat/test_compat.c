/*
 * Test file for Hemlock compatibility layer
 * Verifies all compat headers compile and link correctly
 */

#include "../../src/compat/platform.h"
#include "../../src/compat/threading.h"
#include "../../src/compat/socket.h"
#include "../../src/compat/dlfcn.h"
#include "../../src/compat/process.h"
#include "../../src/compat/filesystem.h"
#include "../../src/compat/time.h"
#include "../../src/compat/signals.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test results */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", name); \
    tests_run++; \
} while(0)

#define PASS() do { \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAILED: %s\n", msg); \
} while(0)

/* Test platform detection */
void test_platform(void) {
    printf("\n=== Platform Detection ===\n");

    TEST("platform defined");
#if defined(HML_WINDOWS) || defined(HML_LINUX) || defined(HML_MACOS) || defined(HML_BSD)
    PASS();
#else
    FAIL("No platform detected");
#endif

    TEST("platform name");
    if (strlen(HML_PLATFORM_NAME) > 0) {
        printf("(%s) ", HML_PLATFORM_NAME);
        PASS();
    } else {
        FAIL("Empty platform name");
    }

    TEST("architecture");
    if (strlen(HML_ARCH_NAME) > 0) {
        printf("(%s) ", HML_ARCH_NAME);
        PASS();
    } else {
        FAIL("Empty arch name");
    }

    TEST("path separator");
    if (HML_PATH_SEP == '/' || HML_PATH_SEP == '\\') {
        printf("('%c') ", HML_PATH_SEP);
        PASS();
    } else {
        FAIL("Invalid path separator");
    }
}

/* Test threading primitives */
static void *test_thread_func(void *arg) {
    int *value = (int *)arg;
    *value = 42;
    return NULL;
}

void test_threading(void) {
    printf("\n=== Threading ===\n");

    TEST("mutex init/destroy");
    hml_mutex_t mutex;
    if (hml_mutex_init(&mutex) == 0) {
        hml_mutex_destroy(&mutex);
        PASS();
    } else {
        FAIL("mutex_init failed");
    }

    TEST("mutex lock/unlock");
    hml_mutex_init(&mutex);
    if (hml_mutex_lock(&mutex) == 0 && hml_mutex_unlock(&mutex) == 0) {
        PASS();
    } else {
        FAIL("lock/unlock failed");
    }
    hml_mutex_destroy(&mutex);

    TEST("thread create/join");
    int value = 0;
    hml_thread_t thread;
    if (hml_thread_create(&thread, test_thread_func, &value) == 0) {
        hml_thread_join(thread, NULL);
        if (value == 42) {
            PASS();
        } else {
            FAIL("thread didn't run");
        }
    } else {
        FAIL("thread_create failed");
    }

    TEST("sleep_ms");
    hml_sleep_ms(10);
    PASS();

    TEST("atomic operations");
    hml_atomic_int counter = 0;
    hml_atomic_store(&counter, 5);
    if (hml_atomic_load(&counter) == 5) {
        hml_atomic_fetch_add(&counter, 3);
        if (hml_atomic_load(&counter) == 8) {
            PASS();
        } else {
            FAIL("atomic_add failed");
        }
    } else {
        FAIL("atomic_store/load failed");
    }
}

/* Test time functions */
void test_time(void) {
    printf("\n=== Time Functions ===\n");

    TEST("gettimeofday");
    struct timeval tv;
    if (hml_gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 0) {
        PASS();
    } else {
        FAIL("gettimeofday failed");
    }

    TEST("clock_gettime REALTIME");
    struct timespec ts;
    if (hml_clock_gettime(CLOCK_REALTIME, &ts) == 0 && ts.tv_sec > 0) {
        PASS();
    } else {
        FAIL("clock_gettime failed");
    }

    TEST("clock_gettime MONOTONIC");
    if (hml_clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        PASS();
    } else {
        FAIL("clock_gettime monotonic failed");
    }

    TEST("hml_now");
    double now = hml_now();
    if (now > 1000000000.0) {  /* After 2001 */
        PASS();
    } else {
        FAIL("hml_now returned invalid time");
    }

    TEST("hml_time_ms");
    long long ms = hml_time_ms();
    hml_sleep_ms(50);
    long long ms2 = hml_time_ms();
    if (ms2 >= ms + 40) {  /* Allow some tolerance */
        PASS();
    } else {
        FAIL("time_ms not advancing");
    }
}

/* Test filesystem functions */
void test_filesystem(void) {
    printf("\n=== Filesystem ===\n");

    TEST("getcwd");
    char cwd[1024];
    if (hml_getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("(%s) ", cwd);
        PASS();
    } else {
        FAIL("getcwd failed");
    }

    TEST("opendir/readdir/closedir");
    hml_dir_t *dir = hml_opendir(".");
    if (dir != NULL) {
        hml_dirent_t *entry = hml_readdir(dir);
        if (entry != NULL && strlen(entry->d_name) > 0) {
            hml_closedir(dir);
            PASS();
        } else {
            hml_closedir(dir);
            FAIL("readdir failed");
        }
    } else {
        FAIL("opendir failed");
    }

    TEST("stat");
    hml_stat_t st;
    if (hml_stat(".", &st) == 0 && st.is_directory) {
        PASS();
    } else {
        FAIL("stat failed");
    }

    TEST("access");
    if (hml_access(".", F_OK) == 0) {
        PASS();
    } else {
        FAIL("access failed");
    }
}

/* Test process functions */
void test_process(void) {
    printf("\n=== Process ===\n");

    TEST("getpid");
    hml_pid_t pid = hml_getpid();
    if (pid > 0) {
        printf("(%d) ", (int)pid);
        PASS();
    } else {
        FAIL("getpid returned invalid pid");
    }

    TEST("setenv/getenv");
    if (hml_setenv("HML_TEST_VAR", "test_value", 1) == 0) {
        char *val = getenv("HML_TEST_VAR");
        if (val && strcmp(val, "test_value") == 0) {
            PASS();
        } else {
            FAIL("getenv didn't return set value");
        }
        hml_unsetenv("HML_TEST_VAR");
    } else {
        FAIL("setenv failed");
    }
}

/* Test dynamic loading */
void test_dlfcn(void) {
    printf("\n=== Dynamic Loading ===\n");

    TEST("dlopen libc");
#ifdef HML_WINDOWS
    hml_lib_t lib = hml_dlopen("kernel32.dll", RTLD_LAZY);
#elif defined(HML_MACOS)
    hml_lib_t lib = hml_dlopen("libc.dylib", RTLD_LAZY);
#else
    hml_lib_t lib = hml_dlopen("libc.so.6", RTLD_LAZY);
#endif
    if (lib != HML_LIB_INVALID) {
        PASS();

        TEST("dlsym");
#ifdef HML_WINDOWS
        void *sym = hml_dlsym(lib, "GetCurrentProcessId");
#else
        void *sym = hml_dlsym(lib, "getpid");
#endif
        if (sym != NULL) {
            PASS();
        } else {
            FAIL("dlsym failed");
        }

        TEST("dlclose");
        if (hml_dlclose(lib) == 0) {
            PASS();
        } else {
            FAIL("dlclose failed");
        }
    } else {
        char *err = hml_dlerror();
        printf("(%s) ", err ? err : "unknown error");
        FAIL("dlopen failed");
    }
}

/* Test signals */
void test_signals(void) {
    printf("\n=== Signals ===\n");

    TEST("signal_supported SIGINT");
    if (hml_signal_supported(SIGINT)) {
        PASS();
    } else {
        FAIL("SIGINT not supported");
    }

    TEST("signal_supported SIGTERM");
    if (hml_signal_supported(SIGTERM)) {
        PASS();
    } else {
        FAIL("SIGTERM not supported");
    }

#ifndef HML_WINDOWS
    TEST("signal_supported SIGUSR1");
    if (hml_signal_supported(SIGUSR1)) {
        PASS();
    } else {
        FAIL("SIGUSR1 not supported on POSIX");
    }
#endif

    TEST("sigset operations");
    hml_sigset_t set;
    hml_sig_emptyset(&set);
    hml_sigaddset(&set, SIGINT);
    if (hml_sigismember(&set, SIGINT)) {
        hml_sigdelset(&set, SIGINT);
        if (!hml_sigismember(&set, SIGINT)) {
            PASS();
        } else {
            FAIL("sigdelset failed");
        }
    } else {
        FAIL("sigaddset failed");
    }
}

/* Test sockets */
void test_socket(void) {
    printf("\n=== Sockets ===\n");

    TEST("socket_init");
    if (hml_socket_init() == 0) {
        PASS();
    } else {
        FAIL("socket_init failed");
    }

    TEST("socket create TCP");
    hml_socket_t sock = hml_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != HML_INVALID_SOCKET) {
        PASS();

        TEST("closesocket");
        if (hml_closesocket(sock) == 0) {
            PASS();
        } else {
            FAIL("closesocket failed");
        }
    } else {
        FAIL("socket create failed");
    }

    TEST("socket create UDP");
    sock = hml_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != HML_INVALID_SOCKET) {
        hml_closesocket(sock);
        PASS();
    } else {
        FAIL("UDP socket create failed");
    }

    hml_socket_cleanup();
}

int main(void) {
    printf("Hemlock Compatibility Layer Tests\n");
    printf("==================================\n");

    test_platform();
    test_threading();
    test_time();
    test_filesystem();
    test_process();
    test_dlfcn();
    test_signals();
    test_socket();

    printf("\n==================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

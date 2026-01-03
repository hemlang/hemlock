/*
 * Hemlock Language Limits
 *
 * Centralized definitions for all compile-time and runtime limits.
 * This file documents hard limits in the language implementation.
 */

#ifndef HEMLOCK_LIMITS_H
#define HEMLOCK_LIMITS_H

// ========== PARSER LIMITS ==========

// Maximum number of function parameters (hard limit)
#define HML_MAX_FUNCTION_PARAMS 64

// Initial capacity for dynamic arrays (will grow as needed)
#define HML_INITIAL_STATEMENTS_CAPACITY 256
#define HML_INITIAL_ARRAY_LITERAL_CAPACITY 64
#define HML_INITIAL_OBJECT_FIELDS_CAPACITY 32
#define HML_INITIAL_SWITCH_CASES_CAPACITY 32
#define HML_INITIAL_IMPORT_NAMES_CAPACITY 32
#define HML_INITIAL_ENUM_VARIANTS_CAPACITY 32
#define HML_INITIAL_EXTERN_PARAMS_CAPACITY 32
#define HML_INITIAL_INTERPOLATION_PARTS_CAPACITY 32
#define HML_INITIAL_STRING_BUFFER_SIZE 1024

// ========== INTERPRETER LIMITS ==========

// Environment pool size - maximum number of pre-allocated environments
// Exceeding this falls back to malloc (slower but still works)
#define HML_ENV_POOL_SIZE 1024

// Default capacity for variables in an environment
#define HML_ENV_DEFAULT_CAPACITY 16

// Default maximum call stack depth (prevents stack overflow)
// Can be changed at runtime via --stack-depth flag
#define HML_DEFAULT_MAX_STACK_DEPTH 10000

// Maximum signal number for signal handlers (POSIX standard)
#define HML_MAX_SIGNAL 64

// ========== COMPILER LIMITS ==========

// Buffer size for mangled names (module prefix + symbol name)
#define HML_MANGLED_NAME_SIZE 256

// Buffer size for generated variable names
#define HML_GENERATED_NAME_SIZE 64

// ========== NETWORK LIMITS ==========

// Maximum hostname length for network connections
#define HML_MAX_HOSTNAME_LENGTH 256

// WebSocket and HTTP buffer sizes
#define HML_WS_HEADER_BUFFER_SIZE 8192
#define HML_WS_BODY_BUFFER_SIZE 4096
#define HML_WS_MAX_HEADER_DATA 16384

// ========== I/O LIMITS ==========

// File read chunk size
#define HML_FILE_READ_CHUNK_SIZE 4096

// Error message buffer size
#define HML_ERROR_MESSAGE_SIZE 256

// ========== GROWTH FACTOR ==========

// When dynamic arrays need to grow, multiply by this factor
#define HML_GROWTH_FACTOR 2

// ========== TYPE RANGE LIMITS ==========

// Signed integer ranges
#define HML_I8_MIN  (-128)
#define HML_I8_MAX  127
#define HML_I16_MIN (-32768)
#define HML_I16_MAX 32767
#define HML_I32_MIN (-2147483648LL)
#define HML_I32_MAX 2147483647LL
#define HML_I64_MIN (-9223372036854775807LL - 1)
#define HML_I64_MAX 9223372036854775807LL

// Unsigned integer ranges
#define HML_U8_MAX  255
#define HML_U16_MAX 65535
#define HML_U32_MAX 4294967295LL
#define HML_U64_MAX 18446744073709551615ULL

// ========== INITIAL CAPACITIES ==========

// Initial capacity for arrays
#define HML_INITIAL_ARRAY_CAPACITY 8

// Initial capacity for visited sets (cycle detection)
#define HML_INITIAL_VISITED_SET_CAPACITY 16

// Initial capacity for lexer string buffers
#define HML_INITIAL_LEXER_BUFFER_CAPACITY 256

// ========== HASH CONSTANTS ==========

// DJB2 hash function seed value
#define HML_DJB2_HASH_SEED 5381

// ========== TIME CONSTANTS ==========

// Time unit conversions
#define HML_NANOSECONDS_PER_SECOND  1000000000L
#define HML_NANOSECONDS_PER_MS      1000000L
#define HML_MILLISECONDS_PER_SECOND 1000

// Default sleep interval for polling (1ms in nanoseconds)
#define HML_POLL_SLEEP_NS 1000000L

// ========== ASCII CONSTANTS ==========

// ASCII case conversion offset (difference between 'a' and 'A')
#define HML_ASCII_CASE_OFFSET 32

// ASCII printable character range
#define HML_ASCII_PRINTABLE_START 32
#define HML_ASCII_PRINTABLE_END   127

// ========== THREAD POOL CONFIGURATION ==========

// Default number of worker threads (0 = auto-detect based on CPU count)
#define HML_THREADPOOL_DEFAULT_WORKERS 0

// Maximum number of worker threads
#define HML_THREADPOOL_MAX_WORKERS 256

// Minimum number of worker threads
#define HML_THREADPOOL_MIN_WORKERS 2

// Initial capacity of per-worker work-stealing deque
#define HML_THREADPOOL_DEQUE_INITIAL_CAPACITY 64

// Maximum capacity of per-worker work-stealing deque
#define HML_THREADPOOL_DEQUE_MAX_CAPACITY 65536

// Global submission queue capacity
#define HML_THREADPOOL_SUBMISSION_QUEUE_CAPACITY 4096

// Number of steal attempts before sleeping
#define HML_THREADPOOL_STEAL_ATTEMPTS 32

// Sleep duration when no work is available (microseconds)
#define HML_THREADPOOL_IDLE_SLEEP_US 100

// Work stealing random seed multiplier (for LFSR)
#define HML_THREADPOOL_STEAL_SEED_MULT 1103515245

// ========== SANDBOX CONFIGURATION ==========

// Sandbox restriction flags (bitmask)
#define HML_SANDBOX_RESTRICT_FFI         0x0001  // Disable FFI (dlopen, extern fn)
#define HML_SANDBOX_RESTRICT_NETWORK     0x0002  // Disable network (sockets, http, websocket)
#define HML_SANDBOX_RESTRICT_PROCESS     0x0004  // Disable process spawning (exec, fork, spawn)
#define HML_SANDBOX_RESTRICT_FILE_WRITE  0x0008  // Disable file write operations
#define HML_SANDBOX_RESTRICT_FILE_READ   0x0010  // Disable file read operations (outside sandbox root)
#define HML_SANDBOX_RESTRICT_SIGNALS     0x0020  // Disable signal operations (signal, raise, kill, abort)

// Default sandbox restrictions (everything restricted)
#define HML_SANDBOX_RESTRICT_ALL         0x003F
#define HML_SANDBOX_RESTRICT_NONE        0x0000

// Maximum path length for sandbox root directory
#define HML_SANDBOX_ROOT_MAX_PATH        4096

#endif // HEMLOCK_LIMITS_H

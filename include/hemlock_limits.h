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
// Note: 10000 can exceed C stack limits on some systems; 8000 is safer
#define HML_DEFAULT_MAX_STACK_DEPTH 8000

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

// ========== FUNCTION CALL LIMITS ==========

// Maximum arguments for stack-allocated argument arrays
// Beyond this count, heap allocation is used (slower but unlimited)
#define HML_MAX_STACK_ARGS 8

// ========== FFI LIMITS ==========

// Maximum FFI arguments using stack allocation
// Must match HML_MAX_STACK_ARGS for consistency
#define HML_FFI_MAX_STACK_ARGS 8

// ========== TYPE STRING BUFFERS ==========

// Buffer size for type name formatting (type_to_string, etc.)
#define HML_TYPE_NAME_BUFSIZE 256

// ========== SERIALIZATION LIMITS ==========

// Initial buffer size for AST serialization
#define HML_AST_SERIALIZE_INITIAL_BUFFER 4096

// ========== MEMORY OPTIMIZATION CONSTANTS ==========

// Small String Optimization (SSO) threshold
// Strings up to this length are stored inline in the HmlString struct
// 23 bytes chosen to fit well in typical allocator size classes (32-byte slot)
#define HML_SSO_THRESHOLD 23

// String interning table size for object field names
// Should be a prime number for better hash distribution
#define HML_INTERN_TABLE_SIZE 1021

// Maximum interned string length (longer strings bypass interning)
#define HML_INTERN_MAX_LENGTH 64

// ========== THREAD STACK SIZE ==========

// Stack size for spawned task threads (in bytes)
// The interpreter's eval_stmt is recursive, so deep Hemlock call stacks
// (especially with closures in WebSocket callbacks) can overflow the default
// pthread stack (typically 2 MB on Linux).  16 MB gives comfortable headroom
// for sequential server-spawn patterns without wasting significant virtual memory.
#define HML_THREAD_STACK_SIZE (16 * 1024 * 1024)

// ========== ATOMIC OPERATION CONSTANTS ==========

// Required alignment for atomic operations
// Atomic operations require naturally aligned pointers to guarantee atomicity
#define HML_ATOMIC_I32_ALIGNMENT 4   // _Alignof(_Atomic int32_t)
#define HML_ATOMIC_I64_ALIGNMENT 8   // _Alignof(_Atomic int64_t)

// ========== WASM PERSISTENT CONTEXT ==========

// Maximum number of concurrent persistent WASM contexts
#define HML_MAX_WASM_CONTEXTS 64

// Maximum number of cached (pre-compiled) WASM scripts
#define HML_MAX_WASM_SCRIPTS 256

// ========== OBJECT POOL ==========

// Object pool size - maximum number of pre-allocated Object structs
// Exceeding this falls back to malloc (slower but still works)
#define HML_OBJECT_POOL_SIZE 512

// Default field capacity for pooled objects
// Pooled objects pre-allocate this many FieldEntry slots to avoid realloc
#define HML_OBJECT_POOL_FIELDS_CAPACITY 8

// ========== FUNCTION POOL ==========

// Function pool size - maximum number of pre-allocated Function structs
// Closures in hot loops benefit the most from pooled allocation
#define HML_FUNCTION_POOL_SIZE 512

// ========== INLINE CACHE CONSTANTS ==========

// Inline caching is used to speed up property access and method dispatch
// by caching the result of lookups at call sites

// IC state values
#define HML_IC_STATE_UNINITIALIZED 0   // Cache not yet populated
#define HML_IC_STATE_MONOMORPHIC   1   // Single type/shape seen
#define HML_IC_STATE_MEGAMORPHIC   2   // Too many types, disable caching

// Maximum number of misses before going megamorphic
#define HML_IC_MAX_MISSES 4

// IC type tags for method dispatch (matches ValueType enum ordering)
// These allow fast type comparison without needing the full Value struct
#define HML_IC_TYPE_STRING   1
#define HML_IC_TYPE_ARRAY    2
#define HML_IC_TYPE_OBJECT   3
#define HML_IC_TYPE_FILE     4
#define HML_IC_TYPE_SOCKET   5
#define HML_IC_TYPE_CHANNEL  6
#define HML_IC_TYPE_BUFFER   7

// Compiler inlining limits
#define HML_MAX_INLINE_DEPTH 3  // Maximum nesting depth for function inlining

#endif // HEMLOCK_LIMITS_H

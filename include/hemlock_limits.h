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

#endif // HEMLOCK_LIMITS_H

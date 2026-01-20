/**
 * hemlock_errors.h - Error codes and categories for Hemlock
 *
 * This file defines error codes for programmatic error handling and
 * provides consistent error message formatting.
 */

#ifndef HEMLOCK_ERRORS_H
#define HEMLOCK_ERRORS_H

// ========== ERROR CATEGORIES ==========
// Error codes are organized by category with distinct ranges

// Parse Errors (1000-1999)
#define HML_ERR_PARSE_BASE           1000
#define HML_ERR_UNEXPECTED_TOKEN     1001
#define HML_ERR_UNTERMINATED_STRING  1002
#define HML_ERR_UNTERMINATED_COMMENT 1003
#define HML_ERR_INVALID_ESCAPE       1004
#define HML_ERR_INVALID_NUMBER       1005
#define HML_ERR_EXPECTED_EXPRESSION  1006
#define HML_ERR_EXPECTED_IDENTIFIER  1007
#define HML_ERR_EXPECTED_TYPE        1008
#define HML_ERR_INVALID_ANNOTATION   1009

// Type Errors (2000-2999)
#define HML_ERR_TYPE_BASE            2000
#define HML_ERR_TYPE_MISMATCH        2001
#define HML_ERR_UNDEFINED_VARIABLE   2002
#define HML_ERR_NOT_CALLABLE         2003
#define HML_ERR_WRONG_ARG_COUNT      2004
#define HML_ERR_NOT_INDEXABLE        2005
#define HML_ERR_INVALID_OPERAND      2006
#define HML_ERR_UNDEFINED_PROPERTY   2007
#define HML_ERR_NOT_ITERABLE         2008
#define HML_ERR_UNDEFINED_METHOD     2009

// Runtime Errors (3000-3999)
#define HML_ERR_RUNTIME_BASE         3000
#define HML_ERR_DIVISION_BY_ZERO     3001
#define HML_ERR_OUT_OF_BOUNDS        3002
#define HML_ERR_NULL_REFERENCE       3003
#define HML_ERR_STACK_OVERFLOW       3004
#define HML_ERR_ASSERTION_FAILED     3005
#define HML_ERR_TIMEOUT              3006
#define HML_ERR_DEADLOCK             3007

// Memory Errors (4000-4999)
#define HML_ERR_MEMORY_BASE          4000
#define HML_ERR_OUT_OF_MEMORY        4001
#define HML_ERR_DOUBLE_FREE          4002
#define HML_ERR_USE_AFTER_FREE       4003
#define HML_ERR_BUFFER_OVERFLOW      4004
#define HML_ERR_INVALID_POINTER      4005
#define HML_ERR_ALLOCATION_FAILED    4006

// I/O Errors (5000-5999)
#define HML_ERR_IO_BASE              5000
#define HML_ERR_FILE_NOT_FOUND       5001
#define HML_ERR_PERMISSION_DENIED    5002
#define HML_ERR_FILE_EXISTS          5003
#define HML_ERR_NOT_A_DIRECTORY      5004
#define HML_ERR_IS_A_DIRECTORY       5005
#define HML_ERR_IO_ERROR             5006
#define HML_ERR_FILE_TOO_LARGE       5007
#define HML_ERR_DISK_FULL            5008

// Module Errors (6000-6999)
#define HML_ERR_MODULE_BASE          6000
#define HML_ERR_MODULE_NOT_FOUND     6001
#define HML_ERR_CIRCULAR_DEPENDENCY  6002
#define HML_ERR_EXPORT_NOT_FOUND     6003
#define HML_ERR_INVALID_IMPORT       6004
#define HML_ERR_MODULE_PARSE_FAILED  6005
#define HML_ERR_MODULE_LOAD_FAILED   6006

// Async/Concurrency Errors (7000-7999)
#define HML_ERR_ASYNC_BASE           7000
#define HML_ERR_TASK_CANCELLED       7001
#define HML_ERR_TASK_PANICKED        7002
#define HML_ERR_CHANNEL_CLOSED       7003
#define HML_ERR_CHANNEL_FULL         7004
#define HML_ERR_CHANNEL_EMPTY        7005
#define HML_ERR_JOIN_FAILED          7006
#define HML_ERR_SPAWN_FAILED         7007

// FFI Errors (8000-8999)
#define HML_ERR_FFI_BASE             8000
#define HML_ERR_FFI_LOAD_FAILED      8001
#define HML_ERR_FFI_SYMBOL_NOT_FOUND 8002
#define HML_ERR_FFI_TYPE_MISMATCH    8003
#define HML_ERR_FFI_CALL_FAILED      8004
#define HML_ERR_FFI_INVALID_ARG      8005

// System Errors (9000-9999)
#define HML_ERR_SYSTEM_BASE          9000
#define HML_ERR_SIGNAL_ERROR         9001
#define HML_ERR_PROCESS_ERROR        9002
#define HML_ERR_NETWORK_ERROR        9003
#define HML_ERR_ENV_ERROR            9004
#define HML_ERR_SANDBOX_VIOLATION    9005

// ========== ERROR SEVERITY ==========

typedef enum {
    HML_SEVERITY_WARNING = 0,   // Non-fatal, execution continues
    HML_SEVERITY_ERROR = 1,     // Fatal, execution stops (may be catchable)
    HML_SEVERITY_FATAL = 2      // Unrecoverable, program exits (not catchable)
} HmlErrorSeverity;

// ========== ERROR MESSAGE MACROS ==========

// Standard error message format: "category: message"
// Examples:
//   "error: division by zero"
//   "warning: unused variable 'x'"
//   "type error: expected string, got i32"

#define HML_ERR_PREFIX_ERROR    "error"
#define HML_ERR_PREFIX_WARNING  "warning"
#define HML_ERR_PREFIX_TYPE     "type error"
#define HML_ERR_PREFIX_PARSE    "parse error"
#define HML_ERR_PREFIX_RUNTIME  "runtime error"
#define HML_ERR_PREFIX_MEMORY   "memory error"
#define HML_ERR_PREFIX_IO       "I/O error"
#define HML_ERR_PREFIX_MODULE   "module error"
#define HML_ERR_PREFIX_ASYNC    "async error"
#define HML_ERR_PREFIX_FFI      "FFI error"
#define HML_ERR_PREFIX_SYSTEM   "system error"

// ========== UTILITY FUNCTIONS ==========

/**
 * Get the category name for an error code.
 */
static inline const char* hml_error_category(int code) {
    if (code >= HML_ERR_PARSE_BASE && code < HML_ERR_TYPE_BASE)
        return "parse";
    if (code >= HML_ERR_TYPE_BASE && code < HML_ERR_RUNTIME_BASE)
        return "type";
    if (code >= HML_ERR_RUNTIME_BASE && code < HML_ERR_MEMORY_BASE)
        return "runtime";
    if (code >= HML_ERR_MEMORY_BASE && code < HML_ERR_IO_BASE)
        return "memory";
    if (code >= HML_ERR_IO_BASE && code < HML_ERR_MODULE_BASE)
        return "I/O";
    if (code >= HML_ERR_MODULE_BASE && code < HML_ERR_ASYNC_BASE)
        return "module";
    if (code >= HML_ERR_ASYNC_BASE && code < HML_ERR_FFI_BASE)
        return "async";
    if (code >= HML_ERR_FFI_BASE && code < HML_ERR_SYSTEM_BASE)
        return "FFI";
    if (code >= HML_ERR_SYSTEM_BASE)
        return "system";
    return "unknown";
}

/**
 * Check if an error code is catchable (can be handled with try/catch).
 * Fatal errors and panics are not catchable.
 */
static inline int hml_error_is_catchable(int code) {
    // Memory errors (OOM, double-free) are generally not catchable
    if (code >= HML_ERR_MEMORY_BASE && code < HML_ERR_IO_BASE)
        return 0;
    // Stack overflow is not catchable
    if (code == HML_ERR_STACK_OVERFLOW)
        return 0;
    // Most other errors are catchable
    return 1;
}

#endif // HEMLOCK_ERRORS_H

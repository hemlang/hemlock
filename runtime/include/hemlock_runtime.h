/*
 * Hemlock Runtime Library
 *
 * Main header file for the Hemlock runtime library.
 * Include this header in compiled Hemlock programs.
 */

#ifndef HEMLOCK_RUNTIME_H
#define HEMLOCK_RUNTIME_H

#include "hemlock_value.h"
#include <setjmp.h>

// Forward declarations
typedef struct HmlClosureEnv HmlClosureEnv;

// ========== RUNTIME INITIALIZATION ==========

// Initialize the Hemlock runtime (call at start of main)
void hml_runtime_init(int argc, char **argv);

// Cleanup the Hemlock runtime (call at end of main)
void hml_runtime_cleanup(void);

// Get command-line arguments as Hemlock array
HmlValue hml_get_args(void);

// ========== BINARY OPERATIONS ==========

typedef enum {
    HML_OP_ADD,
    HML_OP_SUB,
    HML_OP_MUL,
    HML_OP_DIV,
    HML_OP_MOD,
    HML_OP_EQUAL,
    HML_OP_NOT_EQUAL,
    HML_OP_LESS,
    HML_OP_LESS_EQUAL,
    HML_OP_GREATER,
    HML_OP_GREATER_EQUAL,
    HML_OP_AND,
    HML_OP_OR,
    HML_OP_BIT_AND,
    HML_OP_BIT_OR,
    HML_OP_BIT_XOR,
    HML_OP_LSHIFT,
    HML_OP_RSHIFT,
} HmlBinaryOp;

// Perform binary operation with automatic type promotion
HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right);

// ========== UNARY OPERATIONS ==========

typedef enum {
    HML_UNARY_NOT,
    HML_UNARY_NEGATE,
    HML_UNARY_BIT_NOT,
} HmlUnaryOp;

HmlValue hml_unary_op(HmlUnaryOp op, HmlValue operand);

// ========== BUILTIN FUNCTIONS ==========

// I/O
void hml_print(HmlValue val);
void hml_eprint(HmlValue val);
HmlValue hml_read_line(void);

// Type checking
const char* hml_typeof(HmlValue val);
void hml_check_type(HmlValue val, HmlValueType expected, const char *var_name);
int hml_values_equal(HmlValue left, HmlValue right);

// Type conversion with range checking (used for typed variable declarations)
HmlValue hml_convert_to_type(HmlValue val, HmlValueType target_type);

// Assertions
void hml_assert(HmlValue condition, HmlValue message);
void hml_panic(HmlValue message);

// Command execution
HmlValue hml_exec(HmlValue command);

// ========== MATH OPERATIONS ==========

HmlValue hml_sqrt(HmlValue x);
HmlValue hml_sin(HmlValue x);
HmlValue hml_cos(HmlValue x);
HmlValue hml_tan(HmlValue x);
HmlValue hml_asin(HmlValue x);
HmlValue hml_acos(HmlValue x);
HmlValue hml_atan(HmlValue x);
HmlValue hml_floor(HmlValue x);
HmlValue hml_ceil(HmlValue x);
HmlValue hml_round(HmlValue x);
HmlValue hml_trunc(HmlValue x);
HmlValue hml_abs(HmlValue x);
HmlValue hml_pow(HmlValue base, HmlValue exp);
HmlValue hml_exp(HmlValue x);
HmlValue hml_log(HmlValue x);
HmlValue hml_min(HmlValue a, HmlValue b);
HmlValue hml_max(HmlValue a, HmlValue b);
HmlValue hml_clamp(HmlValue x, HmlValue min_val, HmlValue max_val);
HmlValue hml_log10(HmlValue x);
HmlValue hml_log2(HmlValue x);
HmlValue hml_atan2(HmlValue y, HmlValue x);
HmlValue hml_rand(void);
HmlValue hml_rand_range(HmlValue min_val, HmlValue max_val);
HmlValue hml_seed_val(HmlValue seed);
void hml_seed(HmlValue seed);

// Builtin wrappers for compiler (match calling convention: HmlClosureEnv*, args...)
HmlValue hml_builtin_sin(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_cos(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_tan(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_asin(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_acos(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_atan(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_atan2(HmlClosureEnv *env, HmlValue y, HmlValue x);
HmlValue hml_builtin_sqrt(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_pow(HmlClosureEnv *env, HmlValue base, HmlValue exp);
HmlValue hml_builtin_exp(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_log(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_log10(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_log2(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_floor(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_ceil(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_round(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_trunc(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_abs(HmlClosureEnv *env, HmlValue x);
HmlValue hml_builtin_min(HmlClosureEnv *env, HmlValue a, HmlValue b);
HmlValue hml_builtin_max(HmlClosureEnv *env, HmlValue a, HmlValue b);
HmlValue hml_builtin_clamp(HmlClosureEnv *env, HmlValue x, HmlValue lo, HmlValue hi);
HmlValue hml_builtin_rand(HmlClosureEnv *env);
HmlValue hml_builtin_rand_range(HmlClosureEnv *env, HmlValue min_val, HmlValue max_val);
HmlValue hml_builtin_seed(HmlClosureEnv *env, HmlValue seed);

// ========== TIME OPERATIONS ==========

HmlValue hml_now(void);
HmlValue hml_time_ms(void);
HmlValue hml_clock(void);
void hml_sleep(HmlValue seconds);

// Time builtin wrappers
HmlValue hml_builtin_now(HmlClosureEnv *env);
HmlValue hml_builtin_time_ms(HmlClosureEnv *env);
HmlValue hml_builtin_clock(HmlClosureEnv *env);
HmlValue hml_builtin_sleep(HmlClosureEnv *env, HmlValue seconds);

// ========== DATETIME OPERATIONS ==========

HmlValue hml_localtime(HmlValue timestamp);
HmlValue hml_gmtime(HmlValue timestamp);
HmlValue hml_mktime(HmlValue time_obj);
HmlValue hml_strftime(HmlValue format, HmlValue time_obj);

// Datetime builtin wrappers
HmlValue hml_builtin_localtime(HmlClosureEnv *env, HmlValue timestamp);
HmlValue hml_builtin_gmtime(HmlClosureEnv *env, HmlValue timestamp);
HmlValue hml_builtin_mktime(HmlClosureEnv *env, HmlValue time_obj);
HmlValue hml_builtin_strftime(HmlClosureEnv *env, HmlValue format, HmlValue time_obj);

// ========== ENVIRONMENT OPERATIONS ==========

HmlValue hml_getenv(HmlValue name);
void hml_setenv(HmlValue name, HmlValue value);
void hml_exit(HmlValue code);
HmlValue hml_get_pid(void);
HmlValue hml_exec(HmlValue command);

// Environment builtin wrappers
HmlValue hml_builtin_getenv(HmlClosureEnv *env, HmlValue name);
HmlValue hml_builtin_setenv(HmlClosureEnv *env, HmlValue name, HmlValue value);
HmlValue hml_builtin_unsetenv(HmlClosureEnv *env, HmlValue name);
HmlValue hml_builtin_exit(HmlClosureEnv *env, HmlValue code);
HmlValue hml_builtin_get_pid(HmlClosureEnv *env);
HmlValue hml_builtin_exec(HmlClosureEnv *env, HmlValue command);

// ========== PROCESS OPERATIONS ==========

HmlValue hml_getppid(void);
HmlValue hml_getuid(void);
HmlValue hml_geteuid(void);
HmlValue hml_getgid(void);
HmlValue hml_getegid(void);
HmlValue hml_unsetenv(HmlValue name);
HmlValue hml_kill(HmlValue pid, HmlValue sig);
HmlValue hml_fork(void);
HmlValue hml_wait(void);
HmlValue hml_waitpid(HmlValue pid, HmlValue options);
void hml_abort(void);

// Process builtin wrappers
HmlValue hml_builtin_getppid(HmlClosureEnv *env);
HmlValue hml_builtin_getuid(HmlClosureEnv *env);
HmlValue hml_builtin_geteuid(HmlClosureEnv *env);
HmlValue hml_builtin_getgid(HmlClosureEnv *env);
HmlValue hml_builtin_getegid(HmlClosureEnv *env);
HmlValue hml_builtin_kill(HmlClosureEnv *env, HmlValue pid, HmlValue sig);
HmlValue hml_builtin_fork(HmlClosureEnv *env);
HmlValue hml_builtin_wait(HmlClosureEnv *env);
HmlValue hml_builtin_waitpid(HmlClosureEnv *env, HmlValue pid, HmlValue options);
HmlValue hml_builtin_abort(HmlClosureEnv *env);

// ========== I/O OPERATIONS ==========

HmlValue hml_read_line(void);

// ========== TYPE OPERATIONS ==========

HmlValue hml_sizeof(HmlValue type_name);

// ========== STRING OPERATIONS ==========

HmlValue hml_string_concat(HmlValue a, HmlValue b);
HmlValue hml_string_append_inplace(HmlValue *dest, HmlValue src);  // For x = x + y pattern
HmlValue hml_string_concat3(HmlValue a, HmlValue b, HmlValue c);
HmlValue hml_string_concat4(HmlValue a, HmlValue b, HmlValue c, HmlValue d);
HmlValue hml_string_concat5(HmlValue a, HmlValue b, HmlValue c, HmlValue d, HmlValue e);
HmlValue hml_string_length(HmlValue str);
HmlValue hml_string_byte_length(HmlValue str);
HmlValue hml_string_char_count(HmlValue str);  // UTF-8 codepoint count
HmlValue hml_string_char_at(HmlValue str, HmlValue index);
HmlValue hml_string_byte_at(HmlValue str, HmlValue index);
HmlValue hml_string_rune_at(HmlValue str, HmlValue index);  // UTF-8 aware rune at char index
HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length);
HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end);
HmlValue hml_string_find(HmlValue str, HmlValue needle);
HmlValue hml_string_contains(HmlValue str, HmlValue needle);
HmlValue hml_string_split(HmlValue str, HmlValue delimiter);
HmlValue hml_string_trim(HmlValue str);
HmlValue hml_string_to_upper(HmlValue str);
HmlValue hml_string_to_lower(HmlValue str);
HmlValue hml_string_starts_with(HmlValue str, HmlValue prefix);
HmlValue hml_string_ends_with(HmlValue str, HmlValue suffix);
HmlValue hml_string_replace(HmlValue str, HmlValue old, HmlValue new_str);
HmlValue hml_string_replace_all(HmlValue str, HmlValue old, HmlValue new_str);
HmlValue hml_string_repeat(HmlValue str, HmlValue count);
HmlValue hml_string_concat_many(HmlValue arr);  // Concatenate array of strings

// String index access (returns rune)
HmlValue hml_string_index(HmlValue str, HmlValue index);
void hml_string_index_assign(HmlValue str, HmlValue index, HmlValue rune);

// String to array conversion
HmlValue hml_string_chars(HmlValue str);   // Returns array of runes
HmlValue hml_string_bytes(HmlValue str);   // Returns array of u8 bytes
HmlValue hml_string_to_bytes(HmlValue str); // Converts string to buffer

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val);
HmlValue hml_array_pop(HmlValue arr);
HmlValue hml_array_shift(HmlValue arr);
void hml_array_unshift(HmlValue arr, HmlValue val);
void hml_array_insert(HmlValue arr, HmlValue index, HmlValue val);
HmlValue hml_array_remove(HmlValue arr, HmlValue index);
HmlValue hml_array_find(HmlValue arr, HmlValue val);
HmlValue hml_array_contains(HmlValue arr, HmlValue val);
HmlValue hml_array_slice(HmlValue arr, HmlValue start, HmlValue end);
HmlValue hml_array_join(HmlValue arr, HmlValue delimiter);
HmlValue hml_array_concat(HmlValue arr1, HmlValue arr2);
void hml_array_reverse(HmlValue arr);
HmlValue hml_array_first(HmlValue arr);
HmlValue hml_array_last(HmlValue arr);
void hml_array_clear(HmlValue arr);
HmlValue hml_array_length(HmlValue arr);

// Array index access
HmlValue hml_array_get(HmlValue arr, HmlValue index);
void hml_array_set(HmlValue arr, HmlValue index, HmlValue val);

// Typed array support
void hml_array_set_element_type(HmlValue arr, HmlValueType element_type);
HmlValue hml_validate_typed_array(HmlValue arr, HmlValueType element_type);

// Higher-order array operations
HmlValue hml_array_map(HmlValue arr, HmlValue callback);
HmlValue hml_array_filter(HmlValue arr, HmlValue predicate);
HmlValue hml_array_reduce(HmlValue arr, HmlValue reducer, HmlValue initial);

// ========== OBJECT OPERATIONS ==========

HmlValue hml_object_get_field(HmlValue obj, const char *field);
void hml_object_set_field(HmlValue obj, const char *field, HmlValue val);
int hml_object_has_field(HmlValue obj, const char *field);
int hml_object_num_fields(HmlValue obj);
HmlValue hml_object_key_at(HmlValue obj, int index);
HmlValue hml_object_value_at(HmlValue obj, int index);
HmlValue hml_object_keys(HmlValue obj);

// ========== SERIALIZATION (JSON) ==========

// Serialize a value to JSON string
HmlValue hml_serialize(HmlValue val);

// Deserialize JSON string to value
HmlValue hml_deserialize(HmlValue json_str);

// ========== MEMORY OPERATIONS ==========

HmlValue hml_alloc(int32_t size);
void hml_free(HmlValue ptr_or_buffer);
HmlValue hml_realloc(HmlValue ptr, int32_t new_size);
void hml_memset(HmlValue ptr, uint8_t byte_val, int32_t size);
void hml_memcpy(HmlValue dest, HmlValue src, int32_t size);
int32_t hml_sizeof_type(HmlValueType type);
HmlValue hml_talloc(HmlValue type_name, HmlValue count);
HmlValue hml_builtin_talloc(HmlClosureEnv *env, HmlValue type_name, HmlValue count);

// Buffer operations
HmlValue hml_buffer_get(HmlValue buf, HmlValue index);
void hml_buffer_set(HmlValue buf, HmlValue index, HmlValue val);
HmlValue hml_buffer_length(HmlValue buf);
HmlValue hml_buffer_capacity(HmlValue buf);

// FFI callback operations
HmlValue hml_callback_create(HmlValue fn, HmlValue arg_types, HmlValue ret_type);
void hml_callback_free(HmlValue callback);

// ========== FUNCTION CALLS ==========

// Global self reference for method calls (thread-local for safety)
extern __thread HmlValue hml_self;

// Call a function value with arguments
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args);

// Call a method on an object (sets hml_self before calling)
HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args);

// ========== EXCEPTION HANDLING ==========

// Exception context using setjmp/longjmp
typedef struct HmlExceptionContext {
    jmp_buf exception_buf;
    HmlValue exception_value;
    int is_active;
    struct HmlExceptionContext *prev;
} HmlExceptionContext;

// Exception stack management
HmlExceptionContext* hml_exception_push(void);
void hml_exception_pop(void);
void hml_throw(HmlValue exception_value);
HmlValue hml_exception_get_value(void);

// Runtime error helper - throws catchable exception with formatted message
void hml_runtime_error(const char *format, ...);

// ========== DEFER SUPPORT ==========

typedef void (*HmlDeferFn)(void *arg);

void hml_defer_push(HmlDeferFn fn, void *arg);
void hml_defer_push_call(HmlValue fn);
void hml_defer_pop_and_execute(void);
void hml_defer_execute_all(void);

// ========== ASYNC/CONCURRENCY ==========

// Task management
HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args);
HmlValue hml_join(HmlValue task);
void hml_detach(HmlValue task);
void hml_task_debug_info(HmlValue task);

// Channels
HmlValue hml_channel(int32_t capacity);
void hml_channel_send(HmlValue channel, HmlValue value);
HmlValue hml_channel_recv(HmlValue channel);
HmlValue hml_channel_recv_timeout(HmlValue channel, HmlValue timeout_ms);
HmlValue hml_channel_send_timeout(HmlValue channel, HmlValue value, HmlValue timeout_ms);
void hml_channel_close(HmlValue channel);
HmlValue hml_select(HmlValue channels, HmlValue timeout);
HmlValue hml_poll(HmlValue fds, HmlValue timeout);

// ========== FILE I/O ==========

HmlValue hml_open(HmlValue path, HmlValue mode);
HmlValue hml_file_read(HmlValue file, HmlValue size);
HmlValue hml_file_read_all(HmlValue file);
HmlValue hml_file_write(HmlValue file, HmlValue data);
HmlValue hml_file_seek(HmlValue file, HmlValue position);
HmlValue hml_file_tell(HmlValue file);
void hml_file_close(HmlValue file);

// ========== FILESYSTEM OPERATIONS ==========

HmlValue hml_exists(HmlValue path);
HmlValue hml_read_file(HmlValue path);
HmlValue hml_write_file(HmlValue path, HmlValue content);
HmlValue hml_append_file(HmlValue path, HmlValue content);
HmlValue hml_remove_file(HmlValue path);
HmlValue hml_rename_file(HmlValue old_path, HmlValue new_path);
HmlValue hml_copy_file(HmlValue src_path, HmlValue dest_path);
HmlValue hml_is_file(HmlValue path);
HmlValue hml_is_dir(HmlValue path);
HmlValue hml_file_stat(HmlValue path);

// ========== DIRECTORY OPERATIONS ==========

HmlValue hml_make_dir(HmlValue path, HmlValue mode);
HmlValue hml_remove_dir(HmlValue path);
HmlValue hml_list_dir(HmlValue path);
HmlValue hml_cwd(void);
HmlValue hml_chdir(HmlValue path);
HmlValue hml_absolute_path(HmlValue path);

// ========== FILESYSTEM BUILTIN WRAPPERS ==========

HmlValue hml_builtin_exists(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_read_file(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_write_file(HmlClosureEnv *env, HmlValue path, HmlValue content);
HmlValue hml_builtin_append_file(HmlClosureEnv *env, HmlValue path, HmlValue content);
HmlValue hml_builtin_remove_file(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_rename(HmlClosureEnv *env, HmlValue old_path, HmlValue new_path);
HmlValue hml_builtin_copy_file(HmlClosureEnv *env, HmlValue src, HmlValue dest);
HmlValue hml_builtin_is_file(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_is_dir(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_file_stat(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_make_dir(HmlClosureEnv *env, HmlValue path, HmlValue mode);
HmlValue hml_builtin_remove_dir(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_list_dir(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_cwd(HmlClosureEnv *env);
HmlValue hml_builtin_chdir(HmlClosureEnv *env, HmlValue path);
HmlValue hml_builtin_absolute_path(HmlClosureEnv *env, HmlValue path);

// ========== SYSTEM INFO OPERATIONS ==========

HmlValue hml_platform(void);
HmlValue hml_arch(void);
HmlValue hml_hostname(void);
HmlValue hml_username(void);
HmlValue hml_homedir(void);
HmlValue hml_cpu_count(void);
HmlValue hml_total_memory(void);
HmlValue hml_free_memory(void);
HmlValue hml_os_version(void);
HmlValue hml_os_name(void);
HmlValue hml_tmpdir(void);
HmlValue hml_uptime(void);

// System info builtin wrappers
HmlValue hml_builtin_platform(HmlClosureEnv *env);
HmlValue hml_builtin_arch(HmlClosureEnv *env);
HmlValue hml_builtin_hostname(HmlClosureEnv *env);
HmlValue hml_builtin_username(HmlClosureEnv *env);
HmlValue hml_builtin_homedir(HmlClosureEnv *env);
HmlValue hml_builtin_cpu_count(HmlClosureEnv *env);
HmlValue hml_builtin_total_memory(HmlClosureEnv *env);
HmlValue hml_builtin_free_memory(HmlClosureEnv *env);
HmlValue hml_builtin_os_version(HmlClosureEnv *env);
HmlValue hml_builtin_os_name(HmlClosureEnv *env);
HmlValue hml_builtin_tmpdir(HmlClosureEnv *env);
HmlValue hml_builtin_uptime(HmlClosureEnv *env);

// ========== COMPRESSION OPERATIONS ==========

HmlValue hml_zlib_compress(HmlValue data, HmlValue level);
HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size);
HmlValue hml_gzip_compress(HmlValue data, HmlValue level);
HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size);
HmlValue hml_zlib_compress_bound(HmlValue source_len);
HmlValue hml_crc32_val(HmlValue data);
HmlValue hml_adler32_val(HmlValue data);

// Compression builtin wrappers
HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level);
HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size);
HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level);
HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size);
HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len);
HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data);
HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data);

// ========== INTERNAL HELPER OPERATIONS ==========

HmlValue hml_read_u32(HmlValue ptr);
HmlValue hml_read_u64(HmlValue ptr);
HmlValue hml_strerror(void);
HmlValue hml_dirent_name(HmlValue ptr);
HmlValue hml_string_to_cstr(HmlValue str);
HmlValue hml_cstr_to_string(HmlValue ptr);

// Internal helper builtin wrappers
HmlValue hml_builtin_read_u32(HmlClosureEnv *env, HmlValue ptr);
HmlValue hml_builtin_read_u64(HmlClosureEnv *env, HmlValue ptr);
HmlValue hml_builtin_strerror(HmlClosureEnv *env);
HmlValue hml_builtin_dirent_name(HmlClosureEnv *env, HmlValue ptr);
HmlValue hml_builtin_string_to_cstr(HmlClosureEnv *env, HmlValue str);
HmlValue hml_builtin_cstr_to_string(HmlClosureEnv *env, HmlValue ptr);

// ========== DNS/NETWORKING OPERATIONS ==========

HmlValue hml_dns_resolve(HmlValue hostname);

// DNS builtin wrapper
HmlValue hml_builtin_dns_resolve(HmlClosureEnv *env, HmlValue hostname);

// ========== SOCKET OPERATIONS ==========

// Socket creation and lifecycle
HmlValue hml_socket_create(HmlValue domain, HmlValue sock_type, HmlValue protocol);
void hml_socket_bind(HmlValue socket_val, HmlValue address, HmlValue port);
void hml_socket_listen(HmlValue socket_val, HmlValue backlog);
HmlValue hml_socket_accept(HmlValue socket_val);
void hml_socket_connect(HmlValue socket_val, HmlValue address, HmlValue port);
void hml_socket_close(HmlValue socket_val);

// Socket I/O
HmlValue hml_socket_send(HmlValue socket_val, HmlValue data);
HmlValue hml_socket_recv(HmlValue socket_val, HmlValue size);
HmlValue hml_socket_sendto(HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data);
HmlValue hml_socket_recvfrom(HmlValue socket_val, HmlValue size);

// Socket options
void hml_socket_setsockopt(HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value);
void hml_socket_set_timeout(HmlValue socket_val, HmlValue seconds);
void hml_socket_set_nonblocking(HmlValue socket_val, HmlValue enable);

// Socket property getters
HmlValue hml_socket_get_fd(HmlValue socket_val);
HmlValue hml_socket_get_address(HmlValue socket_val);
HmlValue hml_socket_get_port(HmlValue socket_val);
HmlValue hml_socket_get_closed(HmlValue socket_val);

// Socket builtin wrappers
HmlValue hml_builtin_socket_create(HmlClosureEnv *env, HmlValue domain, HmlValue sock_type, HmlValue protocol);
HmlValue hml_builtin_socket_bind(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port);
HmlValue hml_builtin_socket_listen(HmlClosureEnv *env, HmlValue socket_val, HmlValue backlog);
HmlValue hml_builtin_socket_accept(HmlClosureEnv *env, HmlValue socket_val);
HmlValue hml_builtin_socket_connect(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port);
HmlValue hml_builtin_socket_close(HmlClosureEnv *env, HmlValue socket_val);
HmlValue hml_builtin_socket_send(HmlClosureEnv *env, HmlValue socket_val, HmlValue data);
HmlValue hml_builtin_socket_recv(HmlClosureEnv *env, HmlValue socket_val, HmlValue size);
HmlValue hml_builtin_socket_sendto(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data);
HmlValue hml_builtin_socket_recvfrom(HmlClosureEnv *env, HmlValue socket_val, HmlValue size);
HmlValue hml_builtin_socket_setsockopt(HmlClosureEnv *env, HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value);
HmlValue hml_builtin_socket_get_fd(HmlClosureEnv *env, HmlValue socket_val);
HmlValue hml_builtin_socket_get_address(HmlClosureEnv *env, HmlValue socket_val);
HmlValue hml_builtin_socket_get_port(HmlClosureEnv *env, HmlValue socket_val);
HmlValue hml_builtin_socket_get_closed(HmlClosureEnv *env, HmlValue socket_val);

// ========== SIGNAL HANDLING ==========

// Maximum signal number supported
#define HML_MAX_SIGNAL 64

// Register a signal handler (handler can be function or null to reset)
// Returns previous handler
HmlValue hml_signal(HmlValue signum, HmlValue handler);

// Raise a signal to the current process
HmlValue hml_raise(HmlValue signum);

// ========== TYPE DEFINITIONS (DUCK TYPING) ==========

// Register a type definition
void hml_register_type(const char *name, HmlTypeField *fields, int num_fields);

// Lookup a type definition by name
HmlTypeDef* hml_lookup_type(const char *name);

// Validate an object against a type definition (duck typing)
// Returns the object with optional fields filled in, or exits on type error
HmlValue hml_validate_object_type(HmlValue obj, const char *type_name);

// ========== CLOSURE SUPPORT ==========

// Closure environment structure
typedef struct HmlClosureEnv {
    HmlValue *captured;     // Array of captured values
    int num_captured;       // Number of captured values
    int ref_count;          // Reference count
} HmlClosureEnv;

// Create a new closure environment with given capacity
HmlClosureEnv* hml_closure_env_new(int num_vars);

// Free a closure environment
void hml_closure_env_free(HmlClosureEnv *env);

// Retain/release closure environment
void hml_closure_env_retain(HmlClosureEnv *env);
void hml_closure_env_release(HmlClosureEnv *env);

// Get captured value from environment
HmlValue hml_closure_env_get(HmlClosureEnv *env, int index);

// Set captured value in environment
void hml_closure_env_set(HmlClosureEnv *env, int index, HmlValue val);

// ========== FFI (Foreign Function Interface) ==========

// FFI type identifiers for argument/return types
typedef enum {
    HML_FFI_VOID,
    HML_FFI_I8, HML_FFI_I16, HML_FFI_I32, HML_FFI_I64,
    HML_FFI_U8, HML_FFI_U16, HML_FFI_U32, HML_FFI_U64,
    HML_FFI_F32, HML_FFI_F64,
    HML_FFI_PTR,
    HML_FFI_STRING,
} HmlFFIType;

// Load a shared library, returns opaque handle
HmlValue hml_ffi_load(const char *path);

// Close a library handle
void hml_ffi_close(HmlValue lib);

// Get a function pointer from a library
void* hml_ffi_sym(HmlValue lib, const char *name);

// Call an FFI function with given arguments and types
// types array contains: [return_type, arg1_type, arg2_type, ...]
HmlValue hml_ffi_call(void *func_ptr, HmlValue *args, int num_args, HmlFFIType *types);

// ========== FFI CALLBACKS ==========

// Opaque handle for FFI callback
typedef struct HmlFFICallback HmlFFICallback;

// Create a C-callable function pointer from a Hemlock function
// param_types: array of HmlFFIType for each parameter
// return_type: HmlFFIType for the return value
// Returns a callback handle (must be freed with hml_ffi_callback_free)
HmlFFICallback* hml_ffi_callback_create(HmlValue fn, HmlFFIType *param_types, int num_params, HmlFFIType return_type);

// Get the C-callable function pointer from a callback handle
void* hml_ffi_callback_ptr(HmlFFICallback *cb);

// Free a callback handle
void hml_ffi_callback_free(HmlFFICallback *cb);

// Free a callback by its code pointer (for cleanup)
int hml_ffi_callback_free_by_ptr(void *ptr);

// Builtin wrappers for callback functions
HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue param_types, HmlValue return_type);
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr);

// Pointer helper functions for FFI
HmlValue hml_builtin_ptr_deref_i32(HmlClosureEnv *env, HmlValue ptr);
HmlValue hml_builtin_ptr_write_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value);
HmlValue hml_builtin_ptr_offset(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue element_size);
HmlValue hml_builtin_ptr_read_i32(HmlClosureEnv *env, HmlValue ptr);

// ========== HTTP/WEBSOCKET FUNCTIONS ==========
// These require libwebsockets at runtime

// HTTP GET request
HmlValue hml_lws_http_get(HmlValue url);

// HTTP POST request
HmlValue hml_lws_http_post(HmlValue url, HmlValue body, HmlValue content_type);

// Get HTTP response status code
HmlValue hml_lws_response_status(HmlValue resp);

// Get HTTP response body
HmlValue hml_lws_response_body(HmlValue resp);

// Get HTTP response headers
HmlValue hml_lws_response_headers(HmlValue resp);

// Free HTTP response
HmlValue hml_lws_response_free(HmlValue resp);

// Get redirect URL from response (if any)
HmlValue hml_lws_response_redirect(HmlValue resp);

// Get response body as binary buffer (preserves null bytes)
HmlValue hml_lws_response_body_binary(HmlValue resp);

// Builtin wrappers for function-as-value
HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url);
HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type);
HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue resp);
HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue resp);
HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue resp);
HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue resp);
HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue resp);
HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue resp);

// WebSocket client functions
HmlValue hml_lws_ws_connect(HmlValue url);
HmlValue hml_lws_ws_send_text(HmlValue conn, HmlValue text);
HmlValue hml_lws_ws_recv(HmlValue conn, HmlValue timeout_ms);
HmlValue hml_lws_ws_close(HmlValue conn);
HmlValue hml_lws_ws_is_closed(HmlValue conn);

// WebSocket message functions
HmlValue hml_lws_msg_type(HmlValue msg);
HmlValue hml_lws_msg_text(HmlValue msg);
HmlValue hml_lws_msg_len(HmlValue msg);
HmlValue hml_lws_msg_free(HmlValue msg);

// WebSocket server functions
HmlValue hml_lws_ws_server_create(HmlValue host, HmlValue port);
HmlValue hml_lws_ws_server_accept(HmlValue server, HmlValue timeout_ms);
HmlValue hml_lws_ws_server_close(HmlValue server);

// WebSocket builtin wrappers
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url);
HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue conn, HmlValue text);
HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue conn, HmlValue timeout_ms);
HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue conn);
HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue conn);
HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue msg);
HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue msg);
HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue msg);
HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue msg);
HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue host, HmlValue port);
HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue server, HmlValue timeout_ms);
HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue server);

// ========== CRYPTOGRAPHIC HASH FUNCTIONS ==========

// Hash functions - return hex string
HmlValue hml_hash_sha256(HmlValue input);
HmlValue hml_hash_sha512(HmlValue input);
HmlValue hml_hash_md5(HmlValue input);

// Builtin wrappers for function-as-value usage
HmlValue hml_builtin_hash_sha256(HmlClosureEnv *env, HmlValue input);
HmlValue hml_builtin_hash_sha512(HmlClosureEnv *env, HmlValue input);
HmlValue hml_builtin_hash_md5(HmlClosureEnv *env, HmlValue input);

// ========== CALL STACK TRACKING ==========

// Maximum call stack depth (matches interpreter's limit)
#define HML_MAX_CALL_DEPTH 1000

// Call depth tracking - called at function entry/exit to detect stack overflow
void hml_call_enter(void);
void hml_call_exit(void);

// Thread-local call depth counter (exposed for inline macros)
extern __thread int hml_g_call_depth;

// Inline call depth tracking macros (faster than function calls)
// Use branch prediction hint - stack overflow is rare
#define HML_CALL_ENTER() do { \
    if (__builtin_expect(++hml_g_call_depth > HML_MAX_CALL_DEPTH, 0)) { \
        hml_g_call_depth = 0; \
        hml_runtime_error("Maximum call stack depth exceeded (infinite recursion?)"); \
    } \
} while(0)

#define HML_CALL_EXIT() do { \
    hml_g_call_depth--; \
} while(0)

// ========== UTILITY MACROS ==========

// Create a string literal value (compile-time optimization)
#define HML_STRING(s) hml_val_string(s)

// Create an integer literal value
#define HML_I32(n) hml_val_i32(n)

// Create a float literal value
#define HML_F64(n) hml_val_f64(n)

// Create a boolean literal value
#define HML_BOOL(b) hml_val_bool(b)

// Create a null value
#define HML_NULL hml_val_null()

#endif // HEMLOCK_RUNTIME_H

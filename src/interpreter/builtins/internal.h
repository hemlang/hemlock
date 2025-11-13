#ifndef BUILTINS_INTERNAL_H
#define BUILTINS_INTERNAL_H

#include "../internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

// Define math constants if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

// Maximum signal number we'll support
#define MAX_SIGNAL 64

// Global signal handler table (defined in signals.c)
extern Function *signal_handlers[MAX_SIGNAL];

// Helper function to get the size of a type (defined in memory.c)
int get_type_size(TypeKind kind);

// Memory management builtins (memory.c)
Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_free(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx);

// Debugging builtins (debugging.c)
Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_panic(Value *args, int num_args, ExecutionContext *ctx);

// Math builtins (math.c)
Value builtin_sin(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cos(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_tan(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_asin(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_acos(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atan(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atan2(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sqrt(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_pow(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exp(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log10(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log2(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_floor(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ceil(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_round(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_trunc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_abs(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_min(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_max(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_clamp(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rand(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rand_range(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_seed(Value *args, int num_args, ExecutionContext *ctx);

// Time builtins (time.c)
Value builtin_now(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_time_ms(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sleep(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_clock(Value *args, int num_args, ExecutionContext *ctx);

// Environment builtins (env.c)
Value builtin_getenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_setenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_unsetenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exit(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_get_pid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exec(Value *args, int num_args, ExecutionContext *ctx);

// Signal handling builtins (signals.c)
Value builtin_signal(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_raise(Value *args, int num_args, ExecutionContext *ctx);

// Concurrency builtins (concurrency.c)
Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_join(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_task_debug_info(Value *args, int num_args, ExecutionContext *ctx);

// Filesystem builtins (filesystem.c)
Value builtin_exists(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_remove_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rename(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_copy_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_is_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_is_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_file_stat(Value *args, int num_args, ExecutionContext *ctx);

// Directory builtins (directories.c)
Value builtin_make_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_remove_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_list_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cwd(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_chdir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_absolute_path(Value *args, int num_args, ExecutionContext *ctx);

// I/O helper builtins (io_helpers.c)
Value builtin_print(Value *args, int num_args, ExecutionContext *ctx);

// Internal helper builtins (internal_helpers.c)
Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx);

#endif // BUILTINS_INTERNAL_H

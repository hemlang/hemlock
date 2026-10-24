/*
 * Hemlock Parser - gpu fn internals shared by the kernel validator /
 * lowering (gpu_kernel.c) and the OpenCL C code generator (gpu_codegen.c).
 */

#ifndef HEMLOCK_PARSER_GPU_INTERNAL_H
#define HEMLOCK_PARSER_GPU_INTERNAL_H

#include "internal.h"

// Diagnostics context for one kernel. `p` is NULL when regenerating a
// kernel that was already validated (e.g. after .hmlc deserialization),
// in which case errors are not reported.
typedef struct {
    Parser *p;
    const char *kernel_name;
    Expr *fn;
    int failed;
    // Position of the innermost statement being checked: the fallback for
    // expressions the parser leaves without a line (some literals).
    int stmt_line;
    int stmt_column;
} KernelCheck;

void kernel_error(KernelCheck *kc, int line, int column, const char *fmt, ...);

int gpu_is_scalar_type(Type *t);
int gpu_is_buffer_type(Type *t);   // gpu_buffer<T> or gpu_local<T>
int gpu_is_local_type(Type *t);    // gpu_local<T>
// The gpu_buffer<T> type of parameter `name`, or NULL if it is not a buffer
Type *gpu_buffer_param(Expr *fn, const char *name);
const char *gpu_scalar_kind_name(TypeKind k);

int gpu_intrinsic_arity(const char *name);
int gpu_is_conversion_intrinsic(const char *name);

// OpenCL C for a validated kernel, plus what the host needs to launch it
// and to report device-side errors.
typedef struct {
    char *msg;          // message template: %v = detail value, %n = buffer length
    const char *detail; // how to print the detail: "none", "i64", "u64", "f32", "f64"
    char *buf;          // buffer named by %n (NULL if none)
    int line;
} GpuSite;

typedef struct {
    char *source;       // complete OpenCL C program
    char *entry;        // __kernel function name
    GpuSite *sites;
    int num_sites;
    int uses_f64;
    char *f64_reason;   // first f64 use, for the "device has no fp64" error
    int uses_groups;    // local_id/group_id/local_size/num_groups/barrier appear
    int uses_barrier;   // barrier() appears: work-group-ordered error keys
} GpuDeviceCode;

// Type-checks the kernel (reporting through kc) and generates OpenCL C.
// Returns 1 on success; on failure `out` is left empty.
int gpu_codegen(KernelCheck *kc, const char *name, Expr *kernel, GpuDeviceCode *out);
void gpu_device_code_free(GpuDeviceCode *dc);

#endif

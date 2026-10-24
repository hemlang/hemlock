# Hemlock GPU Module

GPU compute through OpenCL, bound with FFI. Every step is explicit: you
allocate device memory, copy to and from it, launch kernels, and free what
you allocated. Nothing is transferred, synchronized, or released for you.

## Overview

- **Device discovery**: `gpu_available()`, `gpu_devices()`
- **Contexts**: one device plus one in-order command queue
- **Programs**: built from OpenCL C source. A build failure throws with the compiler log
- **Device buffers**: `ctx.alloc` / `ctx.write` / `ctx.read` / `ctx.free`, plus array helpers
- **Kernel launch**: arguments are marshalled by their Hemlock runtime type

## Requirements

- An OpenCL ICD loader: `libOpenCL.so.1` (Debian/Ubuntu: `ocl-icd-libopencl1`)
- At least one OpenCL driver (ICD):
  - NVIDIA, AMD (ROCm), and Intel GPU drivers each ship one
  - `pocl-opencl-icd` gives a portable **CPU** device. It is useful for development and CI without a GPU

If the loader is present but no driver is installed, `gpu_available()` returns
`false` and `gpu_context()` throws.

## Usage

```hemlock
import { gpu_context } from "@stdlib/gpu";

let ctx = gpu_context();
print("running on " + ctx.device.name);

let prog = ctx.program(`
__kernel void saxpy(uint n, float a, __global const float *x, __global float *y) {
    size_t i = get_global_id(0);
    if (i < n) { y[i] = a * x[i] + y[i]; }
}
`);
let k = prog.kernel("saxpy");

let dx = ctx.from_array([1, 2, 3, 4], "f32");
let dy = ctx.from_array([10, 10, 10, 10], "f32");

// float literals are f64 in Hemlock: the kernel takes `float`, so pass f32()
ctx.launch(k, [4], null, [u32(4), f32(2.0), dx, dy]);
print(ctx.to_array(dy, "f32", 4));   // [12, 14, 16, 18]

ctx.free(dx);
ctx.free(dy);
k.release();
prog.release();
ctx.release();
```

## Kernel Arguments

`ctx.launch` sets each argument from the value's runtime type, so the Hemlock
type you pass must match the kernel's parameter type **exactly**:

| Kernel parameter | Pass |
|---|---|
| `char` / `uchar` | `i8(x)` / `u8(x)` |
| `short` / `ushort` | `i16(x)` / `u16(x)` |
| `int` / `uint` | `i32` (integer literals default to it) / `u32(x)` |
| `long` / `ulong` | `i64(x)` / `u64(x)` |
| `float` | `f32(x)` |
| `double` | `f64` (float literals default to it). The device must support fp64 |
| `__global T *` | a device buffer from `ctx.alloc` / `ctx.from_array` |
| `__local T *` | `local_mem(bytes)` |

A size mismatch throws a message that names the argument. For example,
passing `2.0` (f64) to a `float` parameter says so and suggests `f32(2.0)`.
Hemlock never narrows values implicitly.

## API

### `gpu_available(): bool`
`true` if at least one OpenCL platform exposes at least one device.

### `gpu_devices(): array`
One descriptor per device on every platform:
`{ name, vendor, version, platform, type, compute_units, max_work_group_size, global_mem, local_mem, has_f64 }`.
`type` is one of `"gpu"`, `"cpu"`, `"accelerator"`, `"other"`.

### `gpu_context(opts?)`
Creates a context and an in-order command queue on one device.
- `opts.device`: a descriptor from `gpu_devices()`
- `opts.device_type`: `DEVICE_GPU` (default), `DEVICE_CPU`, `DEVICE_ACCELERATOR`, or `DEVICE_ALL`

With no device given, the first GPU is used, falling back to the first device of any type.

The context object has the methods below.

| Method | Description |
|---|---|
| `program(source, options?)` | Build OpenCL C source and return a program. Throws with the build log on failure |
| `alloc(bytes, flags?)` | Uninitialized device buffer (`MEM_READ_WRITE` by default) |
| `free(dbuf)` | Release a device buffer. A double free throws |
| `write(dbuf, src, bytes?)` | Blocking host→device copy. `src` is a `buffer` (size defaults to its length) or a `ptr` (needs `bytes`) |
| `read(dbuf, dst, bytes?)` | Blocking device→host copy, same rules as `write` |
| `from_array(arr, elem)` | Allocate and upload an array as `elem` (`"i8"`…`"u64"`, `"f32"`, `"f64"`) |
| `to_array(dbuf, elem, count)` | Download `count` elements into a new array |
| `launch(kernel, global, local, args)` | Set the arguments, enqueue over a 1-3 D range, and wait. `local` may be `null` |
| `finish()` | Wait for all queued work |
| `release()` | Release the queue and context. Free buffers, kernels, and programs first |

A program has `kernel(name)` and `release()`. A kernel has `name` and `release()`.

### `local_mem(bytes)`
Size-only marker for a `__local` kernel argument.

### `cl_error_name(code): string`
Symbolic name for an OpenCL status code.

## Limitations (current)

- `launch` is synchronous. It blocks until the kernel finishes. Async launches mapped onto Hemlock tasks are planned
- Linux only (`libOpenCL.so.1`)

## `gpu fn` kernels

Kernels can be written in Hemlock with `gpu fn`. Their bodies are a statically
typed subset of Hemlock with no device runtime. The same kernel runs on a
device through `ctx.launch` and on the CPU through
[`@stdlib/gpu_emu`](gpu_emu.md), with the same results:

```hemlock
import { gpu_context } from "@stdlib/gpu";

gpu fn saxpy(n: u32, a: f32, x: gpu_buffer<f32>, y: gpu_buffer<f32>) {
    let i: u32 = global_id(0);
    if (i < n) {
        y[i] = a * x[i] + y[i];
    }
}

let ctx = gpu_context();
let dx = ctx.from_array([1, 2, 3, 4], "f32");
let dy = ctx.from_array([10, 10, 10, 10], "f32");
ctx.launch(saxpy, [4], null, [u32(4), f32(2.0), dx, dy]);
print(ctx.to_array(dy, "f32", 4));   // [12, 14, 16, 18]
```

### The kernel subset

The frontend checks the subset, so the interpreter, `hemlockc`, and
`hemlock check` all reject the same programs:

- Parameters are numeric or `bool` scalars, `gpu_buffer<T>` (global memory) or `gpu_local<T>` (work-group local memory) of a numeric `T`. Each one must be annotated, with no `ref`, defaults, or rest parameters
- The return type is omitted or `void`. Results are written into buffers
- Every local is annotated with a scalar type and initialized
- Allowed: control flow (`if`, `while`, `for`, `loop`, `switch`, labeled `break`/`continue`), arithmetic, and indexing of `gpu_buffer` parameters (`x[i]`, `x[i] = v`, `x[i] += v`). A buffer can only be indexed, never used bare or reassigned
- Assignments, buffer stores and `++`/`--` are statements. They cannot appear inside a larger expression
- Expressions are statically typed with Hemlock's promotion rules. Arithmetic needs numeric operands (no `bool`), bitwise ops need integers, ternary branches and `min`/`max` operands must have the same type, and `switch` takes an integer value with integer-literal cases
- Calls are limited to device intrinsics: `global_id`, `local_id`, `group_id`, `global_size`, `local_size`, `num_groups`, `work_dim`, `barrier` (a statement), `min`, `max`, `abs`, `sqrt`, `floor`, `ceil`, `exp`, `log`, `sin`, `cos`, `pow`, and the conversions `i8()` … `u64()`, `f32()`, `f64()`. See [gpu_emu.md](gpu_emu.md#kernel-intrinsics) for return types
- Rejected: strings, runes, `null`, property access, method calls, closures, nested functions, array/object literals, `match`, `for-in`, `try`/`throw`, `defer`, `await`, recursion, named arguments, and names starting with `__hml_`

`gpu` is a contextual keyword. It only means something directly before `fn`,
so `gpu` still works as a variable name.

### Kernel descriptors

The name a `gpu fn` declares is bound to a **kernel descriptor**, not a function:

```hemlock
print(saxpy.kind);            // "gpu_kernel"
print(saxpy.params);          // [{ name: "n", type: "u32", buffer: false }, ...]
print(saxpy.device.source);   // the generated OpenCL C program
// saxpy.emulate: the kernel lowered to plain Hemlock, used by @stdlib/gpu_emu
```

### Launching on a device

`ctx.launch(kernel, global, local, args)` accepts a descriptor. The arguments
follow the emulator's rules: each scalar must have **exactly** the parameter's
type, and each `gpu_buffer<T>` takes a device buffer. Buffers made by
`from_array` remember their element type, and a mismatch is an error. A
buffer's element count (its size divided by `T`'s size) is passed to the kernel
alongside it, so every access is bounds-checked.

The program is built on first launch and cached on the context. When the
device supports it, it is built with `-cl-fp32-correctly-rounded-divide-sqrt`.

Requirements beyond plain OpenCL C kernels:

- **`cl_khr_int64_base_atomics`**: kernels report errors with 64-bit atomics
- **fp64**, when the kernel computes with `f64`. `descriptor.device.uses_f64`
  says whether it does, and `f64_reason` names the first use. Remember that
  float literals are `f64` and integer `/` returns `f64`, so `x * 2.0` and
  `n / 2` both need fp64. Write `x * f32(2.0)`, and convert operands with
  `f32()` for f32 division. A device without fp64 fails with an error naming
  that line
- An explicit `local` size when the kernel uses `barrier`, `local_id`,
  `group_id`, `local_size` or `num_groups` (`descriptor.device.uses_groups`).
  The emulator enforces the same rule

### Work-group memory and barriers

A `gpu_local<T>` parameter is memory shared by the work-items of one
work-group. Pass `local_array(count)` for it, which gives each work-group its own
`count` elements. It is bounds-checked like a `gpu_buffer`. `barrier()` waits
until every work-item of the work-group has reached it, and makes their
local and global memory writes visible to each other:

```hemlock
import { gpu_context, local_array } from "@stdlib/gpu";

gpu fn block_sum(xs: gpu_buffer<i32>, tmp: gpu_local<i32>, sums: gpu_buffer<i32>) {
    let l: u32 = local_id(0);
    tmp[l] = xs[global_id(0)];
    barrier();
    for (let s: u32 = local_size(0) / u32(2); s > 0; s = s >> 1) {
        if (l < s) {
            tmp[l] += tmp[l + s];
        }
        barrier();
    }
    if (l == 0) {
        sums[group_id(0)] = tmp[0];
    }
}

ctx.launch(block_sum, [n], [64], [dx, local_array(64), dsums]);
```

Rules (the same as OpenCL's):

- Every work-item of a work-group must reach the same barriers in the same
  order. Only put a barrier under a condition or loop whose outcome is the
  same for the whole work-group (for example, a loop bound computed from
  `local_size`). On a device, breaking this rule is undefined behavior,
  usually a hang. `@stdlib/gpu_emu` detects it and reports a barrier
  divergence, so run new kernels under the emulator first
- Local memory starts undefined on a device; the emulator zero-fills it.
  Write before you read
- Work-groups of a kernel that uses `barrier()` hold at most 4096 work-items

Errors in a kernel that uses barriers stop the whole work-group at its next
barrier (a failing work-item cannot simply return, or its group would wait
for it forever). The reported error is still the one the emulator stops at:
the lowest-numbered failing work-item of the first failing work-group.

### Semantics and errors

Device code follows Hemlock's rules rather than C's, so results match the
emulator. This covers type promotion, i32/i64 overflow checks, wrapping
narrow and unsigned arithmetic, `/` always producing a float, `%` and
shift-count rules, range-checked conversions, and `switch` value equality.
A generated differential test (`tests/stdlib_gpu/test_codegen_diff.hml`)
holds the device and the emulator to this across every operator, type pair
and conversion.

An error in a kernel (overflow, division by zero, a bad index, a failed
conversion) stops that work-item. `launch` then throws the error of the
**lowest-numbered** failing work-item, which is the one the emulator stops
at, in the same words:

```
gpu fn 'fill' work-item (0, 0, 0): out[4] is out of bounds (length 2) (line 12)
```

Differences from the emulator:

- Other work-items still run on the device, so buffer contents after a
  failed launch differ from the emulator's, which stops at the first error
- An error's detail value (the bad index, the out-of-range value) is shown
  when it was captured, and as `?` otherwise. It is captured for the first
  64 new-lowest errors, which in practice includes the reported one
- `exp`, `log`, `sin`, `cos` and `pow` may differ in the last bit. They are
  computed through f64 when the device has fp64, which matches the emulator
  far more often than single precision
- Racy kernels (work-items writing the same element) are deterministic only
  in the emulator
- Devices that flush denormals to zero can differ on denormal values
- Barrier divergence is detected by the emulator only; on a device it is
  undefined behavior

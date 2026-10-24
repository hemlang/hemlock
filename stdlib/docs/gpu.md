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
- Kernels are OpenCL C source strings. `gpu fn` kernels written in Hemlock parse and validate (see below), but they are not compiled to device code yet
- Linux only (`libOpenCL.so.1`)

## `gpu fn` (preview)

Hemlock can declare kernels with `gpu fn`. Their bodies are a statically typed
subset of Hemlock with no device runtime:

```hemlock
gpu fn saxpy(n: u32, a: f32, x: gpu_buffer<f32>, y: gpu_buffer<f32>) {
    let i: u32 = global_id(0);
    if (i < n) {
        y[i] = a * x[i] + y[i];
    }
}
```

The frontend enforces the subset, so the interpreter, `hemlockc`, and
`hemlock check` all reject the same programs:

- Parameters are numeric or `bool` scalars, or `gpu_buffer<T>` of a numeric `T`. Each one must be annotated, with no `ref`, defaults, or rest parameters
- The return type is omitted or `void`. Results are written into buffers
- Every local is annotated with a scalar type
- Allowed: control flow (`if`, `while`, `for`, `loop`, `switch`, `break`, `continue`), arithmetic, indexing, `++`/`--`, and calls to plain named functions such as `global_id(0)`
- Rejected: strings, runes, `null`, property access, method calls, closures, nested functions, array/object literals, `match`, `for-in`, `try`/`throw`, `defer`, `await`, recursion, named arguments

`gpu` is a contextual keyword. It only means something directly before `fn`,
so `gpu` still works as a variable name.

Device code generation is not implemented yet. Until it is, the name a `gpu fn`
declares is bound to a host stub that throws when called:

```
gpu fn 'saxpy' is a device kernel and cannot be called from host code
```

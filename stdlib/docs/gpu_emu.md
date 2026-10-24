# Hemlock GPU Emulator Module

Runs `gpu fn` kernels on the CPU. You can write, test, and debug kernels
without a GPU or an OpenCL install, on the interpreter and on `hemlockc`.
Both backends produce identical results.

## Overview

A `gpu fn` declaration binds a **kernel descriptor**. Its `emulate` field
holds the kernel lowered to ordinary Hemlock by the frontend. `emulate()`
runs that function once per work-item over a 1-, 2-, or 3-D range.

This module is pure Hemlock and needs no native libraries.

## Usage

```hemlock
import { emulate } from "@stdlib/gpu_emu";

gpu fn saxpy(n: u32, a: f32, x: gpu_buffer<f32>, y: gpu_buffer<f32>) {
    let i: u32 = global_id(0);
    if (i < n) {
        y[i] = a * x[i] + y[i];
    }
}

let xs = [1, 2, 3, 4];
let ys = [10, 10, 10, 10];
emulate(saxpy, [4], null, [u32(4), f32(2.0), xs, ys]);
print(ys);   // [12, 14, 16, 18]
```

## API

### `emulate(kernel, global, local, args)`
Runs `kernel` over an N-D range and returns `null`. Buffers are modified in place.

- `global`: `[x]`, `[x, y]` or `[x, y, z]`, with positive integer work-item counts
- `local`: a work-group size of the same rank, where each entry must divide the matching `global` entry. Pass `null` for one work-group spanning the whole range
- `args`: the kernel arguments in order. Pass a Hemlock array for each `gpu_buffer<T>` parameter, and a scalar of **exactly** the declared type for each scalar parameter

### `is_kernel(value): bool`
`true` if `value` is a `gpu fn` kernel descriptor.

## Semantics

The emulator defines the reference semantics for `gpu fn` kernels:

| Aspect | Behavior |
|---|---|
| Order | Work-items run one at a time: x fastest, then y, then z. Racy kernels are deterministic here, but they will not be on a GPU |
| Arithmetic | Hemlock rules: i32/i64 overflow throws, unsigned and small types wrap |
| Buffer access | Bounds-checked. An out-of-bounds index throws, and buffers never grow |
| Element types | Loads and stores convert to the buffer's declared `T`, like a C store. Stored values become `T` in the array |
| Scalar args | Must match the parameter type exactly (`f32(2.0)`, not `2.0`). This is the same rule `@stdlib/gpu` enforces for real devices |
| Errors | The first error stops the launch and is rethrown with its work-item, e.g. `gpu fn 'fill' work-item (0, 0, 0): out[4] is out of bounds (length 2)` |

## Kernel Intrinsics

| Intrinsic | Returns |
|---|---|
| `global_id(d)`, `local_id(d)`, `group_id(d)` | `u32`. Dimensions past `work_dim()` return 0 |
| `global_size(d)`, `local_size(d)`, `num_groups(d)` | `u32`. Dimensions past `work_dim()` return 1 |
| `work_dim()` | `u32` |
| `min(a, b)`, `max(a, b)`, `abs(x)` | the operand's type |
| `sqrt`, `floor`, `ceil`, `exp`, `log`, `sin`, `cos` (1 arg), `pow(a, b)` | `f32` for an `f32` argument, otherwise `f64` |
| `i8()` … `u64()`, `f32()`, `f64()` | explicit conversions |

There is no `barrier()` yet. Running work-items sequentially cannot honor a
barrier, and kernels have no `__local` memory yet.

## Testing Kernels Against a Device

`tests/stdlib_gpu/test_emulator_matches_device.hml` runs one computation as a
`gpu fn` under the emulator and as OpenCL C through `@stdlib/gpu`, then checks
that the results are equal. Until `gpu fn` compiles to device code, that is
the pattern for validating a kernel on real hardware.

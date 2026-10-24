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
- `local`: a work-group size of the same rank, where each entry must divide the matching `global` entry. Pass `null` for one work-group spanning the whole range. Kernels that use `local_id`, `group_id`, `local_size` or `num_groups` must pass an explicit size, as they must on a device
- `args`: the kernel arguments in order. Pass a Hemlock array for each `gpu_buffer<T>` parameter, `local_array(count)` for each `gpu_local<T>` parameter, and a scalar of **exactly** the declared type for each scalar parameter

### `local_array(count)`
Work-group local memory of `count` elements for a `gpu_local<T>` parameter.
Each work-group gets its own zero-filled array. On a device, local memory
starts undefined.

### `is_kernel(value): bool`
`true` if `value` is a `gpu fn` kernel descriptor.

## Semantics

The emulator defines the reference semantics for `gpu fn` kernels. Kernels
compiled for a device through `@stdlib/gpu` match it, including which work-item
an error is reported for (see [gpu.md](gpu.md#semantics-and-errors) for the
small remaining differences):

| Aspect | Behavior |
|---|---|
| Order | Work-items run one at a time: x fastest, then y, then z. Racy kernels are deterministic here, but they will not be on a GPU |
| Arithmetic | Hemlock rules: i32/i64 overflow throws, unsigned and small types wrap |
| Buffer access | Bounds-checked. An out-of-bounds index throws, and buffers never grow |
| Element types | Loads and stores convert to the buffer's declared `T`, like a C store. Stored values become `T` in the array |
| Scalar args | Must match the parameter type exactly (`f32(2.0)`, not `2.0`). This is the same rule `@stdlib/gpu` enforces for real devices |
| Errors | The first error stops the launch and is rethrown with its work-item, e.g. `gpu fn 'fill' work-item (0, 0, 0): out[4] is out of bounds (length 2)` |
| Barriers | Kernels that call `barrier()` run work-group by work-group. Within a group, each work-item runs (one at a time, in local order) up to its next barrier, then the next round starts. Every work-item must reach the same barrier: otherwise the run stops with a barrier divergence error naming the two work-items and where each stopped |

## Kernel Intrinsics

| Intrinsic | Returns |
|---|---|
| `global_id(d)`, `local_id(d)`, `group_id(d)` | `u32`. Dimensions past `work_dim()` return 0 |
| `global_size(d)`, `local_size(d)`, `num_groups(d)` | `u32`. Dimensions past `work_dim()` return 1 |
| `work_dim()` | `u32` |
| `min(a, b)`, `max(a, b)`, `abs(x)` | the operand's type |
| `sqrt`, `floor`, `ceil`, `exp`, `log`, `sin`, `cos` (1 arg), `pow(a, b)` | `f32` for an `f32` argument, otherwise `f64` |
| `i8()` … `u64()`, `f32()`, `f64()` | explicit conversions |
| `barrier()` | a statement: waits for the whole work-group |

Barrier kernels run each work-item of a work-group as a task that is resumed
one at a time, so they are deterministic but slower than kernels without
barriers, which run as plain loops.

## Testing Kernels Against a Device

The same descriptor runs under `emulate()` and under `@stdlib/gpu`'s
`ctx.launch`, so a kernel can be checked on real hardware by running both and
comparing. `tests/stdlib_gpu/diff_harness.hml` does that, including matching
the errors, and the differential tests next to it use it.

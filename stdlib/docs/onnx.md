# Hemlock ONNX Runtime Module

A standard library module for running ONNX models locally via ONNX Runtime FFI. Enables local embeddings, classification, and general ML inference without API calls.

## Overview

The ONNX module provides:

- **Session management** — Load ONNX models, configure threading and optimization
- **Tensor operations** — Create input tensors, read output tensors (f32, f64, i32, i64)
- **Inference** — Run models with named or positional inputs
- **Model inspection** — Query input/output names, shapes, and types
- **Resource management** — Explicit cleanup of sessions and tensors

## Usage

```hemlock
import { create_session, run, create_tensor_f32, tensor_data_f32,
         free_tensor, free_session } from "@stdlib/onnx";

// Load an ONNX model
let session = create_session("model.onnx");

// Create an input tensor
let input = create_tensor_f32([1.0, 2.0, 3.0, 4.0], [1, 4]);

// Run inference
let outputs = run(session, { "input": input });

// Read results
let result = tensor_data_f32(outputs[0]);
print(result);  // e.g., [0.8, 0.1, 0.05, 0.05]

// Cleanup
free_tensor(outputs[0]);
free_tensor(input);
free_session(session);
```

Or import all:

```hemlock
import * as onnx from "@stdlib/onnx";

let session = onnx.create_session("model.onnx");
// ...
onnx.free_session(session);
```

---

## Session Lifecycle

### create_session(model_path, log_level?, intra_threads?, inter_threads?, optimization?)

Loads an ONNX model and creates an inference session.

**Parameters:**
- `model_path: string` — Path to the `.onnx` model file (required)
- `log_level: i32` — Logging verbosity (default: `LOG_WARNING`)
- `intra_threads: i32` — Intra-op parallelism threads (default: `0` = auto)
- `inter_threads: i32` — Inter-op parallelism threads (default: `0` = auto)
- `optimization: i32` — Graph optimization level (default: `OPT_ALL`)

**Returns:** `Session`

**Throws:** On invalid model path, corrupt model, or initialization failure

```hemlock
import { create_session, free_session,
         LOG_ERROR, OPT_BASIC } from "@stdlib/onnx";

// Simple — sane defaults
let session = create_session("model.onnx");

// Full control
let session2 = create_session("model.onnx",
    log_level: LOG_ERROR,
    intra_threads: 4,
    inter_threads: 2,
    optimization: OPT_BASIC);

free_session(session);
free_session(session2);
```

### free_session(session)

Releases all resources (environment, session, options, memory info).

**Parameters:**
- `session: Session` — Session to free

**Returns:** `null`

Calling `free_session` on an already-freed session is safe (no-op).

---

## Model Inspection

### input_count(session) / output_count(session)

Get the number of model inputs/outputs.

**Returns:** `i64`

### input_names(session) / output_names(session)

Get all input/output names as an array of strings.

**Returns:** `array` of `string`

### input_shape(session, idx) / output_shape(session, idx)

Get the shape of an input/output tensor by index.

**Parameters:**
- `session: Session`
- `idx: i64` — 0-based index

**Returns:** `array` of `i64` — Dimension sizes (dynamic dimensions may be `-1`)

### input_type(session, idx) / output_type(session, idx)

Get the element type of an input/output tensor.

**Returns:** `i32` — One of the `DTYPE_*` constants

### describe(session)

Print a human-readable summary of the model's inputs and outputs.

```hemlock
import { create_session, describe, free_session } from "@stdlib/onnx";

let session = create_session("model.onnx");
describe(session);
// Output:
// Model inputs (1):
//   [0] input: float32 [1, 4]
// Model outputs (1):
//   [0] output: float32 [1, 2]
free_session(session);
```

---

## Tensor Creation

### create_tensor_f32(data, shape)

Creates a float32 tensor from a flat data array and shape.

**Parameters:**
- `data: array` — Flat array of numeric values
- `shape: array` — Array of dimension sizes (e.g., `[1, 3, 224, 224]`)

**Returns:** `Tensor`

**Throws:** If `data.length` doesn't match the product of `shape` dimensions

```hemlock
import { create_tensor_f32, free_tensor } from "@stdlib/onnx";

// 1D tensor
let t1 = create_tensor_f32([1.0, 2.0, 3.0], [3]);

// 2D tensor (batch of 1, 4 features)
let t2 = create_tensor_f32([1.0, 2.0, 3.0, 4.0], [1, 4]);

// 4D tensor (batch=1, channels=3, height=2, width=2)
let t3 = create_tensor_f32(
    [0.1, 0.2, 0.3, 0.4,
     0.5, 0.6, 0.7, 0.8,
     0.9, 1.0, 1.1, 1.2],
    [1, 3, 2, 2]);

free_tensor(t1);
free_tensor(t2);
free_tensor(t3);
```

### create_tensor_f64(data, shape)

Creates a float64 (double) tensor. Same interface as `create_tensor_f32`.

### create_tensor_i32(data, shape)

Creates an int32 tensor. Same interface.

### create_tensor_i64(data, shape)

Creates an int64 tensor. Same interface.

```hemlock
import { create_tensor_i64, free_tensor } from "@stdlib/onnx";

// Token IDs for a language model
let input_ids = create_tensor_i64([101, 2054, 2003, 1996, 3007, 102], [1, 6]);
free_tensor(input_ids);
```

### free_tensor(tensor)

Frees tensor resources. Safe to call multiple times (no-op after first).

---

## Tensor Inspection

### tensor_shape(tensor)

Get the tensor's shape.

**Returns:** `array` of `i64`

### tensor_element_count(tensor)

Get the total number of elements.

**Returns:** `i64`

### tensor_type(tensor)

Get the element data type.

**Returns:** `i32` — One of the `DTYPE_*` constants

### tensor_data_f32(tensor) / tensor_data_f64(tensor)

Read tensor data as an array of float values.

**Returns:** `array`

### tensor_data_i32(tensor) / tensor_data_i64(tensor)

Read tensor data as an array of integer values.

**Returns:** `array`

```hemlock
import { create_tensor_f32, tensor_shape, tensor_element_count,
         tensor_type, tensor_data_f32, free_tensor, DTYPE_FLOAT } from "@stdlib/onnx";

let t = create_tensor_f32([1.0, 2.0, 3.0, 4.0], [2, 2]);
print(tensor_shape(t));          // [2, 2]
print(tensor_element_count(t));  // 4
print(tensor_type(t));           // 1 (DTYPE_FLOAT)
print(tensor_data_f32(t));       // [1.0, 2.0, 3.0, 4.0]
free_tensor(t);
```

---

## Inference

### run(session, inputs)

Run model inference.

**Parameters:**
- `session: Session` — Loaded model session
- `inputs` — Either:
  - **Object** mapping input names to Tensors: `{ "input": tensor }`
  - **Array** of Tensors in model input order: `[tensor]`

**Returns:** `array` of `Tensor` — Output tensors (caller must `free_tensor()` each)

**Throws:** On inference error, missing inputs, or input count mismatch

```hemlock
import { create_session, run, create_tensor_f32, tensor_data_f32,
         free_tensor, free_session } from "@stdlib/onnx";

let session = create_session("classifier.onnx");

let features = create_tensor_f32([5.1, 3.5, 1.4, 0.2], [1, 4]);

// Named inputs (recommended)
let outputs = run(session, { "input": features });

// Positional inputs (alternative)
let outputs2 = run(session, [features]);

let probabilities = tensor_data_f32(outputs[0]);
print("Class probabilities:", probabilities);

free_tensor(outputs[0]);
free_tensor(outputs2[0]);
free_tensor(features);
free_session(session);
```

---

## Utility Functions

### version()

Get the ONNX Runtime library version string.

**Returns:** `string` (e.g., `"1.17.0"`)

### api_version()

Get the ORT API version this wrapper was built against.

**Returns:** `i32`

### dtype_name(dtype)

Get a human-readable name for a `DTYPE_*` constant.

**Returns:** `string` (e.g., `"float32"`, `"int64"`)

```hemlock
import { version, api_version, dtype_name, DTYPE_FLOAT } from "@stdlib/onnx";

print("ONNX Runtime:", version());
print("API version:", api_version());
print(dtype_name(DTYPE_FLOAT));  // "float32"
```

---

## Constants

### Logging Levels

```hemlock
LOG_VERBOSE  // 0 - All messages
LOG_INFO     // 1 - Info and above
LOG_WARNING  // 2 - Warnings and above (default)
LOG_ERROR    // 3 - Errors only
LOG_FATAL    // 4 - Fatal errors only
```

### Graph Optimization Levels

```hemlock
OPT_DISABLE   // 0 - No optimization
OPT_BASIC     // 1 - Constant folding, redundant node elimination
OPT_EXTENDED  // 2 - Node fusions, etc.
OPT_ALL       // 99 - All available optimizations (default)
```

### Tensor Element Types

```hemlock
DTYPE_UNDEFINED  // 0
DTYPE_FLOAT      // 1 - 32-bit float
DTYPE_UINT8      // 2 - 8-bit unsigned int
DTYPE_INT8       // 3 - 8-bit signed int
DTYPE_UINT16     // 4 - 16-bit unsigned int
DTYPE_INT16      // 5 - 16-bit signed int
DTYPE_INT32      // 6 - 32-bit signed int
DTYPE_INT64      // 7 - 64-bit signed int
DTYPE_STRING     // 8 - String
DTYPE_BOOL       // 9 - Boolean
DTYPE_FLOAT16    // 10 - IEEE half precision
DTYPE_DOUBLE     // 11 - 64-bit double
DTYPE_UINT32     // 12 - 32-bit unsigned int
DTYPE_UINT64     // 13 - 64-bit unsigned int
```

---

## Examples

### Image Classification

```hemlock
import { create_session, run, create_tensor_f32, tensor_data_f32,
         free_tensor, free_session } from "@stdlib/onnx";
import { read_file } from "@stdlib/fs";

// Load a pre-trained image classifier (e.g., MobileNet)
let session = create_session("mobilenet_v2.onnx", intra_threads: 4);

// Prepare input (1x3x224x224 normalized image tensor)
// In practice, you'd load and preprocess an image here
let pixels = [];
let i = 0;
while (i < 1 * 3 * 224 * 224) {
    pixels.push(0.5);  // placeholder
    i = i + 1;
}
let input = create_tensor_f32(pixels, [1, 3, 224, 224]);

// Run inference
let outputs = run(session, [input]);
let logits = tensor_data_f32(outputs[0]);

// Find argmax
let max_idx = 0;
let max_val = logits[0];
i = 1;
while (i < logits.length) {
    if (logits[i] > max_val) {
        max_val = logits[i];
        max_idx = i;
    }
    i = i + 1;
}
print("Predicted class:", max_idx, "score:", max_val);

free_tensor(outputs[0]);
free_tensor(input);
free_session(session);
```

### Text Embeddings

```hemlock
import { create_session, run, create_tensor_i64, tensor_data_f32,
         free_tensor, free_session } from "@stdlib/onnx";

// Load a sentence embedding model (e.g., all-MiniLM-L6-v2)
let session = create_session("embedding_model.onnx");

// Tokenized input (from a tokenizer)
let input_ids = create_tensor_i64([101, 2054, 2003, 1996, 3007, 102], [1, 6]);
let attention_mask = create_tensor_i64([1, 1, 1, 1, 1, 1], [1, 6]);

// Run inference
let outputs = run(session, {
    "input_ids": input_ids,
    "attention_mask": attention_mask,
});

// Get embedding vector
let embedding = tensor_data_f32(outputs[0]);
print("Embedding dimensions:", embedding.length);
print("First 5 values:", embedding[0], embedding[1], embedding[2], embedding[3], embedding[4]);

// Cleanup
let i = 0;
while (i < outputs.length) {
    free_tensor(outputs[i]);
    i = i + 1;
}
free_tensor(input_ids);
free_tensor(attention_mask);
free_session(session);
```

### Model Inspection

```hemlock
import { create_session, describe, input_count, output_count,
         input_names, output_names, input_shape, output_shape,
         input_type, dtype_name, free_session } from "@stdlib/onnx";

let session = create_session("model.onnx");

// Quick overview
describe(session);

// Detailed inspection
let in_names = input_names(session);
let i = 0;
while (i < in_names.length) {
    let shape = input_shape(session, i);
    let dtype = input_type(session, i);
    print("Input '" + in_names[i] + "': " + dtype_name(dtype) + " " + shape);
    i = i + 1;
}

free_session(session);
```

---

## Error Handling

All functions throw exceptions on errors:

```hemlock
import { create_session, run, create_tensor_f32,
         free_tensor, free_session } from "@stdlib/onnx";

// Model loading errors
try {
    let session = create_session("nonexistent.onnx");
} catch (e) {
    print("Load error:", e);
}

// Inference errors
try {
    let session = create_session("model.onnx");
    // Wrong shape
    let bad_input = create_tensor_f32([1.0], [1, 1]);
    let outputs = run(session, [bad_input]);
} catch (e) {
    print("Inference error:", e);
}

// Data/shape mismatch
try {
    let t = create_tensor_f32([1.0, 2.0, 3.0], [2, 2]);  // 3 != 2*2
} catch (e) {
    print("Tensor error:", e);
}
```

---

## Memory Management

Hemlock's explicit memory management applies to ONNX tensors and sessions:

1. **Always free sessions** with `free_session()` when done
2. **Always free tensors** with `free_tensor()` — both input and output tensors
3. **Output tensors from `run()` are owned by the caller** — you must free each one
4. **Input tensor data must remain valid** until after `run()` completes
5. **Double-free is safe** — calling `free_tensor()` or `free_session()` twice is a no-op

```hemlock
import { create_session, run, create_tensor_f32,
         free_tensor, free_session } from "@stdlib/onnx";

let session = create_session("model.onnx");
let input = create_tensor_f32([1.0, 2.0, 3.0, 4.0], [1, 4]);

let outputs = run(session, [input]);

// Process outputs...
let result = tensor_data_f32(outputs[0]);

// Free everything
let i = 0;
while (i < outputs.length) {
    free_tensor(outputs[i]);
    i = i + 1;
}
free_tensor(input);
free_session(session);
```

---

## Performance Tips

1. **Reuse sessions** — Creating a session loads and optimizes the model. Create once, run many times.
2. **Set thread counts** — Use `intra_threads` and `inter_threads` for CPU parallelism.
3. **Use `OPT_ALL`** — Graph optimization (default) can significantly speed up inference.
4. **Batch inputs** — Use batch dimension > 1 when processing multiple inputs.
5. **Match tensor types** — Use the correct `create_tensor_*` for the model's expected input type.

---

## Architecture

The module uses a two-layer architecture:

```
Hemlock program
    ↓
@stdlib/onnx (onnx.hml)      ← Pure Hemlock: high-level API
    ↓ FFI
libhemlock_onnx.so            ← C wrapper: flattens OrtApi struct
    ↓
libonnxruntime.so             ← ONNX Runtime: ML inference engine
```

The C wrapper (`stdlib/c/onnx_wrapper.c`) is needed because ONNX Runtime's C API uses a struct-of-function-pointers pattern (`OrtApi`) that can't be called directly through Hemlock's FFI. The wrapper exposes flat C functions that Hemlock can bind via `extern fn`.

---

## System Requirements

- **ONNX Runtime** shared library (`libonnxruntime.so`)
- **Hemlock ONNX wrapper** (`libhemlock_onnx.so`)

### Installing ONNX Runtime

**From GitHub releases (recommended):**
```bash
# Download the latest release
wget https://github.com/microsoft/onnxruntime/releases/download/v1.17.0/onnxruntime-linux-x64-1.17.0.tgz
tar xzf onnxruntime-linux-x64-1.17.0.tgz
cd onnxruntime-linux-x64-1.17.0

# Install
sudo cp lib/libonnxruntime.so* /usr/local/lib/
sudo cp -r include/* /usr/local/include/
sudo ldconfig
```

**On macOS:**
```bash
brew install onnxruntime
```

### Building the wrapper

```bash
# From hemlock root directory
make stdlib

# Or manually:
gcc -shared -fPIC -o stdlib/c/libhemlock_onnx.so stdlib/c/onnx_wrapper.c -lonnxruntime
```

---

## See Also

- [ONNX Runtime Documentation](https://onnxruntime.ai/docs/)
- [ONNX Runtime GitHub](https://github.com/microsoft/onnxruntime)
- [ONNX Model Zoo](https://github.com/onnx/models) — Pre-trained models
- `@stdlib/vector` — Vector similarity search (pair with embeddings)
- `@stdlib/http` — Fetch models or call APIs
- `@stdlib/json` — Parse model metadata

---

## License

Part of the Hemlock standard library. ONNX Runtime is licensed under MIT.

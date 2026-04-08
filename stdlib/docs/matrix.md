# Hemlock Matrix Module

A standard library module providing matrix and tensor math operations for numerical computing and AI/ML workloads.

## Overview

The matrix module provides numerical primitives without external dependencies:

- **Matrix creation** — zeros, ones, identity, random, from arrays
- **Arithmetic** — add, sub, mul, scale, element-wise operations
- **Linear algebra** — matmul, transpose, outer product, dot product
- **Vector operations** — normalize, magnitude, cosine similarity, euclidean distance
- **Activation functions** — sigmoid, relu, leaky_relu, gelu, softmax, tanh
- **Statistics** — mean, variance, std, sum, argmax, argmin
- **Loss functions** — MSE, cross-entropy
- **Normalization** — layer norm, min-max normalization
- **Shape operations** — reshape, flatten, clone

**Status:** Production-ready. Pure Hemlock — no FFI dependencies.

## Usage

```hemlock
import { zeros, matmul, softmax, dot, cosine_similarity } from "@stdlib/matrix";
```

### Matrix Representation

Matrices are objects with `{ data, rows, cols }` where `data` is a flat array stored in row-major order:

```hemlock
let m = { data: [1, 2, 3, 4, 5, 6], rows: 2, cols: 3 };
// Represents:
// [1, 2, 3]
// [4, 5, 6]
```

## API Reference

### Creation

| Function | Description |
|----------|-------------|
| `zeros(rows, cols)` | Matrix of zeros |
| `ones(rows, cols)` | Matrix of ones |
| `eye(n)` | n×n identity matrix |
| `full(rows, cols, value)` | Matrix filled with value |
| `randn(rows, cols)` | Random values in [0, 1) |
| `from_array(arr2d)` | From 2D array `[[1,2],[3,4]]` |
| `vec(arr)` | Column vector from 1D array |
| `row_vec(arr)` | Row vector from 1D array |

### Element Access

| Function | Description |
|----------|-------------|
| `get(m, row, col)` | Get element at position |
| `set(m, row, col, val)` | Set element at position |
| `get_row(m, row)` | Get row as flat array |
| `get_col(m, col)` | Get column as flat array |

### Arithmetic (element-wise)

| Function | Description |
|----------|-------------|
| `add(a, b)` | Element-wise addition |
| `sub(a, b)` | Element-wise subtraction |
| `mul(a, b)` | Hadamard product |
| `scale(m, s)` | Scalar multiplication |
| `add_scalar(m, s)` | Scalar addition |
| `map(m, fn)` | Apply function element-wise |

### Matrix Operations

| Function | Description |
|----------|-------------|
| `matmul(a, b)` | Matrix multiplication (a.cols must equal b.rows) |
| `transpose(m)` | Transpose matrix |
| `outer(a, b)` | Outer product of two vectors |

### Vector Operations

These work on flat arrays (not matrix objects):

| Function | Description |
|----------|-------------|
| `dot(a, b)` | Dot product |
| `magnitude(v)` | L2 norm |
| `normalize(v)` | Unit vector |
| `cosine_similarity(a, b)` | Cosine similarity [-1, 1] |
| `euclidean_distance(a, b)` | Euclidean distance |

### Activation Functions

All take a matrix and return a new matrix:

| Function | Description |
|----------|-------------|
| `sigmoid(m)` | 1 / (1 + e^(-x)) |
| `relu(m)` | max(0, x) |
| `leaky_relu(m, alpha?)` | x > 0 ? x : alpha*x (default alpha=0.01) |
| `gelu(m)` | Gaussian Error Linear Unit |
| `softmax(m)` | Row-wise softmax (numerically stable) |
| `tanh_activation(m)` | Hyperbolic tangent |

### Statistics

| Function | Description |
|----------|-------------|
| `mean(m)` | Mean of all elements |
| `variance(m)` | Variance |
| `std(m)` | Standard deviation |
| `sum(m)` | Sum of all elements |
| `argmax(m)` | Index of largest element |
| `argmin(m)` | Index of smallest element |
| `max_val(m)` | Largest element |
| `min_val(m)` | Smallest element |

### Loss Functions

| Function | Description |
|----------|-------------|
| `mse(predicted, actual)` | Mean squared error |
| `cross_entropy(predicted, actual)` | Cross-entropy loss |

### Normalization

| Function | Description |
|----------|-------------|
| `layer_norm(m, eps?)` | Layer normalization (per-row) |
| `min_max_norm(m)` | Scale to [0, 1] |

### Shape Operations

| Function | Description |
|----------|-------------|
| `reshape(m, rows, cols)` | Reshape (must preserve element count) |
| `flatten(m)` | Flatten to single row |
| `shape(m)` | Returns [rows, cols] |
| `clone(m)` | Deep copy |
| `same_shape(a, b)` | Check shape equality |
| `to_array(m)` | Convert to 2D array |
| `print_matrix(m)` | Pretty-print to stdout |

## Examples

### Neural network forward pass

```hemlock
import { from_array, matmul, add, relu, softmax } from "@stdlib/matrix";

// Weights and biases
let W1 = from_array([[0.1, 0.2], [0.3, 0.4], [0.5, 0.6]]);  // 3x2
let b1 = from_array([[0.1, 0.2]]);  // 1x2
let W2 = from_array([[0.7, 0.8, 0.9], [0.3, 0.4, 0.5]]);  // 2x3
let b2 = from_array([[0.1, 0.2, 0.3]]);  // 1x3

// Input: 1 sample, 3 features
let x = from_array([[1.0, 2.0, 3.0]]);

// Forward pass
let h = relu(add(matmul(x, W1), b1));   // hidden layer
let out = softmax(add(matmul(h, W2), b2));  // output probabilities

print_matrix(out);
```

### Cosine similarity for embeddings

```hemlock
import { cosine_similarity, normalize } from "@stdlib/matrix";

let doc1 = [0.1, 0.3, 0.5, 0.7];
let doc2 = [0.2, 0.4, 0.4, 0.6];
let doc3 = [0.9, 0.1, 0.0, 0.2];

print("doc1 vs doc2:", cosine_similarity(doc1, doc2));  // high similarity
print("doc1 vs doc3:", cosine_similarity(doc1, doc3));  // low similarity
```

### Softmax classifier

```hemlock
import { from_array, softmax, argmax } from "@stdlib/matrix";

let logits = from_array([[2.0, 1.0, 0.1]]);
let probs = softmax(logits);
print("Probabilities:", probs.data);
print("Predicted class:", argmax(probs));
```

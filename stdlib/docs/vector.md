# Hemlock Vector Module

A standard library module providing high-performance vector similarity search for Hemlock programs via USearch FFI.

## Overview

The vector module provides approximate nearest neighbor (ANN) search using the [USearch](https://github.com/unum-cloud/USearch) library:

- **Index management** — Create, load, save, and free vector indexes
- **Vector operations** — Add, search, remove, retrieve, and rename vectors
- **Batch operations** — Bulk add and search for throughput
- **Distance metrics** — Cosine, L2, inner product, Hamming, Jaccard, and more
- **Quantization** — f32, f64, f16, i8, b1, bf16 scalar types
- **Persistence** — Save to disk, load, or memory-map for read-only access
- **HNSW tuning** — Full control over connectivity, expansion factors, and threading
- **Exact search** — Brute-force k-NN without building an index

## Usage

```hemlock
import { create_index, add, search, save, free_index } from "@stdlib/vector";

// Create a 384-dimensional cosine index
let idx = create_index(384);

// Add some vectors
add(idx, 1, [0.1, 0.2, 0.3, /* ... 384 floats ... */]);
add(idx, 2, [0.4, 0.5, 0.6, /* ... */]);
add(idx, 3, [0.7, 0.8, 0.9, /* ... */]);

// Search for 5 nearest neighbors
let results = search(idx, [0.15, 0.25, 0.35, /* ... */], k: 5);
for (r in results) {
    print("key:", r.key, "distance:", r.distance);
}

// Save to disk
save(idx, "my_embeddings.usearch");

// Clean up
free_index(idx);
```

Or import all:

```hemlock
import * as vec from "@stdlib/vector";

let idx = vec.create_index(128, metric: vec.METRIC_L2SQ);
// ...
vec.free_index(idx);
```

---

## Index Lifecycle

### create_index(dimensions, metric?, quantization?, connectivity?, expansion_add?, expansion_search?, multi?)

Creates a new vector index.

**Parameters:**
- `dimensions: i64` — Number of dimensions per vector (required)
- `metric: i32` — Distance metric (default: `METRIC_COSINE`)
- `quantization: i32` — Storage quantization scalar type (default: `SCALAR_F32`)
- `connectivity: i64` — HNSW graph connectivity M (default: `0` = library default ~16)
- `expansion_add: i64` — Expansion factor for insertion ef_construction (default: `0` = library default ~128)
- `expansion_search: i64` — Expansion factor for search ef (default: `0` = library default ~64)
- `multi: bool` — Allow multiple vectors per key (default: `false`)

**Returns:** `VectorIndex`

**Throws:** On invalid dimensions or initialization failure

```hemlock
import { create_index, METRIC_L2SQ, SCALAR_F16, free_index } from "@stdlib/vector";

// Simple — sane defaults
let idx = create_index(384);

// Full control
let idx2 = create_index(768,
    metric: METRIC_L2SQ,
    quantization: SCALAR_F16,
    connectivity: 32,
    expansion_add: 256,
    expansion_search: 128);

free_index(idx);
free_index(idx2);
```

### load_index(path, dimensions?, metric?, quantization?)

Loads a previously saved index from disk.

**Parameters:**
- `path: string` — Path to the saved index file
- `dimensions: i64` — Override dimensions (default: `0` = read from file metadata)
- `metric: i32` — Override metric (default: `0` = read from file metadata)
- `quantization: i32` — Override quantization (default: `0` = read from file metadata)

**Returns:** `VectorIndex`

```hemlock
import { load_index, search, free_index } from "@stdlib/vector";

let idx = load_index("embeddings.usearch");
let results = search(idx, query_vector, k: 5);
free_index(idx);
```

### view_index(path, dimensions?, metric?, quantization?)

Opens a memory-mapped read-only view of an index file. More memory-efficient than `load_index` for large indexes — the OS pages data in on demand rather than reading the entire file.

**Parameters:** Same as `load_index`

**Returns:** `VectorIndex`

```hemlock
import { view_index, search, free_index } from "@stdlib/vector";

// Memory-map a large index (only pages accessed data into RAM)
let idx = view_index("large_embeddings.usearch");
let results = search(idx, query_vector);
free_index(idx);
```

### index_metadata(path)

Reads index metadata from a file without loading the full index.

**Parameters:**
- `path: string` — Path to the saved index file

**Returns:** `object` with fields: `dimensions`, `metric`, `quantization`, `connectivity`

```hemlock
import { index_metadata } from "@stdlib/vector";

let meta = index_metadata("embeddings.usearch");
print("Dimensions:", meta.dimensions);
print("Metric:", meta.metric);
```

### free_index(idx)

Frees index resources. Always call this when done with an index.

**Parameters:**
- `idx: VectorIndex` — Index to free

**Returns:** `null`

```hemlock
import { create_index, free_index } from "@stdlib/vector";

let idx = create_index(128);
// ... use index ...
free_index(idx);
// Calling free_index again is safe (no-op)
free_index(idx);
```

---

## Data Operations

### add(idx, key, vector, scalar?)

Adds a vector to the index.

**Parameters:**
- `idx: VectorIndex` — Target index
- `key: i64` — Unique integer key identifying this vector
- `vector: array` — Array of numbers (length must match index dimensions)
- `scalar: i32` — Scalar type of input data (default: `SCALAR_F32`)

**Returns:** `null`

**Throws:** If vector length doesn't match dimensions

```hemlock
import { create_index, add, free_index } from "@stdlib/vector";

let idx = create_index(3);
add(idx, 1, [1.0, 0.0, 0.0]);
add(idx, 2, [0.0, 1.0, 0.0]);
add(idx, 3, [0.0, 0.0, 1.0]);
free_index(idx);
```

### add_batch(idx, keys, vectors, scalar?)

Adds multiple vectors in batch.

**Parameters:**
- `idx: VectorIndex` — Target index
- `keys: array` — Array of integer keys
- `vectors: array` — Array of vector arrays
- `scalar: i32` — Scalar type (default: `SCALAR_F32`)

**Returns:** `null`

**Throws:** If keys/vectors length mismatch or any vector has wrong dimensions

```hemlock
import { create_index, add_batch, free_index } from "@stdlib/vector";

let idx = create_index(3);

let keys = [1, 2, 3];
let vecs = [
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0],
];

add_batch(idx, keys, vecs);
free_index(idx);
```

### search(idx, query, k?, scalar?)

Searches for k nearest neighbors.

**Parameters:**
- `idx: VectorIndex` — Index to search
- `query: array` — Query vector
- `k: i64` — Number of nearest neighbors (default: `10`)
- `scalar: i32` — Scalar type of query (default: `SCALAR_F32`)

**Returns:** `array` of `{ key, distance }` objects, sorted by distance ascending

```hemlock
import { create_index, add, search, free_index } from "@stdlib/vector";

let idx = create_index(3);
add(idx, 1, [1.0, 0.0, 0.0]);
add(idx, 2, [0.9, 0.1, 0.0]);
add(idx, 3, [0.0, 0.0, 1.0]);

let results = search(idx, [1.0, 0.0, 0.0], k: 2);
for (r in results) {
    print("key:", r.key, "distance:", r.distance);
}
// Output:
// key: 1 distance: 0        (exact match)
// key: 2 distance: 0.00499  (close)

free_index(idx);
```

### search_batch(idx, queries, k?, scalar?)

Searches for k nearest neighbors of multiple queries.

**Parameters:**
- `idx: VectorIndex` — Index to search
- `queries: array` — Array of query vectors
- `k: i64` — Number of nearest neighbors per query (default: `10`)
- `scalar: i32` — Scalar type (default: `SCALAR_F32`)

**Returns:** `array` of result arrays (one per query)

```hemlock
import { create_index, add_batch, search_batch, free_index } from "@stdlib/vector";

let idx = create_index(3);
add_batch(idx, [1, 2, 3], [
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0],
]);

let batch_results = search_batch(idx, [
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
], k: 2);

// batch_results[0] = results for first query
// batch_results[1] = results for second query

free_index(idx);
```

### remove(idx, key)

Removes a vector by key.

**Parameters:**
- `idx: VectorIndex` — Target index
- `key: i64` — Key to remove

**Returns:** `i64` — Number of vectors removed (0 if key not found)

```hemlock
import { create_index, add, remove, size, free_index } from "@stdlib/vector";

let idx = create_index(3);
add(idx, 1, [1.0, 0.0, 0.0]);
print(size(idx));  // 1

remove(idx, 1);
print(size(idx));  // 0

free_index(idx);
```

### rename(idx, from_key, to_key)

Renames a vector's key.

**Parameters:**
- `idx: VectorIndex` — Target index
- `from_key: i64` — Current key
- `to_key: i64` — New key

**Returns:** `i64` — Number of vectors renamed

### contains(idx, key)

Checks if a key exists in the index.

**Parameters:**
- `idx: VectorIndex` — Index to check
- `key: i64` — Key to look up

**Returns:** `bool`

```hemlock
import { create_index, add, contains, free_index } from "@stdlib/vector";

let idx = create_index(3);
add(idx, 42, [1.0, 0.0, 0.0]);

print(contains(idx, 42));  // true
print(contains(idx, 99));  // false

free_index(idx);
```

### count(idx, key)

Counts vectors for a key. Useful for multi-indexes where multiple vectors can share a key.

**Parameters:**
- `idx: VectorIndex` — Index to check
- `key: i64` — Key to count

**Returns:** `i64`

### get(idx, key, scalar?)

Retrieves a stored vector by key.

**Parameters:**
- `idx: VectorIndex` — Index to retrieve from
- `key: i64` — Key to look up
- `scalar: i32` — Scalar type to retrieve as (default: `SCALAR_F32`)

**Returns:** `array` of float values, or `null` if key not found

```hemlock
import { create_index, add, get, free_index } from "@stdlib/vector";

let idx = create_index(3);
add(idx, 1, [0.5, 0.3, 0.8]);

let vec = get(idx, 1);
print(vec);  // [0.5, 0.3, 0.8]

let missing = get(idx, 999);
print(missing);  // null

free_index(idx);
```

---

## Persistence

### save(idx, path)

Saves an index to disk.

**Parameters:**
- `idx: VectorIndex` — Index to save
- `path: string` — Output file path

**Returns:** `null`

```hemlock
import { create_index, add, save, load_index, search, free_index } from "@stdlib/vector";

// Build and save
let idx = create_index(128);
// ... add vectors ...
save(idx, "embeddings.usearch");
free_index(idx);

// Load later
let idx2 = load_index("embeddings.usearch");
let results = search(idx2, query_vector);
free_index(idx2);
```

---

## Index Properties

### size(idx)

Gets the number of vectors in the index.

**Returns:** `i64`

### capacity(idx)

Gets the allocated capacity.

**Returns:** `i64`

### dimensions(idx)

Gets the number of dimensions.

**Returns:** `i64`

### connectivity(idx)

Gets the HNSW graph connectivity (M parameter).

**Returns:** `i64`

### memory_usage(idx)

Gets current memory usage in bytes.

**Returns:** `i64`

### serialized_length(idx)

Gets the serialized length in bytes (file size if saved).

**Returns:** `i64`

### hardware_acceleration(idx)

Gets the SIMD hardware acceleration in use.

**Returns:** `string` (e.g., `"avx512"`, `"neon"`, `"serial"`)

```hemlock
import { create_index, hardware_acceleration, free_index } from "@stdlib/vector";

let idx = create_index(128);
print("SIMD:", hardware_acceleration(idx));
free_index(idx);
```

### get_expansion_add(idx)

Gets the current expansion factor for insertion.

**Returns:** `i64`

### get_expansion_search(idx)

Gets the current expansion factor for search.

**Returns:** `i64`

---

## Index Configuration (Runtime Tuning)

These functions allow tuning HNSW parameters after index creation.

### set_expansion_add(idx, expansion)

Sets the expansion factor for insertion (ef_construction). Higher values improve recall at the cost of slower insertion.

### set_expansion_search(idx, expansion)

Sets the expansion factor for search (ef). Higher values improve recall at the cost of slower search.

### set_threads_add(idx, threads)

Sets the number of threads for parallel insertion.

### set_threads_search(idx, threads)

Sets the number of threads for parallel search.

### set_metric(idx, metric)

Changes the distance metric at runtime.

### reserve(idx, capacity)

Pre-allocates capacity for a known number of vectors. Call this before bulk insertion to avoid repeated internal reallocations.

```hemlock
import { create_index, reserve, add_batch, free_index } from "@stdlib/vector";

let idx = create_index(128);
reserve(idx, 100000);  // We know we'll add ~100K vectors

// Bulk insert without reallocation overhead
// ...

free_index(idx);
```

### clear(idx)

Removes all vectors from the index.

---

## Standalone Distance Computation

### distance(a, b, metric?, scalar?)

Computes the distance between two vectors without an index.

**Parameters:**
- `a: array` — First vector
- `b: array` — Second vector (same length as `a`)
- `metric: i32` — Distance metric (default: `METRIC_COSINE`)
- `scalar: i32` — Scalar type (default: `SCALAR_F32`)

**Returns:** `f32`

```hemlock
import { distance, METRIC_COSINE, METRIC_L2SQ } from "@stdlib/vector";

let a = [1.0, 0.0, 0.0];
let b = [0.0, 1.0, 0.0];

print("Cosine:", distance(a, b));
print("L2:", distance(a, b, metric: METRIC_L2SQ));
```

---

## Exact Brute-Force Search

### exact_search(dataset, queries, k?, metric?, threads?, scalar?)

Performs exact (brute-force) k-NN search over a dataset without building an index. Guarantees 100% recall. Good for small datasets or validation.

**Parameters:**
- `dataset: array` — Array of vectors (array of arrays)
- `queries: array` — Array of query vectors
- `k: i64` — Number of nearest neighbors per query (default: `10`)
- `metric: i32` — Distance metric (default: `METRIC_COSINE`)
- `threads: i64` — Number of threads for parallel search (default: `1`)
- `scalar: i32` — Scalar type (default: `SCALAR_F32`)

**Returns:** `array` of result arrays, each containing `{ key, distance }` objects. Keys are 0-based indices into the dataset array.

```hemlock
import { exact_search } from "@stdlib/vector";

let dataset = [
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0],
    [0.7, 0.7, 0.0],
];

let queries = [[1.0, 0.0, 0.0]];
let results = exact_search(dataset, queries, k: 2);

for (r in results[0]) {
    print("dataset[" + r.key + "] distance:", r.distance);
}
```

---

## Constants

### Distance Metrics

```hemlock
METRIC_COSINE      // 1 - Cosine distance (default)
METRIC_IP          // 2 - Inner product (dot product)
METRIC_L2SQ        // 3 - Euclidean (L2 squared)
METRIC_HAVERSINE   // 4 - Geographic distance
METRIC_DIVERGENCE  // 5 - Jensen-Shannon divergence
METRIC_PEARSON     // 6 - Pearson correlation
METRIC_JACCARD     // 7 - Jaccard similarity
METRIC_HAMMING     // 8 - Hamming distance
METRIC_TANIMOTO    // 9 - Tanimoto coefficient
METRIC_SORENSEN    // 10 - Sorensen-Dice coefficient
```

### Scalar Types

```hemlock
SCALAR_F32         // 1 - 32-bit float (default)
SCALAR_F64         // 2 - 64-bit float
SCALAR_F16         // 3 - 16-bit float (half precision)
SCALAR_I8          // 4 - 8-bit integer (quantized)
SCALAR_B1          // 5 - Binary (1-bit)
SCALAR_BF16        // 6 - Brain float 16
```

---

## Performance Tips

1. **Call `reserve()` before bulk insertion** — Avoids internal reallocations
2. **Use `add_batch()` over repeated `add()`** — Reduces per-call overhead
3. **Tune `expansion_search`** — Lower values = faster search, higher = better recall
4. **Use `SCALAR_F16` or `SCALAR_I8` quantization** — Reduces memory by 2-4x with minimal recall loss
5. **Use `view_index()` for read-only workloads** — Memory-maps the file instead of loading everything
6. **Set thread counts** — `set_threads_add()` and `set_threads_search()` for parallelism
7. **Use `search_batch()`** — More efficient than individual searches for multiple queries

---

## HNSW Algorithm Notes

USearch uses the Hierarchical Navigable Small World (HNSW) graph algorithm:

- **Approximate** — Results are approximate nearest neighbors, not necessarily exact
- **Tunable recall** — Higher `expansion_search` values increase recall toward 100%
- **Sublinear search** — O(log n) search time vs O(n) for brute force
- **Use `exact_search()` when you need guaranteed 100% recall**

Key HNSW parameters:
- `connectivity` (M) — Number of bidirectional links per node. Higher = better recall, more memory
- `expansion_add` (ef_construction) — Search depth during insertion. Higher = better graph quality, slower build
- `expansion_search` (ef) — Search depth during query. Higher = better recall, slower search

---

## Examples

### RAG / Embedding Search

```hemlock
import { create_index, add, search, save, free_index } from "@stdlib/vector";
import { http_post } from "@stdlib/http";
import { parse } from "@stdlib/json";

// Create index for OpenAI ada-002 embeddings (1536 dimensions)
let idx = create_index(1536);

// Add document embeddings (keys = document IDs)
add(idx, 1, embedding_for_doc_1);
add(idx, 2, embedding_for_doc_2);
// ...

// Query: find the 5 most relevant documents
let query_embedding = get_embedding("What is the capital of France?");
let results = search(idx, query_embedding, k: 5);

for (r in results) {
    print("Document", r.key, "relevance:", 1.0 - r.distance);
}

save(idx, "knowledge_base.usearch");
free_index(idx);
```

### Recommendation System

```hemlock
import { create_index, add_batch, search, reserve, free_index, METRIC_IP } from "@stdlib/vector";

// Inner product for pre-normalized embeddings
let idx = create_index(64, metric: METRIC_IP);
reserve(idx, 50000);

// Add user embeddings
add_batch(idx, user_ids, user_embeddings);

// Find similar users
let similar = search(idx, target_user_embedding, k: 20);
for (s in similar) {
    print("Similar user:", s.key);
}

free_index(idx);
```

### Persistence Workflow

```hemlock
import { create_index, add, save, load_index, view_index, search,
         size, memory_usage, free_index } from "@stdlib/vector";

// Build phase
let idx = create_index(128);
// ... add thousands of vectors ...
save(idx, "index.usearch");
print("Saved", size(idx), "vectors,", memory_usage(idx), "bytes");
free_index(idx);

// Serve phase (read-only, memory-efficient)
let idx2 = view_index("index.usearch");
let results = search(idx2, query);
free_index(idx2);
```

---

## Error Handling

All functions throw exceptions on errors:

```hemlock
import { create_index, add, search, free_index } from "@stdlib/vector";

try {
    let idx = create_index(128);

    try {
        // Wrong dimensions — throws
        add(idx, 1, [1.0, 2.0, 3.0]);
    } catch (e) {
        print("Error:", e);
    }

    free_index(idx);
} catch (e) {
    print("Init error:", e);
}
```

---

## System Requirements

- USearch C shared library (`libusearch_c.so`)
- Install from source:
  ```bash
  git clone https://github.com/unum-cloud/usearch
  cd usearch
  cmake -B build -DUSEARCH_BUILD_LIB_C=ON
  cmake --build build
  sudo cmake --install build
  sudo ldconfig
  ```
- Or via package manager if available:
  ```bash
  sudo apt install libusearch-dev  # Debian/Ubuntu (if packaged)
  ```

---

## See Also

- [USearch Documentation](https://unum-cloud.github.io/usearch/)
- [USearch GitHub](https://github.com/unum-cloud/usearch)
- `@stdlib/sqlite` — SQLite database operations
- `@stdlib/json` — JSON parsing for vector data
- `@stdlib/math` — Mathematical operations

---

## License

Part of the Hemlock standard library. USearch is licensed under Apache-2.0.

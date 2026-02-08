# Proposal: `@stdlib/vector` — Vector Similarity Search Module

**Status:** Exploration / RFC
**Date:** 2026-02-07

---

## Summary

Add a `@stdlib/vector` module providing in-process vector similarity search (nearest neighbor lookup). This enables embedding-based search, recommendation systems, and AI/ML workflows directly from Hemlock without requiring an external server.

---

## Options Evaluated

Ten vector database / library options were evaluated against these criteria:

| Criteria | Weight | Rationale |
|----------|--------|-----------|
| C API quality | Critical | Hemlock FFI requires C linkage (`extern fn`) to `.so` libraries |
| Embeddable (in-process) | Critical | Hemlock stdlib modules are libraries, not client-server protocols |
| Dependency weight | High | Minimal deps preferred (like sqlite — just one `.so`) |
| API simplicity | High | Hemlock values explicit, small APIs |
| Performance | Medium | Good enough for 1M+ vectors; doesn't need billion-scale |
| Persistence | Medium | Save/load indexes to disk |
| License | Medium | Must be permissive (Apache-2.0, MIT, BSD) |

### Results

| Option | C API | Embeddable | Deps | Performance | Verdict |
|--------|-------|------------|------|-------------|---------|
| **USearch** | C99 first-class | Yes | Zero | HNSW + SIMD | **PRIMARY** |
| **sqlite-vec** | Via SQL | Yes | Zero (pure C) | Brute-force | **SECONDARY** |
| hnswlib | C++ only | Yes | Zero | HNSW | No C API — skip |
| FAISS | C API (faiss_c) | Yes | BLAS required | State of the art | Too heavy |
| pgvector | N/A | No (needs PG) | PostgreSQL | Good | Server required — skip |
| Qdrant | None | No (server) | Heavy | Excellent | Server required — skip |
| Milvus | C++ SDK | No (distributed) | Very heavy | Excellent | Distributed system — skip |
| Annoy | C++ only | Yes | Zero | Moderate | Outdated, no C API |
| LanceDB | Community C only | Yes | Moderate (Rust) | Good | No official C API |
| ChromaDB | None | Limited | Heavy | Moderate | No C API |

---

## Recommendation: USearch (primary) + sqlite-vec (lightweight alternative)

### Why NOT pgvector

pgvector requires a running PostgreSQL server. Hemlock's stdlib modules are embeddable libraries loaded via FFI (`import "libfoo.so"`), not client-server protocols. Requiring users to install, configure, and run PostgreSQL for vector search is fundamentally misaligned with the stdlib pattern. The sqlite module works precisely because SQLite is an in-process library with zero server requirements.

### Primary: USearch (`libusearch_c.so`)

[USearch](https://github.com/unum-cloud/USearch) is an open-source (Apache-2.0) vector similarity search library with a first-class C99 API. It uses the HNSW (Hierarchical Navigable Small World) algorithm with SIMD optimization.

**Why USearch fits Hemlock:**

1. **C99 API maps directly to Hemlock FFI.** The pattern is identical to `@stdlib/sqlite`:
   ```hemlock
   import "libusearch_c.so";
   extern fn usearch_init(options: ptr, error: ptr): ptr;
   extern fn usearch_add(index: ptr, key: i64, vector: ptr, kind: i32, error: ptr): void;
   extern fn usearch_search(index: ptr, query: ptr, kind: i32, count: i64,
                            keys: ptr, distances: ptr, error: ptr): i64;
   ```

2. **Zero mandatory dependencies.** Compiles to a single `.so` with no BLAS, LAPACK, or other external requirements.

3. **In-process and persistent.** Memory-mapped file support — indexes saved to disk, loaded without reading everything into RAM.

4. **Small, explicit API.** ~20 C functions covering: init, add, search, remove, save, load, free. Fits Hemlock's "explicit over implicit" philosophy.

5. **Production-proven.** Used by ScyllaDB and YugabyteDB for vector indexing. v2.23+ as of Jan 2026.

6. **Performance.** HNSW algorithm with SIMD (AVX-512, NEON). Supports f32, f64, f16, and int8 quantization. Handles millions of vectors in-process.

**USearch C API surface (from `usearch.h`):**

```c
// Lifecycle
usearch_index_t usearch_init(usearch_init_options_t*, usearch_error_t*);
void usearch_free(usearch_index_t, usearch_error_t*);

// Mutation
void usearch_add(index, key, vector, scalar_kind, error);
size_t usearch_remove(index, key, error);

// Query
size_t usearch_search(index, query, scalar_kind, count, keys_out, distances_out, error);
size_t usearch_filtered_search(index, query, scalar_kind, count, filter_fn, filter_state,
                               keys_out, distances_out, error);
bool usearch_contains(index, key, error);
size_t usearch_count(index, error);

// Persistence
void usearch_save(index, path, error);
void usearch_load(index, path, error);
void usearch_view(index, path, error);  // memory-mapped read-only

// Utility
usearch_distance_t usearch_distance(a, b, scalar_kind, dimensions, metric_kind, error);
```

### Secondary: sqlite-vec (extension to `@stdlib/sqlite`)

[sqlite-vec](https://github.com/asg017/sqlite-vec) is a SQLite extension providing vector search through SQL. It could be loaded via `sqlite3_load_extension()` from the existing `@stdlib/sqlite` module with no new FFI bindings needed.

**Tradeoffs vs USearch:**

| | USearch | sqlite-vec |
|---|---------|-----------|
| Install | New `.so` | SQLite extension `.so` |
| API | Dedicated Hemlock functions | SQL queries via existing sqlite module |
| Algorithm | HNSW (approximate) | Brute-force (exact) |
| Scale | Millions of vectors | ~100K vectors |
| New code | New stdlib module | Small extension to existing sqlite module |
| Recall | Approximate (tunable) | 100% exact |

sqlite-vec is a good "batteries included" option for small datasets where users already have `@stdlib/sqlite`. USearch is the right choice for anything beyond prototyping scale.

---

## Proposed API Design (`@stdlib/vector`)

Following the patterns established by `@stdlib/sqlite` (FFI wrapper with Hemlock-idiomatic API):

```hemlock
import { VectorIndex, create_index, load_index } from "@stdlib/vector";

// Create an index
let idx = create_index(dimensions: 384, metric: "cosine");

// Add vectors (key + float array)
idx.add(1, [0.1, 0.2, 0.3, ...]);
idx.add(2, [0.4, 0.5, 0.6, ...]);
idx.add(3, [0.7, 0.8, 0.9, ...]);

// Search for k nearest neighbors
let results = idx.search([0.15, 0.25, 0.35, ...], k: 10);
// returns: [{ key: 1, distance: 0.023 }, { key: 3, distance: 0.15 }, ...]

// Persistence
idx.save("embeddings.usearch");
let loaded = load_index("embeddings.usearch", dimensions: 384);

// Filtered search (with predicate)
let filtered = idx.search_filtered([0.1, ...], k: 5, filter: fn(key) {
    return key > 100;  // only match keys > 100
});

// Info
print(idx.size());       // number of vectors
print(idx.dimensions()); // dimensionality
print(idx.contains(42)); // membership check

// Cleanup
idx.remove(2);
idx.free();
```

### Module exports

```
create_index(dimensions, metric?, connectivity?, expansion_add?, expansion_search?)
load_index(path, dimensions?, metric?)
view_index(path)  // memory-mapped, read-only

VectorIndex.add(key, vector)
VectorIndex.search(query, k?)
VectorIndex.search_filtered(query, k?, filter)
VectorIndex.remove(key)
VectorIndex.contains(key)
VectorIndex.count()
VectorIndex.size()
VectorIndex.dimensions()
VectorIndex.save(path)
VectorIndex.free()

distance(a, b, metric?)  // standalone distance calculation

// Distance metrics
METRIC_COSINE
METRIC_L2SQ       // Euclidean (L2 squared)
METRIC_IP          // Inner product (dot product)
METRIC_HAMMING
METRIC_JACCARD

// Scalar types
SCALAR_F32
SCALAR_F64
SCALAR_F16
SCALAR_I8
```

### System requirements

```
# Debian/Ubuntu
sudo apt install libusearch-dev

# From source
git clone https://github.com/unum-cloud/usearch
cd usearch && cmake -B build && cmake --build build
sudo cmake --install build
```

---

## Implementation Plan

1. **Write FFI bindings** — `extern fn` declarations for USearch C API (~20 functions)
2. **Implement Hemlock wrapper** — `create_index()`, `VectorIndex` define with methods
3. **Handle memory** — Proper `alloc`/`free` for vector data marshaling, error string cleanup
4. **Add documentation** — `stdlib/docs/vector.md` following sqlite.md pattern
5. **Add tests** — `tests/stdlib_vector/` with basic CRUD, persistence, search accuracy
6. **Add parity tests** — If compiler support is needed for FFI patterns used

Estimated scope: ~400-600 lines of Hemlock (comparable to `sqlite.hml` at 968 lines, but simpler API surface).

---

## Open Questions

1. **Module name:** `@stdlib/vector` vs `@stdlib/vectordb` vs `@stdlib/similarity`?
2. **sqlite-vec integration:** Ship as part of `@stdlib/sqlite` (extension loading) or separate module?
3. **Quantization API:** Expose USearch's int8/f16 quantization, or default to f32 and keep it simple?
4. **Batch operations:** Add `add_batch()` / `search_batch()` for bulk operations, or keep single-item API?
5. **Index configuration:** How much of USearch's HNSW tuning (connectivity, expansion factors) to expose?

# Borrow Checker (Ownership Analysis)

The Hemlock compiler (`hemlockc`) ships a **Rust-inspired ownership checker** —
a static analysis pass that finds the memory-safety bugs Hemlock deliberately
permits at runtime: use-after-free, double-free, leaks, and (in strict mode)
use-after-move.

It is **advisory by design.** Hemlock's philosophy is "we give you the tools to
be safe, but we don't force you to use them." So the checker emits **warnings**,
not errors, and never changes what your program does. You can tighten it, loosen
it, or turn it off entirely.

```
Source (.hml)
    ↓
  Parse → AST
    ↓
  Type Check
    ↓
  Borrow Check   ← this pass (warnings by default)
    ↓
  C Code Generation
```

---

## What it catches

| Diagnostic | When | Default |
|------------|------|---------|
| **use-after-free** | a resource is read/passed after `free()`/`.close()` | on |
| **double-free** | `free()`/`.close()` on an already-released resource | on |
| **free-inside-loop** | a resource acquired outside a loop is freed in its body (next iteration double-frees) | on |
| **defer/explicit double-free** | a resource freed explicitly *and* via `defer free(x)` | on |
| **use-after-move** | a binding is used after its resource was moved away | strict only |
| **leaked resource** | an owned resource goes out of scope without being freed, moved, or returned | strict only |

A *resource* is anything acquired through an explicit acquisition builtin:

- `alloc(n)` → raw memory (released by `free`)
- `buffer(n)` → safe buffer (released by `free`)
- `open(path, mode)` → file handle (released by `.close()`)

---

## Examples

```hemlock
fn main() {
    let p = alloc(64);
    free(p);
    memset(p, 0, 64);   // warning: use of 'p' after it was freed (line 2)
}
```

```hemlock
fn main() {
    let p = alloc(64);
    free(p);
    free(p);            // warning: double free: 'p' was already freed (line 2)
}
```

```hemlock
fn main() {
    let p = alloc(64);
    defer free(p);
    free(p);            // warning: 'p' is already scheduled to be freed via defer
}
```

The pass is **flow-sensitive**: branches and loops are merged conservatively,
and paths that leave early (`return`, `break`, `continue`, `throw`, `panic`,
`exit`) are excluded from the merge, so guarded cleanup does not produce false
positives:

```hemlock
fn cleanup(c: bool) {
    let p = alloc(64);
    if (c) {
        free(p);
        return;         // this path is done...
    }
    free(p);            // ...so this is NOT a double free
}
```

Because Hemlock values are **shared** (reference-counted), aliasing is legal and
the checker does not complain about it in the default mode:

```hemlock
let b = buffer(32);
let view = b;           // alias, fine
free(b);                // releases the shared resource
```

---

## Strict mode

`--borrow-strict` imposes the stricter, more Rust-like rules on top of the
defaults:

- **Move semantics** — binding one owner to another moves it; the source is
  then invalid:

  ```hemlock
  let p = alloc(64);
  let q = p;            // p moved into q
  free(q);
  memset(p, 0, 64);     // warning: use of 'p' after it was moved
  ```

- **Leak detection** — an owned resource that is never freed, moved, or returned
  before its scope ends is reported:

  ```hemlock
  fn main() {
      let p = alloc(64);
      memset(p, 0, 64);
  }                     // warning: 'p' (memory) is never freed (possible leak)
  ```

---

## Flags

| Flag | Effect |
|------|--------|
| *(default)* | ownership checking on, advisory warnings, build always succeeds |
| `--no-borrow-check` | disable the pass entirely |
| `--borrow-strict` | add move tracking and leak detection |
| `--borrow-error` | treat findings as errors — a finding fails the build (exit 1) |

`--borrow-error` is the knob for CI: combine it with `--borrow-strict` to make a
clean ownership analysis a build requirement.

---

## Scope and limitations

This is the **first stage** of the analysis. It is intraprocedural and tracks
resources from the built-in acquisition functions. The following are
intentionally out of scope for now and may be layered on later without changing
the surface:

- **Lifetimes** — no `'a`-style lifetime parameters or borrow regions.
- **Interprocedural ownership** — passing a resource to a user function is
  treated as a borrow, not a move; the checker does not yet read callee
  signatures to decide whether a callee consumes its argument.
- **Borrow-conflict checking** — simultaneous mutable/immutable borrows are not
  yet modeled (Hemlock has no borrow syntax beyond `ref`/`const` parameters).
- **Custom allocators** — only `alloc`/`buffer`/`open` are recognized as
  acquisitions; arena/stdlib allocators are not yet tracked.

The implementation lives in `src/backends/compiler/borrow_check.c` with the
public API in `include/compiler/borrow_check.h`. Diagnostics are collectible
(`borrow_check_enable_collection`) so the LSP can surface them inline in a
future revision.

---

## Testing

```bash
make test-borrow
```

Each `tests/borrow/<name>.hml` has a `<name>.expected` file with the exact
diagnostics, and an optional `<name>.flags` file for per-test compiler flags.

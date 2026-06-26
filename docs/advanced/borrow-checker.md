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
| **use-after-free** | a resource is read/passed after it was released | on |
| **double-free** | a resource is released when it was already released | on |
| **release mismatch** | a resource is released with the wrong operation (e.g. `free()` on a file) | on |
| **free-inside-loop** | a resource acquired outside a loop is freed in its body (next iteration double-frees) | on |
| **defer/explicit double-free** | a resource freed explicitly *and* via `defer free(x)` | on |
| **use-after-move** | a binding is used after its resource was moved away | strict only |
| **leaked resource** | an owned resource goes out of scope without being released, moved, or returned | strict only |

A *resource* is anything acquired through an explicit acquisition builtin. The
checker knows each resource's correct release operation and tracks them all
uniformly:

| Acquisition | Resource | Released by |
|-------------|----------|-------------|
| `alloc(n)` | raw memory | `free(x)` |
| `buffer(n)` | safe buffer | `free(x)` |
| `open(path, mode)` | file handle | `x.close()` |
| `channel(n)` | channel | `x.close()` |
| `spawn(fn, …)` / `spawn_with(…)` | async task | `join(x)`, `detach(x)`, or `await x` |
| `ffi_open(path)` | dynamic FFI library | `ffi_close(x)` |
| `mmap_open(…)` / `mmap_open_anon(…)` | memory mapping | `mmap_close(x)` |

Releasing a resource with the wrong operation — `free()` on a file, `.close()`
on raw memory, `free()` on a task — is reported as a **release mismatch** and the
resource is treated as still live (so any real leak is still caught):

```hemlock
fn main() {
    let f = open("x.txt", "r");
    free(f);   // warning: 'f' (file) cannot be released with free(); use .close()
}
```

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

## Crossing function boundaries

The checker summarises how each top-level function treats its parameters. If a
function releases one of its parameters, passing a resource to it **transfers
ownership** — the resource is consumed at the call site, so using or freeing it
afterwards is reported just as if you had released it inline:

```hemlock
fn consume(p: ptr) {
    free(p);
}

fn main() {
    let p = alloc(64);
    consume(p);
    memset(p, 0, 64);   // warning: use of 'p' after it was freed
    free(p);            // warning: double free
}
```

A function that only *reads* a parameter borrows it — passing a resource to it is
fine, and you remain responsible for releasing it:

```hemlock
fn use_it(p: ptr) { memset(p, 0, 16); }   // borrow, not release

fn main() {
    let p = alloc(64);
    use_it(p);          // still owned here
    free(p);            // fine
}
```

Summaries are order-independent (a function may be defined after its callers) and
conservative: a parameter freed only on *some* paths makes the call a *possible*
consume, and calls using named arguments are left untouched. The analysis is
one level deep — it does not yet follow a parameter that is forwarded into a
*second* consuming function.

Storing a resource into a container (object literal, array, field, or index
assignment) hands ownership to that container, so it is treated as an escape and
does not trip leak detection in strict mode:

```hemlock
fn main() {
    let p = alloc(64);
    let holder = { data: p };   // ownership moves into `holder`; no leak warning
}
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

## Checking without compiling

`--check` runs the full static-analysis front end — parse, type check, and
borrow check — and stops before code generation. Nothing is compiled or
linked. This makes `hemlockc` usable as a pure linter, including for code you
intend to run with the interpreter:

```sh
hemlockc --check app.hml && hemlock app.hml      # lint, then interpret
hemlockc --check --borrow-strict app.hml         # lint with move/leak checks
hemlockc --check --borrow-error app.hml          # exit 1 if anything is found
```

By default `--check` is advisory: borrow findings print as warnings and the
exit code stays 0. Add `--borrow-error` to make a finding fail the check
(exit 1) — handy for gating an interpreter run or a pre-commit hook.

---

## Scope and limitations

The analysis is flow-sensitive, tracks the built-in acquisition functions, and
carries a one-level interprocedural summary (consuming functions + container
escape). The following are intentionally out of scope for now and may be layered
on later without changing the surface:

- **Lifetimes** — no `'a`-style lifetime parameters or borrow regions.
- **Transitive consumption** — a function that forwards its parameter into a
  *second* consuming function is not yet recognised as consuming; only a
  parameter released directly in the body is summarised.
- **Borrow-conflict checking** — simultaneous mutable/immutable borrows are not
  yet modeled (Hemlock has no borrow syntax beyond `ref`/`const` parameters).
- **Custom allocators** — only the built-in acquisitions (`alloc`, `buffer`,
  `open`, `channel`, `spawn`, `ffi_open`, `mmap_open`) are recognized; arena and
  other stdlib allocators are not yet tracked.

The implementation lives in `src/backends/compiler/borrow_check.c` with the
public API in `include/compiler/borrow_check.h`.

## Editor integration (LSP)

The language server runs the borrow checker on every document change and
publishes its findings inline alongside type-check diagnostics, so
use-after-free / double-free / free-in-loop warnings appear live in the editor.
The LSP uses the default (non-strict) mode to keep editor feedback
high-precision and low-noise. Diagnostics are collected through
`borrow_check_enable_collection` and mapped to LSP warnings in
`lsp_document_parse` (`src/tools/lsp/lsp.c`).

---

## Testing

```bash
make test-borrow
```

This runs the dedicated memory-safety suite in `tests/borrow/` and is wired
into CI (the `borrow-checker` job in `.github/workflows/tests.yml`) and into
`make test-all`. The suite covers every diagnostic class plus precision cases:

- **Positive** fixtures (e.g. `use_after_free`, `double_free_buffer`,
  `free_in_while`, `defer_twice`, `strict_double_after_move`, `leak_file`)
  assert the exact expected diagnostic.
- **Negative** fixtures (prefixed `neg_`, e.g. `neg_borrow_then_free`,
  `neg_free_both_branches`, `neg_alloc_free_per_iter`, `neg_switch_free_each`,
  `neg_return_resource`) assert that no diagnostic is produced — these guard
  against false positives.

Each `tests/borrow/<name>.hml` has a `<name>.expected` file with the exact
diagnostics (empty for negative cases), and an optional `<name>.flags` file for
per-test compiler flags such as `--borrow-strict`.

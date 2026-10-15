# The Memory-Safety Model: Buffer Invariants, the Safe Fragment, and `ptr` Proof Obligations

> "A small, unsafe language for writing unsafe things safely."

This document makes that tagline precise. Read formally, it is a claim with the
same shape as the RustBelt theorem for Rust (Jung et al., POPL 2018), scaled to
Hemlock's size:

> **Claim.** Programs that stay inside the *safe fragment* of Hemlock — in
> particular, programs that use `buffer` and never touch `ptr` — cannot exhibit
> memory-related undefined behavior. Every unsafe operation (`ptr` and its
> friends) carries a precisely stated *proof obligation*; a program that
> discharges every obligation at every unsafe crossing recovers the same
> guarantee.

This document states the invariants `buffer` maintains, defines the safe
fragment, proves the safety theorem (as a rigorous pen-and-paper argument over
the actual implementation, not a mechanized proof), and catalogs the obligation
at every crossing into unsafe territory. It also honestly lists what the
theorem does *not* cover, and the audit findings that had to be fixed to make
the theorem true.

Everything here is grounded in the implementation: invariants cite the C source
that maintains them, and each claim is anchored by tests under `tests/buffers/`
and `tests/parity/`.

---

## 1. The two-world design

Hemlock deliberately splits memory access into two worlds:

| | `buffer` (safe world) | `ptr` (unsafe world) |
|---|---|---|
| Representation | pointer + length + capacity + refcount + freed flag | bare 8-byte address |
| Bounds checking | every access | never |
| Liveness checking | every access (freed flag / zeroed length) | never |
| `free()` | validated: double-free detected, views protected | raw `free(3)`: double-free is a crash |
| Lifetime | refcounted; auto-released at scope exit | fully manual |
| Failure mode | **catchable Hemlock error** | **undefined behavior** |

The design intent — "we give you the tools to be safe but don't force you to
use them" — is only meaningful if the safe tools are *actually* safe. That is
the theorem below.

### The abstract model

To state invariants we model the runtime as a machine state with:

- a **heap** `H`: a finite map from allocations (blocks) `ℓ` to
  `(size_ℓ, bytes_ℓ, state_ℓ ∈ {live, dead})`. Concretely a block is one
  `malloc` allocation.
- a **buffer registry** `B`: a finite map from buffer identities `β` to
  records. Concretely a record is a `Buffer` struct
  (`include/runtime/types.h`):

  ```c
  typedef struct Buffer {
      void *data;             // base of the accessible byte range
      int length;             // accessible length (bytes)
      int capacity;           // allocated capacity (== length for roots)
      int ref_count;          // refcount (mutated atomically; Hemlock-level references)
      _Atomic int freed;      // 1 after free(), monotone
      struct Buffer *parent;  // NULL for roots; root buffer for slice views
      _Atomic int view_count; // roots: number of live slice views (blocks free())
  } Buffer;
  ```

A buffer is a **root** if `parent == NULL` (it owns a heap block) or a **view**
if `parent != NULL` (a zero-copy window into its root's block, created by
`.slice()`). Views are always *flattened*: `.slice()` follows the parent chain
so `parent` always points directly at a root
(`src/backends/interpreter/runtime/eval_calls.c`, slice case).

Hemlock values of type `buffer` are references `buf(β)`; values of type `ptr`
are bare integers `ptr(a)` with no attached provenance.

---

## 2. The invariants `buffer` maintains

The runtime maintains the following invariants at every observable step
(between primitive operations). Together they are inductive: each primitive
assumes them, checks its inputs, and re-establishes them.

**B1 — Root validity.**
For every live root `β` with `freed(β) = 0`: `data(β)` is the base of a live
heap block of at least `max(capacity(β), 1)` bytes, and
`0 ≤ length(β) ≤ capacity(β)`. (`buffer(n)` requires `n > 0`; zero-length
roots arise only internally — empty file reads, `read_bytes(off, 0)` — and
still carry a valid allocation.)
*Where:* `val_buffer()` allocates zero-initialized
(`src/backends/interpreter/values.c`); nothing after creation mutates
`data`/`length`/`capacity` of a live root — `buffer` has no resize operation,
and `realloc()` rejects buffers.

**B2 — Freed-state normalization.**
For every root with `freed(β) = 1`: `data(β) = NULL`, `length(β) = 0`,
`capacity(β) = 0`. Every access path bounds-checks against `length`, so an
access through *any* alias of a freed buffer fails the check `index <
length = 0` and raises a catchable error without dereferencing.
*Where:* `builtin_free()` (`src/backends/interpreter/builtins/memory.c`)
zeroes the struct under the `freed` flag.
*Test:* `tests/buffers/use_after_free_error.hml`.

**B3 — Freed monotonicity and double-free detection.**
`freed` transitions `0 → 1` exactly once, via an atomic compare-and-swap; a
second `free()` observes the CAS failure and raises "double free detected on
buffer" instead of calling `free(3)` twice.
*Where:* `builtin_free()`, `atomic_compare_exchange_strong(&buf->freed, …)`.

**B4 — View containment.**
For every view `v`: `parent(v)` is a root `r`, and the window
`[data(v), data(v) + length(v))` lies within `[data(r), data(r) + length(r))`
*as of view creation*. `.slice(start, end)` clamps `start`/`end` into
`[0, length]` of the buffer being sliced before constructing the view, so
containment holds inductively (slicing a view stays inside the view, hence
inside the root).
*Where:* slice case in `eval_calls.c` (clamping, root flattening).

**B5 — Views cannot outlive their root's allocation.**
A view retains its root (`buffer_retain(root)` at creation; released when the
view is destroyed), so the root *struct* outlives the view. Additionally,
`free()` on a root **refuses** while any view of it is alive, and `free()` on
a view is always refused ("cannot free() a buffer slice view"). Hence
`freed(r) = 1` implies no view of `r` exists, and B4's window always points
into live memory.
*Where:* view refusal in `builtin_free()`; the live-view refusal is enforced
by an atomic per-root view count (see §6, finding F1 — the original
refcount-threshold heuristic did not enforce this and permitted a real
use-after-free).
*Tests:* `tests/buffers/slice_view_free_guard.hml`,
`tests/parity/language/buffer_safety_invariants.hml` (regressions for F1).

**B6 — Complete mediation (bounds checks on every dereference).**
Every primitive that dereferences `data(β)` first verifies liveness and
`0 ≤ off ∧ off + size ≤ length(β)`, computing `off + size` in 64-bit
arithmetic so the check cannot be bypassed by `int` overflow — and raises a
catchable error *before* touching memory when any check fails. Liveness is
established in one of two equivalent ways: an explicit check of the `freed`
flag (`memcpy`/`memset`, `ptr_read_T`/`ptr_write_T`, `@stdlib/bytes`), or
B2's freed-state normalization — a freed buffer has `length = 0`, so any
bounds check against `length` fails without needing to consult the flag
(indexing, the typed `read_*`/`write_*` methods, `read_bytes`/`write_bytes`,
I/O builtins that read `data` together with `length`). The mediated
primitives are exactly:

| Primitive | Check site (interpreter) |
|---|---|
| `b[i]` read | `runtime/expressions.c` (index case, and the fast-path index case) |
| `b[i] = v` write | `runtime/expressions.c` (index-assign case) |
| `b.read_u8/…/read_f64_be(off)` | `runtime/eval_calls.c` (typed reads; per-type size) |
| `b.write_u8/…/write_f64_be(off, v)` | `runtime/eval_calls.c` (typed writes; effective check before store) |
| `b.read_bytes(off, len)` / `b.write_bytes(off, src, len)` | `runtime/eval_calls.c` (64-bit `off+len` guard) |
| `b.to_string()` | `runtime/eval_calls.c` (copies exactly `length` bytes) |
| `memset(b, byte, n)` / `memcpy(b, …, n)` | `builtins/memory.c` (`n ≤ length`, freed check, NULL check) |
| `ptr_read_T(b)` / `ptr_write_T(b, v)` | `builtins/ffi_builtins.c`, `extract_raw_ptr_checked()` (`sizeof(T) ≤ length`, freed check) |
| `@stdlib/bytes` builtins on buffers | `builtins/byteorder.c` |

A note on check *discipline*, because it differs by backend: the
interpreter's `runtime_error()` sets exception state and **returns to its
caller**, so every interpreter check is only sound if the call site
`return`s before touching memory — a per-site obligation the code comments
track explicitly. The compiled runtime's `hml_runtime_error()` is
`noreturn` (`longjmp` to the nearest `try`), so its checks are fail-safe by
construction. Identical-looking check code thus carries different proof
burdens in the two backends; the B6 table was audited under both
disciplines.

*Tests:* `tests/buffers/bounds_error.hml.expected_error`,
`tests/buffers/negative_write_no_corruption.hml` (error raised **before** the
store — an OOB write must not smash heap metadata even when caught),
`tests/buffers/ptr_write_bounds_error.hml`, `tests/buffers/memcpy_bounds_error.hml`,
`tests/buffers/memset_bounds_error.hml`.

**B7 — Encapsulation (interior pointers escape only through named unsafe doors).**
No *safe-fragment* operation returns `data(β)`, or an address derived from
it, as a Hemlock value. Buffer property access exposes only `length` and
`capacity` (integers); `.slice()` returns a `buffer` (a view, still governed
by B1–B6); `.to_string()` and `read_bytes()` return *copies*. There is no
implicit `buffer → ptr` coercion: annotating a buffer as `ptr` is a type
error (`types.c`: `ptr` accepts only `VAL_PTR`). Raw addresses of buffer
memory reach the program through exactly two doors, both on the unsafe side
of the §3 line: the explicit `buffer_ptr(b)` builtin, and FFI argument
marshalling (a buffer passed where an `extern fn` expects a pointer hands
the callee `data(β)` raw). Their obligations are in §5.

**B8 — Struct stability (registry soundness).**
The `Buffer` struct itself is released only when its atomic `ref_count`
reaches zero, and every Hemlock-visible reference (environment slot, array
element, object field, task argument, channel message, view parent link)
contributes to `ref_count`. Hence no operation ever inspects a dangling
struct: even after `free()`, aliases point at a live struct in freed-state
normal form (B2). Manual `free()` deliberately does **not** release the
struct — only the data block — precisely so B2 can protect aliases.
*Where:* `buffer_retain`/`buffer_release`/`buffer_free`
(`src/backends/interpreter/values.c`), retain/release discipline throughout
`values.c` (`value_retain`/`value_release`).

---

## 3. The safe fragment, precisely

The split is per-*operation*, not per-module. Define:

**Unsafe values.** A value of type `ptr`. The only producers are:
`alloc()`, `talloc()`, `realloc()`, pointer arithmetic (`p ± n`),
`ptr_offset()`, `buffer_ptr()`, `string.byte_ptr()`, `__string_to_cstr()`,
`ptr_null()`, FFI return values of pointer type, `mmap`-family results of
pointer kind, and addresses read back out of memory the program itself
wrote.

**Unsafe operations.** An evaluation step is *unsafe* iff it is one of:

1. any producer above;
2. `p[i]` / `p[i] = v` where `p : ptr`;
3. `ptr_read_T(p)` / `ptr_write_T(p, v)` / `ptr_deref_T(p)` where `p : ptr`;
4. `memcpy` / `memset` where **any** memory argument is a `ptr`;
5. `free(p)` / `realloc(p, n)` where `p : ptr`;
6. an FFI call (`extern fn`, `ffi_bind`) — regardless of argument types;
7. unsafe stdlib natives that traffic in raw addresses (`@stdlib/mmap`, parts
   of `@stdlib/ffi`, `@stdlib/atomic` when applied to `ptr`).

A program execution **stays in the safe fragment** iff no step is unsafe.
Syntactically sufficient condition: the program never calls any producer in
(1) and never uses `extern`/FFI — then values of type `ptr` cannot exist
(there is no `ptr` literal), so (2)–(5) are unreachable.

Note what is *in* the safe fragment: `buffer()` and `free()` on buffers,
all buffer methods, all of `memcpy`/`memset` *applied to buffers*,
`ptr_read_T`/`ptr_write_T` *applied to buffers* (despite the name — with a
buffer argument they bounds-check via `extract_raw_ptr_checked`), strings,
arrays, objects, tasks and channels, and manual `free()` of buffers, arrays,
and objects. Manual memory management is not what makes Hemlock unsafe;
*unmediated addresses* are.

---

## 4. The theorem

> **Theorem (memory safety of the safe fragment).**
> Let `P` be any Hemlock program whose execution stays in the safe fragment,
> run single-tasked. Then every load and store the runtime performs on behalf
> of `P`'s buffer operations targets a live heap block, within its bounds.
> Consequently each buffer operation either
> (i) completes normally with in-bounds accesses,
> (ii) raises a catchable Hemlock error having accessed no memory, or
> (iii) terminates the process cleanly (`panic`, resource exhaustion).
> In particular `P` exhibits no memory-related undefined behavior — no
> out-of-bounds access, no use-after-free, no double-`free(3)`, no
> overlapping-`memcpy` — from buffer usage, and a caught error leaves the
> invariants B1–B8 intact.

**Proof sketch.** By induction over the execution trace, with the machine
invariant `I = B1 ∧ … ∧ B8`.

*Base.* Initially no buffers exist; `I` holds vacuously.

*Inductive step.* Assume `I`; case-analyze the step. Steps that do not touch
buffers preserve `I` trivially (they neither mutate `Buffer` structs nor free
heap blocks — the safe fragment excludes every operation that could). The
buffer primitives:

- **`buffer(n)`**: for `n ≤ 0` the constructor raises a catchable error
  without allocating (case ii); on allocation failure it yields `null`
  (case i — `null` is a value, and any later misuse of it is itself
  mediated); otherwise it creates a fresh zero-initialized root satisfying
  B1 with `ref_count = 1`, `freed = 0`, `parent = NULL`, `view_count = 0`.
  `I` extended pointwise.
- **Reads/writes** (`b[i]`, typed `read_*`/`write_*`, `read_bytes`,
  `write_bytes`, `memset`/`memcpy` on buffers, `ptr_read_T`/`ptr_write_T` on
  buffers): by B6 the primitive first checks liveness and
  `0 ≤ off ∧ off + size ≤ length(β)` in 64-bit arithmetic. If the check
  fails, it raises before any access (case ii, `I` unchanged — checks don't
  mutate). If it passes: for a root, B1 + `freed = 0` give
  `[data+off, data+off+size) ⊆` live block; for a view, B5 gives
  `freed(root) = 0`, so B4's window is live and containment gives the same.
  The access is in-bounds into live memory (case i). No invariant is mutated
  except `bytes_ℓ`, which none of B1–B8 constrain. For the two primitives
  that copy between two buffer ranges (`memcpy(b₁, b₂, n)`,
  `b.write_bytes(off, src, n)`), both ranges may overlap (views of one
  root); the implementation uses `memmove` semantics for buffer↔buffer
  copies, so overlap is defined (see finding F2).
- **`b.slice(start, end)`**: clamps into `[0, length(b)]`, flattens to the
  root, retains the root, increments the root's view count. The new view
  satisfies B4 by clamping + induction, B5 by the retain + count. If `b` is
  freed, `length(b) = 0` (B2) clamps the window to an empty view of a freed
  root — its every access fails B6's `length = 0` check, so B5's "points
  into live memory" is never consulted. `I` preserved.
- **`free(b)`, `b` a buffer**: refused (case ii, no mutation) if `b` is a
  view, if a view of `b` is alive (B5), if a conservative shared-reference
  heuristic trips (interpreter only; refusal is always the safe direction),
  or — after those guards — if the CAS on `freed` fails (double free, B3).
  Otherwise `b` is a root with no views; the CAS marks it freed exactly
  once; `free(3)` is called on the block base `data(β)` — valid by B1, and
  only reachable once by B3 — and the struct is normalized per B2. The
  struct persists for aliases (B8). All invariants re-established.
- **`b.to_string()`, `read_bytes`, `@stdlib/bytes` reads**: copy out of a
  mediated range (B6) into fresh allocations; no aliasing of `data(β)`
  escapes (B7).
- **Scope exit / last release**: `buffer_release` frees the struct at
  `ref_count = 0`; for a view this releases the root and decrements its view
  count; for an unfreed root this frees the data block — safe because
  `ref_count = 0` means no Hemlock value, view, or container references it
  (B8), so no future step can name it.

Since every step preserves `I` and every dereference is mediated by B6 into
memory proven live by B1/B2/B4/B5, no access in the trace is out-of-bounds or
dangling. ∎

### Audit assumptions

The proof is against the C implementation, so it rests on finitely many
auditable assumptions rather than a mechanized model:

- **A1 (retain discipline).** Every code path that stores a buffer reference
  into a reachable location retains it (B8). Spot-audited in `values.c`,
  `environment.c`, container element paths; not exhaustively verified.
- **A2 (mediation completeness).** The table in B6 lists *every* primitive
  that dereferences `data(β)`. Established by auditing all
  `as_buffer`/`->data` uses across `src/backends/interpreter` and the
  compiled runtime; future code that reaches into `->data` with a
  caller-controlled offset must add itself to the table (and a test).
- **A3 (backend parity).** The compiled runtime (`runtime/`,
  `libhemlock_runtime.a`) performs the same checks as the interpreter.
  Guarded by parity tests (`tests/parity/`); divergences are bugs.
- **A4 (host correctness).** libc `malloc`/`free`/`memmove` and the C
  compiler are correct; the interpreter's own C code has no unrelated UB.

---

## 5. Proof obligations at each `ptr` crossing

Outside the safe fragment, safety is not lost — it becomes *conditional*.
Every unsafe operation has a precondition. This is exactly RustBelt's
semantic story scaled down: the safe API is verified once, unconditionally
(§4); each unsafe operation is an axiom whose precondition the *programmer*
must discharge; a program that discharges every obligation composes back into
the whole-program safety guarantee.

Terminology for obligations:

- **valid(p, n)** — `p` points into a live allocation with at least `n`
  bytes remaining from `p`; the allocation was obtained from
  `alloc`/`talloc`/`realloc`, FFI, or `mmap`, and has not been freed,
  reallocated, or unmapped.
- **owned(p)** — `p` is exactly the base address of a live allocation that
  the program is responsible for releasing, and no other agent will release it.
- **aligned(p, T)** — `p` is suitably aligned for type `T`. (Addresses from
  `alloc`/`talloc` are max-aligned at base; interior addresses are the
  programmer's problem.)
- **noalias(p, q, n)** — the ranges `[p, p+n)` and `[q, q+n)` do not overlap.

| Crossing | Signature | Obligation (precondition) | What you get / UB on violation |
|---|---|---|---|
| `alloc(n)` / `talloc(T, c)` | `→ ptr` | none (sizes validated; overflow-checked) | fresh `owned(p)`, `valid(p, n)`; contents **indeterminate** |
| `realloc(p, n)` | `ptr → ptr` | `owned(p)` | old `p` (and every copy/offset of it) is **dead**; new result is `owned` |
| `free(p)` (`p : ptr`) | `ptr → null` | `owned(p)`, first free | double free / interior free = heap corruption (libc abort at best) |
| `p + k`, `p - k`, `ptr_offset` | `ptr → ptr` | none to *compute* | result carries obligation: only dereference under `valid` within the **same** allocation |
| `p[i]` read | `→ u8` | `valid(p + i, 1)` | OOB/dangling read |
| `p[i] = v` write | | `valid(p + i, 1)` and no live view depends on byte immutability you're breaking | OOB/dangling write |
| `ptr_read_T(p)` / `ptr_deref_T(p)` | `ptr → T` | `valid(p, sizeof T)` ∧ `aligned(p, T)` | misaligned/OOB read (the implementation dereferences `*(T*)p` directly) |
| `ptr_write_T(p, v)` | | `valid(p, sizeof T)` ∧ `aligned(p, T)` | misaligned/OOB write |
| `memset(p, b, n)` (`p : ptr`) | | `valid(p, n)` | OOB write |
| `memcpy(d, s, n)` (either arg `ptr`) | | `valid(d, n)` ∧ `valid(s, n)` ∧ `noalias(d, s, n)` | OOB access or overlapping-copy UB (raw-`ptr` copies use C `memcpy`) |
| `buffer_ptr(b)` | `buffer → ptr` | freed-buffer input is rejected (catchable error); the returned address does **not** retain `b` — use only while `b` is provably live and unfreed, within `[p, p + b.length)` | dangling interior pointer after `free(b)` or last release of `b` |
| `string.byte_ptr()` | `→ ptr` | use only while the string is not mutated, grown, or released — any string method, index-assign, or scope exit may invalidate it (in compiled code small strings are stored inline in the struct, so the pointer also dies with the *struct*) | dangling interior pointer |
| `__string_to_cstr(s)` | `→ ptr` | resulting allocation is `owned` by caller (must `free`) | leak (safe) or double free |
| `extern fn` / `ffi_bind` call | any | callee respects the C ABI and the documented contract of every pointer passed: buffers passed to FFI expose `data(β)` for `length(β)` bytes **for the duration of the call only**; callee must not retain, resize, or free them | arbitrary UB — FFI is a full trust boundary |
| FFI callback (`callback()`) | `→ ptr` | C caller invokes it with ABI-conformant arguments; `callback_free` exactly once, and not while C may still call it | arbitrary UB |
| `@stdlib/mmap` | `→ mapping` | accesses within `[addr, addr+len)` before `mmap_close`; no access after | SIGSEGV/SIGBUS (bounds are checked for buffer-typed reads; raw-pointer access is unmediated) |
| `@stdlib/atomic` on `ptr` | | `valid(p, size)` ∧ `aligned(p, size)` | UB; on buffers the operations are mediated |

Two structural notes:

1. **Buffers passed where a `ptr` is accepted are still safe.** `memcpy`,
   `memset`, `ptr_read_T`, `ptr_write_T`, and `@stdlib/bytes` all detect a
   buffer argument and bounds-check it (B6). The obligation table applies
   only when the argument is *literally* a `ptr`. The one nuance: passing a
   buffer to **FFI** hands the callee a raw `data(β)` — that crossing is
   unsafe no matter the argument type, and its obligation (duration-of-call,
   no retention, ≤ `length` bytes) is on the caller.
2. **Obligations are per-address, not per-variable.** Copying a `ptr` copies
   its obligations; `realloc`/`free` kill *all* copies. Hemlock tracks no
   provenance at runtime — that is the point of the design — so nothing
   detects a violated obligation. The advisory borrow checker
   (`docs/advanced/borrow-checker.md`) statically flags the common violations
   (use-after-free, double free, leaks) but is deliberately non-authoritative.

---

## 6. Findings: what the audit had to fix (and what it flags)

The theorem in §4 is true of the implementation *after* the following
repairs. In RustBelt terms these were soundness bugs in the trusted library:
counterexamples reachable from the safe fragment. They are listed here
because the honest form of the claim includes its history and its edges.
Each was demonstrated empirically before the fix and is pinned by a
regression test after it.

**F1 — Use-after-free through slice views (fixed, both backends).**
The intended B5 invariant was enforced by nothing that worked:

- *Interpreter:* `free(b)` guarded against outside references with a
  refcount **threshold heuristic** (`ref_count > 3`, "creation + env +
  env_get"). A live view raises the root's refcount, but not past the
  threshold in ordinary code, so `free(b)` succeeded while a view was
  alive. The view kept its stale `length` and dangling window: `s[0]` then
  *read*, and `s[1] = v` *wrote*, freed heap memory — from pure
  safe-fragment code.
- *Compiled runtime:* `hml_free()` had **no reference guard at all**, so
  the same program was a use-after-free with even less standing in the way.
- The interpreter additionally performed the freed-flag CAS *before* the
  refcount guard, so a refused free left the buffer poisoned (marked freed,
  data leaked, unusable).

**Fix:** roots carry an atomic `view_count`; `.slice()` increments it, view
destruction decrements it, and `free()` on a root with live views raises a
catchable "Cannot free buffer with N live slice views" in both backends —
enforcing B5 directly. The interpreter's guards now run before the CAS. A
compiled-backend corollary: `free()`'s thrown refusals `longjmp` and used to
leak the argument temporary's reference (keeping the view — and therefore
the root's un-freeability — alive forever); codegen now emits an
exception-safe `hml_free_owned()` that consumes the reference on both paths.
*Regression tests:* `tests/buffers/slice_view_free_guard.hml`,
`tests/parity/language/buffer_safety_invariants.*`.

**F2 — Overlapping-copy UB between views (fixed, both backends).**
`memcpy(s1, s2, n)` and `big.write_bytes(off, s2, n)` where `s1`, `s2` are
overlapping views of one root passed both bounds checks and then invoked C
`memcpy` on overlapping ranges — undefined behavior (C11 §7.24.2.1)
reachable from the safe fragment. **Fix:** buffer↔buffer copies use
`memmove`. Raw-`ptr` copies keep `memcpy` and the `noalias` obligation —
that world is explicitly the programmer's responsibility.
*Regression tests:* `tests/buffers/overlap_copy.hml`,
`tests/parity/language/buffer_safety_invariants.*`.

**F3 — `buffer(0)` divergence: hard exit vs. silent success (fixed, both backends).**
The interpreter's `val_buffer(0)` printed "buffer size must be positive" and
called `exit(1)` — safe but uncatchable, and inconsistent with `alloc(0)`,
which throws. The compiled runtime silently produced a 0-length buffer for
`buffer(0)` and a `null` value for `buffer(-1)` (which later code would
happily dereference). **Fix:** the user-facing constructor rejects
`size ≤ 0` with the same catchable error in both backends; zero-length
buffers remain valid *internal* values (empty file reads,
`read_bytes(off, 0)`, deep copies of freed buffers) and always carry a
valid non-NULL allocation.

**F4 — Buffer contents were uninitialized in the interpreter (fixed).**
Interpreter buffers were `malloc`'d while compiled buffers were `calloc`'d:
`buffer(64)[0]` returned recycled heap bytes under the interpreter and `0`
under the compiler — an information leak and a real observable-semantics
parity break. **Fix:** both backends zero-initialize. (This also upgrades
the theorem: fresh-buffer reads are now deterministic, closing the
"indeterminate contents" caveat for the current implementation.)

**F5 — `buffer_ptr()` accepted freed buffers (fixed, both backends).**
The escape hatch returned `data(β)` with no checks, so on a freed buffer it
returned `NULL` which downstream unsafe code would dereference without
diagnosis. It now raises a catchable error for freed input. Its lifetime
obligation (no retention — the address dies with the buffer) remains the
caller's, per §5.

**F6 — Documentation used a nonexistent deref syntax (fixed).**
`docs/language-guide/memory.md` demonstrated raw pointers with `*p = 65;` —
which does not parse; Hemlock has no unary dereference operator. The actual
raw-pointer access surface is exactly: `p[i]`, `ptr_read_T`/`ptr_write_T`/
`ptr_deref_T`, `memcpy`/`memset`, pointer arithmetic, and FFI. The examples
now use `p[i]`, and the obligation table in §5 covers the real surface.

**F7 — Residual flags (documented, not changed here).**
Known rough edges that do not break the sequential theorem but deserve
future attention: the interpreter's `ref_count > 3` free guard remains a
heuristic (kept as an extra conservative layer; the view count now carries
the soundness argument); `Buffer.ref_count` is declared plain `int` in the
interpreter (mutations use atomic builtins, but reads such as the free
guard are plain loads) versus `_Atomic int` in the compiled runtime;
pointer-arithmetic offsets truncate to 32 bits in the interpreter but are
64-bit in compiled code; the typed `write_*` methods' inner per-suffix
checks rely on the outer `buffer_typed_access_size` check having covered
every suffix (a new suffix added without updating that helper would
silently skip the effective check — see the audit note in
`src/backends/interpreter/runtime/eval_calls.c`); and several I/O builtins
(`socket.send`, `file.write_bytes`, `write_file`, compression, crypto)
omit the explicit freed check and rely solely on B2's length-zeroing,
which is sound but leaves `fwrite(NULL, 1, 0, …)`-shaped pedantic-UB edges
on already-freed inputs.

---

## 7. What the theorem does not cover

Stating scope honestly is part of the claim.

- **Initialization.** Fresh buffers are zero-initialized in both backends
  (since F4), so this is no longer a caveat of the *implementation* — but
  the theorem does not depend on it. Raw `alloc()` memory remains
  indeterminate, as documented.
- **Concurrency.** With tasks, `freed` and the view count are atomic, and
  sharing a buffer across tasks keeps its struct alive (B8), so the
  *allocation-level* argument mostly survives. The sharing routes matter:
  `spawn()` **deep-copies** its arguments, so a spawned task gets its own
  buffer; only **channels** (`ch.send(b)` retains, it does not copy)
  create genuinely shared mutable buffers. Shared buffers admit: content
  races (two tasks writing `b[i]` — formally a C-level data race,
  practically byte tearing); check-then-act windows (a bounds check
  passing in task A while task B's `free()` nulls `data` before A's
  dereference — the flag CAS and the `data`/`length` stores are not one
  atomic unit as observed by readers); a `.slice()` racing `free()` past
  the view-count check; and the interpreter's non-atomic reads of
  `ref_count` in the free guard. The theorem as stated is for single-task
  executions; the multi-task extension needs either data-race freedom as a
  premise (don't `free` or mutate a channel-shared buffer without
  coordination) or per-buffer synchronization, and is future work. Use
  channels for handoff or `@stdlib/atomic` to coordinate.
- **Resource exhaustion.** Leaks are safe. Allocation failure returns
  `null` (`buffer()`/`alloc()` on OOM) or exits; neither is UB.
- **Panics and process exits.** `panic()` and stack exhaustion end the
  process; "clean termination" is within the theorem's case (iii).
- **The trusted computing base.** The theorem trusts: the interpreter and
  compiled runtime C code (assumptions A1–A4), libc, the C compiler, the
  OS. Native stdlib modules are *inside* the TCB when they reach into
  buffers; the B6 table plus A2 is the discipline that keeps them honest.
  FFI is not covered at all: one `extern` call is one unbounded axiom.
- **The borrow checker is advisory.** Its warnings (use-after-free, double
  free, leaks on the `ptr` side) are diagnostics, not premises; the theorem
  never relies on it.

---

## 8. Relation to RustBelt, and what a mechanization would look like

RustBelt proves: (1) a semantic model of Rust's types in Iris/Coq, (2) the
fundamental theorem — syntactically well-typed safe code is semantically
safe, and (3) that each `unsafe`-using library (Cell, Rc, Mutex, …) inhabits
its semantic type, so linking it with safe code preserves safety.

Hemlock's scaled-down analog, as realized in this document:

1. The machine model of §1 plays the role of the semantic domain; B1–B8 are
   the semantic interpretation of the type `buffer` (a predicate on machine
   states, i.e., precisely an invariant in the separation-logic sense: the
   root's block is owned by the buffer runtime; views hold a fraction tied
   to the view count).
2. §4 is the fundamental theorem for the (much smaller) safe fragment:
   safety holds *unconditionally*, by complete mediation rather than by a
   type system — Hemlock is dynamically checked, so where Rust discharges
   bounds at compile time, Hemlock's semantic typing lives in the runtime
   checks themselves. What is proved is that the checks are *complete* (A2)
   and *sound* (B1–B8 imply checked accesses hit live memory).
3. §5 is the per-library obligation catalog: each unsafe operation is
   specified by a Hoare-style precondition. A Hemlock program that wraps its
   `ptr` usage in a module which (a) maintains a stated invariant and (b)
   discharges every §5 obligation under that invariant is exactly a
   "RustBelt library": the composed program regains the §4 guarantee. The
   `buffer` runtime itself is the first such library — implemented on raw
   memory, verified here once, so that no user ever has to re-prove it.

A mechanization would formalize the machine of §1 in Coq/Iris (or even a
first-order Hoare logic — no higher-order ghost state is needed at this
scale, which is what makes the project tractable), give each B-invariant as
a separation-logic assertion, and verify the ~20 mediated primitives of B6
against them. The audit assumptions A1–A3 become proof obligations on the C
code (or on a verified reimplementation of the ~600 lines that constitute
the buffer runtime). Nothing in Hemlock's semantics requires the
higher-order machinery that makes RustBelt hard; the hard part — stating the
invariants so that every primitive preserves them and every crossing is
cataloged — is this document.

---

## 9. Test anchors

Every load-bearing claim above is pinned by an executable test:

| Claim | Test |
|---|---|
| OOB read/write raises catchable error, before any access | `tests/buffers/bounds_error.hml.expected_error`, `tests/buffers/negative_write_no_corruption.hml` |
| Use-after-free through any alias raises, never dereferences | `tests/buffers/use_after_free_error.hml` |
| Double free detected | `tests/buffers/use_after_free_error.hml` |
| `free()` refused on views; refused on roots with live views (F1) | `tests/buffers/slice_view_free_guard.hml` |
| Overlapping buffer↔buffer copies are defined (F2) | `tests/buffers/overlap_copy.hml` |
| `buffer(0)` raises catchably; fresh buffers read as zero (F3, F4) | `tests/parity/language/buffer_safety_invariants.hml` |
| `ptr_read/write_T` on buffers is bounds-checked | `tests/buffers/ptr_write_bounds_error.hml` |
| `memcpy`/`memset` on buffers respect `length` | `tests/buffers/memcpy_bounds_error.hml`, `tests/buffers/memset_bounds_error.hml` |
| Backend parity on F1–F4 and the freed/double-free/view guards | `tests/parity/language/buffer_safety_invariants.hml` + `.expected` (`make parity`) |

---

## See also

- [Memory Management](../language-guide/memory.md) — the user-facing guide
- [Memory Ownership](../advanced/memory-ownership.md) — ownership across tasks
- [Borrow Checker](../advanced/borrow-checker.md) — advisory static analysis
- [Design Philosophy](philosophy.md) — why the split exists
- Jung, Jourdan, Krebbers, Dreyer. *RustBelt: Securing the Foundations of
  the Rust Programming Language.* POPL 2018 — the full-scale version of
  this document's program.

# Hemlock 3 · Language Changes

Everything here applies to `edition = "2026"` (see
[06-migration.md](06-migration.md) for how 2.x code keeps running).

---

## 1. Scoped `unsafe` — the keystone change

### Rationale

Hemlock 2.x is *ambiently* unsafe: any line of any file may do raw
pointer arithmetic. That makes "audit the unsafe parts" impossible — the
unsafe part is the whole program. Hemlock 3 keeps every capability and
adds one requirement: **operations that can corrupt memory must appear
inside an `unsafe { }` block.** This is *more* explicit, not less — fully
aligned with the core philosophy. After the change,
`grep -rn "unsafe" src/` is a complete audit surface, and the marketing
claim "unsafe is a scope" is literally true.

### Operations requiring `unsafe`

| Operation | Examples |
|-----------|----------|
| Raw pointer dereference | `ptr_read_*(p, …)`, `ptr_write_*(p, …)` on a `ptr` |
| Pointer arithmetic | `p + n`, `p - n` where `p: ptr` |
| Pointer casts | `ptr` → integer, integer → `ptr` |
| Taking interior pointers | `buf.ptr()`, `s.byte_ptr()` (the *use* of the result is what's dangerous, so acquiring it is gated) |

### Operations NOT requiring `unsafe`

- `alloc` / `free` / `realloc` / `memset` / `memcpy` with `buffer`
  arguments — allocation itself doesn't corrupt memory; `buffer` stays
  bounds-checked.
- All `buffer` methods (bounds-checked by definition).
- FFI: `extern fn` declarations and `ffi_bind` are already explicit,
  per-call-site opt-ins; they do not additionally require `unsafe`
  blocks. (Stdlib FFI wrappers would otherwise force `unsafe` onto every
  consumer, destroying the signal.)
- `defer`, `spawn`, channels, everything else.

### Syntax

```hemlock
let p = alloc(64);              // fine anywhere
unsafe {
    ptr_write_u32(p, 0xDEADBEEF);
    let q = p + 4;              // pointer arithmetic
    ptr_write_u32(q, 42);
}
free(p);                        // fine anywhere

// Function-level form for ptr-heavy code:
unsafe fn poke(p: ptr, off: i32, v: u8) {
    ptr_write_u8(p + off, v);
}
```

`unsafe` is a **lexical marker, not an effect system**: calling an
`unsafe fn` does not require an `unsafe` block at the call site. The goal
is greppable audit surface and reviewer attention, not Rust-grade proof.
(A dynamic language cannot honestly promise the latter; promising the
former and delivering it builds more trust than overclaiming.)

### Enforcement

- Parser attaches an `in_unsafe` flag to scopes; the gated operations
  raise a compile-time error (hemlockc) / parse-time error (interpreter)
  outside `unsafe`.
- `edition = "2024"` code: gated operations produce a *warning* with the
  file:line, so existing programs run unmodified while migration happens.
- `hml fix --edition 2026` auto-wraps offending statements (see
  [06-migration.md](06-migration.md)).

### Debug-mode allocator guards

In `hml run` and unoptimized builds, `free()` goes through a tracking
allocator: double-free and free-of-non-allocation abort with a message
and backtrace instead of corrupting the heap. Release builds (`hml build
--release`) keep raw libc speed. This converts the two most common memory
bugs from "silent corruption" to "immediate diagnostic" during
development without taxing production. Opt out with `HML_NO_ALLOC_GUARD=1`.

---

## 2. Structured errors

2.x exceptions are bare values with no trace. 3.0 keeps `throw`-anything
but adds a standard error shape and tracebacks.

```hemlock
// New builtin:
let e = error("connection refused", { host: host, port: port });
e.message;      // "connection refused"
e.data;         // { host: ..., port: ... }
e.stack;        // array of { function, file, line } captured at error()

throw e;
```

- `throw` of any value remains legal (edition-stable).
- **Uncaught exceptions print a full stack trace** (function, file, line)
  in both backends — extending the existing fatal-signal backtrace
  handler to the exception path.
- `catch (e)` is unchanged; `typeid(e)` distinguishes error objects from
  legacy thrown strings.
- The exception-path temporary-leak gap (documented in
  `docs/plans/MEMORY_LEAK_PREVENTION_PLAN.md` Gap 1) is a 3.0 release
  blocker: unwinding must release temporaries.

`panic()` is unchanged: immediate, uncatchable, now always with
backtrace.

---

## 3. Strings, bytes, and the UTF-8 boundary

Production experience (Witchgrid): `Content-Length` computed from
`.length` (runes) instead of `.byte_length` (bytes) truncated non-ASCII
responses; bytes→string required the undocumented `__string_from_bytes`;
byte-as-rune decoding produced double-encoded mojibake.

### 3.1 Promote the hidden conversions

```hemlock
let s = string.from_bytes(buf);          // buffer | array<u8> → string (validates UTF-8)
let s = string.from_bytes(buf, "latin1"); // optional encoding for lossy sources
let b = s.to_bytes();                     // already exists; unchanged
```

`string.from_bytes` replaces `__string_from_bytes`, which stays as a
deprecated alias for one minor cycle. Invalid UTF-8 throws an `error`
unless an encoding argument says otherwise — silent mojibake is exactly
the bug class this kills.

### 3.2 Length is never ambiguous again

- `.byte_length` (bytes) and `.length` (runes) both remain — but the LSP
  and `hml lint` flag `.length` used in byte-position contexts
  (`Content-Length`, `write`, buffer sizing) via a builtin lint.
- New: `s.rune_count` as an explicit synonym of `.length`, so new code
  can self-document. Docs and examples use `rune_count`/`byte_length`
  exclusively.

### 3.3 Method gaps (all straight from consumer code)

| New | Semantics |
|-----|-----------|
| `s.rfind(needle)` | byte index of last occurrence, `-1` if absent |
| `s.substr(start)` | one-argument form: from `start` to end |
| `f32.from_bits(u: u32)` / `f64.from_bits(u: u64)` | bit-cast (IEEE 754 reinterpret) |
| `f.to_bits()` | inverse bit-cast |

Buffers already have the full `read_*/write_*` LE/BE matrix
(`docs/reference/memory-api.md`); the bit-casts close the remaining gap
(GGUF-style parsers that read a `u32` and need the `f32` it spells).

---

## 4. Object literals: method syntax

2.x rejects `fn` inside object literals, forcing `name: fn() {}`. 3.0
accepts the method form gn.hml asked for:

```hemlock
let server = {
    port: 3000,
    fn listen(self) { ... },        // sugar for listen: fn(self) { ... }
    fn on(self, event, cb) { ... },
};
```

Pure parser sugar; no semantic change; `define` method signatures already
use this syntax, so the grammar becomes uniform.

---

## 5. Side-effect imports

```hemlock
import "@stdlib/init_terminal";   // executes module top-level, binds nothing
import "./register_tests.hml";
```

Already proposed in `docs/proposals/`; promoted to 3.0. This removes the
"every test file must export `register()` and the runner must call it"
pattern in Witchgrid's test suite.

---

## 6. Compile-time asset embedding

The Go-`embed.FS` request, currently solved by a 5,500-line generated
string-constant module in Witchgrid:

```hemlock
const INDEX: string  = embed("assets/index.html");   // UTF-8 validated
const FONT: buffer   = embed_bytes("assets/font.woff2");
const ASSETS: object = embed_dir("assets/");          // { "rel/path": buffer }
```

- Paths resolve relative to the source file, at **bundle/compile time**
  (compiler: baked into the C output; interpreter: loaded eagerly at
  module load so behavior is identical — parity test required).
- Only legal in `const` initializers at module top level, so the embed
  set is statically knowable by the bundler and `hml build`.

---

## 7. Minimal generics (parameterized types)

The survey's "no generics" gap, scoped deliberately small. Type
parameters on `define` and `fn` are **checked constraints, not
monomorphized templates**:

```hemlock
define Pair<T> { first: T, second: T }

fn max<T>(a: T, b: T): T {
    if (a > b) { return a; }
    return b;
}

let p: Pair<i32> = { first: 1, second: 2 };   // ok
let q: Pair<i32> = { first: 1, second: "x" }; // runtime type error (interpreter),
                                              // compile error (hemlockc)
```

- Interpreter: `T` binds to the first concrete type seen and is enforced
  for the rest of the call/object — same machinery as existing
  `array<i32>` element checking, generalized.
- Compiler: static inference where types are known; may monomorphize as
  an optimization, never as a semantic requirement.
- Explicitly **out of scope**: trait bounds, variance, higher-kinded
  anything. If a constraint can't be expressed, write dynamic code — the
  language is dynamic by default, typed by choice.

---

## 8. Deliberately unchanged

Reaffirmed after considering (and rejecting) changes:

- **`/` always returns float; `divi()` for integer division.** Changing
  this silently alters arithmetic in every existing program — worst
  possible breakage class. Stays.
- **Mandatory semicolons, no ASI.** Identity-level.
- **Mutable strings via index assignment.** Documented semantic with
  tests; no consumer complained.
- **No GC, no auto-free, no RAII.** `defer` plus the new debug allocator
  guards are the assistance budget.
- **Type promotion ladder** (i8 → … → f64, floats win, i64/u64 + f32 →
  f64). Settled, precision-preserving, stays.

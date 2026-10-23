# Formal Verification

Machine-checked proofs about the pure core of Hemlock's runtime: the parts
both backends share or must agree on, and where a bug is a silent wrong
answer rather than a crash.

| Area | Code | Tool | What is proved |
|------|------|------|----------------|
| UTF-8 | `src/shared/utf8.c` | CBMC | round-trip for every scalar value; decoders memory-safe on arbitrary bytes |
| Type promotion | `src/shared/type_promotion.c` | CBMC | commutative, associative, monotone, matches the documented lattice |
| Integer ops (compiled) | `runtime/src/builtins_ops.c` | CBMC | `+ - * % & \| ^ << >>`, comparisons, unary `- ~ ++ --` match an executable spec for **every** type pair and **every** value; no UB (`*` and `%`: see method notes) |
| Integer ops (interpreter) | `src/backends/interpreter/runtime/binary_ops.c` (`eval_binary_values`) | CBMC | same spec, so passing both = a parity proof |
| Channels | `src/shared/chan_core.c` | TLA+ / TLC | FIFO, no duplicate or lost items, send status tells the truth, no lost wakeups / deadlock |

The CBMC proofs are exhaustive over their inputs: a `VERIFICATION SUCCESSFUL`
for `rt_add` means there is no pair of integer types and no pair of values for
which `hml_binary_op(ADD, ...)` disagrees with the spec, overflows, or invokes
undefined behavior. The string harnesses are bounded: arbitrary strings of up
to 4 bytes for navigation and validation, 6 for memory safety. That covers
every UTF-8 sequence shape, truncated or not, and every pair of adjacent
sequences up to 4 bytes in total.
The TLA+ model is exhaustive over 2-3 senders/receivers and capacities 0-2.

## Running

```bash
verification/run.sh              # everything (~31 min on 4 cores)
verification/run.sh utf8 chan    # subset, by name substring
KEEP_LOGS=/tmp/v verification/run.sh rt_shr   # keep logs to read counterexamples
make verify                      # same as run.sh
```

Every proof in `proofs.txt` has an expected verdict. `pass` proofs must
verify; `fail` proofs are known, accepted properties that do not hold (today
only the lossy promotion pairs, finding 7) and must still produce a
counterexample. The run fails on any mismatch, so a regression
breaks it, and so does fixing a defect without flipping its line to `pass`.

### Tools

| Tool | Version used | Install |
|------|--------------|---------|
| [CBMC](https://github.com/diffblue/cbmc) | 6.4.1 | release `.deb` (`ubuntu-24.04-cbmc-6.4.1-Linux.deb`); the Ubuntu package is too old |
| [Bitwuzla](https://github.com/bitwuzla/bitwuzla) | 0.8.2 | static release zip, `bitwuzla` on `PATH`; used by `@bitwuzla` proofs (all integer ops). Without it they fall back to CaDiCaL, which does not finish `*`/`%` in practical time |
| [TLA+ tools](https://github.com/tlaplus/tlaplus) | 1.8.0 | `tla2tools.jar` in `verification/.cache/` (or `TLA2TOOLS_JAR=`), Java 11+ |

`.github/workflows/verify.yml` installs exactly these.

## Layout

```
verification/
├── proofs.txt                 manifest: proof -> harness, sources, flags, expected verdict
├── run.sh                     runner
├── cbmc/
│   ├── harnesses/
│   │   ├── nondet.h           unconstrained inputs
│   │   ├── int_spec.h         executable spec of integer operators (backend-independent)
│   │   ├── int_binop_runtime.c / int_binop_interp.c   per-backend adapters
│   │   ├── int_unary_runtime.c
│   │   ├── type_promotion*.c
│   │   └── utf8_*.c
│   └── stubs/runtime_errors.c error entry points: "raise only if the spec allows it"
└── tla/
    ├── Channel.tla            PlusCal model of chan_core.c (+ generated TLA+)
    └── *.cfg                  model-checking configurations
```

### Method notes

- **Specs are independent of the code.** `int_spec.h` computes results with
  `__int128` from the documented rules; it does not call `hml_tk_promote` or
  `__builtin_*_overflow`. The promotion proof likewise compares against its
  own table of the lattice.
- **Errors are modelled, not ignored.** `hml_runtime_error*` (compiled) and
  `runtime_error*` (interpreter) are stubbed to assert that the spec says this
  operation raises; the harness asserts the converse after a normal return.
- **Unreachable code is checked, not assumed.** The interpreter harness cuts
  string/serialization paths of `eval_binary_values` with `@unreachable`, which
  replaces them with `assert(false); assume(false)`, so reaching one is a
  failure.
- **UB is checked without `-fwrapv`.** The native builds use `-fwrapv`, the
  WASM builds (`WASM_CFLAGS`, `WASM_CFLAGS_INTERP`) do not, so every CBMC proof
  runs `--signed-overflow-check`.
- **Condition variables get POSIX's weakest guarantees** in the TLA+ model:
  `signal` wakes any one waiter, spurious wakeups happen, and a timed wait may
  time out *after* being signalled (consuming the signal). The model was
  mutation-tested: replacing the rendezvous `broadcast` with `signal`
  deadlocks, and replacing the `recv_gen` handoff check with "slot is empty"
  violates `SendContract`.

- **`*` and `%` are stated in each backend's arithmetic shape.** For `+ -`
  the spec computes the exact result in `__int128` and range-checks it. For
  `*` and `%` that asks the solver to prove two differently-shaped
  multipliers or dividers equivalent, which no solver here finishes; it does
  not even finish relating a 32-bit and a 64-bit divider as an isolated
  lemma. So `int_spec.h` computes them exactly in the shape the backend
  under proof uses: 64-bit then truncate for the compiled runtime
  (`SPEC_SHAPE_RUNTIME`), C's native promotions for the interpreter. Both
  are exact, because promoted operands always fit. Operands are converted by
  C cast, asserted equal to the `spec_wrap` conversion. These proofs also
  run per type pair (`@pairs` in `proofs.txt`), so each proof sees a single
  width.
- **`__builtin_{add,sub,mul}_overflow` are replaced, not trusted.**
  `cbmc/stubs/overflow_builtins.h` is force-included into every CBMC proof.
  It redefines them as exact `__int128` arithmetic plus a range check. This
  is also required for soundness: CBMC (6.4.1 and 6.8.0) encodes the
  builtins' `{value, overflow}` result for Bitwuzla, which has no datatypes,
  by concatenating the fields, then reads them back from the wrong bit
  positions. Under `--bitwuzla` that both "refutes" true overflow facts
  (`ov == (exact product out of range)`) and "proves" false ones. Any CBMC
  proof using these builtins with `--bitwuzla` needs this header.
- **The interpreter harness enters at `eval_binary_values`**, the part of
  `eval_binary_expr` after operand evaluation, extracted for this purpose.
  Putting an `Expr` in the program state trips a CBMC 6.4.1 bug: unions with
  a `double`-bearing member produce invalid SMT2.

### What is not covered

- **Checked `i64 × i64` multiply** (`rt_mul_i64_i64`, `in_mul_i64_i64`,
  marked `skip`). Full-width 64×64 multiplier equivalence did not finish on
  Bitwuzla, CaDiCaL, or `--refine-arithmetic`, even with one operand bounded
  to 8 bits. All other 63 multiply pairs are proved, including `i64` with
  every narrower type and `u64 × u64`.
- Everything not in the table: the object/array/refcount machinery,
  `select()` (not in the channel model), FFI, I/O, the parser and codegen,
  and float arithmetic beyond promotion.
- Proofs are about the C as written, compiled by CBMC's front end. They say
  nothing about GCC or Emscripten miscompiling it.

## Findings

Every finding below was fixed in the same change that added these proofs.
Where the backends disagreed, the interpreter's behavior was taken as the
reference. The old repros are now parity tests
(`tests/parity/language/shift_mixed_promotion.hml`,
`tests/parity/language/string_malformed_utf8.hml`), and the corresponding
proofs flipped from `fail` to `pass`.

### 1. String index assignment wrote one uninitialized byte (both backends)

`s[i] = r` sized the hole with `hml_utf8_encode_len(r)` and wrote it with
`hml_utf8_encode(r)`. For `r > 0x10FFFF` these disagreed (4 vs 3, since
`encode` substitutes U+FFFD), so one byte of fresh `malloc` memory stayed in
the string:

- Interpreter, integer RHS never range-checked:
  `s[1] = 0x7FFFFFFF` gave `[97, 239, 191, 189, 0, 99]`.
- Compiled, a rune decoded from malformed input (up to `0x1FFFFF`):
  `s[1] = from_bytes([0xF7,0xBF,0xBF,0xBF])[0]` gave
  `[97, 239, 191, 189, 121, 99]`.

**Fix:**
- `encode_len` agrees with `encode` for every `uint32_t`
  (`utf8_encode_len_total`).
- Decoders return only Unicode scalars, mapping out-of-range sequences to
  U+FFFD (`utf8_decode_memsafe`). Previously the interpreter `exit(1)`ed on
  reading such a rune ("Invalid Unicode codepoint"). That crash was not kept
  as reference behavior.
- The interpreter now range-checks integer RHS, throwing "out of range for
  rune" like its own `rune(x)` and like compiled code.

### 2. `>>` gave different answers on the two backends

With an unsigned promoted type and a narrower negative signed left operand,
the compiled runtime shifted the sign-extended 64-bit value. The interpreter
shifts the converted value.

```
                      interp     compiled (before)
i8(-1)   >> u8(1)       127        255
i8(-117) >> u32(10)     4194303    4294967295
i16(-2)  >> u16(4)      4095       65535
```

**Fix:** the compiled generic path now shifts in the promoted type
(`rt_shr`, `in_shr`).

### 3. Negative shift amounts: interpreter returned 0, compiled threw

The interpreter converts the shift amount to the promoted type before checking
its sign, so `u32(1) >> -1` is `0` (amount `0xFFFFFFFF` ≥ width), while
compiled code threw. **Fix:** compiled code now does the same; the rule is
written into `int_spec.h` ("both operands are converted to R, including the
shift amount, read as int64"). A `u64` amount ≥ 2⁶³ therefore throws on both
backends (`rt_shl`, `in_shl`).

### 4. Index-based and iterator-based string walking disagreed on malformed UTF-8

`.length` / `s[i]` counted non-continuation bytes. `chars()` (both backends)
and compiled `for-in` advanced by the lead byte's nominal length. Compiled
`for-in` also wrote its count into the string's shared `char_length` cache,
so a later `s[1]` threw "out of bounds (length=1)" on a string whose
`.length` was 2.

```
from_bytes([0xC3, 0x41])   interp   compiled   (before)
.length                      2        2
.chars().length              1        1
for (c in s) iterations      2        1
s[1]                         65       out of bounds (length=1)
```

**Fix:** a codepoint is now uniformly "a non-continuation byte plus the
continuation bytes after it". `hml_utf8_next_start` is the iteration step;
`chars()`, `for-in`, `hml_string_char_count`, `hml_string_rune_at`, and the
"old character" size in `s[i] = r` all use it, and the iterating decoders'
`bytes_read` agrees with it (`utf8_navigation_malformed`). Index assignment
previously also used the nominal length: on `C3 41`, `s[0] = 'x'` deleted
the `A` that `s[1]` reads.

### 5. `hml_utf8_validate` was not an RFC 3629 validator

It accepted surrogates (`ED A0 80`), overlong encodings (`C0 80`), and
sequences above U+10FFFF (`F4 90 80 80`, `F5..F7` leads). **Fix:** it now
implements Unicode Table 3-7 (`utf8_validate_rfc3629`). It is still unused
outside the interpreter wrapper. The encoder still writes surrogate runes
WTF-8 style, so such strings round-trip but do not validate
(`utf8_roundtrip`).

### 6. `hml_utf8_decode_next` could advance past the NUL terminator

**Fix:** it advances by the same rule as finding 4, which stops at the
terminator (`utf8_decode_next_bounded`).

### 7. Implicit promotion is lossy on 16 of 100 type pairs (documentation)

`type_promotion_lossless_*` pins down exactly which pairs: signed with
same-or-wider unsigned, `i32`/`u32` with `f32`, and `i64`/`u64` with any
float. The types doc claimed `i64/u64 + f32 → f64` "preserves precision" and
that "f32 is sufficient for i32". Its own example value, 2⁵³+1, is not
representable in f64. **Fix (docs only):** `docs/language-guide/types.md` and
`CLAUDE.md` now state the lossy pairs. The lattice itself is unchanged; the
`fail` rows of those proofs are the spec of where it is lossy.

### 8. Interpreter runtime errors reported a stale line

`runtime_error` used `ctx->current_line`, which only calls and property reads
updated. An error from a plain statement such as `s[i] = x;` pointed at the
last call made anywhere. **Fix:** `eval_stmt` sets the line on entry. Found
while reproducing, not proved. A side effect: errors that used to carry no
location now do. `tests/parity/builtins/call_depth.hml` printed the raw
message and only matched compiled output because the line happened to be
unset; it now compares the message text, as `call_depth_try.hml` already
did.

### 9. Interpreter `u16 * u16` was signed-overflow UB

The interpreter multiplied `uint16_t` operands, which C promotes to `int`, so
`65535 * 65535` overflowed `INT_MAX`. That is harmless under the native
builds' `-fwrapv`, but undefined behavior in the WASM build, which lacks it.
**Fix:** multiply as `uint32_t` (`in_mul_u16_u16`; parity test
`tests/parity/language/unsigned_narrow_mul.hml`).

## Results

Full run on 4 cores (CBMC 6.4.1, Bitwuzla 0.8.2, TLC 1.8.0): **305 proofs,
305 matched their expected verdict**, plus 2 `skip` (checked `i64 × i64`
multiply, see "What is not covered"). Wall time 31 min, CPU time 2h02m.

| Area | Proofs | Verified | Expected `fail` | CPU time | Slowest |
|------|-------:|---------:|----------------:|---------:|--------:|
| UTF-8 | 7 | 7 | 0 | 6m18s | 271s |
| Type promotion | 11 | 5 | 6 | 0m12s | 4s |
| Other binary ops (compiled) | 13 | 13 | 0 | 1m32s | 10s |
| `*` (compiled) | 63 | 63 | 0 | 39m28s | 741s |
| `%` (compiled) | 64 | 64 | 0 | 5m02s | 6s |
| Unary ops (compiled) | 1 | 1 | 0 | 0m07s | 7s |
| Other binary ops (interpreter) | 13 | 13 | 0 | 10m08s | 91s |
| `*` (interpreter) | 63 | 63 | 0 | 51m30s | 929s |
| `%` (interpreter) | 64 | 64 | 0 | 7m43s | 9s |
| Channels (TLA+) | 6 | 6 | 0 | 0m25s | 8s |

The six expected failures are the lossy promotion rows of finding 7. They
are the spec of where promotion is lossy, not defects.

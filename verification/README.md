# Formal Verification

Machine-checked proofs about the pure core of Hemlock's runtime: the parts
both backends share or must agree on, and where a bug is a silent wrong
answer rather than a crash.

| Area | Code | Tool | What is proved |
|------|------|------|----------------|
| UTF-8 | `src/shared/utf8.c` | CBMC | round-trip for every scalar value; decoders memory-safe on arbitrary bytes |
| Type promotion | `src/shared/type_promotion.c` | CBMC | commutative, associative, monotone, matches the documented lattice |
| Integer ops (compiled) | `runtime/src/builtins_ops.c` | CBMC | `+ - * % & \| ^ << >>`, comparisons, unary `- ~ ++ --` match an executable spec for **every** type pair and **every** value; no UB |
| Integer ops (interpreter) | `src/backends/interpreter/runtime/binary_ops.c` | CBMC | same spec, so passing both = a parity proof |
| Channels | `src/shared/chan_core.c` | TLA+ / TLC | FIFO, no duplicate or lost items, send status tells the truth, no lost wakeups / deadlock |

The CBMC proofs are exhaustive over their inputs: a `VERIFICATION SUCCESSFUL`
for `rt_add` means there is no pair of integer types and no pair of values for
which `hml_binary_op(ADD, ...)` disagrees with the spec, overflows, or invokes
undefined behavior. The string harnesses are bounded (strings up to 4-6 bytes,
which covers every UTF-8 sequence shape and every pair of adjacent sequences).
The TLA+ model is exhaustive over 2-3 senders/receivers and capacities 0-2.

## Running

```bash
verification/run.sh              # everything (~1h on 4 cores, see below)
verification/run.sh utf8 chan    # subset, by name substring
KEEP_LOGS=/tmp/v verification/run.sh rt_shr   # keep logs to read counterexamples
make verify                      # same as run.sh
```

Every proof in `proofs.txt` has an expected verdict. `pass` proofs must
verify; `fail` proofs are **known defects** (listed below) and must still
produce a counterexample. The run fails on any mismatch, so a regression
breaks it, and so does fixing a defect without flipping its line to `pass`.

### Tools

| Tool | Version used | Install |
|------|--------------|---------|
| [CBMC](https://github.com/diffblue/cbmc) | 6.4.1 | release `.deb` (`ubuntu-24.04-cbmc-6.4.1-Linux.deb`); the Ubuntu package is too old |
| [Bitwuzla](https://github.com/bitwuzla/bitwuzla) | 0.8.2 | static release zip, `bitwuzla` on `PATH`; optional, ~20x faster than SAT on 64-bit multiply/divide |
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
  string/serialization paths of `eval_binary_expr` with `@unreachable`, which
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

### What is not covered

Everything not in the table: the GC-free object/array/refcount machinery,
`select()` (not in the channel model), FFI, I/O, the parser and codegen, and
float arithmetic beyond promotion. Proofs are about the C as written,
compiled by CBMC's front end. They say nothing about GCC or Emscripten
miscompiling it.

## Findings

Each `fail` line in `proofs.txt` is one of these. The repro snippets were run
on both backends at the commit these proofs were written against.

### 1. String index assignment writes one uninitialized byte (both backends)

`s[i] = r` sizes the hole with `hml_utf8_encode_len(r)`, then writes it with
`hml_utf8_encode(r)`. For `r > 0x10FFFF` these disagree (4 vs 3, since
`encode` substitutes U+FFFD), so the 4th byte of fresh `malloc` memory stays
in the string. Proof: `utf8_encode_len_total`.

- Interpreter: an integer RHS is never range-checked.
  `let s = "abc"; s[1] = 0x7FFFFFFF;` gives bytes `[97, 239, 191, 189, 0, 99]`.
- Compiled: integers are range-checked, but a rune decoded from malformed
  input can be up to `0x1FFFFF`:
  `s[1] = from_bytes([0xF7,0xBF,0xBF,0xBF])[0];` gives
  `[97, 239, 191, 189, 121, 99]`. The `121` is heap garbage.

Related parity split: reading that rune throws in the interpreter
("Invalid Unicode codepoint") but succeeds in compiled code, and the integer
assignment is accepted by the interpreter but rejected by compiled code.

### 2. `>>` gives different answers on the two backends

When the promoted type is unsigned and the left operand is a negative signed
value of a narrower type, the compiled runtime shifts the sign-extended
64-bit value and then truncates. It should shift the value converted to the
promoted type, which is what the interpreter does. Proof: `rt_shr`.

```
                      interp     compiled
i8(-1)   >> u8(1)       127        255
i8(-117) >> u32(10)     4194303    4294967295
i16(-2)  >> u16(4)      4095       65535
```

### 3. Negative shift amounts: interpreter returns 0, compiled throws

The interpreter converts the shift amount to the promoted type before the
`r < 0` check, so with an unsigned left operand a negative amount becomes huge
and the result is 0. Proof: `in_shl`, `in_shr`.

```
let a: u32 = 1;  a >> -1   // interp: 0     compiled: "Shift amount must be non-negative"
                 a << -1   // interp: 0     compiled: same error
```

The compiled runtime has the mirror-image corner case: a `u64` shift amount
≥ 2⁶³ is read as negative and throws, where any amount ≥ the width should
give 0 (`rt_shl`, `rt_shr`).

### 4. Index-based and iterator-based string walking disagree on malformed UTF-8

`hml_utf8_count_codepoints` / `hml_utf8_byte_offset` count non-continuation
bytes. `hml_utf8_decode` iteration advances by the lead byte's nominal length.
The comments in `utf8.c` claim these match "even for malformed input". They
don't, and the backends pick different ones in different places, so the same
string disagrees with itself and across backends:

```
from_bytes([0xC3, 0x41])   interp   compiled
.length                      2        2
.chars().length              1        1
for (c in s) iterations      2        1
s[1]                         65       "String index 1 out of bounds (length=1)"
```

Proof: `utf8_navigation_malformed`. The property holds for valid UTF-8
(`utf8_navigation_valid`), so this only bites strings built from raw bytes.

### 5. `hml_utf8_validate` is not an RFC 3629 validator

It accepts surrogates (`ED A0 80`), overlong encodings (`C0 80`), and
sequences above U+10FFFF (`F4 90 80 80`, `F5..F7` leads). Today it has no
callers outside the interpreter wrapper, but its name invites them. Proof:
`utf8_validate_rfc3629`. The decoders likewise return overlong and
out-of-range values instead of U+FFFD, which is how finding 1's `0x1FFFFF`
rune arises.

### 6. `hml_utf8_decode_next` can advance past the NUL terminator

On a truncated sequence at the end of a string (e.g. `"\xE0"`), it advances
by the nominal length (3) past the terminator. It has no callers today.
Proof: `utf8_decode_next_bounded`.

### 7. Implicit promotion is lossy on 16 of 100 type pairs (documentation)

`type_promotion_lossless` enumerates them: signed with same-or-wider unsigned
(`-1 → UINT_MAX`), `i32`/`u32` with `f32` (24-bit mantissa), and `i64`/`u64`
with both `f32` and `f64`. The last group contradicts the types doc, which
says `i64/u64 + f32 → f64` "to preserve precision": f64 has 53 bits, so
`2^53 + 1` does not survive. This is a documentation and philosophy issue
("Lose precision in implicit conversions" is under What NOT to Do), not a
crash. The proof pins down exactly where the lattice is lossy.

### Incidental (found while reproducing, not proved)

- The interpreter can report a runtime error at the wrong line: in a file
  whose line 6 is `try { print(c << -3); } catch ...`, the caught message
  and source snippet point at line 4, the previous error site.

## Results

Run on 4 cores, CBMC 6.4.1 + Bitwuzla 0.8.2, TLC 1.8.0:

Run `verification/run.sh` for the current table. Expected verdicts live in
`proofs.txt`.

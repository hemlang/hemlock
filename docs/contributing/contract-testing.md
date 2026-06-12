# Contract Testing Guide

This guide describes Hemlock's **contract test suite** (`tests/contracts/`) and
the practices that keep documented behavior from changing silently.

For general testing (how to write feature tests, run the suites, etc.), see
[testing.md](testing.md).

---

## What Is a Contract?

A contract is a **documented, user-visible guarantee** of the language or
stdlib. Users write code against contracts, so breaking one breaks user
programs even when every implementation test still passes.

Examples of contracts:

- `42` infers `i32`; `5000000000` infers `i64`; `3.14` infers `f64`.
- `/` always returns a float — even `6 / 3`. Integer division is `divi()`.
- The type promotion lattice: `i8 → u8 → i16 → u16 → i32 → u32 → i64 → u64 → f32 → f64`,
  with the precision exception `i64/u64 + f32 → f64`.
- `i32(3.99)` truncates toward zero; annotations coerce numbers but never
  parse strings.
- String indexes are codepoints; `find` composes with `substr`/`slice`/`char_at`.
- String methods return new strings; index assignment mutates in place.
- `defer` runs LIFO at function exit; `switch` falls through without `break`.
- `TYPEID_*` constants keep their integer values forever.
- `panic()` is never catchable; `throw` always is.
- The stable substring of a runtime error message
  (e.g. `Division by zero`) that user code matches with `e.contains(...)`.

Contracts are distinct from implementation details (exact float formatting of
unusual values, error *location* prefixes, internal field ordering), which may
change.

## How Contract Tests Differ from Feature Tests

| | Feature test | Contract test |
|---|---|---|
| Lives in | `tests/<feature>/` | `tests/contracts/` |
| Verifies | the implementation works | the *documented promise* holds |
| Backends | interpreter (`make test`) | **both**, always |
| When it fails | fix the code | stop: either the change is a bug, or docs + test must change **together** |
| Source of truth | the code | `docs/` and `CLAUDE.md` |

The key practice: **a contract test's `.expected` file is written from the
documentation, then verified against both backends** — never generated blindly
from whatever the implementation happens to print. If the implementation and
the docs disagree, that is a finding to resolve, not an output to pin.

## Suite Layout

```
tests/contracts/
├── run_contract_tests.sh     # runner: every test goes through BOTH backends
├── types/                    # inference, promotion, conversion, typeid contracts
├── semantics/                # evaluation-order, mutation, null, control-flow contracts
└── errors/                   # rejection contracts and error-message contracts
```

Run with:

```bash
timeout 120 make test-contracts        # or: bash tests/contracts/run_contract_tests.sh
bash tests/contracts/run_contract_tests.sh promotion   # filter by substring
```

`make test-all` includes `test-contracts`.

## Test Kinds

### Output contracts (`<name>.hml` + `<name>.expected`)

Both the interpreter and a compiled binary must produce the `.expected`
output **byte-for-byte**. A mismatch on either backend fails the suite —
a contract honored by only one backend is a parity bug.

### Rejection contracts (filename contains `error`, `invalid`, `overflow`, or `negative`)

The program must be **rejected by both backends**. The interpreter must exit
non-zero; the compiler must either refuse to compile or produce a binary that
exits non-zero. Backends may reject at different stages — e.g.
`let n: i32 = "42";` fails at runtime in the interpreter but at compile time
in `hemlockc` — the contract is *rejection*, not the stage.

An optional `<name>.expected_error` file pins the stable part of the
interpreter's message: every non-empty line must appear as a substring of the
interpreter's output.

## Writing a New Contract Test

1. **Find the promise in the docs** (`docs/`, `CLAUDE.md`, `stdlib/docs/`).
   If it isn't written down, it isn't a contract yet — document it first, in
   the same PR.
2. Write the `.hml` test with a header comment naming the contract and the
   doc that states it:
   ```hemlock
   // CONTRACT: `/` always returns a float (docs/language-guide/operators.md)
   ```
3. Derive the expected output **from the documentation**, then run the test
   under both backends to confirm reality matches:
   ```bash
   ./hemlock tests/contracts/semantics/my_contract.hml
   ./hemlockc tests/contracts/semantics/my_contract.hml -o /tmp/t && /tmp/t
   ```
   If the backends disagree with the docs or each other, file/fix that first.
4. Save the verified output as `<name>.expected` and run the suite:
   ```bash
   bash tests/contracts/run_contract_tests.sh my_contract
   ```

## Practices

**Pin substrings of error messages, not full formatted output.** Location
prefixes (`[file:line]`) and caret context depend on the invocation path and
may legitimately improve. The message text users match with `e.contains(...)`
is the contract. In output tests, print `e.contains("...")` rather than `e`.

**Keep tests deterministic.** No timing, no concurrency races, no
filesystem/network dependence, no map-iteration-order assumptions (sort keys
before printing). A flaky contract test teaches people to ignore contract
failures.

**One contract domain per file.** Small files with focused names
(`type_promotion_lattice`, `switch_fallthrough`) make a failure
self-describing in CI output.

**Never regenerate `.expected` to make a failure go away.** That converts the
suite into a snapshot test of the implementation. The only valid responses to
a red contract test are (a) fix the regression, or (b) change the
documentation and the test in the same commit, with the behavior change
called out in `CHANGELOG.md`.

**Name rejection tests with the standard keywords** (`error`, `invalid`,
`overflow`, `negative`) and avoid those keywords in output-test filenames —
the runners key off them.

**When adding a language feature**, add its contract tests alongside the
parity test: the parity test shows the feature works identically in both
backends; the contract tests pin the specific guarantees the docs make about
it (inference, promotion, error cases, mutation behavior).

## Current Coverage

| Area | Tests |
|---|---|
| `types/` | literal inference, promotion lattice, conversion rules, `typeid`/`TYPEID_*` stability |
| `semantics/` | division-returns-float, codepoint string indexing, string method immutability, `defer` LIFO ordering, `switch` fall-through, `??`/`?.`/`??=`, object bracket-key coercion, array method mutation behavior |
| `errors/` | catchable error-message substrings, typed-assignment overflow rejection, annotation-never-parses-strings rejection, `panic()` uncatchability |

Gaps worth closing next: numeric formatting contracts (`print` of floats),
overflow/wrapping semantics of unsigned arithmetic, `for (item in ...)`
iteration-order guarantees, buffer bounds-checking contracts, stdlib API
contracts (`@stdlib/json` round-tripping, `@stdlib/math` domain errors).

# Parity Test Suite

Every test in this directory runs through **both** the interpreter (`hemlock`)
and the compiler (`hemlockc`), and both must produce output identical to the
test's `.expected` file. This enforces the project's parity-first rule: the two
backends are only correct when they agree.

## Running

```bash
make parity                          # build everything and run the suite
bash tests/parity/run_parity_tests.sh             # run directly
bash tests/parity/run_parity_tests.sh rune        # only tests whose path matches "rune"
TEST_TIMEOUT=30 bash tests/parity/run_parity_tests.sh   # slower machines
```

Exit codes: `0` full parity, `1` at least one test failed on both backends,
`2` parity incomplete (a test passed on one backend only).

## Layout

| Directory   | Contents                                            |
|-------------|-----------------------------------------------------|
| `language/` | Core language semantics (operators, control flow, closures, match, ...) |
| `builtins/` | Global builtins (print, alloc, channels, FFI, HTTP, ...) |
| `methods/`  | String/array/object method behavior                 |
| `modules/`  | Module system and `@stdlib` modules                 |

## Adding a test

1. Create `category/feature.hml`.
2. Generate the expected output with the interpreter and **review it** — the
   interpreter is the reference implementation, but confirm the output is the
   *intended* semantics, not just current behavior:

   ```bash
   ./hemlock tests/parity/category/feature.hml > tests/parity/category/feature.expected 2>&1
   ```

3. Run `bash tests/parity/run_parity_tests.sh feature` and make sure it shows
   full parity (`✓`).

Notes:

- stdout and stderr are both captured and compared.
- A `.hml` file without an `.expected` file is skipped. Files imported by
  other tests in the same directory are reported as module fixtures.
- Tests must be deterministic: no timestamps, PIDs, pointer addresses, or
  network/host-dependent output. Hash or compare such values instead of
  printing them.

## Environment requirements

Tests that need optional environment support declare it with a directive
comment at the top of the file:

```hemlock
// REQUIRES: http
```

The runner probes each capability once at startup and skips tests whose
requirements are unmet (instead of reporting false parity failures).

| Capability | Meaning                                              |
|------------|------------------------------------------------------|
| `http`     | libwebsockets compiled into the interpreter/runtime  |

Add new capabilities in `check_requirements()` in `run_parity_tests.sh`.

## Known divergences (not yet fixed)

These are real interpreter/compiler differences discovered by probing; they
require architectural work and are documented here so tests don't silently
encode one backend's behavior as truth:

1. **Closure capture of locals inside functions.** The interpreter captures
   variables by reference (the documented semantics); compiled closures inside
   functions snapshot captured values at closure-creation time, so mutations of
   an outer local after closure creation (and mutations from inside the closure,
   as seen by the outer function) do not propagate. Top-level closures are not
   affected (main-file variables are shared C globals). Fixing this requires
   per-scope shared environments in the compiler's closure model. The same
   limitation makes `defer f(x)` capture argument values at registration time
   in compiled code, while the interpreter evaluates the deferred expression at
   function exit.

2. **Runtime error message location prefixes.** Interpreter exceptions carry
   `[file:line]` prefixes (e.g. `[test.hml:7] Division by zero`); compiled
   binaries throw the bare message. Parity tests that print caught exception
   text must either avoid the prefix (e.g. test with `contains()`) or not
   print the message verbatim.

3. **Statically detectable errors.** The compiler rejects some programs at
   compile time (undefined functions/variables, typed-array element type
   violations) that the interpreter only reports when the offending line
   executes. Both reject the program, but output cannot match.

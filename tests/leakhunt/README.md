# leakhunt — per-construct codegen leak audit

A repeatable loop for finding and fixing the refcount/ownership leaks
in Hemlock's compiled output. This is the system behind the "ongoing
codegen audit" `tests/stress/run_stress.sh` defers (it only runs the
green `*_leak.hml` regression set; the broad LSan backlog lives here).

Goal: drain the codegen-leak backlog construct by construct, toward a
clean **Hemlock 2.5.0**. The motivating symptom was the Witchgrid
control-plane bleeding hundreds of MB over days.

## Why per-construct

Generated C has **no `#line` directives**, so an LSan stack from a
real program (e.g. the CP) can't be mapped back to a `.hml`
construct. The fix: each `tests/leakhunt/*_leak.hml` exercises exactly
**one** construct in a bounded loop, so an LSan hit isolates to one
codegen path. Whole-program leak reports are near-useless; single-
construct ones are tractable.

## Harness

`./run.sh` (from anywhere):
- builds the ASan/LSan-instrumented runtime **once** into `.rt/`
  (cached — reused across runs; pass `--rebuild-rt` after editing
  `runtime/`). A *codegen* fix rebuilds `hemlockc` (`make compiler`),
  NOT the runtime, so verify is seconds.
- `./run.sh` runs every micro-repro; `./run.sh <name>` runs one
  (the fix-loop verify step; `<name>` = filename without `.hml`).
- per construct: `hemlockc --emit-c` → `gcc -fsanitize=address` (exact
  recipe copied from `tests/stress/run_stress.sh`) → run with
  `detect_leaks=1`, full report captured to `out/<name>.lsan`.
- classifies PASS / LEAK / CRASH, prints a `worklist:` line, exits
  non-zero if anything leaked or crashed.

## The loop (one construct per cycle)

1. `./run.sh` → read the `worklist:`. Pick one construct.
2. Triage `out/<name>.lsan`: the alloc frames (`hml_val_array`,
   `hml_val_string_owned`, `hml_string_split`, …) + the
   generated-C frame name tell you the runtime allocation and which
   construct. Because the repro is single-construct, that *is* the
   localization.
3. Regenerate the C to read what codegen actually emits:
   `./hemlockc --emit-c /tmp/x.c tests/leakhunt/<name>.hml` then grep
   the relevant pattern (env build, retain/release pairing, scope-exit
   cleanup, throw path).
4. Root-cause = find the missing `hml_release`. The three classes seen
   so far:
   - **Orphaned creation ref** (runtime builtin): a builtin makes an
     owned value (`hml_val_*_owned`, refcount 1) and `hml_array_push`/
     `hml_object_set_field` it (those *retain*, →2) but never releases
     the creation ref → element leaks when the container frees. Fix in
     `runtime/src/builtins_*.c`: `hml_release(&v)` after the push/set.
     (Precedents: `hml_object_keys`; **`hml_string_split` fixed here**.)
   - **Missing scope-exit release** (codegen): a heap local (esp. a
     closure value `hml_val_function_with_env*` + its
     `HmlClosureEnv`) is never released when its block ends → it +
     everything it retains leaks per iteration. Fix in
     `src/backends/compiler/codegen_*.c` (closures: `codegen_closure.c`
     / `codegen_program.c` local-cleanup emission).
   - **Exception-unwind leak** (codegen): locals allocated before a
     `throw` aren't released on the unwind path. Fix in
     `codegen_stmt.c` `STMT_TRY` / throw cleanup emission.
5. Apply the minimal fix (add the missing release; do not restructure).
6. Rebuild: runtime fix → `./run.sh --rebuild-rt <name>`; codegen fix
   → `make compiler && ./run.sh <name>`. Must report `PASS`.
7. Re-run the **full** `./run.sh` — confirm no regression elsewhere.
8. Regression-lock: copy the repro to `tests/stress/<name>.hml` with a
   header documenting the bug + fix (match the existing
   `tests/stress/*_leak.hml` style: "Regression: … [fixed …]"). It
   then guards permanently via `make stress-lsan`.
9. Commit (one construct per commit; message: what leaked + the fix).
   Next construct.

## Codegen entry points (high-risk)

- closures/capture env: `src/backends/compiler/codegen_closure.c`,
  scope-cleanup in `codegen_program.c`
- containers literals/methods: `codegen_expr.c`,
  `codegen_call_methods.c`
- `try/catch`/throw unwind: `codegen_stmt.c` (`STMT_TRY`)
- async spawn/join: `codegen_call_async.c`
- runtime builtins: `runtime/src/builtins_{string,array,object}.c`
- refcount primitives: `runtime/src/value.c`
  (`hml_retain` ~605, `hml_release` ~770; immortal-string sentinel)

## Expanding the corpus

Add more single-construct `*_leak.hml` as new constructs are
suspected (string interpolation, method chains, default args, struct
field reassignment, generators, `match`, optional chaining, channel
send/recv, …). Keep each: one construct, bounded loop (so LSan
reports at exit), prints `name: ok …`, nothing legitimately live at
exit except small fixed singletons (those are suppressed in
`tests/stress/lsan.supp` / not flagged).

## Status (update as you go)

- ✅ `string_ops` — `hml_string_split` orphaned creation refs. Fixed,
  regression-locked (`tests/stress/string_split_leak.hml`).
- ⏳ `closure_capture` — closure value + its `HmlClosureEnv` (holding
  the captured array) never released at block exit. Root-caused;
  codegen fix pending.
- ⏳ `try_unwind` — `big`/`o` + contents not released on the `throw`
  path (11 leak blocks). Pending.
- ✅ clean baseline: `array_pop`, `array_remove`, `map_overwrite`,
  `map_delete`, `nested_literal`, `spawn_join`.

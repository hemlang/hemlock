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

- ✅ `string_ops` — `hml_string_split` orphaned creation refs (3 push
  sites). Fixed in `runtime/src/builtins_string.c`, regression-locked
  (`tests/stress/string_split_leak.hml`), committed.
- ✅ `try_unwind` — **FIXED & shipped**. `STMT_THROW` codegen now
  emits `codegen_emit_local_cleanup` before `hml_throw` (mirrors
  `STMT_RETURN`). Verified PASS (was 11 blocks), no regression,
  regression-locked `tests/stress/throw_unwind_leak.hml`. Scope: the
  *throwing* function's own locals only.
- ⏳ `throw_indirect` — **NEW, found while verifying try_unwind**:
  locals in *intermediate* frames between the throw and the catch
  leak (9 blocks) — `hml_throw` longjmps straight to the setjmp,
  skipping every frame in between, so their normal-path cleanup never
  runs. Harder class: a throw-site emit can't fix it (the throw site
  doesn't know the callers' locals). Needs either per-call-site
  setjmp + frame-local cleanup + re-throw, or a runtime scope-cleanup
  registration stack that `hml_throw` unwinds. Real design effort —
  land deliberately, not rushed. Repro: `throw_indirect_leak.hml`.
- ✅ `closure_capture` — **FIXED & shipped**. Closure-prologue
  `hml_closure_env_get` retains each capture; its release was emitted
  only by an explicit loop at the implicit fall-through return, so
  explicit `return`/`throw` left it as dead code after `return`.
  Instrumentation proved the env lifecycle was balanced
  (env_new==env_free==2000) — the capture array was over-retained.
  Fixed by releasing captures inside `codegen_emit_local_cleanup`
  (the one helper all exit paths call), guarded by
  `ctx->current_closure`; removed the redundant explicit loop.
  Verified PASS, full sweep no regression, regression-locked
  `tests/stress/closure_capture_leak.hml`. (commit 881b5c1b)
- ✅ clean baseline: `array_pop`, `array_remove`, `map_overwrite`,
  `map_delete`, `nested_literal`, `spawn_join`.

Current sweep: **9 pass, 1 leak** (`throw_indirect` only).

The one remaining item — a real design effort, land it as its own
deliberate, reviewed change, NOT rushed:

`throw_indirect` — intermediate-frame unwind leak. `hml_throw`
longjmps straight to the nearest `try`'s setjmp, skipping every C
frame in between, so heap locals in caller frames between the throw
and the catch are never released. A throw-site or return-path emit
*cannot* fix this (the throw site doesn't know callers' locals).
Two models:
  - **Per-function setjmp shim**: every function with cleanup that
    can propagate a throw pushes its own HmlExceptionContext +
    setjmp; on a propagating throw it runs codegen_emit_local_cleanup
    then `hml_exception_pop()` + re-`hml_throw`. Frame-by-frame
    unwind. Correct, localized to codegen, no runtime ABI change,
    but a setjmp per such call. Mirrors the throwing-frame fix
    pattern (codegen_emit_local_cleanup is already the one cleanup
    home — closure captures + body locals + shared env all route
    through it now, so the shim just calls it).
  - **Runtime scope-cleanup registration stack**: scopes register
    cleanup records; `hml_throw` walks+runs them up to the target.
    Cleaner conceptually but adds normal-path register/deregister
    cost and a runtime ABI change.
  Recommendation: the setjmp-shim — it composes with the now-unified
  `codegen_emit_local_cleanup` and needs no runtime change. Scope it
  to functions that (have heap locals OR captures OR a shared env)
  AND contain a call that can throw. Verify with
  `tests/leakhunt/throw_indirect_leak.hml` + a full sweep + a full
  `make stress-lsan`.

Shipped: `string_ops`, `try_unwind` (throwing-frame),
`closure_capture`. Cut 2.5.0 once `throw_indirect` lands and a full
`make stress-lsan` is green. Keep one-construct-per-commit.

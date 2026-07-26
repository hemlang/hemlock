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
- ✅ `throw_indirect` — **FIXED & shipped** via the unwind-cleanup
  registry (the CORRECTED DESIGN below, implemented as designed):
  generated code registers the C slot of every owned expression
  temporary / local / param / capture in a thread-local registry
  (`hml_uw_track`, inline in `hemlock_runtime.h`); every expression
  node pops its children and registers its own result, statements
  and scopes reset to entry watermarks, and `hml_throw` releases
  every slot registered after the target handler was installed
  BEFORE its `longjmp` (all intermediate frames are still alive at
  that point, so the slots are valid). This also re-fixes
  `try_unwind` (the reverted 2.5.0 attempt) and mid-expression
  temporary leaks (`expr_temp_unwind_leak.hml`). Verified: full
  leakhunt sweep PASS, `make stress-lsan`/`stress-asan` green,
  54/54 compiler tests, 309/309 parity (incl. the new
  `exception_unwind_semantics` semantic guards). Regression-locked:
  `tests/stress/{throw_unwind,throw_indirect,expr_temp_unwind}_leak.hml`.
  Repro: `throw_indirect_leak.hml`.
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

Current sweep: **all pass** (`throw_indirect` fixed by the
unwind-cleanup registry — see the CORRECTED DESIGN below, which is
what shipped).

(HISTORICAL design record for the fix that landed:)

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
  ⚠️ INFEASIBILITY FINDING (verified by attempting it): the
  per-function setjmp-shim **does not work** with Hemlock's codegen.
  Locals are declared at-use, frequently inside nested C blocks
  (`if (c) { HmlValue _tmpN = ...; }`), and are never hoisted to
  function top. A `setjmp` landing pad in the prologue cannot
  reference them — the cleanup it would emit is textually *before*
  the locals' declarations and in outer scope → does not compile.
  Hoisting every local to function top to make it work is itself a
  massive codegen change. So the setjmp-shim spec below is RETAINED
  ONLY as a record of the dead end; do NOT pursue it.

  CORRECTED DESIGN — runtime cleanup-registration stack. Key
  enabling insight: `hml_throw` is a normal function call that runs
  BEFORE its `longjmp`, so at that moment every intermediate C frame
  is still alive and their locals are valid. So: maintain a
  thread-local stack of cleanup records (each = address of a live
  HmlValue slot, or a small per-scope batch). Codegen registers a
  record when a cleanup-needing local goes live and deregisters on
  normal scope exit (no run — normal cleanup already releases it).
  `hml_throw`, before `longjmp`, walks the registration stack from
  the top down to the target exception context and `hml_release`s
  each registered slot. No landing pad, no locals-in-scope problem,
  composes with the existing exception-context stack. Costs a
  register/deregister per cleanup-needing local on the normal path
  (batch per scope to amortize). This is a runtime-ABI + every-scope
  codegen change — its own deliberate effort with the full
  validation gate set below. `hml_rethrow` is NOT needed in this
  design (no re-throw; cleanup happens in-place before the longjmp).

  (DEAD-END REFERENCE — setjmp-shim, do not implement:)

  1. New runtime primitive (runtime/src/builtins_func.c +
     hemlock_runtime.h), a move-semantics sibling of hml_throw —
     hml_throw RETAINS its arg (right for `throw expr`, wrong for a
     re-throw where we own the in-flight ref; retaining there leaks
     the exception value once per propagated frame):

       __attribute__((noreturn))
       void hml_rethrow(HmlValue v) {
           if (!g_exception_stack || !g_exception_stack->is_active) {
               fprintf(stderr, "Uncaught exception: ");
               print_value_to(stderr, v); fprintf(stderr, "\n");
               exit(1);
           }
           g_exception_stack->exception_value = v;  // MOVE, no retain
           longjmp(g_exception_stack->exception_buf, 1);
       }

  2. Shim prologue, emitted at each of the 3 function-gen sites in
     codegen_program.c (~139, ~300, ~658) right AFTER HML_CALL_ENTER
     and BEFORE funcgen_generate_body, ONLY when the function needs
     it (gate: has body locals OR captures OR shared_env, AND its
     body contains a call/throw that can propagate). Push FIRST so
     it is the outermost ctx (inner `try`s nest inside it):

       HmlExceptionContext *_ushim = hml_exception_push();
       if (setjmp(_ushim->exception_buf) != 0) {
           HmlValue _ev = _ushim->exception_value;   // own in-flight +1
           _ushim->exception_value = hml_val_null();  // detach: pop must not release it
           hml_exception_pop();                       // free our ctx
           codegen_emit_local_cleanup(ctx, NULL);     // release THIS frame's locals/captures/env
           hml_rethrow(_ev);                          // move ref to next ctx; no leak, no UAF
       }

  3. Normal-exit integration (the dangerous part — get it exactly
     right): the shim ctx must be popped on EVERY normal exit before
     the frame dies, exactly once, composing with
     codegen_emit_return_try_pops. Cleanest: emit `hml_exception_pop()`
     for the shim as the first thing in a single helper
     `codegen_emit_unwind_shim_pop(ctx)` and call it at every site
     that currently calls codegen_emit_return_try_pops AND at the
     implicit fall-through return — gated on "this function has a
     shim". Do NOT fold it into codegen_emit_local_cleanup (that
     helper runs in non-shim contexts and the throw path already
     popped). Audit every STMT_RETURN branch in codegen_stmt.c
     (plain/defer/finally-goto/tail-call) + codegen_program.c
     implicit returns.

  4. Validation gates (ALL must pass — this is a control-flow
     change, not just a leak fix): `tests/leakhunt` full sweep
     (throw_indirect now PASS, nothing regressed) + `make stress-lsan`
     + `make stress-asan` + `make test` + the parity suite + the
     exception regression tests. A wrong ctx-pop corrupts the
     exception stack invisibly to LSan — the non-leak suites are the
     real guard. Land as ONE reviewed commit; revert cleanly if any
     non-leak suite regresses.

Shipped: `string_ops`, `try_unwind` (throwing-frame),
`closure_capture`, `throw_indirect` + `expr_temp_unwind` (the
unwind-cleanup registry). Keep one-construct-per-commit.

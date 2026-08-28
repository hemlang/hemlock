# Gradual-guarantee and blame witnesses

Runnable counterexamples for `../gradual-typing-analysis.md`. Each `*_typed.hml` /
`*_untyped.hml` pair differs **only** in type annotations; under the gradual
guarantee the pair must behave identically (except that the untyped version may
succeed where the typed one raises a cast error — never the reverse).

These programs are intentionally **not** part of the test suites:

- several exit non-zero or crash *by design* (that is the finding);
- `blame3_ref.hml` is accepted and runs under the interpreter but is rejected by
  `hemlock check` / `hemlockc`, so it cannot be a parity test;
- if any of the semantics documented in the analysis are changed (see §6 of the
  analysis), the expected outputs below change with them — update both.

Run with:

```bash
./hemlock <file>            # dynamic semantics
./hemlock check <file>      # static checker verdict
```

| Witness | Demonstrates | Typed behavior | Untyped behavior |
|---|---|---|---|
| `gg1_*` | ascription converts values (lossy) | `3 / true / "42"` | `3.99 / 3 / 42` |
| `gg2_*` | array annotation rewrites aliased elements in place | `a[0]` prints `1` | `1.5` |
| `gg3_*` | object check coerces fields, materializes optional fields, stamps `match` brand | runs, prints `30 / true / is Person / 3` | **crashes** on `alice.active` |
| `gg4_*` | numeric annotations select overflow semantics (inherent, see §4.4) | `2147483648` | overflow error |
| `sgg1_*` | static guarantee: erasing `: array` introduces a type error | `check`: 0 errors | `check`: rejected |
| `blame1.hml` | untyped fn through typed HOF: error surfaces in, and names, typed code | uncaught exception blaming `applyf` | — |
| `blame2.hml` | same, surfacing at a distant `let x: i32 = ...` binding | uncaught exception at typed binding | — |
| `blame3_ref.hml` | `ref` param annotations unenforced (interpreter) vs rejected (checker) | prints `hi1 / string` | — |
| `blame4_exit.hml` | fn-signature cast failure is uncatchable `exit(1)`, no location | `catch` never runs | — |
| `blame5_brand.hml` | brand-time coercion vs exact-tag `push` check | error on `a.push(3)` | — |
| `b6_fnalias.hml` | `fn`-typed binding installs no runtime monitoring | prints `hello / string` | — |
| `b8_argblame.hml` | the correct case: first-order arg check, catchable at boundary | prints `caught: ...` | — |

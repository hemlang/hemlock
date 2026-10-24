# UndefinedBehaviorSanitizer sweeps

Hemlock is unsafe by design. UB a program asks for itself (raw `ptr`
arithmetic, reading freed `alloc` memory, double free) is the user's
responsibility. UB the **runtime** commits while executing a valid program is
a bug: out-of-range float-to-int casts, misaligned typed loads on a `buffer`,
signed overflow, oversized shifts and so on. These sweeps look for the second
kind.

| Script | What it instruments | What it runs |
|--------|---------------------|--------------|
| `run_interp_ubsan.sh` | `hemlock` and `hemlockc` (in-tree rebuild) | interpreter suite, parity suite (which drives `hemlockc`), `probes/` |
| `run_compiled_ubsan.sh` | an isolated copy of the runtime | every program under `tests/parity`, `tests/stress`, `probes/`, compiled with `--emit-c` |

Both build without `-fwrapv`. The native builds use it, but the WASM builds do
not, so signed overflow there is real UB. `float-divide-by-zero` is not
checked: float division by zero is defined under IEC 60559 (GCC defines
`__STDC_IEC_559__`), and Hemlock relies on it for `Inf`/`NaN`.

`probes/` are small programs feeding edge inputs (huge or NaN floats where
integers are expected, misaligned buffer slices, extreme shift and slice
amounts, ...) to builtins on both backends. The suites stay on ordinary
inputs and almost never reach these paths. Only UBSan reports count; a probe's
own output and exit status are ignored. Behavior worth pinning lives in
`tests/parity/builtins/ub_edge_inputs.hml`.

Both scripts exit non-zero on any report. CI runs them in the `ubsan` job of
`.github/workflows/sanitizers.yml`.

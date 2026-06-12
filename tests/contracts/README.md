# Contract Tests

Tests in this directory pin **documented, user-visible guarantees** of the
language and stdlib — type promotion rules, literal inference, conversion
semantics, error messages, evaluation order — so they can never change
silently. Every test runs through **both** the interpreter and the compiler.

```bash
make test-contracts
# or, with an optional substring filter:
bash tests/contracts/run_contract_tests.sh promotion
```

Layout:

- `types/` — literal inference, the promotion lattice, conversion rules, `typeid`/`TYPEID_*` stability
- `semantics/` — division-returns-float, codepoint string indexing, string/array mutation behavior, `defer` ordering, `switch` fall-through, null handling, object key coercion
- `errors/` — error-message substrings and programs both backends must reject

Test kinds:

- `<name>.hml` + `<name>.expected` — both backends must match the expected output exactly.
- Filenames containing `error`/`invalid`/`overflow`/`negative` — both backends must reject the program (compile- or run-time). An optional `<name>.expected_error` file pins interpreter error-message substrings.

**A failing contract test is not a test bug.** Either the change is a
regression, or the documentation and the test must be updated together in the
same commit. Never regenerate a `.expected` file just to make CI green.

Full practices guide: [docs/contributing/contract-testing.md](../../docs/contributing/contract-testing.md)

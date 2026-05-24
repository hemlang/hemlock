# Hemlock 3 Proposal: Engine Abstractions and Stronger Language Primitives

## Context
This proposal summarizes five language ideas from engine-management code patterns (install discovery, route registration, backend switches, callback contracts, and engine module boilerplate), evaluates their value for Hemlock 3, and proposes an implementation roadmap.

## Summary of the Five Ideas

1. **Typed records / structs**
   - Introduce named record types (for example `InstallEntry`) and typed containers (for example `array<InstallEntry>`).
   - Purpose: stop shape drift between modules that currently reconstruct object literals by convention.

2. **Traits / protocols (module conformance)**
   - Introduce protocol definitions that modules can implement (for example `Engine`).
   - Purpose: let coordinator code iterate a collection of engines and derive routing/health/rescan behavior from a shared interface.

3. **Pattern matching with exhaustiveness**
   - Extend `match` so enum/sum-type matches must cover all variants (or explicit default).
   - Purpose: make backend branching safer and less stringly-typed.

4. **First-class function types in signatures**
   - Allow explicit callback types in function parameters and values.
   - Purpose: verify callback arity and return types at compile time.

5. **Compile-time macros / module synthesis**
   - Add compile-time code generation to produce engine modules from declarative specs.
   - Purpose: large boilerplate reduction and consistency, especially for repetitive engine wrappers.

## Value Assessment for Hemlock 3

### Highest value (ship in Hemlock 3 core)

1. **Typed records / structs** — **Critical foundation**
   - Highest leverage per implementation effort.
   - Improves readability, tooling, and diagnostics immediately.
   - Enables safer protocols, pattern matching payloads, and generated code later.

2. **First-class function types** — **High ROI and low conceptual risk**
   - Tightens API contracts without requiring full protocol system first.
   - Immediately useful across stdlib and runtime-facing APIs.

3. **Pattern matching with exhaustiveness + sum types** — **High safety for control-flow heavy code**
   - Eliminates silent omissions when variants evolve.
   - Directly upgrades backend dispatch and other tagged-union decisions.

### Medium value (likely Hemlock 3.x, or staged in 3.0 with limits)

4. **Traits / protocols** — **Very valuable, moderate design surface**
   - Major boilerplate reduction in orchestrator modules.
   - Needs careful interaction design with modules, records, and function types.
   - Recommend shipping a narrow v1 (read-only property + function signature conformance) before advanced features.

### Strategic / longer horizon

5. **Compile-time macros / synthesis** — **Huge upside, highest complexity/risk**
   - Strong long-term payoff.
   - Highest risk for build determinism, debuggability, and language complexity if introduced too early.
   - Best as a constrained post-3.0 feature after type foundations and protocol model stabilize.

## Proposed Hemlock 3 Language Changes

## 1) Typed records / structs (MVP)

### Syntax sketch
```hml
record InstallEntry {
  tag: string
  path: string
  binary: string?
  is_current: bool
  has_binary: bool
}

fn list_installs_at(root: string): array<InstallEntry>
```

### Semantics
- Nominal record type identity.
- Required fields by default; optional field marker via nullable type (`T?`) not missing-key semantics.
- Structural literal checks at assignment/return sites.
- Field name typo diagnostics with nearest-name suggestions.

### Why now
- Unblocks protocol signatures and generated scaffolding with strong compile-time guarantees.

## 2) First-class function types (MVP)

### Syntax sketch
```hml
fn flip_current(
  root: string,
  tag: string,
  find: fn(string) -> string?,
  binary_name: string
): bool
```

### Semantics
- Function type includes parameter types, return type, and optionally purity/effect qualifiers in later phases.
- Assignability requires arity/type compatibility.

### Why now
- Immediate correctness wins with limited compiler-surface expansion.

## 3) Exhaustive pattern matching over sum types (MVP+)

### Syntax sketch
```hml
enum Backend {
  Cpu
  Vulkan
  Metal
  Cuda(arch: string)
}

fn cmake_flags_for(backend: Backend): array<string> {
  match backend {
    Backend::Cuda(arch) => ["-DGGML_CUDA=ON", "-DCMAKE_CUDA_ARCHITECTURES=" + arch],
    Backend::Vulkan => ["-DGGML_VULKAN=ON"],
    Backend::Metal => ["-DGGML_METAL=ON"],
    Backend::Cpu => []
  }
}
```

### Semantics
- Exhaustiveness required when matching enum/sum types.
- Compiler error on missing variants.
- Optional `_` wildcard allowed but discouraged by lint where strictness desired.

### Why now
- Prevents version drift bugs as variants evolve.

## 4) Protocols / traits for modules (constrained v1)

### Syntax sketch
```hml
protocol Engine {
  const name: string
  fn schema(): object
  fn current_path(root: string): string?
  fn handlers(): array<RouteHandler>
  fn detect_flavor(path: string): string?
}

module llama implements Engine { ... }
module whisper implements Engine { ... }

let engines: array<Engine> = [llama, whisper, sd, piper]
```

### Semantics (v1 boundaries)
- Conformance checks constants + function signatures.
- No inheritance, no default impls, no associated types in v1.
- Protocol values usable for iteration/dispatch in coordinator modules.

### Why constrained
- Captures most boilerplate savings while avoiding early overdesign.

## 5) Compile-time module synthesis (post-3.0 target)

### Direction
- Add deterministic compile-time generation from typed specs.
- Keep expansion inspectable (`--emit-expanded`) and cache-stable.
- Restrict side effects during expansion.

### Example direction
```hml
let llama_cpp = make_engine({
  name: "llama",
  repo: "...",
  candidates: [...],
  cmake_flags: cmake_flags_for,
  prebuilt_url: "..."
})
```

## Recommended Priority and Delivery Plan

### Hemlock 3.0 target
1. Typed records / structs
2. First-class function types
3. Sum types + exhaustive `match`
4. Protocols v1 (if schedule permits; otherwise 3.1)

### Hemlock 3.1+ target
5. Compile-time synthesis/macros (constrained, deterministic design)

## Acceptance Criteria (language/compiler)

- Record return values fail compile when fields are missing, extra, or mistyped.
- Callback parameters fail compile on incompatible function type.
- Enum/sum matches fail compile when non-exhaustive.
- Protocol conformance fails compile on missing/incorrect members.
- Diagnostics include actionable messages naming missing variants/fields/members.

## Risks and Mitigations

- **Complexity creep:** Stage features and keep v1 scopes narrow.
- **Tooling lag:** Land parser + typechecker + LSP updates in the same milestone for each feature.
- **Adoption churn:** Provide codemods and migration docs for string-tag switches and ad-hoc objects.

## Recommendation
Adopt all five ideas, but sequence them by dependency and risk:
- Build the type-system foundation first (records, function types, sum/exhaustive match).
- Layer protocols next for major boilerplate elimination in engine orchestrators.
- Defer synthesis/macros until the type and protocol surfaces are stable.

This plan captures most of the practical value for Hemlock 3 with manageable language-design risk.

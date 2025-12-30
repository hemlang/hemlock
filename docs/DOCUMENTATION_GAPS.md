# Hemlock Documentation Gaps Analysis

This document identifies gaps and areas for improvement in the Hemlock documentation. The analysis compares the current documentation in `docs/` and `stdlib/docs/` against features described in `CLAUDE.md` and implemented in the codebase.

---

## Critical Gaps (Missing Documentation)

### 1. Atomic Operations - NO DOCUMENTATION

**Status:** Implemented but not documented

**Location in codebase:** `src/backends/interpreter/builtins/atomics.c`, `runtime/src/atomics.c`

**Description in CLAUDE.md:**
```hemlock
// Atomic load/store
let val = atomic_load_i32(p);
atomic_store_i32(p, 42);

// Fetch-and-modify operations
let old = atomic_add_i32(p, 10);
old = atomic_sub_i32(p, 5);
old = atomic_and_i32(p, 0xFF);
old = atomic_or_i32(p, 0x10);
old = atomic_xor_i32(p, 0x0F);

// Compare-and-swap (CAS)
let success = atomic_cas_i32(p, 42, 100);

// Atomic exchange
old = atomic_exchange_i32(p, 999);

// Memory fence
atomic_fence();
```

**Recommended action:** Create `docs/advanced/atomics.md` with:
- Overview of lock-free programming in Hemlock
- All atomic functions (i32 and i64 variants)
- Memory ordering semantics (sequential consistency)
- Use cases and patterns
- Thread safety considerations

---

### 2. `defer` Statement - NO DOCUMENTATION

**Status:** Implemented but only mentioned as future feature in `docs/design/philosophy.md`

**Description in CLAUDE.md:**
```hemlock
defer cleanup();  // runs when function returns
```

**Current docs state:** `philosophy.md` line 66 says "No automatic resource cleanup (no RAII, no defer yet)" but CLAUDE.md shows it's implemented.

**Recommended action:** Create documentation for `defer`:
- Add section in `docs/language-guide/control-flow.md` or create `docs/language-guide/defer.md`
- Explain execution order with try/catch/finally
- Show resource cleanup patterns
- Update `philosophy.md` to reflect current state

---

### 3. Type Constructor Functions - INCOMPLETE DOCUMENTATION

**Status:** Implemented, partially documented

**Description in CLAUDE.md:**
```hemlock
let n = i32("42");       // Parse string to i32
let f = f64("3.14");     // Parse string to f64
let b = bool("true");    // Parse string to bool

// Convert between numeric types
let big = i64(42);       // i32 to i64
let truncated = i32(3.99); // f64 to i32 (truncates to 3)
```

**Current docs:** `docs/language-guide/types.md` covers type annotations but not type constructor functions.

**Recommended action:** Add section to `docs/language-guide/types.md`:
- Type constructor function syntax
- String parsing (i32("42"), f64("3.14"), bool("true"))
- Numeric type conversion (i64(42), i32(3.99))
- Hex and negative number parsing

---

### 4. Template Strings - NO DEDICATED DOCUMENTATION

**Status:** Implemented, only briefly mentioned

**Description in CLAUDE.md:**
```hemlock
Template strings: `` `Hello ${name}!` ``
```

**Current docs:** Brief mention in `docs/language-guide/strings.md` but no detailed documentation.

**Recommended action:** Add section to `docs/language-guide/strings.md`:
- Template string syntax with backticks
- Expression interpolation with `${}`
- Escaping special characters
- Multi-line template strings

---

### 5. Serialization Methods - INCOMPLETE DOCUMENTATION

**Status:** Implemented, mentioned in CLAUDE.md but not fully documented

**Description in CLAUDE.md:**
```hemlock
let json = p.serialize();
let restored = json.deserialize();
```

**Recommended action:** Document in `docs/reference/object-api.md` or create new file:
- `.serialize()` method on objects
- `.deserialize()` method on strings
- JSON format details
- Error handling

---

## Outdated/Conflicting Documentation

### 1. `select()` Function - CONFLICTING DOCUMENTATION

**Issue:** `docs/reference/builtins.md` documents `select()` as implemented, but `docs/advanced/async-concurrency.md` says "NOT YET SUPPORTED" in the limitations section.

**`builtins.md` (lines 839-900):** Documents `select(channels, timeout_ms?)` with examples

**`async-concurrency.md` (lines 651-669):** States "No select() for Multiplexing" as a current limitation

**Recommended action:** Remove the limitation section from `async-concurrency.md` if `select()` is now implemented, or clarify any differences.

---

## Documentation Organization Gaps

### 1. Missing API Reference Pages

The following could benefit from dedicated reference pages:

| Topic | Current State | Recommendation |
|-------|--------------|----------------|
| Object API | Scattered across docs | Create `docs/reference/object-api.md` |
| Enum API | In `types.md` only | Consider `docs/reference/enum-api.md` |
| Buffer API | In `memory-api.md` | Expand with all buffer methods |

---

### 2. Missing Advanced Topics

| Topic | Description | Priority |
|-------|-------------|----------|
| Atomics | Lock-free concurrent programming | High |
| Debugging | Debugging techniques, common errors | Medium |
| Performance | Optimization tips, profiling | Medium |
| Security | OWASP considerations, safe patterns | Medium |
| Best Practices | Comprehensive style guide | Low |

---

### 3. Missing Guides

| Guide Type | Description |
|------------|-------------|
| Troubleshooting | Common errors and solutions |
| Migration | Upgrading between versions |
| Cookbook | Common patterns and recipes |
| Integration | Using Hemlock with other tools |

---

## Standard Library Documentation Review

### Coverage Check

All 39 stdlib modules have documentation in `stdlib/docs/`:

| Module | Doc File | Status |
|--------|----------|--------|
| args | args.md | Present |
| assert | assert.md | Present |
| async | async.md | Present |
| async_fs | async_fs.md | Present |
| collections | collections.md | Present |
| compression | compression.md | Present |
| crypto | crypto.md | Present |
| csv | csv.md | Present |
| datetime | datetime.md | Present |
| encoding | encoding.md | Present |
| env | env.md | Present |
| fmt | fmt.md | Present |
| fs | fs.md | Present |
| glob | glob.md | Present |
| hash | hash.md | Present |
| http | http.md | Present |
| ipc | ipc.md | Present |
| iter | iter.md | Present |
| json | json.md | Present |
| logging | logging.md | Present |
| math | math.md | Present |
| net | net.md | Present |
| os | os.md | Present |
| path | path.md | Present |
| process | process.md | Present |
| random | random.md | Present |
| regex | regex.md | Present |
| retry | retry.md | Present |
| semver | semver.md | Present |
| shell | shell.md | Present |
| sqlite | sqlite.md | Present |
| strings | strings.md | Present |
| terminal | terminal.md | Present |
| testing | testing.md | Present |
| time | time.md | Present |
| toml | toml.md | Present |
| url | url.md | Present |
| uuid | uuid.md | Present |
| websocket | websocket.md | Present |

**All 39 modules documented.**

---

## Minor Issues

### 1. Inconsistent Type References

- Some docs use `integer` alias, others use `i32`
- Some docs use `number` alias, others use `f64`
- Recommendation: Standardize on primary types with alias mentions

### 2. Code Examples Using `typeof()` for String Conversion

**Status: Partially fixed**

Multiple documentation files incorrectly use `typeof()` for converting values to strings in concatenation:
```hemlock
print("Count: " + typeof(42));  // Wrong - typeof returns "i32", not "42"
```

Should use template strings:
```hemlock
print(`Count: ${42}`);  // Correct - template string interpolation
```

**Fixed in:**
- `docs/getting-started/quick-start.md`
- `docs/getting-started/tutorial.md`
- `docs/language-guide/control-flow.md`

**Still needs fixing:**
- `docs/language-guide/error-handling.md`
- `docs/advanced/command-execution.md`
- `docs/advanced/command-line-args.md`
- `docs/advanced/file-io.md`
- `docs/advanced/signals.md`
- `docs/reference/string-api.md`
- `docs/reference/operators.md`
- `docs/reference/builtins.md`
- `docs/reference/concurrency-api.md`
- `docs/language-guide/runes.md`
- `docs/language-guide/objects.md`

### 3. Missing Cross-References

Some docs could benefit from more cross-references:
- `types.md` should link to `builtins.md` for type constructor functions
- `memory.md` should link to `atomics.md` (once created)
- `async-concurrency.md` should link to stdlib `async.md`

---

## Priority Recommendations

### High Priority (Should be addressed first)

1. **Create `docs/advanced/atomics.md`** - Atomic operations are fully implemented but completely undocumented
2. **Document `defer` statement** - Either in control-flow.md or separate file
3. **Fix `select()` documentation conflict** - Resolve async-concurrency.md vs builtins.md

### Medium Priority

4. Add type constructor functions to `types.md`
5. Expand template string documentation in `strings.md`
6. Document serialization methods
7. Create troubleshooting guide

### Low Priority

8. Standardize type alias usage
9. Fix code examples using typeof() incorrectly
10. Add more cross-references
11. Create cookbook with common patterns

---

## Summary

| Category | Count |
|----------|-------|
| Missing documentation (critical) | 5 |
| Outdated/conflicting docs | 1 |
| Missing advanced topics | 5 |
| Missing guides | 4 |
| Minor issues | 3 |

The most significant gap is the lack of documentation for **atomic operations** - a fully implemented feature with no documentation. The `defer` statement is similarly undocumented despite being implemented. The `select()` function has conflicting documentation that needs resolution.

Overall, the documentation is comprehensive for most features, but these gaps should be addressed to ensure users can fully utilize Hemlock's capabilities.

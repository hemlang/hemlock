# Hemlock and the Gradual Guarantee: A Formal Analysis

**Status:** Research analysis (negative result with witnesses)
**Question:** Hemlock has optional type annotations enforced statically by `hemlockc` and
at runtime by the interpreter — a gradual type system. Does it satisfy the
*gradual guarantee* (Siek, Vitousek, Cimini & Boyland, SNAPL 2015)? Does its runtime
checking assign *blame* correctly (Wadler & Findler, ESOP 2009: "well-typed programs
can't be blamed")?

**Answer:** No, and no — both properties fail, each for several independent reasons.
This document formalizes what Hemlock's annotations actually do, states the two
criteria precisely, and gives minimal witness programs (all verified against the
current interpreter and compiler; both backends agree except where noted). It closes
with a taxonomy of the root causes, separating (a) deliberate design decisions that
make the *strict* guarantee unattainable for Hemlock, (b) semantic choices that
violate even a weakened guarantee, and (c) outright inconsistencies that look like
bugs under any reading.

All witness programs live in `docs/design/gradual-typing/` and are runnable directly:

```bash
./hemlock docs/design/gradual-typing/gg1_typed.hml
```

They are deliberately **not** wired into the test suites: several crash by design,
and one (`blame3_ref.hml`) is accepted by the interpreter but rejected by the static
checker, so it cannot be a parity test until the divergence is resolved.

---

## 1. The two criteria

### 1.1 The gradual guarantee

Write `e' ⊑ e` when program `e'` is `e` with some type annotations removed
(`e'` is *less precise*). The gradual guarantee has two halves:

- **Static gradual guarantee (SGG).** If `e` is accepted by the type checker and
  `e' ⊑ e`, then `e'` is accepted too. Removing annotations never introduces a
  static error.
- **Dynamic gradual guarantee (DGG).** If `e ⇓ v` (runs to a value), then `e' ⇓ v'`
  with `v'` the same value up to precision of type information. Removing annotations
  never changes a successful result and never introduces a runtime error; the *only*
  permitted behavioral difference is that the less-precise program may succeed where
  the more-precise one raised a cast error.

Intuition: annotations may *narrow* what a program accepts, but they must otherwise
be behaviorally inert. An annotation is a checked assertion, not an operation.

### 1.2 The blame theorem

In a gradual language, a runtime cast failure occurs at a *boundary* between typed
and untyped code. A blame-tracking semantics labels each boundary and reports, on
failure, which side supplied the value that broke the contract. The blame theorem
states that a fully, statically typed region can never be the blamed party: when a
cast fails, the fault always lies with the less-precisely-typed side. Practically,
this is what makes gradual types *debuggable*: the error message points at the
boundary and at the untyped culprit, not at innocent typed code downstream.

---

## 2. What a Hemlock annotation actually does

This section is the formalization. Hemlock annotations appear in five positions,
and the runtime gives each a semantics via a single function,
`convert_to_type` (`src/backends/interpreter/types.c:1416`). The compiled backend
implements the same semantics (verified on all witnesses below); the static checker
adds a separate layer described in §3.

Let `κ_T(v)` denote `convert_to_type(v, T)`. The crucial fact is:

> **`κ_T` is a *coercion*, not a cast.** It does not merely check `v : T` and pass
> `v` through or fail — it *constructs a new value of type T from v*, and for
> aggregates it *mutates v in place*.

Concretely, from the implementation:

| Source → Target | Behavior of `κ_T` | Where |
|---|---|---|
| numeric → numeric | converts, with range check; **f64 → int truncates** | `types.c:1726-1825` |
| numeric/rune → `bool` | truthiness conversion (`3` ⇒ `true`) | `types.c:1827-1836` |
| numeric/bool/rune → `string` | stringifies (`42` ⇒ `"42"`) | `types.c:1838-1867` |
| int ↔ `rune` | codepoint conversion | `types.c:1869-1883` |
| string → numeric/bool | catchable runtime error | `types.c:1674-1689` |
| array → `array<T>` | **in-place**: brands the array with element type `T` and rewrites every element to `κ_T(elem)` | `types.c:1599-1653` |
| object → `define`d type | **in-place**: rewrites each declared field to `κ_F(field)`, **inserts missing optional fields** (defaults or `null`), and **stamps the nominal brand** `obj->type_name` | `types.c:1028-1033`, `963-1027`, `1126-1128` |
| function → `fn(...)  : R` | compares *declared* signatures only (missing annotation = compatible with anything), then returns the function **unwrapped** — no runtime monitoring is installed | `types.c:1429-1499` |

The five annotation positions all invoke `κ`:

1. **`let`/`const` binding** — `κ` at initialization, and the declared type is
   remembered and re-applied on every reassignment
   (`src/backends/interpreter/runtime/statements.c:16-27`,
   `src/backends/interpreter/runtime/expressions.c:239-254`).
2. **Function parameter** — `κ` on the argument at call time
   (`src/backends/interpreter/runtime/eval_calls.c:1081-1082`) — **except `ref`
   parameters, which skip the check entirely** (the `!is_ref_param` guard on that
   line), and writes through a `ref` also bypass the declared-type re-check
   (`expressions.c:232-237`).
3. **Function return type** — `κ` on the returned value in the callee's frame
   (`eval_calls.c:1157-1162`).
4. **`define` field types** — `κ` applied when an object is checked against the
   type, i.e. only at annotation boundaries; field *writes* after the check are
   unmonitored.
5. **Typed-array element types** — `κ` at branding; thereafter `push`/`unshift`/
   `insert` enforce the brand by **exact tag equality, with no coercion**
   (`src/backends/interpreter/io/array_methods.c:94-127`).

Two consequences of the aggregate cases deserve emphasis, because every deep
counterexample below flows from them:

- **Annotations are effectful.** `κ` mutates shared state (array elements, object
  fields, the brand) visible through *other aliases* that never mentioned a type.
- **Annotations are lossy.** `κ` can change a value (`3.99` ⇒ `3`) without error.

The runtime's *consistency relation* is otherwise the standard gradual one: a
missing annotation is compatible with everything (`types_compatible`,
`types.c:806-808`, treats `NULL`/`INFER` as "any"). So Hemlock genuinely has the
skeleton of a gradual language; the trouble is what happens at the boundaries.

---

## 3. The static gradual guarantee: VIOLATED

**Witness S1** (`sgg1_typed.hml` / `sgg1_untyped.hml`):

```hemlock
// Accepted: 0 errors            // REJECTED after erasing the annotation:
let a: array = [1, 2, 3];        let a = [1, 2, 3];
a = ["x", "y"];                  a = ["x", "y"];
print(a.length);                 // error: cannot assign 'array<string>' to
                                 // variable 'a' of type 'array<i32>'
```

Verified: `hemlock check sgg1_typed.hml` reports 0 errors; erasing `: array`
produces a type error from both `hemlock check` and `hemlockc`. Both versions run
identically under the interpreter (which applies no static checks). Removing an
annotation introduced a static error — a direct SGG violation.

**Root cause.** For an unannotated `let`, the checker does not assign the dynamic
type; it *infers a rigid type from the initializer*
(`src/backends/compiler/type_check_stmt.c:343-344`) and then enforces it on every
assignment (`type_check_expr.c:574-586`). Array literals infer their element type
from the first element (`type_infer.c:274-281`). So `let a = [1,2,3]` is pinned to
`array<i32>`, while the *annotated* `let a: array = [1,2,3]` is `array` with no
element constraint — the annotation is strictly *more permissive* than its own
erasure. In a language satisfying SGG, erasure must always move toward `any`; here
inference moves it toward a rigid type instead. (The only escape hatch is
`let x = null`, which the checker special-cases to dynamic —
`type_check_expr.c:575-577`.)

This is a general phenomenon, not an array quirk: any position where inference is
stricter than some legal annotation yields an SGG counterexample. Inference-as-
pinning and gradual typing are in direct tension unless inferred types are treated
as *lower bounds subject to widening* (or unless there is an explicit `any`
annotation, which Hemlock's surface syntax lacks).

A related, smaller gap: the checker validates argument types at *direct* calls
(`inc(s)` where `fn inc(ref x: i32)` is rejected statically) but not at calls
through `fn`-typed variables — `let g: fn(i32): i32 = ...; g("hello")` passes
`hemlock check` with 0 errors. This does not violate SGG by itself, but it means
the static layer cannot compensate for the runtime gaps in §5.

---

## 4. The dynamic gradual guarantee: VIOLATED

Three independent mechanisms, in increasing order of severity. All verified on both
backends with identical output.

### 4.1 Ascription changes values (lossy `κ`)

**Witness GG1** (`gg1_typed.hml` / `gg1_untyped.hml`):

```hemlock
let b: i32 = 3.99;    // b = 3        // erased: b = 3.99
let c: bool = 3;      // c = true     // erased: c = 3
let s: string = 42;   // s = "42"     // erased: s = 42 (i32)
```

Annotated output: `3 / true / 42` with types `i32 / bool / string`. Erased output:
`3.99 / 3 / 42` with types `f64 / i32 / i32`. No error is involved on either side;
the results simply differ. DGG requires the results to agree.

### 4.2 Annotation effects are visible through aliases

**Witness GG2** (`gg2_typed.hml` / `gg2_untyped.hml`):

```hemlock
let a = [1.5, 2.5];
let b: array<i32> = a;   // in-place: a's own elements become 1, 2 (i32)
print(a[0]);             // annotated: 1   — erased: 1.5
```

The annotation on `b` rewrote the array that `a` — a binding in "untyped code" —
refers to. Action at a distance: the observable behavior of code that mentions no
types depends on an annotation elsewhere in the program.

### 4.3 Removing an annotation can *introduce* a runtime error

This is the strongest violation: DGG's one permitted asymmetry is that erasure may
remove errors, never add them. **Witness GG3** (`gg3_typed.hml` / `gg3_untyped.hml`):

```hemlock
define Person { name: string, age: i32, active?: true }
let alice = { name: "Alice", age: 30.7 };
let p: Person = alice;        // ← the only annotation
print(alice.age);             // annotated: 30 (coerced in place; erased: 30.7)
print(alice.active);          // annotated: true — the check MATERIALIZED the
                              //   optional field on the shared object.
                              // erased: UNCAUGHT EXCEPTION
                              //   "Object has no field 'active'"
let category = match (alice) {
    q: Person => "is Person", // annotated: matches — the check stamped the
    _ => "not Person"         //   nominal brand obj->type_name = "Person"
};                            // erased: would print "not Person"
```

Verified: the annotated program prints `30 / true / is Person / 3` and exits 0; the
erased program crashes at `alice.active`. Three distinct sub-mechanisms, each
sufficient for a DGG violation:

1. **Field coercion in place** (`types.c:1031-1032`): `30.7` becomes `30` as seen
   through `alice`.
2. **Optional-field materialization** (`types.c:988-1020`): the check *adds*
   `active: true` to the object; erasure removes the field and turns a working
   program into a crash. The object's `keys().length` also changes (3 vs 2).
3. **Nominal branding** (`types.c:1126-1128`): `match` type patterns test the brand,
   not structure (`src/backends/interpreter/runtime/eval_pattern_match.c:28-37`), so
   whether *any annotation anywhere* ever touched an object changes which `match`
   arm runs — even in otherwise untyped code holding an alias.

### 4.4 An inherent obstruction (design, not bug)

Even if every mechanism above were fixed, Hemlock could not satisfy the strict DGG,
for a principled reason: **numeric types carry semantics**. Width and signedness
determine overflow behavior, and `typeof`/`typeid` reify tags into observable
values. **Witness GG4**:

```hemlock
let a: i64 = 2147483647;
print(a + 1);            // annotated: 2147483648
                         // erased: Uncaught "Integer overflow: i32 addition"
```

The erased program infers `i32` for the literal and i32 addition overflows —
by documented policy (`docs/language-guide/types.md`). Here the annotation is not
an assertion about a value; it *selects which arithmetic* the program performs.
Languages in which annotations select representations (like typed Racket's numeric
tower or Cython) face the same obstruction; the literature's response is a
*weakened* guarantee that quotients results by numeric precision. The honest
statement for Hemlock is therefore:

> Hemlock should target the DGG *modulo type-directed numeric semantics* — i.e.
> annotations may select width/precision (GG4 is acceptable), but must otherwise be
> value-preserving and effect-free (GG1–GG3 are not acceptable, weakened or not).

Notably, GG1–GG3 also violate Hemlock's *own* stated philosophy ("explicit over
implicit"; "no hidden behavior"): a `let x: bool = 3;` that silently manufactures
`true` is an implicit conversion of exactly the kind the language rejects elsewhere
(it already refuses implicit `string → i32` on this very ground, telling the user
to write `i32("...")` — the same argument applies to `bool(3)` and truncation).

---

## 5. Blame: there is no blame — and typed code gets blamed

Hemlock installs **no wrappers and no blame labels**. Function-type "casts" compare
declared signatures once and then erase (`types.c:1429-1499`); values that cross a
typed/untyped boundary carry no record of the boundary. The consequences are
exactly what the blame theorem exists to prevent.

### 5.1 A `fn`-typed binding monitors nothing

**Witness B6** (`b6_fnalias.hml`):

```hemlock
let g: fn(i32): i32 = fn(x) { return x; };   // accepted: unannotated ≈ any
let r = g("hello");
print(r);          // "hello"
print(typeof(r));  // string  — from a binding whose type says i32 → i32
```

No error, ever (and `hemlock check` is also silent, §3). The annotation on `g` is
purely declarative. In cast-calculus terms, Hemlock implements the *erasure*
semantics for higher-order casts — the semantics the blame literature uses as the
counterpoint that satisfies nothing.

### 5.2 When the failure does surface, the typed side is blamed

**Witness B1** (`blame1.hml`):

```hemlock
fn applyf(f: fn(i32): i32, x: i32): i32 {
    return f(x) + 1;      // typed code, provably type-correct
}
let untyped = fn(x) { return "oops"; };
let r = applyf(untyped, 5);
```

Under Wadler–Findler, the cast `fn(i32):i32 ⇐ dyn` on `untyped` gets a negative
blame label; when `untyped` returns a string, blame falls on `untyped`. What
Hemlock actually reports:

```
Uncaught exception: Cannot convert string to i32 via type annotation. Use i32("...") instead.
Stack trace (most recent call first):
  #0  applyf (blame1.hml:5)
```

Sequence of events: the boundary check passed (unannotated params ≈ any);
`f(x)` returned `"oops"` unchecked; typed code then computed `"oops" + 1 = "oops1"`
(string concatenation) — **the ill-typed value was already used, and corrupted, by
typed code before any check fired**; finally `applyf`'s *return* coercion failed.
The report names only the well-typed function and even suggests editing it
(`Use i32("...")`). The untyped culprit appears nowhere. Witness B2
(`blame2.hml`) shows the same failure surfacing at a `let x: i32 = call_it(liar);`
binding — again a site in typed code, arbitrarily far from the lie.

This is a precise counterexample to "well-typed programs can't be blamed": the
blamed frame is statically well-typed under Hemlock's own checker.

### 5.3 `ref` parameters: unsoundness with no error at all

**Witness B3** (`blame3_ref.hml`):

```hemlock
fn inc(ref x: i32) { x = x + 1; }
let s = "hi";
inc(s);
print(s);          // "hi1"  (string!) — no error, annotation silently ignored
```

`ref` parameters skip the boundary coercion (`eval_calls.c:1081`) *and* writes
through the reference skip the declared-type re-check (`expressions.c:232-237`).
Typed code computed on a string under an `i32` annotation and wrote the corrupted
value back into the caller — silently. There is no cast to fail, hence nothing to
blame. (The static checker *does* reject this program at a direct call site, so the
interpreter and `hemlockc` disagree about whether it is a program — itself a
parity-adjacent divergence.)

### 5.4 Inconsistent failure discipline

Cast failures currently land in three different regimes:

| Failure | Discipline | Location info | Witness |
|---|---|---|---|
| scalar coercion failure (e.g. string→i32) | catchable exception | file/line when context available | B8 (`b8_argblame.hml`) |
| typed-array `push` violation | catchable exception | none | B5 |
| function-signature, enum, object-shape mismatch | `fprintf` + `exit(1)` — **uncatchable**, bypasses `try`/`defer` | none | B4 (`blame4_exit.hml`) |

B4 verified: the `catch` block never runs; the process exits from inside
`convert_to_type` (`types.c:1435-1498` and the object/enum paths use
`exit(1)` directly). A language whose selling point is that errors are explicit
and catchable (`try`/`catch`, catchable overflow) hard-exits on a type boundary
depending on *which kind* of type is involved.

### 5.5 Brand-time coercion vs. use-time exact match

**Witness B5** (`blame5_brand.hml`):

```hemlock
fn setup(arr: array<i64>) { }   // parameter annotation brands caller's array
let a = [1, 2];                 // i32 elements — coerced to i64 by the brand
setup(a);
a.push(3);                      // FAILS: push requires exact i64 tag; 3 is i32
```

At branding time, `κ` happily coerces `i32 → i64`; at use time,
`check_array_element_type_for_method` demands exact tag equality
(`array_methods.c:102-124`). The same value is acceptable or fatal depending on
which code path checks it, and the error (`"Type mismatch in typed array"`) carries
no location and no mention of which annotation created the constraint — the
constraint was installed by a callee the caller may not even source-control. This is
the one case where blame lands on the untyped side, but for the wrong reason and
with no way to trace it.

### 5.6 Where blame is (approximately) right

For completeness: first-order, non-`ref` parameter and return annotations behave
like a *transient* (Vitousek-style) check done shallowly — the check fires at the
boundary, is catchable, and when the bad value comes directly from the caller the
error surfaces at the call, which is the correct party (Witness B8). Assignment to
an annotated variable is likewise re-checked (`expressions.c:239-254`). The failures
above are therefore not "no checking anywhere" — they are specifically: aggregates
(effectful, §4), higher-order (erased, §5.1–5.2), `ref` (absent, §5.3), and the
error-discipline split (§5.4).

---

## 6. Taxonomy and repair options

**(a) Inherent to Hemlock's design — keep, and weaken the criterion.**
Type-directed numeric semantics (GG4) and reified tags (`typeof`). Adopt the DGG
*modulo numeric precision* as Hemlock's official target; document it.

**(b) Semantic choices that violate even the weakened guarantee — change semantics.**

1. **Make ascription a check, not a conversion, for non-numeric targets.** The
   precedent already exists: string→numeric is refused with "use `i32("...")`".
   Apply the same rule to numeric→bool, numeric→string, int↔rune, and float→int
   truncation at annotation boundaries. (Numeric width conversions can stay, per (a),
   though float→int truncation is hard to defend as "width selection".)
2. **Stop mutating aggregates at checks.** Array branding should validate elements
   without rewriting them (or brands should be per-*binding* views, not stamps on
   the shared heap object); object checks should not insert optional fields into the
   checked object and should not let a brand affect `match` in code that never
   asserted the type. If branding must stay, `match` type patterns should fall back
   to structural checking so behavior does not depend on annotation history.
3. **Fix inference-as-pinning** (SGG): treat inferred types as widenable, or widen
   the inferred type to `array` / dynamic on conflicting assignment, or introduce an
   explicit `any` so users can at least write the dynamic type the checker already
   has internally (`CHECKED_ANY`).

**(c) Inconsistencies that are bugs under any reading — fix.**

4. **`ref` parameters must check** at the boundary and on write-back
   (`eval_calls.c:1081`, `expressions.c:232-237`) — or the manual should state that
   `ref` erases annotations. Interpreter and static checker must agree.
5. **Unify failure discipline**: every boundary failure should be a catchable
   runtime error with file/line (replace the `exit(1)` paths in
   `convert_to_type` / `check_object_type` with `runtime_error`).
6. **Align brand-time and use-time element checks** (coerce in both places or
   exact-match in both; today's mix makes annotated arrays reject values the
   annotation itself accepted).

**Toward blame.** Full Wadler–Findler wrappers contradict Hemlock's "no hidden
behavior" stance (proxies are hidden allocation and indirection) and are not
recommended. A Hemlock-flavored alternative is *transient blame*: keep shallow
checks, but (i) check function results at the *call site* of any call through a
`fn`-typed binding or parameter (one tag check, no wrapper), and (ii) attach to
every boundary error the source location of the *annotation* that installed the
constraint alongside the location where the value tripped it. That is enough to
restore the practical content of the blame theorem — errors point at boundaries,
never at the interior of well-typed code — at the cost of one tag check per
boundary crossing, with zero heap machinery. Vitousek et al. (POPL 2017) prove the
corresponding theorem ("open-world soundness") for exactly this class of designs,
so the proof technique is off the shelf once §6(b) makes annotations effect-free.

---

## 7. Summary

| Property | Verdict | Independent causes |
|---|---|---|
| Static gradual guarantee | **Violated** | inference pins unannotated bindings to rigid types (no `any`) |
| Dynamic gradual guarantee | **Violated** | lossy ascription; effectful checks on shared aggregates (in-place coercion, field materialization, nominal brand + `match`); plus an inherent numeric-semantics obstruction that warrants a weakened criterion |
| Blame theorem | **Fails** | higher-order casts erased (no monitoring); failures surface inside and are attributed to well-typed code; `ref` params unchecked; three inconsistent failure disciplines, one uncatchable |

The failures are not exotic corner cases; each witness is a handful of lines of
idiomatic Hemlock. The encouraging part of the result: the *first-order, non-`ref`,
scalar* fragment of Hemlock already behaves like a correct transient gradual
system, and the fixes in §6(b)/(c) are local to `convert_to_type` and its call
sites — after which a formal proof of the weakened DGG and a transient blame
theorem for Hemlock look tractable with existing techniques.

## References

- J. Siek, M. Vitousek, M. Cimini, J. T. Boyland. *Refined Criteria for Gradual
  Typing.* SNAPL 2015. (The gradual guarantee.)
- P. Wadler, R. B. Findler. *Well-Typed Programs Can't Be Blamed.* ESOP 2009.
- M. Vitousek, C. Swords, J. Siek. *Big Types in Little Runtime: Open-World
  Soundness and Collaborative Blame for Gradual Type Systems.* POPL 2017.
  (Transient checks; the model closest to Hemlock's first-order fragment.)

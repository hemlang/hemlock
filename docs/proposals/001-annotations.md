# Proposal 001: Annotations

**Status:** Idea
**Author:** Claude + Human
**Related:** Tricycle memory safety checker

---

## Summary

Add `@annotation` syntax to Hemlock for metadata on functions, types, and declarations.

---

## Motivation

Tricycle needs a way to mark functions as safe/unsafe. Rather than invent something ad-hoc, a general annotation system enables:

- Safety markers (`@safe`, `@unsafe`)
- Compiler hints (`@inline`, `@cold`)
- Deprecation warnings (`@deprecated`)
- Testing (`@test`)
- Documentation (`@since`, `@author`)
- Custom tooling hooks

---

## Syntax Options

### Option A: `@name` (Java/Rust-like)

```hemlock
@safe
fn process(borrow buf: ptr): i32 {
    return ptr_read_i32(buf);
}

@deprecated("use v2")
@inline
fn old_func() { }

@test
fn test_addition() {
    assert(1 + 1 == 2);
}
```

**Pros:** Familiar, clean, widely recognized
**Cons:** `@` is new syntax for Hemlock

### Option B: `#[name]` (Rust attributes)

```hemlock
#[safe]
fn process(borrow buf: ptr): i32 {
    return ptr_read_i32(buf);
}

#[deprecated("use v2")]
#[inline]
fn old_func() { }
```

**Pros:** Distinct from other syntax, allows `#[name(args)]`
**Cons:** More verbose, less intuitive

### Option C: `@name:` prefix (comment-like)

```hemlock
// @safe:
fn process(borrow buf: ptr): i32 {
    return ptr_read_i32(buf);
}

// @deprecated: use v2
fn old_func() { }
```

**Pros:** No parser changes (parsed from comments)
**Cons:** Fragile, feels hacky

### Option D: Pragmas (C-like)

```hemlock
#pragma safe
fn process(borrow buf: ptr): i32 {
    return ptr_read_i32(buf);
}
```

**Pros:** Familiar to C programmers
**Cons:** Doesn't feel modern, verbose

---

## Recommended: Option A (`@name`)

Most readable, widely understood, minimal syntax addition.

---

## Grammar

```ebnf
annotation      ::= "@" IDENT annotation_args?
annotation_args ::= "(" annotation_arg ("," annotation_arg)* ")"
annotation_arg  ::= STRING | NUMBER | IDENT | IDENT "=" (STRING | NUMBER | IDENT)

annotated_decl  ::= annotation* declaration
declaration     ::= fn_decl | define_decl | enum_decl | let_decl
```

---

## Core Annotations

### Safety (Tricycle)

| Annotation | Meaning |
|------------|---------|
| `@safe` | Function must pass tricycle analysis |
| `@unsafe` | Function is exempt from tricycle (explicit opt-out) |
| `@trusted` | Function is assumed safe without analysis (escape hatch) |

```hemlock
@safe
fn validated_read(borrow buf: ptr, len: i32, idx: i32): i32 {
    if (idx < 0 || idx >= len) {
        panic("out of bounds");
    }
    return ptr_read_i32(ptr_offset(buf, idx * 4));
}

@unsafe
fn raw_read(buf: ptr, idx: i32): i32 {
    return ptr_read_i32(ptr_offset(buf, idx * 4));
}

@trusted  // "I verified this manually, trust me"
extern fn system_alloc(size: i32): ptr;
```

### Compiler Hints

| Annotation | Meaning |
|------------|---------|
| `@inline` | Hint to inline this function |
| `@noinline` | Prevent inlining |
| `@cold` | Rarely called (optimize for size) |
| `@hot` | Frequently called (optimize for speed) |
| `@pure` | No side effects, same inputs = same output |

```hemlock
@inline
fn min(a: i32, b: i32): i32 => a < b ? a : b;

@cold
fn handle_error(msg: string) {
    eprint("ERROR: " + msg);
    exit(1);
}

@pure
fn square(x: i32): i32 => x * x;
```

### Deprecation

```hemlock
@deprecated
fn old_api() { }

@deprecated("use new_api() instead")
fn old_api_v2() { }

@deprecated(since: "1.5.0", replacement: "new_api")
fn old_api_v3() { }
```

Compiler emits warning when deprecated function is called.

### Testing

```hemlock
@test
fn test_basic_math() {
    assert(2 + 2 == 4);
}

@test("handles empty input")
fn test_empty_string() {
    assert(len("") == 0);
}

@test
@skip("not implemented yet")
fn test_future_feature() { }

@test
@timeout(5000)  // 5 second timeout
fn test_slow_operation() { }
```

Run with: `hemlock test file.hml`

### Documentation

```hemlock
@author("Alice")
@since("1.2.0")
@see("related_function")
fn my_function() { }
```

### Custom / User-Defined

Allow arbitrary annotations for tooling:

```hemlock
@my_tool::special_marker
fn custom_handled() { }

@benchmark(iterations: 1000)
fn perf_test() { }
```

Unknown annotations are ignored by compiler but available in AST for tools.

---

## AST Representation

```c
typedef struct Annotation {
    char *name;                 // "safe", "deprecated", etc.
    AnnotationArg *args;        // Optional arguments
    int arg_count;
    int line, column;           // Source location
} Annotation;

typedef struct AnnotationArg {
    char *name;                 // NULL for positional, "since" for named
    enum { ARG_STRING, ARG_NUMBER, ARG_IDENT } kind;
    union {
        char *string_val;
        double number_val;
        char *ident_val;
    } value;
} AnnotationArg;

// Added to relevant AST nodes
struct FnDecl {
    Annotation *annotations;
    int annotation_count;
    // ... existing fields
};
```

---

## Implementation Phases

### Phase 1: Parser Support
- Lexer recognizes `@` token
- Parser attaches annotations to declarations
- Annotations stored in AST
- Unknown annotations ignored (forward compatibility)

### Phase 2: Core Annotations
- `@deprecated` — compiler warnings
- `@inline` / `@noinline` — codegen hints

### Phase 3: Tricycle Integration
- `@safe` / `@unsafe` / `@trusted`
- Tricycle reads annotations from AST

### Phase 4: Testing Framework
- `@test` annotation
- `hemlock test` command
- `@skip`, `@timeout`, `@only`

### Phase 5: Custom Annotations
- API for tools to query annotations
- Documentation generation from annotations

---

## Alternatives Considered

### Do Nothing
Keep tricycle annotation-free, use file-level or project-level config.

**Problem:** No per-function granularity.

### Magic Comments
```hemlock
// tricycle: safe
fn foo() { }
```

**Problem:** Fragile, not part of AST, tooling has to parse comments.

### Separate Declaration
```hemlock
@safe(foo, bar, baz);  // Mark multiple functions

fn foo() { }
fn bar() { }
```

**Problem:** Separates annotation from function, easy to get out of sync.

---

## Open Questions

1. **Should annotations be expressions?**
   ```hemlock
   let handler = @memoize fn(x) { expensive(x) };
   ```

2. **Annotation inheritance?**
   ```hemlock
   @safe  // Does this apply to nested functions?
   fn outer() {
       fn inner() { }  // Also @safe?
   }
   ```

3. **Annotation on statements?**
   ```hemlock
   @likely
   if (common_case) { }
   ```

4. **Runtime access to annotations?**
   ```hemlock
   let annots = reflect.annotations(my_function);
   ```

---

## References

- [Java Annotations](https://docs.oracle.com/javase/tutorial/java/annotations/)
- [Rust Attributes](https://doc.rust-lang.org/reference/attributes.html)
- [Python Decorators](https://peps.python.org/pep-0318/)
- [C++ Attributes](https://en.cppreference.com/w/cpp/language/attributes)

---

*Status: Idea — needs discussion before implementation*

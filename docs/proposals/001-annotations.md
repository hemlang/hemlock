# Proposal 001: Annotations

**Status:** Ready for Implementation
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

## Syntax

**Chosen: Option A (`@name`)**

Most readable, widely understood, minimal syntax addition.

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

---

## Grammar

```ebnf
annotation      ::= "@" IDENT annotation_args?
annotation_args ::= "(" annotation_arg ("," annotation_arg)* ")"
annotation_arg  ::= STRING | NUMBER | IDENT | IDENT "=" (STRING | NUMBER | IDENT)

annotated_decl  ::= annotation* declaration
declaration     ::= fn_decl | define_decl | enum_decl | let_decl | const_decl
```

---

## Design Decisions

### What Can Be Annotated

**Phase 1 — Declarations only:**
- Functions (`fn`, `async fn`)
- Type definitions (`define`)
- Enums (`enum`)
- Top-level variables (`let`, `const`)

**NOT supported in Phase 1:**
- Expressions (no `@memoize fn(x) { }`)
- Statements (no `@likely if (...)`)
- Parameters (no `fn foo(@unused x: i32)`)

### Annotation Inheritance

**No inheritance.** Annotations do not propagate to nested functions.

```hemlock
@safe
fn outer() {
    fn inner() { }  // NOT @safe — must be explicit
}
```

### Multiple Annotations

**Allowed.** Order does not matter semantically.

```hemlock
@deprecated("use v2")
@inline
@pure
fn old_math(x: i32): i32 => x * 2;

// Equivalent to:
@pure
@inline
@deprecated("use v2")
fn old_math(x: i32): i32 => x * 2;
```

### Unknown Annotations

**Warning, not error.** Enables forward compatibility and custom tooling.

```hemlock
@my_custom_tool_marker  // Warning: unknown annotation 'my_custom_tool_marker'
fn handler() { }
```

### Duplicate Annotations

**Warning.** Same annotation twice is likely a mistake.

```hemlock
@inline
@inline  // Warning: duplicate annotation 'inline'
fn foo() { }
```

### Invalid Targets

**Error.** Annotation on wrong declaration type.

```hemlock
@inline
let x = 42;  // Error: @inline is only valid on functions

@deprecated("old")
fn valid() { }  // OK
```

### Runtime Reflection

**Not supported in Phase 1.** Annotations are compile-time only.

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

| Annotation | Meaning | Valid On |
|------------|---------|----------|
| `@inline` | Hint to inline this function | `fn` |
| `@noinline` | Prevent inlining | `fn` |
| `@cold` | Rarely called (optimize for size) | `fn` |
| `@hot` | Frequently called (optimize for speed) | `fn` |
| `@pure` | No side effects, same inputs = same output | `fn` |

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

| Annotation | Valid On |
|------------|----------|
| `@deprecated` | `fn`, `define`, `enum`, `let`, `const` |

```hemlock
@deprecated
fn old_api() { }

@deprecated("use new_api() instead")
fn old_api_v2() { }

@deprecated(since: "1.5.0", replacement: "new_api")
fn old_api_v3() { }
```

Compiler emits warning when deprecated item is used.

### Testing

| Annotation | Meaning | Valid On |
|------------|---------|----------|
| `@test` | Marks function as test case | `fn` |
| `@skip` | Skip this test | `fn` (with `@test`) |
| `@timeout(ms)` | Test timeout in milliseconds | `fn` (with `@test`) |

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

| Annotation | Meaning | Valid On |
|------------|---------|----------|
| `@author(name)` | Code author | any |
| `@since(version)` | Version introduced | any |
| `@see(reference)` | Related item | any |

```hemlock
@author("Alice")
@since("1.2.0")
@see("related_function")
fn my_function() { }
```

---

## AST Representation

```c
typedef enum {
    ARG_STRING,
    ARG_NUMBER,
    ARG_IDENT
} AnnotationArgKind;

typedef struct AnnotationArg {
    char *name;                 // NULL for positional, "since" for named
    AnnotationArgKind kind;
    union {
        char *string_val;
        double number_val;
        char *ident_val;
    } value;
    int line, column;
} AnnotationArg;

typedef struct Annotation {
    char *name;                 // "safe", "deprecated", etc.
    AnnotationArg *args;        // Optional arguments
    int arg_count;
    int arg_capacity;
    int line, column;           // Source location
} Annotation;

// Helper functions
Annotation *annotation_new(const char *name, int line, int column);
void annotation_add_arg_string(Annotation *a, const char *name, const char *value);
void annotation_add_arg_number(Annotation *a, const char *name, double value);
void annotation_add_arg_ident(Annotation *a, const char *name, const char *ident);
void annotation_free(Annotation *a);

// Query functions
bool annotation_has(Annotation **annotations, int count, const char *name);
Annotation *annotation_get(Annotation **annotations, int count, const char *name);
const char *annotation_get_string_arg(Annotation *a, const char *name, const char *default_val);
double annotation_get_number_arg(Annotation *a, const char *name, double default_val);
```

### AST Node Changes

Add to relevant AST node structs:

```c
// In ast.h

typedef struct FnDeclData {
    // ... existing fields ...
    Annotation **annotations;
    int annotation_count;
} FnDeclData;

typedef struct DefineDeclData {
    // ... existing fields ...
    Annotation **annotations;
    int annotation_count;
} DefineDeclData;

typedef struct EnumDeclData {
    // ... existing fields ...
    Annotation **annotations;
    int annotation_count;
} EnumDeclData;

typedef struct LetDeclData {
    // ... existing fields ...
    Annotation **annotations;
    int annotation_count;
} LetDeclData;
```

---

## Lexer Changes

Add new token type:

```c
// In lexer.h
typedef enum {
    // ... existing tokens ...
    TOKEN_AT,           // @
    // ...
} TokenType;
```

In `lexer.c`, handle `@`:

```c
case '@':
    return make_token(lexer, TOKEN_AT);
```

---

## Parser Changes

### Annotation Parsing

```c
// In parser, add:

static Annotation *parse_annotation(Parser *p) {
    expect(p, TOKEN_AT);  // consume @

    Token name = expect(p, TOKEN_IDENT);
    Annotation *a = annotation_new(name.lexeme, name.line, name.column);

    // Optional arguments
    if (match(p, TOKEN_LPAREN)) {
        do {
            if (check(p, TOKEN_IDENT) && check_next(p, TOKEN_EQ)) {
                // Named argument: name = value
                Token arg_name = expect(p, TOKEN_IDENT);
                expect(p, TOKEN_EQ);
                parse_annotation_arg_value(p, a, arg_name.lexeme);
            } else {
                // Positional argument
                parse_annotation_arg_value(p, a, NULL);
            }
        } while (match(p, TOKEN_COMMA));
        expect(p, TOKEN_RPAREN);
    }

    return a;
}

static Annotation **parse_annotations(Parser *p, int *count) {
    Annotation **annotations = NULL;
    *count = 0;
    int capacity = 0;

    while (check(p, TOKEN_AT)) {
        Annotation *a = parse_annotation(p);
        // grow array if needed, append a
        ...
    }

    return annotations;
}
```

### Declaration Parsing

Modify declaration parsers to collect annotations first:

```c
static Stmt *parse_declaration(Parser *p) {
    int annotation_count = 0;
    Annotation **annotations = parse_annotations(p, &annotation_count);

    Stmt *decl;
    if (match(p, TOKEN_FN)) {
        decl = parse_fn_decl(p);
    } else if (match(p, TOKEN_ASYNC)) {
        expect(p, TOKEN_FN);
        decl = parse_async_fn_decl(p);
    } else if (match(p, TOKEN_DEFINE)) {
        decl = parse_define_decl(p);
    } else if (match(p, TOKEN_ENUM)) {
        decl = parse_enum_decl(p);
    } else if (match(p, TOKEN_LET)) {
        decl = parse_let_decl(p);
    } else if (match(p, TOKEN_CONST)) {
        decl = parse_const_decl(p);
    } else {
        if (annotation_count > 0) {
            error(p, "annotations must precede a declaration");
        }
        return parse_statement(p);
    }

    // Attach annotations to declaration
    attach_annotations(decl, annotations, annotation_count);

    return decl;
}
```

---

## Validation

After parsing, validate annotations:

```c
typedef struct {
    const char *name;
    int valid_on;           // Bitmask: ANNOT_FN | ANNOT_DEFINE | etc.
    bool allows_args;
    int min_args;
    int max_args;
} AnnotationSpec;

static AnnotationSpec known_annotations[] = {
    // Safety
    {"safe",       ANNOT_FN, false, 0, 0},
    {"unsafe",     ANNOT_FN, false, 0, 0},
    {"trusted",    ANNOT_FN, false, 0, 0},

    // Compiler hints
    {"inline",     ANNOT_FN, false, 0, 0},
    {"noinline",   ANNOT_FN, false, 0, 0},
    {"cold",       ANNOT_FN, false, 0, 0},
    {"hot",        ANNOT_FN, false, 0, 0},
    {"pure",       ANNOT_FN, false, 0, 0},

    // Deprecation
    {"deprecated", ANNOT_ALL, true, 0, 2},

    // Testing
    {"test",       ANNOT_FN, true, 0, 1},
    {"skip",       ANNOT_FN, true, 0, 1},
    {"timeout",    ANNOT_FN, true, 1, 1},

    // Documentation
    {"author",     ANNOT_ALL, true, 1, 1},
    {"since",      ANNOT_ALL, true, 1, 1},
    {"see",        ANNOT_ALL, true, 1, 1},

    {NULL, 0, false, 0, 0}  // Sentinel
};

void validate_annotations(Stmt *decl, DiagnosticList *diags) {
    Annotation **annotations = get_annotations(decl);
    int count = get_annotation_count(decl);
    int decl_kind = get_decl_kind(decl);

    for (int i = 0; i < count; i++) {
        Annotation *a = annotations[i];
        AnnotationSpec *spec = find_annotation_spec(a->name);

        if (spec == NULL) {
            // Unknown annotation
            warn(diags, a->line, a->column,
                 "unknown annotation '@%s'", a->name);
            continue;
        }

        if (!(spec->valid_on & decl_kind)) {
            // Invalid target
            error(diags, a->line, a->column,
                  "@%s is not valid on %s declarations",
                  a->name, decl_kind_name(decl_kind));
        }

        if (a->arg_count < spec->min_args || a->arg_count > spec->max_args) {
            error(diags, a->line, a->column,
                  "@%s expects %d-%d arguments, got %d",
                  a->name, spec->min_args, spec->max_args, a->arg_count);
        }

        // Check for duplicates
        for (int j = 0; j < i; j++) {
            if (strcmp(annotations[j]->name, a->name) == 0) {
                warn(diags, a->line, a->column,
                     "duplicate annotation '@%s'", a->name);
            }
        }
    }
}
```

---

## Implementation Phases

### Phase 1: Parser Support (This Spec)

**Scope:**
- [ ] Add `TOKEN_AT` to lexer
- [ ] Add `Annotation` struct and helpers to AST
- [ ] Parse annotations before declarations
- [ ] Store annotations in AST nodes
- [ ] Validate known annotations, warn on unknown
- [ ] Pass annotations through to codegen (no-op for now)
- [ ] LSP: show annotations in hover
- [ ] Test suite for parsing

**Files to modify:**
- `include/ast.h` — Add Annotation types
- `src/frontend/lexer.c` — Handle `@` token
- `src/frontend/parser/core.c` — Parse annotations
- `src/frontend/parser/statements.c` — Attach to declarations
- `src/backends/compiler/codegen.c` — Pass through (ignore)
- `src/backends/interpreter/eval.c` — Pass through (ignore)
- `src/lsp/handlers.c` — Show in hover

### Phase 2: Core Annotations
- [ ] `@deprecated` — compiler warnings on use
- [ ] `@inline` / `@noinline` — codegen hints for hemlockc

### Phase 3: Tricycle Integration
- [ ] `@safe` / `@unsafe` / `@trusted`
- [ ] Tricycle reads annotations from AST

### Phase 4: Testing Framework
- [ ] `@test` annotation
- [ ] `hemlock test` command
- [ ] `@skip`, `@timeout`

---

## Test Cases

### Basic Parsing

```hemlock
// tests/annotations/basic_parsing.hml

@inline
fn add(a: i32, b: i32): i32 {
    return a + b;
}

@deprecated
fn old_add(a: i32, b: i32): i32 {
    return a + b;
}

print("parsed");
```

```
// tests/annotations/basic_parsing.expected
parsed
```

### Multiple Annotations

```hemlock
// tests/annotations/multiple.hml

@deprecated("use new_math")
@inline
@pure
fn old_math(x: i32): i32 {
    return x * 2;
}

print("ok");
```

```
// tests/annotations/multiple.expected
ok
```

### Annotation Arguments

```hemlock
// tests/annotations/arguments.hml

@deprecated("use v2")
fn positional_arg() { }

@deprecated(message: "use v3", since: "1.0.0")
fn named_args() { }

@timeout(5000)
fn numeric_arg() { }

print("ok");
```

```
// tests/annotations/arguments.expected
ok
```

### Annotations on Different Declarations

```hemlock
// tests/annotations/declarations.hml

@deprecated
fn deprecated_fn() { }

@deprecated
define OldPerson {
    name: string
}

@deprecated
enum OldColor { RED, GREEN, BLUE }

@deprecated
let old_constant = 42;

print("ok");
```

```
// tests/annotations/declarations.expected
ok
```

### Unknown Annotation (Warning)

```hemlock
// tests/annotations/unknown.hml

@custom_marker
fn handler() {
    print("called");
}

handler();
```

```
// tests/annotations/unknown.expected
warning: unknown annotation '@custom_marker' at line 3
called
```

### Invalid Target (Error)

```hemlock
// tests/annotations/invalid_target.hml

@inline
let x = 42;

print(x);
```

```
// tests/annotations/invalid_target.expected
error: @inline is not valid on variable declarations at line 3
```

### Duplicate Annotation (Warning)

```hemlock
// tests/annotations/duplicate.hml

@inline
@inline
fn double_inline() { }

print("ok");
```

```
// tests/annotations/duplicate.expected
warning: duplicate annotation '@inline' at line 4
ok
```

### Annotations Preserved Across Compilation

```hemlock
// tests/annotations/codegen.hml

@pure
fn square(x: i32): i32 => x * x;

print(square(5));
```

```
// tests/annotations/codegen.expected
25
```

---

## Edge Cases

| Case | Behavior |
|------|----------|
| `@@inline fn f() {}` | Error: unexpected `@` |
| `@ inline fn f() {}` | Error: expected identifier after `@` |
| `@inline()` fn f() {}` | OK: empty args allowed |
| `@inline(,)` fn f() {}` | Error: expected argument |
| `@123 fn f() {}` | Error: expected identifier after `@` |
| `@if fn f() {}` | Warning: unknown annotation `if` |
| Annotation on expression | Error: annotations must precede declaration |
| `@test` on non-function | Error: @test is only valid on functions |

---

## Future Extensions (Not Phase 1)

These are explicitly **out of scope** for the initial implementation:

1. **Annotations on expressions:** `let f = @memoize fn(x) { }`
2. **Annotations on statements:** `@likely if (x) { }`
3. **Annotations on parameters:** `fn foo(@unused x: i32)`
4. **Runtime reflection:** `reflect.annotations(fn)`
5. **Annotation inheritance:** nested functions inheriting outer annotations
6. **Conditional annotations:** `@cfg(debug)` style

---

## References

- [Java Annotations](https://docs.oracle.com/javase/tutorial/java/annotations/)
- [Rust Attributes](https://doc.rust-lang.org/reference/attributes.html)
- [Python Decorators](https://peps.python.org/pep-0318/)
- [C++ Attributes](https://en.cppreference.com/w/cpp/language/attributes)

---

*Status: Ready for Implementation*

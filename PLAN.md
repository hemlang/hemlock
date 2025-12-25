# Result Type Implementation Plan for Hemlock

## Executive Summary

This plan outlines the implementation of a `Result<T, E>` type for Hemlock, providing explicit error handling similar to Rust's Result. Following Hemlock's "explicit over implicit" philosophy, Result gives programmers fine-grained control over error propagation without hiding complexity.

---

## 1. Syntax Design

### 1.1 Type Syntax
```hemlock
// Generic Result type annotation
let result: Result<i32, string> = Ok(42);
let error: Result<i32, string> = Err("division by zero");

// Function return type
fn divide(a: i32, b: i32): Result<i32, string> {
    if (b == 0) {
        return Err("division by zero");
    }
    return Ok(divi(a, b));
}
```

### 1.2 Constructor Functions
```hemlock
Ok(value)     // Creates a success Result containing value
Err(error)    // Creates an error Result containing error
```

### 1.3 Methods
```hemlock
result.is_ok()              // Returns true if Ok
result.is_err()             // Returns true if Err
result.unwrap()             // Returns value or panics if Err
result.unwrap_or(default)   // Returns value or default if Err
result.expect(msg)          // Returns value or panics with msg if Err
result.unwrap_err()         // Returns error or panics if Ok
result.ok()                 // Returns value or null if Err
result.err()                // Returns error or null if Ok
result.map(fn)              // Applies fn to Ok value, passes through Err
result.map_err(fn)          // Applies fn to Err value, passes through Ok
result.and_then(fn)         // Chains Result-returning operations
result.or_else(fn)          // Handles errors, can recover
```

### 1.4 Value Access
```hemlock
result.value   // Property to access the contained value (Ok or Err)
result.variant // Property returning "Ok" or "Err" string
```

---

## 2. AST Changes

### 2.1 New TypeKind (`include/ast.h`)
```c
typedef enum {
    // ... existing types ...
    TYPE_RESULT,        // Result<T, E> generic type
} TypeKind;
```

### 2.2 Extended Type struct (`include/ast.h`)
```c
struct Type {
    TypeKind kind;
    char *type_name;           // For TYPE_CUSTOM_OBJECT
    struct Type *element_type; // For TYPE_ARRAY, TYPE_RESULT (ok type)
    struct Type *error_type;   // NEW: For TYPE_RESULT (error type)
    int nullable;              // For nullable types (type?)
};
```

### 2.3 New Type Constructor (`src/frontend/ast.c`)
```c
Type* type_result(Type *ok_type, Type *err_type) {
    Type *type = malloc(sizeof(Type));
    type->kind = TYPE_RESULT;
    type->type_name = NULL;
    type->element_type = ok_type;   // Reuse for Ok type
    type->error_type = err_type;    // New field for Err type
    type->nullable = 0;
    return type;
}
```

---

## 3. Parser Changes

### 3.1 Lexer Token (`src/frontend/lexer.c`)
Add recognition for `Result` as a type keyword:
```c
// In keyword/type handling
{"Result", TOK_TYPE_RESULT},
```

### 3.2 parse_type Enhancement (`src/frontend/parser/expressions.c`)
```c
Type* parse_type(Parser *p) {
    // ... existing code ...

    // Check for Result<T, E> syntax
    if (p->current.type == TOK_TYPE_RESULT ||
        (p->current.type == TOK_IDENTIFIER && strcmp(p->current.lexeme, "Result") == 0)) {
        advance(p);
        consume(p, TOK_LESS, "Expect '<' after Result");

        Type *ok_type = parse_type(p);
        consume(p, TOK_COMMA, "Expect ',' between Result type parameters");
        Type *err_type = parse_type(p);
        consume(p, TOK_GREATER, "Expect '>' after Result type parameters");

        Type *type = type_new(TYPE_RESULT);
        type->element_type = ok_type;
        type->error_type = err_type;
        return type;
    }

    // ... rest of existing code ...
}
```

---

## 4. Interpreter Implementation

### 4.1 New ValueType (`include/interpreter.h`)
```c
typedef enum {
    // ... existing types ...
    VAL_RESULT,         // Result type (Ok or Err)
} ValueType;
```

### 4.2 Result Structure
```c
typedef struct {
    int is_ok;          // 1 for Ok, 0 for Err
    Value value;        // The contained value (success or error)
    Type *ok_type;      // Optional type constraint for Ok
    Type *err_type;     // Optional type constraint for Err
    int ref_count;      // Reference counting
} ResultHandle;
```

### 4.3 Value Union Extension
```c
typedef struct Value {
    ValueType type;
    union {
        // ... existing fields ...
        ResultHandle *as_result;  // NEW
    } as;
} Value;
```

### 4.4 Value Constructors (`src/backends/interpreter/values.c`)
```c
ResultHandle* result_new(int is_ok, Value value) {
    ResultHandle *result = malloc(sizeof(ResultHandle));
    result->is_ok = is_ok;
    result->value = value;
    result->ok_type = NULL;
    result->err_type = NULL;
    result->ref_count = 1;
    VALUE_RETAIN(value);
    return result;
}

Value val_result_ok(Value value) {
    Value v = {0};
    v.type = VAL_RESULT;
    v.as.as_result = result_new(1, value);
    return v;
}

Value val_result_err(Value value) {
    Value v = {0};
    v.type = VAL_RESULT;
    v.as.as_result = result_new(0, value);
    return v;
}
```

### 4.5 Builtin Functions
```c
// Register Ok and Err constructors
Value builtin_ok(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "Ok() requires exactly 1 argument");
        return val_null();
    }
    return val_result_ok(args[0]);
}

Value builtin_err(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "Err() requires exactly 1 argument");
        return val_null();
    }
    return val_result_err(args[0]);
}
```

### 4.6 Result Methods (new file: `src/backends/interpreter/io/result_methods.c`)
```c
Value call_result_method(ResultHandle *result, const char *method,
                         Value *args, int num_args, ExecutionContext *ctx) {

    if (strcmp(method, "is_ok") == 0) {
        return val_bool(result->is_ok);
    }

    if (strcmp(method, "is_err") == 0) {
        return val_bool(!result->is_ok);
    }

    if (strcmp(method, "unwrap") == 0) {
        if (!result->is_ok) {
            char *err_str = value_to_string(result->value);
            runtime_error(ctx, "Called unwrap() on an Err value: %s", err_str);
            free(err_str);
            return val_null();
        }
        VALUE_RETAIN(result->value);
        return result->value;
    }

    if (strcmp(method, "unwrap_or") == 0) {
        if (num_args != 1) {
            runtime_error(ctx, "unwrap_or() requires exactly 1 argument");
            return val_null();
        }
        if (result->is_ok) {
            VALUE_RETAIN(result->value);
            return result->value;
        }
        VALUE_RETAIN(args[0]);
        return args[0];
    }

    if (strcmp(method, "expect") == 0) {
        if (num_args != 1) {
            runtime_error(ctx, "expect() requires exactly 1 argument");
            return val_null();
        }
        if (!result->is_ok) {
            if (args[0].type != VAL_STRING) {
                runtime_error(ctx, "expect() argument must be a string");
                return val_null();
            }
            runtime_error(ctx, "%s", args[0].as.as_string->data);
            return val_null();
        }
        VALUE_RETAIN(result->value);
        return result->value;
    }

    if (strcmp(method, "unwrap_err") == 0) {
        if (result->is_ok) {
            char *ok_str = value_to_string(result->value);
            runtime_error(ctx, "Called unwrap_err() on an Ok value: %s", ok_str);
            free(ok_str);
            return val_null();
        }
        VALUE_RETAIN(result->value);
        return result->value;
    }

    if (strcmp(method, "ok") == 0) {
        if (result->is_ok) {
            VALUE_RETAIN(result->value);
            return result->value;
        }
        return val_null();
    }

    if (strcmp(method, "err") == 0) {
        if (!result->is_ok) {
            VALUE_RETAIN(result->value);
            return result->value;
        }
        return val_null();
    }

    // map, map_err, and_then, or_else implementations...

    runtime_error(ctx, "Unknown Result method: %s", method);
    return val_null();
}
```

---

## 5. Compiler Implementation

### 5.1 Runtime Value Type (`runtime/include/hemlock_value.h`)
```c
typedef enum {
    // ... existing types ...
    HML_VAL_RESULT,     // Result type
} HmlValueType;

typedef struct HmlResult {
    int is_ok;
    HmlValue value;
    int ref_count;
} HmlResult;

// In HmlValue union:
HmlResult *as_result;
```

### 5.2 Runtime Functions (`runtime/src/value.c`)
```c
HmlValue hml_result_ok(HmlValue value) {
    HmlResult *result = malloc(sizeof(HmlResult));
    result->is_ok = 1;
    result->value = value;
    result->ref_count = 1;
    hml_retain(&value);
    return (HmlValue){ .type = HML_VAL_RESULT, .as.as_result = result };
}

HmlValue hml_result_err(HmlValue value) {
    HmlResult *result = malloc(sizeof(HmlResult));
    result->is_ok = 0;
    result->value = value;
    result->ref_count = 1;
    hml_retain(&value);
    return (HmlValue){ .type = HML_VAL_RESULT, .as.as_result = result };
}

// Method implementations
HmlValue hml_result_is_ok(HmlValue result);
HmlValue hml_result_is_err(HmlValue result);
HmlValue hml_result_unwrap(HmlValue result);
HmlValue hml_result_unwrap_or(HmlValue result, HmlValue default_val);
HmlValue hml_result_expect(HmlValue result, HmlValue message);
// ... etc
```

### 5.3 Code Generation (`src/backends/compiler/codegen_expr.c`)
```c
// When generating builtin function calls
if (strcmp(func_name, "Ok") == 0) {
    char *arg = codegen_expr(ctx, call->args[0]);
    char *result = codegen_temp(ctx);
    codegen_writeln(ctx, "HmlValue %s = hml_result_ok(%s);", result, arg);
    codegen_writeln(ctx, "hml_release(&%s);", arg);
    free(arg);
    return result;
}

if (strcmp(func_name, "Err") == 0) {
    char *arg = codegen_expr(ctx, call->args[0]);
    char *result = codegen_temp(ctx);
    codegen_writeln(ctx, "HmlValue %s = hml_result_err(%s);", result, arg);
    codegen_writeln(ctx, "hml_release(&%s);", arg);
    free(arg);
    return result;
}
```

---

## 6. Standard Library Module

### `stdlib/result.hml`
```hemlock
// Result utility functions

// Combines multiple Results - returns first Err or array of all Ok values
export fn all(results: array) {
    let values = [];
    for (result in results) {
        if (result.is_err()) {
            return result;
        }
        values.push(result.unwrap());
    }
    return Ok(values);
}

// Returns first Ok value or last Err
export fn any(results: array) {
    let last_err = null;
    for (result in results) {
        if (result.is_ok()) {
            return result;
        }
        last_err = result;
    }
    return last_err;
}

// Try to execute a function, returning Result
export fn try_fn(fn_to_try, ...args) {
    try {
        let value = apply(fn_to_try, args);
        return Ok(value);
    } catch (e) {
        return Err(e);
    }
}

// Flatten nested Result
export fn flatten(result) {
    if (result.is_err()) {
        return result;
    }
    let inner = result.unwrap();
    if (typeof(inner) == "result") {
        return inner;
    }
    return result;
}
```

---

## 7. Pattern Matching (Future Enhancement)

### Match Expression Syntax (Phase 2)
```hemlock
let result = divide(10, 2);

match (result) {
    Ok(value): {
        print(`Result: ${value}`);
    }
    Err(msg): {
        print(`Error: ${msg}`);
    }
}
```

### If-Let Syntax (Phase 2)
```hemlock
if let Ok(value) = divide(10, 2) {
    print(`Got ${value}`);
} else {
    print("Division failed");
}
```

---

## 8. Parity Tests

### `tests/parity/language/result_basic.hml`
```hemlock
let ok_result = Ok(42);
let err_result = Err("error message");

print(ok_result.is_ok());
print(ok_result.is_err());
print(err_result.is_ok());
print(err_result.is_err());
print(ok_result.variant);
print(err_result.variant);
print("done");
```

**Expected output:**
```
true
false
false
true
Ok
Err
done
```

### `tests/parity/language/result_unwrap.hml`
```hemlock
let ok = Ok(100);
let err = Err("failed");

print(ok.unwrap());
print(ok.unwrap_or(0));
print(err.unwrap_or(0));
print(ok.ok());
print(err.ok());
print(ok.err());
print(err.err());
print("done");
```

**Expected output:**
```
100
100
0
100
null
null
failed
done
```

### `tests/parity/language/result_functions.hml`
```hemlock
fn safe_divide(a: i32, b: i32): Result<i32, string> {
    if (b == 0) {
        return Err("division by zero");
    }
    return Ok(divi(a, b));
}

let r1 = safe_divide(10, 2);
let r2 = safe_divide(10, 0);

print(r1.is_ok());
print(r1.unwrap());
print(r2.is_err());
print(r2.unwrap_err());
print("done");
```

**Expected output:**
```
true
5
true
division by zero
done
```

### `tests/parity/language/result_map.hml`
```hemlock
let ok = Ok(5);
let err = Err("error");

let doubled = ok.map(fn(x) { return x * 2; });
print(doubled.unwrap());

let upper_err = err.map_err(fn(e) { return e.to_upper(); });
print(upper_err.unwrap_err());

let passed = err.map(fn(x) { return x * 2; });
print(passed.is_err());
print("done");
```

**Expected output:**
```
10
ERROR
true
done
```

---

## 9. Implementation Order

### Phase 1: Core Implementation
1. Add `TYPE_RESULT` to TypeKind enum in `include/ast.h`
2. Extend Type struct with `error_type` field
3. Implement `parse_type` for `Result<T, E>` syntax
4. Add `VAL_RESULT` to interpreter ValueType
5. Implement `ResultHandle` structure with reference counting
6. Add value constructors (`val_result_ok`, `val_result_err`)
7. Implement builtin `Ok()` and `Err()` functions
8. Add basic methods (`is_ok`, `is_err`, `unwrap`, `unwrap_or`)

### Phase 2: Full Method Support
1. Implement remaining methods (`expect`, `ok`, `err`, `unwrap_err`)
2. Implement `map`, `map_err`, `and_then`, `or_else`
3. Add property access (`value`, `variant`)
4. Integrate with `typeof()` builtin (returns `"result"`)
5. Add `print_value` support for Result

### Phase 3: Compiler Backend
1. Add `HML_VAL_RESULT` to runtime
2. Implement runtime Result structure
3. Add runtime functions for all operations
4. Implement code generation for `Ok`/`Err`
5. Implement code generation for method calls
6. Add reference counting support

### Phase 4: Testing & Polish
1. Create all parity tests
2. Run `make parity` and fix discrepancies
3. Create `stdlib/result` module
4. Write documentation
5. Add examples

---

## 10. Critical Files to Modify

| File | Changes |
|------|---------|
| `include/ast.h` | Add `TYPE_RESULT` to TypeKind, add `error_type` to Type struct |
| `src/frontend/ast.c` | Add `type_result()` constructor |
| `src/frontend/parser/expressions.c` | Extend `parse_type()` for `Result<T, E>` |
| `include/interpreter.h` | Add `VAL_RESULT`, define `ResultHandle` struct |
| `src/backends/interpreter/values.c` | Add Result value constructors |
| `src/backends/interpreter/builtins/` | Register `Ok()` and `Err()` builtins |
| `src/backends/interpreter/io/` | New `result_methods.c` for method handling |
| `src/backends/interpreter/runtime/expressions.c` | Handle Result in property/method access |
| `runtime/include/hemlock_value.h` | Add `HML_VAL_RESULT`, `HmlResult` struct |
| `runtime/src/value.c` | Runtime Result functions |
| `src/backends/compiler/codegen_expr.c` | Code generation for Result operations |

---

## 11. Design Decisions

### Why not extend Enums?
Hemlock's enums are simple integer variants. Result requires a generic tagged union with associated data, which is a different paradigm.

### Why methods instead of pattern matching initially?
Method-based access follows Hemlock's existing patterns (string methods, array methods) and is simpler to implement. Pattern matching can be added later.

### Why not use exceptions?
Exceptions (try/catch/throw) remain for truly exceptional cases. Result provides explicit, type-checked error handling for expected failure cases, making error handling visible and composable.

### Type checking approach
Result type annotations are checked at runtime (consistent with Hemlock's philosophy). The generic parameters `<T, E>` provide documentation and runtime validation, not compile-time checking.

---

## 12. Breaking Changes

**None.** This implementation is purely additive:
- `Ok` and `Err` are new builtins
- `Result` is a new type keyword
- No existing functionality is modified

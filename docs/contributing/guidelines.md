# Contributing to Hemlock

Thank you for your interest in contributing to Hemlock! This guide will help you understand how to contribute effectively while maintaining the language's design philosophy and code quality.

---

## Table of Contents

- [Before You Start](#before-you-start)
- [Contribution Workflow](#contribution-workflow)
- [Code Style Guidelines](#code-style-guidelines)
- [What to Contribute](#what-to-contribute)
- [What NOT to Contribute](#what-not-to-contribute)
- [Common Patterns](#common-patterns)
- [Adding New Features](#adding-new-features)
- [Code Review Process](#code-review-process)

---

## Before You Start

### Required Reading

Before contributing, please read these documents in order:

1. **`/home/user/hemlock/docs/design/philosophy.md`** - Understand Hemlock's core principles
2. **`/home/user/hemlock/docs/design/implementation.md`** - Learn the codebase structure
3. **`/home/user/hemlock/docs/contributing/testing.md`** - Understand testing requirements
4. **This document** - Learn contribution guidelines

### Prerequisites

**Required knowledge:**
- C programming (pointers, memory management, structs)
- Compiler/interpreter basics (lexing, parsing, AST)
- Git and GitHub workflow
- Unix/Linux command line

**Required tools:**
- GCC or Clang compiler
- Make build system
- Git version control
- Valgrind (for memory leak detection)
- Basic text editor or IDE

### Communication Channels

**Where to ask questions:**
- GitHub Issues - Bug reports and feature requests
- GitHub Discussions - General questions and design discussions
- Pull Request comments - Specific code feedback

---

## Contribution Workflow

### 1. Find or Create an Issue

**Before writing code:**
- Check if an issue exists for your contribution
- If not, create one describing what you want to do
- Wait for maintainer feedback before starting large changes
- Small bug fixes can skip this step

**Good issue descriptions include:**
- Problem statement (what's broken or missing)
- Proposed solution (how you plan to fix it)
- Examples (code snippets showing the issue)
- Rationale (why this change aligns with Hemlock's philosophy)

### 2. Fork and Clone

```bash
# Fork the repository on GitHub first, then:
git clone https://github.com/YOUR_USERNAME/hemlock.git
cd hemlock
git checkout -b feature/your-feature-name
```

### 3. Make Your Changes

Follow these guidelines:
- Write tests first (TDD approach)
- Implement the feature
- Ensure all tests pass
- Check for memory leaks
- Update documentation

### 4. Test Your Changes

```bash
# Run the full test suite
make test

# Run specific test category
./tests/run_tests.sh tests/category/

# Check for memory leaks
valgrind ./hemlock tests/your_test.hml

# Build and test
make clean && make && make test
```

### 5. Commit Your Changes

**Good commit messages:**
```
Add bitwise operators for integer types

- Implement &, |, ^, <<, >>, ~ operators
- Add type checking to ensure integer-only operations
- Update operator precedence table
- Add comprehensive tests for all operators

Closes #42
```

**Commit message format:**
- First line: Brief summary (50 chars max)
- Blank line
- Detailed explanation (wrap at 72 chars)
- Reference issue numbers

### 6. Submit a Pull Request

**Before submitting:**
- Rebase on latest main branch
- Ensure all tests pass
- Run valgrind to check for leaks
- Update CLAUDE.md if adding user-facing features

**Pull request description should include:**
- What problem this solves
- How it solves it
- Breaking changes (if any)
- Examples of new syntax or behavior
- Test coverage summary

---

## Code Style Guidelines

### C Code Style

**Formatting:**
```c
// Indent with 4 spaces (no tabs)
// K&R brace style for functions
void function_name(int arg1, char *arg2)
{
    if (condition) {
        // Brace on same line for control structures
        do_something();
    }
}

// Line length: 100 characters max
// Use spaces around operators
int result = (a + b) * c;

// Pointer asterisk with type
char *string;   // Good
char* string;   // Avoid
char * string;  // Avoid
```

**Naming conventions:**
```c
// Functions: lowercase_with_underscores
void eval_expression(ASTNode *node);

// Types: PascalCase
typedef struct Value Value;
typedef enum ValueType ValueType;

// Constants: UPPERCASE_WITH_UNDERSCORES
#define MAX_BUFFER_SIZE 4096

// Variables: lowercase_with_underscores
int item_count;
Value *current_value;

// Enums: TYPE_PREFIX_NAME
typedef enum {
    TYPE_I32,
    TYPE_STRING,
    TYPE_OBJECT
} ValueType;
```

**Comments:**
```c
// Single-line comments for brief explanations
// Use complete sentences with proper capitalization

/*
 * Multi-line comments for longer explanations
 * Align asterisks for readability
 */

/**
 * Function documentation comment
 * @param node - AST node to evaluate
 * @return Evaluated value
 */
Value eval_expr(ASTNode *node);
```

**Error handling:**
```c
// Check all malloc calls
char *buffer = malloc(size);
if (!buffer) {
    fprintf(stderr, "Error: Out of memory\n");
    exit(1);
}

// Provide context in error messages
if (file == NULL) {
    fprintf(stderr, "Error: Failed to open '%s': %s\n",
            filename, strerror(errno));
    exit(1);
}

// Use meaningful error messages
// Bad: "Error: Invalid value"
// Good: "Error: Expected integer, got string"
```

**Memory management:**
```c
// Always free what you allocate
Value *val = value_create_i32(42);
// ... use val
value_free(val);

// Set pointers to NULL after freeing (prevents double-free)
free(ptr);
ptr = NULL;

// Document ownership in comments
// This function takes ownership of 'value' and will free it
void store_value(Value *value);

// This function does NOT take ownership (caller must free)
Value *get_value(void);
```

### Code Organization

**File structure:**
```c
// 1. Includes (system headers first, then local)
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
#include "values.h"

// 2. Constants and macros
#define INITIAL_CAPACITY 16

// 3. Type definitions
typedef struct Foo Foo;

// 4. Static function declarations (internal helpers)
static void helper_function(void);

// 5. Public function implementations
void public_api_function(void)
{
    // Implementation
}

// 6. Static function implementations
static void helper_function(void)
{
    // Implementation
}
```

**Header files:**
```c
// Use header guards
#ifndef HEMLOCK_MODULE_H
#define HEMLOCK_MODULE_H

// Forward declarations
typedef struct Value Value;

// Public API only in headers
void public_function(Value *val);

// Document parameters and return values
/**
 * Evaluates an expression AST node
 * @param node - The AST node to evaluate
 * @param env - The current environment
 * @return The result value
 */
Value *eval_expr(ASTNode *node, Environment *env);

#endif // HEMLOCK_MODULE_H
```

---

## What to Contribute

### ✅ Encouraged Contributions

**Bug fixes:**
- Memory leaks
- Segmentation faults
- Incorrect behavior
- Error message improvements

**Documentation:**
- Code comments
- API documentation
- User guides and tutorials
- Example programs
- Test case documentation

**Tests:**
- Additional test cases for existing features
- Edge case coverage
- Regression tests for fixed bugs
- Performance benchmarks

**Small feature additions:**
- New built-in functions (if they fit the philosophy)
- String/array methods
- Utility functions
- Error handling improvements

**Performance improvements:**
- Faster algorithms (without changing semantics)
- Memory usage reduction
- Benchmark suite
- Profiling tools

**Tooling:**
- Editor syntax highlighting
- Language server protocol (LSP)
- Debugger integration
- Build system improvements

### 🤔 Discuss First

**Major features:**
- New language constructs
- Type system changes
- Syntax additions
- Concurrency primitives

**How to discuss:**
1. Open a GitHub issue or discussion
2. Describe the feature and rationale
3. Show example code
4. Explain how it fits Hemlock's philosophy
5. Wait for maintainer feedback
6. Iterate on design before implementing

---

## What NOT to Contribute

### ❌ Discouraged Contributions

**Don't add features that:**
- Hide complexity from the user
- Make behavior implicit or magical
- Break existing semantics or syntax
- Add garbage collection or automatic memory management
- Violate the "explicit over implicit" principle

**Examples of rejected contributions:**

**1. Automatic semicolon insertion**
```hemlock
// BAD: This would be rejected
let x = 5  // No semicolon
let y = 10 // No semicolon
```
Why: Makes syntax ambiguous, hides errors

**2. RAII/destructors**
```hemlock
// BAD: This would be rejected
let f = open("file.txt");
// File automatically closed at end of scope
```
Why: Hides when resources are released, not explicit

**3. Implicit type coercion that loses data**
```hemlock
// BAD: This would be rejected
let x: i32 = 3.14;  // Silently truncates to 3
```
Why: Data loss should be explicit, not silent

**4. Garbage collection**
```c
// BAD: This would be rejected
void *gc_malloc(size_t size) {
    // Track allocation for automatic cleanup
}
```
Why: Hides memory management, unpredictable performance

**5. Complex macro system**
```hemlock
// BAD: This would be rejected
macro repeat($n, $block) {
    for (let i = 0; i < $n; i++) $block
}
```
Why: Too much magic, makes code hard to reason about

### Common Rejection Reasons

**"This is too implicit"**
- Solution: Make the behavior explicit and document it

**"This hides complexity"**
- Solution: Expose the complexity but make it ergonomic

**"This breaks existing code"**
- Solution: Find a non-breaking alternative or discuss versioning

**"This doesn't fit Hemlock's philosophy"**
- Solution: Re-read philosophy.md and reconsider the approach

---

## Common Patterns

### Error Handling Pattern

```c
// Use this pattern for recoverable errors in Hemlock code
Value *divide(Value *a, Value *b)
{
    // Check preconditions
    if (b->type != TYPE_I32) {
        // Return error value or throw exception
        return create_error("Expected integer divisor");
    }

    if (b->i32_value == 0) {
        return create_error("Division by zero");
    }

    // Perform operation
    return value_create_i32(a->i32_value / b->i32_value);
}
```

### Memory Management Pattern

```c
// Pattern: Allocate, use, free
void process_data(void)
{
    // Allocate
    Buffer *buf = create_buffer(1024);
    char *str = malloc(256);

    // Use
    if (buf && str) {
        // ... do work
    }

    // Free (in reverse order of allocation)
    free(str);
    free_buffer(buf);
}
```

### Value Creation Pattern

```c
// Create values using constructors
Value *create_integer(int32_t n)
{
    Value *val = malloc(sizeof(Value));
    if (!val) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    val->type = TYPE_I32;
    val->i32_value = n;
    return val;
}
```

### Type Checking Pattern

```c
// Check types before operations
Value *add_values(Value *a, Value *b)
{
    // Type checking
    if (a->type != TYPE_I32 || b->type != TYPE_I32) {
        return create_error("Type mismatch");
    }

    // Safe to proceed
    return value_create_i32(a->i32_value + b->i32_value);
}
```

### String Building Pattern

```c
// Build strings efficiently
void build_error_message(char *buffer, size_t size, const char *detail)
{
    snprintf(buffer, size, "Error: %s (line %d)", detail, line_number);
}
```

---

## Adding New Features

### Feature Addition Checklist

When adding a new feature, follow these steps:

#### 1. Design Phase

- [ ] Read philosophy.md to ensure alignment
- [ ] Create GitHub issue describing the feature
- [ ] Get maintainer approval for design
- [ ] Write specification (syntax, semantics, examples)
- [ ] Consider edge cases and error conditions

#### 2. Implementation Phase

**If adding a language construct:**

- [ ] Add token type to `lexer.h` (if needed)
- [ ] Add lexer rule in `lexer.c` (if needed)
- [ ] Add AST node type in `ast.h`
- [ ] Add AST constructor in `ast.c`
- [ ] Add parser rule in `parser.c`
- [ ] Add runtime behavior in `runtime.c` or appropriate module
- [ ] Handle cleanup in AST free functions

**If adding a built-in function:**

- [ ] Add function implementation in `builtins.c`
- [ ] Register function in `register_builtins()`
- [ ] Handle all parameter type combinations
- [ ] Return appropriate error values
- [ ] Document parameters and return type

**If adding a value type:**

- [ ] Add type enum in `values.h`
- [ ] Add field to Value union
- [ ] Add constructor in `values.c`
- [ ] Add to `value_free()` for cleanup
- [ ] Add to `value_copy()` for copying
- [ ] Add to `value_to_string()` for printing
- [ ] Add type promotion rules if numeric

#### 3. Testing Phase

- [ ] Write test cases (see testing.md)
- [ ] Test success cases
- [ ] Test error cases
- [ ] Test edge cases
- [ ] Run full test suite (`make test`)
- [ ] Check for memory leaks with valgrind
- [ ] Test on multiple platforms (if possible)

#### 4. Documentation Phase

- [ ] Update CLAUDE.md with user-facing documentation
- [ ] Add code comments explaining implementation
- [ ] Create examples in `examples/`
- [ ] Update relevant docs/ files
- [ ] Document any breaking changes

#### 5. Submission Phase

- [ ] Clean up debug code and comments
- [ ] Verify code style compliance
- [ ] Rebase on latest main
- [ ] Create pull request with detailed description
- [ ] Respond to code review feedback

### Example: Adding a New Operator

Let's walk through adding the modulo operator `%` as an example:

**1. Lexer (lexer.c):**
```c
// Add to switch statement in get_next_token()
case '%':
    return create_token(TOKEN_PERCENT, "%", line);
```

**2. Lexer header (lexer.h):**
```c
typedef enum {
    // ... existing tokens
    TOKEN_PERCENT,
    // ...
} HmlTokenType;
```

**3. AST (ast.h):**
```c
typedef enum {
    // ... existing operators
    OP_MOD,
    // ...
} BinaryOp;
```

**4. Parser (parser.c):**
```c
// Add to parse_multiplicative() or appropriate precedence level
if (match(TOKEN_PERCENT)) {
    BinaryOp op = OP_MOD;
    ASTNode *right = parse_unary();
    left = create_binary_op_node(op, left, right);
}
```

**5. Runtime (runtime.c):**
```c
// Add to eval_binary_op()
case OP_MOD:
    // Type checking
    if (left->type == TYPE_I32 && right->type == TYPE_I32) {
        if (right->i32_value == 0) {
            fprintf(stderr, "Error: Modulo by zero\n");
            exit(1);
        }
        return value_create_i32(left->i32_value % right->i32_value);
    }
    // ... handle other type combinations
    break;
```

**6. Tests (tests/operators/modulo.hml):**
```hemlock
// Basic modulo
print(10 % 3);  // Expect: 2

// Negative modulo
print(-10 % 3); // Expect: -1

// Error case (should fail)
// print(10 % 0);  // Division by zero
```

**7. Documentation (CLAUDE.md):**
```markdown
### Arithmetic Operators
- `+` - Addition
- `-` - Subtraction
- `*` - Multiplication
- `/` - Division
- `%` - Modulo (remainder)
```

---

## Code Review Process

### What Reviewers Look For

**1. Correctness**
- Does the code do what it claims?
- Are edge cases handled?
- Are there memory leaks?
- Are errors handled properly?

**2. Philosophy Alignment**
- Does this fit Hemlock's design principles?
- Is it explicit or implicit?
- Does it hide complexity?

**3. Code Quality**
- Is the code readable and maintainable?
- Are variable names descriptive?
- Are functions reasonably sized?
- Is there adequate documentation?

**4. Testing**
- Are there sufficient test cases?
- Do tests cover success and failure paths?
- Are edge cases tested?

**5. Documentation**
- Is user-facing documentation updated?
- Are code comments clear?
- Are examples provided?

### Responding to Feedback

**Do:**
- Thank reviewers for their time
- Ask clarifying questions if you don't understand
- Explain your reasoning if you disagree
- Make requested changes promptly
- Update the PR description if scope changes

**Don't:**
- Take criticism personally
- Argue defensively
- Ignore feedback
- Force-push over review comments (unless rebasing)
- Add unrelated changes to the PR

### Getting Your PR Merged

**Requirements for merge:**
- [ ] All tests pass
- [ ] No memory leaks (valgrind clean)
- [ ] Code review approval from maintainer
- [ ] Documentation updated
- [ ] Follows code style guidelines
- [ ] Aligns with Hemlock's philosophy

**Timeline:**
- Small PRs (bug fixes): Usually reviewed within a few days
- Medium PRs (new features): May take 1-2 weeks
- Large PRs (major changes): Requires extensive discussion

---

## Additional Resources

### Learning Resources

**Understanding interpreters:**
- "Crafting Interpreters" by Robert Nystrom
- "Writing An Interpreter In Go" by Thorsten Ball
- "Modern Compiler Implementation in C" by Andrew Appel

**C programming:**
- "The C Programming Language" by K&R
- "Expert C Programming" by Peter van der Linden
- "C Interfaces and Implementations" by David Hanson

**Memory management:**
- Valgrind documentation
- "Understanding and Using C Pointers" by Richard Reese

### Useful Commands

```bash
# Build with debug symbols
make clean && make CFLAGS="-g -O0"

# Run with valgrind
valgrind --leak-check=full ./hemlock script.hml

# Run specific test category
./tests/run_tests.sh tests/strings/

# Generate tags file for code navigation
ctags -R .

# Find all TODOs and FIXMEs
grep -rn "TODO\|FIXME" src/ include/
```

---

## Questions?

If you have questions about contributing:

1. Check the documentation in `docs/`
2. Search existing GitHub issues
3. Ask in GitHub Discussions
4. Open a new issue with your question

**Thank you for contributing to Hemlock!**

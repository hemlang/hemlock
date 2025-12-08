/*
 * VM Feature Parity Test Suite
 *
 * Tests VM against expected outputs to track feature implementation progress.
 * Run with: ./vm_parity_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../../include/ast.h"
#include "../../include/interpreter.h"
#include "../../src/vm/vm.h"
#include "../../src/vm/compiler.h"
#include "../../src/vm/chunk.h"
#include "../../src/vm/bytecode.h"

// Forward declaration
extern Chunk* vm_compile_ast(Stmt **statements, int count, const char *source_file);

// Test result tracking
typedef struct {
    int passed;
    int failed;
    int skipped;
} TestStats;

static TestStats stats = {0, 0, 0};

// Colors for output
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[0;33m"
#define RESET  "\033[0m"

// Capture print output
static char captured_output[4096];
static int capture_pos = 0;
static bool capturing = false;

// Hook to capture print output (simplified - real impl would redirect stdout)
void start_capture(void) {
    capturing = true;
    capture_pos = 0;
    captured_output[0] = '\0';
}

void stop_capture(void) {
    capturing = false;
}

// Test helper: run VM and check global value
typedef struct {
    const char *name;
    Stmt **stmts;
    int stmt_count;
    const char *check_var;
    Value expected;
    bool expect_error;
} VMTest;

static bool values_match(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NULL: return true;
        case VAL_BOOL: return a.as.as_bool == b.as.as_bool;
        case VAL_I32: return a.as.as_i32 == b.as.as_i32;
        case VAL_I64: return a.as.as_i64 == b.as.as_i64;
        case VAL_F64: return a.as.as_f64 == b.as.as_f64;
        case VAL_STRING:
            return strcmp(a.as.as_string->data, b.as.as_string->data) == 0;
        default: return false;
    }
}

static void print_value_brief(Value v) {
    switch (v.type) {
        case VAL_NULL: printf("null"); break;
        case VAL_BOOL: printf("%s", v.as.as_bool ? "true" : "false"); break;
        case VAL_I32: printf("%d", v.as.as_i32); break;
        case VAL_I64: printf("%lld", (long long)v.as.as_i64); break;
        case VAL_F64: printf("%g", v.as.as_f64); break;
        case VAL_STRING: printf("\"%s\"", v.as.as_string->data); break;
        default: printf("<type %d>", v.type);
    }
}

static bool run_vm_test(const char *name, Stmt **stmts, int count,
                        const char *check_var, Value expected, bool expect_error) {
    printf("  %-40s ", name);

    Chunk *chunk = vm_compile_ast(stmts, count, "test");
    if (!chunk) {
        if (expect_error) {
            printf(GREEN "PASS" RESET " (expected compile error)\n");
            stats.passed++;
            return true;
        }
        printf(RED "FAIL" RESET " (compile error)\n");
        stats.failed++;
        return false;
    }

    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    VMResult result = vm_run(vm, chunk);

    if (result != VM_OK) {
        if (expect_error) {
            printf(GREEN "PASS" RESET " (expected runtime error)\n");
            vm_free(vm);
            chunk_free(chunk);
            stats.passed++;
            return true;
        }
        printf(RED "FAIL" RESET " (runtime error: %s)\n", vm_get_error(vm));
        vm_free(vm);
        chunk_free(chunk);
        stats.failed++;
        return false;
    }

    if (expect_error) {
        printf(RED "FAIL" RESET " (expected error but succeeded)\n");
        vm_free(vm);
        chunk_free(chunk);
        stats.failed++;
        return false;
    }

    // Check result
    Value actual;
    if (!vm_get_global(vm, check_var, &actual)) {
        printf(RED "FAIL" RESET " (variable '%s' not found)\n", check_var);
        vm_free(vm);
        chunk_free(chunk);
        stats.failed++;
        return false;
    }

    if (values_match(actual, expected)) {
        printf(GREEN "PASS" RESET "\n");
        vm_free(vm);
        chunk_free(chunk);
        stats.passed++;
        return true;
    }

    printf(RED "FAIL" RESET " (expected ");
    print_value_brief(expected);
    printf(", got ");
    print_value_brief(actual);
    printf(")\n");

    vm_free(vm);
    chunk_free(chunk);
    stats.failed++;
    return false;
}

// ========== Test Categories ==========

static void test_literals(void) {
    printf("\n" YELLOW "=== Literals ===" RESET "\n");

    // Integer literal
    {
        Stmt *s = stmt_let("x", expr_number(42));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 42};
        run_vm_test("integer literal", stmts, 1, "x", expected, false);
    }

    // Float literal
    {
        Stmt *s = stmt_let("x", expr_number_float(3.14));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_F64, .as.as_f64 = 3.14};
        run_vm_test("float literal", stmts, 1, "x", expected, false);
    }

    // Boolean true
    {
        Stmt *s = stmt_let("x", expr_bool(1));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("boolean true", stmts, 1, "x", expected, false);
    }

    // Boolean false
    {
        Stmt *s = stmt_let("x", expr_bool(0));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = false};
        run_vm_test("boolean false", stmts, 1, "x", expected, false);
    }

    // Null
    {
        Stmt *s = stmt_let("x", expr_null());
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_NULL};
        run_vm_test("null literal", stmts, 1, "x", expected, false);
    }

    // String
    {
        Stmt *s = stmt_let("x", expr_string("hello"));
        Stmt *stmts[] = {s};
        Value expected = val_string("hello");
        run_vm_test("string literal", stmts, 1, "x", expected, false);
    }
}

static void test_arithmetic(void) {
    printf("\n" YELLOW "=== Arithmetic ===" RESET "\n");

    // Addition
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(10), OP_ADD, expr_number(20)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 30};
        run_vm_test("addition (10 + 20)", stmts, 1, "x", expected, false);
    }

    // Subtraction
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(50), OP_SUB, expr_number(30)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 20};
        run_vm_test("subtraction (50 - 30)", stmts, 1, "x", expected, false);
    }

    // Multiplication
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(6), OP_MUL, expr_number(7)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 42};
        run_vm_test("multiplication (6 * 7)", stmts, 1, "x", expected, false);
    }

    // Division
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(100), OP_DIV, expr_number(4)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_F64, .as.as_f64 = 25.0};
        run_vm_test("division (100 / 4)", stmts, 1, "x", expected, false);
    }

    // Modulo
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(17), OP_MOD, expr_number(5)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 2};
        run_vm_test("modulo (17 % 5)", stmts, 1, "x", expected, false);
    }

    // Complex expression
    {
        // (10 + 20) * 3 - 5
        Expr *add = expr_binary(expr_number(10), OP_ADD, expr_number(20));
        Expr *mul = expr_binary(add, OP_MUL, expr_number(3));
        Expr *sub = expr_binary(mul, OP_SUB, expr_number(5));
        Stmt *s = stmt_let("x", sub);
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 85};
        run_vm_test("complex ((10+20)*3-5)", stmts, 1, "x", expected, false);
    }

    // Negation
    {
        Stmt *s = stmt_let("x", expr_unary(UNARY_NEGATE, expr_number(42)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = -42};
        run_vm_test("negation (-42)", stmts, 1, "x", expected, false);
    }
}

static void test_comparisons(void) {
    printf("\n" YELLOW "=== Comparisons ===" RESET "\n");

    // Less than (true)
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(5), OP_LESS, expr_number(10)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("less than (5 < 10)", stmts, 1, "x", expected, false);
    }

    // Less than (false)
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(10), OP_LESS, expr_number(5)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = false};
        run_vm_test("less than (10 < 5)", stmts, 1, "x", expected, false);
    }

    // Greater than
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(10), OP_GREATER, expr_number(5)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("greater than (10 > 5)", stmts, 1, "x", expected, false);
    }

    // Equal
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(42), OP_EQUAL, expr_number(42)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("equal (42 == 42)", stmts, 1, "x", expected, false);
    }

    // Not equal
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(1), OP_NOT_EQUAL, expr_number(2)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("not equal (1 != 2)", stmts, 1, "x", expected, false);
    }

    // Less or equal
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(5), OP_LESS_EQUAL, expr_number(5)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("less or equal (5 <= 5)", stmts, 1, "x", expected, false);
    }
}

static void test_logical(void) {
    printf("\n" YELLOW "=== Logical ===" RESET "\n");

    // NOT true
    {
        Stmt *s = stmt_let("x", expr_unary(UNARY_NOT, expr_bool(1)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = false};
        run_vm_test("not true (!true)", stmts, 1, "x", expected, false);
    }

    // NOT false
    {
        Stmt *s = stmt_let("x", expr_unary(UNARY_NOT, expr_bool(0)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("not false (!false)", stmts, 1, "x", expected, false);
    }

    // AND (true && true)
    {
        Stmt *s = stmt_let("x", expr_binary(expr_bool(1), OP_AND, expr_bool(1)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("and (true && true)", stmts, 1, "x", expected, false);
    }

    // AND (true && false)
    {
        Stmt *s = stmt_let("x", expr_binary(expr_bool(1), OP_AND, expr_bool(0)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = false};
        run_vm_test("and (true && false)", stmts, 1, "x", expected, false);
    }

    // OR (false || true)
    {
        Stmt *s = stmt_let("x", expr_binary(expr_bool(0), OP_OR, expr_bool(1)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_BOOL, .as.as_bool = true};
        run_vm_test("or (false || true)", stmts, 1, "x", expected, false);
    }
}

static void test_strings(void) {
    printf("\n" YELLOW "=== Strings ===" RESET "\n");

    // Concatenation
    {
        Stmt *s = stmt_let("x", expr_binary(expr_string("Hello"), OP_ADD, expr_string(" World")));
        Stmt *stmts[] = {s};
        Value expected = val_string("Hello World");
        run_vm_test("concatenation", stmts, 1, "x", expected, false);
    }

    // Multiple concatenation
    {
        Expr *a = expr_binary(expr_string("a"), OP_ADD, expr_string("b"));
        Expr *b = expr_binary(a, OP_ADD, expr_string("c"));
        Stmt *s = stmt_let("x", b);
        Stmt *stmts[] = {s};
        Value expected = val_string("abc");
        run_vm_test("multi concat (a+b+c)", stmts, 1, "x", expected, false);
    }
}

static void test_variables(void) {
    printf("\n" YELLOW "=== Variables ===" RESET "\n");

    // Variable reference
    {
        Stmt *s1 = stmt_let("a", expr_number(10));
        Stmt *s2 = stmt_let("b", expr_ident("a"));
        Stmt *stmts[] = {s1, s2};
        Value expected = {.type = VAL_I32, .as.as_i32 = 10};
        run_vm_test("variable reference", stmts, 2, "b", expected, false);
    }

    // Variable in expression
    {
        Stmt *s1 = stmt_let("a", expr_number(5));
        Stmt *s2 = stmt_let("b", expr_number(3));
        Stmt *s3 = stmt_let("c", expr_binary(expr_ident("a"), OP_ADD, expr_ident("b")));
        Stmt *stmts[] = {s1, s2, s3};
        Value expected = {.type = VAL_I32, .as.as_i32 = 8};
        run_vm_test("vars in expression (a+b)", stmts, 3, "c", expected, false);
    }

    // Assignment
    {
        Stmt *s1 = stmt_let("x", expr_number(10));
        Stmt *s2 = stmt_expr(expr_assign("x", expr_number(20)));
        Stmt *stmts[] = {s1, s2};
        Value expected = {.type = VAL_I32, .as.as_i32 = 20};
        run_vm_test("assignment (x = 20)", stmts, 2, "x", expected, false);
    }
}

static void test_bitwise(void) {
    printf("\n" YELLOW "=== Bitwise ===" RESET "\n");

    // AND
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(0xFF), OP_BIT_AND, expr_number(0x0F)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 0x0F};
        run_vm_test("bitwise AND (0xFF & 0x0F)", stmts, 1, "x", expected, false);
    }

    // OR
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(0xF0), OP_BIT_OR, expr_number(0x0F)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 0xFF};
        run_vm_test("bitwise OR (0xF0 | 0x0F)", stmts, 1, "x", expected, false);
    }

    // XOR
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(0xFF), OP_BIT_XOR, expr_number(0x0F)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 0xF0};
        run_vm_test("bitwise XOR (0xFF ^ 0x0F)", stmts, 1, "x", expected, false);
    }

    // Left shift
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(1), OP_BIT_LSHIFT, expr_number(4)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 16};
        run_vm_test("left shift (1 << 4)", stmts, 1, "x", expected, false);
    }

    // Right shift
    {
        Stmt *s = stmt_let("x", expr_binary(expr_number(16), OP_BIT_RSHIFT, expr_number(2)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = 4};
        run_vm_test("right shift (16 >> 2)", stmts, 1, "x", expected, false);
    }

    // NOT
    {
        Stmt *s = stmt_let("x", expr_unary(UNARY_BIT_NOT, expr_number(0)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I64, .as.as_i64 = -1};
        run_vm_test("bitwise NOT (~0)", stmts, 1, "x", expected, false);
    }
}

static void test_ternary(void) {
    printf("\n" YELLOW "=== Ternary ===" RESET "\n");

    // true ? 1 : 2
    {
        Stmt *s = stmt_let("x", expr_ternary(expr_bool(1), expr_number(1), expr_number(2)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 1};
        run_vm_test("ternary true (true ? 1 : 2)", stmts, 1, "x", expected, false);
    }

    // false ? 1 : 2
    {
        Stmt *s = stmt_let("x", expr_ternary(expr_bool(0), expr_number(1), expr_number(2)));
        Stmt *stmts[] = {s};
        Value expected = {.type = VAL_I32, .as.as_i32 = 2};
        run_vm_test("ternary false (false ? 1 : 2)", stmts, 1, "x", expected, false);
    }
}

static void test_control_flow(void) {
    printf("\n" YELLOW "=== Control Flow ===" RESET "\n");

    // if-then (true branch)
    {
        Stmt *then_branch = stmt_expr(expr_assign("x", expr_number(1)));
        Stmt *if_stmt = stmt_if(expr_bool(1), then_branch, NULL);
        Stmt *let_x = stmt_let("x", expr_number(0));
        Stmt *stmts[] = {let_x, if_stmt};
        Value expected = {.type = VAL_I32, .as.as_i32 = 1};
        run_vm_test("if true branch", stmts, 2, "x", expected, false);
    }

    // if-then (skipped)
    {
        Stmt *then_branch = stmt_expr(expr_assign("x", expr_number(1)));
        Stmt *if_stmt = stmt_if(expr_bool(0), then_branch, NULL);
        Stmt *let_x = stmt_let("x", expr_number(0));
        Stmt *stmts[] = {let_x, if_stmt};
        Value expected = {.type = VAL_I32, .as.as_i32 = 0};
        run_vm_test("if false (skipped)", stmts, 2, "x", expected, false);
    }

    // if-else (else branch)
    {
        Stmt *then_branch = stmt_expr(expr_assign("x", expr_number(1)));
        Stmt *else_branch = stmt_expr(expr_assign("x", expr_number(2)));
        Stmt *if_stmt = stmt_if(expr_bool(0), then_branch, else_branch);
        Stmt *let_x = stmt_let("x", expr_number(0));
        Stmt *stmts[] = {let_x, if_stmt};
        Value expected = {.type = VAL_I32, .as.as_i32 = 2};
        run_vm_test("if-else (else branch)", stmts, 2, "x", expected, false);
    }
}

static void test_loops(void) {
    printf("\n" YELLOW "=== Loops ===" RESET "\n");

    // Simple while loop: while (x < 5) x = x + 1
    {
        Expr *cond = expr_binary(expr_ident("x"), OP_LESS, expr_number(5));
        Expr *inc = expr_assign("x", expr_binary(expr_ident("x"), OP_ADD, expr_number(1)));
        Stmt *body = stmt_expr(inc);
        Stmt *while_stmt = stmt_while(cond, body);
        Stmt *let_x = stmt_let("x", expr_number(0));
        Stmt *stmts[] = {let_x, while_stmt};
        Value expected = {.type = VAL_I32, .as.as_i32 = 5};
        run_vm_test("while loop (count to 5)", stmts, 2, "x", expected, false);
    }

    // For loop: for (i = 0; i < 3; i = i + 1) sum = sum + i
    {
        Stmt *init = stmt_let("i", expr_number(0));
        Expr *cond = expr_binary(expr_ident("i"), OP_LESS, expr_number(3));
        Expr *inc = expr_assign("i", expr_binary(expr_ident("i"), OP_ADD, expr_number(1)));
        Stmt *body = stmt_expr(expr_assign("sum",
            expr_binary(expr_ident("sum"), OP_ADD, expr_ident("i"))));
        Stmt *for_stmt = stmt_for(init, cond, inc, body);
        Stmt *let_sum = stmt_let("sum", expr_number(0));
        Stmt *stmts[] = {let_sum, for_stmt};
        Value expected = {.type = VAL_I32, .as.as_i32 = 3}; // 0+1+2
        run_vm_test("for loop (sum 0..2)", stmts, 2, "sum", expected, false);
    }
}

static void test_builtins(void) {
    printf("\n" YELLOW "=== Builtins ===" RESET "\n");

    // typeof
    {
        Expr **args = malloc(sizeof(Expr*));
        args[0] = expr_number(42);
        Stmt *s = stmt_let("x", expr_call(expr_ident("typeof"), args, 1));
        Stmt *stmts[] = {s};
        Value expected = val_string("i32");
        run_vm_test("typeof(42)", stmts, 1, "x", expected, false);
    }
}

// ========== Summary ==========

static void print_summary(void) {
    printf("\n========================================\n");
    printf("           VM Test Summary\n");
    printf("========================================\n");
    printf(GREEN "Passed:  %d" RESET "\n", stats.passed);
    printf(RED "Failed:  %d" RESET "\n", stats.failed);
    printf(YELLOW "Skipped: %d" RESET "\n", stats.skipped);
    printf("----------------------------------------\n");
    printf("Total:   %d\n", stats.passed + stats.failed + stats.skipped);
    printf("========================================\n");

    if (stats.failed == 0) {
        printf(GREEN "\nAll tests passed!\n" RESET);
    } else {
        printf(RED "\n%d test(s) failed.\n" RESET, stats.failed);
    }
}

int main(void) {
    printf("========================================\n");
    printf("    Hemlock VM Feature Parity Tests\n");
    printf("========================================\n");

    test_literals();
    test_arithmetic();
    test_comparisons();
    test_logical();
    test_strings();
    test_variables();
    test_bitwise();
    test_ternary();
    test_control_flow();
    test_loops();
    test_builtins();

    print_summary();

    return stats.failed > 0 ? 1 : 0;
}

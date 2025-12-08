/*
 * Simple VM test - demonstrates bytecode VM functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ast.h"
#include "../include/interpreter.h"
#include "../src/vm/vm.h"
#include "../src/vm/compiler.h"
#include "../src/vm/chunk.h"
#include "../src/vm/bytecode.h"

// Forward declaration from vm_debug.c
extern Chunk* vm_compile_ast(Stmt **statements, int count, const char *source_file);

// Test 1: Compile and disassemble simple expressions
static void test_compile_expressions(void) {
    printf("\n=== Test 1: Compile Expressions ===\n");

    // Build: let x = 10 + 20;
    Expr *left = expr_number(10);
    Expr *right = expr_number(20);
    Expr *add = expr_binary(left, OP_ADD, right);
    Stmt *let_stmt = stmt_let("x", add);

    // Compile
    Stmt *stmts[] = { let_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 1, "test_expressions");

    if (chunk) {
        printf("Compilation successful!\n");
        chunk_disassemble(chunk, "expressions");
        chunk_free(chunk);
    } else {
        printf("Compilation FAILED!\n");
    }
}

// Test 2: Compile and run arithmetic
static void test_run_arithmetic(void) {
    printf("\n=== Test 2: Run Arithmetic ===\n");

    // Build: let x = 10 + 20 * 3;
    Expr *ten = expr_number(10);
    Expr *twenty = expr_number(20);
    Expr *three = expr_number(3);
    Expr *mul = expr_binary(twenty, OP_MUL, three);
    Expr *add = expr_binary(ten, OP_ADD, mul);
    Stmt *let_stmt = stmt_let("result", add);

    // Compile
    Stmt *stmts[] = { let_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 1, "test_arithmetic");

    if (!chunk) {
        printf("Compilation FAILED!\n");
        return;
    }

    chunk_disassemble(chunk, "arithmetic");

    // Create VM and run
    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("Execution OK!\n");

        // Check the global variable 'result'
        Value val;
        if (vm_get_global(vm, "result", &val)) {
            printf("result = ");
            if (val.type == VAL_I32) {
                printf("%d\n", val.as.as_i32);
            } else if (val.type == VAL_I64) {
                printf("%lld\n", (long long)val.as.as_i64);
            } else {
                printf("(type=%d)\n", val.type);
            }
        } else {
            printf("'result' not found in globals\n");
        }
    } else {
        printf("Execution FAILED: %s\n", vm_get_error(vm));
    }

    vm_print_globals(vm);
    vm_free(vm);
    chunk_free(chunk);
}

// Test 3: Compile and run print
static void test_run_print(void) {
    printf("\n=== Test 3: Run Print ===\n");

    // Build: print("Hello from VM!");
    Expr *print_fn = expr_ident("print");
    Expr **args = malloc(sizeof(Expr*));
    args[0] = expr_string("Hello from VM!");
    Expr *call = expr_call(print_fn, args, 1);
    Stmt *print_stmt = stmt_expr(call);

    // Compile
    Stmt *stmts[] = { print_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 1, "test_print");

    if (!chunk) {
        printf("Compilation FAILED!\n");
        return;
    }

    chunk_disassemble(chunk, "print_test");

    // Create VM and run
    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    printf("Output: ");
    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("\nExecution OK!\n");
    } else {
        printf("Execution FAILED: %s\n", vm_get_error(vm));
    }

    vm_free(vm);
    chunk_free(chunk);
}

// Test 4: Multiple statements
static void test_multiple_statements(void) {
    printf("\n=== Test 4: Multiple Statements ===\n");

    // Build:
    //   let a = 5;
    //   let b = 10;
    //   let c = a + b;
    //   print(c);

    Stmt *let_a = stmt_let("a", expr_number(5));
    Stmt *let_b = stmt_let("b", expr_number(10));

    Expr *a_ref = expr_ident("a");
    Expr *b_ref = expr_ident("b");
    Expr *add = expr_binary(a_ref, OP_ADD, b_ref);
    Stmt *let_c = stmt_let("c", add);

    Expr **print_args = malloc(sizeof(Expr*));
    print_args[0] = expr_ident("c");
    Expr *print_call = expr_call(expr_ident("print"), print_args, 1);
    Stmt *print_stmt = stmt_expr(print_call);

    // Compile
    Stmt *stmts[] = { let_a, let_b, let_c, print_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 4, "test_multi");

    if (!chunk) {
        printf("Compilation FAILED!\n");
        return;
    }

    chunk_disassemble(chunk, "multi_statements");

    // Create VM and run
    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    printf("Output: ");
    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("\nExecution OK!\n");
        vm_print_globals(vm);
    } else {
        printf("Execution FAILED: %s\n", vm_get_error(vm));
    }

    vm_free(vm);
    chunk_free(chunk);
}

// Test 5: Opcode table
static void test_opcode_table(void) {
    printf("\n=== Test 5: Opcode Table ===\n");

    printf("Total opcodes: %d\n\n", BC_COUNT);
    printf("Sample opcodes:\n");
    printf("  BC_LOAD_CONST  = %d, name = %s\n", BC_LOAD_CONST, opcode_name(BC_LOAD_CONST));
    printf("  BC_ADD         = %d, name = %s\n", BC_ADD, opcode_name(BC_ADD));
    printf("  BC_CALL        = %d, name = %s\n", BC_CALL, opcode_name(BC_CALL));
    printf("  BC_RETURN      = %d, name = %s\n", BC_RETURN, opcode_name(BC_RETURN));
    printf("  BC_JMP         = %d, name = %s\n", BC_JMP, opcode_name(BC_JMP));
}

// Test 6: Comparison and branch
static void test_comparison(void) {
    printf("\n=== Test 6: Comparison ===\n");

    // Build: let x = 5 < 10;
    Expr *five = expr_number(5);
    Expr *ten = expr_number(10);
    Expr *lt = expr_binary(five, OP_LESS, ten);
    Stmt *let_stmt = stmt_let("result", lt);

    // Compile
    Stmt *stmts[] = { let_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 1, "test_comparison");

    if (!chunk) {
        printf("Compilation FAILED!\n");
        return;
    }

    chunk_disassemble(chunk, "comparison");

    // Create VM and run
    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("Execution OK!\n");
        Value val;
        if (vm_get_global(vm, "result", &val)) {
            printf("result (5 < 10) = %s\n", val.as.as_bool ? "true" : "false");
        }
    } else {
        printf("Execution FAILED: %s\n", vm_get_error(vm));
    }

    vm_free(vm);
    chunk_free(chunk);
}

// Test 7: String concatenation
static void test_strings(void) {
    printf("\n=== Test 7: String Concatenation ===\n");

    // Build: let msg = "Hello" + " " + "World";
    Expr *hello = expr_string("Hello");
    Expr *space = expr_string(" ");
    Expr *world = expr_string("World");
    Expr *concat1 = expr_binary(hello, OP_ADD, space);
    Expr *concat2 = expr_binary(concat1, OP_ADD, world);
    Stmt *let_stmt = stmt_let("msg", concat2);

    // Compile
    Stmt *stmts[] = { let_stmt };
    Chunk *chunk = vm_compile_ast(stmts, 1, "test_strings");

    if (!chunk) {
        printf("Compilation FAILED!\n");
        return;
    }

    chunk_disassemble(chunk, "strings");

    // Create VM and run
    VM *vm = vm_new();
    vm_register_all_builtins(vm);

    VMResult result = vm_run(vm, chunk);

    if (result == VM_OK) {
        printf("Execution OK!\n");
        Value val;
        if (vm_get_global(vm, "msg", &val) && val.type == VAL_STRING) {
            printf("msg = \"%s\"\n", val.as.as_string->data);
        }
    } else {
        printf("Execution FAILED: %s\n", vm_get_error(vm));
    }

    vm_free(vm);
    chunk_free(chunk);
}

int main(void) {
    printf("====================================\n");
    printf("  Hemlock Bytecode VM Test Suite\n");
    printf("====================================\n");

    test_opcode_table();
    test_compile_expressions();
    test_run_arithmetic();
    test_comparison();
    test_strings();
    test_run_print();
    test_multiple_statements();

    printf("\n====================================\n");
    printf("  All tests completed!\n");
    printf("====================================\n");

    return 0;
}

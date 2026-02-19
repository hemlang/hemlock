/*
 * Native test for the WASM Persistent Context API
 *
 * Tests the core logic behind hemlock_context_create/eval/destroy/get/set
 * by exercising the same internal APIs (Environment, ExecutionContext,
 * serialization) without requiring the Emscripten SDK.
 *
 * Build & run:
 *   make test-wasm-context
 *
 * Or manually:
 *   gcc -std=c11 -O0 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared \
 *       tests/wasm/test_persistent_context.c \
 *       -o /tmp/test_persistent_context \
 *       -Lbuild -lcommon -ltools <interp objects> ...
 *   /tmp/test_persistent_context
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend.h"
#include "interpreter.h"
#include "interpreter/internal.h"
#include "interpreter/io/internal.h"
#include "hemlock_limits.h"

/* ---- Test helpers ---- */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s ", name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASSED\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf("FAILED: %s\n", msg); \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while (0)

/* FFI init/cleanup provided by the real ffi.c in native builds */

/* ---- Persistent context (mirrors wasm_interp_main.c logic) ---- */

/* Retained AST block — Function values store Stmt* pointers into
 * the AST, so we must keep the ASTs alive while the context exists. */
typedef struct AstBlock {
    Stmt **stmts;
    int    count;
    struct AstBlock *next;
} AstBlock;

typedef struct {
    Environment     *env;
    ExecutionContext *ctx;
    AstBlock        *ast_blocks;
    int              alive;
} TestContext;

static TestContext ctx_new(void) {
    TestContext tc;
    tc.env        = env_new(NULL);
    tc.ctx        = exec_context_new();
    tc.ast_blocks = NULL;
    tc.alive      = 1;
    register_builtins(tc.env, 0, NULL, tc.ctx);
    return tc;
}

static int ctx_eval(TestContext *tc, const char *source) {
    set_current_source_file("<test-ctx>");
    set_current_source_code(source);

    /* Reset control-flow state */
    tc->ctx->return_state.is_returning = 0;
    tc->ctx->loop_state.is_breaking    = 0;
    tc->ctx->loop_state.is_continuing  = 0;
    if (tc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(tc->ctx->exception_state.exception_value);
        tc->ctx->exception_state.is_throwing = 0;
    }

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count = 0;
    Stmt **stmts = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        for (int i = 0; i < stmt_count; i++) stmt_free(stmts[i]);
        free((void *)stmts);
        set_current_source_file(NULL);
        set_current_source_code(NULL);
        return 1;
    }

    resolve_program(stmts, stmt_count);
    optimize_program(stmts, stmt_count);
    eval_program(stmts, stmt_count, tc->env, tc->ctx);

    /* Retain AST — Function values reference Stmt* nodes */
    AstBlock *block = malloc(sizeof(AstBlock));
    if (block) {
        block->stmts = stmts;
        block->count = stmt_count;
        block->next  = tc->ast_blocks;
        tc->ast_blocks = block;
    }

    set_current_source_file(NULL);
    set_current_source_code(NULL);
    return 0;
}

static char *ctx_get(TestContext *tc, const char *varname) {
    Value val = env_get(tc->env, varname, tc->ctx);
    if (tc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(tc->ctx->exception_state.exception_value);
        tc->ctx->exception_state.is_throwing = 0;
        return NULL;
    }

    SerializeVisitedSet visited;
    serialize_visited_init(&visited);
    char *json = serialize_value(val, &visited, tc->ctx);
    serialize_visited_free(&visited);
    VALUE_RELEASE(val);

    if (!json && tc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(tc->ctx->exception_state.exception_value);
        tc->ctx->exception_state.is_throwing = 0;
    }
    return json;
}

static int ctx_set(TestContext *tc, const char *varname, const char *json) {
    JSONParser jp;
    jp.input = json;
    jp.pos   = 0;

    Value val = json_parse_value(&jp, tc->ctx);
    if (tc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(tc->ctx->exception_state.exception_value);
        tc->ctx->exception_state.is_throwing = 0;
        VALUE_RELEASE(val);
        return 1;
    }

    env_set(tc->env, varname, val, tc->ctx);
    if (tc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(tc->ctx->exception_state.exception_value);
        tc->ctx->exception_state.is_throwing = 0;
        env_define(tc->env, varname, val, 0, tc->ctx);
        if (tc->ctx->exception_state.is_throwing) {
            VALUE_RELEASE(tc->ctx->exception_state.exception_value);
            tc->ctx->exception_state.is_throwing = 0;
            VALUE_RELEASE(val);
            return 1;
        }
    }

    VALUE_RELEASE(val);
    return 0;
}

static void ctx_destroy(TestContext *tc) {
    env_break_cycles(tc->env);
    env_release(tc->env);
    exec_context_free(tc->ctx);

    /* Free retained AST blocks */
    AstBlock *block = tc->ast_blocks;
    while (block) {
        AstBlock *next = block->next;
        for (int i = 0; i < block->count; i++) {
            stmt_free(block->stmts[i]);
        }
        free((void *)block->stmts);
        free(block);
        block = next;
    }

    tc->env        = NULL;
    tc->ctx        = NULL;
    tc->ast_blocks = NULL;
    tc->alive      = 0;
}

/* ---- Tests ---- */

static void test_create_destroy(void) {
    TEST("create and destroy context");
    TestContext tc = ctx_new();
    ASSERT(tc.alive, "context should be alive");
    ASSERT(tc.env != NULL, "env should be non-NULL");
    ASSERT(tc.ctx != NULL, "ctx should be non-NULL");
    ctx_destroy(&tc);
    ASSERT(!tc.alive, "context should be dead after destroy");
    PASS();
}

static void test_eval_basic(void) {
    TEST("eval basic expression");
    TestContext tc = ctx_new();
    int rc = ctx_eval(&tc, "let x = 42;");
    ASSERT(rc == 0, "eval should succeed");
    ctx_destroy(&tc);
    PASS();
}

static void test_variable_persistence(void) {
    TEST("variables persist across eval calls");
    TestContext tc = ctx_new();

    int rc = ctx_eval(&tc, "let x = 10;");
    ASSERT(rc == 0, "first eval should succeed");

    rc = ctx_eval(&tc, "x = x + 5;");
    ASSERT(rc == 0, "second eval should succeed");

    char *json = ctx_get(&tc, "x");
    ASSERT(json != NULL, "x should exist");
    ASSERT(strcmp(json, "15") == 0, "x should be 15");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_integer(void) {
    TEST("get integer variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let n = 42;");

    char *json = ctx_get(&tc, "n");
    ASSERT(json != NULL, "n should exist");
    ASSERT(strcmp(json, "42") == 0, "expected '42'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_string(void) {
    TEST("get string variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let s = \"hello\";");

    char *json = ctx_get(&tc, "s");
    ASSERT(json != NULL, "s should exist");
    ASSERT(strcmp(json, "\"hello\"") == 0, "expected '\"hello\"'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_float(void) {
    TEST("get float variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let pi = 3.14;");

    char *json = ctx_get(&tc, "pi");
    ASSERT(json != NULL, "pi should exist");
    ASSERT(strcmp(json, "3.14") == 0, "expected '3.14'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_bool(void) {
    TEST("get boolean variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let b = true;");

    char *json = ctx_get(&tc, "b");
    ASSERT(json != NULL, "b should exist");
    ASSERT(strcmp(json, "true") == 0, "expected 'true'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_null(void) {
    TEST("get null variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let v = null;");

    char *json = ctx_get(&tc, "v");
    ASSERT(json != NULL, "v should exist");
    ASSERT(strcmp(json, "null") == 0, "expected 'null'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_array(void) {
    TEST("get array variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let arr = [1, 2, 3];");

    char *json = ctx_get(&tc, "arr");
    ASSERT(json != NULL, "arr should exist");
    ASSERT(strcmp(json, "[1,2,3]") == 0, "expected '[1,2,3]'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_object(void) {
    TEST("get object variable as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let obj = { x: 10, y: 20 };");

    char *json = ctx_get(&tc, "obj");
    ASSERT(json != NULL, "obj should exist");
    ASSERT(strcmp(json, "{\"x\":10,\"y\":20}") == 0, "expected '{\"x\":10,\"y\":20}'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_get_nonexistent(void) {
    TEST("get nonexistent variable returns NULL");
    TestContext tc = ctx_new();

    char *json = ctx_get(&tc, "does_not_exist");
    ASSERT(json == NULL, "should return NULL for missing var");
    /* Exception state should have been cleared */
    ASSERT(!tc.ctx->exception_state.is_throwing, "exception should be cleared");

    ctx_destroy(&tc);
    PASS();
}

static void test_set_integer(void) {
    TEST("set integer from JSON");
    TestContext tc = ctx_new();

    int rc = ctx_set(&tc, "x", "99");
    ASSERT(rc == 0, "set should succeed");

    char *json = ctx_get(&tc, "x");
    ASSERT(json != NULL, "x should exist");
    ASSERT(strcmp(json, "99") == 0, "expected '99'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_string(void) {
    TEST("set string from JSON");
    TestContext tc = ctx_new();

    int rc = ctx_set(&tc, "name", "\"Alice\"");
    ASSERT(rc == 0, "set should succeed");

    char *json = ctx_get(&tc, "name");
    ASSERT(json != NULL, "name should exist");
    ASSERT(strcmp(json, "\"Alice\"") == 0, "expected '\"Alice\"'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_object(void) {
    TEST("set object from JSON");
    TestContext tc = ctx_new();

    int rc = ctx_set(&tc, "point", "{\"x\":5,\"y\":10}");
    ASSERT(rc == 0, "set should succeed");

    char *json = ctx_get(&tc, "point");
    ASSERT(json != NULL, "point should exist");
    ASSERT(strcmp(json, "{\"x\":5,\"y\":10}") == 0, "expected '{\"x\":5,\"y\":10}'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_overwrites_existing(void) {
    TEST("set overwrites existing variable");
    TestContext tc = ctx_new();

    ctx_eval(&tc, "let x = 1;");
    int rc = ctx_set(&tc, "x", "999");
    ASSERT(rc == 0, "set should succeed");

    char *json = ctx_get(&tc, "x");
    ASSERT(json != NULL, "x should exist");
    ASSERT(strcmp(json, "999") == 0, "expected '999'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_invalid_json(void) {
    TEST("set with invalid JSON returns error");
    TestContext tc = ctx_new();

    int rc = ctx_set(&tc, "x", "{invalid json}}}");
    ASSERT(rc == 1, "set should return 1 for bad JSON");
    ASSERT(!tc.ctx->exception_state.is_throwing, "exception should be cleared");

    ctx_destroy(&tc);
    PASS();
}

static void test_set_then_eval(void) {
    TEST("set variable then use in eval");
    TestContext tc = ctx_new();

    ctx_set(&tc, "base", "100");
    ctx_eval(&tc, "let result = base + 50;");

    char *json = ctx_get(&tc, "result");
    ASSERT(json != NULL, "result should exist");
    ASSERT(strcmp(json, "150") == 0, "expected '150'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_function_persistence(void) {
    TEST("functions persist across eval calls");
    TestContext tc = ctx_new();

    ctx_eval(&tc, "fn double(n) { return n * 2; }");
    ctx_eval(&tc, "let r = double(21);");

    char *json = ctx_get(&tc, "r");
    ASSERT(json != NULL, "r should exist");
    ASSERT(strcmp(json, "42") == 0, "expected '42'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_multiple_evals_accumulate(void) {
    TEST("multiple evals accumulate state");
    TestContext tc = ctx_new();

    ctx_eval(&tc, "let a = 1;");
    ctx_eval(&tc, "let b = 2;");
    ctx_eval(&tc, "let c = a + b;");

    char *json_a = ctx_get(&tc, "a");
    char *json_b = ctx_get(&tc, "b");
    char *json_c = ctx_get(&tc, "c");

    ASSERT(json_a && strcmp(json_a, "1") == 0, "a should be 1");
    ASSERT(json_b && strcmp(json_b, "2") == 0, "b should be 2");
    ASSERT(json_c && strcmp(json_c, "3") == 0, "c should be 3");

    free(json_a);
    free(json_b);
    free(json_c);
    ctx_destroy(&tc);
    PASS();
}

static void test_parse_error_does_not_corrupt(void) {
    TEST("parse error doesn't corrupt context");
    TestContext tc = ctx_new();

    ctx_eval(&tc, "let x = 42;");
    int rc = ctx_eval(&tc, "let y = ;");  /* parse error */
    ASSERT(rc == 1, "should return parse error");

    /* x should still be accessible */
    char *json = ctx_get(&tc, "x");
    ASSERT(json != NULL, "x should still exist");
    ASSERT(strcmp(json, "42") == 0, "x should still be 42");
    free(json);

    /* Can still eval successfully after error */
    rc = ctx_eval(&tc, "let z = x + 1;");
    ASSERT(rc == 0, "eval should succeed after error");

    char *json_z = ctx_get(&tc, "z");
    ASSERT(json_z != NULL, "z should exist");
    ASSERT(strcmp(json_z, "43") == 0, "z should be 43");
    free(json_z);

    ctx_destroy(&tc);
    PASS();
}

static void test_caught_exception_does_not_leak(void) {
    TEST("caught exception doesn't leak into next eval");
    TestContext tc = ctx_new();

    ctx_eval(&tc, "let x = 10;");
    /* Throw and catch within one eval — exception is handled */
    ctx_eval(&tc, "try { throw \"oops\"; } catch(e) { }");

    /* Context should still work */
    ctx_eval(&tc, "let z = x + 5;");

    char *json = ctx_get(&tc, "z");
    ASSERT(json != NULL, "z should exist");
    ASSERT(strcmp(json, "15") == 0, "z should be 15");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_nested_object_get(void) {
    TEST("get nested object as JSON");
    TestContext tc = ctx_new();
    ctx_eval(&tc, "let user = { name: \"Bob\", scores: [10, 20, 30] };");

    char *json = ctx_get(&tc, "user");
    ASSERT(json != NULL, "user should exist");
    ASSERT(strcmp(json, "{\"name\":\"Bob\",\"scores\":[10,20,30]}") == 0,
           "expected nested object JSON");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_array_from_json(void) {
    TEST("set array from JSON and iterate in eval");
    TestContext tc = ctx_new();

    ctx_set(&tc, "items", "[10,20,30]");
    ctx_eval(&tc, "let total = 0; for (x in items) { total = total + x; }");

    char *json = ctx_get(&tc, "total");
    ASSERT(json != NULL, "total should exist");
    ASSERT(strcmp(json, "60") == 0, "expected '60'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_null(void) {
    TEST("set null from JSON");
    TestContext tc = ctx_new();

    int rc = ctx_set(&tc, "v", "null");
    ASSERT(rc == 0, "set null should succeed");

    char *json = ctx_get(&tc, "v");
    ASSERT(json != NULL, "v should exist");
    ASSERT(strcmp(json, "null") == 0, "expected 'null'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_set_bool_from_json(void) {
    TEST("set boolean from JSON");
    TestContext tc = ctx_new();

    ctx_set(&tc, "flag", "true");
    char *json = ctx_get(&tc, "flag");
    ASSERT(json != NULL, "flag should exist");
    ASSERT(strcmp(json, "true") == 0, "expected 'true'");
    free(json);

    ctx_set(&tc, "flag", "false");
    json = ctx_get(&tc, "flag");
    ASSERT(json != NULL, "flag should exist");
    ASSERT(strcmp(json, "false") == 0, "expected 'false'");
    free(json);

    ctx_destroy(&tc);
    PASS();
}

static void test_independent_contexts(void) {
    TEST("independent contexts don't share state");
    TestContext tc1 = ctx_new();
    TestContext tc2 = ctx_new();

    ctx_eval(&tc1, "let x = 111;");
    ctx_eval(&tc2, "let x = 222;");

    char *json1 = ctx_get(&tc1, "x");
    char *json2 = ctx_get(&tc2, "x");

    ASSERT(json1 && strcmp(json1, "111") == 0, "tc1.x should be 111");
    ASSERT(json2 && strcmp(json2, "222") == 0, "tc2.x should be 222");

    free(json1);
    free(json2);
    ctx_destroy(&tc1);
    ctx_destroy(&tc2);
    PASS();
}

static void test_empty_eval(void) {
    TEST("eval empty string is no-op");
    TestContext tc = ctx_new();
    int rc = ctx_eval(&tc, "");
    ASSERT(rc == 0, "empty eval should return 0");
    rc = ctx_eval(&tc, "   ");
    ASSERT(rc == 0, "whitespace eval should succeed");
    ctx_destroy(&tc);
    PASS();
}

/* ---- Main ---- */

int main(void) {
    printf("==========================================\n");
    printf("  Persistent Context API Tests (native)\n");
    printf("==========================================\n\n");

    /* Create / destroy */
    test_create_destroy();

    /* Basic eval */
    test_eval_basic();
    test_empty_eval();

    /* Variable persistence */
    test_variable_persistence();
    test_multiple_evals_accumulate();
    test_function_persistence();

    /* Get (read) */
    test_get_integer();
    test_get_string();
    test_get_float();
    test_get_bool();
    test_get_null();
    test_get_array();
    test_get_object();
    test_nested_object_get();
    test_get_nonexistent();

    /* Set (write) */
    test_set_integer();
    test_set_string();
    test_set_object();
    test_set_null();
    test_set_bool_from_json();
    test_set_array_from_json();
    test_set_overwrites_existing();
    test_set_invalid_json();
    test_set_then_eval();

    /* Error recovery */
    test_parse_error_does_not_corrupt();
    test_caught_exception_does_not_leak();

    /* Isolation */
    test_independent_contexts();

    printf("\n==========================================\n");
    printf("  Results: %d passed, %d failed (%d total)\n",
           tests_passed, tests_failed, tests_run);
    printf("==========================================\n");

    return tests_failed > 0 ? 1 : 0;
}

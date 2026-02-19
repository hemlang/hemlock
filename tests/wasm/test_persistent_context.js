#!/usr/bin/env node
/*
 * WASM Persistent Context API Tests (Node.js)
 *
 * Tests the hemlock_context_* exported functions via the WASM module.
 * Requires the WASM interpreter to be built first:
 *   make wasm-interpreter
 *
 * Run:
 *   node tests/wasm/test_persistent_context.js
 */

"use strict";

const path = require("path");
const wasmPath = path.join(__dirname, "..", "..", "wasm", "hemlock.js");

let passed = 0;
let failed = 0;
let total = 0;

function test(name, fn) {
    total++;
    process.stdout.write(`  ${name.padEnd(50)} `);
    try {
        fn();
        passed++;
        console.log("PASSED");
    } catch (e) {
        failed++;
        console.log(`FAILED: ${e.message}`);
    }
}

function assert(cond, msg) {
    if (!cond) throw new Error(msg || "assertion failed");
}

function assertEqual(actual, expected, msg) {
    if (actual !== expected) {
        throw new Error(
            (msg || "assertEqual") +
                `: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`
        );
    }
}

async function main() {
    /* Load the WASM module */
    let Module;
    try {
        Module = require(wasmPath);
    } catch (e) {
        console.log(
            "WARNING: wasm/hemlock.js not found. " +
                "Run 'make wasm-interpreter' first."
        );
        process.exit(0);
    }

    /* Wait for module initialization if it returns a promise */
    if (typeof Module.then === "function") {
        Module = await Module;
    }
    /* Give Emscripten time to initialize */
    await new Promise((r) => setTimeout(r, 500));

    /* Wrap the C functions */
    const ctxCreate = Module.cwrap(
        "hemlock_context_create",
        "number",
        []
    );
    const ctxEval = Module.cwrap(
        "hemlock_context_eval",
        "number",
        ["number", "string"]
    );
    const ctxDestroy = Module.cwrap(
        "hemlock_context_destroy",
        null,
        ["number"]
    );
    const ctxGet = Module.cwrap(
        "hemlock_context_get",
        "string",
        ["number", "string"]
    );
    const ctxSet = Module.cwrap(
        "hemlock_context_set",
        "number",
        ["number", "string", "string"]
    );
    const ctxLastError = Module.cwrap(
        "hemlock_context_last_error",
        "string",
        ["number"]
    );

    console.log("==========================================");
    console.log("  Persistent Context API Tests (WASM)");
    console.log("==========================================\n");

    /* ---- Create / Destroy ---- */

    test("create returns positive handle", () => {
        const h = ctxCreate();
        assert(h > 0, `handle should be > 0, got ${h}`);
        ctxDestroy(h);
    });

    test("destroy invalid handle is safe", () => {
        ctxDestroy(0);
        ctxDestroy(-1);
        ctxDestroy(9999);
    });

    /* ---- Basic Eval ---- */

    test("eval returns 0 on success", () => {
        const h = ctxCreate();
        assertEqual(ctxEval(h, "let x = 42;"), 0);
        ctxDestroy(h);
    });

    test("eval empty string returns 0", () => {
        const h = ctxCreate();
        assertEqual(ctxEval(h, ""), 0);
        ctxDestroy(h);
    });

    test("eval invalid handle returns -1", () => {
        assertEqual(ctxEval(0, "let x = 1;"), -1);
        assertEqual(ctxEval(9999, "let x = 1;"), -1);
    });

    test("eval parse error returns 1", () => {
        const h = ctxCreate();
        assertEqual(ctxEval(h, "let y = ;"), 1);
        ctxDestroy(h);
    });

    /* ---- Variable Persistence ---- */

    test("variables persist across eval calls", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 10;");
        ctxEval(h, "x = x + 5;");
        assertEqual(ctxGet(h, "x"), "15");
        ctxDestroy(h);
    });

    test("functions persist across eval calls", () => {
        const h = ctxCreate();
        ctxEval(h, "fn double(n) { return n * 2; }");
        ctxEval(h, "let r = double(21);");
        assertEqual(ctxGet(h, "r"), "42");
        ctxDestroy(h);
    });

    test("multiple evals accumulate state", () => {
        const h = ctxCreate();
        ctxEval(h, "let a = 1;");
        ctxEval(h, "let b = 2;");
        ctxEval(h, "let c = a + b;");
        assertEqual(ctxGet(h, "a"), "1");
        assertEqual(ctxGet(h, "b"), "2");
        assertEqual(ctxGet(h, "c"), "3");
        ctxDestroy(h);
    });

    /* ---- Get ---- */

    test("get integer as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let n = 42;");
        assertEqual(ctxGet(h, "n"), "42");
        ctxDestroy(h);
    });

    test("get string as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, 'let s = "hello";');
        assertEqual(ctxGet(h, "s"), '"hello"');
        ctxDestroy(h);
    });

    test("get float as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let pi = 3.14;");
        assertEqual(ctxGet(h, "pi"), "3.14");
        ctxDestroy(h);
    });

    test("get boolean as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let b = true;");
        assertEqual(ctxGet(h, "b"), "true");
        ctxDestroy(h);
    });

    test("get null as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let v = null;");
        assertEqual(ctxGet(h, "v"), "null");
        ctxDestroy(h);
    });

    test("get array as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let arr = [1, 2, 3];");
        assertEqual(ctxGet(h, "arr"), "[1,2,3]");
        ctxDestroy(h);
    });

    test("get object as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, "let obj = { x: 10, y: 20 };");
        assertEqual(ctxGet(h, "obj"), '{"x":10,"y":20}');
        ctxDestroy(h);
    });

    test("get nested object as JSON", () => {
        const h = ctxCreate();
        ctxEval(h, 'let user = { name: "Bob", scores: [10, 20, 30] };');
        assertEqual(
            ctxGet(h, "user"),
            '{"name":"Bob","scores":[10,20,30]}'
        );
        ctxDestroy(h);
    });

    test("get nonexistent variable returns null", () => {
        const h = ctxCreate();
        const result = ctxGet(h, "nope");
        assert(
            result === null || result === "" || result === undefined,
            `expected null/empty for missing var, got: ${result}`
        );
        ctxDestroy(h);
    });

    /* ---- Set ---- */

    test("set integer from JSON", () => {
        const h = ctxCreate();
        assertEqual(ctxSet(h, "x", "99"), 0);
        assertEqual(ctxGet(h, "x"), "99");
        ctxDestroy(h);
    });

    test("set string from JSON", () => {
        const h = ctxCreate();
        ctxSet(h, "name", '"Alice"');
        assertEqual(ctxGet(h, "name"), '"Alice"');
        ctxDestroy(h);
    });

    test("set object from JSON", () => {
        const h = ctxCreate();
        ctxSet(h, "pt", '{"x":5,"y":10}');
        assertEqual(ctxGet(h, "pt"), '{"x":5,"y":10}');
        ctxDestroy(h);
    });

    test("set overwrites existing variable", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 1;");
        ctxSet(h, "x", "999");
        assertEqual(ctxGet(h, "x"), "999");
        ctxDestroy(h);
    });

    test("set invalid JSON returns 1", () => {
        const h = ctxCreate();
        assertEqual(ctxSet(h, "x", "{bad json}}}"), 1);
        ctxDestroy(h);
    });

    test("set then use in eval", () => {
        const h = ctxCreate();
        ctxSet(h, "base", "100");
        ctxEval(h, "let result = base + 50;");
        assertEqual(ctxGet(h, "result"), "150");
        ctxDestroy(h);
    });

    test("set null from JSON", () => {
        const h = ctxCreate();
        ctxSet(h, "v", "null");
        assertEqual(ctxGet(h, "v"), "null");
        ctxDestroy(h);
    });

    test("set boolean from JSON", () => {
        const h = ctxCreate();
        ctxSet(h, "flag", "true");
        assertEqual(ctxGet(h, "flag"), "true");
        ctxSet(h, "flag", "false");
        assertEqual(ctxGet(h, "flag"), "false");
        ctxDestroy(h);
    });

    test("set array from JSON and iterate", () => {
        const h = ctxCreate();
        ctxSet(h, "items", "[10,20,30]");
        ctxEval(h, "let total = 0; for (x in items) { total = total + x; }");
        assertEqual(ctxGet(h, "total"), "60");
        ctxDestroy(h);
    });

    /* ---- Error Recovery ---- */

    test("parse error doesn't corrupt context", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 42;");
        assertEqual(ctxEval(h, "let y = ;"), 1); /* parse error */
        assertEqual(ctxGet(h, "x"), "42"); /* x still intact */
        assertEqual(ctxEval(h, "let z = x + 1;"), 0);
        assertEqual(ctxGet(h, "z"), "43");
        ctxDestroy(h);
    });

    test("caught exception doesn't leak into next eval", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 10;");
        ctxEval(h, 'try { throw "oops"; } catch(e) { }');
        ctxEval(h, "let z = x + 5;");
        assertEqual(ctxGet(h, "z"), "15");
        ctxDestroy(h);
    });

    /* ---- Last Error API ---- */

    test("last_error is null after successful eval", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 42;");
        const err = ctxLastError(h);
        assert(err === null || err === "" || err === undefined,
            `expected null/empty after success, got: ${err}`);
        ctxDestroy(h);
    });

    test("last_error returns message after parse error", () => {
        const h = ctxCreate();
        assertEqual(ctxEval(h, "let y = ;"), 1);
        const err = ctxLastError(h);
        assert(err && err.length > 0,
            `expected non-empty error after parse failure, got: ${err}`);
        ctxDestroy(h);
    });

    test("last_error returns message after runtime error", () => {
        const h = ctxCreate();
        const rc = ctxEval(h, 'throw "oops something broke";');
        assertEqual(rc, 2, "eval should return 2 on runtime error");
        const err = ctxLastError(h);
        assert(err && err.includes("oops something broke"),
            `expected error containing 'oops something broke', got: ${err}`);
        ctxDestroy(h);
    });

    test("runtime error doesn't corrupt context", () => {
        const h = ctxCreate();
        ctxEval(h, "let x = 42;");
        assertEqual(ctxEval(h, 'throw "boom";'), 2);
        /* previous state survives */
        assertEqual(ctxGet(h, "x"), "42");
        /* next eval clears the error */
        assertEqual(ctxEval(h, "let z = x + 1;"), 0);
        assertEqual(ctxGet(h, "z"), "43");
        const err = ctxLastError(h);
        assert(err === null || err === "" || err === undefined,
            `expected null/empty after success, got: ${err}`);
        ctxDestroy(h);
    });

    test("last_error returns null for invalid handle", () => {
        const err = ctxLastError(9999);
        assert(err === null || err === "" || err === undefined,
            `expected null for invalid handle, got: ${err}`);
    });

    test("last_error cleared on each new eval", () => {
        const h = ctxCreate();
        ctxEval(h, 'throw "first error";');
        assert(ctxLastError(h) && ctxLastError(h).includes("first error"));
        ctxEval(h, 'throw "second error";');
        const err = ctxLastError(h);
        assert(err && err.includes("second error"),
            `expected 'second error', got: ${err}`);
        assert(!err.includes("first error"),
            "first error should be gone");
        ctxDestroy(h);
    });

    /* ---- Isolation ---- */

    test("independent contexts don't share state", () => {
        const h1 = ctxCreate();
        const h2 = ctxCreate();
        ctxEval(h1, "let x = 111;");
        ctxEval(h2, "let x = 222;");
        assertEqual(ctxGet(h1, "x"), "111");
        assertEqual(ctxGet(h2, "x"), "222");
        ctxDestroy(h1);
        ctxDestroy(h2);
    });

    /* ---- Summary ---- */

    console.log(`\n==========================================`);
    console.log(`  Results: ${passed} passed, ${failed} failed (${total} total)`);
    console.log(`==========================================`);

    process.exit(failed > 0 ? 1 : 0);
}

main().catch((e) => {
    console.error("Fatal error:", e);
    process.exit(1);
});

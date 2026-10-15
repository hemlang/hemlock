#!/usr/bin/env node
/*
 * Hemlock JS API Wrapper Tests (Node.js)
 *
 * Tests the hemlock-api.js wrapper module over the WASM interpreter.
 * Requires the WASM interpreter to be built first:
 *   make wasm-interpreter
 *
 * Run:
 *   node tests/wasm/test_js_api.js
 */

"use strict";

const path = require("path");

let passed = 0;
let failed = 0;
let total = 0;

function test(name, fn) {
    total++;
    process.stdout.write(`  ${name.padEnd(55)} `);
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

function assertDeepEqual(actual, expected, msg) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(
            (msg || "assertDeepEqual") +
                `: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`
        );
    }
}

async function main() {
    /* Load the wrapper */
    const apiPath = path.join(__dirname, "..", "..", "wasm", "hemlock-api.js");
    const { Hemlock } = require(apiPath);

    /* Load the WASM module */
    const wasmPath = path.join(__dirname, "..", "..", "wasm", "hemlock.js");
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
    await new Promise((r) => setTimeout(r, 500));

    /* Initialize the Hemlock API */
    const hemlock = await Hemlock.init({ Module: Module });

    console.log("==========================================");
    console.log("  Hemlock JS API Wrapper Tests");
    console.log("==========================================\n");

    /* ---- Hemlock instance ---- */

    test("hemlock.version returns version string", () => {
        assert(hemlock.version && hemlock.version.length > 0,
            `expected version string, got: ${hemlock.version}`);
    });

    test("hemlock.eval runs code", () => {
        const rc = hemlock.eval('let __test_api = 42;');
        assertEqual(rc, 0, "eval should return 0");
    });

    test("hemlock.eval with empty string", () => {
        const rc = hemlock.eval('');
        assertEqual(rc, 0, "eval of empty string should return 0");
    });

    test("hemlock.getModule returns Module", () => {
        assert(hemlock.getModule() === Module, "should return same Module");
    });

    /* ---- Context creation ---- */

    test("createContext returns HemlockContext", () => {
        const ctx = hemlock.createContext();
        assert(ctx !== null && ctx !== undefined, "context should exist");
        assert(typeof ctx.eval === 'function', "context should have eval");
        assert(typeof ctx.get === 'function', "context should have get");
        assert(typeof ctx.set === 'function', "context should have set");
        assert(typeof ctx.destroy === 'function', "context should have destroy");
        ctx.destroy();
    });

    /* ---- Context eval ---- */

    test("ctx.eval returns {ok: true} on success", () => {
        const ctx = hemlock.createContext();
        const result = ctx.eval('let x = 42;');
        assertEqual(result.ok, true, "should succeed");
        assertEqual(result.error, null, "no error");
        ctx.destroy();
    });

    test("ctx.eval returns {ok: false} on parse error", () => {
        const ctx = hemlock.createContext();
        const result = ctx.eval('let x = ;');
        assertEqual(result.ok, false, "should fail");
        assert(result.error !== null, "should have error message");
        ctx.destroy();
    });

    test("ctx.eval returns {ok: false} on runtime error", () => {
        const ctx = hemlock.createContext();
        const result = ctx.eval('throw "boom";');
        assertEqual(result.ok, false, "should fail");
        assert(result.error !== null && result.error.includes("boom"),
            `error should contain 'boom', got: ${result.error}`);
        ctx.destroy();
    });

    test("ctx.eval after destroy returns error", () => {
        const ctx = hemlock.createContext();
        ctx.destroy();
        const result = ctx.eval('let x = 1;');
        assertEqual(result.ok, false);
        assert(result.error.includes("destroyed"));
    });

    /* ---- Context get ---- */

    test("ctx.get returns parsed integer", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let n = 42;');
        assertEqual(ctx.get('n'), 42);
        ctx.destroy();
    });

    test("ctx.get returns parsed float", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let pi = 3.14;');
        assertEqual(ctx.get('pi'), 3.14);
        ctx.destroy();
    });

    test("ctx.get returns parsed string", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let s = "hello";');
        assertEqual(ctx.get('s'), "hello");
        ctx.destroy();
    });

    test("ctx.get returns parsed boolean", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let b = true;');
        assertEqual(ctx.get('b'), true);
        ctx.destroy();
    });

    test("ctx.get returns null for null value", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let v = null;');
        assertEqual(ctx.get('v'), null);
        ctx.destroy();
    });

    test("ctx.get returns parsed array", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let arr = [1, 2, 3];');
        assertDeepEqual(ctx.get('arr'), [1, 2, 3]);
        ctx.destroy();
    });

    test("ctx.get returns parsed object", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let obj = { x: 10, y: 20 };');
        assertDeepEqual(ctx.get('obj'), { x: 10, y: 20 });
        ctx.destroy();
    });

    test("ctx.get returns undefined for missing var", () => {
        const ctx = hemlock.createContext();
        assertEqual(ctx.get('nope'), undefined);
        ctx.destroy();
    });

    test("ctx.getJSON returns raw JSON string", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let n = 42;');
        assertEqual(ctx.getJSON('n'), '42');
        ctx.destroy();
    });

    test("ctx.getJSON returns null for missing var", () => {
        const ctx = hemlock.createContext();
        assertEqual(ctx.getJSON('nope'), null);
        ctx.destroy();
    });

    /* ---- Context set ---- */

    test("ctx.set injects integer", () => {
        const ctx = hemlock.createContext();
        assert(ctx.set('x', 99), "set should succeed");
        assertEqual(ctx.get('x'), 99);
        ctx.destroy();
    });

    test("ctx.set injects string", () => {
        const ctx = hemlock.createContext();
        ctx.set('name', "Alice");
        assertEqual(ctx.get('name'), "Alice");
        ctx.destroy();
    });

    test("ctx.set injects object", () => {
        const ctx = hemlock.createContext();
        ctx.set('pt', { x: 5, y: 10 });
        assertDeepEqual(ctx.get('pt'), { x: 5, y: 10 });
        ctx.destroy();
    });

    test("ctx.set injects array", () => {
        const ctx = hemlock.createContext();
        ctx.set('items', [10, 20, 30]);
        assertDeepEqual(ctx.get('items'), [10, 20, 30]);
        ctx.destroy();
    });

    test("ctx.set overwrites existing variable", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let x = 1;');
        ctx.set('x', 999);
        assertEqual(ctx.get('x'), 999);
        ctx.destroy();
    });

    test("ctx.setJSON injects raw JSON", () => {
        const ctx = hemlock.createContext();
        assert(ctx.setJSON('data', '{"a":1,"b":2}'), "setJSON should succeed");
        assertDeepEqual(ctx.get('data'), { a: 1, b: 2 });
        ctx.destroy();
    });

    test("ctx.set then use in eval", () => {
        const ctx = hemlock.createContext();
        ctx.set('base', 100);
        ctx.eval('let result = base + 50;');
        assertEqual(ctx.get('result'), 150);
        ctx.destroy();
    });

    /* ---- Context lastError ---- */

    test("ctx.lastError returns null after success", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let x = 1;');
        assertEqual(ctx.lastError(), null);
        ctx.destroy();
    });

    test("ctx.lastError returns message after error", () => {
        const ctx = hemlock.createContext();
        ctx.eval('throw "oops";');
        assert(ctx.lastError() !== null && ctx.lastError().includes("oops"));
        ctx.destroy();
    });

    /* ---- Context isolation ---- */

    test("contexts are isolated", () => {
        const c1 = hemlock.createContext();
        const c2 = hemlock.createContext();
        c1.eval('let x = 111;');
        c2.eval('let x = 222;');
        assertEqual(c1.get('x'), 111);
        assertEqual(c2.get('x'), 222);
        c1.destroy();
        c2.destroy();
    });

    /* ---- Variable persistence ---- */

    test("variables persist across eval calls", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let x = 10;');
        ctx.eval('x = x + 5;');
        assertEqual(ctx.get('x'), 15);
        ctx.destroy();
    });

    test("functions persist across eval calls", () => {
        const ctx = hemlock.createContext();
        ctx.eval('fn double(n) { return n * 2; }');
        ctx.eval('let r = double(21);');
        assertEqual(ctx.get('r'), 42);
        ctx.destroy();
    });

    /* ---- Error recovery ---- */

    test("parse error doesn't corrupt context", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let x = 42;');
        ctx.eval('let y = ;'); // parse error
        assertEqual(ctx.get('x'), 42);
        ctx.eval('let z = x + 1;');
        assertEqual(ctx.get('z'), 43);
        ctx.destroy();
    });

    test("runtime error doesn't corrupt context", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let x = 42;');
        ctx.eval('throw "boom";');
        assertEqual(ctx.get('x'), 42);
        ctx.eval('let z = x + 1;');
        assertEqual(ctx.get('z'), 43);
        ctx.destroy();
    });

    /* ---- Compiled scripts ---- */

    test("compile returns HemlockScript", () => {
        const script = hemlock.compile('let __s = 1;');
        assert(script !== null && script !== undefined);
        assert(typeof script.run === 'function');
        assert(typeof script.free === 'function');
        script.free();
    });

    test("compile throws on parse error", () => {
        let threw = false;
        try {
            hemlock.compile('let x = ;');
        } catch (e) {
            threw = true;
        }
        assert(threw, "compile should throw on parse error");
    });

    test("script.run executes in context", () => {
        const ctx = hemlock.createContext();
        ctx.eval('let count = 0;');
        const script = hemlock.compile('count = count + 1;');
        script.run(ctx);
        script.run(ctx);
        script.run(ctx);
        assertEqual(ctx.get('count'), 3);
        script.free();
        ctx.destroy();
    });

    test("script.run returns {ok: true} on success", () => {
        const ctx = hemlock.createContext();
        const script = hemlock.compile('let __r = 1;');
        const result = script.run(ctx);
        assertEqual(result.ok, true);
        assertEqual(result.error, null);
        script.free();
        ctx.destroy();
    });

    test("script.run returns error after free", () => {
        const ctx = hemlock.createContext();
        const script = hemlock.compile('let __f = 1;');
        script.free();
        const result = script.run(ctx);
        assertEqual(result.ok, false);
        assert(result.error.includes("freed"));
        ctx.destroy();
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

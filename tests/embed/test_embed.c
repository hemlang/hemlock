/*
 * Tests for the embedding API (include/hemlock_embed.h).
 * Run from tests/embed (make test-embed does).
 */
#include "hemlock_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int native_calls = 0;

static int host_add(void *ud, const HmlEmbedValue *args, int n, HmlEmbedValue *result,
                    char *error, size_t error_size) {
    native_calls += *(int *)ud;
    if (n != 2 || args[0].kind != HML_EMBED_INT || args[1].kind != HML_EMBED_INT) {
        snprintf(error, error_size, "host_add wants two ints");
        return 1;
    }
    *result = hml_embed_int(args[0].as.i + args[1].as.i);
    return 0;
}

static HmlEmbedValue call1(HmlInstance *inst, const char *fn, HmlEmbedValue arg, HmlEmbedStatus *st) {
    HmlEmbedValue out;
    *st = hml_instance_call(inst, fn, &arg, 1, &out);
    hml_embed_value_free(&arg);
    return out;
}

int main(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 2;
    char stdlib_path[PATH_MAX + 32];
    snprintf(stdlib_path, sizeof(stdlib_path), "%s/../../stdlib", cwd);
    char script_path[PATH_MAX + 32];
    snprintf(script_path, sizeof(script_path), "%s/script.hml", cwd);

    HmlVMConfig cfg;
    hml_vm_config_init(&cfg);
    cfg.stdlib_path = stdlib_path;
    HmlVM *vm = hml_vm_new(&cfg);
    CHECK(vm != NULL, "vm");

    int weight = 1;
    CHECK(hml_vm_define_native(vm, "host_add", host_add, &weight) == HML_EMBED_OK,
          "define_native: %s", hml_vm_last_error(vm));
    CHECK(hml_vm_define_native(vm, "bad name", host_add, &weight) == HML_EMBED_BAD_ARGUMENT, "bad native name");

    /* Prelude: a handle wrapper with a method */
    HmlEmbedStatus st = hml_vm_eval(vm,
        "fn __wrap(h) { return { __handle: h, fn id() { return self.__handle; } }; }",
        "prelude.hml");
    CHECK(st == HML_EMBED_OK, "prelude: %s", hml_vm_last_error(vm));
    hml_vm_set_handle_wrapper(vm, "__wrap");
    hml_vm_declare_global(vm, "owner");

    char *src = read_all("script.hml");
    HmlScript *script = hml_script_compile(vm, src, script_path, &st);
    CHECK(script != NULL && st == HML_EMBED_OK, "compile: %s", hml_vm_last_error(vm));
    if (!script) return 1;

    /* Declarations without running */
    CHECK(hml_script_has_function(script, "hit"), "has hit");
    CHECK(!hml_script_has_function(script, "speed"), "speed is not a fn");
    int found_speed = 0, found_hits = 0;
    for (int i = 0; i < hml_script_variable_count(script); i++) {
        const char *nm = hml_script_variable_name(script, i);
        if (strcmp(nm, "speed") == 0) found_speed = hml_script_variable_exported(script, i);
        if (strcmp(nm, "hits") == 0) found_hits = 1 + hml_script_variable_exported(script, i);
    }
    CHECK(found_speed == 1, "speed exported");
    CHECK(found_hits == 1, "hits not exported");
    for (int i = 0; i < hml_script_function_count(script); i++) {
        if (strcmp(hml_script_function_name(script, i), "hit") == 0)
            CHECK(hml_script_function_arity(script, i) == 0, "hit arity (default param)");
        if (strcmp(hml_script_function_name(script, i), "echo") == 0)
            CHECK(hml_script_function_arity(script, i) == 1, "echo arity");
    }

    /* Static metadata: lines, annotations, literal defaults, params */
    {
        int vi = hml_script_find_variable(script, "speed");
        CHECK(vi >= 0 && hml_script_variable_line(script, vi) == 5, "speed line");
        CHECK(vi >= 0 && hml_script_variable_type(script, vi) &&
              strcmp(hml_script_variable_type(script, vi), "f64") == 0, "speed annotation");
        HmlEmbedValue dv;
        CHECK(hml_script_variable_default(script, vi, &dv) == HML_EMBED_OK && dv.kind == HML_EMBED_FLOAT &&
              dv.as.f == 2.5, "speed default literal");
        int li = hml_script_find_variable(script, "label");
        CHECK(li >= 0 && hml_script_variable_default(script, li, &dv) == HML_EMBED_NOT_FOUND, "computed default");
        CHECK(li >= 0 && hml_script_variable_type(script, li) == NULL, "no annotation");
        int hi = hml_script_find_function(script, "hit");
        CHECK(hi >= 0 && hml_script_function_line(script, hi) == 9, "hit line");
        CHECK(hml_script_function_param_count(script, hi) == 1 &&
              strcmp(hml_script_function_param_name(script, hi, 0), "n") == 0, "hit param");
        int si = hml_script_find_function(script, "sum");
        CHECK(si >= 0 && hml_script_function_has_rest(script, si) && hml_script_function_param_count(script, si) == 0,
              "sum rest");
        CHECK(hml_script_find_function(script, "speed") == -1, "speed not a fn");

        HmlScript *lit = hml_script_compile(vm,
            "const A = -3;\nlet b: f64 = 2;\nlet c = [1, \"x\", true, null];\nlet d = { k: -1.5 };\nlet e: array<i32> = [];\n",
            "/tmp/lit.hml", &st);
        CHECK(lit != NULL, "compile literals: %s", hml_vm_last_error(vm));
        if (lit) {
            CHECK(hml_script_variable_is_const(lit, 0), "A const");
            CHECK(hml_script_variable_default(lit, 0, &dv) == HML_EMBED_OK && dv.kind == HML_EMBED_INT && dv.as.i == -3, "-3");
            CHECK(hml_script_variable_default(lit, 1, &dv) == HML_EMBED_OK && dv.kind == HML_EMBED_FLOAT && dv.as.f == 2.0, "f64 from int literal");
            CHECK(hml_script_variable_default(lit, 2, &dv) == HML_EMBED_OK && dv.kind == HML_EMBED_ARRAY && dv.as.arr.count == 4 &&
                  dv.as.arr.items[3].kind == HML_EMBED_NULL, "array literal");
            hml_embed_value_free(&dv);
            CHECK(hml_script_variable_default(lit, 3, &dv) == HML_EMBED_OK && dv.kind == HML_EMBED_OBJECT &&
                  hml_embed_object_get(&dv, "k")->as.f == -1.5, "object literal");
            hml_embed_value_free(&dv);
            CHECK(strcmp(hml_script_variable_type(lit, 4), "array<i32>") == 0, "array annotation");
            hml_script_release(lit);
        }
    }

    /* Two instances, independent state */
    const char *names[] = { "owner" };
    HmlEmbedValue owner_a = hml_embed_object(NULL, 1);
    hml_embed_object_set(&owner_a, "name", hml_embed_cstring("A"));
    HmlInstance *a = hml_instance_new(script, names, &owner_a, 1, &st);
    CHECK(a != NULL, "instance a: %s", hml_vm_last_error(vm));
    HmlEmbedValue owner_b = hml_embed_object(NULL, 1);
    hml_embed_object_set(&owner_b, "name", hml_embed_cstring("B"));
    HmlInstance *b = hml_instance_new(script, names, &owner_b, 1, &st);
    CHECK(b != NULL, "instance b: %s", hml_vm_last_error(vm));
    hml_embed_value_free(&owner_a);
    hml_embed_value_free(&owner_b);
    if (!a || !b) return 1;

    HmlEmbedValue r;
    CHECK(hml_instance_call(a, "hit", NULL, 0, &r) == HML_EMBED_OK && r.kind == HML_EMBED_INT && r.as.i == 1,
          "a.hit() -> 1 (%s)", hml_vm_last_error(vm));
    hml_embed_value_free(&r);
    r = call1(a, "hit", hml_embed_int(5), &st);
    CHECK(st == HML_EMBED_OK && r.as.i == 6, "a.hit(5) -> 6");
    hml_embed_value_free(&r);
    CHECK(hml_instance_call(b, "hit", NULL, 0, &r) == HML_EMBED_OK && r.as.i == 1, "b independent");
    hml_embed_value_free(&r);

    /* Rest params */
    HmlEmbedValue three[3] = { hml_embed_int(1), hml_embed_int(2), hml_embed_int(3) };
    CHECK(hml_instance_call(a, "sum", three, 3, &r) == HML_EMBED_OK && r.as.i == 6,
          "sum(1,2,3) (%s)", hml_vm_last_error(vm));
    hml_embed_value_free(&r);

    /* Structured return: predefined owner, stdlib + relative imports */
    CHECK(hml_instance_call(b, "info", NULL, 0, &r) == HML_EMBED_OK && r.kind == HML_EMBED_OBJECT,
          "info: %s", hml_vm_last_error(vm));
    const HmlEmbedValue *f = hml_embed_object_get(&r, "name");
    CHECK(f && f->kind == HML_EMBED_STRING && strcmp(f->as.s.data, "B") == 0, "owner.name");
    f = hml_embed_object_get(&r, "root");
    CHECK(f && f->kind == HML_EMBED_FLOAT && f->as.f == 4.0, "sqrt via stdlib");
    f = hml_embed_object_get(&r, "doubled");
    CHECK(f && f->kind == HML_EMBED_INT && f->as.i == 42, "relative import");
    f = hml_embed_object_get(&r, "tags");
    CHECK(f && f->kind == HML_EMBED_ARRAY && f->as.arr.count == 2 &&
          strcmp(f->as.arr.items[1].as.s.data, "b") == 0, "array field");
    f = hml_embed_object_get(&r, "ok");
    CHECK(f && f->kind == HML_EMBED_BOOL && f->as.b == 1, "bool field");
    hml_embed_value_free(&r);

    /* Natives */
    HmlEmbedValue two[2] = { hml_embed_int(40), hml_embed_int(2) };
    CHECK(hml_instance_call(a, "add_via_host", two, 2, &r) == HML_EMBED_OK && r.as.i == 42 && native_calls == 1,
          "native call (%s)", hml_vm_last_error(vm));
    hml_embed_value_free(&r);
    HmlEmbedValue bad[2] = { hml_embed_int(1), hml_embed_float(2.0) };
    CHECK(hml_instance_call(a, "add_via_host", bad, 2, &r) == HML_EMBED_RUNTIME_ERROR &&
          strcmp(hml_vm_last_error(vm), "host_add wants two ints") == 0, "native error -> exception: %s",
          hml_vm_last_error(vm));
    CHECK(hml_vm_last_error_line(vm) == 24 && hml_vm_last_error_file(vm) &&
          strstr(hml_vm_last_error_file(vm), "script.hml"), "native error located in the script (%s:%d)",
          hml_vm_last_error_file(vm) ? hml_vm_last_error_file(vm) : "?", hml_vm_last_error_line(vm));

    /* Round trips: big ints, floats, strings with UTF-8, nested */
    r = call1(a, "echo", hml_embed_int(5000000000LL), &st);
    CHECK(st == HML_EMBED_OK && r.kind == HML_EMBED_INT && r.as.i == 5000000000LL, "i64 round trip");
    hml_embed_value_free(&r);
    r = call1(a, "echo", hml_embed_cstring("h\xc3\xa9llo"), &st);
    CHECK(st == HML_EMBED_OK && r.kind == HML_EMBED_STRING && r.as.s.len == 6, "utf8 round trip");
    hml_embed_value_free(&r);
    HmlEmbedValue nested = hml_embed_array(0);
    HmlEmbedValue inner = hml_embed_object("Vec", 0);
    hml_embed_object_set(&inner, "x", hml_embed_float(1.5));
    hml_embed_object_set(&inner, "x", hml_embed_float(2.5));   /* replaces */
    hml_embed_array_push(&nested, inner);
    hml_embed_array_push(&nested, hml_embed_null());
    HmlEmbedValue nested_copy = hml_embed_value_copy(&nested);
    CHECK(nested_copy.kind == HML_EMBED_ARRAY && nested_copy.as.arr.count == 2 &&
          nested_copy.as.arr.items[0].as.obj.values[0].as.f == 2.5 &&
          strcmp(nested_copy.as.arr.items[0].as.obj.type_name, "Vec") == 0 &&
          nested_copy.as.arr.items[0].as.obj.keys[0] != nested.as.arr.items[0].as.obj.keys[0], "deep copy");
    hml_embed_value_free(&nested_copy);
    r = call1(a, "echo", nested, &st);
    CHECK(st == HML_EMBED_OK && r.kind == HML_EMBED_ARRAY && r.as.arr.count == 2 &&
          r.as.arr.items[0].kind == HML_EMBED_OBJECT &&
          r.as.arr.items[0].as.obj.count == 1 &&
          r.as.arr.items[0].as.obj.values[0].as.f == 2.5 &&
          r.as.arr.items[1].kind == HML_EMBED_NULL, "nested round trip");
    hml_embed_value_free(&r);

    /* Handles: wrapper gives scripts methods; objects with __handle come back as handles */
    r = call1(a, "handle_id", hml_embed_handle(77), &st);
    CHECK(st == HML_EMBED_OK && r.kind == HML_EMBED_INT && r.as.i == 77, "handle wrapper method (%s)",
          hml_vm_last_error(vm));
    hml_embed_value_free(&r);
    r = call1(a, "echo", hml_embed_handle(0xFFFFFFFFFFULL), &st);
    CHECK(st == HML_EMBED_OK && r.kind == HML_EMBED_HANDLE && r.as.handle == 0xFFFFFFFFFFULL, "handle round trip");
    hml_embed_value_free(&r);

    /* Functions come back opaque */
    HmlEmbedValue fnval;
    CHECK(hml_instance_get(a, "echo", &fnval) == HML_EMBED_OK && fnval.kind == HML_EMBED_OPAQUE, "fn opaque");
    hml_embed_value_free(&fnval);

    /* Runtime errors are captured with a location; the VM stays usable */
    CHECK(hml_instance_call(a, "boom", NULL, 0, &r) == HML_EMBED_RUNTIME_ERROR, "boom");
    CHECK(strstr(hml_vm_last_error(vm), "kaboom") != NULL, "boom message: %s", hml_vm_last_error(vm));
    CHECK(hml_vm_last_error_line(vm) == 30, "boom line (%d)", hml_vm_last_error_line(vm));
    CHECK(hml_instance_call(a, "hit", NULL, 0, &r) == HML_EMBED_OK && r.as.i == 7, "usable after error (%s)",
          hml_vm_last_error(vm));
    hml_embed_value_free(&r);
    CHECK(hml_instance_call(a, "nope", NULL, 0, &r) == HML_EMBED_NOT_FOUND, "missing fn");
    CHECK(hml_instance_call(a, "hits", NULL, 0, &r) == HML_EMBED_NOT_FOUND, "variable is not callable");

    /* get / set through annotations */
    HmlEmbedValue v;
    CHECK(hml_instance_get(a, "speed", &v) == HML_EMBED_OK && v.kind == HML_EMBED_FLOAT && v.as.f == 2.5, "get speed");
    HmlEmbedValue nv = hml_embed_int(4);   /* i32 -> f64 via annotation */
    CHECK(hml_instance_set(a, "speed", &nv) == HML_EMBED_OK, "set speed: %s", hml_vm_last_error(vm));
    CHECK(hml_instance_get(a, "speed", &v) == HML_EMBED_OK && v.kind == HML_EMBED_FLOAT && v.as.f == 4.0,
          "speed now f64 4.0 (kind %d)", v.kind);
    HmlEmbedValue sv = hml_embed_cstring("fast");
    CHECK(hml_instance_set(a, "speed", &sv) == HML_EMBED_RUNTIME_ERROR, "string into f64 rejected");
    hml_embed_value_free(&sv);
    CHECK(hml_instance_get(b, "speed", &v) == HML_EMBED_OK && v.as.f == 2.5, "b.speed untouched");
    CHECK(hml_instance_get(a, "missing", &v) == HML_EMBED_NOT_FOUND, "missing var");
    CHECK(hml_instance_get(a, "print", &v) == HML_EMBED_NOT_FOUND, "builtins are not instance vars");

    /* Compile errors */
    HmlScript *s2 = hml_script_compile(vm, "let x = ;\n", "/tmp/bad.hml", &st);
    CHECK(s2 == NULL && st == HML_EMBED_PARSE_ERROR && hml_vm_last_error_line(vm) == 1,
          "parse error (st %d line %d: %s)", st, hml_vm_last_error_line(vm), hml_vm_last_error(vm));
    s2 = hml_script_compile(vm, "fn f() {\n  return undefined_thing;\n}\n", "/tmp/names.hml", &st);
    CHECK(s2 == NULL && st == HML_EMBED_NAME_ERROR && hml_vm_last_error_line(vm) == 2 &&
          strstr(hml_vm_last_error(vm), "undefined_thing"),
          "name error (st %d line %d: %s)", st, hml_vm_last_error_line(vm), hml_vm_last_error(vm));
    s2 = hml_script_compile(vm, "import { x } from \"./does_not_exist.hml\";\n", script_path, &st);
    CHECK(s2 == NULL && (st == HML_EMBED_LOAD_ERROR || st == HML_EMBED_NAME_ERROR),
          "missing import (st %d: %s)", st, hml_vm_last_error(vm));

    /* Top level that throws: no instance, error reported */
    s2 = hml_script_compile(vm, "let a = 1;\nthrow \"top-level \" + a;\n", "/tmp/top.hml", &st);
    CHECK(s2 != NULL, "compile top-level-throw script: %s", hml_vm_last_error(vm));
    if (s2) {
        HmlInstance *bad_inst = hml_instance_new(s2, NULL, NULL, 0, &st);
        CHECK(bad_inst == NULL && st == HML_EMBED_RUNTIME_ERROR &&
              strstr(hml_vm_last_error(vm), "top-level 1"), "top-level throw: %s", hml_vm_last_error(vm));
        hml_script_release(s2);
    }

    /* A module that throws while being imported must not exit the host */
    s2 = hml_script_compile(vm, "import { v } from \"./throws.hml\";\nfn get() { return v; }\n",
                            script_path, &st);
    CHECK(s2 != NULL, "compile importer: %s", hml_vm_last_error(vm));
    if (s2) {
        HmlInstance *bad_inst = hml_instance_new(s2, NULL, NULL, 0, &st);
        CHECK(bad_inst == NULL && st == HML_EMBED_RUNTIME_ERROR &&
              strstr(hml_vm_last_error(vm), "module init failed"), "import throw: %s", hml_vm_last_error(vm));
        hml_script_release(s2);
    }

    /* Instances keep their script alive */
    hml_script_release(script);
    CHECK(hml_instance_call(b, "hit", NULL, 0, &r) == HML_EMBED_OK && r.as.i == 2, "script outlives release");
    hml_embed_value_free(&r);

    /* Many calls: no state leaks between calls */
    for (int i = 0; i < 20000; i++) {
        HmlEmbedValue arg = hml_embed_float(i * 0.5);
        if (hml_instance_call(b, "echo", &arg, 1, &r) != HML_EMBED_OK) { CHECK(0, "loop call"); break; }
        hml_embed_value_free(&r);
    }

    hml_instance_free(a);
    hml_instance_free(b);
    hml_vm_free(vm);
    free(src);

    /* A second VM after the first is gone */
    HmlVM *vm2 = hml_vm_new(&cfg);
    CHECK(vm2 != NULL && hml_vm_eval(vm2, "let z = 1 + 1;", NULL) == HML_EMBED_OK, "second vm");
    hml_vm_free(vm2);

    printf("%d/%d embedding checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}

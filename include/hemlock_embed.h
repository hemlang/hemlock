/*
 * Hemlock embedding API
 *
 * Run Hemlock scripts inside a host program (a game engine, an editor, a
 * server) through the tree-walking interpreter. Link libhemlock_embed.a
 * (`make embed`) plus the interpreter's system libraries
 * (`make embed-ldflags` prints them).
 *
 * The model is three objects:
 *
 *   HmlVM        One interpreter: a global environment holding the builtins
 *                plus whatever the host defines, and a module cache, so a
 *                stdlib module imported by many scripts is executed once.
 *   HmlScript    A parsed, name-checked source file whose imports are loaded.
 *                Compiling never runs the script.
 *   HmlInstance  One run of a script's top-level code in a fresh environment
 *                (a child of the VM's globals). Top-level `let`s are the
 *                instance's state, top-level `fn`s its methods. Two instances
 *                of one script share nothing but imported modules.
 *
 * Values cross the boundary as HmlEmbedValue, a plain tagged tree the host
 * owns. Nothing the host holds points into interpreter memory, so there is
 * no refcount to get wrong on the host side. Host objects (a Godot node, a
 * file in an editor) travel as HML_EMBED_HANDLE: an opaque u64 the host
 * defines. Inside Hemlock a handle is whatever the host's wrap function
 * (hml_vm_set_handle_wrapper) builds from it, usually an object with
 * methods that call back into the host; any object with a u64 `__handle`
 * field converts back to HML_EMBED_HANDLE.
 *
 * Errors: every call that runs Hemlock code returns an HmlEmbedStatus. On
 * failure, hml_vm_last_error() holds the message (uncaught exception text,
 * or the first parse/name error with its location) until the next call.
 *
 * Threading: a VM is single-threaded. Call it from one host thread at a
 * time; Hemlock code may still spawn its own tasks. Some interpreter state
 * (the current source file used in messages) is process-wide, so two VMs
 * must not run concurrently either.
 *
 * Process exits: `panic()` and `exit()` in a script end the host process.
 * That is the language's contract, and the embedding does not hide it.
 */

#ifndef HEMLOCK_EMBED_H
#define HEMLOCK_EMBED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HML_EMBED_API_VERSION 1

typedef struct HmlVM HmlVM;
typedef struct HmlScript HmlScript;
typedef struct HmlInstance HmlInstance;

typedef enum {
    HML_EMBED_OK = 0,
    HML_EMBED_PARSE_ERROR,    /* source would not parse */
    HML_EMBED_NAME_ERROR,     /* undefined name (see docs/reference/name-resolution.md) */
    HML_EMBED_LOAD_ERROR,     /* an imported module could not be loaded */
    HML_EMBED_RUNTIME_ERROR,  /* uncaught exception */
    HML_EMBED_NOT_FOUND,      /* no such function / variable */
    HML_EMBED_BAD_ARGUMENT    /* NULL handle, value that cannot convert, ... */
} HmlEmbedStatus;

typedef enum {
    HML_EMBED_NULL = 0,
    HML_EMBED_BOOL,
    HML_EMBED_INT,       /* every Hemlock integer type and rune, as i64 (u64 above INT64_MAX saturates) */
    HML_EMBED_FLOAT,     /* f32 / f64 */
    HML_EMBED_STRING,    /* UTF-8, NUL-terminated, length in bytes */
    HML_EMBED_ARRAY,
    HML_EMBED_OBJECT,    /* ordered string-keyed fields; type_name from `define`, else NULL */
    HML_EMBED_HANDLE,    /* opaque host object id */
    HML_EMBED_OPAQUE     /* Hemlock-only value (function, ptr, task, ...): read as null on the
                            host side; describes itself in .as.s for messages */
} HmlEmbedKind;

typedef struct HmlEmbedValue HmlEmbedValue;

struct HmlEmbedValue {
    HmlEmbedKind kind;
    union {
        int b;
        int64_t i;
        double f;
        uint64_t handle;
        struct { char *data; size_t len; } s;
        struct { HmlEmbedValue *items; int count; } arr;
        struct { char *type_name; char **keys; HmlEmbedValue *values; int count; } obj;
    } as;
};

/* ---------- Values (host-owned trees) ---------- */

HmlEmbedValue hml_embed_null(void);
HmlEmbedValue hml_embed_bool(int b);
HmlEmbedValue hml_embed_int(int64_t i);
HmlEmbedValue hml_embed_float(double f);
HmlEmbedValue hml_embed_handle(uint64_t handle);
/* Copies `len` bytes of `data` (need not be NUL-terminated). */
HmlEmbedValue hml_embed_string(const char *data, size_t len);
HmlEmbedValue hml_embed_cstring(const char *data);
/* Empty containers; fill with the push/set helpers (which take ownership of `v`). */
HmlEmbedValue hml_embed_array(int reserve);
void hml_embed_array_push(HmlEmbedValue *array, HmlEmbedValue v);
HmlEmbedValue hml_embed_object(const char *type_name, int reserve);
void hml_embed_object_set(HmlEmbedValue *object, const char *key, HmlEmbedValue v);
/* Field lookup on an HML_EMBED_OBJECT; NULL when absent. */
const HmlEmbedValue *hml_embed_object_get(const HmlEmbedValue *object, const char *key);
/* Deep copy. */
HmlEmbedValue hml_embed_value_copy(const HmlEmbedValue *v);
/* Free everything `v` owns and reset it to null. Safe on null values. */
void hml_embed_value_free(HmlEmbedValue *v);

/* ---------- VM ---------- */

typedef struct {
    int api_version;           /* HML_EMBED_API_VERSION */
    const char *stdlib_path;   /* directory `@stdlib/` resolves to; NULL = search next to the
                                  executable, the cwd, /usr/local/lib/hemlock/stdlib */
    const char *base_dir;      /* directory bare relative imports of the VM resolve against;
                                  NULL = cwd */
    int name_check;            /* nonzero: reject scripts with undefined names (recommended) */
    int max_stack_depth;       /* 0 = interpreter default */
} HmlVMConfig;

/* Fill `config` with defaults (name_check on). */
void hml_vm_config_init(HmlVMConfig *config);

HmlVM *hml_vm_new(const HmlVMConfig *config);
void hml_vm_free(HmlVM *vm);

const char *hml_vm_last_error(const HmlVM *vm);
/* Location of the last error when known (1-based; 0 = unknown), and the
   file it was in (NULL = unknown). */
int hml_vm_last_error_line(const HmlVM *vm);
int hml_vm_last_error_column(const HmlVM *vm);
const char *hml_vm_last_error_file(const HmlVM *vm);

/*
 * Host functions. `args` and `result` are owned by the VM for the duration
 * of the call; set *result (default null) to return a value. To raise a
 * Hemlock exception, write a message into `error` (capacity error_size) and
 * return nonzero.
 */
typedef int (*HmlNativeFn)(void *userdata, const HmlEmbedValue *args, int num_args,
                           HmlEmbedValue *result, char *error, size_t error_size);

/* Define a global function `name` visible to every script of this VM. Call
   before compiling scripts that use it. Redefining a name replaces it. */
HmlEmbedStatus hml_vm_define_native(HmlVM *vm, const char *name, HmlNativeFn fn, void *userdata);

/* Define (or replace) a global variable. */
HmlEmbedStatus hml_vm_define_global(HmlVM *vm, const char *name, const HmlEmbedValue *value);

/* Declare a global the host will provide (through hml_vm_eval) so name
   checking accepts scripts that use it. hml_vm_define_* declare theirs. */
void hml_vm_declare_global(HmlVM *vm, const char *name);

/*
 * Run `source` in the VM's global environment (a prelude): its top-level
 * definitions become globals for every script compiled afterwards. Not
 * name-checked against host globals declared later. `name` labels errors.
 */
HmlEmbedStatus hml_vm_eval(HmlVM *vm, const char *source, const char *name);

/* Name of a global Hemlock function that turns a handle (u64) into the
   value scripts see. NULL (default) gives `{ __handle: <u64> }`. */
void hml_vm_set_handle_wrapper(HmlVM *vm, const char *function_name);

/* ---------- Scripts ---------- */

/*
 * Parse and name-check `source`, and load (not run) the modules it imports.
 * `path` is the script's absolute file path: relative imports resolve
 * against its directory, and messages name it. The script need not exist on
 * disk at that path. Returns NULL on failure (see hml_vm_last_error), with
 * *status set when non-NULL.
 */
HmlScript *hml_script_compile(HmlVM *vm, const char *source, const char *path,
                              HmlEmbedStatus *status);
/* Scripts are refcounted: each instance holds one reference. */
void hml_script_retain(HmlScript *script);
void hml_script_release(HmlScript *script);

/*
 * Top-level declarations, read from the AST without running anything (what
 * an editor needs for an outline or an inspector). Functions are top-level
 * `fn`s (and `let`s initialized with a function literal); variables are the
 * other top-level `let` / `const`s. Lines are 1-based. Out-of-range indexes
 * give NULL / 0.
 */
int hml_script_function_count(const HmlScript *script);
const char *hml_script_function_name(const HmlScript *script, int index);
int hml_script_function_arity(const HmlScript *script, int index);        /* params without defaults */
int hml_script_function_param_count(const HmlScript *script, int index);  /* excluding a rest param */
const char *hml_script_function_param_name(const HmlScript *script, int index, int param);
int hml_script_function_has_rest(const HmlScript *script, int index);
int hml_script_function_line(const HmlScript *script, int index);
int hml_script_find_function(const HmlScript *script, const char *name);  /* index or -1 */
int hml_script_has_function(const HmlScript *script, const char *name);

int hml_script_variable_count(const HmlScript *script);
const char *hml_script_variable_name(const HmlScript *script, int index);
int hml_script_variable_exported(const HmlScript *script, int index);     /* `export let` */
int hml_script_variable_is_const(const HmlScript *script, int index);
int hml_script_variable_line(const HmlScript *script, int index);
/* The annotation as written ("f64", "string?", "array<i32>", "Vector2"), or
   NULL when there is none or it is not a simple one. */
const char *hml_script_variable_type(const HmlScript *script, int index);
/* The initializer, when it is made of literals only (numbers, strings,
   bools, null, runes, negation, array and object literals); a float
   annotation makes an integer literal a float, as at runtime. Returns
   HML_EMBED_NOT_FOUND when the initializer is computed. */
HmlEmbedStatus hml_script_variable_default(const HmlScript *script, int index, HmlEmbedValue *out);
int hml_script_find_variable(const HmlScript *script, const char *name);  /* index or -1 */

/* ---------- Instances ---------- */

/*
 * Run the script's top level in a fresh environment. `predefined` names
 * bound in that environment before the script runs (e.g. "owner" for the
 * host object the instance belongs to); either array may be NULL when
 * num_predefined is 0. Returns NULL if the top level raised.
 */
HmlInstance *hml_instance_new(HmlScript *script, const char *const *predefined_names,
                              const HmlEmbedValue *predefined_values, int num_predefined,
                              HmlEmbedStatus *status);
void hml_instance_free(HmlInstance *instance);
HmlScript *hml_instance_script(HmlInstance *instance);

/* Does the instance bind `name` to a function (a top-level fn, or a
   function-valued variable)? */
int hml_instance_has_function(HmlInstance *instance, const char *name);

/* Call the instance's function `name`. *result (may be NULL) receives the
   return value, which the caller frees with hml_embed_value_free. */
HmlEmbedStatus hml_instance_call(HmlInstance *instance, const char *name,
                                 const HmlEmbedValue *args, int num_args,
                                 HmlEmbedValue *result);

/* Read / assign one of the instance's top-level bindings. Assignment goes
   through the variable's type annotation like `x = v;` would. */
HmlEmbedStatus hml_instance_get(HmlInstance *instance, const char *name, HmlEmbedValue *out);
HmlEmbedStatus hml_instance_set(HmlInstance *instance, const char *name, const HmlEmbedValue *value);

#ifdef __cplusplus
}
#endif

#endif /* HEMLOCK_EMBED_H */

/*
 * Hemlock Code Generator - Program Generation
 *
 * Handles top-level program generation, function declarations,
 * closure implementation/wrappers, and module initialization.
 */

#include "codegen_internal.h"

// ========== PROGRAM CODE GENERATION ==========

// Check if a statement is a function definition (let name = fn() {} or export fn name())
int is_function_def(Stmt *stmt, char **name_out, Expr **func_out) {
    // Direct let statement with function
    if (stmt->type == STMT_LET && stmt->as.let.value &&
        stmt->as.let.value->type == EXPR_FUNCTION) {
        *name_out = stmt->as.let.name;
        *func_out = stmt->as.let.value;
        return 1;
    }
    // Export statement with function declaration (export fn name())
    if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration &&
        stmt->as.export_stmt.declaration) {
        Stmt *decl = stmt->as.export_stmt.declaration;
        if (decl->type == STMT_LET && decl->as.let.value &&
            decl->as.let.value->type == EXPR_FUNCTION) {
            *name_out = decl->as.let.name;
            *func_out = decl->as.let.value;
            return 1;
        }
    }
    return 0;
}

// Generate a top-level function declaration
void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name) {
    // Generate function signature with closure env for uniform calling convention
    // Even named functions take HmlClosureEnv* as first param (unused, passed as NULL)
    codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
        int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[i];
        if (is_ref) {
            codegen_write(ctx, ", HmlValue *%s", safe_param);
        } else {
            codegen_write(ctx, ", HmlValue %s", safe_param);
        }
        free(safe_param);
    }
    if (func->as.function.rest_param) {
        char *safe_rest = codegen_sanitize_ident(func->as.function.rest_param);
        codegen_write(ctx, ", HmlValue %s", safe_rest);
        free(safe_rest);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "(void)_closure_env;");

    // Save state and initialize for function body
    FuncGenState saved_state;
    funcgen_save_state(ctx, &saved_state);

    // Note: Type inference scope management is disabled.
    // Type checking is done in a separate pass before codegen.
    (void)name;  // Suppress unused warning when optimization disabled

    // Add parameters as locals and apply defaults
    funcgen_add_params(ctx, func);
    funcgen_apply_defaults(ctx, func);

    // Track call depth for stack overflow detection (can be disabled for performance)
    if (ctx->stack_check) {
        codegen_writeln(ctx, "HML_CALL_ENTER();");
    }

    // OPTIMIZATION: Tail call elimination
    // Check if function is tail recursive and set up for tail call optimization
    // Tail call optimization converts: return func(args) -> reassign params; goto start
    // This is only safe when there are no defers and no rest params
    if (ctx->optimize && !func->as.function.rest_param &&
        is_tail_recursive_function(func->as.function.body, name)) {
        ctx->tail_call_func_name = (char*)name;  // Borrowed reference
        ctx->tail_call_label = codegen_label(ctx);
        ctx->tail_call_func_expr = func;
        codegen_writeln(ctx, "%s:;  // tail call target", ctx->tail_call_label);
    }

    // Set up shared environment for closures
    funcgen_setup_shared_env(ctx, func, NULL);

    // Generate body
    funcgen_generate_body(ctx, func);

    // Execute defers before implicit return
    codegen_defer_execute_all(ctx);
    if (ctx->has_defers) {
        codegen_writeln(ctx, "hml_defer_execute_all();");
    }

    // Decrement call depth and return
    if (ctx->stack_check) {
        codegen_writeln(ctx, "HML_CALL_EXIT();");
    }
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Note: Type inference scope pop is disabled (see above)

    // Restore state
    funcgen_restore_state(ctx, &saved_state);
}

// Generate a closure function (takes environment as first hidden parameter)
void codegen_closure_impl(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate function signature with environment parameter
    codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", closure->func_name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
        int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[i];
        if (is_ref) {
            codegen_write(ctx, ", HmlValue *%s", safe_param);
        } else {
            codegen_write(ctx, ", HmlValue %s", safe_param);
        }
        free(safe_param);
    }
    if (func->as.function.rest_param) {
        char *safe_rest = codegen_sanitize_ident(func->as.function.rest_param);
        codegen_write(ctx, ", HmlValue %s", safe_rest);
        free(safe_rest);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);

    // Save state and initialize for closure body
    FuncGenState saved_state;
    funcgen_save_state(ctx, &saved_state);
    ctx->num_locals = 0;  // Closures have their own isolated scope
    ctx->current_module = closure->source_module;
    ctx->current_closure = closure;

    // Add parameters as locals
    funcgen_add_params(ctx, func);

    // Extract captured variables from environment
    for (int i = 0; i < closure->num_captured; i++) {
        const char *var_name = closure->captured_vars[i];

        // Skip if this captured variable name conflicts with a function parameter
        int is_param = 0;
        for (int j = 0; j < func->as.function.num_params; j++) {
            if (strcmp(var_name, func->as.function.param_names[j]) == 0) {
                is_param = 1;
                break;
            }
        }
        if (!is_param && func->as.function.rest_param &&
            strcmp(var_name, func->as.function.rest_param) == 0) {
            is_param = 1;
        }
        if (is_param) continue;

        char *safe_var = codegen_sanitize_ident(var_name);

        // Check if this is a module-level export
        int is_module_export = 0;
        if (closure->source_module) {
            ExportedSymbol *exp = module_find_export(closure->source_module, var_name);
            if (exp) {
                is_module_export = 1;
                codegen_writeln(ctx, "HmlValue %s = %s;", safe_var, exp->mangled_name);
            }
        }

        if (!is_module_export) {
            int env_index = closure->shared_env_indices ? closure->shared_env_indices[i] : i;

            if (env_index == -1) {
                if (codegen_is_main_var(ctx, var_name)) {
                    codegen_writeln(ctx, "HmlValue %s = _main_%s;", safe_var, var_name);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = %s;", safe_var, safe_var);
                }
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_closure_env_get(_closure_env, %d);",
                              safe_var, env_index);
            }
        }
        codegen_add_local(ctx, var_name);
        free(safe_var);
    }

    // Apply defaults and track call depth
    funcgen_apply_defaults(ctx, func);
    if (ctx->stack_check) {
        codegen_writeln(ctx, "HML_CALL_ENTER();");
    }

    // Set up shared environment for nested closures
    funcgen_setup_shared_env(ctx, func, closure);

    // Generate body
    funcgen_generate_body(ctx, func);

    // Execute defers before implicit return
    codegen_defer_execute_all(ctx);
    if (ctx->has_defers) {
        codegen_writeln(ctx, "hml_defer_execute_all();");
    }

    // Release captured variables before return
    for (int i = 0; i < closure->num_captured; i++) {
        codegen_writeln(ctx, "hml_release(&%s);", closure->captured_vars[i]);
    }

    // Decrement call depth and return
    if (ctx->stack_check) {
        codegen_writeln(ctx, "HML_CALL_EXIT();");
    }
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore state
    funcgen_restore_state(ctx, &saved_state);
}

// Generate wrapper function for closure (to match function pointer signature)
void codegen_closure_wrapper(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate wrapper that extracts env from function value and calls real implementation
    codegen_write(ctx, "HmlValue %s_wrapper(HmlValue *_args, int _nargs, void *_env) {\n", closure->func_name);
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "HmlClosureEnv *_closure_env = (HmlClosureEnv*)_env;");

    // If function has rest param, collect extra args into an array
    if (func->as.function.rest_param) {
        codegen_writeln(ctx, "HmlValue _rest_array = hml_val_array();");
        codegen_writeln(ctx, "for (int _i = %d; _i < _nargs; _i++) {", func->as.function.num_params);
        codegen_indent_inc(ctx);
        codegen_writeln(ctx, "hml_array_push(_rest_array, _args[_i]);");
        codegen_indent_dec(ctx);
        codegen_writeln(ctx, "}");
    }

    // Call the actual closure function
    codegen_write(ctx, "");
    codegen_indent(ctx);
    if (func->as.function.rest_param) {
        // Need to capture result, release rest array, then return
        fprintf(ctx->output, "HmlValue _result = %s(_closure_env", closure->func_name);
    } else {
        fprintf(ctx->output, "return %s(_closure_env", closure->func_name);
    }
    for (int i = 0; i < func->as.function.num_params; i++) {
        fprintf(ctx->output, ", _args[%d]", i);
    }
    // Pass rest array as last param if present
    if (func->as.function.rest_param) {
        fprintf(ctx->output, ", _rest_array");
    }
    fprintf(ctx->output, ");\n");

    // Cleanup and return for rest param case
    if (func->as.function.rest_param) {
        codegen_writeln(ctx, "hml_release(&_rest_array);");
        codegen_writeln(ctx, "return _result;");
    }

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");
}

// Helper to generate init function for a module
void codegen_module_init(CodegenContext *ctx, CompiledModule *module) {
    codegen_write(ctx, "// Module init: %s\n", module->absolute_path);
    codegen_write(ctx, "static int %sinit_done = 0;\n", module->module_prefix);
    codegen_write(ctx, "static void %sinit(void) {\n", module->module_prefix);
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "if (%sinit_done) return;", module->module_prefix);
    codegen_writeln(ctx, "%sinit_done = 1;", module->module_prefix);
    codegen_writeln(ctx, "");

    // Save current module context
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = module;

    // First call init functions of imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = module_resolve_path(ctx->module_cache, module->absolute_path, import_path);
            if (resolved) {
                CompiledModule *imported = module_get_cached(ctx->module_cache, resolved);
                if (imported) {
                    codegen_writeln(ctx, "%sinit();", imported->module_prefix);
                }
                free(resolved);
            }
        }
    }
    codegen_writeln(ctx, "");

    // Generate code for each statement in the module
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        // Skip imports (already handled above)
        if (stmt->type == STMT_IMPORT) {
            // Generate import bindings
            codegen_stmt(ctx, stmt);
            continue;
        }

        // Handle exports
        if (stmt->type == STMT_EXPORT) {
            codegen_stmt(ctx, stmt);
            continue;
        }

        // Check if it's a function definition
        char *name = NULL;
        Expr *func = NULL;
        if (stmt->type == STMT_LET && stmt->as.let.value &&
            stmt->as.let.value->type == EXPR_FUNCTION) {
            name = stmt->as.let.name;
            func = stmt->as.let.value;
        }

        if (name && func) {
            // Function definition - already declared as global, just initialize
            char mangled[CODEGEN_MANGLED_NAME_SIZE];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
            int num_required = count_required_params(func->as.function.param_defaults, func->as.function.num_params);
            int has_rest = func->as.function.rest_param ? 1 : 0;
            codegen_writeln(ctx, "%s = hml_val_function_rest_named((void*)%sfn_%s, %d, %d, %d, %d, \"%s\");",
                          mangled, module->module_prefix, name,
                          func->as.function.num_params, num_required, func->as.function.is_async, has_rest, name);
        } else if (stmt->type == STMT_LET && stmt->as.let.value) {
            // Non-function let statement - assign to module global
            char mangled[CODEGEN_MANGLED_NAME_SIZE];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, stmt->as.let.name);
            char *value = codegen_expr(ctx, stmt->as.let.value);
            codegen_writeln(ctx, "%s = %s;", mangled, value);
            free(value);
        } else if (stmt->type == STMT_CONST && stmt->as.const_stmt.value) {
            // Const statement - assign to module global
            char mangled[CODEGEN_MANGLED_NAME_SIZE];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, stmt->as.const_stmt.name);
            char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
            codegen_writeln(ctx, "%s = %s;", mangled, value);
            free(value);
        } else {
            // Regular statement (import bindings, etc.)
            codegen_stmt(ctx, stmt);
        }
    }

    // Restore module context
    ctx->current_module = saved_module;

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");
}

// Helper to generate function declarations for a module
void codegen_module_funcs(CodegenContext *ctx, CompiledModule *module, MemBuffer *decl_buffer, MemBuffer *impl_buffer) {
    FILE *saved_output = ctx->output;
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = module;

    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        // Find function definitions (both exported and regular)
        const char *name = NULL;
        Expr *func = NULL;

        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            if (decl->type == STMT_LET && decl->as.let.value &&
                decl->as.let.value->type == EXPR_FUNCTION) {
                name = decl->as.let.name;
                func = decl->as.let.value;
            }
        } else if (stmt->type == STMT_LET && stmt->as.let.value &&
                   stmt->as.let.value->type == EXPR_FUNCTION) {
            name = stmt->as.let.name;
            func = stmt->as.let.value;
        }

        if (name && func) {
            char mangled_fn[CODEGEN_MANGLED_NAME_SIZE];
            snprintf(mangled_fn, sizeof(mangled_fn), "%sfn_%s", module->module_prefix, name);

            // Generate forward declaration
            ctx->output = decl_buffer->stream;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                char *safe_param = codegen_sanitize_ident(func->as.function.param_names[j]);
                int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[j];
                if (is_ref) {
                    codegen_write(ctx, ", HmlValue *%s", safe_param);
                } else {
                    codegen_write(ctx, ", HmlValue %s", safe_param);
                }
                free(safe_param);
            }
            codegen_write(ctx, ");\n");

            // Generate implementation
            ctx->output = impl_buffer->stream;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                char *safe_param = codegen_sanitize_ident(func->as.function.param_names[j]);
                int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[j];
                if (is_ref) {
                    codegen_write(ctx, ", HmlValue *%s", safe_param);
                } else {
                    codegen_write(ctx, ", HmlValue %s", safe_param);
                }
                free(safe_param);
            }
            codegen_write(ctx, ") {\n");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "(void)_closure_env;");

            // Save state and initialize for function body
            FuncGenState saved_state;
            funcgen_save_state(ctx, &saved_state);

            // Add parameters, apply defaults, set up shared env, and generate body
            funcgen_add_params(ctx, func);
            funcgen_apply_defaults(ctx, func);
            funcgen_setup_shared_env(ctx, func, NULL);
            funcgen_generate_body(ctx, func);

            // Execute defers and return
            codegen_defer_execute_all(ctx);
            codegen_writeln(ctx, "return hml_val_null();");

            // Restore state
            funcgen_restore_state(ctx, &saved_state);

            codegen_indent_dec(ctx);
            codegen_write(ctx, "}\n\n");
        }
    }

    ctx->output = saved_output;
    ctx->current_module = saved_module;
}

// Helper to collect all extern fn statements recursively (including from block scopes)
typedef struct ExternFnList {
    Stmt **stmts;
    int count;
    int capacity;
} ExternFnList;

static void collect_extern_fn_from_stmt(Stmt *stmt, ExternFnList *list);

static void collect_extern_fn_from_stmts(Stmt **stmts, int count, ExternFnList *list) {
    for (int i = 0; i < count; i++) {
        collect_extern_fn_from_stmt(stmts[i], list);
    }
}

static void collect_extern_fn_from_stmt(Stmt *stmt, ExternFnList *list) {
    if (!stmt) return;

    if (stmt->type == STMT_EXTERN_FN) {
        // Check if already in list (avoid duplicates)
        for (int i = 0; i < list->count; i++) {
            if (strcmp(list->stmts[i]->as.extern_fn.function_name,
                      stmt->as.extern_fn.function_name) == 0) {
                return;  // Already collected
            }
        }
        // Add to list
        if (list->count >= list->capacity) {
            list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
            list->stmts = realloc(list->stmts, list->capacity * sizeof(Stmt*));
        }
        list->stmts[list->count++] = stmt;
        return;
    }

    // Recursively check block statements
    if (stmt->type == STMT_BLOCK) {
        collect_extern_fn_from_stmts(stmt->as.block.statements, stmt->as.block.count, list);
    } else if (stmt->type == STMT_IF) {
        collect_extern_fn_from_stmt(stmt->as.if_stmt.then_branch, list);
        collect_extern_fn_from_stmt(stmt->as.if_stmt.else_branch, list);
    } else if (stmt->type == STMT_WHILE) {
        collect_extern_fn_from_stmt(stmt->as.while_stmt.body, list);
    } else if (stmt->type == STMT_FOR) {
        collect_extern_fn_from_stmt(stmt->as.for_loop.body, list);
    } else if (stmt->type == STMT_FOR_IN) {
        collect_extern_fn_from_stmt(stmt->as.for_in.body, list);
    } else if (stmt->type == STMT_TRY) {
        collect_extern_fn_from_stmt(stmt->as.try_stmt.try_block, list);
        collect_extern_fn_from_stmt(stmt->as.try_stmt.catch_block, list);
        collect_extern_fn_from_stmt(stmt->as.try_stmt.finally_block, list);
    } else if (stmt->type == STMT_SWITCH) {
        for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
            collect_extern_fn_from_stmt(stmt->as.switch_stmt.case_bodies[i], list);
        }
    } else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
        // Handle export extern fn
        collect_extern_fn_from_stmt(stmt->as.export_stmt.declaration, list);
    }
}

// Helper to collect struct definitions used in FFI
typedef struct FFIStructInfo {
    char *name;
    Stmt *define_stmt;  // The STMT_DEFINE_OBJECT
} FFIStructInfo;

typedef struct FFIStructList {
    FFIStructInfo *structs;
    int count;
    int capacity;
} FFIStructList;

// Check if a struct name is already in the list
static int ffi_struct_list_contains(FFIStructList *list, const char *name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->structs[i].name, name) == 0) return 1;
    }
    return 0;
}

// Add struct to list if not already present
static void ffi_struct_list_add(FFIStructList *list, const char *name, Stmt *define_stmt) {
    if (ffi_struct_list_contains(list, name)) return;
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->structs = realloc(list->structs, list->capacity * sizeof(FFIStructInfo));
    }
    list->structs[list->count].name = strdup(name);
    list->structs[list->count].define_stmt = define_stmt;
    list->count++;
}

// Find a define statement by name in the statements array
static Stmt* find_define_stmt(Stmt **stmts, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_DEFINE_OBJECT) {
            if (strcmp(stmts[i]->as.define_object.name, name) == 0) {
                return stmts[i];
            }
        }
    }
    return NULL;
}

// Collect structs used in extern functions
static void collect_ffi_structs(Stmt **stmts, int stmt_count, ExternFnList *extern_fns, FFIStructList *struct_list) {
    for (int i = 0; i < extern_fns->count; i++) {
        Stmt *fn = extern_fns->stmts[i];
        // Check return type
        Type *ret_type = fn->as.extern_fn.return_type;
        if (ret_type && ret_type->kind == TYPE_CUSTOM_OBJECT && ret_type->type_name) {
            Stmt *def = find_define_stmt(stmts, stmt_count, ret_type->type_name);
            if (def) {
                ffi_struct_list_add(struct_list, ret_type->type_name, def);
            }
        }
        // Check param types
        for (int j = 0; j < fn->as.extern_fn.num_params; j++) {
            Type *param_type = fn->as.extern_fn.param_types[j];
            if (param_type && param_type->kind == TYPE_CUSTOM_OBJECT && param_type->type_name) {
                Stmt *def = find_define_stmt(stmts, stmt_count, param_type->type_name);
                if (def) {
                    ffi_struct_list_add(struct_list, param_type->type_name, def);
                }
            }
        }
    }
}

// Check if an extern function uses any struct types
static int extern_fn_uses_structs(Stmt *fn) {
    Type *ret_type = fn->as.extern_fn.return_type;
    if (ret_type && ret_type->kind == TYPE_CUSTOM_OBJECT) return 1;
    for (int j = 0; j < fn->as.extern_fn.num_params; j++) {
        Type *param_type = fn->as.extern_fn.param_types[j];
        if (param_type && param_type->kind == TYPE_CUSTOM_OBJECT) return 1;
    }
    return 0;
}

void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count) {
    // Multi-pass approach:
    // 1. First pass through imports to compile all modules
    // 2. Generate named function bodies to a buffer to collect closures
    // 3. Output header + all forward declarations (functions + closures)
    // 4. Output module global variables and init functions
    // 5. Output closure implementations
    // 6. Output named function implementations
    // 7. Output main function

    // First pass: compile all imported modules
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                char *import_path = stmts[i]->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    module_compile(ctx, resolved);
                    free(resolved);
                }
            }
        }
    }

    // In-memory buffers for code generation (faster than tmpfile)
    MemBuffer *func_buffer = membuf_new();
    MemBuffer *main_buffer = membuf_new();
    MemBuffer *module_decl_buffer = membuf_new();
    MemBuffer *module_impl_buffer = membuf_new();
    FILE *saved_output = ctx->output;

    // Pre-pass: Collect all main file variable names BEFORE generating code
    // This ensures codegen_is_main_var() works during main() body generation
    // Always add 'args' as a main var (built-in global for command-line arguments)
    codegen_add_main_var(ctx, "args");
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        char *name;
        Expr *func;
        if (is_function_def(stmt, &name, &func)) {
            codegen_add_main_var(ctx, name);
            codegen_add_main_func(ctx, name, func->as.function.num_params, func->as.function.rest_param != NULL, func->as.function.param_is_ref, func);  // Also track as function definition with param count, rest param, ref params, and AST for inlining
        } else if (stmt->type == STMT_CONST) {
            codegen_add_main_var(ctx, stmt->as.const_stmt.name);
            codegen_add_const(ctx, stmt->as.const_stmt.name);
        } else if (stmt->type == STMT_LET) {
            codegen_add_main_var(ctx, stmt->as.let.name);
        } else if (stmt->type == STMT_ENUM) {
            codegen_add_main_var(ctx, stmt->as.enum_decl.name);
        } else if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace &&
                   stmt->as.import_stmt.namespace_name != NULL) {
            // Track namespace imports as main vars so they get _main_ prefix
            // (star imports without namespace don't need this)
            codegen_add_main_var(ctx, stmt->as.import_stmt.namespace_name);
        }
    }

    // Pre-pass: Collect import bindings for main file function call resolution
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                Stmt *import_stmt = stmts[i];
                char *import_path = import_stmt->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    CompiledModule *mod = module_get_cached(ctx->module_cache, resolved);
                    if (mod) {
                        // Add import bindings for named imports
                        if (!import_stmt->as.import_stmt.is_namespace) {
                            for (int j = 0; j < import_stmt->as.import_stmt.num_imports; j++) {
                                const char *import_name = import_stmt->as.import_stmt.import_names[j];
                                const char *alias = import_stmt->as.import_stmt.import_aliases[j];
                                const char *local_name = alias ? alias : import_name;
                                // Look up export to get function info
                                ExportedSymbol *exp = module_find_export(mod, import_name);
                                int is_function = exp ? exp->is_function : 0;
                                int num_params = exp ? exp->num_params : 0;
                                int is_extern = module_is_extern_fn(mod, import_name);
                                codegen_add_main_import(ctx, local_name, import_name, mod->module_prefix, is_function, num_params, is_extern);
                            }
                        }
                    }
                    free(resolved);
                }
            }
        }
    }

    // Pre-pass: Collect extern functions for FFI (need this before main() generation for struct registration)
    ExternFnList all_extern_fns = {NULL, 0, 0};
    collect_extern_fn_from_stmts(stmts, stmt_count, &all_extern_fns);
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            collect_extern_fn_from_stmts(mod->statements, mod->num_statements, &all_extern_fns);
            mod = mod->next;
        }
    }

    // Pre-pass: Collect struct types used in extern functions for FFI struct support
    FFIStructList ffi_structs = {NULL, 0, 0};
    collect_ffi_structs(stmts, stmt_count, &all_extern_fns, &ffi_structs);

    // Generate module functions first (to collect closures)
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_funcs(ctx, mod, module_decl_buffer, module_impl_buffer);
            mod = mod->next;
        }
    }

    // Pass 1: Generate named function bodies to buffer (this collects closures)
    ctx->output = func_buffer->stream;
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_function_decl(ctx, func, name);
        }
    }

    // Pass 2: Generate main function body to buffer (this collects more closures)
    ctx->output = main_buffer->stream;
    codegen_write(ctx, "int main(int argc, char **argv) {\n");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "hml_runtime_init(argc, argv);");

    // Initialize sandbox if enabled
    if (ctx->sandbox_flags != 0) {
        if (ctx->sandbox_root) {
            codegen_writeln(ctx, "hml_sandbox_init(%d, \"%s\");", ctx->sandbox_flags, ctx->sandbox_root);
        } else {
            codegen_writeln(ctx, "hml_sandbox_init(%d, NULL);", ctx->sandbox_flags);
        }
    }
    codegen_writeln(ctx, "");

    // Initialize global args array from command-line arguments
    // args is a static global (_main_args) so it's accessible from all functions
    codegen_writeln(ctx, "_main_args = hml_get_args();");
    codegen_add_local(ctx, "args");
    codegen_writeln(ctx, "");

    // Initialize imported modules
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                char *import_path = stmts[i]->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    CompiledModule *mod = module_get_cached(ctx->module_cache, resolved);
                    if (mod) {
                        codegen_writeln(ctx, "%sinit();", mod->module_prefix);
                    }
                    free(resolved);
                }
            }
        }
        codegen_writeln(ctx, "");
    }

    // Register FFI struct types (for extern functions that use struct params/returns)
    if (ffi_structs.count > 0) {
        codegen_writeln(ctx, "// Register FFI struct types");
        for (int i = 0; i < ffi_structs.count; i++) {
            Stmt *def = ffi_structs.structs[i].define_stmt;
            const char *struct_name = ffi_structs.structs[i].name;
            int num_fields = def->as.define_object.num_fields;

            // Generate field names array
            codegen_writeln(ctx, "{");
            ctx->indent++;
            codegen_writeln(ctx, "static const char *_ffi_struct_%s_names[%d] = {", struct_name, num_fields > 0 ? num_fields : 1);
            for (int j = 0; j < num_fields; j++) {
                codegen_writeln(ctx, "    \"%s\"%s", def->as.define_object.field_names[j], j < num_fields - 1 ? "," : "");
            }
            codegen_writeln(ctx, "};");

            // Generate field types array
            codegen_writeln(ctx, "static HmlFFIType _ffi_struct_%s_types[%d] = {", struct_name, num_fields > 0 ? num_fields : 1);
            for (int j = 0; j < num_fields; j++) {
                Type *ftype = def->as.define_object.field_types[j];
                const char *type_str = ftype ? type_kind_to_ffi_type(ftype->kind) : "HML_FFI_I32";
                codegen_writeln(ctx, "    %s%s", type_str, j < num_fields - 1 ? "," : "");
            }
            codegen_writeln(ctx, "};");

            // Register the struct
            codegen_writeln(ctx, "hml_ffi_register_struct(\"%s\", _ffi_struct_%s_names, _ffi_struct_%s_types, %d);",
                          struct_name, struct_name, struct_name, num_fields);
            ctx->indent--;
            codegen_writeln(ctx, "}");
        }
        codegen_writeln(ctx, "");
    }

    // Initialize top-level function variables (they're static globals now)
    // First pass: add all function names as "locals" for codegen tracking
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_add_local(ctx, name);
        }
    }
    codegen_writeln(ctx, "");

    // Generate all statements
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements to handle their embedded declarations
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        char *name;
        Expr *func;
        if (is_function_def(stmt, &name, &func)) {
            // Function definitions: assign function value to static global
            // Use _main_ prefix to avoid C name conflicts (e.g., kill, exit, fork)
            char *value = codegen_expr(ctx, func);
            codegen_writeln(ctx, "_main_%s = %s;", name, value);
            free(value);

            // Check if this was a self-referential function (e.g., let factorial = fn(n) { ... factorial(n-1) ... })
            // If so, update the closure environment to point to the now-initialized variable
            if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                for (int j = 0; j < ctx->last_closure_num_captured; j++) {
                    if (strcmp(ctx->last_closure_captured[j], name) == 0) {
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      ctx->last_closure_env_id, j, name);
                    }
                }
                // Reset the tracking - we've handled this closure
                ctx->last_closure_env_id = -1;
            }
        } else if (stmt->type == STMT_CONST) {
            // Top-level const: assign to static global instead of declaring local
            // Use _main_ prefix to avoid C name conflicts
            if (stmt->as.const_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
                codegen_writeln(ctx, "_main_%s = %s;", stmt->as.const_stmt.name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "_main_%s = hml_val_null();", stmt->as.const_stmt.name);
            }
        } else if (stmt->type == STMT_LET) {
            // Top-level let (non-function): assign to static global instead of declaring local
            // Use _main_ prefix to avoid C name conflicts
            if (stmt->as.let.value) {
                char *value = codegen_expr(ctx, stmt->as.let.value);
                // Check if there's a custom object type annotation (for duck typing)
                if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name) {
                    codegen_writeln(ctx, "_main_%s = hml_validate_object_type(%s, \"%s\");",
                                  stmt->as.let.name, value, stmt->as.let.type_annotation->type_name);
                } else if (stmt->as.let.type_annotation) {
                    // Handle type annotations
                    if (stmt->as.let.type_annotation->kind == TYPE_ARRAY) {
                        // Typed array: let arr: array<type> = [...]
                        Type *elem_type = stmt->as.let.type_annotation->element_type;
                        const char *arr_type = elem_type ? type_kind_to_hml_val(elem_type->kind) : NULL;
                        if (!arr_type) arr_type = "HML_VAL_NULL";
                        codegen_writeln(ctx, "_main_%s = hml_validate_typed_array(%s, %s);",
                                      stmt->as.let.name, value, arr_type);
                    } else {
                        // Primitive type annotation: let x: i64 = 0;
                        const char *hml_type = type_kind_to_hml_val(stmt->as.let.type_annotation->kind);
                        if (hml_type) {
                            codegen_writeln(ctx, "_main_%s = hml_convert_to_type(%s, %s);",
                                          stmt->as.let.name, value, hml_type);
                        } else {
                            codegen_writeln(ctx, "_main_%s = %s;", stmt->as.let.name, value);
                        }
                    }
                } else {
                    codegen_writeln(ctx, "_main_%s = %s;", stmt->as.let.name, value);
                }
                free(value);

                // Check if this was a self-referential closure
                if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                    for (int j = 0; j < ctx->last_closure_num_captured; j++) {
                        if (strcmp(ctx->last_closure_captured[j], stmt->as.let.name) == 0) {
                            codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                          ctx->last_closure_env_id, j, stmt->as.let.name);
                        }
                    }
                    ctx->last_closure_env_id = -1;
                }
            } else {
                codegen_writeln(ctx, "_main_%s = hml_val_null();", stmt->as.let.name);
            }
        } else {
            codegen_stmt(ctx, stmts[i]);  // Use original statement for non-unwrapped cases
        }
    }

    codegen_writeln(ctx, "");
    codegen_writeln(ctx, "hml_runtime_cleanup();");
    codegen_writeln(ctx, "return 0;");
    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n");

    // Now output everything in the correct order
    ctx->output = saved_output;

    // Header
    codegen_write(ctx, "/*\n");
    codegen_write(ctx, " * Generated by Hemlock Compiler\n");
    codegen_write(ctx, " */\n\n");
    codegen_write(ctx, "#include \"hemlock_runtime.h\"\n");
    codegen_write(ctx, "#include <setjmp.h>\n");
    codegen_write(ctx, "#include <signal.h>\n");
    codegen_write(ctx, "#include <sys/socket.h>\n");
    codegen_write(ctx, "#include <netinet/in.h>\n");
    codegen_write(ctx, "#include <arpa/inet.h>\n");
    codegen_write(ctx, "#include <poll.h>\n\n");

    // Signal constants
    codegen_write(ctx, "// Signal constants\n");
    codegen_write(ctx, "#define SIGINT_VAL 2\n");
    codegen_write(ctx, "#define SIGTERM_VAL 15\n");
    codegen_write(ctx, "#define SIGHUP_VAL 1\n");
    codegen_write(ctx, "#define SIGQUIT_VAL 3\n");
    codegen_write(ctx, "#define SIGABRT_VAL 6\n");
    codegen_write(ctx, "#define SIGUSR1_VAL 10\n");
    codegen_write(ctx, "#define SIGUSR2_VAL 12\n");
    codegen_write(ctx, "#define SIGALRM_VAL 14\n");
    codegen_write(ctx, "#define SIGCHLD_VAL 17\n");
    codegen_write(ctx, "#define SIGPIPE_VAL 13\n");
    codegen_write(ctx, "#define SIGCONT_VAL 18\n");
    codegen_write(ctx, "#define SIGSTOP_VAL 19\n");
    codegen_write(ctx, "#define SIGTSTP_VAL 20\n\n");

    // FFI: Global library handle and function pointer declarations
    // (all_extern_fns and ffi_structs already collected in pre-pass)
    int has_ffi = 0;
    for (int i = 0; i < stmt_count; i++) {
        if (stmts[i]->type == STMT_IMPORT_FFI) {
            has_ffi = 1;
            break;
        }
    }
    // Also check modules for FFI imports
    if (!has_ffi && ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod && !has_ffi) {
            for (int i = 0; i < mod->num_statements; i++) {
                if (mod->statements[i]->type == STMT_IMPORT_FFI) {
                    has_ffi = 1;
                    break;
                }
            }
            mod = mod->next;
        }
    }
    if (!has_ffi && all_extern_fns.count > 0) {
        has_ffi = 1;
    }
    if (has_ffi) {
        codegen_write(ctx, "// FFI globals\n");
        codegen_write(ctx, "static HmlValue _ffi_lib = {0};\n");
        for (int i = 0; i < all_extern_fns.count; i++) {
            codegen_write(ctx, "static void *_ffi_ptr_%s = NULL;\n",
                        all_extern_fns.stmts[i]->as.extern_fn.function_name);
        }
        codegen_write(ctx, "\n");
    }

    // Track declared static globals to avoid C redefinition errors
    // (when Hemlock code redeclares a variable - that's a semantic error caught elsewhere)
    char **declared_statics = NULL;
    int num_declared_statics = 0;
    int declared_statics_capacity = 0;

    // Helper macro to check if variable was already declared
    #define IS_STATIC_DECLARED(name) ({ \
        int found = 0; \
        for (int j = 0; j < num_declared_statics; j++) { \
            if (strcmp(declared_statics[j], (name)) == 0) { found = 1; break; } \
        } \
        found; \
    })
    #define ADD_STATIC_DECLARED(name) do { \
        if (num_declared_statics >= declared_statics_capacity) { \
            declared_statics_capacity = declared_statics_capacity == 0 ? 16 : declared_statics_capacity * 2; \
            declared_statics = realloc(declared_statics, declared_statics_capacity * sizeof(char*)); \
        } \
        declared_statics[num_declared_statics++] = strdup(name); \
    } while(0)

    // Static global for built-in 'args' array (command-line arguments)
    codegen_write(ctx, "// Built-in globals\n");
    codegen_write(ctx, "static HmlValue _main_args = {0};\n\n");
    ADD_STATIC_DECLARED("args");

    // Static globals for top-level function variables (so closures can access them)
    // Use _main_ prefix to avoid C name conflicts (e.g., kill, exit, fork)
    // Note: main_vars are already collected in the pre-pass above
    int has_toplevel_funcs = 0;
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            if (!IS_STATIC_DECLARED(name)) {
                if (!has_toplevel_funcs) {
                    codegen_write(ctx, "// Top-level function variables (static for closure access)\n");
                    has_toplevel_funcs = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", name);
                ADD_STATIC_DECLARED(name);
            }
        }
    }
    if (has_toplevel_funcs) {
        codegen_write(ctx, "\n");
    }

    // Static globals for top-level const and let declarations (so functions can access them)
    // Use _main_ prefix to avoid C name conflicts
    // Note: main_vars are already collected in the pre-pass above
    int has_toplevel_vars = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        if (stmt->type == STMT_CONST) {
            if (!IS_STATIC_DECLARED(stmt->as.const_stmt.name)) {
                if (!has_toplevel_vars) {
                    codegen_write(ctx, "// Top-level variables (static for function access)\n");
                    has_toplevel_vars = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.const_stmt.name);
                ADD_STATIC_DECLARED(stmt->as.const_stmt.name);
            }
        } else if (stmt->type == STMT_LET) {
            // Check if this is NOT a function definition (those are handled above)
            char *name;
            Expr *func;
            if (!is_function_def(stmt, &name, &func)) {
                if (!IS_STATIC_DECLARED(stmt->as.let.name)) {
                    if (!has_toplevel_vars) {
                        codegen_write(ctx, "// Top-level variables (static for function access)\n");
                        has_toplevel_vars = 1;
                    }
                    codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.let.name);
                    ADD_STATIC_DECLARED(stmt->as.let.name);
                }
            }
        }
    }
    if (has_toplevel_vars) {
        codegen_write(ctx, "\n");
    }

    // Static globals for top-level enum declarations (so functions can access them)
    int has_toplevel_enums = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        if (stmt->type == STMT_ENUM) {
            if (!IS_STATIC_DECLARED(stmt->as.enum_decl.name)) {
                if (!has_toplevel_enums) {
                    codegen_write(ctx, "// Top-level enum declarations (static for function access)\n");
                    has_toplevel_enums = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.enum_decl.name);
                ADD_STATIC_DECLARED(stmt->as.enum_decl.name);
            }
        }
    }
    if (has_toplevel_enums) {
        codegen_write(ctx, "\n");
    }

    // Static globals for namespace imports in main file (import * as name)
    // Note: star imports (import * from) have namespace_name=NULL and don't need static vars
    int has_namespace_imports = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace &&
            stmt->as.import_stmt.namespace_name != NULL) {
            if (!IS_STATIC_DECLARED(stmt->as.import_stmt.namespace_name)) {
                if (!has_namespace_imports) {
                    codegen_write(ctx, "// Namespace import variables (static for function access)\n");
                    has_namespace_imports = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.import_stmt.namespace_name);
                ADD_STATIC_DECLARED(stmt->as.import_stmt.namespace_name);
            }
        }
    }
    if (has_namespace_imports) {
        codegen_write(ctx, "\n");
    }

    // Clean up helper macros and memory
    #undef IS_STATIC_DECLARED
    #undef ADD_STATIC_DECLARED
    for (int j = 0; j < num_declared_statics; j++) {
        free(declared_statics[j]);
    }
    free(declared_statics);

    // Generate closure implementations to a buffer first (this may create nested closures)
    MemBuffer *closure_buffer = membuf_new();
    FILE *saved_for_closures = ctx->output;
    ctx->output = closure_buffer->stream;

    // Iteratively generate closures until no new ones are created
    // This handles nested closures (functions inside functions)
    ClosureInfo *processed_tail = NULL;
    while (ctx->closures != processed_tail) {
        ClosureInfo *c = ctx->closures;
        while (c != processed_tail) {
            // Find the last one before processed_tail to process in order
            ClosureInfo *to_process = c;
            while (to_process->next != processed_tail) {
                to_process = to_process->next;
            }
            codegen_closure_impl(ctx, to_process);
            if (processed_tail == NULL) {
                processed_tail = to_process;
            } else {
                // Move processed_tail backward
                ClosureInfo *prev = ctx->closures;
                while (prev->next != processed_tail) {
                    prev = prev->next;
                }
                processed_tail = prev;
            }
            // Re-check from start in case new closures were prepended
            c = ctx->closures;
        }
    }
    ctx->output = saved_for_closures;

    // Now generate forward declarations for ALL closures (including nested ones)
    if (ctx->closures) {
        codegen_write(ctx, "// Closure forward declarations\n");
        ClosureInfo *c = ctx->closures;
        while (c) {
            Expr *func = c->func_expr;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", c->func_name);
            for (int i = 0; i < func->as.function.num_params; i++) {
                char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
                int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[i];
                if (is_ref) {
                    codegen_write(ctx, ", HmlValue *%s", safe_param);
                } else {
                    codegen_write(ctx, ", HmlValue %s", safe_param);
                }
                free(safe_param);
            }
            // Add rest param to forward declaration
            if (func->as.function.rest_param) {
                char *safe_rest = codegen_sanitize_ident(func->as.function.rest_param);
                codegen_write(ctx, ", HmlValue %s", safe_rest);
                free(safe_rest);
            }
            codegen_write(ctx, ");\n");
            c = c->next;
        }
        codegen_write(ctx, "\n");
    }

    // Module global variables and forward declarations
    if (ctx->module_cache && ctx->module_cache->modules) {
        codegen_write(ctx, "// Module global variables\n");
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            // Generate global variable for each export
            for (int i = 0; i < mod->num_exports; i++) {
                codegen_write(ctx, "static HmlValue %s = {0};\n", mod->exports[i].mangled_name);
            }
            // Also generate global variables for non-exported (private) variables
            for (int i = 0; i < mod->num_statements; i++) {
                Stmt *stmt = mod->statements[i];
                // Skip exports (already handled above)
                if (stmt->type == STMT_EXPORT) continue;
                // Check if it's a namespace import (import * as name)
                // Star imports (import * from) have namespace_name=NULL and don't need static vars
                if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace &&
                    stmt->as.import_stmt.namespace_name != NULL) {
                    codegen_write(ctx, "static HmlValue %s%s = {0};\n",
                                mod->module_prefix, stmt->as.import_stmt.namespace_name);
                }
                // Check if it's a private const
                if (stmt->type == STMT_CONST) {
                    // Skip if already in exports (to avoid duplicate declaration)
                    if (module_find_export(mod, stmt->as.const_stmt.name)) continue;
                    codegen_write(ctx, "static HmlValue %s%s = {0};\n",
                                mod->module_prefix, stmt->as.const_stmt.name);
                }
                // Check if it's a private let (function or not)
                if (stmt->type == STMT_LET) {
                    // Skip if already in exports (to avoid duplicate declaration)
                    if (module_find_export(mod, stmt->as.let.name)) continue;
                    codegen_write(ctx, "static HmlValue %s%s = {0};\n",
                                mod->module_prefix, stmt->as.let.name);
                }
            }
            mod = mod->next;
        }
        codegen_write(ctx, "\n");

        // Module function forward declarations (from buffer)
        codegen_write(ctx, "// Module function forward declarations\n");
        membuf_flush_to(module_decl_buffer, ctx->output);
        codegen_write(ctx, "\n");

        // Module init function forward declarations
        codegen_write(ctx, "// Module init function declarations\n");
        mod = ctx->module_cache->modules;
        while (mod) {
            codegen_write(ctx, "static void %sinit(void);\n", mod->module_prefix);
            mod = mod->next;
        }
        codegen_write(ctx, "\n");
    }

    // Forward declarations for named functions
    codegen_write(ctx, "// Named function forward declarations\n");
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            // All functions use closure env as first param for uniform calling convention
            codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", name);
            for (int j = 0; j < func->as.function.num_params; j++) {
                char *safe_param = codegen_sanitize_ident(func->as.function.param_names[j]);
                int is_ref = func->as.function.param_is_ref && func->as.function.param_is_ref[j];
                if (is_ref) {
                    codegen_write(ctx, ", HmlValue *%s", safe_param);
                } else {
                    codegen_write(ctx, ", HmlValue %s", safe_param);
                }
                free(safe_param);
            }
            // Add rest param to forward declaration
            if (func->as.function.rest_param) {
                char *safe_rest = codegen_sanitize_ident(func->as.function.rest_param);
                codegen_write(ctx, ", HmlValue %s", safe_rest);
                free(safe_rest);
            }
            codegen_write(ctx, ");\n");
        }
    }
    // Forward declarations for extern functions (including from block scopes)
    for (int i = 0; i < all_extern_fns.count; i++) {
        const char *fn_name = all_extern_fns.stmts[i]->as.extern_fn.function_name;
        int num_params = all_extern_fns.stmts[i]->as.extern_fn.num_params;
        codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", fn_name);
        for (int j = 0; j < num_params; j++) {
            codegen_write(ctx, ", HmlValue _arg%d", j);
        }
        codegen_write(ctx, ");\n");
    }
    codegen_write(ctx, "\n");

    // Output closure implementations from buffer
    if (ctx->closures) {
        codegen_write(ctx, "// Closure implementations\n");
        membuf_flush_to(closure_buffer, ctx->output);
    }
    membuf_free(closure_buffer);

    // FFI extern function wrapper implementations (including from block scopes)
    for (int i = 0; i < all_extern_fns.count; i++) {
        Stmt *stmt = all_extern_fns.stmts[i];
        const char *fn_name = stmt->as.extern_fn.function_name;
        int num_params = stmt->as.extern_fn.num_params;
        Type *return_type = stmt->as.extern_fn.return_type;
        int uses_structs = extern_fn_uses_structs(stmt);

        codegen_write(ctx, "// FFI wrapper for %s\n", fn_name);
        codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_env", fn_name);
        for (int j = 0; j < num_params; j++) {
            codegen_write(ctx, ", HmlValue _arg%d", j);
        }
        codegen_write(ctx, ") {\n");
        codegen_write(ctx, "    (void)_env;\n");
        codegen_write(ctx, "    if (!_ffi_ptr_%s) {\n", fn_name);
        codegen_write(ctx, "        _ffi_ptr_%s = hml_ffi_sym(_ffi_lib, \"%s\");\n", fn_name, fn_name);
        codegen_write(ctx, "        if (!_ffi_ptr_%s) {\n", fn_name);
        codegen_write(ctx, "            hml_runtime_error(\"FFI function '%%s' not found in library\", \"%s\");\n", fn_name);
        codegen_write(ctx, "        }\n");
        codegen_write(ctx, "    }\n");
        codegen_write(ctx, "    HmlFFIType _types[%d];\n", num_params + 1);

        // Return type
        const char *ret_str = return_type ? type_kind_to_ffi_type(return_type->kind) : "HML_FFI_VOID";
        codegen_write(ctx, "    _types[0] = %s;\n", ret_str);

        // Parameter types
        for (int j = 0; j < num_params; j++) {
            Type *ptype = stmt->as.extern_fn.param_types[j];
            const char *type_str = ptype ? type_kind_to_ffi_type(ptype->kind) : "HML_FFI_I32";
            codegen_write(ctx, "    _types[%d] = %s;\n", j + 1, type_str);
        }

        // Generate struct names array if using structs
        if (uses_structs) {
            codegen_write(ctx, "    static const char *_struct_names[%d] = {\n", num_params + 1);
            // Return type struct name
            if (return_type && return_type->kind == TYPE_CUSTOM_OBJECT && return_type->type_name) {
                codegen_write(ctx, "        \"%s\"", return_type->type_name);
            } else {
                codegen_write(ctx, "        NULL");
            }
            // Param struct names
            for (int j = 0; j < num_params; j++) {
                Type *ptype = stmt->as.extern_fn.param_types[j];
                if (ptype && ptype->kind == TYPE_CUSTOM_OBJECT && ptype->type_name) {
                    codegen_write(ctx, ",\n        \"%s\"", ptype->type_name);
                } else {
                    codegen_write(ctx, ",\n        NULL");
                }
            }
            codegen_write(ctx, "\n    };\n");
        }

        if (num_params > 0) {
            codegen_write(ctx, "    HmlValue _args[%d];\n", num_params);
            for (int j = 0; j < num_params; j++) {
                codegen_write(ctx, "    _args[%d] = _arg%d;\n", j, j);
            }
            if (uses_structs) {
                codegen_write(ctx, "    return hml_ffi_call_with_structs(_ffi_ptr_%s, _args, %d, _types, _struct_names);\n", fn_name, num_params);
            } else {
                codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, _args, %d, _types);\n", fn_name, num_params);
            }
        } else {
            if (uses_structs) {
                codegen_write(ctx, "    return hml_ffi_call_with_structs(_ffi_ptr_%s, NULL, 0, _types, _struct_names);\n", fn_name);
            } else {
                codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, NULL, 0, _types);\n", fn_name);
            }
        }
        codegen_write(ctx, "}\n\n");
    }
    // Free the extern fn list and struct list
    free(all_extern_fns.stmts);
    for (int i = 0; i < ffi_structs.count; i++) {
        free(ffi_structs.structs[i].name);
    }
    free(ffi_structs.structs);

    // Module function implementations (from buffer)
    if (ctx->module_cache && ctx->module_cache->modules) {
        codegen_write(ctx, "// Module function implementations\n");
        membuf_flush_to(module_impl_buffer, ctx->output);

        // Module init function implementations
        codegen_write(ctx, "// Module init functions\n");
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_init(ctx, mod);
            mod = mod->next;
        }
    }
    membuf_free(module_decl_buffer);
    membuf_free(module_impl_buffer);

    // Named function implementations (from buffer)
    codegen_write(ctx, "// Named function implementations\n");
    membuf_flush_to(func_buffer, ctx->output);
    membuf_free(func_buffer);

    // Main function (from buffer)
    membuf_flush_to(main_buffer, ctx->output);
    membuf_free(main_buffer);
}

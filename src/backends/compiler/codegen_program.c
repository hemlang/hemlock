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
        codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);
    // Suppress unused parameter warning
    codegen_writeln(ctx, "(void)_closure_env;");

    // Save locals and defer state
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function
    int saved_in_function = ctx->in_function;
    ctx->in_function = 1;  // We're now inside a function

    // Reset closure env tracking to prevent cross-function pollution
    ctx->last_closure_env_id = -1;

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Apply default values for optional parameters
    // If a parameter with a default is null, assign the default
    if (func->as.function.param_defaults) {
        for (int i = 0; i < func->as.function.num_params; i++) {
            if (func->as.function.param_defaults[i]) {
                char *param_name = func->as.function.param_names[i];
                codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                codegen_indent_inc(ctx);
                char *default_val = codegen_expr(ctx, func->as.function.param_defaults[i]);
                codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                free(default_val);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
        }
    }

    // Track call depth for stack overflow detection (inline macro for speed)
    codegen_writeln(ctx, "HML_CALL_ENTER();");

    // Scan for all closures in the function body and set up a shared environment
    // This allows multiple closures within this function to share the same environment
    Scope *scan_scope = scope_new(NULL);
    for (int i = 0; i < func->as.function.num_params; i++) {
        scope_add_var(scan_scope, func->as.function.param_names[i]);
    }
    shared_env_clear(ctx);  // Clear any previous shared environment
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            scan_closures_stmt(ctx, func->as.function.body->as.block.statements[i], scan_scope);
        }
    } else {
        scan_closures_stmt(ctx, func->as.function.body, scan_scope);
    }
    scope_free(scan_scope);

    // If there are captured variables, create the shared environment
    if (ctx->shared_env_num_vars > 0) {
        char env_name[64];
        snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
        ctx->shared_env_name = strdup(env_name);
        codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                      env_name, ctx->shared_env_num_vars);
    }

    // Generate body
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }

    // Execute any remaining defers before implicit return
    codegen_defer_execute_all(ctx);

    // Execute any runtime defers (from loops)
    codegen_writeln(ctx, "hml_defer_execute_all();");

    // Decrement call depth before implicit return
    codegen_writeln(ctx, "HML_CALL_EXIT();");

    // Default return null
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore locals, defer state, in_function flag, and clear shared environment
    codegen_defer_clear(ctx);
    ctx->defer_stack = saved_defer_stack;
    ctx->num_locals = saved_num_locals;
    ctx->in_function = saved_in_function;
    shared_env_clear(ctx);
}

// Generate a closure function (takes environment as first hidden parameter)
void codegen_closure_impl(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate function signature with environment parameter
    codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", closure->func_name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);

    // Save locals, defer state, module context, current closure, and in_function flag
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = closure->source_module;  // Restore module context for function resolution
    ClosureInfo *saved_closure = ctx->current_closure;
    ctx->current_closure = closure;  // Track current closure for mutable captured variables
    int saved_in_function = ctx->in_function;
    ctx->in_function = 1;  // We're now inside a function

    // Reset closure env tracking to prevent cross-function pollution
    ctx->last_closure_env_id = -1;

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Extract captured variables from environment
    for (int i = 0; i < closure->num_captured; i++) {
        const char *var_name = closure->captured_vars[i];

        // Check if this is a module-level export (self-reference like Set calling Set)
        int is_module_export = 0;
        if (closure->source_module) {
            ExportedSymbol *exp = module_find_export(closure->source_module, var_name);
            if (exp) {
                is_module_export = 1;
                codegen_writeln(ctx, "HmlValue %s = %s;", var_name, exp->mangled_name);
            }
        }

        if (!is_module_export) {
            // Use shared_env_indices if available (shared environment), otherwise use local ordering
            int env_index = closure->shared_env_indices ? closure->shared_env_indices[i] : i;

            if (env_index == -1) {
                // Variable not in shared environment - check if it's a main file variable
                if (codegen_is_main_var(ctx, var_name)) {
                    // Main file function/variable - use _main_ prefix
                    codegen_writeln(ctx, "HmlValue %s = _main_%s;", var_name, var_name);
                } else {
                    // Fallback - just use the variable name directly (might be a global or builtin)
                    codegen_writeln(ctx, "HmlValue %s = %s;", var_name, var_name);
                }
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_closure_env_get(_closure_env, %d);",
                              var_name, env_index);
            }
        }
        codegen_add_local(ctx, var_name);
    }

    // Apply default values for optional parameters
    if (func->as.function.param_defaults) {
        for (int i = 0; i < func->as.function.num_params; i++) {
            if (func->as.function.param_defaults[i]) {
                char *param_name = func->as.function.param_names[i];
                codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                codegen_indent_inc(ctx);
                char *default_val = codegen_expr(ctx, func->as.function.param_defaults[i]);
                codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                free(default_val);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
        }
    }

    // Track call depth for stack overflow detection (inline macro for speed)
    codegen_writeln(ctx, "HML_CALL_ENTER();");

    // Scan for all closures in the function body and set up a shared environment
    // This allows multiple closures within this function to share the same environment
    Scope *scan_scope = scope_new(NULL);
    for (int i = 0; i < func->as.function.num_params; i++) {
        scope_add_var(scan_scope, func->as.function.param_names[i]);
    }
    // Add captured variables to scan scope (they're local to this closure)
    for (int i = 0; i < closure->num_captured; i++) {
        scope_add_var(scan_scope, closure->captured_vars[i]);
    }
    shared_env_clear(ctx);  // Clear any previous shared environment
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            scan_closures_stmt(ctx, func->as.function.body->as.block.statements[i], scan_scope);
        }
    } else {
        scan_closures_stmt(ctx, func->as.function.body, scan_scope);
    }
    scope_free(scan_scope);

    // If there are captured variables, create the shared environment
    if (ctx->shared_env_num_vars > 0) {
        char env_name[64];
        snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
        ctx->shared_env_name = strdup(env_name);
        codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                      env_name, ctx->shared_env_num_vars);
    }

    // Generate body
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }

    // Execute any remaining defers before implicit return
    codegen_defer_execute_all(ctx);

    // Execute any runtime defers (from loops)
    codegen_writeln(ctx, "hml_defer_execute_all();");

    // Release captured variables before default return
    for (int i = 0; i < closure->num_captured; i++) {
        codegen_writeln(ctx, "hml_release(&%s);", closure->captured_vars[i]);
    }

    // Decrement call depth before implicit return
    codegen_writeln(ctx, "HML_CALL_EXIT();");

    // Default return null
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore locals, defer state, module context, current closure, in_function flag, and clear shared environment
    codegen_defer_clear(ctx);
    ctx->defer_stack = saved_defer_stack;
    ctx->num_locals = saved_num_locals;
    ctx->current_module = saved_module;
    ctx->current_closure = saved_closure;
    ctx->in_function = saved_in_function;
    shared_env_clear(ctx);  // Clear shared environment after generating this closure
}

// Generate wrapper function for closure (to match function pointer signature)
void codegen_closure_wrapper(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate wrapper that extracts env from function value and calls real implementation
    codegen_write(ctx, "HmlValue %s_wrapper(HmlValue *_args, int _nargs, void *_env) {\n", closure->func_name);
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "HmlClosureEnv *_closure_env = (HmlClosureEnv*)_env;");

    // Call the actual closure function
    codegen_write(ctx, "");
    codegen_indent(ctx);
    fprintf(ctx->output, "return %s(_closure_env", closure->func_name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        fprintf(ctx->output, ", _args[%d]", i);
    }
    fprintf(ctx->output, ");\n");

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
            char mangled[256];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
            int num_required = count_required_params(func->as.function.param_defaults, func->as.function.num_params);
            codegen_writeln(ctx, "%s = hml_val_function((void*)%sfn_%s, %d, %d, %d);",
                          mangled, module->module_prefix, name,
                          func->as.function.num_params, num_required, func->as.function.is_async);
        } else if (stmt->type == STMT_LET && stmt->as.let.value) {
            // Non-function let statement - assign to module global
            char mangled[256];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, stmt->as.let.name);
            char *value = codegen_expr(ctx, stmt->as.let.value);
            codegen_writeln(ctx, "%s = %s;", mangled, value);
            free(value);
        } else if (stmt->type == STMT_CONST && stmt->as.const_stmt.value) {
            // Const statement - assign to module global
            char mangled[256];
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
void codegen_module_funcs(CodegenContext *ctx, CompiledModule *module, FILE *decl_buffer, FILE *impl_buffer) {
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
            char mangled_fn[256];
            snprintf(mangled_fn, sizeof(mangled_fn), "%sfn_%s", module->module_prefix, name);

            // Generate forward declaration
            ctx->output = decl_buffer;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ");\n");

            // Generate implementation
            ctx->output = impl_buffer;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ") {\n");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "(void)_closure_env;");

            // Save and reset locals
            int saved_num_locals = ctx->num_locals;
            DeferEntry *saved_defer_stack = ctx->defer_stack;
            ctx->defer_stack = NULL;

            // Reset closure env tracking to prevent cross-function pollution
            ctx->last_closure_env_id = -1;

            // Add parameters as locals
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_add_local(ctx, func->as.function.param_names[j]);
            }

            // Apply default values for optional parameters
            if (func->as.function.param_defaults) {
                for (int j = 0; j < func->as.function.num_params; j++) {
                    if (func->as.function.param_defaults[j]) {
                        char *param_name = func->as.function.param_names[j];
                        codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                        codegen_indent_inc(ctx);
                        char *default_val = codegen_expr(ctx, func->as.function.param_defaults[j]);
                        codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                        free(default_val);
                        codegen_indent_dec(ctx);
                        codegen_writeln(ctx, "}");
                    }
                }
            }

            // Scan for all closures in the function body and set up a shared environment
            // This allows multiple closures to share the same environment for captured variables
            Scope *scan_scope = scope_new(NULL);
            for (int j = 0; j < func->as.function.num_params; j++) {
                scope_add_var(scan_scope, func->as.function.param_names[j]);
            }
            shared_env_clear(ctx);  // Clear any previous shared environment
            if (func->as.function.body->type == STMT_BLOCK) {
                for (int j = 0; j < func->as.function.body->as.block.count; j++) {
                    scan_closures_stmt(ctx, func->as.function.body->as.block.statements[j], scan_scope);
                }
            } else {
                scan_closures_stmt(ctx, func->as.function.body, scan_scope);
            }
            scope_free(scan_scope);

            // If there are captured variables, create the shared environment
            if (ctx->shared_env_num_vars > 0) {
                char env_name[64];
                snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
                ctx->shared_env_name = strdup(env_name);
                codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                              env_name, ctx->shared_env_num_vars);
            }

            // Generate body
            if (func->as.function.body->type == STMT_BLOCK) {
                for (int j = 0; j < func->as.function.body->as.block.count; j++) {
                    codegen_stmt(ctx, func->as.function.body->as.block.statements[j]);
                }
            } else {
                codegen_stmt(ctx, func->as.function.body);
            }

            // Execute any remaining defers before implicit return
            codegen_defer_execute_all(ctx);

            // Default return null
            codegen_writeln(ctx, "return hml_val_null();");

            // Restore locals, defer state, and clear shared environment
            codegen_defer_clear(ctx);
            ctx->defer_stack = saved_defer_stack;
            ctx->num_locals = saved_num_locals;
            shared_env_clear(ctx);

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
    }
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

    // Buffer for named function implementations
    FILE *func_buffer = tmpfile();
    FILE *main_buffer = tmpfile();
    FILE *module_decl_buffer = tmpfile();
    FILE *module_impl_buffer = tmpfile();
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
            codegen_add_main_func(ctx, name, func->as.function.num_params);  // Also track as function definition with param count
        } else if (stmt->type == STMT_CONST) {
            codegen_add_main_var(ctx, stmt->as.const_stmt.name);
            codegen_add_const(ctx, stmt->as.const_stmt.name);
        } else if (stmt->type == STMT_LET) {
            codegen_add_main_var(ctx, stmt->as.let.name);
        } else if (stmt->type == STMT_ENUM) {
            codegen_add_main_var(ctx, stmt->as.enum_decl.name);
        } else if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace) {
            // Track namespace imports as main vars so they get _main_ prefix
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
                                codegen_add_main_import(ctx, local_name, import_name, mod->module_prefix, is_function, num_params);
                            }
                        }
                    }
                    free(resolved);
                }
            }
        }
    }

    // Generate module functions first (to collect closures)
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_funcs(ctx, mod, module_decl_buffer, module_impl_buffer);
            mod = mod->next;
        }
    }

    // Pass 1: Generate named function bodies to buffer (this collects closures)
    ctx->output = func_buffer;
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_function_decl(ctx, func, name);
        }
    }

    // Pass 2: Generate main function body to buffer (this collects more closures)
    ctx->output = main_buffer;
    codegen_write(ctx, "int main(int argc, char **argv) {\n");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "hml_runtime_init(argc, argv);");
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
                    // Primitive type annotation: let x: i64 = 0;
                    // Convert value to the annotated type with range checking
                    const char *hml_type = NULL;
                    switch (stmt->as.let.type_annotation->kind) {
                        case TYPE_I8:    hml_type = "HML_VAL_I8"; break;
                        case TYPE_I16:   hml_type = "HML_VAL_I16"; break;
                        case TYPE_I32:   hml_type = "HML_VAL_I32"; break;
                        case TYPE_I64:   hml_type = "HML_VAL_I64"; break;
                        case TYPE_U8:    hml_type = "HML_VAL_U8"; break;
                        case TYPE_U16:   hml_type = "HML_VAL_U16"; break;
                        case TYPE_U32:   hml_type = "HML_VAL_U32"; break;
                        case TYPE_U64:   hml_type = "HML_VAL_U64"; break;
                        case TYPE_F32:   hml_type = "HML_VAL_F32"; break;
                        case TYPE_F64:   hml_type = "HML_VAL_F64"; break;
                        case TYPE_BOOL:  hml_type = "HML_VAL_BOOL"; break;
                        case TYPE_STRING: hml_type = "HML_VAL_STRING"; break;
                        case TYPE_RUNE:  hml_type = "HML_VAL_RUNE"; break;
                        case TYPE_ARRAY: {
                            // Typed array: let arr: array<type> = [...]
                            Type *elem_type = stmt->as.let.type_annotation->element_type;
                            const char *arr_type = "HML_VAL_NULL";
                            if (elem_type) {
                                switch (elem_type->kind) {
                                    case TYPE_I8:    arr_type = "HML_VAL_I8"; break;
                                    case TYPE_I16:   arr_type = "HML_VAL_I16"; break;
                                    case TYPE_I32:   arr_type = "HML_VAL_I32"; break;
                                    case TYPE_I64:   arr_type = "HML_VAL_I64"; break;
                                    case TYPE_U8:    arr_type = "HML_VAL_U8"; break;
                                    case TYPE_U16:   arr_type = "HML_VAL_U16"; break;
                                    case TYPE_U32:   arr_type = "HML_VAL_U32"; break;
                                    case TYPE_U64:   arr_type = "HML_VAL_U64"; break;
                                    case TYPE_F32:   arr_type = "HML_VAL_F32"; break;
                                    case TYPE_F64:   arr_type = "HML_VAL_F64"; break;
                                    case TYPE_BOOL:  arr_type = "HML_VAL_BOOL"; break;
                                    case TYPE_STRING: arr_type = "HML_VAL_STRING"; break;
                                    case TYPE_RUNE:  arr_type = "HML_VAL_RUNE"; break;
                                    default: break;
                                }
                            }
                            codegen_writeln(ctx, "_main_%s = hml_validate_typed_array(%s, %s);",
                                          stmt->as.let.name, value, arr_type);
                            break;
                        }
                        default: break;
                    }
                    if (hml_type) {
                        codegen_writeln(ctx, "_main_%s = hml_convert_to_type(%s, %s);",
                                      stmt->as.let.name, value, hml_type);
                    } else if (stmt->as.let.type_annotation->kind != TYPE_ARRAY) {
                        codegen_writeln(ctx, "_main_%s = %s;", stmt->as.let.name, value);
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
    // Collect all extern fn declarations recursively (including from block scopes and modules)
    ExternFnList all_extern_fns = {NULL, 0, 0};
    collect_extern_fn_from_stmts(stmts, stmt_count, &all_extern_fns);

    // Also collect from imported modules
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            collect_extern_fn_from_stmts(mod->statements, mod->num_statements, &all_extern_fns);
            mod = mod->next;
        }
    }

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
    int has_namespace_imports = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace) {
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
    FILE *closure_buffer = tmpfile();
    FILE *saved_for_closures = ctx->output;
    ctx->output = closure_buffer;

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
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
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
                if (stmt->type == STMT_IMPORT && stmt->as.import_stmt.is_namespace) {
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
        rewind(module_decl_buffer);
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), module_decl_buffer)) > 0) {
            fwrite(buf, 1, n, ctx->output);
        }
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
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
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
        rewind(closure_buffer);
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), closure_buffer)) > 0) {
            fwrite(buf, 1, n, ctx->output);
        }
    }
    fclose(closure_buffer);

    // FFI extern function wrapper implementations (including from block scopes)
    for (int i = 0; i < all_extern_fns.count; i++) {
        Stmt *stmt = all_extern_fns.stmts[i];
        const char *fn_name = stmt->as.extern_fn.function_name;
        int num_params = stmt->as.extern_fn.num_params;
        Type *return_type = stmt->as.extern_fn.return_type;

        codegen_write(ctx, "// FFI wrapper for %s\n", fn_name);
        codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_env", fn_name);
        for (int j = 0; j < num_params; j++) {
            codegen_write(ctx, ", HmlValue _arg%d", j);
        }
        codegen_write(ctx, ") {\n");
        codegen_write(ctx, "    (void)_env;\n");
        codegen_write(ctx, "    if (!_ffi_ptr_%s) {\n", fn_name);
        codegen_write(ctx, "        _ffi_ptr_%s = hml_ffi_sym(_ffi_lib, \"%s\");\n", fn_name, fn_name);
        codegen_write(ctx, "    }\n");
        codegen_write(ctx, "    HmlFFIType _types[%d];\n", num_params + 1);

        // Return type
        const char *ret_str = "HML_FFI_VOID";
        if (return_type) {
            switch (return_type->kind) {
                case TYPE_I8: ret_str = "HML_FFI_I8"; break;
                case TYPE_I16: ret_str = "HML_FFI_I16"; break;
                case TYPE_I32: ret_str = "HML_FFI_I32"; break;
                case TYPE_I64: ret_str = "HML_FFI_I64"; break;
                case TYPE_U8: ret_str = "HML_FFI_U8"; break;
                case TYPE_U16: ret_str = "HML_FFI_U16"; break;
                case TYPE_U32: ret_str = "HML_FFI_U32"; break;
                case TYPE_U64: ret_str = "HML_FFI_U64"; break;
                case TYPE_F32: ret_str = "HML_FFI_F32"; break;
                case TYPE_F64: ret_str = "HML_FFI_F64"; break;
                case TYPE_PTR: ret_str = "HML_FFI_PTR"; break;
                case TYPE_STRING: ret_str = "HML_FFI_STRING"; break;
                default: ret_str = "HML_FFI_I32"; break;
            }
        }
        codegen_write(ctx, "    _types[0] = %s;\n", ret_str);

        // Parameter types
        for (int j = 0; j < num_params; j++) {
            Type *ptype = stmt->as.extern_fn.param_types[j];
            const char *type_str = "HML_FFI_I32";
            if (ptype) {
                switch (ptype->kind) {
                    case TYPE_I8: type_str = "HML_FFI_I8"; break;
                    case TYPE_I16: type_str = "HML_FFI_I16"; break;
                    case TYPE_I32: type_str = "HML_FFI_I32"; break;
                    case TYPE_I64: type_str = "HML_FFI_I64"; break;
                    case TYPE_U8: type_str = "HML_FFI_U8"; break;
                    case TYPE_U16: type_str = "HML_FFI_U16"; break;
                    case TYPE_U32: type_str = "HML_FFI_U32"; break;
                    case TYPE_U64: type_str = "HML_FFI_U64"; break;
                    case TYPE_F32: type_str = "HML_FFI_F32"; break;
                    case TYPE_F64: type_str = "HML_FFI_F64"; break;
                    case TYPE_PTR: type_str = "HML_FFI_PTR"; break;
                    case TYPE_STRING: type_str = "HML_FFI_STRING"; break;
                    default: type_str = "HML_FFI_I32"; break;
                }
            }
            codegen_write(ctx, "    _types[%d] = %s;\n", j + 1, type_str);
        }

        if (num_params > 0) {
            codegen_write(ctx, "    HmlValue _args[%d];\n", num_params);
            for (int j = 0; j < num_params; j++) {
                codegen_write(ctx, "    _args[%d] = _arg%d;\n", j, j);
            }
            codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, _args, %d, _types);\n", fn_name, num_params);
        } else {
            codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, NULL, 0, _types);\n", fn_name);
        }
        codegen_write(ctx, "}\n\n");
    }
    // Free the extern fn list
    free(all_extern_fns.stmts);

    // Module function implementations (from buffer)
    if (ctx->module_cache && ctx->module_cache->modules) {
        codegen_write(ctx, "// Module function implementations\n");
        rewind(module_impl_buffer);
        char mbuf[4096];
        size_t mn;
        while ((mn = fread(mbuf, 1, sizeof(mbuf), module_impl_buffer)) > 0) {
            fwrite(mbuf, 1, mn, ctx->output);
        }

        // Module init function implementations
        codegen_write(ctx, "// Module init functions\n");
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_init(ctx, mod);
            mod = mod->next;
        }
    }
    fclose(module_decl_buffer);
    fclose(module_impl_buffer);

    // Named function implementations (from buffer)
    codegen_write(ctx, "// Named function implementations\n");
    rewind(func_buffer);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), func_buffer)) > 0) {
        fwrite(buf, 1, n, ctx->output);
    }
    fclose(func_buffer);

    // Main function (from buffer)
    rewind(main_buffer);
    while ((n = fread(buf, 1, sizeof(buf), main_buffer)) > 0) {
        fwrite(buf, 1, n, ctx->output);
    }
    fclose(main_buffer);
}

#include "internal.h"
#include "frontend/annotations.h"

// ========== ANNOTATION PARSING ==========

static void parse_annotation_arg_value(Parser *p, Annotation *a, const char *arg_name) {
    if (match(p, TOK_STRING)) {
        // String argument
        annotation_add_arg_string(a, arg_name, p->previous.string_value);
    } else if (match(p, TOK_NUMBER)) {
        // Number argument
        double value = p->previous.is_float ? p->previous.float_value : (double)p->previous.int_value;
        annotation_add_arg_number(a, arg_name, value);
    } else if (match(p, TOK_IDENT) || match(p, TOK_TRUE) || match(p, TOK_FALSE)) {
        // Identifier argument (including true/false as identifiers)
        char *ident = token_text(&p->previous);
        annotation_add_arg_ident(a, arg_name, ident);
        free(ident);
    } else {
        error(p, "Expect string, number, or identifier as annotation argument");
    }
}

static Annotation *parse_annotation(Parser *p) {
    consume(p, TOK_AT, "Expect '@'");

    Token name_token = p->current;
    char *name = consume_identifier_or_type_keyword(p, "Expect annotation name");

    Annotation *a = annotation_new(name, name_token.line, name_token.column);
    free(name);

    // Optional arguments
    if (match(p, TOK_LPAREN)) {
        if (!check(p, TOK_RPAREN)) {
            do {
                // Check for named argument: name = value
                if (check(p, TOK_IDENT) && p->next.type == TOK_EQUAL) {
                    char *arg_name = consume_identifier_or_type_keyword(p, "Expect argument name");
                    consume(p, TOK_EQUAL, "Expect '=' after argument name");
                    parse_annotation_arg_value(p, a, arg_name);
                    free(arg_name);
                } else {
                    // Positional argument
                    parse_annotation_arg_value(p, a, NULL);
                }
            } while (match(p, TOK_COMMA));
        }
        consume(p, TOK_RPAREN, "Expect ')' after annotation arguments");
    }

    return a;
}

static Annotation **parse_annotations(Parser *p, int *count) {
    Annotation **annotations = NULL;
    *count = 0;
    int capacity = 0;

    while (check(p, TOK_AT)) {
        if (*count >= capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2;
            Annotation **new_annotations = realloc(annotations, sizeof(Annotation*) * capacity);
            if (!new_annotations) {
                // Free existing annotations on failure
                for (int i = 0; i < *count; i++) {
                    annotation_free(annotations[i]);
                }
                free(annotations);
                error(p, "Memory allocation failed for annotations");
                *count = 0;
                return NULL;
            }
            annotations = new_annotations;
        }
        annotations[(*count)++] = parse_annotation(p);
    }

    return annotations;
}

// ========== STATEMENT PARSING ==========

Stmt* statement(Parser *p);
Stmt* extern_fn_statement(Parser *p);
Stmt* define_statement(Parser *p);

Stmt* let_statement(Parser *p) {
    char *name = consume_identifier_or_type_keyword(p, "Expect variable name");

    Type *type_annotation = NULL;

    // Check for optional type annotation
    if (match(p, TOK_COLON)) {
        type_annotation = parse_type(p);
    }

    consume(p, TOK_EQUAL, "Expect '=' after variable name");

    Expr *value = expression(p);

    consume(p, TOK_SEMICOLON, "Expect ';' after variable declaration");

    Stmt *stmt = stmt_let_typed(name, type_annotation, value);
    free(name);
    return stmt;
}

Stmt* const_statement(Parser *p) {
    char *name = consume_identifier_or_type_keyword(p, "Expect variable name");

    Type *type_annotation = NULL;

    // Check for optional type annotation
    if (match(p, TOK_COLON)) {
        type_annotation = parse_type(p);
    }

    consume(p, TOK_EQUAL, "Expect '=' after variable name");

    Expr *value = expression(p);

    consume(p, TOK_SEMICOLON, "Expect ';' after variable declaration");

    Stmt *stmt = stmt_const_typed(name, type_annotation, value);
    free(name);
    return stmt;
}

Stmt* block_statement(Parser *p) {
    int capacity = 256;
    Stmt **statements = malloc(sizeof(Stmt*) * capacity);
    if (!statements) {
        error(p, "Memory allocation failed for block statements");
        return stmt_block(NULL, 0);
    }
    int count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        // Grow array if needed
        if (count >= capacity) {
            capacity *= 2;
            Stmt **new_statements = realloc(statements, sizeof(Stmt*) * capacity);
            if (!new_statements) {
                error(p, "Memory allocation failed for block statements");
                // Return what we have so far
                return stmt_block(statements, count);
            }
            statements = new_statements;
        }
        statements[count++] = statement(p);
    }

    consume(p, TOK_RBRACE, "Expect '}' after block");

    return stmt_block(statements, count);
}

Stmt* if_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'if'");
    Expr *condition = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after condition");

    // Parse then branch - braces optional for single statements
    Stmt *then_branch;
    if (match(p, TOK_LBRACE)) {
        then_branch = block_statement(p);
    } else {
        then_branch = statement(p);
    }

    Stmt *else_branch = NULL;
    if (match(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) {
            // else if - recursively parse the if statement
            advance(p);  // consume the IF token
            else_branch = if_statement(p);
        } else if (match(p, TOK_LBRACE)) {
            // else with braces - parse block
            else_branch = block_statement(p);
        } else {
            // else without braces - parse single statement
            else_branch = statement(p);
        }
    }

    return stmt_if(condition, then_branch, else_branch);
}

Stmt* while_statement_with_label(Parser *p, const char *label) {
    consume(p, TOK_LPAREN, "Expect '(' after 'while'");
    Expr *condition = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after condition");

    // Parse body - braces optional for single statements
    Stmt *body;
    if (match(p, TOK_LBRACE)) {
        body = block_statement(p);
    } else {
        body = statement(p);
    }

    return stmt_while_labeled(label, condition, body);
}

Stmt* while_statement(Parser *p) {
    return while_statement_with_label(p, NULL);
}

Stmt* loop_statement_with_label(Parser *p, const char *label) {
    consume(p, TOK_LBRACE, "Expect '{' after 'loop'");
    Stmt *body = block_statement(p);

    return stmt_loop_labeled(label, body);
}

Stmt* loop_statement(Parser *p) {
    return loop_statement_with_label(p, NULL);
}

Stmt* switch_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'switch'");
    Expr *expr = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after switch expression");
    consume(p, TOK_LBRACE, "Expect '{' after switch expression");

    // Parse cases
    int case_capacity = 32;
    Expr **case_values = malloc(sizeof(Expr*) * case_capacity);
    Stmt **case_bodies = malloc(sizeof(Stmt*) * case_capacity);
    if (!case_values || !case_bodies) {
        free(case_values);
        free(case_bodies);
        error(p, "Memory allocation failed for switch cases");
        return stmt_switch(expr, NULL, NULL, 0);
    }
    int num_cases = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        // Grow case arrays if needed
        if (num_cases >= case_capacity) {
            case_capacity *= 2;
            Expr **new_case_values = realloc(case_values, sizeof(Expr*) * case_capacity);
            Stmt **new_case_bodies = realloc(case_bodies, sizeof(Stmt*) * case_capacity);
            if (!new_case_values || !new_case_bodies) {
                // Keep the original pointers if realloc failed
                if (new_case_values) case_values = new_case_values;
                if (new_case_bodies) case_bodies = new_case_bodies;
                error(p, "Memory allocation failed for switch cases");
                break;
            }
            case_values = new_case_values;
            case_bodies = new_case_bodies;
        }

        if (match(p, TOK_CASE)) {
            // Parse case value
            case_values[num_cases] = expression(p);
            consume(p, TOK_COLON, "Expect ':' after case value");

            // Parse case body statements until we hit another case/default/closing brace
            int stmt_capacity = 32;
            Stmt **statements = malloc(sizeof(Stmt*) * stmt_capacity);
            if (!statements) {
                error(p, "Memory allocation failed for case statements");
                break;
            }
            int count = 0;

            while (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                if (count >= stmt_capacity) {
                    stmt_capacity *= 2;
                    Stmt **new_statements = realloc(statements, sizeof(Stmt*) * stmt_capacity);
                    if (!new_statements) {
                        error(p, "Memory allocation failed for case statements");
                        // Use what we have so far
                        break;
                    }
                    statements = new_statements;
                }
                statements[count++] = statement(p);
            }

            case_bodies[num_cases] = stmt_block(statements, count);
            num_cases++;
        } else if (match(p, TOK_DEFAULT)) {
            consume(p, TOK_COLON, "Expect ':' after 'default'");

            // NULL value indicates default case
            case_values[num_cases] = NULL;

            // Parse default body statements
            int stmt_capacity = 32;
            Stmt **statements = malloc(sizeof(Stmt*) * stmt_capacity);
            if (!statements) {
                error(p, "Memory allocation failed for default statements");
                break;
            }
            int count = 0;

            while (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                if (count >= stmt_capacity) {
                    stmt_capacity *= 2;
                    Stmt **new_statements = realloc(statements, sizeof(Stmt*) * stmt_capacity);
                    if (!new_statements) {
                        error(p, "Memory allocation failed for default statements");
                        // Use what we have so far
                        break;
                    }
                    statements = new_statements;
                }
                statements[count++] = statement(p);
            }

            case_bodies[num_cases] = stmt_block(statements, count);
            num_cases++;
        } else {
            error(p, "Expect 'case' or 'default' in switch body");
            break;
        }
    }

    consume(p, TOK_RBRACE, "Expect '}' after switch body");
    return stmt_switch(expr, case_values, case_bodies, num_cases);
}

Stmt* for_statement_with_label(Parser *p, const char *label) {
    consume(p, TOK_LPAREN, "Expect '(' after 'for'");

    // Check if this is a for-in loop by looking ahead
    int is_for_in = 0;
    char *first_var = NULL;
    char *second_var = NULL;

    if (match(p, TOK_LET)) {
        first_var = consume_identifier_or_type_keyword(p, "Expect variable name");

        if (match(p, TOK_COMMA)) {
            // for (let key, value in ...)
            second_var = consume_identifier_or_type_keyword(p, "Expect second variable name");
            consume(p, TOK_IN, "Expect 'in' in for-in loop");
            is_for_in = 1;
        } else if (match(p, TOK_IN)) {
            // for (let value in ...)
            is_for_in = 1;
        }

        if (is_for_in) {
            // Parse for-in loop
            Expr *iterable = expression(p);
            consume(p, TOK_RPAREN, "Expect ')' after for-in");

            // Parse body - braces optional for single statements
            Stmt *body;
            if (match(p, TOK_LBRACE)) {
                body = block_statement(p);
            } else {
                body = statement(p);
            }

            if (second_var) {
                // Two variables: for (let key, value in ...)
                return stmt_for_in_labeled(label, first_var, second_var, iterable, body);
            } else {
                // One variable: for (let value in ...)
                return stmt_for_in_labeled(label, NULL, first_var, iterable, body);
            }
        }

        // Not for-in, continue parsing as C-style for loop
        // We already parsed "let identifier", need to finish the let statement
        Type *type = NULL;
        if (match(p, TOK_COLON)) {
            type = parse_type(p);
        }
        consume(p, TOK_EQUAL, "Expect '=' in for loop initializer");
        Expr *init_value = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after for loop initializer");

        Stmt *initializer = stmt_let_typed(first_var, type, init_value);
        free(first_var);

        // Parse condition
        Expr *condition = NULL;
        if (!check(p, TOK_SEMICOLON)) {
            condition = expression(p);
        }
        consume(p, TOK_SEMICOLON, "Expect ';' after condition");

        // Parse increment
        Expr *increment = NULL;
        if (!check(p, TOK_RPAREN)) {
            increment = expression(p);
        }
        consume(p, TOK_RPAREN, "Expect ')' after for clauses");

        // Parse body - braces optional for single statements
        Stmt *body;
        if (match(p, TOK_LBRACE)) {
            body = block_statement(p);
        } else {
            body = statement(p);
        }

        return stmt_for_labeled(label, initializer, condition, increment, body);
    }

    // Check for for-in loop without let (e.g., for (item in array))
    // Contextual keywords can be used as loop variables
    if (check_identifier_or_type_keyword(p)) {
        // Save state for potential backtracking
        Token saved_current = p->current;
        Token saved_previous = p->previous;
        const char *saved_lexer_start = p->lexer->start;
        const char *saved_lexer_current = p->lexer->current;
        const char *saved_lexer_line_start = p->lexer->line_start;
        int saved_lexer_line = p->lexer->line;

        // Look ahead: if we have "identifier in" or "identifier, identifier in", it's a for-in loop
        advance(p);  // consume identifier

        if (check(p, TOK_COMMA)) {
            // for (key, value in ...) - two variable form without let
            char *for_first_var = token_text(&saved_current);
            advance(p);  // consume comma
            char *for_second_var = consume_identifier_or_type_keyword(p, "Expect second variable name");
            consume(p, TOK_IN, "Expect 'in' in for-in loop");

            Expr *iterable = expression(p);
            consume(p, TOK_RPAREN, "Expect ')' after for-in");

            // Parse body - braces optional for single statements
            Stmt *body;
            if (match(p, TOK_LBRACE)) {
                body = block_statement(p);
            } else {
                body = statement(p);
            }

            // stmt_for_in takes ownership of the strings, don't free them
            return stmt_for_in_labeled(label, for_first_var, for_second_var, iterable, body);
        } else if (check(p, TOK_IN)) {
            // for (item in ...) - single variable form without let
            char *var_name = token_text(&saved_current);
            advance(p);  // consume 'in'

            Expr *iterable = expression(p);
            consume(p, TOK_RPAREN, "Expect ')' after for-in");

            // Parse body - braces optional for single statements
            Stmt *body;
            if (match(p, TOK_LBRACE)) {
                body = block_statement(p);
            } else {
                body = statement(p);
            }

            // stmt_for_in takes ownership of the string, don't free it
            return stmt_for_in_labeled(label, NULL, var_name, iterable, body);
        } else {
            // Not a for-in loop, restore state and fall through to C-style
            p->current = saved_current;
            p->previous = saved_previous;
            p->lexer->start = saved_lexer_start;
            p->lexer->current = saved_lexer_current;
            p->lexer->line_start = saved_lexer_line_start;
            p->lexer->line = saved_lexer_line;
        }
    }

    // C-style for loop without let (e.g., for (; i < 10; i = i + 1))
    Stmt *initializer = NULL;
    if (!check(p, TOK_SEMICOLON)) {
        Expr *init_expr = expression(p);
        initializer = stmt_expr(init_expr);
    }
    consume(p, TOK_SEMICOLON, "Expect ';' after initializer");

    Expr *condition = NULL;
    if (!check(p, TOK_SEMICOLON)) {
        condition = expression(p);
    }
    consume(p, TOK_SEMICOLON, "Expect ';' after condition");

    Expr *increment = NULL;
    if (!check(p, TOK_RPAREN)) {
        increment = expression(p);
    }
    consume(p, TOK_RPAREN, "Expect ')' after for clauses");

    // Parse body - braces optional for single statements
    Stmt *body;
    if (match(p, TOK_LBRACE)) {
        body = block_statement(p);
    } else {
        body = statement(p);
    }

    return stmt_for_labeled(label, initializer, condition, increment, body);
}

Stmt* for_statement(Parser *p) {
    return for_statement_with_label(p, NULL);
}

Stmt* expression_statement(Parser *p) {
    Expr *expr = expression(p);
    consume(p, TOK_SEMICOLON, "Expect ';' after expression");
    return stmt_expr(expr);
}

Stmt* return_statement(Parser *p) {
    Expr *value = NULL;

    // Check if there's a return value
    if (!check(p, TOK_SEMICOLON)) {
        value = expression(p);
    }

    consume(p, TOK_SEMICOLON, "Expect ';' after return statement");
    return stmt_return(value);
}

Stmt* import_statement(Parser *p) {
    // Check for FFI import: import "library.so"
    if (check(p, TOK_STRING)) {
        advance(p);
        char *library_path = p->previous.string_value;
        consume(p, TOK_SEMICOLON, "Expect ';' after FFI import");

        Stmt *stmt = stmt_import_ffi(library_path);
        free(library_path);
        return stmt;
    }

    // Check for star import: import * from "module" or import * as name from "module"
    if (match(p, TOK_STAR)) {
        // Check if this is a namespace import (import * as name) or a star import (import *)
        if (match_contextual(p, "as")) {
            // Namespace import: import * as name from "module"
            consume(p, TOK_IDENT, "Expect identifier for namespace name");
            char *namespace_name = token_text(&p->previous);

            consume_contextual(p, "from", "Expect 'from' in import statement");
            consume(p, TOK_STRING, "Expect module path string");
            char *module_path = p->previous.string_value;

            consume(p, TOK_SEMICOLON, "Expect ';' after import statement");

            Stmt *stmt = stmt_import_namespace(namespace_name, module_path);
            free(namespace_name);
            free(module_path);
            return stmt;
        } else {
            // Star import: import * from "module" (imports all exports into current scope)
            consume_contextual(p, "from", "Expect 'from' after '*' in import statement");
            consume(p, TOK_STRING, "Expect module path string");
            char *module_path = p->previous.string_value;

            consume(p, TOK_SEMICOLON, "Expect ';' after import statement");

            Stmt *stmt = stmt_import_star(module_path);
            free(module_path);
            return stmt;
        }
    }

    // Named imports: import { name1, name2 as alias } from "module"
    consume(p, TOK_LBRACE, "Expect '{', '*', or string after 'import'");

    int import_capacity = 32;
    char **import_names = malloc(sizeof(char*) * import_capacity);
    char **import_aliases = malloc(sizeof(char*) * import_capacity);
    if (!import_names || !import_aliases) {
        free(import_names);
        free(import_aliases);
        error(p, "Memory allocation failed for import names");
        return stmt_import_named(NULL, NULL, 0, NULL);
    }
    int num_imports = 0;

    // Parse import list - supports trailing commas: import { a, b, c, }
    do {
        // Allow trailing comma before closing brace
        if (check(p, TOK_RBRACE)) break;

        // Grow arrays if needed
        if (num_imports >= import_capacity) {
            import_capacity *= 2;
            char **new_import_names = realloc(import_names, sizeof(char*) * import_capacity);
            char **new_import_aliases = realloc(import_aliases, sizeof(char*) * import_capacity);
            if (!new_import_names || !new_import_aliases) {
                if (new_import_names) import_names = new_import_names;
                if (new_import_aliases) import_aliases = new_import_aliases;
                error(p, "Memory allocation failed for import names");
                break;
            }
            import_names = new_import_names;
            import_aliases = new_import_aliases;
        }
        consume(p, TOK_IDENT, "Expect import name");
        import_names[num_imports] = token_text(&p->previous);

        // Check for alias: name as alias
        if (match_contextual(p, "as")) {
            consume(p, TOK_IDENT, "Expect alias name after 'as'");
            import_aliases[num_imports] = token_text(&p->previous);
        } else {
            import_aliases[num_imports] = NULL;
        }

        num_imports++;
    } while (match(p, TOK_COMMA));

    consume(p, TOK_RBRACE, "Expect '}' after import list");
    consume_contextual(p, "from", "Expect 'from' in import statement");
    consume(p, TOK_STRING, "Expect module path string");
    char *module_path = p->previous.string_value;

    consume(p, TOK_SEMICOLON, "Expect ';' after import statement");

    Stmt *stmt = stmt_import_named(import_names, import_aliases, num_imports, module_path);
    free(module_path);
    return stmt;
}

Stmt* export_statement(Parser *p) {
    // Check for re-export: export { name1, name2 } from "module"
    if (match(p, TOK_LBRACE)) {
        int export_capacity = 32;
        char **export_names = malloc(sizeof(char*) * export_capacity);
        char **export_aliases = malloc(sizeof(char*) * export_capacity);
        if (!export_names || !export_aliases) {
            free(export_names);
            free(export_aliases);
            error(p, "Memory allocation failed for export names");
            return stmt_export_list(NULL, NULL, 0);
        }
        int num_exports = 0;

        // Parse export list - supports trailing commas: export { a, b, c, }
        do {
            // Allow trailing comma before closing brace
            if (check(p, TOK_RBRACE)) break;

            // Grow arrays if needed
            if (num_exports >= export_capacity) {
                export_capacity *= 2;
                char **new_export_names = realloc(export_names, sizeof(char*) * export_capacity);
                char **new_export_aliases = realloc(export_aliases, sizeof(char*) * export_capacity);
                if (!new_export_names || !new_export_aliases) {
                    if (new_export_names) export_names = new_export_names;
                    if (new_export_aliases) export_aliases = new_export_aliases;
                    error(p, "Memory allocation failed for export names");
                    break;
                }
                export_names = new_export_names;
                export_aliases = new_export_aliases;
            }
            consume(p, TOK_IDENT, "Expect export name");
            export_names[num_exports] = token_text(&p->previous);

            // Check for alias: name as alias
            if (match_contextual(p, "as")) {
                consume(p, TOK_IDENT, "Expect alias name after 'as'");
                export_aliases[num_exports] = token_text(&p->previous);
            } else {
                export_aliases[num_exports] = NULL;
            }

            num_exports++;
        } while (match(p, TOK_COMMA));

        consume(p, TOK_RBRACE, "Expect '}' after export list");

        // Check for re-export
        if (match_contextual(p, "from")) {
            consume(p, TOK_STRING, "Expect module path string");
            char *module_path = p->previous.string_value;
            consume(p, TOK_SEMICOLON, "Expect ';' after export statement");

            Stmt *stmt = stmt_export_reexport(export_names, export_aliases, num_exports, module_path);
            free(module_path);
            return stmt;
        } else {
            // Regular export list
            consume(p, TOK_SEMICOLON, "Expect ';' after export statement");
            return stmt_export_list(export_names, export_aliases, num_exports);
        }
    }

    // Export declaration: export fn/const/let
    if (match(p, TOK_CONST)) {
        Stmt *decl = const_statement(p);
        return stmt_export_declaration(decl);
    }

    if (match(p, TOK_LET)) {
        Stmt *decl = let_statement(p);
        return stmt_export_declaration(decl);
    }

    // Export extern function: export extern fn name(...)
    if (match(p, TOK_EXTERN)) {
        Stmt *extern_stmt = extern_fn_statement(p);
        return stmt_export_declaration(extern_stmt);
    }

    // Export define: export define TypeName { ... }
    if (match(p, TOK_DEFINE)) {
        Stmt *define_stmt = define_statement(p);
        return stmt_export_declaration(define_stmt);
    }

    // Named function: export fn name(...) or export async fn name(...)
    int is_async = 0;
    if (match(p, TOK_ASYNC)) {
        is_async = 1;
        consume(p, TOK_FN, "Expect 'fn' after 'async'");
    } else if (match(p, TOK_FN)) {
        is_async = 0;
    } else {
        error(p, "Expected declaration or export list after 'export'");
        return stmt_expr(expr_number(0));
    }

    // Must be a named function - contextual keywords can be used as function names
    char *name = consume_identifier_or_type_keyword(p, "Expect function name after 'export fn'");

    // Parse function (same as in statement())
    consume(p, TOK_LPAREN, "Expect '(' after function name");

    // Start with small capacity and grow as needed
    int param_capacity = 8;
    char **param_names = malloc(sizeof(char*) * param_capacity);
    Type **param_types = malloc(sizeof(Type*) * param_capacity);
    Expr **param_defaults = malloc(sizeof(Expr*) * param_capacity);
    int *param_is_ref = malloc(sizeof(int) * param_capacity);
    int *param_is_const = malloc(sizeof(int) * param_capacity);
    if (!param_names || !param_types || !param_defaults || !param_is_ref || !param_is_const) {
        free(param_names);
        free(param_types);
        free(param_defaults);
        free(param_is_ref);
        free(param_is_const);
        free(name);
        error(p, "Memory allocation failed for function parameters");
        return stmt_expr(expr_number(0));
    }
    int num_params = 0;
    int seen_optional = 0;
    char *rest_param = NULL;
    Type *rest_param_type = NULL;

    // Parse function parameters - supports trailing commas: fn name(a, b, c,)
    if (!check(p, TOK_RPAREN)) {
        do {
            // Allow trailing comma before closing paren
            if (check(p, TOK_RPAREN)) break;

            // Check for rest parameter: ...name
            if (match(p, TOK_DOT_DOT_DOT)) {
                rest_param = consume_identifier_or_type_keyword(p, "Expect parameter name after '...'");
                if (match(p, TOK_COLON)) {
                    rest_param_type = parse_type(p);
                }
                if (!check(p, TOK_RPAREN)) {
                    error_at(p, &p->current, "Rest parameter must be the last parameter");
                }
                break;
            }

            // Check parameter limit before adding
            if (num_params >= MAX_FUNCTION_PARAMS) {
                error_at(p, &p->current, "functions cannot have more than 64 parameters");
                break;
            }

            // Grow arrays if needed
            if (num_params >= param_capacity) {
                int new_capacity = param_capacity * 2;
                char **new_param_names = realloc(param_names, sizeof(char*) * new_capacity);
                Type **new_param_types = realloc(param_types, sizeof(Type*) * new_capacity);
                Expr **new_param_defaults = realloc(param_defaults, sizeof(Expr*) * new_capacity);
                int *new_param_is_ref = realloc(param_is_ref, sizeof(int) * new_capacity);
                int *new_param_is_const = realloc(param_is_const, sizeof(int) * new_capacity);
                if (!new_param_names || !new_param_types || !new_param_defaults || !new_param_is_ref || !new_param_is_const) {
                    if (new_param_names) param_names = new_param_names;
                    if (new_param_types) param_types = new_param_types;
                    if (new_param_defaults) param_defaults = new_param_defaults;
                    if (new_param_is_ref) param_is_ref = new_param_is_ref;
                    if (new_param_is_const) param_is_const = new_param_is_const;
                    error(p, "Memory allocation failed for function parameters");
                    break;
                }
                param_names = new_param_names;
                param_types = new_param_types;
                param_defaults = new_param_defaults;
                param_is_ref = new_param_is_ref;
                param_is_const = new_param_is_const;
                param_capacity = new_capacity;
            }

            // Check for const keyword (immutable parameter)
            int is_const = match(p, TOK_CONST);
            param_is_const[num_params] = is_const;

            // Check for ref keyword (pass-by-reference)
            int is_ref = match(p, TOK_REF);
            param_is_ref[num_params] = is_ref;

            // const and ref are mutually exclusive
            if (is_const && is_ref) {
                error_at(p, &p->current, "const and ref modifiers cannot be combined");
            }

            param_names[num_params] = consume_identifier_or_type_keyword(p, "Expect parameter name");

            if (match(p, TOK_COLON)) {
                param_types[num_params] = parse_type(p);
            } else {
                param_types[num_params] = NULL;
            }

            // Check for optional parameter
            if (match(p, TOK_QUESTION)) {
                if (is_ref) {
                    error_at(p, &p->current, "ref parameters cannot have default values");
                }
                consume(p, TOK_COLON, "Expect ':' after '?' for default value");
                param_defaults[num_params] = expression(p);
                seen_optional = 1;
            } else {
                if (seen_optional) {
                    error_at(p, &p->current, "Required parameters must come before optional parameters");
                }
                param_defaults[num_params] = NULL;
            }

            num_params++;
        } while (match(p, TOK_COMMA));
    }

    consume(p, TOK_RPAREN, "Expect ')' after parameters");

    // Optional return type
    Type *return_type = NULL;
    if (match(p, TOK_COLON)) {
        return_type = parse_type(p);
    }

    // Parse body
    consume(p, TOK_LBRACE, "Expect '{' before function body");
    Stmt *body = block_statement(p);

    // Create function expression
    Expr *fn_expr = expr_function(is_async, param_names, param_types, param_defaults, param_is_ref, param_is_const, num_params, rest_param, rest_param_type, return_type, body);

    // Create let statement
    Stmt *decl = stmt_let_typed(name, NULL, fn_expr);
    free(name);

    return stmt_export_declaration(decl);
}

Stmt* extern_fn_statement(Parser *p) {
    // extern fn name(param1: type1, param2: type2): return_type;
    consume(p, TOK_FN, "Expect 'fn' after 'extern'");
    char *function_name = consume_identifier_or_type_keyword(p, "Expect function name");

    consume(p, TOK_LPAREN, "Expect '(' after function name");

    // Parse extern function parameters - supports trailing commas
    int param_capacity = 32;
    char **param_names = malloc(sizeof(char*) * param_capacity);
    Type **param_types = malloc(sizeof(Type*) * param_capacity);
    if (!param_names || !param_types) {
        free(param_names);
        free(param_types);
        free(function_name);
        error(p, "Memory allocation failed for extern function parameters");
        return stmt_expr(expr_number(0));
    }
    int num_params = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            // Allow trailing comma before closing paren
            if (check(p, TOK_RPAREN)) break;

            // Grow array if needed
            if (num_params >= param_capacity) {
                param_capacity *= 2;
                char **new_param_names = realloc(param_names, sizeof(char*) * param_capacity);
                Type **new_param_types = realloc(param_types, sizeof(Type*) * param_capacity);
                if (!new_param_names || !new_param_types) {
                    if (new_param_names) param_names = new_param_names;
                    if (new_param_types) param_types = new_param_types;
                    error(p, "Memory allocation failed for extern function parameters");
                    break;
                }
                param_names = new_param_names;
                param_types = new_param_types;
            }
            // Parameter name is required for syntax and preserved for formatting
            // Contextual keywords can be used as parameter names
            char *param_name = consume_identifier_or_type_keyword(p, "Expect parameter name");
            param_names[num_params] = param_name;
            consume(p, TOK_COLON, "Expect ':' after parameter name in extern declaration");
            param_types[num_params] = parse_type(p);
            num_params++;
        } while (match(p, TOK_COMMA));
    }

    consume(p, TOK_RPAREN, "Expect ')' after parameters");

    // Parse return type (optional, defaults to void)
    Type *return_type = NULL;
    if (match(p, TOK_COLON)) {
        return_type = parse_type(p);
    }

    consume(p, TOK_SEMICOLON, "Expect ';' after extern declaration");

    Stmt *stmt = stmt_extern_fn(function_name, param_names, param_types, num_params, return_type);
    free(function_name);
    return stmt;
}

Stmt* define_statement(Parser *p) {
    // Object type definition: define TypeName { ... } or define TypeName<T, U> { ... }
    // Contextual keywords can be used as type names
    char *name = consume_identifier_or_type_keyword(p, "Expect object type name");

    // Parse optional type parameters: <T, U, ...> - supports trailing commas
    char **type_params = NULL;
    int num_type_params = 0;
    int type_param_capacity = 4;

    if (match(p, TOK_LESS)) {
        type_params = malloc(sizeof(char*) * type_param_capacity);
        if (!type_params) {
            free(name);
            error(p, "Memory allocation failed for type parameters");
            return stmt_expr(expr_number(0));
        }

        do {
            // Allow trailing comma before closing >
            if (check(p, TOK_GREATER)) break;

            consume(p, TOK_IDENT, "Expect type parameter name");
            if (num_type_params >= type_param_capacity) {
                type_param_capacity *= 2;
                char **new_type_params = realloc(type_params, sizeof(char*) * type_param_capacity);
                if (!new_type_params) {
                    error(p, "Memory allocation failed for type parameters");
                    break;
                }
                type_params = new_type_params;
            }
            type_params[num_type_params++] = token_text(&p->previous);
        } while (match(p, TOK_COMMA));

        consume(p, TOK_GREATER, "Expect '>' after type parameters");
    }

    // Set type parameters in parser scope for field type parsing
    char **old_type_params = p->type_params;
    int old_num_type_params = p->num_type_params;
    p->type_params = type_params;
    p->num_type_params = num_type_params;

    consume(p, TOK_LBRACE, "Expect '{' after type name");

    // Parse fields and methods
    int field_capacity = 32;
    char **field_names = malloc(sizeof(char*) * field_capacity);
    Type **field_types = malloc(sizeof(Type*) * field_capacity);
    int *field_optional = malloc(sizeof(int) * field_capacity);
    Expr **field_defaults = malloc(sizeof(Expr*) * field_capacity);
    if (!field_names || !field_types || !field_optional || !field_defaults) {
        free(field_names);
        free(field_types);
        free(field_optional);
        free(field_defaults);
        free(name);
        error(p, "Memory allocation failed for define fields");
        p->type_params = old_type_params;
        p->num_type_params = old_num_type_params;
        return stmt_expr(expr_number(0));
    }
    int num_fields = 0;

    int method_capacity = 16;
    char **method_names = malloc(sizeof(char*) * method_capacity);
    Type **method_types = malloc(sizeof(Type*) * method_capacity);
    int *method_optional = malloc(sizeof(int) * method_capacity);
    Expr **method_defaults = malloc(sizeof(Expr*) * method_capacity);
    if (!method_names || !method_types || !method_optional || !method_defaults) {
        free(method_names);
        free(method_types);
        free(method_optional);
        free(method_defaults);
        free(field_names);
        free(field_types);
        free(field_optional);
        free(field_defaults);
        free(name);
        error(p, "Memory allocation failed for define methods");
        p->type_params = old_type_params;
        p->num_type_params = old_num_type_params;
        return stmt_expr(expr_number(0));
    }
    int num_methods = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        // Check if this is a method definition (starts with 'fn')
        if (match(p, TOK_FN)) {
            // Grow method arrays if needed
            if (num_methods >= method_capacity) {
                int new_capacity = method_capacity * 2;
                char **new_method_names = realloc(method_names, sizeof(char*) * new_capacity);
                Type **new_method_types = realloc(method_types, sizeof(Type*) * new_capacity);
                int *new_method_optional = realloc(method_optional, sizeof(int) * new_capacity);
                Expr **new_method_defaults = realloc(method_defaults, sizeof(Expr*) * new_capacity);
                if (!new_method_names || !new_method_types || !new_method_optional || !new_method_defaults) {
                    if (new_method_names) method_names = new_method_names;
                    if (new_method_types) method_types = new_method_types;
                    if (new_method_optional) method_optional = new_method_optional;
                    if (new_method_defaults) method_defaults = new_method_defaults;
                    error(p, "Memory allocation failed for method arrays");
                    break;
                }
                method_names = new_method_names;
                method_types = new_method_types;
                method_optional = new_method_optional;
                method_defaults = new_method_defaults;
                method_capacity = new_capacity;
            }

            // Parse method name - contextual keywords can be used as method names
            method_names[num_methods] = consume_identifier_or_type_keyword(p, "Expect method name");

            // Check for optional marker (fn name?(): type means optional method)
            method_optional[num_methods] = match(p, TOK_QUESTION) ? 1 : 0;

            // Parse parameter types for the function type
            consume(p, TOK_LPAREN, "Expect '(' after method name");

            int fn_param_capacity = 8;
            Type **fn_param_types = malloc(sizeof(Type*) * fn_param_capacity);
            char **fn_param_names = malloc(sizeof(char*) * fn_param_capacity);
            int *fn_param_optional = malloc(sizeof(int) * fn_param_capacity);
            int *fn_param_is_const = malloc(sizeof(int) * fn_param_capacity);
            if (!fn_param_types || !fn_param_names || !fn_param_optional || !fn_param_is_const) {
                free(fn_param_types);
                free(fn_param_names);
                free(fn_param_optional);
                free(fn_param_is_const);
                error(p, "Memory allocation failed for method parameters");
                break;
            }
            int fn_num_params = 0;

            // Parse method parameters - supports trailing commas
            if (!check(p, TOK_RPAREN)) {
                do {
                    // Allow trailing comma before closing paren
                    if (check(p, TOK_RPAREN)) break;

                    if (fn_num_params >= fn_param_capacity) {
                        int new_capacity = fn_param_capacity * 2;
                        Type **new_fn_param_types = realloc(fn_param_types, sizeof(Type*) * new_capacity);
                        char **new_fn_param_names = realloc(fn_param_names, sizeof(char*) * new_capacity);
                        int *new_fn_param_optional = realloc(fn_param_optional, sizeof(int) * new_capacity);
                        int *new_fn_param_is_const = realloc(fn_param_is_const, sizeof(int) * new_capacity);
                        if (!new_fn_param_types || !new_fn_param_names || !new_fn_param_optional || !new_fn_param_is_const) {
                            if (new_fn_param_types) fn_param_types = new_fn_param_types;
                            if (new_fn_param_names) fn_param_names = new_fn_param_names;
                            if (new_fn_param_optional) fn_param_optional = new_fn_param_optional;
                            if (new_fn_param_is_const) fn_param_is_const = new_fn_param_is_const;
                            error(p, "Memory allocation failed for method parameters");
                            break;
                        }
                        fn_param_types = new_fn_param_types;
                        fn_param_names = new_fn_param_names;
                        fn_param_optional = new_fn_param_optional;
                        fn_param_is_const = new_fn_param_is_const;
                        fn_param_capacity = new_capacity;
                    }

                    // Parse const modifier
                    fn_param_is_const[fn_num_params] = match(p, TOK_CONST) ? 1 : 0;

                    // Parse parameter name - contextual keywords allowed
                    fn_param_names[fn_num_params] = consume_identifier_or_type_keyword(p, "Expect parameter name");

                    // Parse optional type annotation
                    if (match(p, TOK_COLON)) {
                        fn_param_types[fn_num_params] = parse_type(p);
                    } else {
                        fn_param_types[fn_num_params] = NULL;
                    }

                    // Check for optional parameter
                    fn_param_optional[fn_num_params] = 0;  // TODO: support optional params in method signatures

                    fn_num_params++;
                } while (match(p, TOK_COMMA));
            }

            consume(p, TOK_RPAREN, "Expect ')' after method parameters");

            // Parse return type
            Type *return_type = NULL;
            if (match(p, TOK_COLON)) {
                return_type = parse_type(p);
            }

            // Create the function type
            method_types[num_methods] = type_function(
                fn_param_types, fn_param_names, fn_param_optional, fn_param_is_const,
                fn_num_params, NULL, NULL, return_type, 0);

            // Check for default implementation (block) or just signature (semicolon)
            if (match(p, TOK_LBRACE)) {
                // Parse default implementation as anonymous function
                Stmt *body = block_statement(p);

                // Create function expression for the default
                // Duplicate param_names for the function expression
                char **default_param_names = malloc(sizeof(char*) * fn_num_params);
                for (int i = 0; i < fn_num_params; i++) {
                    default_param_names[i] = strdup(fn_param_names[i]);
                }

                method_defaults[num_methods] = expr_function(
                    0, default_param_names, fn_param_types, NULL,
                    NULL, fn_param_is_const, fn_num_params,
                    NULL, NULL, return_type, body);
            } else {
                // Just a signature, no default
                method_defaults[num_methods] = NULL;
            }

            num_methods++;

            // Comma-delimited like fields (TypeScript style)
            if (!match(p, TOK_COMMA)) break;
            continue;
        }

        // Otherwise, it's a field
        // Grow arrays if needed
        if (num_fields >= field_capacity) {
            int new_capacity = field_capacity * 2;
            char **new_field_names = realloc(field_names, sizeof(char*) * new_capacity);
            Type **new_field_types = realloc(field_types, sizeof(Type*) * new_capacity);
            int *new_field_optional = realloc(field_optional, sizeof(int) * new_capacity);
            Expr **new_field_defaults = realloc(field_defaults, sizeof(Expr*) * new_capacity);
            if (!new_field_names || !new_field_types || !new_field_optional || !new_field_defaults) {
                if (new_field_names) field_names = new_field_names;
                if (new_field_types) field_types = new_field_types;
                if (new_field_optional) field_optional = new_field_optional;
                if (new_field_defaults) field_defaults = new_field_defaults;
                error(p, "Memory allocation failed for field arrays");
                break;
            }
            field_names = new_field_names;
            field_types = new_field_types;
            field_optional = new_field_optional;
            field_defaults = new_field_defaults;
            field_capacity = new_capacity;
        }
        field_names[num_fields] = consume_identifier_or_type_keyword(p, "Expect field name");

        // Check for optional marker followed by colon (?: syntax)
        if (match(p, TOK_QUESTION)) {
            field_optional[num_fields] = 1;

            if (match(p, TOK_COLON)) {
                // ?: could mean:
                // 1. Optional with type: name?: string
                // 2. Optional with default: name?: true
                // Check if current token is a type keyword
                if (check(p, TOK_TYPE_I8) || check(p, TOK_TYPE_I16) || check(p, TOK_TYPE_I32) ||
                    check(p, TOK_TYPE_U8) || check(p, TOK_TYPE_U16) || check(p, TOK_TYPE_U32) ||
                    check(p, TOK_TYPE_F32) || check(p, TOK_TYPE_F64) ||
                    check(p, TOK_TYPE_INTEGER) || check(p, TOK_TYPE_NUMBER) || check(p, TOK_TYPE_BYTE) ||
                    check(p, TOK_TYPE_BOOL) || check(p, TOK_TYPE_STRING) || check(p, TOK_TYPE_RUNE) ||
                    check(p, TOK_TYPE_PTR) || check(p, TOK_TYPE_BUFFER) ||
                    check(p, TOK_OBJECT) || check(p, TOK_IDENT)) {
                    // It's a type
                    field_types[num_fields] = parse_type(p);
                    // Optional type with no default (defaults to null)
                    field_defaults[num_fields] = NULL;
                } else {
                    // It's a default value expression
                    field_types[num_fields] = NULL;  // infer type from default
                    field_defaults[num_fields] = expression(p);
                }
            } else {
                // Just ? with no :, means optional with no type or default
                field_types[num_fields] = NULL;
                field_defaults[num_fields] = NULL;
            }
        } else {
            // Not optional
            field_optional[num_fields] = 0;

            // Check for type annotation
            if (match(p, TOK_COLON)) {
                field_types[num_fields] = parse_type(p);
            } else {
                field_types[num_fields] = NULL;  // dynamic field
            }

            // Check for default value
            if (match(p, TOK_EQUAL)) {
                field_defaults[num_fields] = expression(p);
            } else {
                field_defaults[num_fields] = NULL;
            }
        }

        num_fields++;

        if (!match(p, TOK_COMMA)) break;
    }

    consume(p, TOK_RBRACE, "Expect '}' after fields");

    // Restore old type parameters scope
    p->type_params = old_type_params;
    p->num_type_params = old_num_type_params;

    Stmt *stmt = stmt_define_object(name, type_params, num_type_params,
                                   field_names, field_types,
                                   field_optional, field_defaults, num_fields,
                                   method_names, method_types,
                                   method_optional, method_defaults, num_methods);
    free(name);
    return stmt;
}

// Helper macro to set line number and return
#define RETURN_STMT(s) do { Stmt *_s = (s); _s->line = stmt_start_line; _s->column = stmt_start_column; return _s; } while(0)

Stmt* statement(Parser *p) {
    // Save the starting line/column for this statement
    int stmt_start_line = p->current.line;
    int stmt_start_column = p->current.column;

    // Parse annotations first (they come before the actual statement)
    int annotation_count = 0;
    Annotation **annotations = parse_annotations(p, &annotation_count);

    // After parsing annotations, the keyword line might be more accurate
    if (annotation_count > 0) {
        stmt_start_line = p->current.line;
        stmt_start_column = p->current.column;
    }

    if (match(p, TOK_LET)) {
        Stmt *stmt = let_statement(p);
        stmt->as.let.annotations = annotations;
        stmt->as.let.annotation_count = annotation_count;
        if (!validate_annotations(stmt)) {
            p->had_error = 1;
        }
        RETURN_STMT(stmt);
    }

    if (match(p, TOK_CONST)) {
        Stmt *stmt = const_statement(p);
        stmt->as.const_stmt.annotations = annotations;
        stmt->as.const_stmt.annotation_count = annotation_count;
        if (!validate_annotations(stmt)) {
            p->had_error = 1;
        }
        RETURN_STMT(stmt);
    }

    // Type alias: type Name = Type; or type Name<T> = Type<T>;
    if (match(p, TOK_TYPE)) {
        // Contextual keywords can be used as type alias names
        char *name = consume_identifier_or_type_keyword(p, "Expect type alias name");

        // Parse optional type parameters: <T, U, ...> - supports trailing commas
        int param_capacity = 4;
        char **type_params = NULL;
        int num_type_params = 0;

        if (match(p, TOK_LESS)) {
            type_params = malloc(sizeof(char*) * param_capacity);
            if (!type_params) {
                free(name);
                error(p, "Memory allocation failed for type parameters");
                RETURN_STMT(stmt_expr(expr_number(0)));
            }

            do {
                // Allow trailing comma before closing >
                if (check(p, TOK_GREATER)) break;

                if (num_type_params >= param_capacity) {
                    param_capacity *= 2;
                    char **new_type_params = realloc(type_params, sizeof(char*) * param_capacity);
                    if (!new_type_params) {
                        error(p, "Memory allocation failed for type parameters");
                        break;
                    }
                    type_params = new_type_params;
                }
                // Contextual keywords can be used as type parameter names
                type_params[num_type_params++] = consume_identifier_or_type_keyword(p, "Expect type parameter name");
            } while (match(p, TOK_COMMA));

            consume(p, TOK_GREATER, "Expect '>' after type parameters");
        }

        consume(p, TOK_EQUAL, "Expect '=' after type alias name");

        Type *aliased_type = parse_type(p);

        consume(p, TOK_SEMICOLON, "Expect ';' after type alias");

        Stmt *stmt = stmt_type_alias(name, type_params, num_type_params, aliased_type);
        free(name);
        RETURN_STMT(stmt);
    }

    // Object type definition: define TypeName { ... }
    if (match(p, TOK_DEFINE)) {
        Stmt *stmt = define_statement(p);
        stmt->as.define_object.annotations = annotations;
        stmt->as.define_object.annotation_count = annotation_count;
        if (!validate_annotations(stmt)) {
            p->had_error = 1;
        }
        RETURN_STMT(stmt);
    }

    // Enum definition: enum EnumName { ... }
    if (match(p, TOK_ENUM)) {
        // Contextual keywords can be used as enum type names
        char *name = consume_identifier_or_type_keyword(p, "Expect enum type name");

        consume(p, TOK_LBRACE, "Expect '{' after enum name");

        // Parse variants
        int variant_capacity = 32;
        char **variant_names = malloc(sizeof(char*) * variant_capacity);
        Expr **variant_values = malloc(sizeof(Expr*) * variant_capacity);
        if (!variant_names || !variant_values) {
            free(variant_names);
            free(variant_values);
            free(name);
            error(p, "Memory allocation failed for enum variants");
            RETURN_STMT(stmt_expr(expr_number(0)));
        }
        int num_variants = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            // Grow arrays if needed
            if (num_variants >= variant_capacity) {
                variant_capacity *= 2;
                char **new_variant_names = realloc(variant_names, sizeof(char*) * variant_capacity);
                Expr **new_variant_values = realloc(variant_values, sizeof(Expr*) * variant_capacity);
                if (!new_variant_names || !new_variant_values) {
                    if (new_variant_names) variant_names = new_variant_names;
                    if (new_variant_values) variant_values = new_variant_values;
                    error(p, "Memory allocation failed for enum variants");
                    break;
                }
                variant_names = new_variant_names;
                variant_values = new_variant_values;
            }
            // Contextual keywords can be used as variant names
            variant_names[num_variants] = consume_identifier_or_type_keyword(p, "Expect variant name");

            // Check for explicit value assignment
            if (match(p, TOK_EQUAL)) {
                variant_values[num_variants] = expression(p);
            } else {
                variant_values[num_variants] = NULL;  // auto value
            }

            num_variants++;

            if (!match(p, TOK_COMMA)) break;
        }

        consume(p, TOK_RBRACE, "Expect '}' after enum variants");

        Stmt *stmt = stmt_enum(name, variant_names, variant_values, num_variants);
        stmt->as.enum_decl.annotations = annotations;
        stmt->as.enum_decl.annotation_count = annotation_count;
        if (!validate_annotations(stmt)) {
            p->had_error = 1;
        }
        free(name);
        RETURN_STMT(stmt);
    }

    // Named function: fn name(...) { ... } or async fn name(...) { ... }
    // Desugar to: let name = fn(...) { ... }; or let name = async fn(...) { ... };
    int is_async = 0;
    if (match(p, TOK_ASYNC)) {
        is_async = 1;
        consume(p, TOK_FN, "Expect 'fn' after 'async'");
    } else if (match(p, TOK_FN)) {
        is_async = 0;
    } else {
        // Not a function declaration, continue to next check
        goto not_function;
    }

    // Check if it's a named function (next token is identifier or contextual keyword)
    if (check_identifier_or_type_keyword(p)) {
        char *name = token_text(&p->current);
        advance(p);  // consume identifier

        // Now parse as function expression
        consume(p, TOK_LPAREN, "Expect '(' after function name");

        // Parse parameters - start with small capacity and grow as needed
        int param_capacity = 8;
        char **param_names = malloc(sizeof(char*) * param_capacity);
        Type **param_types = malloc(sizeof(Type*) * param_capacity);
        Expr **param_defaults = malloc(sizeof(Expr*) * param_capacity);
        int *param_is_ref = malloc(sizeof(int) * param_capacity);
        int *param_is_const = malloc(sizeof(int) * param_capacity);
        if (!param_names || !param_types || !param_defaults || !param_is_ref || !param_is_const) {
            free(param_names);
            free(param_types);
            free(param_defaults);
            free(param_is_ref);
            free(param_is_const);
            free(name);
            error(p, "Memory allocation failed for function parameters");
            RETURN_STMT(stmt_expr(expr_number(0)));
        }
        int num_params = 0;
        int seen_optional = 0;
        char *rest_param = NULL;
        Type *rest_param_type = NULL;

        // Parse function parameters - supports trailing commas
        if (!check(p, TOK_RPAREN)) {
            do {
                // Allow trailing comma before closing paren
                if (check(p, TOK_RPAREN)) break;

                // Check for rest parameter: ...name
                if (match(p, TOK_DOT_DOT_DOT)) {
                    rest_param = consume_identifier_or_type_keyword(p, "Expect parameter name after '...'");
                    if (match(p, TOK_COLON)) {
                        rest_param_type = parse_type(p);
                    }
                    if (!check(p, TOK_RPAREN)) {
                        error_at(p, &p->current, "Rest parameter must be the last parameter");
                    }
                    break;
                }

                // Check parameter limit before adding
                if (num_params >= MAX_FUNCTION_PARAMS) {
                    error_at(p, &p->current, "functions cannot have more than 64 parameters");
                    break;
                }

                // Grow arrays if needed
                if (num_params >= param_capacity) {
                    int new_capacity = param_capacity * 2;
                    char **new_param_names = realloc(param_names, sizeof(char*) * new_capacity);
                    Type **new_param_types = realloc(param_types, sizeof(Type*) * new_capacity);
                    Expr **new_param_defaults = realloc(param_defaults, sizeof(Expr*) * new_capacity);
                    int *new_param_is_ref = realloc(param_is_ref, sizeof(int) * new_capacity);
                    int *new_param_is_const = realloc(param_is_const, sizeof(int) * new_capacity);
                    if (!new_param_names || !new_param_types || !new_param_defaults || !new_param_is_ref || !new_param_is_const) {
                        if (new_param_names) param_names = new_param_names;
                        if (new_param_types) param_types = new_param_types;
                        if (new_param_defaults) param_defaults = new_param_defaults;
                        if (new_param_is_ref) param_is_ref = new_param_is_ref;
                        if (new_param_is_const) param_is_const = new_param_is_const;
                        error(p, "Memory allocation failed for function parameters");
                        break;
                    }
                    param_names = new_param_names;
                    param_types = new_param_types;
                    param_defaults = new_param_defaults;
                    param_is_ref = new_param_is_ref;
                    param_is_const = new_param_is_const;
                    param_capacity = new_capacity;
                }

                // Check for const keyword (immutable parameter)
                int is_const = match(p, TOK_CONST);
                param_is_const[num_params] = is_const;

                // Check for ref keyword (pass-by-reference)
                int is_ref = match(p, TOK_REF);
                param_is_ref[num_params] = is_ref;

                // const and ref are mutually exclusive
                if (is_const && is_ref) {
                    error_at(p, &p->current, "const and ref modifiers cannot be combined");
                }

                param_names[num_params] = consume_identifier_or_type_keyword(p, "Expect parameter name");

                if (match(p, TOK_COLON)) {
                    param_types[num_params] = parse_type(p);
                } else {
                    param_types[num_params] = NULL;
                }

                // Check for optional parameter
                if (match(p, TOK_QUESTION)) {
                    if (is_ref) {
                        error_at(p, &p->current, "ref parameters cannot have default values");
                    }
                    consume(p, TOK_COLON, "Expect ':' after '?' for default value");
                    param_defaults[num_params] = expression(p);
                    seen_optional = 1;
                } else {
                    if (seen_optional) {
                        error_at(p, &p->current, "Required parameters must come before optional parameters");
                    }
                    param_defaults[num_params] = NULL;
                }

                num_params++;
            } while (match(p, TOK_COMMA));
        }

        consume(p, TOK_RPAREN, "Expect ')' after parameters");

        // Optional return type
        Type *return_type = NULL;
        if (match(p, TOK_COLON)) {
            return_type = parse_type(p);
        }

        // Parse body - either block { } or expression-bodied => expr;
        Stmt *body;
        if (match(p, TOK_ARROW)) {
            // Expression-bodied function: fn name(...) => expr;
            Expr *body_expr = expression(p);
            consume(p, TOK_SEMICOLON, "Expect ';' after expression-bodied function");
            // Wrap expression in return statement, then in block
            Stmt *return_stmt = stmt_return(body_expr);
            Stmt **stmts = malloc(sizeof(Stmt*));
            stmts[0] = return_stmt;
            body = stmt_block(stmts, 1);
        } else {
            consume(p, TOK_LBRACE, "Expect '{' or '=>' before function body");
            body = block_statement(p);
        }

        // Create function expression (with is_async flag)
        Expr *fn_expr = expr_function(is_async, param_names, param_types, param_defaults, param_is_ref, param_is_const, num_params, rest_param, rest_param_type, return_type, body);

        // Desugar to let statement
        Stmt *stmt = stmt_let_typed(name, NULL, fn_expr);
        stmt->as.let.annotations = annotations;
        stmt->as.let.annotation_count = annotation_count;
        if (!validate_annotations(stmt)) {
            p->had_error = 1;
        }
        free(name);
        RETURN_STMT(stmt);
    } else {
        // Anonymous function at statement level - error
        error(p, "Unexpected anonymous function (did you mean to assign it?)");
        RETURN_STMT(stmt_expr(expr_number(0)));
    }

not_function:

    // Check if annotations were provided but statement doesn't support them
    if (annotation_count > 0) {
        error(p, "Annotations are only valid on 'let', 'const', 'fn', 'define', or 'enum' declarations");
        // Free the annotations
        for (int i = 0; i < annotation_count; i++) {
            annotation_free(annotations[i]);
        }
        free(annotations);
    }

    if (match(p, TOK_IF)) {
        RETURN_STMT(if_statement(p));
    }

    // Check for labeled loop: identifier: while/for/loop
    // Contextual keywords can also be used as loop labels
    if (check_identifier_or_type_keyword(p)) {
        // Look ahead to see if this is a labeled loop
        Token saved_current = p->current;
        Token saved_previous = p->previous;
        Token saved_next = p->next;
        const char *saved_lexer_start = p->lexer->start;
        const char *saved_lexer_current = p->lexer->current;
        const char *saved_lexer_line_start = p->lexer->line_start;
        int saved_lexer_line = p->lexer->line;

        advance(p);  // consume identifier
        if (check(p, TOK_COLON)) {
            advance(p);  // consume colon
            if (check(p, TOK_WHILE) || check(p, TOK_LOOP) || check(p, TOK_FOR)) {
                // This is a labeled loop
                char *label = token_text(&saved_current);
                if (match(p, TOK_WHILE)) {
                    Stmt *stmt = while_statement_with_label(p, label);
                    free(label);
                    RETURN_STMT(stmt);
                } else if (match(p, TOK_LOOP)) {
                    Stmt *stmt = loop_statement_with_label(p, label);
                    free(label);
                    RETURN_STMT(stmt);
                } else if (match(p, TOK_FOR)) {
                    Stmt *stmt = for_statement_with_label(p, label);
                    free(label);
                    RETURN_STMT(stmt);
                }
                free(label);
            }
        }
        // Not a labeled loop, restore state
        // Free any string_value in tokens that are NEW (not from saved state)
        // We may have consumed 1-2 tokens during look-ahead; free their string_value if allocated
        if (p->current.string_value &&
            p->current.string_value != saved_current.string_value &&
            p->current.string_value != saved_next.string_value) {
            free(p->current.string_value);
        }
        if (p->next.string_value &&
            p->next.string_value != saved_current.string_value &&
            p->next.string_value != saved_next.string_value &&
            p->next.string_value != p->current.string_value) {  // Don't double-free if same
            free(p->next.string_value);
        }
        p->current = saved_current;
        p->previous = saved_previous;
        p->next = saved_next;
        p->lexer->start = saved_lexer_start;
        p->lexer->current = saved_lexer_current;
        p->lexer->line_start = saved_lexer_line_start;
        p->lexer->line = saved_lexer_line;
    }

    if (match(p, TOK_WHILE)) {
        RETURN_STMT(while_statement(p));
    }

    if (match(p, TOK_LOOP)) {
        RETURN_STMT(loop_statement(p));
    }

    if (match(p, TOK_FOR)) {
        RETURN_STMT(for_statement(p));
    }

    if (match(p, TOK_BREAK)) {
        // Check for optional label - contextual keywords can be used as labels
        if (check_identifier_or_type_keyword(p)) {
            advance(p);
            char *label = token_text(&p->previous);
            consume(p, TOK_SEMICOLON, "Expect ';' after 'break'");
            Stmt *stmt = stmt_break_labeled(label);
            free(label);
            RETURN_STMT(stmt);
        }
        consume(p, TOK_SEMICOLON, "Expect ';' after 'break'");
        RETURN_STMT(stmt_break());
    }

    if (match(p, TOK_CONTINUE)) {
        // Check for optional label - contextual keywords can be used as labels
        if (check_identifier_or_type_keyword(p)) {
            advance(p);
            char *label = token_text(&p->previous);
            consume(p, TOK_SEMICOLON, "Expect ';' after 'continue'");
            Stmt *stmt = stmt_continue_labeled(label);
            free(label);
            RETURN_STMT(stmt);
        }
        consume(p, TOK_SEMICOLON, "Expect ';' after 'continue'");
        RETURN_STMT(stmt_continue());
    }

    if (match(p, TOK_RETURN)) {
        RETURN_STMT(return_statement(p));
    }

    if (match(p, TOK_TRY)) {
        // Parse try block
        consume(p, TOK_LBRACE, "Expect '{' after 'try'");
        Stmt *try_block = block_statement(p);

        // Parse optional catch block
        char *catch_param = NULL;
        Stmt *catch_block = NULL;
        if (match(p, TOK_CATCH)) {
            consume(p, TOK_LPAREN, "Expect '(' after 'catch'");
            // Contextual keywords can be used as catch parameter names
            catch_param = consume_identifier_or_type_keyword(p, "Expect parameter name");
            consume(p, TOK_RPAREN, "Expect ')' after catch parameter");
            consume(p, TOK_LBRACE, "Expect '{' before catch block");
            catch_block = block_statement(p);
        }

        // Parse optional finally block
        Stmt *finally_block = NULL;
        if (match(p, TOK_FINALLY)) {
            consume(p, TOK_LBRACE, "Expect '{' after 'finally'");
            finally_block = block_statement(p);
        }

        // Must have either catch or finally
        if (catch_block == NULL && finally_block == NULL) {
            error(p, "Try statement must have either 'catch' or 'finally' block");
        }

        RETURN_STMT(stmt_try(try_block, catch_param, catch_block, finally_block));
    }

    if (match(p, TOK_THROW)) {
        Expr *value = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after throw statement");
        RETURN_STMT(stmt_throw(value));
    }

    if (match(p, TOK_DEFER)) {
        Expr *call = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after defer statement");
        RETURN_STMT(stmt_defer(call));
    }

    if (match(p, TOK_SWITCH)) {
        RETURN_STMT(switch_statement(p));
    }

    if (match(p, TOK_IMPORT)) {
        RETURN_STMT(import_statement(p));
    }

    if (match(p, TOK_EXPORT)) {
        RETURN_STMT(export_statement(p));
    }

    if (match(p, TOK_EXTERN)) {
        RETURN_STMT(extern_fn_statement(p));
    }

    // Bare block statement
    if (match(p, TOK_LBRACE)) {
        RETURN_STMT(block_statement(p));
    }

    RETURN_STMT(expression_statement(p));
}

#undef RETURN_STMT

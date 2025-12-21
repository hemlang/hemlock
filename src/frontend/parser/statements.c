#include "internal.h"

// ========== STATEMENT PARSING ==========

Stmt* statement(Parser *p);

Stmt* let_statement(Parser *p) {
    consume(p, TOK_IDENT, "Expect variable name");
    char *name = token_text(&p->previous);

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
    consume(p, TOK_IDENT, "Expect variable name");
    char *name = token_text(&p->previous);

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
    Stmt **statements = malloc(sizeof(Stmt*) * 256);
    int count = 0;
    
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        statements[count++] = statement(p);
    }
    
    consume(p, TOK_RBRACE, "Expect '}' after block");
    
    return stmt_block(statements, count);
}

Stmt* if_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'if'");
    Expr *condition = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after condition");
    
    consume(p, TOK_LBRACE, "Expect '{' after if condition");
    Stmt *then_branch = block_statement(p);
    
    Stmt *else_branch = NULL;
    if (match(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) {
            // else if - recursively parse the if statement
            advance(p);  // consume the IF token
            else_branch = if_statement(p);
        } else {
            // regular else - parse block
            consume(p, TOK_LBRACE, "Expect '{' after 'else'");
            else_branch = block_statement(p);
        }
    }
    
    return stmt_if(condition, then_branch, else_branch);
}

Stmt* while_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'while'");
    Expr *condition = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after condition");

    consume(p, TOK_LBRACE, "Expect '{' after while condition");
    Stmt *body = block_statement(p);

    return stmt_while(condition, body);
}

Stmt* switch_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'switch'");
    Expr *expr = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after switch expression");
    consume(p, TOK_LBRACE, "Expect '{' after switch expression");

    // Parse cases
    Expr **case_values = malloc(sizeof(Expr*) * 128);
    Stmt **case_bodies = malloc(sizeof(Stmt*) * 128);
    int num_cases = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_CASE)) {
            // Parse case value
            case_values[num_cases] = expression(p);
            consume(p, TOK_COLON, "Expect ':' after case value");

            // Parse case body statements until we hit another case/default/closing brace
            Stmt **statements = malloc(sizeof(Stmt*) * 128);
            int count = 0;

            while (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                statements[count++] = statement(p);
            }

            case_bodies[num_cases] = stmt_block(statements, count);
            num_cases++;
        } else if (match(p, TOK_DEFAULT)) {
            consume(p, TOK_COLON, "Expect ':' after 'default'");

            // NULL value indicates default case
            case_values[num_cases] = NULL;

            // Parse default body statements
            Stmt **statements = malloc(sizeof(Stmt*) * 128);
            int count = 0;

            while (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
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

Stmt* for_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'for'");

    // Check if this is a for-in loop by looking ahead
    int is_for_in = 0;
    char *first_var = NULL;
    char *second_var = NULL;

    if (match(p, TOK_LET)) {
        consume(p, TOK_IDENT, "Expect variable name");
        first_var = token_text(&p->previous);

        if (match(p, TOK_COMMA)) {
            // for (let key, value in ...)
            consume(p, TOK_IDENT, "Expect second variable name");
            second_var = token_text(&p->previous);
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
            consume(p, TOK_LBRACE, "Expect '{' after for-in");
            Stmt *body = block_statement(p);

            if (second_var) {
                // Two variables: for (let key, value in ...)
                return stmt_for_in(first_var, second_var, iterable, body);
            } else {
                // One variable: for (let value in ...)
                return stmt_for_in(NULL, first_var, iterable, body);
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

        consume(p, TOK_LBRACE, "Expect '{' after for");
        Stmt *body = block_statement(p);

        return stmt_for(initializer, condition, increment, body);
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

    consume(p, TOK_LBRACE, "Expect '{' after for");
    Stmt *body = block_statement(p);

    return stmt_for(initializer, condition, increment, body);
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

    // Check for namespace import: import * as name from "module"
    if (match(p, TOK_STAR)) {
        consume_contextual(p, "as", "Expect 'as' after '*' in namespace import");
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
    }

    // Named imports: import { name1, name2 as alias } from "module"
    consume(p, TOK_LBRACE, "Expect '{', '*', or string after 'import'");

    char **import_names = malloc(sizeof(char*) * 32);
    char **import_aliases = malloc(sizeof(char*) * 32);
    int num_imports = 0;

    do {
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
        char **export_names = malloc(sizeof(char*) * 32);
        char **export_aliases = malloc(sizeof(char*) * 32);
        int num_exports = 0;

        do {
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

    // Must be a named function
    consume(p, TOK_IDENT, "Expect function name after 'export fn'");
    char *name = token_text(&p->previous);

    // Parse function (same as in statement())
    consume(p, TOK_LPAREN, "Expect '(' after function name");

    char **param_names = malloc(sizeof(char*) * 32);
    Type **param_types = malloc(sizeof(Type*) * 32);
    Expr **param_defaults = malloc(sizeof(Expr*) * 32);
    int num_params = 0;
    int seen_optional = 0;
    char *rest_param = NULL;
    Type *rest_param_type = NULL;

    if (!check(p, TOK_RPAREN)) {
        do {
            // Check for rest parameter: ...name
            if (match(p, TOK_DOT_DOT_DOT)) {
                consume(p, TOK_IDENT, "Expect parameter name after '...'");
                rest_param = token_text(&p->previous);
                if (match(p, TOK_COLON)) {
                    rest_param_type = parse_type(p);
                }
                if (!check(p, TOK_RPAREN)) {
                    error_at(p, &p->current, "Rest parameter must be the last parameter");
                }
                break;
            }

            consume(p, TOK_IDENT, "Expect parameter name");
            param_names[num_params] = token_text(&p->previous);

            if (match(p, TOK_COLON)) {
                param_types[num_params] = parse_type(p);
            } else {
                param_types[num_params] = NULL;
            }

            // Check for optional parameter
            if (match(p, TOK_QUESTION)) {
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
    Expr *fn_expr = expr_function(is_async, param_names, param_types, param_defaults, num_params, rest_param, rest_param_type, return_type, body);

    // Create let statement
    Stmt *decl = stmt_let_typed(name, NULL, fn_expr);
    free(name);

    return stmt_export_declaration(decl);
}

Stmt* extern_fn_statement(Parser *p) {
    // extern fn name(param1: type1, param2: type2): return_type;
    consume(p, TOK_FN, "Expect 'fn' after 'extern'");
    consume(p, TOK_IDENT, "Expect function name");
    char *function_name = token_text(&p->previous);

    consume(p, TOK_LPAREN, "Expect '(' after function name");

    // Parse parameters
    Type **param_types = malloc(sizeof(Type*) * 32);
    int num_params = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            // Parameter name is not used in FFI, but required for syntax
            consume(p, TOK_IDENT, "Expect parameter name");
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

    Stmt *stmt = stmt_extern_fn(function_name, param_types, num_params, return_type);
    free(function_name);
    return stmt;
}

Stmt* statement(Parser *p) {
    if (match(p, TOK_LET)) {
        return let_statement(p);
    }

    if (match(p, TOK_CONST)) {
        return const_statement(p);
    }

    // Object type definition: define TypeName { ... }
    if (match(p, TOK_DEFINE)) {
        consume(p, TOK_IDENT, "Expect object type name");
        char *name = token_text(&p->previous);

        consume(p, TOK_LBRACE, "Expect '{' after type name");

        // Parse fields
        char **field_names = malloc(sizeof(char*) * 32);
        Type **field_types = malloc(sizeof(Type*) * 32);
        int *field_optional = malloc(sizeof(int) * 32);
        Expr **field_defaults = malloc(sizeof(Expr*) * 32);
        int num_fields = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            consume(p, TOK_IDENT, "Expect field name");
            field_names[num_fields] = token_text(&p->previous);

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

        Stmt *stmt = stmt_define_object(name, field_names, field_types,
                                       field_optional, field_defaults, num_fields);
        free(name);
        return stmt;
    }

    // Enum definition: enum EnumName { ... }
    if (match(p, TOK_ENUM)) {
        consume(p, TOK_IDENT, "Expect enum type name");
        char *name = token_text(&p->previous);

        consume(p, TOK_LBRACE, "Expect '{' after enum name");

        // Parse variants
        char **variant_names = malloc(sizeof(char*) * 32);
        Expr **variant_values = malloc(sizeof(Expr*) * 32);
        int num_variants = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            consume(p, TOK_IDENT, "Expect variant name");
            variant_names[num_variants] = token_text(&p->previous);

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
        free(name);
        return stmt;
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

    // Check if it's a named function (next token is identifier)
    if (check(p, TOK_IDENT)) {
        char *name = token_text(&p->current);
        advance(p);  // consume identifier

        // Now parse as function expression
        consume(p, TOK_LPAREN, "Expect '(' after function name");

        // Parse parameters
        char **param_names = malloc(sizeof(char*) * 32);
        Type **param_types = malloc(sizeof(Type*) * 32);
        Expr **param_defaults = malloc(sizeof(Expr*) * 32);
        int num_params = 0;
        int seen_optional = 0;
        char *rest_param = NULL;
        Type *rest_param_type = NULL;

        if (!check(p, TOK_RPAREN)) {
            do {
                // Check for rest parameter: ...name
                if (match(p, TOK_DOT_DOT_DOT)) {
                    consume(p, TOK_IDENT, "Expect parameter name after '...'");
                    rest_param = token_text(&p->previous);
                    if (match(p, TOK_COLON)) {
                        rest_param_type = parse_type(p);
                    }
                    if (!check(p, TOK_RPAREN)) {
                        error_at(p, &p->current, "Rest parameter must be the last parameter");
                    }
                    break;
                }

                consume(p, TOK_IDENT, "Expect parameter name");
                param_names[num_params] = token_text(&p->previous);

                if (match(p, TOK_COLON)) {
                    param_types[num_params] = parse_type(p);
                } else {
                    param_types[num_params] = NULL;
                }

                // Check for optional parameter
                if (match(p, TOK_QUESTION)) {
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

        // Create function expression (with is_async flag)
        Expr *fn_expr = expr_function(is_async, param_names, param_types, param_defaults, num_params, rest_param, rest_param_type, return_type, body);

        // Desugar to let statement
        Stmt *stmt = stmt_let_typed(name, NULL, fn_expr);
        free(name);
        return stmt;
    } else {
        // Anonymous function at statement level - error
        error(p, "Unexpected anonymous function (did you mean to assign it?)");
        return stmt_expr(expr_number(0));
    }

not_function:

    if (match(p, TOK_IF)) {
        return if_statement(p);
    }

    if (match(p, TOK_WHILE)) {
        return while_statement(p);
    }

    if (match(p, TOK_FOR)) {
        return for_statement(p);
    }

    if (match(p, TOK_BREAK)) {
        consume(p, TOK_SEMICOLON, "Expect ';' after 'break'");
        return stmt_break();
    }

    if (match(p, TOK_CONTINUE)) {
        consume(p, TOK_SEMICOLON, "Expect ';' after 'continue'");
        return stmt_continue();
    }

    if (match(p, TOK_RETURN)) {
        return return_statement(p);
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
            consume(p, TOK_IDENT, "Expect parameter name");
            catch_param = token_text(&p->previous);
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

        return stmt_try(try_block, catch_param, catch_block, finally_block);
    }

    if (match(p, TOK_THROW)) {
        int throw_line = p->previous.line;  // Save line of 'throw' for stack trace
        Expr *value = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after throw statement");
        Stmt *stmt = stmt_throw(value);
        stmt->line = throw_line;  // Set line number for stack trace
        return stmt;
    }

    if (match(p, TOK_DEFER)) {
        Expr *call = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after defer statement");
        return stmt_defer(call);
    }

    if (match(p, TOK_SWITCH)) {
        return switch_statement(p);
    }

    if (match(p, TOK_IMPORT)) {
        return import_statement(p);
    }

    if (match(p, TOK_EXPORT)) {
        return export_statement(p);
    }

    if (match(p, TOK_EXTERN)) {
        return extern_fn_statement(p);
    }

    // Bare block statement
    if (match(p, TOK_LBRACE)) {
        return block_statement(p);
    }

    return expression_statement(p);
}


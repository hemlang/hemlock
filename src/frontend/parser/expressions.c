#include "internal.h"

// ========== EXPRESSION PARSING ==========

// Forward declarations
Expr* expression(Parser *p);
Expr* assignment(Parser *p);
Expr* ternary(Parser *p);
Expr* null_coalesce(Parser *p);
Expr* logical_or(Parser *p);
Expr* logical_and(Parser *p);
Expr* bitwise_or(Parser *p);
Expr* bitwise_xor(Parser *p);
Expr* bitwise_and(Parser *p);
Expr* equality(Parser *p);
Expr* comparison(Parser *p);
Expr* shift(Parser *p);
Expr* term(Parser *p);
Expr* factor(Parser *p);
Expr* unary(Parser *p);
Expr* postfix(Parser *p);
// Parse the remainder of a function expression after the 'fn' keyword has
// been consumed: (params) [: type] { body }  or  (params) [: type] => expr
// Shared by fn/async fn expressions and object-literal method shorthand.
static Expr* fn_expression_rest(Parser *p, int is_async_fn) {
    int fn_start_line = p->previous.line;  // the 'fn' keyword just consumed
    consume(p, TOK_LPAREN, "Expect '(' after 'fn'");

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
        error(p, "Memory allocation failed for function parameters");
        return expr_function(is_async_fn, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, stmt_block(NULL, 0));
    }
    int num_params = 0;
    int seen_optional = 0;  // Track if we've seen an optional parameter
    char *rest_param = NULL;
    Type *rest_param_type = NULL;

    // Parse function parameters - supports trailing commas: fn(a, b, c,)
    if (!check(p, TOK_RPAREN)) {
        do {
            // Allow trailing comma before closing paren
            if (check(p, TOK_RPAREN)) break;

            // Check for rest parameter: ...name
            if (match(p, TOK_DOT_DOT_DOT)) {
                consume(p, TOK_IDENT, "Expect parameter name after '...'");
                rest_param = token_text(&p->previous);
                // Optional type annotation for rest param
                if (match(p, TOK_COLON)) {
                    rest_param_type = parse_type(p);
                }
                // Rest parameter must be last
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

            consume(p, TOK_IDENT, "Expect parameter name");
            param_names[num_params] = token_text(&p->previous);

            // Optional type annotation
            if (match(p, TOK_COLON)) {
                param_types[num_params] = parse_type(p);
            } else {
                param_types[num_params] = NULL;
            }

            // Check for optional parameter (?) with default value
            if (match(p, TOK_QUESTION)) {
                if (is_ref) {
                    error_at(p, &p->current, "ref parameters cannot have default values");
                }
                consume(p, TOK_COLON, "Expect ':' after '?' for default value");
                param_defaults[num_params] = expression(p);
                seen_optional = 1;
            } else {
                // Required parameter
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

    // Parse body - either block { } or expression-bodied => expr
    Stmt *body;
    if (match(p, TOK_ARROW)) {
        // Expression-bodied anonymous function: fn(...) => expr
        Expr *body_expr = expression(p);
        // Wrap expression in return statement, then in block
        Stmt *return_stmt = stmt_return(body_expr);
        Stmt **stmts = malloc(sizeof(Stmt*));
        stmts[0] = return_stmt;
        body = stmt_block(stmts, 1);
    } else {
        consume(p, TOK_LBRACE, "Expect '{' or '=>' before function body");
        body = block_statement(p);
    }

    Expr *fn_expr = expr_function(is_async_fn, param_names, param_types, param_defaults, param_is_ref, param_is_const, num_params, rest_param, rest_param_type, return_type, body);
    fn_expr->line = fn_start_line;
    return fn_expr;
}

Expr* primary(Parser *p);
Type* parse_type(Parser *p);
Pattern* parse_pattern(Parser *p);
static Pattern* parse_primary_pattern(Parser *p);

// Use the shared is_identifier_or_type_keyword and consume_identifier_or_type_keyword from core.c

// Helper: Parse interpolated string with ${...} expressions
static Expr* parse_interpolated_string(Parser *p, const char *str_content) {
    char **string_parts = malloc(sizeof(char*) * 32);  // Array of string literals
    Expr **expr_parts = malloc(sizeof(Expr*) * 32);    // Array of expressions
    if (!string_parts || !expr_parts) {
        free(string_parts);
        free(expr_parts);
        error(p, "Memory allocation failed in string interpolation");
        return expr_string("");
    }
    int num_parts = 0;
    int capacity = 32;

    const char *ptr = str_content;
    char *current_string = malloc(1024);
    if (!current_string) {
        free(string_parts);
        free(expr_parts);
        error(p, "Memory allocation failed in string interpolation");
        return expr_string("");
    }
    int str_len = 0;
    int str_capacity = 1024;

    while (*ptr != '\0') {
        if (*ptr == '$' && *(ptr + 1) == '{') {
            // Found interpolation start
            // Save current string part
            current_string[str_len] = '\0';
            string_parts[num_parts] = strdup(current_string);
            if (!string_parts[num_parts]) {
                error(p, "Memory allocation failed in string interpolation");
                free(current_string);
                for (int i = 0; i < num_parts; i++) {
                    free(string_parts[i]);
                }
                free(string_parts);
                free(expr_parts);
                return expr_string("");
            }
            str_len = 0;

            // Find matching }
            ptr += 2;  // Skip ${
            const char *expr_start = ptr;
            int brace_count = 1;
            while (*ptr != '\0' && brace_count > 0) {
                if (*ptr == '{') brace_count++;
                if (*ptr == '}') brace_count--;
                if (brace_count > 0) ptr++;
            }

            if (brace_count != 0) {
                error(p, "Unclosed ${...} in string interpolation");
                // Cleanup all allocated memory before returning
                free(current_string);
                for (int i = 0; i <= num_parts; i++) {
                    free(string_parts[i]);
                    // Note: expr_parts[i] will be cleaned by AST system
                }
                free(string_parts);
                free(expr_parts);
                return expr_string("");
            }

            // Extract expression text
            int expr_len = ptr - expr_start;
            char *expr_text = malloc(expr_len + 1);
            if (!expr_text) {
                error(p, "Memory allocation failed in string interpolation");
                free(current_string);
                for (int i = 0; i <= num_parts; i++) {
                    free(string_parts[i]);
                }
                free(string_parts);
                free(expr_parts);
                return expr_string("");
            }
            memcpy(expr_text, expr_start, expr_len);
            expr_text[expr_len] = '\0';

            // Parse the expression using a new parser
            Lexer expr_lexer;
            lexer_init(&expr_lexer, expr_text);

            Parser expr_parser;
            parser_init(&expr_parser, &expr_lexer);

            Expr *interpolated_expr = expression(&expr_parser);
            expr_parts[num_parts] = interpolated_expr;

            // Safe to free expr_text now - token_text() makes copies of all string data
            free(expr_text);

            num_parts++;
            if (num_parts >= capacity) {
                int new_capacity = capacity * 2;
                char **new_string_parts = realloc(string_parts, sizeof(char*) * new_capacity);
                if (!new_string_parts) {
                    error(p, "Memory allocation failed in string interpolation");
                    free(current_string);
                    for (int i = 0; i < num_parts; i++) {
                        free(string_parts[i]);
                    }
                    free(string_parts);
                    free(expr_parts);
                    return expr_string("");
                }
                string_parts = new_string_parts;

                Expr **new_expr_parts = realloc(expr_parts, sizeof(Expr*) * new_capacity);
                if (!new_expr_parts) {
                    error(p, "Memory allocation failed in string interpolation");
                    free(current_string);
                    for (int i = 0; i < num_parts; i++) {
                        free(string_parts[i]);
                    }
                    free(string_parts);
                    free(expr_parts);
                    return expr_string("");
                }
                expr_parts = new_expr_parts;
                capacity = new_capacity;
            }

            ptr++;  // Skip closing }
        } else {
            // Regular character
            if (str_len >= str_capacity - 1) {
                str_capacity *= 2;
                char *new_string = realloc(current_string, str_capacity);
                if (!new_string) {
                    error(p, "Memory allocation failed in string interpolation");
                    // Cleanup and return
                    free(current_string);
                    for (int i = 0; i < num_parts; i++) {
                        free(string_parts[i]);
                    }
                    free(string_parts);
                    free(expr_parts);
                    return expr_string("");
                }
                current_string = new_string;
            }
            current_string[str_len++] = *ptr;
            ptr++;
        }
    }

    // Save final string part
    current_string[str_len] = '\0';
    string_parts[num_parts] = strdup(current_string);
    free(current_string);

    // Create interpolation expression
    Expr *result = expr_string_interpolation(string_parts, expr_parts, num_parts);
    return result;
}

// ========== PATTERN PARSING (for match expressions) ==========

// Parse a primary pattern (non-OR pattern)
static Pattern* parse_primary_pattern(Parser *p) {
    int line = p->current.line;
    int column = p->current.column;

    // Wildcard pattern: _
    if (check(p, TOK_IDENT) && p->current.length == 1 && p->current.start[0] == '_') {
        advance(p);
        Pattern *pat = pattern_wildcard();
        pat->line = line;
        pat->column = column;
        return pat;
    }

    // Literal patterns: numbers, strings, booleans, null
    // Handle negative numbers: -NUMBER
    if (match(p, TOK_MINUS)) {
        if (match(p, TOK_NUMBER)) {
            Expr *lit;
            if (p->previous.is_float) {
                lit = expr_number_float(-p->previous.float_value);
            } else if (p->previous.is_u64) {
                // Negating a u64 literal > INT64_MAX: cast to i64 (wraps, same as C)
                lit = expr_number_int(-(int64_t)p->previous.uint_value);
            } else {
                lit = expr_number_int(-p->previous.int_value);
            }
            Pattern *pat = pattern_literal(lit);
            pat->line = line;
            pat->column = column;
            return pat;
        } else {
            error(p, "Expect number after '-' in pattern");
            return pattern_wildcard();
        }
    }

    if (match(p, TOK_NUMBER)) {
        Expr *lit;
        if (p->previous.is_float) {
            lit = expr_number_float(p->previous.float_value);
        } else if (p->previous.is_u64) {
            lit = expr_number_u64(p->previous.uint_value);
        } else {
            lit = expr_number_int(p->previous.int_value);
        }
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        pat->column = column;
        return pat;
    }

    if (match(p, TOK_STRING)) {
        char *str = p->previous.string_value;
        Expr *lit = expr_string(str);
        free(str);
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        pat->column = column;
        return pat;
    }

    if (match(p, TOK_TRUE)) {
        Pattern *pat = pattern_literal(expr_bool(1));
        pat->line = line;
        pat->column = column;
        return pat;
    }

    if (match(p, TOK_FALSE)) {
        Pattern *pat = pattern_literal(expr_bool(0));
        pat->line = line;
        pat->column = column;
        return pat;
    }

    if (match(p, TOK_NULL)) {
        Pattern *pat = pattern_literal(expr_null());
        pat->line = line;
        pat->column = column;
        return pat;
    }

    if (match(p, TOK_RUNE)) {
        Pattern *pat = pattern_literal(expr_rune(p->previous.rune_value));
        pat->line = line;
        pat->column = column;
        return pat;
    }

    // Object pattern: { field, field: pattern, ...rest }
    if (match(p, TOK_LBRACE)) {
        int field_capacity = 8;
        ObjectFieldPattern *fields = malloc(sizeof(ObjectFieldPattern) * field_capacity);
        if (!fields) {
            error(p, "Memory allocation failed for object pattern");
            return pattern_wildcard();
        }
        int num_fields = 0;
        int has_rest = 0;
        char *rest_name = NULL;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            if (num_fields >= field_capacity) {
                field_capacity *= 2;
                ObjectFieldPattern *new_fields = realloc(fields, sizeof(ObjectFieldPattern) * field_capacity);
                if (!new_fields) {
                    error(p, "Memory allocation failed for object pattern");
                    break;
                }
                fields = new_fields;
            }

            // Check for rest pattern: ...name
            if (match(p, TOK_DOT_DOT_DOT)) {
                char *name = consume_identifier_or_type_keyword(p, "Expect identifier after '...' in object pattern");
                has_rest = 1;
                rest_name = name;
                match(p, TOK_COMMA);
                continue;
            }

            // Regular field: name or name: pattern
            char *field_name = consume_identifier_or_type_keyword(p, "Expect field name in object pattern");
            fields[num_fields].name = field_name;

            if (match(p, TOK_COLON)) {
                // Field with explicit pattern
                fields[num_fields].pattern = parse_pattern(p);
            } else {
                // Shorthand: just the name (binds to same name)
                fields[num_fields].pattern = NULL;
            }

            num_fields++;
            if (!check(p, TOK_RBRACE)) {
                match(p, TOK_COMMA);
            }
        }

        consume(p, TOK_RBRACE, "Expect '}' after object pattern");
        Pattern *pat = pattern_object(fields, num_fields, has_rest, rest_name);
        free(rest_name);  // pattern_object makes a copy
        pat->line = line;
        pat->column = column;
        return pat;
    }

    // Array pattern: [elem, elem, ...rest]
    if (match(p, TOK_LBRACKET)) {
        int elem_capacity = 8;
        ArrayElementPattern *elements = malloc(sizeof(ArrayElementPattern) * elem_capacity);
        if (!elements) {
            error(p, "Memory allocation failed for array pattern");
            return pattern_wildcard();
        }
        int num_elements = 0;

        while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF)) {
            if (num_elements >= elem_capacity) {
                elem_capacity *= 2;
                ArrayElementPattern *new_elements = realloc(elements, sizeof(ArrayElementPattern) * elem_capacity);
                if (!new_elements) {
                    error(p, "Memory allocation failed for array pattern");
                    break;
                }
                elements = new_elements;
            }

            // Check for rest pattern: ...name
            if (match(p, TOK_DOT_DOT_DOT)) {
                char *name = consume_identifier_or_type_keyword(p, "Expect identifier after '...' in array pattern");
                elements[num_elements].is_rest = 1;
                elements[num_elements].rest_name = name;
                elements[num_elements].pattern = NULL;
                num_elements++;
                match(p, TOK_COMMA);
                continue;
            }

            // Regular element pattern
            elements[num_elements].is_rest = 0;
            elements[num_elements].rest_name = NULL;
            elements[num_elements].pattern = parse_pattern(p);
            num_elements++;

            if (!check(p, TOK_RBRACKET)) {
                match(p, TOK_COMMA);
            }
        }

        consume(p, TOK_RBRACKET, "Expect ']' after array pattern");
        Pattern *pat = pattern_array(elements, num_elements);
        pat->line = line;
        pat->column = column;
        return pat;
    }

    // Variable binding or typed pattern: name or name: type
    if (is_identifier_or_type_keyword(p->current.type)) {
        char *name = consume_identifier_or_type_keyword(p, "Expect pattern");

        // Check for type annotation: name: type
        if (match(p, TOK_COLON)) {
            Type *type_ann = parse_type(p);
            Pattern *pat = pattern_typed(name, type_ann);
            free(name);
            pat->line = line;
            pat->column = column;
            return pat;
        }

        // Plain binding
        Pattern *pat = pattern_binding(name);
        free(name);
        pat->line = line;
        pat->column = column;
        return pat;
    }

    error(p, "Expect pattern");
    return pattern_wildcard();
}

// Parse a pattern, including OR patterns: pattern | pattern | ...
Pattern* parse_pattern(Parser *p) {
    Pattern *first = parse_primary_pattern(p);

    // Check for OR pattern
    if (!check(p, TOK_PIPE)) {
        return first;
    }

    // Build OR pattern
    int alt_capacity = 4;
    Pattern **alternatives = malloc(sizeof(Pattern*) * alt_capacity);
    if (!alternatives) {
        error(p, "Memory allocation failed for OR pattern");
        return first;
    }
    alternatives[0] = first;
    int num_alternatives = 1;

    while (match(p, TOK_PIPE)) {
        if (num_alternatives >= alt_capacity) {
            alt_capacity *= 2;
            Pattern **new_alternatives = realloc(alternatives, sizeof(Pattern*) * alt_capacity);
            if (!new_alternatives) {
                error(p, "Memory allocation failed for OR pattern");
                break;
            }
            alternatives = new_alternatives;
        }
        alternatives[num_alternatives++] = parse_primary_pattern(p);
    }

    Pattern *or_pat = pattern_or(alternatives, num_alternatives);
    or_pat->line = first->line;
    or_pat->column = first->column;
    return or_pat;
}

// ========== PRIMARY EXPRESSION PARSING ==========

Expr* primary(Parser *p) {
    if (match(p, TOK_TRUE)) {
        return expr_bool(1);
    }

    if (match(p, TOK_FALSE)) {
        return expr_bool(0);
    }

    if (match(p, TOK_NULL)) {
        return expr_null();
    }

    if (match(p, TOK_NUMBER)) {
        Expr *expr;
        if (p->previous.is_float) {
            expr = expr_number_float(p->previous.float_value);
        } else if (p->previous.is_u64) {
            expr = expr_number_u64(p->previous.uint_value);
        } else {
            expr = expr_number_int(p->previous.int_value);
        }
        // Preserve source location for formatter literal preservation
        expr->line = p->previous.line;
        expr->column = p->previous.column;
        return expr;
    }

    if (match(p, TOK_STRING)) {
        char *str = p->previous.string_value;
        Expr *expr = expr_string(str);
        free(str);  // Parser owns this memory from lexer
        return expr;
    }

    if (match(p, TOK_TEMPLATE_STRING)) {
        char *str = p->previous.string_value;
        Expr *expr = parse_interpolated_string(p, str);
        free(str);  // Parser owns this memory from lexer
        return expr;
    }

    if (match(p, TOK_RUNE)) {
        return expr_rune(p->previous.rune_value);
    }

    // Match expression: match (expr) { pattern => expr, ... }
    if (match(p, TOK_MATCH)) {
        consume(p, TOK_LPAREN, "Expect '(' after 'match'");
        Expr *scrutinee = expression(p);
        consume(p, TOK_RPAREN, "Expect ')' after match expression");
        consume(p, TOK_LBRACE, "Expect '{' to begin match arms");

        int arm_capacity = 16;
        MatchArm *arms = malloc(sizeof(MatchArm) * arm_capacity);
        if (!arms) {
            error(p, "Memory allocation failed for match arms");
            return expr_null();
        }
        int num_arms = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            // Snapshot the current token so we can detect no-progress
            // iterations (e.g. when an arm body is malformed and its
            // parser returns without consuming the problem token).
            Token arm_before = p->current;

            // Grow array if needed
            if (num_arms >= arm_capacity) {
                arm_capacity *= 2;
                MatchArm *new_arms = realloc(arms, sizeof(MatchArm) * arm_capacity);
                if (!new_arms) {
                    error(p, "Memory allocation failed for match arms");
                    break;
                }
                arms = new_arms;
            }

            // Parse pattern
            Pattern *pattern = parse_pattern(p);

            // Parse optional guard: if condition
            Expr *guard = NULL;
            if (match(p, TOK_IF)) {
                guard = expression(p);
            }

            // Expect =>
            consume(p, TOK_ARROW, "Expect '=>' after pattern");

            // Parse arm body expression
            Expr *body = expression(p);

            arms[num_arms].pattern = pattern;
            arms[num_arms].guard = guard;
            arms[num_arms].body = body;
            arms[num_arms].line = pattern ? pattern->line : p->previous.line;
            arms[num_arms].column = pattern ? pattern->column : p->previous.column;
            num_arms++;

            // Optional comma between arms
            match(p, TOK_COMMA);

            // Guarantee forward progress: if the arm body errored and its
            // parser returned without consuming the offending token, force
            // a token advance so we can escape this arm and try the next.
            if (p->current.type != TOK_EOF
                && p->current.start == arm_before.start
                && p->current.length == arm_before.length) {
                advance(p);
            }
        }

        consume(p, TOK_RBRACE, "Expect '}' after match arms");
        return expr_match(scrutinee, arms, num_arms);
    }

    // Function expression: fn(...) { ... } or async fn(...) { ... }
    // Must check this BEFORE treating 'async' as a plain identifier
    if (check(p, TOK_ASYNC) && p->next.type == TOK_FN) {
        advance(p);  // consume 'async'
        consume(p, TOK_FN, "Expect 'fn' after 'async'");
        return fn_expression_rest(p, 1);
    } else if (match(p, TOK_FN)) {
        return fn_expression_rest(p, 0);
    }

    // Identifier or contextual keywords used as identifiers
    // Keywords like 'type', 'define', 'enum', 'import', 'export', 'extern', 'async', 'defer'
    // can be used as variable names when not at the start of a statement
    if (is_identifier_or_type_keyword(p->current.type)) {
        advance(p);
        char *name = token_text(&p->previous);
        Expr *ident = expr_ident(name);
        free(name);
        return ident;
    }

    if (match(p, TOK_SELF)) {
        return expr_ident("self");
    }

    if (match(p, TOK_LPAREN)) {
        Expr *expr = expression(p);
        consume(p, TOK_RPAREN, "Expect ')' after expression");
        return expr;
    }

    // Object literal: { field: value, ... }
    // Also supports shorthand: { name } means { name: name }
    // Also supports spread: { ...obj } copies all fields from obj
    if (match(p, TOK_LBRACE)) {
        int capacity = 32;
        char **field_names = malloc(sizeof(char*) * capacity);
        Expr **field_values = malloc(sizeof(Expr*) * capacity);
        if (!field_names || !field_values) {
            free(field_names);
            free(field_values);
            error(p, "Memory allocation failed for object literal");
            return expr_object_literal(NULL, NULL, 0);
        }
        int num_fields = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            // Grow arrays if needed
            if (num_fields >= capacity) {
                int new_capacity = capacity * 2;
                char **new_field_names = realloc(field_names, sizeof(char*) * new_capacity);
                if (!new_field_names) {
                    error(p, "Memory allocation failed for object literal");
                    break;
                }
                field_names = new_field_names;

                Expr **new_field_values = realloc(field_values, sizeof(Expr*) * new_capacity);
                if (!new_field_values) {
                    error(p, "Memory allocation failed for object literal");
                    break;
                }
                field_values = new_field_values;
                capacity = new_capacity;
            }

            // Check for spread operator: ...expr
            if (match(p, TOK_DOT_DOT_DOT)) {
                field_names[num_fields] = NULL;  // NULL marks spread
                field_values[num_fields] = expression(p);
                num_fields++;
            } else if (check(p, TOK_FN) ||
                       (check(p, TOK_ASYNC) && p->next.type == TOK_FN)) {
                // Method shorthand: { fn name(...) { ... } }
                // Sugar for { name: fn(...) { ... } }; entries are still
                // comma-separated like any other field.
                int method_is_async = 0;
                if (check(p, TOK_ASYNC)) {
                    advance(p);  // consume 'async'
                    method_is_async = 1;
                }
                consume(p, TOK_FN, "Expect 'fn'");
                field_names[num_fields] = consume_identifier_or_type_keyword(
                    p, "Expect method name after 'fn' in object literal");
                field_values[num_fields] = fn_expression_rest(p, method_is_async);
                num_fields++;
            } else if (match(p, TOK_STRING)) {
                // Quoted field name: { "chat-mahou": value }
                // Required when the key contains characters that aren't valid
                // in a bare identifier (hyphens, spaces, leading digits, etc.).
                // Shorthand is not allowed for string keys - a colon is required.
                field_names[num_fields] = p->previous.string_value;
                p->previous.string_value = NULL;  // ownership transferred
                consume(p, TOK_COLON, "Expect ':' after string field name");
                field_values[num_fields] = expression(p);
                num_fields++;
            } else {
                // Normal field or shorthand
                if (!check_identifier_or_type_keyword(p)) {
                    error_at_current(p,
                        "Expect field name (use a bare identifier, or quote keys "
                        "with non-identifier characters: { \"my-key\": value })");
                    // Skip the offending token to avoid an infinite loop
                    if (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                        advance(p);
                    }
                    break;
                }
                field_names[num_fields] = consume_identifier_or_type_keyword(p, "Expect field name");

                // Check for shorthand: { name } vs { name: value }
                if (check(p, TOK_COMMA) || check(p, TOK_RBRACE)) {
                    // Shorthand syntax: { name } means { name: name }
                    field_values[num_fields] = expr_ident(field_names[num_fields]);
                } else {
                    consume(p, TOK_COLON, "Expect ':' after field name");
                    field_values[num_fields] = expression(p);
                }
                num_fields++;
            }

            if (!match(p, TOK_COMMA)) break;
        }

        consume(p, TOK_RBRACE, "Expect '}' after object fields");

        return expr_object_literal(field_names, field_values, num_fields);
    }

    // Array literal: [elem1, elem2, ...]
    // Supports trailing commas: [1, 2, 3,]
    if (match(p, TOK_LBRACKET)) {
        int capacity = 64;
        Expr **elements = malloc(sizeof(Expr*) * capacity);
        if (!elements) {
            error(p, "Memory allocation failed for array literal");
            return expr_array_literal(NULL, 0);
        }
        int num_elements = 0;

        if (!check(p, TOK_RBRACKET)) {
            do {
                // Allow trailing comma before closing bracket
                if (check(p, TOK_RBRACKET)) break;
                // Grow array if needed
                if (num_elements >= capacity) {
                    capacity *= 2;
                    Expr **new_elements = realloc(elements, sizeof(Expr*) * capacity);
                    if (!new_elements) {
                        error(p, "Memory allocation failed for array literal");
                        break;
                    }
                    elements = new_elements;
                }
                elements[num_elements++] = expression(p);
            } while (match(p, TOK_COMMA));
        }

        consume(p, TOK_RBRACKET, "Expect ']' after array elements");

        return expr_array_literal(elements, num_elements);
    }


    // Allow type keywords to be used as identifiers (for sizeof, talloc, etc.)
    if (match(p, TOK_TYPE_I8)) return expr_ident("i8");
    if (match(p, TOK_TYPE_I16)) return expr_ident("i16");
    if (match(p, TOK_TYPE_I32)) return expr_ident("i32");
    if (match(p, TOK_TYPE_I64)) return expr_ident("i64");
    if (match(p, TOK_TYPE_INTEGER)) return expr_ident("integer");
    if (match(p, TOK_TYPE_U8)) return expr_ident("u8");
    if (match(p, TOK_TYPE_U16)) return expr_ident("u16");
    if (match(p, TOK_TYPE_U32)) return expr_ident("u32");
    if (match(p, TOK_TYPE_U64)) return expr_ident("u64");
    if (match(p, TOK_TYPE_BYTE)) return expr_ident("byte");
    if (match(p, TOK_TYPE_F32)) return expr_ident("f32");
    if (match(p, TOK_TYPE_F64)) return expr_ident("f64");
    if (match(p, TOK_TYPE_NUMBER)) return expr_ident("number");
    if (match(p, TOK_TYPE_PTR)) return expr_ident("ptr");
    if (match(p, TOK_TYPE_BUFFER)) return expr_ident("buffer");
    if (match(p, TOK_TYPE_ARRAY)) return expr_ident("array");
    if (match(p, TOK_TYPE_STRING)) return expr_ident("string");
    if (match(p, TOK_TYPE_RUNE)) return expr_ident("rune");
    if (match(p, TOK_TYPE_BOOL)) return expr_ident("bool");

    error(p, "Expect expression");
    return expr_number(0);
}

Expr* postfix(Parser *p) {
    Expr *expr = primary(p);

    // Handle chained property access, indexing, method calls, and postfix operators
    for (;;) {
        if (match(p, TOK_QUESTION_LBRACKET)) {
            // Safe indexing: obj?[index] - returns null if obj is null or key missing
            Expr *index = expression(p);
            consume(p, TOK_RBRACKET, "Expect ']' after optional chaining index");
            expr = expr_optional_chain_index(expr, index);
            continue;
        }
        if (match(p, TOK_QUESTION_DOT)) {
            // Optional chaining: obj?.property, obj?.[index], or obj?.method()
            if (match(p, TOK_LBRACKET)) {
                // Optional indexing: obj?.[index]
                Expr *index = expression(p);
                consume(p, TOK_RBRACKET, "Expect ']' after optional chaining index");
                expr = expr_optional_chain_index(expr, index);
            } else if (check(p, TOK_LPAREN)) {
                // Optional call: obj?.() - supports trailing commas
                match(p, TOK_LPAREN);
                Expr **args = NULL;
                char **arg_names = NULL;
                int num_args = 0;
                int has_named_args = 0;
                int seen_positional_after_named = 0;

                if (!check(p, TOK_RPAREN)) {
                    args = malloc(sizeof(Expr*) * MAX_FUNCTION_PARAMS);
                    arg_names = malloc(sizeof(char*) * MAX_FUNCTION_PARAMS);

                    // Parse first argument - check for named argument
                    if ((p->current.type == TOK_IDENT || is_identifier_or_type_keyword(p->current.type)) && p->next.type == TOK_COLON) {
                        has_named_args = 1;
                        arg_names[num_args] = consume_identifier_or_type_keyword(p, "Expect argument name");
                        consume(p, TOK_COLON, "Expect ':' after named argument");
                        args[num_args++] = expression(p);
                    } else {
                        arg_names[num_args] = NULL;
                        args[num_args++] = expression(p);
                    }

                    while (match(p, TOK_COMMA)) {
                        // Allow trailing comma before closing paren
                        if (check(p, TOK_RPAREN)) break;

                        if (num_args >= MAX_FUNCTION_PARAMS) {
                            error_at(p, &p->current, "function calls cannot have more than 64 arguments");
                            break;
                        }

                        // Check for named argument
                        if ((p->current.type == TOK_IDENT || is_identifier_or_type_keyword(p->current.type)) && p->next.type == TOK_COLON) {
                            has_named_args = 1;
                            arg_names[num_args] = consume_identifier_or_type_keyword(p, "Expect argument name");
                            consume(p, TOK_COLON, "Expect ':' after named argument");
                            args[num_args++] = expression(p);
                        } else {
                            if (has_named_args) {
                                seen_positional_after_named = 1;
                            }
                            arg_names[num_args] = NULL;
                            args[num_args++] = expression(p);
                        }
                    }

                    if (seen_positional_after_named) {
                        error_at(p, &p->previous, "Positional arguments cannot follow named arguments");
                    }
                }

                consume(p, TOK_RPAREN, "Expect ')' after optional chaining arguments");

                if (!has_named_args && arg_names) {
                    free(arg_names);
                    arg_names = NULL;
                }

                expr = expr_optional_chain_call(expr, args, arg_names, num_args);
            } else {
                // Optional property access: obj?.property
                char *property = consume_identifier_or_type_keyword(p, "Expect property name after '?.'");
                expr = expr_optional_chain_property(expr, property);
                free(property);
            }
        } else if (match(p, TOK_DOT)) {
            // Property access: obj.property
            int prop_line = p->previous.line;     // Save line of '.' for error reporting
            int prop_column = p->previous.column;
            char *property = consume_identifier_or_type_keyword(p, "Expect property name after '.'");
            expr = expr_get_property(expr, property);
            expr->line = prop_line;               // Set line for error messages
            expr->column = prop_column;
            free(property);
        } else if (match(p, TOK_LBRACKET)) {
            // Indexing: obj[index]
            int index_line = p->previous.line;    // Save line of '[' for error reporting
            int index_column = p->previous.column;
            Expr *index = expression(p);
            consume(p, TOK_RBRACKET, "Expect ']' after index");
            expr = expr_index(expr, index);
            expr->line = index_line;              // Set line for error messages
            expr->column = index_column;
        } else if (match(p, TOK_LPAREN)) {
            // Function call: func(...) or obj.method(...)
            // Supports trailing commas: func(a, b, c,)
            int call_line = p->previous.line;  // Save line of '(' for stack trace
            int call_column = p->previous.column;
            Expr **args = NULL;
            char **arg_names = NULL;
            int num_args = 0;
            int has_named_args = 0;
            int seen_positional_after_named = 0;

            if (!check(p, TOK_RPAREN)) {
                args = malloc(sizeof(Expr*) * MAX_FUNCTION_PARAMS);
                arg_names = malloc(sizeof(char*) * MAX_FUNCTION_PARAMS);

                // Parse first argument
                // Check for named argument: identifier followed by ':'
                if ((p->current.type == TOK_IDENT || is_identifier_or_type_keyword(p->current.type)) && p->next.type == TOK_COLON) {
                    // Named argument
                    has_named_args = 1;
                    arg_names[num_args] = consume_identifier_or_type_keyword(p, "Expect argument name");
                    consume(p, TOK_COLON, "Expect ':' after named argument");
                    args[num_args++] = expression(p);
                } else {
                    arg_names[num_args] = NULL;
                    args[num_args++] = expression(p);
                }

                while (match(p, TOK_COMMA)) {
                    // Allow trailing comma before closing paren
                    if (check(p, TOK_RPAREN)) break;

                    if (num_args >= MAX_FUNCTION_PARAMS) {
                        error_at(p, &p->current, "function calls cannot have more than 64 arguments");
                        break;
                    }

                    // Check for named argument
                    if ((p->current.type == TOK_IDENT || is_identifier_or_type_keyword(p->current.type)) && p->next.type == TOK_COLON) {
                        // Named argument
                        has_named_args = 1;
                        arg_names[num_args] = consume_identifier_or_type_keyword(p, "Expect argument name");
                        consume(p, TOK_COLON, "Expect ':' after named argument");
                        args[num_args++] = expression(p);
                    } else {
                        // Positional argument
                        if (has_named_args) {
                            seen_positional_after_named = 1;
                        }
                        arg_names[num_args] = NULL;
                        args[num_args++] = expression(p);
                    }
                }

                // Warn if positional arguments come after named arguments
                if (seen_positional_after_named) {
                    error_at(p, &p->previous, "Positional arguments cannot follow named arguments");
                }
            }

            consume(p, TOK_RPAREN, "Expect ')' after arguments");

            // If no named arguments were used, free the arg_names array
            if (!has_named_args && arg_names) {
                free(arg_names);
                arg_names = NULL;
            }

            expr = expr_call(expr, args, arg_names, num_args);
            expr->line = call_line;  // Set line number for stack trace
            expr->column = call_column;
        } else if (match(p, TOK_PLUS_PLUS)) {
            // Postfix increment: x++
            expr = expr_postfix_inc(expr);
        } else if (match(p, TOK_MINUS_MINUS)) {
            // Postfix decrement: x--
            expr = expr_postfix_dec(expr);
        } else {
            break;
        }
    }

    return expr;
}

static Expr* unary_inner(Parser *p);

// Public entry: bound expression-recursion depth so deeply nested input
// (e.g. "((((...))))", "[[[[...]]]]", or a long prefix-operator chain) raises a
// parse error instead of overflowing the C stack. Every nesting level descends
// through unary() exactly once, and prefix operators recurse here directly, so
// this single guard covers all expression-nesting vectors.
Expr* unary(Parser *p) {
    if (p->parse_depth >= HML_MAX_PARSE_DEPTH) {
        error_at_current(p, "Maximum expression nesting depth exceeded");
        // Drain to EOF so all enclosing parse loops unwind immediately instead
        // of attempting per-token error recovery (which can be quadratic for
        // pathologically nested input). Only reachable past the depth limit, so
        // valid code is unaffected.
        while (p->current.type != TOK_EOF) advance(p);
        return expr_null();
    }
    p->parse_depth++;
    Expr *e = unary_inner(p);
    p->parse_depth--;
    return e;
}

static Expr* unary_inner(Parser *p) {
    if (match(p, TOK_AWAIT)) {
        Expr *operand = unary(p);  // Recursive for multiple await/unary ops
        return expr_await(operand);
    }

    if (match(p, TOK_BANG)) {
        Expr *operand = unary(p);  // Recursive for multiple unary ops
        return expr_unary(UNARY_NOT, operand);
    }

    if (match(p, TOK_MINUS)) {
        Expr *operand = unary(p);
        return expr_unary(UNARY_NEGATE, operand);
    }

    if (match(p, TOK_TILDE)) {
        Expr *operand = unary(p);
        return expr_unary(UNARY_BIT_NOT, operand);
    }

    if (match(p, TOK_PLUS_PLUS)) {
        Expr *operand = unary(p);
        return expr_prefix_inc(operand);
    }

    if (match(p, TOK_MINUS_MINUS)) {
        Expr *operand = unary(p);
        return expr_prefix_dec(operand);
    }

    return postfix(p);
}

Expr* factor(Parser *p) {
    Expr *expr = unary(p);

    while (match(p, TOK_STAR) || match(p, TOK_SLASH) || match(p, TOK_PERCENT)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_STAR) ? OP_MUL :
                      (op_type == TOK_SLASH) ? OP_DIV : OP_MOD;
        Expr *right = unary(p);
        expr = expr_binary(expr, op, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* term(Parser *p) {
    Expr *expr = factor(p);

    while (match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_PLUS) ? OP_ADD : OP_SUB;
        Expr *right = factor(p);
        expr = expr_binary(expr, op, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* shift(Parser *p) {
    Expr *expr = term(p);

    while (match(p, TOK_LESS_LESS) || match(p, TOK_GREATER_GREATER)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_LESS_LESS) ? OP_BIT_LSHIFT : OP_BIT_RSHIFT;
        Expr *right = term(p);
        expr = expr_binary(expr, op, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* comparison(Parser *p) {
    Expr *expr = shift(p);

    while (match(p, TOK_GREATER) || match(p, TOK_GREATER_EQUAL) ||
           match(p, TOK_LESS) || match(p, TOK_LESS_EQUAL)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        TokenType op_type = p->previous.type;
        BinaryOp op;

        switch (op_type) {
            case TOK_GREATER: op = OP_GREATER; break;
            case TOK_GREATER_EQUAL: op = OP_GREATER_EQUAL; break;
            case TOK_LESS: op = OP_LESS; break;
            case TOK_LESS_EQUAL: op = OP_LESS_EQUAL; break;
            default: op = OP_ADD; break;
        }

        Expr *right = shift(p);
        expr = expr_binary(expr, op, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* equality(Parser *p) {
    Expr *expr = comparison(p);

    while (match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQUAL)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_EQUAL_EQUAL) ? OP_EQUAL : OP_NOT_EQUAL;
        Expr *right = comparison(p);
        expr = expr_binary(expr, op, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* bitwise_and(Parser *p) {
    Expr *expr = equality(p);

    while (match(p, TOK_AMP)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        Expr *right = equality(p);
        expr = expr_binary(expr, OP_BIT_AND, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* bitwise_xor(Parser *p) {
    Expr *expr = bitwise_and(p);

    while (match(p, TOK_CARET)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        Expr *right = bitwise_and(p);
        expr = expr_binary(expr, OP_BIT_XOR, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* bitwise_or(Parser *p) {
    Expr *expr = bitwise_xor(p);

    while (match(p, TOK_PIPE)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        Expr *right = bitwise_xor(p);
        expr = expr_binary(expr, OP_BIT_OR, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* logical_and(Parser *p) {
    Expr *expr = bitwise_or(p);

    while (match(p, TOK_AMP_AMP)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        Expr *right = bitwise_or(p);
        expr = expr_binary(expr, OP_AND, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* logical_or(Parser *p) {
    Expr *expr = logical_and(p);

    while (match(p, TOK_PIPE_PIPE)) {
        int op_line = p->previous.line;
        int op_column = p->previous.column;
        Expr *right = logical_and(p);
        expr = expr_binary(expr, OP_OR, right);
        expr->line = op_line;
        expr->column = op_column;
    }

    return expr;
}

Expr* null_coalesce(Parser *p) {
    Expr *expr = logical_or(p);

    while (match(p, TOK_QUESTION_QUESTION)) {
        Expr *right = logical_or(p);
        expr = expr_null_coalesce(expr, right);
    }

    return expr;
}

Expr* ternary(Parser *p) {
    Expr *expr = null_coalesce(p);

    if (match(p, TOK_QUESTION)) {
        Expr *true_expr = expression(p);
        consume(p, TOK_COLON, "Expect ':' after true expression in ternary operator");
        Expr *false_expr = ternary(p);  // Right-associative
        return expr_ternary(expr, true_expr, false_expr);
    }

    return expr;
}

Expr* assignment(Parser *p) {
    Expr *expr = ternary(p);

    // Check for compound assignment operators (+=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=)
    BinaryOp compound_op;
    int is_compound = 0;

    if (match(p, TOK_PLUS_EQUAL)) {
        compound_op = OP_ADD;
        is_compound = 1;
    } else if (match(p, TOK_MINUS_EQUAL)) {
        compound_op = OP_SUB;
        is_compound = 1;
    } else if (match(p, TOK_STAR_EQUAL)) {
        compound_op = OP_MUL;
        is_compound = 1;
    } else if (match(p, TOK_SLASH_EQUAL)) {
        compound_op = OP_DIV;
        is_compound = 1;
    } else if (match(p, TOK_PERCENT_EQUAL)) {
        compound_op = OP_MOD;
        is_compound = 1;
    } else if (match(p, TOK_AMP_EQUAL)) {
        compound_op = OP_BIT_AND;
        is_compound = 1;
    } else if (match(p, TOK_PIPE_EQUAL)) {
        compound_op = OP_BIT_OR;
        is_compound = 1;
    } else if (match(p, TOK_CARET_EQUAL)) {
        compound_op = OP_BIT_XOR;
        is_compound = 1;
    } else if (match(p, TOK_LESS_LESS_EQUAL)) {
        compound_op = OP_BIT_LSHIFT;
        is_compound = 1;
    } else if (match(p, TOK_GREATER_GREATER_EQUAL)) {
        compound_op = OP_BIT_RSHIFT;
        is_compound = 1;
    }

    // Handle null coalescing assignment: x ??= value becomes x = x ?? value
    if (match(p, TOK_QUESTION_QUESTION_EQUAL)) {
        Expr *rhs = assignment(p);

        if (expr->type == EXPR_IDENT) {
            // Variable null coalescing assignment: x ??= value
            const char *name = expr->as.ident.name;
            Expr *lhs_copy = expr_ident(name);
            Expr *null_coalesce = expr_null_coalesce(lhs_copy, rhs);
            Expr *result = expr_assign(name, null_coalesce);
            expr_free(expr);
            return result;
        } else if (expr->type == EXPR_INDEX) {
            // Index null coalescing assignment: arr[i] ??= value
            Expr *object = expr->as.index.object;
            Expr *index = expr->as.index.index;
            int line = expr->line;  // Preserve line number

            // Clone for the RHS
            Expr *object_clone = expr_clone(object);
            Expr *index_clone = expr_clone(index);

            // Create the read expression: arr[i]
            Expr *read_expr = expr_index(object_clone, index_clone);

            // Create the null coalesce operation: arr[i] ?? value
            Expr *null_coalesce = expr_null_coalesce(read_expr, rhs);

            // Steal the object and index from the EXPR_INDEX for the assignment
            expr->as.index.object = NULL;
            expr->as.index.index = NULL;
            expr_free(expr);

            // Create the assignment: arr[i] = arr[i] ?? value
            Expr *result = expr_index_assign(object, index, null_coalesce);
            result->line = line;  // Set line number for error messages
            return result;
        } else if (expr->type == EXPR_GET_PROPERTY) {
            // Property null coalescing assignment: obj.field ??= value
            Expr *object = expr->as.get_property.object;
            const char *property = expr->as.get_property.property;

            // Clone the object for the RHS
            Expr *object_clone = expr_clone(object);

            // Create the read expression: obj.field
            Expr *read_expr = expr_get_property(object_clone, property);

            // Create the null coalesce operation: obj.field ?? value
            Expr *null_coalesce = expr_null_coalesce(read_expr, rhs);

            // Steal the object from the EXPR_GET_PROPERTY for the assignment
            expr->as.get_property.object = NULL;

            // Create the assignment before freeing expr (need property string)
            Expr *result = expr_set_property(object, property, null_coalesce);
            expr_free(expr);
            return result;
        } else {
            error(p, "Invalid null coalescing assignment target");
            expr_free(expr);
            return expr_null();
        }
    }

    if (is_compound) {
        // Desugar compound assignment: x += 5 becomes x = x + 5
        Expr *rhs = assignment(p);

        if (expr->type == EXPR_IDENT) {
            // Variable compound assignment: x += 5
            // Note: expr_ident and expr_assign both strdup internally
            const char *name = expr->as.ident.name;
            Expr *lhs_copy = expr_ident(name);
            Expr *binary = expr_binary(lhs_copy, compound_op, rhs);
            Expr *result = expr_assign(name, binary);
            expr_free(expr);
            return result;
        } else if (expr->type == EXPR_INDEX) {
            // Index compound assignment: arr[i] += 5
            // Desugar to: arr[i] = arr[i] + 5
            // We clone the object and index expressions to avoid evaluating twice
            Expr *object = expr->as.index.object;
            Expr *index = expr->as.index.index;
            int line = expr->line;  // Preserve line number

            // Clone for the RHS
            Expr *object_clone = expr_clone(object);
            Expr *index_clone = expr_clone(index);

            // Create the read expression: arr[i]
            Expr *read_expr = expr_index(object_clone, index_clone);

            // Create the binary operation: arr[i] + 5
            Expr *binary = expr_binary(read_expr, compound_op, rhs);

            // Steal the object and index from the EXPR_INDEX for the assignment
            expr->as.index.object = NULL;
            expr->as.index.index = NULL;
            expr_free(expr);

            // Create the assignment: arr[i] = arr[i] + 5
            Expr *result = expr_index_assign(object, index, binary);
            result->line = line;  // Set line number for error messages
            return result;
        } else if (expr->type == EXPR_GET_PROPERTY) {
            // Property compound assignment: obj.field += 5
            // Desugar to: obj.field = obj.field + 5
            Expr *object = expr->as.get_property.object;
            // Note: expr_get_property and expr_set_property both strdup internally
            const char *property = expr->as.get_property.property;

            // Clone the object for the RHS
            Expr *object_clone = expr_clone(object);

            // Create the read expression: obj.field
            Expr *read_expr = expr_get_property(object_clone, property);

            // Create the binary operation: obj.field + 5
            Expr *binary = expr_binary(read_expr, compound_op, rhs);

            // Steal the object from the EXPR_GET_PROPERTY for the assignment
            expr->as.get_property.object = NULL;

            // Create the assignment before freeing expr (need property string)
            Expr *result = expr_set_property(object, property, binary);
            expr_free(expr);
            return result;
        } else {
            error(p, "Invalid compound assignment target");
            expr_free(expr);
            return expr_null();
        }
    }

    if (match(p, TOK_EQUAL)) {
        // Check what kind of assignment target we have
        if (expr->type == EXPR_IDENT) {
            // Regular variable assignment
            // Note: expr_assign strdups internally
            const char *name = expr->as.ident.name;
            Expr *value = assignment(p);
            Expr *result = expr_assign(name, value);
            expr_free(expr);
            return result;
        } else if (expr->type == EXPR_INDEX) {
            // Index assignment: obj[index] = value
            Expr *object = expr->as.index.object;
            Expr *index = expr->as.index.index;
            int line = expr->line;  // Preserve line number
            Expr *value = assignment(p);

            // Steal the object and index from the EXPR_INDEX
            // (so we don't double-free them)
            expr->as.index.object = NULL;
            expr->as.index.index = NULL;
            expr_free(expr);

            Expr *result = expr_index_assign(object, index, value);
            result->line = line;  // Set line number for error messages
            return result;
        } else if (expr->type == EXPR_GET_PROPERTY) {
            // Property assignment: obj.field = value
            Expr *object = expr->as.get_property.object;
            // Note: expr_set_property strdups internally
            const char *property = expr->as.get_property.property;
            Expr *value = assignment(p);

            // Steal the object from the EXPR_GET_PROPERTY
            expr->as.get_property.object = NULL;

            // Create result before freeing expr (need property string)
            Expr *result = expr_set_property(object, property, value);
            expr_free(expr);
            return result;
        } else {
            error(p, "Invalid assignment target");
            return expr;
        }
    }

    return expr;
}

Expr* expression(Parser *p) {
    return assignment(p);
}

// Parse function type: fn(params): return_type or async fn(params): return_type
static Type* parse_function_type(Parser *p) {
    int is_async = 0;

    // Check for 'async fn'
    if (p->current.type == TOK_ASYNC) {
        is_async = 1;
        advance(p);
        if (p->current.type != TOK_FN) {
            error_at_current(p, "Expected 'fn' after 'async' in type");
            return type_new(TYPE_INFER);
        }
    }

    // Consume 'fn'
    advance(p);

    // Consume '('
    if (p->current.type != TOK_LPAREN) {
        error_at_current(p, "Expected '(' after 'fn' in function type");
        return type_new(TYPE_INFER);
    }
    advance(p);

    // Parse parameters
    int param_capacity = 8;
    Type **param_types = malloc(sizeof(Type*) * param_capacity);
    char **param_names = malloc(sizeof(char*) * param_capacity);
    int *param_optional = malloc(sizeof(int) * param_capacity);
    int *param_is_const = malloc(sizeof(int) * param_capacity);
    int num_params = 0;
    char *rest_param_name = NULL;
    Type *rest_param_type = NULL;

    // Parse function type parameters - supports trailing commas
    if (p->current.type != TOK_RPAREN) {
        do {
            // Allow trailing comma before closing paren
            if (p->current.type == TOK_RPAREN) break;

            // Check for rest parameter: ...name or ...name: type
            if (p->current.type == TOK_DOT_DOT_DOT) {
                advance(p);
                if (p->current.type == TOK_IDENT) {
                    rest_param_name = token_text(&p->current);
                    advance(p);
                }
                if (p->current.type == TOK_COLON) {
                    advance(p);
                    rest_param_type = parse_type(p);
                }
                break;  // Rest must be last
            }

            if (num_params >= param_capacity) {
                int new_capacity = param_capacity * 2;
                Type **new_param_types = realloc(param_types, sizeof(Type*) * new_capacity);
                if (!new_param_types) {
                    free(param_types);
                    free(param_names);
                    free(param_optional);
                    free(param_is_const);
                    return type_new(TYPE_INFER);
                }
                param_types = new_param_types;

                char **new_param_names = realloc(param_names, sizeof(char*) * new_capacity);
                if (!new_param_names) {
                    free(param_types);
                    free(param_names);
                    free(param_optional);
                    free(param_is_const);
                    return type_new(TYPE_INFER);
                }
                param_names = new_param_names;

                int *new_param_optional = realloc(param_optional, sizeof(int) * new_capacity);
                if (!new_param_optional) {
                    free(param_types);
                    free(param_names);
                    free(param_optional);
                    free(param_is_const);
                    return type_new(TYPE_INFER);
                }
                param_optional = new_param_optional;

                int *new_param_is_const = realloc(param_is_const, sizeof(int) * new_capacity);
                if (!new_param_is_const) {
                    free(param_types);
                    free(param_names);
                    free(param_optional);
                    free(param_is_const);
                    return type_new(TYPE_INFER);
                }
                param_is_const = new_param_is_const;
                param_capacity = new_capacity;
            }

            // Check for const modifier
            int is_const = 0;
            if (p->current.type == TOK_CONST) {
                is_const = 1;
                advance(p);
            }
            param_is_const[num_params] = is_const;

            // Check for optional parameter marker (?)
            int is_optional = 0;
            if (p->current.type == TOK_QUESTION) {
                is_optional = 1;
                advance(p);
            }
            param_optional[num_params] = is_optional;

            // Check if this is "name: type" or just "type"
            // Lookahead: if IDENT followed by COLON, it's a named param
            if (p->current.type == TOK_IDENT && p->next.type == TOK_COLON) {
                param_names[num_params] = token_text(&p->current);
                advance(p);  // consume name
                advance(p);  // consume ':'
                param_types[num_params] = parse_type(p);
            } else {
                // Just a type
                param_names[num_params] = NULL;
                param_types[num_params] = parse_type(p);
            }

            num_params++;
        } while (match(p, TOK_COMMA));
    }

    // Consume ')'
    if (p->current.type != TOK_RPAREN) {
        error_at_current(p, "Expected ')' after function type parameters");
        free(param_types);
        free(param_names);
        free(param_optional);
        free(param_is_const);
        return type_new(TYPE_INFER);
    }
    advance(p);

    // Optional return type
    Type *return_type = NULL;
    if (p->current.type == TOK_COLON) {
        advance(p);
        return_type = parse_type(p);
    }

    return type_function(param_types, param_names, param_optional, param_is_const,
                         num_params, rest_param_name, rest_param_type, return_type, is_async);
}

// Parse a single base type (not compound)
static Type* parse_single_type(Parser *p) {
    TypeKind kind;
    Type *type = NULL;

    // Check for function type: fn(...) or async fn(...)
    if (p->current.type == TOK_FN ||
        (p->current.type == TOK_ASYNC && p->next.type == TOK_FN)) {
        type = parse_function_type(p);

        // Check for nullable function type
        if (p->current.type == TOK_QUESTION) {
            advance(p);
            type->nullable = 1;
        }
        return type;
    }

    // Check for Self type (in define blocks)
    if (p->current.type == TOK_SELF) {
        advance(p);
        type = type_self();

        // Check for nullable
        if (p->current.type == TOK_QUESTION) {
            advance(p);
            type->nullable = 1;
        }
        return type;
    }

    // Check for 'array' or 'array<type>' syntax
    if (p->current.type == TOK_TYPE_ARRAY) {
        advance(p);
        Type *element_type = NULL;

        // Optional: <type> syntax for typed arrays
        if (p->current.type == TOK_LESS) {
            advance(p);  // consume '<'
            element_type = parse_single_type(p);
            consume(p, TOK_GREATER, "Expect '>' after array element type");
        }
        // If no '<', element_type stays NULL (untyped array)

        type = type_new(TYPE_ARRAY);
        type->type_name = NULL;
        type->element_type = element_type;
    }
    // Check for 'object' keyword (generic object type)
    else if (p->current.type == TOK_OBJECT) {
        advance(p);
        type = type_new(TYPE_GENERIC_OBJECT);
        type->type_name = NULL;
    }
    // Check for identifier (could be type parameter or custom object type)
    else if (p->current.type == TOK_IDENT) {
        char *type_name = token_text(&p->current);
        advance(p);

        // Check if it's a type parameter in scope
        int is_type_param = 0;
        for (int i = 0; i < p->num_type_params; i++) {
            if (strcmp(p->type_params[i], type_name) == 0) {
                is_type_param = 1;
                break;
            }
        }

        if (is_type_param) {
            // It's a type parameter reference (e.g., T in items: array<T>)
            type = type_new(TYPE_PARAM);
            type->type_name = type_name;
        } else {
            // It's a custom object type (e.g., Person, Stack<i32>)
            type = type_new(TYPE_CUSTOM_OBJECT);
            type->type_name = type_name;

            // Check for type arguments: <type, type, ...>
            if (p->current.type == TOK_LESS) {
                advance(p);  // consume '<'

                int type_arg_capacity = 4;
                Type **type_args = malloc(sizeof(Type*) * type_arg_capacity);
                int num_type_args = 0;

                do {
                    if (num_type_args >= type_arg_capacity) {
                        type_arg_capacity *= 2;
                        Type **new_type_args = realloc(type_args, sizeof(Type*) * type_arg_capacity);
                        if (!new_type_args) {
                            free(type_args);
                            return type_new(TYPE_INFER);
                        }
                        type_args = new_type_args;
                    }
                    type_args[num_type_args++] = parse_type(p);
                } while (match(p, TOK_COMMA));

                consume(p, TOK_GREATER, "Expect '>' after type arguments");

                type->type_args = type_args;
                type->num_type_args = num_type_args;
            }
        }
    }
    else {
        switch (p->current.type) {
            case TOK_TYPE_I8: kind = TYPE_I8; break;
            case TOK_TYPE_I16: kind = TYPE_I16; break;
            case TOK_TYPE_I32: kind = TYPE_I32; break;
            case TOK_TYPE_I64: kind = TYPE_I64; break;
            case TOK_TYPE_INTEGER: kind = TYPE_I32; break;  // alias
            case TOK_TYPE_U8: kind = TYPE_U8; break;
            case TOK_TYPE_BYTE: kind = TYPE_U8; break;  // alias
            case TOK_TYPE_U16: kind = TYPE_U16; break;
            case TOK_TYPE_U32: kind = TYPE_U32; break;
            case TOK_TYPE_U64: kind = TYPE_U64; break;
            //case TOK_TYPE_F16: kind = TYPE_F16; break;
            case TOK_TYPE_F32: kind = TYPE_F32; break;
            case TOK_TYPE_F64: kind = TYPE_F64; break;
            case TOK_TYPE_NUMBER: kind = TYPE_F64; break;  // alias
            case TOK_TYPE_BOOL: kind = TYPE_BOOL; break;
            case TOK_TYPE_STRING: kind = TYPE_STRING; break;
            case TOK_TYPE_RUNE: kind = TYPE_RUNE; break;
            case TOK_TYPE_PTR: kind = TYPE_PTR; break;
            case TOK_TYPE_BUFFER: kind = TYPE_BUFFER; break;
            case TOK_TYPE_VOID: kind = TYPE_VOID; break;
            case TOK_NULL: kind = TYPE_NULL; break;
            default:
                error_at_current(p, "Expect type name");
                return type_new(TYPE_INFER);
        }

        advance(p);
        type = type_new(kind);
        type->type_name = NULL;
    }

    // Check for nullable type syntax: type?
    if (p->current.type == TOK_QUESTION) {
        advance(p);
        type->nullable = 1;
    }

    return type;
}

// Parse a type, including compound types (A & B & C)
Type* parse_type(Parser *p) {
    Type *first = parse_single_type(p);

    // Check for compound type syntax: Type1 & Type2 & ...
    if (p->current.type == TOK_AMP) {
        // Collect all types in the compound
        int capacity = 4;
        int count = 1;
        Type **types = malloc(sizeof(Type*) * capacity);
        types[0] = first;

        while (p->current.type == TOK_AMP) {
            advance(p);  // consume '&'

            if (count >= capacity) {
                capacity *= 2;
                Type **new_types = realloc(types, sizeof(Type*) * capacity);
                if (!new_types) {
                    free(types);
                    return first;  // Return first type on allocation failure
                }
                types = new_types;
            }

            types[count++] = parse_single_type(p);
        }

        // Create the compound type
        return type_compound(types, count);
    }

    return first;
}


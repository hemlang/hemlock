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
Expr* primary(Parser *p);
Type* parse_type(Parser *p);

// Helper: Check if current token is a type keyword and can be used as identifier
static int is_type_keyword(TokenType type) {
    return type == TOK_TYPE_I8 || type == TOK_TYPE_I16 || type == TOK_TYPE_I32 || type == TOK_TYPE_I64 ||
           type == TOK_TYPE_U8 || type == TOK_TYPE_U16 || type == TOK_TYPE_U32 || type == TOK_TYPE_U64 ||
           type == TOK_TYPE_F32 || type == TOK_TYPE_F64 || type == TOK_TYPE_BOOL || type == TOK_TYPE_STRING ||
           type == TOK_TYPE_RUNE || type == TOK_TYPE_PTR || type == TOK_TYPE_BUFFER || type == TOK_TYPE_ARRAY ||
           type == TOK_TYPE_INTEGER || type == TOK_TYPE_NUMBER || type == TOK_TYPE_BYTE || type == TOK_TYPE_VOID;
}

// Helper: Consume identifier or type keyword (for property/field names)
static char* consume_identifier_or_type(Parser *p, const char *message) {
    if (p->current.type == TOK_IDENT || is_type_keyword(p->current.type)) {
        advance(p);
        return token_text(&p->previous);
    }
    error_at_current(p, message);
    return strdup("error");
}

// Helper: Parse interpolated string with ${...} expressions
static Expr* parse_interpolated_string(Parser *p, const char *str_content) {
    char **string_parts = malloc(sizeof(char*) * 32);  // Array of string literals
    Expr **expr_parts = malloc(sizeof(Expr*) * 32);    // Array of expressions
    int num_parts = 0;
    int capacity = 32;

    const char *ptr = str_content;
    char *current_string = malloc(1024);
    int str_len = 0;
    int str_capacity = 1024;

    while (*ptr != '\0') {
        if (*ptr == '$' && *(ptr + 1) == '{') {
            // Found interpolation start
            // Save current string part
            current_string[str_len] = '\0';
            string_parts[num_parts] = strdup(current_string);
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
                free(current_string);
                return expr_string("");
            }

            // Extract expression text
            int expr_len = ptr - expr_start;
            char *expr_text = malloc(expr_len + 1);
            memcpy(expr_text, expr_start, expr_len);
            expr_text[expr_len] = '\0';

            // Parse the expression using a new parser
            Lexer expr_lexer;
            lexer_init(&expr_lexer, expr_text);

            Parser expr_parser;
            parser_init(&expr_parser, &expr_lexer);

            Expr *interpolated_expr = expression(&expr_parser);
            expr_parts[num_parts] = interpolated_expr;

            // Note: Not freeing expr_text here because the lexer/parser may have pointers into it
            // This is a known memory leak that should be fixed by tracking allocated strings
            // TODO: Track expr_text allocations and free them after AST is no longer needed

            num_parts++;
            if (num_parts >= capacity) {
                capacity *= 2;
                string_parts = realloc(string_parts, sizeof(char*) * capacity);
                expr_parts = realloc(expr_parts, sizeof(Expr*) * capacity);
            }

            ptr++;  // Skip closing }
        } else {
            // Regular character
            if (str_len >= str_capacity - 1) {
                str_capacity *= 2;
                current_string = realloc(current_string, str_capacity);
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
        if (p->previous.is_float) {
            return expr_number_float(p->previous.float_value);
        } else {
            return expr_number_int(p->previous.int_value);
        }
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

    if (match(p, TOK_IDENT)) {
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
    if (match(p, TOK_LBRACE)) {
        char **field_names = malloc(sizeof(char*) * 32);
        Expr **field_values = malloc(sizeof(Expr*) * 32);
        int num_fields = 0;

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            field_names[num_fields] = consume_identifier_or_type(p, "Expect field name");

            consume(p, TOK_COLON, "Expect ':' after field name");
            field_values[num_fields] = expression(p);

            num_fields++;

            if (!match(p, TOK_COMMA)) break;
        }

        consume(p, TOK_RBRACE, "Expect '}' after object fields");

        return expr_object_literal(field_names, field_values, num_fields);
    }

    // Array literal: [elem1, elem2, ...]
    if (match(p, TOK_LBRACKET)) {
        Expr **elements = malloc(sizeof(Expr*) * 256);
        int num_elements = 0;

        if (!check(p, TOK_RBRACKET)) {
            do {
                elements[num_elements++] = expression(p);
            } while (match(p, TOK_COMMA));
        }

        consume(p, TOK_RBRACKET, "Expect ']' after array elements");

        return expr_array_literal(elements, num_elements);
    }

    // Function expression: fn(...) { ... } or async fn(...) { ... }
    int is_async_fn = 0;
    if (match(p, TOK_ASYNC)) {
        is_async_fn = 1;
        consume(p, TOK_FN, "Expect 'fn' after 'async'");
    } else if (match(p, TOK_FN)) {
        is_async_fn = 0;
    } else {
        // Not a function expression, continue to other cases
        goto not_fn_expr;
    }

    // Parse function expression
    consume(p, TOK_LPAREN, "Expect '(' after 'fn'");

    // Parse parameters
    char **param_names = malloc(sizeof(char*) * 32);  // max 32 params
    Type **param_types = malloc(sizeof(Type*) * 32);
    Expr **param_defaults = malloc(sizeof(Expr*) * 32);
    int num_params = 0;
    int seen_optional = 0;  // Track if we've seen an optional parameter

    if (!check(p, TOK_RPAREN)) {
        do {
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

    // Parse body (must be a block)
    consume(p, TOK_LBRACE, "Expect '{' before function body");
    Stmt *body = block_statement(p);

    return expr_function(is_async_fn, param_names, param_types, param_defaults, num_params, return_type, body);

not_fn_expr:

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
        if (match(p, TOK_QUESTION_DOT)) {
            // Optional chaining: obj?.property, obj?.[index], or obj?.method()
            if (match(p, TOK_LBRACKET)) {
                // Optional indexing: obj?.[index]
                Expr *index = expression(p);
                consume(p, TOK_RBRACKET, "Expect ']' after optional chaining index");
                expr = expr_optional_chain_index(expr, index);
            } else if (check(p, TOK_LPAREN)) {
                // Optional call: obj?.()
                match(p, TOK_LPAREN);
                Expr **args = NULL;
                int num_args = 0;

                if (!check(p, TOK_RPAREN)) {
                    args = malloc(sizeof(Expr*) * 8);
                    args[num_args++] = expression(p);

                    while (match(p, TOK_COMMA)) {
                        args[num_args++] = expression(p);
                    }
                }

                consume(p, TOK_RPAREN, "Expect ')' after optional chaining arguments");
                expr = expr_optional_chain_call(expr, args, num_args);
            } else {
                // Optional property access: obj?.property
                char *property = consume_identifier_or_type(p, "Expect property name after '?.'");
                expr = expr_optional_chain_property(expr, property);
                free(property);
            }
        } else if (match(p, TOK_DOT)) {
            // Property access: obj.property
            char *property = consume_identifier_or_type(p, "Expect property name after '.'");
            expr = expr_get_property(expr, property);
            free(property);
        } else if (match(p, TOK_LBRACKET)) {
            // Indexing: obj[index]
            Expr *index = expression(p);
            consume(p, TOK_RBRACKET, "Expect ']' after index");
            expr = expr_index(expr, index);
        } else if (match(p, TOK_LPAREN)) {
            // Function call: func(...) or obj.method(...)
            int call_line = p->previous.line;  // Save line of '(' for stack trace
            Expr **args = NULL;
            int num_args = 0;

            if (!check(p, TOK_RPAREN)) {
                args = malloc(sizeof(Expr*) * 8);
                args[num_args++] = expression(p);

                while (match(p, TOK_COMMA)) {
                    args[num_args++] = expression(p);
                }
            }

            consume(p, TOK_RPAREN, "Expect ')' after arguments");
            expr = expr_call(expr, args, num_args);
            expr->line = call_line;  // Set line number for stack trace
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

Expr* unary(Parser *p) {
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
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_STAR) ? OP_MUL :
                      (op_type == TOK_SLASH) ? OP_DIV : OP_MOD;
        Expr *right = unary(p);
        expr = expr_binary(expr, op, right);
    }

    return expr;
}

Expr* term(Parser *p) {
    Expr *expr = factor(p);
    
    while (match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_PLUS) ? OP_ADD : OP_SUB;
        Expr *right = factor(p);
        expr = expr_binary(expr, op, right);
    }
    
    return expr;
}

Expr* shift(Parser *p) {
    Expr *expr = term(p);

    while (match(p, TOK_LESS_LESS) || match(p, TOK_GREATER_GREATER)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_LESS_LESS) ? OP_BIT_LSHIFT : OP_BIT_RSHIFT;
        Expr *right = term(p);
        expr = expr_binary(expr, op, right);
    }

    return expr;
}

Expr* comparison(Parser *p) {
    Expr *expr = shift(p);

    while (match(p, TOK_GREATER) || match(p, TOK_GREATER_EQUAL) ||
           match(p, TOK_LESS) || match(p, TOK_LESS_EQUAL)) {
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
    }

    return expr;
}

Expr* equality(Parser *p) {
    Expr *expr = comparison(p);

    while (match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQUAL)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_EQUAL_EQUAL) ? OP_EQUAL : OP_NOT_EQUAL;
        Expr *right = comparison(p);
        expr = expr_binary(expr, op, right);
    }

    return expr;
}

Expr* bitwise_and(Parser *p) {
    Expr *expr = equality(p);

    while (match(p, TOK_AMP)) {
        Expr *right = equality(p);
        expr = expr_binary(expr, OP_BIT_AND, right);
    }

    return expr;
}

Expr* bitwise_xor(Parser *p) {
    Expr *expr = bitwise_and(p);

    while (match(p, TOK_CARET)) {
        Expr *right = bitwise_and(p);
        expr = expr_binary(expr, OP_BIT_XOR, right);
    }

    return expr;
}

Expr* bitwise_or(Parser *p) {
    Expr *expr = bitwise_xor(p);

    while (match(p, TOK_PIPE)) {
        Expr *right = bitwise_xor(p);
        expr = expr_binary(expr, OP_BIT_OR, right);
    }

    return expr;
}

Expr* logical_and(Parser *p) {
    Expr *expr = bitwise_or(p);

    while (match(p, TOK_AMP_AMP)) {
        Expr *right = bitwise_or(p);
        expr = expr_binary(expr, OP_AND, right);
    }

    return expr;
}

Expr* logical_or(Parser *p) {
    Expr *expr = logical_and(p);

    while (match(p, TOK_PIPE_PIPE)) {
        Expr *right = logical_and(p);
        expr = expr_binary(expr, OP_OR, right);
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

    // Check for compound assignment operators (+=, -=, *=, /=)
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
    }

    if (is_compound) {
        // Desugar compound assignment: x += 5 becomes x = x + 5
        Expr *rhs = assignment(p);

        if (expr->type == EXPR_IDENT) {
            // Variable compound assignment: x += 5
            char *name = strdup(expr->as.ident);
            Expr *lhs_copy = expr_ident(name);
            Expr *binary = expr_binary(lhs_copy, compound_op, rhs);
            expr_free(expr);
            return expr_assign(name, binary);
        } else if (expr->type == EXPR_INDEX) {
            // Index compound assignment: arr[i] += 5
            // Desugar to: arr[i] = arr[i] + 5
            // We clone the object and index expressions to avoid evaluating twice
            Expr *object = expr->as.index.object;
            Expr *index = expr->as.index.index;

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
            return expr_index_assign(object, index, binary);
        } else if (expr->type == EXPR_GET_PROPERTY) {
            // Property compound assignment: obj.field += 5
            // Desugar to: obj.field = obj.field + 5
            Expr *object = expr->as.get_property.object;
            char *property = strdup(expr->as.get_property.property);

            // Clone the object for the RHS
            Expr *object_clone = expr_clone(object);

            // Create the read expression: obj.field
            Expr *read_expr = expr_get_property(object_clone, property);

            // Create the binary operation: obj.field + 5
            Expr *binary = expr_binary(read_expr, compound_op, rhs);

            // Steal the object from the EXPR_GET_PROPERTY for the assignment
            expr->as.get_property.object = NULL;
            expr_free(expr);

            // Create the assignment: obj.field = obj.field + 5
            return expr_set_property(object, property, binary);
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
            char *name = strdup(expr->as.ident);
            Expr *value = assignment(p);
            expr_free(expr);
            return expr_assign(name, value);
        } else if (expr->type == EXPR_INDEX) {
            // Index assignment: obj[index] = value
            Expr *object = expr->as.index.object;
            Expr *index = expr->as.index.index;
            Expr *value = assignment(p);

            // Steal the object and index from the EXPR_INDEX
            // (so we don't double-free them)
            expr->as.index.object = NULL;
            expr->as.index.index = NULL;
            expr_free(expr);

            return expr_index_assign(object, index, value);
        } else if (expr->type == EXPR_GET_PROPERTY) {
            // Property assignment: obj.field = value
            Expr *object = expr->as.get_property.object;
            char *property = strdup(expr->as.get_property.property);
            Expr *value = assignment(p);

            // Steal the object from the EXPR_GET_PROPERTY
            expr->as.get_property.object = NULL;
            expr_free(expr);

            return expr_set_property(object, property, value);
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

Type* parse_type(Parser *p) {
    TypeKind kind;

    // Check for 'array' or 'array<type>' syntax
    if (p->current.type == TOK_TYPE_ARRAY) {
        advance(p);
        Type *element_type = NULL;

        // Optional: <type> syntax for typed arrays
        if (p->current.type == TOK_LESS) {
            advance(p);  // consume '<'
            element_type = parse_type(p);
            consume(p, TOK_GREATER, "Expect '>' after array element type");
        }
        // If no '<', element_type stays NULL (untyped array)

        Type *type = type_new(TYPE_ARRAY);
        type->type_name = NULL;
        type->element_type = element_type;
        return type;
    }

    // Check for 'object' keyword (generic object type)
    if (p->current.type == TOK_OBJECT) {
        advance(p);
        Type *type = type_new(TYPE_GENERIC_OBJECT);
        type->type_name = NULL;
        return type;
    }

    // Check for custom object type name (identifier)
    if (p->current.type == TOK_IDENT) {
        char *type_name = token_text(&p->current);
        advance(p);
        Type *type = type_new(TYPE_CUSTOM_OBJECT);
        type->type_name = type_name;
        return type;
    }

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
    Type *type = type_new(kind);
    type->type_name = NULL;
    return type;
}


#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== ERROR HANDLING ==========

static void error_at(Parser *p, Token *token, const char *message) {
    if (p->panic_mode) return;
    p->panic_mode = 1;
    
    fprintf(stderr, "[line %d] Error", token->line);
    
    if (token->type == TOK_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOK_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    
    fprintf(stderr, ": %s\n", message);
    p->had_error = 1;
}

static void error(Parser *p, const char *message) {
    error_at(p, &p->previous, message);
}

static void error_at_current(Parser *p, const char *message) {
    error_at(p, &p->current, message);
}

// Forward declaration
static void advance(Parser *p);

static void synchronize(Parser *p) {
    p->panic_mode = 0;

    while (p->current.type != TOK_EOF) {
        if (p->previous.type == TOK_SEMICOLON) return;

        switch (p->current.type) {
            case TOK_LET:
            case TOK_IF:
            case TOK_WHILE:
                return;
            default:
                ; // Do nothing
        }

        advance(p);
    }
}

// ========== TOKEN MANAGEMENT ==========

static void advance(Parser *p) {
    p->previous = p->current;
    
    for (;;) {
        p->current = lexer_next(p->lexer);
        if (p->current.type != TOK_ERROR) break;
        
        error_at_current(p, p->current.start);
    }
}

static void consume(Parser *p, TokenType type, const char *message) {
    if (p->current.type == type) {
        advance(p);
        return;
    }
    
    error_at_current(p, message);
}

static int check(Parser *p, TokenType type) {
    return p->current.type == type;
}

static int match(Parser *p, TokenType type) {
    if (!check(p, type)) return 0;
    advance(p);
    return 1;
}

// ========== EXPRESSION PARSING ==========

// Forward declarations
static Expr* expression(Parser *p);
static Expr* assignment(Parser *p);
static Expr* ternary(Parser *p);
static Expr* equality(Parser *p);
static Expr* comparison(Parser *p);
static Expr* term(Parser *p);
static Expr* factor(Parser *p);
static Expr* primary(Parser *p);
static Type* parse_type(Parser *p);
static Stmt* block_statement(Parser *p);

static Expr* primary(Parser *p) {
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
            consume(p, TOK_IDENT, "Expect field name");
            field_names[num_fields] = token_text(&p->previous);

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
    int num_params = 0;

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

    return expr_function(is_async_fn, param_names, param_types, num_params, return_type, body);

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
    if (match(p, TOK_TYPE_STRING)) return expr_ident("string");
    if (match(p, TOK_TYPE_RUNE)) return expr_ident("rune");
    if (match(p, TOK_TYPE_BOOL)) return expr_ident("bool");

    error(p, "Expect expression");
    return expr_number(0);
}

static Expr* postfix(Parser *p) {
    Expr *expr = primary(p);

    // Handle chained property access, indexing, method calls, and postfix operators
    for (;;) {
        if (match(p, TOK_DOT)) {
            // Property access: obj.property
            consume(p, TOK_IDENT, "Expect property name after '.'");
            char *property = token_text(&p->previous);
            expr = expr_get_property(expr, property);
            free(property);
        } else if (match(p, TOK_LBRACKET)) {
            // Indexing: obj[index]
            Expr *index = expression(p);
            consume(p, TOK_RBRACKET, "Expect ']' after index");
            expr = expr_index(expr, index);
        } else if (match(p, TOK_LPAREN)) {
            // Function call: func(...) or obj.method(...)
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

static Expr* unary(Parser *p) {
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

static Expr* factor(Parser *p) {
    Expr *expr = unary(p);
    
    while (match(p, TOK_STAR) || match(p, TOK_SLASH)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_STAR) ? OP_MUL : OP_DIV;
        Expr *right = unary(p);
        expr = expr_binary(expr, op, right);
    }
    
    return expr;
}

static Expr* term(Parser *p) {
    Expr *expr = factor(p);
    
    while (match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_PLUS) ? OP_ADD : OP_SUB;
        Expr *right = factor(p);
        expr = expr_binary(expr, op, right);
    }
    
    return expr;
}

static Expr* comparison(Parser *p) {
    Expr *expr = term(p);
    
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
        
        Expr *right = term(p);
        expr = expr_binary(expr, op, right);
    }
    
    return expr;
}

static Expr* equality(Parser *p) {
    Expr *expr = comparison(p);
    
    while (match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQUAL)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_EQUAL_EQUAL) ? OP_EQUAL : OP_NOT_EQUAL;
        Expr *right = comparison(p);
        expr = expr_binary(expr, op, right);
    }
    
    return expr;
}

static Expr* logical_and(Parser *p) {
    Expr *expr = equality(p);
    
    while (match(p, TOK_AMP_AMP)) {
        Expr *right = equality(p);
        expr = expr_binary(expr, OP_AND, right);
    }
    
    return expr;
}

static Expr* logical_or(Parser *p) {
    Expr *expr = logical_and(p);

    while (match(p, TOK_PIPE_PIPE)) {
        Expr *right = logical_and(p);
        expr = expr_binary(expr, OP_OR, right);
    }

    return expr;
}

static Expr* ternary(Parser *p) {
    Expr *expr = logical_or(p);

    if (match(p, TOK_QUESTION)) {
        Expr *true_expr = expression(p);
        consume(p, TOK_COLON, "Expect ':' after true expression in ternary operator");
        Expr *false_expr = ternary(p);  // Right-associative
        return expr_ternary(expr, true_expr, false_expr);
    }

    return expr;
}

static Expr* assignment(Parser *p) {
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

static Expr* expression(Parser *p) {
    return assignment(p);
}

static Type* parse_type(Parser *p) {
    TypeKind kind;

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
        default:
            error_at_current(p, "Expect type name");
            return type_new(TYPE_INFER);
    }

    advance(p);
    Type *type = type_new(kind);
    type->type_name = NULL;
    return type;
}

// ========== STATEMENT PARSING ==========

static Stmt* statement(Parser *p);

static Stmt* let_statement(Parser *p) {
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

static Stmt* const_statement(Parser *p) {
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

static Stmt* block_statement(Parser *p) {
    Stmt **statements = malloc(sizeof(Stmt*) * 256);
    int count = 0;
    
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        statements[count++] = statement(p);
    }
    
    consume(p, TOK_RBRACE, "Expect '}' after block");
    
    return stmt_block(statements, count);
}

static Stmt* if_statement(Parser *p) {
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

static Stmt* while_statement(Parser *p) {
    consume(p, TOK_LPAREN, "Expect '(' after 'while'");
    Expr *condition = expression(p);
    consume(p, TOK_RPAREN, "Expect ')' after condition");

    consume(p, TOK_LBRACE, "Expect '{' after while condition");
    Stmt *body = block_statement(p);

    return stmt_while(condition, body);
}

static Stmt* switch_statement(Parser *p) {
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

static Stmt* for_statement(Parser *p) {
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

static Stmt* expression_statement(Parser *p) {
    Expr *expr = expression(p);
    consume(p, TOK_SEMICOLON, "Expect ';' after expression");
    return stmt_expr(expr);
}

static Stmt* return_statement(Parser *p) {
    Expr *value = NULL;

    // Check if there's a return value
    if (!check(p, TOK_SEMICOLON)) {
        value = expression(p);
    }

    consume(p, TOK_SEMICOLON, "Expect ';' after return statement");
    return stmt_return(value);
}

static Stmt* import_statement(Parser *p) {
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
        consume(p, TOK_AS, "Expect 'as' after '*' in namespace import");
        consume(p, TOK_IDENT, "Expect identifier for namespace name");
        char *namespace_name = token_text(&p->previous);

        consume(p, TOK_FROM, "Expect 'from' in import statement");
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
        if (match(p, TOK_AS)) {
            consume(p, TOK_IDENT, "Expect alias name after 'as'");
            import_aliases[num_imports] = token_text(&p->previous);
        } else {
            import_aliases[num_imports] = NULL;
        }

        num_imports++;
    } while (match(p, TOK_COMMA));

    consume(p, TOK_RBRACE, "Expect '}' after import list");
    consume(p, TOK_FROM, "Expect 'from' in import statement");
    consume(p, TOK_STRING, "Expect module path string");
    char *module_path = p->previous.string_value;

    consume(p, TOK_SEMICOLON, "Expect ';' after import statement");

    Stmt *stmt = stmt_import_named(import_names, import_aliases, num_imports, module_path);
    free(module_path);
    return stmt;
}

static Stmt* export_statement(Parser *p) {
    // Check for re-export: export { name1, name2 } from "module"
    if (match(p, TOK_LBRACE)) {
        char **export_names = malloc(sizeof(char*) * 32);
        char **export_aliases = malloc(sizeof(char*) * 32);
        int num_exports = 0;

        do {
            consume(p, TOK_IDENT, "Expect export name");
            export_names[num_exports] = token_text(&p->previous);

            // Check for alias: name as alias
            if (match(p, TOK_AS)) {
                consume(p, TOK_IDENT, "Expect alias name after 'as'");
                export_aliases[num_exports] = token_text(&p->previous);
            } else {
                export_aliases[num_exports] = NULL;
            }

            num_exports++;
        } while (match(p, TOK_COMMA));

        consume(p, TOK_RBRACE, "Expect '}' after export list");

        // Check for re-export
        if (match(p, TOK_FROM)) {
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
    int num_params = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            consume(p, TOK_IDENT, "Expect parameter name");
            param_names[num_params] = token_text(&p->previous);

            if (match(p, TOK_COLON)) {
                param_types[num_params] = parse_type(p);
            } else {
                param_types[num_params] = NULL;
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
    Expr *fn_expr = expr_function(is_async, param_names, param_types, num_params, return_type, body);

    // Create let statement
    Stmt *decl = stmt_let_typed(name, NULL, fn_expr);
    free(name);

    return stmt_export_declaration(decl);
}

static Stmt* extern_fn_statement(Parser *p) {
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

static Stmt* statement(Parser *p) {
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
        int num_params = 0;

        if (!check(p, TOK_RPAREN)) {
            do {
                consume(p, TOK_IDENT, "Expect parameter name");
                param_names[num_params] = token_text(&p->previous);

                if (match(p, TOK_COLON)) {
                    param_types[num_params] = parse_type(p);
                } else {
                    param_types[num_params] = NULL;
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
        Expr *fn_expr = expr_function(is_async, param_names, param_types, num_params, return_type, body);

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
        Expr *value = expression(p);
        consume(p, TOK_SEMICOLON, "Expect ';' after throw statement");
        return stmt_throw(value);
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

// ========== PUBLIC INTERFACE ==========

void parser_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->panic_mode = 0;
    
    advance(parser);  // Prime the pump
}

Stmt** parse_program(Parser *parser, int *stmt_count) {
    Stmt **statements = malloc(sizeof(Stmt*) * 256);  // max 256 statements
    *stmt_count = 0;

    while (!match(parser, TOK_EOF)) {
        if (parser->panic_mode) {
            synchronize(parser);
        }
        statements[(*stmt_count)++] = statement(parser);
    }

    return statements;
}
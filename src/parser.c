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
static Expr* equality(Parser *p);
static Expr* comparison(Parser *p);
static Expr* term(Parser *p);
static Expr* factor(Parser *p);
static Expr* primary(Parser *p);

static Expr* unary(Parser *p) {
    if (match(p, TOK_BANG)) {
        Expr *operand = unary(p);  // Recursive for multiple unary ops
        return expr_unary(UNARY_NOT, operand);
    }
    
    if (match(p, TOK_MINUS)) {
        Expr *operand = unary(p);
        return expr_unary(UNARY_NEGATE, operand);
    }
    
    return primary(p);
}

static Expr* primary(Parser *p) {
    if (match(p, TOK_TRUE)) {
        return expr_bool(1);
    }
    
    if (match(p, TOK_FALSE)) {
        return expr_bool(0);
    }

    if (match(p, TOK_NUMBER)) {
        return expr_number(p->previous.int_value);
    }

    if (match(p, TOK_STRING)) {
        char *str = p->previous.string_value;
        Expr *expr = expr_string(str);
        free(str);  // Parser owns this memory from lexer
        return expr;
    }
    
    if (match(p, TOK_IDENT)) {
        char *name = token_text(&p->previous);
        
        // Check for function call
        if (match(p, TOK_LPAREN)) {
            Expr **args = NULL;
            int num_args = 0;
            
            if (!check(p, TOK_RPAREN)) {
                args = malloc(sizeof(Expr*) * 8);
                args[num_args++] = expression(p);
            }
            
            consume(p, TOK_RPAREN, "Expect ')' after arguments");
            
            Expr *call = expr_call(name, args, num_args);
            free(name);
            return call;
        }
        
        Expr *ident = expr_ident(name);
        free(name);
        return ident;
    }
    
    if (match(p, TOK_LPAREN)) {
        Expr *expr = expression(p);
        consume(p, TOK_RPAREN, "Expect ')' after expression");
        return expr;
    }
    
    error(p, "Expect expression");
    return expr_number(0);
}

static Expr* factor(Parser *p) {
    Expr *expr = unary(p);  // Changed from primary(p)
    
    while (match(p, TOK_STAR) || match(p, TOK_SLASH)) {
        TokenType op_type = p->previous.type;
        BinaryOp op = (op_type == TOK_STAR) ? OP_MUL : OP_DIV;
        Expr *right = unary(p);  // Changed from primary(p)
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

static Expr* assignment(Parser *p) {
    Expr *expr = logical_or(p);
    
    if (match(p, TOK_EQUAL)) {
        // We have an assignment
        if (expr->type != EXPR_IDENT) {
            error(p, "Invalid assignment target");
            return expr;
        }
        
        char *name = strdup(expr->as.ident);
        Expr *value = assignment(p);  // Right-associative
        
        expr_free(expr);
        return expr_assign(name, value);
    }
    
    return expr;
}

static Expr* expression(Parser *p) {
    return assignment(p);
}

// ========== STATEMENT PARSING ==========

static Stmt* statement(Parser *p);

static Stmt* let_statement(Parser *p) {
    consume(p, TOK_IDENT, "Expect variable name");
    char *name = token_text(&p->previous);
    
    consume(p, TOK_EQUAL, "Expect '=' after variable name");
    
    Expr *value = expression(p);
    
    consume(p, TOK_SEMICOLON, "Expect ';' after variable declaration");
    
    Stmt *stmt = stmt_let(name, value);
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
        consume(p, TOK_LBRACE, "Expect '{' after 'else'");
        else_branch = block_statement(p);
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

static Stmt* expression_statement(Parser *p) {
    Expr *expr = expression(p);
    consume(p, TOK_SEMICOLON, "Expect ';' after expression");
    return stmt_expr(expr);
}

static Stmt* statement(Parser *p) {
    if (match(p, TOK_LET)) {
        return let_statement(p);
    }
    
    if (match(p, TOK_IF)) {
        return if_statement(p);
    }
    
    if (match(p, TOK_WHILE)) {
        return while_statement(p);
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
        statements[(*stmt_count)++] = statement(parser);
    }
    
    return statements;
}
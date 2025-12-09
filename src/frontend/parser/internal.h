#ifndef HEMLOCK_PARSER_INTERNAL_H
#define HEMLOCK_PARSER_INTERNAL_H

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== CORE FUNCTIONS (from core.c) ==========

// Error handling
void error_at(Parser *p, Token *token, const char *message);
void error(Parser *p, const char *message);
void error_at_current(Parser *p, const char *message);
void synchronize(Parser *p);

// Token management
void advance(Parser *p);
void consume(Parser *p, TokenType type, const char *message);
int check(Parser *p, TokenType type);
int match(Parser *p, TokenType type);

// ========== EXPRESSION PARSING (from expressions.c) ==========

Expr* expression(Parser *p);
Expr* assignment(Parser *p);
Expr* ternary(Parser *p);
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

// ========== STATEMENT PARSING (from statements.c) ==========

Stmt* statement(Parser *p);
Stmt* block_statement(Parser *p);

#endif // HEMLOCK_PARSER_INTERNAL_H

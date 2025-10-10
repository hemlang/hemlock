#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// ========== PRIVATE HELPERS ==========

static int is_at_end(Lexer *lex) {
    return *lex->current == '\0';
}

static char peek(Lexer *lex) {
    return *lex->current;
}

static char peek_next(Lexer *lex) {
    if (is_at_end(lex)) return '\0';
    return lex->current[1];
}

static char advance(Lexer *lex) {
    lex->current++;
    return lex->current[-1];
}

static void skip_whitespace(Lexer *lex) {
    for (;;) {
        char c = peek(lex);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lex);
                break;
            case '\n':
                lex->line++;
                advance(lex);
                break;
            case '/':
                // Comment support: // until end of line
                if (peek_next(lex) == '/') {
                    while (peek(lex) != '\n' && !is_at_end(lex)) {
                        advance(lex);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token make_token(Lexer *lex, TokenType type) {
    Token token;
    token.type = type;
    token.start = lex->start;
    token.length = (int)(lex->current - lex->start);
    token.line = lex->line;
    token.int_value = 0;
    return token;
}

static Token error_token(Lexer *lex, const char *message) {
    Token token;
    token.type = TOK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lex->line;
    token.int_value = 0;
    return token;
}

static Token number(Lexer *lex) {
    while (isdigit(peek(lex))) {
        advance(lex);
    }
    
    Token token = make_token(lex, TOK_NUMBER);
    
    // Parse the number
    char *text = token_text(&token);
    token.int_value = atoi(text);
    free(text);
    
    return token;
}

static Token string(Lexer *lex) {
    // Consume characters until closing quote
    while (peek(lex) != '"' && !is_at_end(lex)) {
        if (peek(lex) == '\n') lex->line++;
        advance(lex);
    }
    
    if (is_at_end(lex)) {
        return error_token(lex, "Unterminated string");
    }
    
    // Closing quote
    advance(lex);
    
    Token token = make_token(lex, TOK_STRING);
    
    // Extract string content (without quotes)
    int str_len = token.length - 2;  // Exclude opening and closing quotes
    token.string_value = malloc(str_len + 1);
    memcpy(token.string_value, token.start + 1, str_len);
    token.string_value[str_len] = '\0';
    
    return token;
}

static TokenType check_keyword(const char *start, int length, 
                               const char *rest, TokenType type) {
    if (strncmp(start, rest, length) == 0) {
        return type;
    }
    return TOK_IDENT;
}

static TokenType identifier_type(Lexer *lex) {
    switch (lex->start[0]) {
        case 'e':
            if (lex->current - lex->start == 4) {
                return check_keyword(lex->start, 4, "else", TOK_ELSE);
            }
            break;
        case 'f':
            if (lex->current - lex->start == 5) {
                return check_keyword(lex->start, 5, "false", TOK_FALSE);
            }
            break;
        case 'i':
            if (lex->current - lex->start == 2) {
                return check_keyword(lex->start, 2, "if", TOK_IF);
            }
            break;
        case 'l':
            if (lex->current - lex->start == 3) {
                return check_keyword(lex->start, 3, "let", TOK_LET);
            }
            break;
        case 't':
            if (lex->current - lex->start == 4) {
                return check_keyword(lex->start, 4, "true", TOK_TRUE);
            }
            break;
        case 'w':
            if (lex->current - lex->start == 5) {
                return check_keyword(lex->start, 5, "while", TOK_WHILE);
            }
            break;
    }
    
    return TOK_IDENT;
}

static Token identifier(Lexer *lex) {
    while (isalnum(peek(lex)) || peek(lex) == '_') {
        advance(lex);
    }
    
    TokenType type = identifier_type(lex);
    return make_token(lex, type);
}

// ========== PUBLIC INTERFACE ==========

void lexer_init(Lexer *lexer, const char *source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

Token lexer_next(Lexer *lex) {
    skip_whitespace(lex);
    
    lex->start = lex->current;
    
    if (is_at_end(lex)) {
        return make_token(lex, TOK_EOF);
    }
    
    char c = advance(lex);
    
    if (isdigit(c)) return number(lex);
    if (isalpha(c) || c == '_') return identifier(lex);
    if (c == '"') return string(lex);
    
    switch (c) {
        case '+': return make_token(lex, TOK_PLUS);
        case '-': return make_token(lex, TOK_MINUS);
        case '*': return make_token(lex, TOK_STAR);
        case '/': return make_token(lex, TOK_SLASH);
        case ';': return make_token(lex, TOK_SEMICOLON);
        case '(': return make_token(lex, TOK_LPAREN);
        case ')': return make_token(lex, TOK_RPAREN);
        case '{': return make_token(lex, TOK_LBRACE);
        case '}': return make_token(lex, TOK_RBRACE);
        
        case '=':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_EQUAL_EQUAL);
            }
            return make_token(lex, TOK_EQUAL);
            
        case '!':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_BANG_EQUAL);
            }
            return make_token(lex, TOK_BANG);
            
        case '<':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_LESS_EQUAL);
            }
            return make_token(lex, TOK_LESS);
            
        case '>':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_GREATER_EQUAL);
            }
            return make_token(lex, TOK_GREATER);
            
        case '&':
            if (peek(lex) == '&') {
                advance(lex);
                return make_token(lex, TOK_AMP_AMP);
            }
            return error_token(lex, "Unexpected character '&'");
            
        case '|':
            if (peek(lex) == '|') {
                advance(lex);
                return make_token(lex, TOK_PIPE_PIPE);
            }
            return error_token(lex, "Unexpected character '|'");
    }
    
    return error_token(lex, "Unexpected character");
}

char* token_text(Token *token) {
    char *text = malloc(token->length + 1);
    memcpy(text, token->start, token->length);
    text[token->length] = '\0';
    return text;
}
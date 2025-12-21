#include "internal.h"

// ========== ERROR HANDLING ==========

void error_at(Parser *p, Token *token, const char *message) {
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

void error(Parser *p, const char *message) {
    error_at(p, &p->previous, message);
}

void error_at_current(Parser *p, const char *message) {
    error_at(p, &p->current, message);
}

void synchronize(Parser *p) {
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

void advance(Parser *p) {
    p->previous = p->current;
    
    for (;;) {
        p->current = lexer_next(p->lexer);
        if (p->current.type != TOK_ERROR) break;
        
        error_at_current(p, p->current.start);
    }
}

void consume(Parser *p, TokenType type, const char *message) {
    if (p->current.type == type) {
        advance(p);
        return;
    }
    
    error_at_current(p, message);
}

int check(Parser *p, TokenType type) {
    return p->current.type == type;
}

int match(Parser *p, TokenType type) {
    if (!check(p, type)) return 0;
    advance(p);
    return 1;
}

// Contextual keywords - identifiers that act as keywords in specific contexts
int check_contextual(Parser *p, const char *keyword) {
    if (p->current.type != TOK_IDENT) return 0;
    int len = p->current.length;
    int kw_len = strlen(keyword);
    if (len != kw_len) return 0;
    return strncmp(p->current.start, keyword, len) == 0;
}

int match_contextual(Parser *p, const char *keyword) {
    if (!check_contextual(p, keyword)) return 0;
    advance(p);
    return 1;
}

void consume_contextual(Parser *p, const char *keyword, const char *message) {
    if (check_contextual(p, keyword)) {
        advance(p);
        return;
    }
    error_at_current(p, message);
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
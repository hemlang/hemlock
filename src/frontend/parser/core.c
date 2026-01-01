#include "internal.h"

// ========== ERROR HANDLING ==========

// Helper to get a line from source code
static const char* get_source_line(const char *source, int line_num, int *line_length) {
    if (!source) {
        *line_length = 0;
        return NULL;
    }

    const char *p = source;
    int current_line = 1;

    // Find the start of the requested line
    while (*p && current_line < line_num) {
        if (*p == '\n') {
            current_line++;
        }
        p++;
    }

    if (!*p && current_line < line_num) {
        *line_length = 0;
        return NULL;
    }

    // Find the end of the line
    const char *line_start = p;
    while (*p && *p != '\n') {
        p++;
    }

    *line_length = (int)(p - line_start);
    return line_start;
}

void error_at(Parser *p, Token *token, const char *message) {
    if (p->panic_mode) return;
    p->panic_mode = 1;

    fprintf(stderr, "[line %d:%d] Error", token->line, token->column);

    if (token->type == TOK_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOK_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);

    // Show source context if available
    if (p->source) {
        int line_length;
        const char *line = get_source_line(p->source, token->line, &line_length);
        if (line && line_length > 0) {
            // Print the source line
            fprintf(stderr, "    %.*s\n", line_length, line);

            // Print the caret pointing to the error position
            fprintf(stderr, "    ");
            for (int i = 1; i < token->column && i <= line_length; i++) {
                // Preserve tabs for alignment
                if (line[i-1] == '\t') {
                    fprintf(stderr, "\t");
                } else {
                    fprintf(stderr, " ");
                }
            }
            fprintf(stderr, "^\n");
        }
    }

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
    parser->source = lexer->source;  // Store source for error messages

    advance(parser);  // Prime the pump
}

Stmt** parse_program(Parser *parser, int *stmt_count) {
    int capacity = 256;
    Stmt **statements = malloc(sizeof(Stmt*) * capacity);
    *stmt_count = 0;

    while (!match(parser, TOK_EOF)) {
        if (parser->panic_mode) {
            synchronize(parser);
        }
        // Grow array if needed
        if (*stmt_count >= capacity) {
            capacity *= 2;
            statements = realloc(statements, sizeof(Stmt*) * capacity);
        }
        statements[(*stmt_count)++] = statement(parser);
    }

    return statements;
}

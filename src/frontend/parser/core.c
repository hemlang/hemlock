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
        if (p->previous.type == TOK_RBRACE) return;  // End of block

        switch (p->current.type) {
            // Statement-starting keywords
            case TOK_LET:
            case TOK_CONST:
            case TOK_IF:
            case TOK_WHILE:
            case TOK_FOR:
            case TOK_LOOP:
            case TOK_FN:
            case TOK_ASYNC:
            case TOK_RETURN:
            case TOK_DEFINE:
            case TOK_ENUM:
            case TOK_TYPE:
            case TOK_TRY:
            case TOK_THROW:
            case TOK_DEFER:
            case TOK_SWITCH:
            case TOK_IMPORT:
            case TOK_EXPORT:
            case TOK_EXTERN:
            case TOK_MATCH:
                return;
            default:
                // Check for reserved keywords from other languages (as identifiers)
                if (p->current.type == TOK_IDENT) {
                    if (p->current.length == 3 && strncmp(p->current.start, "def", 3) == 0) return;
                    if (p->current.length == 4 && strncmp(p->current.start, "func", 4) == 0) return;
                    if (p->current.length == 8 && strncmp(p->current.start, "function", 8) == 0) return;
                    if (p->current.length == 3 && strncmp(p->current.start, "var", 3) == 0) return;
                    if (p->current.length == 5 && strncmp(p->current.start, "class", 5) == 0) return;
                }
                ; // Do nothing
        }

        advance(p);
    }
}

// ========== TOKEN MANAGEMENT ==========

void advance(Parser *p) {
    p->previous = p->current;
    p->current = p->next;

    for (;;) {
        p->next = lexer_next(p->lexer);
        if (p->next.type != TOK_ERROR) break;

        // Report error for the bad token
        Token bad = p->next;
        error_at(p, &bad, bad.start);
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

// Check if token type is a type keyword that can be used as an identifier
// This includes built-in type names (i32, string, etc.) and contextual keywords
// that only have special meaning at the start of statements
int is_identifier_or_type_keyword(TokenType type) {
    switch (type) {
        // Always an identifier
        case TOK_IDENT:
        // Type keywords (can be used as variable/field names)
        case TOK_TYPE_I8: case TOK_TYPE_I16: case TOK_TYPE_I32: case TOK_TYPE_I64:
        case TOK_TYPE_U8: case TOK_TYPE_U16: case TOK_TYPE_U32: case TOK_TYPE_U64:
        case TOK_TYPE_F32: case TOK_TYPE_F64:
        case TOK_TYPE_BOOL: case TOK_TYPE_STRING: case TOK_TYPE_RUNE:
        case TOK_TYPE_PTR: case TOK_TYPE_BUFFER: case TOK_TYPE_ARRAY:
        case TOK_TYPE_INTEGER: case TOK_TYPE_NUMBER: case TOK_TYPE_BYTE:
        case TOK_TYPE_VOID:
        // Contextual keywords (only special at statement level)
        case TOK_TYPE:      // 'type' for type aliases
        case TOK_DEFINE:    // 'define' for object types
        case TOK_ENUM:      // 'enum' for enumerations
        case TOK_IMPORT:    // 'import' for module imports
        case TOK_EXPORT:    // 'export' for module exports
        case TOK_EXTERN:    // 'extern' for FFI declarations
        case TOK_ASYNC:     // 'async' for async functions
        case TOK_DEFER:     // 'defer' for deferred execution
        case TOK_OBJECT:    // 'object' type name
        case TOK_CONST:     // 'const' can be used as annotation name (@const)
            return 1;
        default:
            return 0;
    }
}

// Check if the current token can be used as an identifier
int check_identifier_or_type_keyword(Parser *p) {
    return is_identifier_or_type_keyword(p->current.type);
}

// Check and advance if current token can be used as an identifier
int match_identifier_or_type_keyword(Parser *p) {
    if (!is_identifier_or_type_keyword(p->current.type)) return 0;
    advance(p);
    return 1;
}

// Consume an identifier or type keyword, returning the token text
// Used for variable names, field names, and property names
char* consume_identifier_or_type_keyword(Parser *p, const char *message) {
    if (is_identifier_or_type_keyword(p->current.type)) {
        advance(p);
        return token_text(&p->previous);
    }
    error_at_current(p, message);
    return strdup("error");
}

// ========== PUBLIC INTERFACE ==========

void parser_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->panic_mode = 0;
    parser->source = lexer->source;  // Store source for error messages
    parser->type_params = NULL;      // No type parameters in scope initially
    parser->num_type_params = 0;

    // Prime the lookahead: get both current and next tokens
    // First, get the first token into 'next'
    for (;;) {
        parser->next = lexer_next(parser->lexer);
        if (parser->next.type != TOK_ERROR) break;
        // Skip error tokens during initialization
    }

    // Now advance to set current = next and get the new next
    advance(parser);
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
            Stmt **new_statements = realloc(statements, sizeof(Stmt*) * capacity);
            if (!new_statements) {
                return statements;  // Return what we have on allocation failure
            }
            statements = new_statements;
        }
        statements[(*stmt_count)++] = statement(parser);
    }

    return statements;
}

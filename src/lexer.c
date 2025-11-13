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

    // Look for decimal point
    int is_float = 0;
    if (peek(lex) == '.' && isdigit(peek_next(lex))) {
        is_float = 1;
        advance(lex);  // consume '.'

        while (isdigit(peek(lex))) {
            advance(lex);
        }
    }

    Token token = make_token(lex, TOK_NUMBER);
    token.is_float = is_float;

    char *text = token_text(&token);

    if (is_float) {
        token.float_value = atof(text);
    } else {
        // Use strtoll to parse 64-bit integers
        char *endptr;
        token.int_value = strtoll(text, &endptr, 10);
        // Note: strtoll will handle negative numbers correctly
    }

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

static Token rune_literal(Lexer *lex) {
    // Read the character or escape sequence
    if (is_at_end(lex) || peek(lex) == '\'') {
        return error_token(lex, "Empty rune literal");
    }

    char c = peek(lex);
    uint32_t codepoint = 0;

    if (c == '\\') {
        // Escape sequence
        advance(lex);
        if (is_at_end(lex)) {
            return error_token(lex, "Unterminated rune literal");
        }
        c = peek(lex);
        advance(lex);

        switch (c) {
            case 'n':  codepoint = '\n'; break;
            case 't':  codepoint = '\t'; break;
            case 'r':  codepoint = '\r'; break;
            case '\\': codepoint = '\\'; break;
            case '\'': codepoint = '\''; break;
            case '"':  codepoint = '"'; break;
            case '0':  codepoint = '\0'; break;
            case 'u': {
                // Unicode escape: \u{XXXX}
                if (peek(lex) != '{') {
                    return error_token(lex, "Expected '{' after \\u");
                }
                advance(lex); // consume '{'

                codepoint = 0;
                int digit_count = 0;
                while (peek(lex) != '}' && !is_at_end(lex) && digit_count < 6) {
                    c = peek(lex);
                    int digit;
                    if (c >= '0' && c <= '9') {
                        digit = c - '0';
                    } else if (c >= 'a' && c <= 'f') {
                        digit = 10 + (c - 'a');
                    } else if (c >= 'A' && c <= 'F') {
                        digit = 10 + (c - 'A');
                    } else {
                        return error_token(lex, "Invalid hex digit in Unicode escape");
                    }
                    codepoint = (codepoint << 4) | digit;
                    advance(lex);
                    digit_count++;
                }

                if (peek(lex) != '}') {
                    return error_token(lex, "Unterminated Unicode escape");
                }
                advance(lex); // consume '}'

                if (codepoint > 0x10FFFF) {
                    return error_token(lex, "Unicode codepoint out of range");
                }
                break;
            }
            default:
                return error_token(lex, "Unknown escape sequence");
        }
    } else {
        // Regular character - decode UTF-8
        codepoint = (unsigned char)c;
        advance(lex);

        // Check for multi-byte UTF-8 sequences
        if ((codepoint & 0x80) != 0) {
            // Multi-byte UTF-8 character
            if ((codepoint & 0xE0) == 0xC0) {
                // 2-byte sequence
                codepoint = (codepoint & 0x1F) << 6;
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F);
                    advance(lex);
                }
            } else if ((codepoint & 0xF0) == 0xE0) {
                // 3-byte sequence
                codepoint = (codepoint & 0x0F) << 12;
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F) << 6;
                    advance(lex);
                }
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F);
                    advance(lex);
                }
            } else if ((codepoint & 0xF8) == 0xF0) {
                // 4-byte sequence
                codepoint = (codepoint & 0x07) << 18;
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F) << 12;
                    advance(lex);
                }
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F) << 6;
                    advance(lex);
                }
                if (!is_at_end(lex)) {
                    codepoint |= (peek(lex) & 0x3F);
                    advance(lex);
                }
            }
        }
    }

    // Expect closing quote
    if (peek(lex) != '\'') {
        return error_token(lex, "Expected closing ' after rune literal");
    }
    advance(lex); // consume closing '

    Token token = make_token(lex, TOK_RUNE);
    token.rune_value = codepoint;
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
    int len = lex->current - lex->start;
    
    switch (lex->start[0]) {
        case 'a':
            if (len == 2) return check_keyword(lex->start, 2, "as", TOK_AS);
            if (len == 5) {
                if (strncmp(lex->start, "async", 5) == 0) return TOK_ASYNC;
                if (strncmp(lex->start, "await", 5) == 0) return TOK_AWAIT;
            }
            break;
        case 'b':
            if (len == 4) {
                if (strncmp(lex->start, "byte", 4) == 0) return TOK_TYPE_BYTE;
                if (strncmp(lex->start, "bool", 4) == 0) return TOK_TYPE_BOOL;
            }
            if (len == 5) return check_keyword(lex->start, 5, "break", TOK_BREAK);
            break;
        case 'c':
            if (len == 4) {
                if (strncmp(lex->start, "case", 4) == 0) return TOK_CASE;
            }
            if (len == 5) {
                if (strncmp(lex->start, "const", 5) == 0) return TOK_CONST;
                if (strncmp(lex->start, "catch", 5) == 0) return TOK_CATCH;
            }
            if (len == 8) return check_keyword(lex->start, 8, "continue", TOK_CONTINUE);
            break;
        case 'd':
            if (len == 6) return check_keyword(lex->start, 6, "define", TOK_DEFINE);
            if (len == 7) return check_keyword(lex->start, 7, "default", TOK_DEFAULT);
            break;
        case 'e':
            if (len == 4) return check_keyword(lex->start, 4, "else", TOK_ELSE);
            if (len == 6) {
                if (strncmp(lex->start, "export", 6) == 0) return TOK_EXPORT;
                if (strncmp(lex->start, "extern", 6) == 0) return TOK_EXTERN;
            }
            break;
        case 'f':
            if (len == 2) return check_keyword(lex->start, 2, "fn", TOK_FN);
            if (len == 3) {
                if (strncmp(lex->start, "for", 3) == 0) return TOK_FOR;
                //if (strncmp(lex->start, "f16", 3) == 0) return TOK_TYPE_F16;
                if (strncmp(lex->start, "f32", 3) == 0) return TOK_TYPE_F32;
                if (strncmp(lex->start, "f64", 3) == 0) return TOK_TYPE_F64;
            }
            if (len == 4) return check_keyword(lex->start, 4, "from", TOK_FROM);
            if (len == 5) return check_keyword(lex->start, 5, "false", TOK_FALSE);
            if (len == 7) return check_keyword(lex->start, 7, "finally", TOK_FINALLY);
            break;
        case 'i':
            if (len == 2) {
                if (strncmp(lex->start, "if", 2) == 0) return TOK_IF;
                if (strncmp(lex->start, "in", 2) == 0) return TOK_IN;
                if (strncmp(lex->start, "i8", 2) == 0) return TOK_TYPE_I8;
            }
            if (len == 3) {
                if (strncmp(lex->start, "i16", 3) == 0) return TOK_TYPE_I16;
                if (strncmp(lex->start, "i32", 3) == 0) return TOK_TYPE_I32;
                if (strncmp(lex->start, "i64", 3) == 0) return TOK_TYPE_I64;
            }
            if (len == 6) return check_keyword(lex->start, 6, "import", TOK_IMPORT);
            if (len == 7) return check_keyword(lex->start, 7, "integer", TOK_TYPE_INTEGER);
            break;
        case 'l':
            if (len == 3) return check_keyword(lex->start, 3, "let", TOK_LET);
            break;
        case 'n':
            if (len == 4) return check_keyword(lex->start, 4, "null", TOK_NULL);
            if (len == 6) return check_keyword(lex->start, 6, "number", TOK_TYPE_NUMBER);
            break;
        case 'o':
            if (len == 6) return check_keyword(lex->start, 6, "object", TOK_OBJECT);
            break;
        case 'r':
            if (len == 3) return check_keyword(lex->start, 3, "ref", TOK_REF);
            if (len == 4) return check_keyword(lex->start, 4, "rune", TOK_TYPE_RUNE);
            if (len == 6) return check_keyword(lex->start, 6, "return", TOK_RETURN);
            break;
        case 'p':
            if (len == 3) return check_keyword(lex->start, 3, "ptr", TOK_TYPE_PTR);
            break;
        case 's':
            if (len == 4) return check_keyword(lex->start, 4, "self", TOK_SELF);
            if (len == 6) {
                if (strncmp(lex->start, "string", 6) == 0) return TOK_TYPE_STRING;
                if (strncmp(lex->start, "switch", 6) == 0) return TOK_SWITCH;
            }
            break;
        case 't':
            if (len == 3) return check_keyword(lex->start, 3, "try", TOK_TRY);
            if (len == 4) return check_keyword(lex->start, 4, "true", TOK_TRUE);
            if (len == 5) return check_keyword(lex->start, 5, "throw", TOK_THROW);
            break;
        case 'u':
            if (len == 2) {
                if (strncmp(lex->start, "u8", 2) == 0) return TOK_TYPE_U8;
            }
            if (len == 3) {
                if (strncmp(lex->start, "u16", 3) == 0) return TOK_TYPE_U16;
                if (strncmp(lex->start, "u32", 3) == 0) return TOK_TYPE_U32;
                if (strncmp(lex->start, "u64", 3) == 0) return TOK_TYPE_U64;
            }
            break;
        case 'v':
            if (len == 4) return check_keyword(lex->start, 4, "void", TOK_TYPE_VOID);
            break;
        case 'w':
            if (len == 5) return check_keyword(lex->start, 5, "while", TOK_WHILE);
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
    if (c == '\'') return rune_literal(lex);

    switch (c) {
        case '+':
            if (peek(lex) == '+') {
                advance(lex);
                return make_token(lex, TOK_PLUS_PLUS);
            }
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_PLUS_EQUAL);
            }
            return make_token(lex, TOK_PLUS);
        case '-':
            if (peek(lex) == '-') {
                advance(lex);
                return make_token(lex, TOK_MINUS_MINUS);
            }
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_MINUS_EQUAL);
            }
            return make_token(lex, TOK_MINUS);
        case '*':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_STAR_EQUAL);
            }
            return make_token(lex, TOK_STAR);
        case '/':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token(lex, TOK_SLASH_EQUAL);
            }
            return make_token(lex, TOK_SLASH);
        case ';': return make_token(lex, TOK_SEMICOLON);
        case ':': return make_token(lex, TOK_COLON);
        case ',': return make_token(lex, TOK_COMMA);
        case '(': return make_token(lex, TOK_LPAREN);
        case ')': return make_token(lex, TOK_RPAREN);
        case '{': return make_token(lex, TOK_LBRACE);
        case '}': return make_token(lex, TOK_RBRACE);
        case '.': return make_token(lex, TOK_DOT);
        case '[': return make_token(lex, TOK_LBRACKET);
        case ']': return make_token(lex, TOK_RBRACKET);
        case '?': return make_token(lex, TOK_QUESTION);

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
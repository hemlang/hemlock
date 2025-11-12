#ifndef HEMLOCK_LEXER_H
#define HEMLOCK_LEXER_H

#include <stdint.h>  // For int64_t

// Token types
typedef enum {
    // Literals
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,

    // Keywords
    TOK_LET,
    TOK_CONST,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_FN,
    TOK_RETURN,
    TOK_REF,
    TOK_DEFINE,
    TOK_OBJECT,
    TOK_SELF,
    TOK_TRY,
    TOK_CATCH,
    TOK_FINALLY,
    TOK_THROW,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_ASYNC,
    TOK_AWAIT,
    TOK_IMPORT,
    TOK_EXPORT,
    TOK_FROM,
    TOK_AS,
    TOK_EXTERN,

    // Type keywords
    TOK_TYPE_I8,
    TOK_TYPE_I16,
    TOK_TYPE_I32,
    TOK_TYPE_I64,
    TOK_TYPE_U8,
    TOK_TYPE_U16,
    TOK_TYPE_U32,
    TOK_TYPE_U64,
    //TOK_TYPE_F16,
    TOK_TYPE_F32,
    TOK_TYPE_F64,
    TOK_TYPE_INTEGER,  // alias for i32
    TOK_TYPE_NUMBER,   // alias for f64
    TOK_TYPE_CHAR,     // alias for u8
    TOK_TYPE_BOOL,
    TOK_TYPE_STRING,
    TOK_TYPE_PTR,
    TOK_TYPE_BUFFER,
    TOK_TYPE_VOID,

    // Operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PLUS_PLUS,
    TOK_MINUS_MINUS,
    TOK_EQUAL,
    TOK_PLUS_EQUAL,
    TOK_MINUS_EQUAL,
    TOK_STAR_EQUAL,
    TOK_SLASH_EQUAL,
    TOK_EQUAL_EQUAL,
    TOK_BANG_EQUAL,
    TOK_BANG,
    TOK_LESS,
    TOK_LESS_EQUAL,
    TOK_GREATER,
    TOK_GREATER_EQUAL,
    TOK_AMP_AMP,
    TOK_PIPE_PIPE,
    
    // Punctuation
    TOK_SEMICOLON,
    TOK_COLON,
    TOK_COMMA,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_DOT,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_QUESTION,
    
    // Special
    TOK_EOF,
    TOK_ERROR,
} TokenType;

// Token structure
typedef struct {
    TokenType type;
    const char *start;  // Points into source (don't free!)
    int length;
    int line;

    // For numbers
    int64_t int_value;  // Changed to int64_t to support 64-bit literals
    double float_value;
    int is_float;

    // For strings
    char *string_value; // Must be freed
} Token;

// Lexer state
typedef struct {
    const char *start;
    const char *current;
    int line;
} Lexer;

// Public interface
void lexer_init(Lexer *lexer, const char *source);
Token lexer_next(Lexer *lexer);

// Helper to extract token text (you must free the result)
char* token_text(Token *token);

#endif // HEMLOCK_LEXER_H
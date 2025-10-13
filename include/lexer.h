#ifndef HEMLOCK_LEXER_H
#define HEMLOCK_LEXER_H

// Token types
typedef enum {
    // Literals
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    TOK_TRUE,
    TOK_FALSE,
    
    // Keywords
    TOK_LET,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,

    // Type keywords
    TOK_TYPE_I8,
    TOK_TYPE_I16,
    TOK_TYPE_I32,
    TOK_TYPE_U8,
    TOK_TYPE_U16,
    TOK_TYPE_U32,
    TOK_TYPE_F16,
    TOK_TYPE_F32,
    TOK_TYPE_F64,
    TOK_TYPE_INTEGER,  // alias for i32
    TOK_TYPE_NUMBER,   // alias for f64
    TOK_TYPE_CHAR,     // alias for u8
    TOK_TYPE_BOOL,
    TOK_TYPE_STRING,

    // Operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQUAL,
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
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_DOT,
    TOK_LBRACKET,
    TOK_RBRACKET,
    
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
    int int_value;
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
#ifndef HEMLOCK_FORMATTER_INTERNAL_H
#define HEMLOCK_FORMATTER_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "frontend.h"
#include "frontend/ast.h"

// ========== STRING BUFFER ==========

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void buf_init(StrBuf *buf);
void buf_free(StrBuf *buf);
void buf_grow(StrBuf *buf, size_t needed);
void buf_append(StrBuf *buf, const char *s);
void buf_append_char(StrBuf *buf, char c);
void buf_printf(StrBuf *buf, const char *fmt, ...);

// ========== COMMENT TYPES AND FUNCTIONS ==========

typedef enum {
    COMMENT_LINE,    // // comment
    COMMENT_BLOCK,   // /* comment */
} CommentType;

typedef struct {
    CommentType type;
    int line;           // 1-based line number
    int column;         // 1-based column number
    char *text;         // Comment text (without // or /* */)
    int is_trailing;    // True if comment follows code on same line
} Comment;

typedef struct {
    Comment *comments;
    int count;
    int capacity;
    int next_idx;       // For iteration during formatting
} CommentList;

// Track blank lines in source
typedef struct {
    int *lines;         // Array of blank line numbers
    int count;
    int capacity;
    int next_idx;       // For iteration during formatting
} BlankLineList;

void blank_line_list_init(BlankLineList *list);
void blank_line_list_free(BlankLineList *list);
void blank_line_list_add(BlankLineList *list, int line);

void comment_list_init(CommentList *list);
void comment_list_free(CommentList *list);
void comment_list_add(CommentList *list, CommentType type, int line, int column,
                      const char *text, int len, int is_trailing);

void extract_comments(const char *source, CommentList *list);
void extract_blank_lines(const char *source, BlankLineList *list);
int has_blank_line_between(BlankLineList *list, int from_line, int to_line);
void advance_blank_lines_past(BlankLineList *list, int line);

// ========== LITERAL MAP ==========

typedef struct {
    int line;
    int col;
    char *text;  // Original source text (e.g., "0xDEADBEEF", "1_000_000")
} LiteralSpan;

typedef struct {
    LiteralSpan *spans;
    int count;
    int capacity;
} LiteralMap;

void literal_map_init(LiteralMap *map);
void literal_map_free(LiteralMap *map);
void literal_map_add(LiteralMap *map, int line, int col, const char *text);
const char *literal_map_lookup(LiteralMap *map, int line, int col);
void extract_literals(const char *source, LiteralMap *map);

// ========== FORMATTER CONTEXT ==========

typedef struct FmtCtx {
    StrBuf buf;
    int indent;
    int column;          // Current column position (for line width tracking)
    CommentList *comments;  // Extracted comments
    BlankLineList *blank_lines;  // Extracted blank lines
    LiteralMap *literals;  // Original literal text map
    int output_line;     // Current output line (1-based)
    int last_source_line;  // Last source line processed (for comment association)
    bool is_else_if;     // Skip leading indent for else-if (follows "else " on same line)
} FmtCtx;

// ========== HELPER FUNCTIONS (shared across files) ==========

void fmt_indent(FmtCtx *ctx);
void fmt_newline(FmtCtx *ctx);
void update_column(FmtCtx *ctx);

void fmt_leading_comments(FmtCtx *ctx, int source_line);
void fmt_trailing_comment(FmtCtx *ctx, int source_line);

void fmt_type(FmtCtx *ctx, Type *type);
void fmt_annotations(FmtCtx *ctx, Annotation **annotations, int count);
void fmt_fn_params(FmtCtx *ctx, Expr *fn, const char *fn_name, const char *prefix);

// ========== ESTIMATION FUNCTIONS ==========

int estimate_expr_len(Expr *expr);
int expr_is_complex(Expr *expr);
int count_complex_elements(Expr **elements, int num_elements);
int estimate_type_len(Type *type);
int estimate_fn_params_len(Expr *fn);
int estimate_import_len(Stmt *stmt);
int count_method_chain_depth(Expr *expr);

// ========== EXPRESSION AND STATEMENT FORMATTING ==========

void fmt_expr(FmtCtx *ctx, Expr *expr);
void fmt_stmt(FmtCtx *ctx, Stmt *stmt);

// Expression formatting helpers
const char *binary_op_str(BinaryOp op);
int needs_parens(Expr *parent, Expr *child, int is_right);
void fmt_escaped_string(FmtCtx *ctx, const char *s);
void fmt_escaped_template_part(FmtCtx *ctx, const char *s);
void fmt_rune(FmtCtx *ctx, uint32_t codepoint);
void fmt_pattern(FmtCtx *ctx, Pattern *pattern);

// ========== PUBLIC API HELPERS ==========

int stmt_end_line(Stmt *stmt);

#endif // HEMLOCK_FORMATTER_INTERNAL_H

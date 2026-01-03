#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "formatter.h"
#include "lexer.h"
#include "parser.h"

// ========== STRING BUFFER ==========

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void buf_init(StrBuf *buf) {
    buf->cap = 256;
    buf->data = malloc(buf->cap);
    buf->data[0] = '\0';
    buf->len = 0;
}

static void buf_free(StrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = buf->cap = 0;
}

static void buf_grow(StrBuf *buf, size_t needed) {
    if (buf->len + needed + 1 > buf->cap) {
        while (buf->len + needed + 1 > buf->cap) {
            buf->cap *= 2;
        }
        buf->data = realloc(buf->data, buf->cap);
    }
}

static void buf_append(StrBuf *buf, const char *s) {
    size_t slen = strlen(s);
    buf_grow(buf, slen);
    memcpy(buf->data + buf->len, s, slen);
    buf->len += slen;
    buf->data[buf->len] = '\0';
}

static void buf_append_char(StrBuf *buf, char c) {
    buf_grow(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

static void buf_printf(StrBuf *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    // First, determine size needed
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    buf_grow(buf, needed);
    vsnprintf(buf->data + buf->len, needed + 1, fmt, args);
    buf->len += needed;

    va_end(args);
}

// Forward declarations
typedef struct FmtCtx FmtCtx;
static void fmt_indent(FmtCtx *ctx);
static void fmt_newline(FmtCtx *ctx);

// ========== COMMENT EXTRACTION ==========

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

static void comment_list_init(CommentList *list) {
    list->capacity = 16;
    list->comments = malloc(sizeof(Comment) * list->capacity);
    list->count = 0;
    list->next_idx = 0;
}

static void comment_list_free(CommentList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->comments[i].text);
    }
    free(list->comments);
    list->comments = NULL;
    list->count = list->capacity = 0;
}

static void comment_list_add(CommentList *list, CommentType type, int line, int column,
                             const char *text, int len, int is_trailing) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->comments = realloc(list->comments, sizeof(Comment) * list->capacity);
    }
    Comment *c = &list->comments[list->count++];
    c->type = type;
    c->line = line;
    c->column = column;
    c->text = malloc(len + 1);
    memcpy(c->text, text, len);
    c->text[len] = '\0';
    c->is_trailing = is_trailing;
}

// Extract all comments from source (separate from lexing)
static void extract_comments(const char *source, CommentList *list) {
    const char *p = source;
    int line = 1;
    int column = 1;
    int line_has_code = 0;  // Track if current line has non-whitespace before comment

    while (*p) {
        // Track if we've seen code on this line
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' &&
            !(*p == '/' && (p[1] == '/' || p[1] == '*'))) {
            line_has_code = 1;
        }

        if (*p == '/' && p[1] == '/') {
            // Line comment
            int comment_col = column;
            int is_trailing = line_has_code;
            p += 2;
            column += 2;

            const char *start = p;
            while (*p && *p != '\n') {
                p++;
                column++;
            }

            // Trim leading space from comment text
            while (start < p && *start == ' ') start++;

            comment_list_add(list, COMMENT_LINE, line, comment_col, start, p - start, is_trailing);

            if (*p == '\n') {
                p++;
                line++;
                column = 1;
                line_has_code = 0;
            }
        } else if (*p == '/' && p[1] == '*') {
            // Block comment
            int comment_col = column;
            int comment_line = line;
            int is_trailing = line_has_code;
            p += 2;
            column += 2;

            const char *start = p;
            while (*p && !(*p == '*' && p[1] == '/')) {
                if (*p == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
                p++;
            }

            // Trim leading/trailing whitespace from comment text
            const char *end = p;
            while (start < end && (*start == ' ' || *start == '\t')) start++;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;

            comment_list_add(list, COMMENT_BLOCK, comment_line, comment_col,
                           start, end - start, is_trailing);

            if (*p == '*') {
                p += 2;
                column += 2;
            }
        } else if (*p == '\n') {
            p++;
            line++;
            column = 1;
            line_has_code = 0;
        } else if (*p == '"') {
            // Skip string literals
            p++;
            column++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    p += 2;
                    column += 2;
                } else if (*p == '\n') {
                    p++;
                    line++;
                    column = 1;
                } else {
                    p++;
                    column++;
                }
            }
            if (*p == '"') {
                p++;
                column++;
            }
        } else if (*p == '\'') {
            // Skip rune literals
            p++;
            column++;
            if (*p == '\\' && p[1]) {
                p += 2;
                column += 2;
            } else if (*p) {
                // Handle UTF-8 runes
                if ((*p & 0x80) == 0) {
                    p++;
                    column++;
                } else if ((*p & 0xE0) == 0xC0) {
                    p += 2;
                    column++;
                } else if ((*p & 0xF0) == 0xE0) {
                    p += 3;
                    column++;
                } else if ((*p & 0xF8) == 0xF0) {
                    p += 4;
                    column++;
                } else {
                    p++;
                    column++;
                }
            }
            if (*p == '\'') {
                p++;
                column++;
            }
        } else if (*p == '`') {
            // Skip template strings
            p++;
            column++;
            while (*p && *p != '`') {
                if (*p == '\n') {
                    line++;
                    column = 1;
                    p++;
                } else {
                    p++;
                    column++;
                }
            }
            if (*p == '`') {
                p++;
                column++;
            }
        } else {
            p++;
            column++;
        }
    }
}

// ========== FORMATTER CONTEXT ==========

struct FmtCtx {
    StrBuf buf;
    int indent;
    int column;          // Current column position (for line width tracking)
    CommentList *comments;  // Extracted comments
    int output_line;     // Current output line (1-based)
};

// Output any leading comments for a given source line
static void fmt_leading_comments(FmtCtx *ctx, int source_line) {
    if (!ctx->comments) return;

    while (ctx->comments->next_idx < ctx->comments->count) {
        Comment *c = &ctx->comments->comments[ctx->comments->next_idx];

        // Only output comments that are:
        // 1. Before or on this line
        // 2. Not trailing comments (those go after code)
        if (c->line > source_line) break;
        if (c->is_trailing) {
            ctx->comments->next_idx++;
            continue;
        }

        fmt_indent(ctx);
        if (c->type == COMMENT_LINE) {
            buf_append(&ctx->buf, "// ");
            buf_append(&ctx->buf, c->text);
        } else {
            buf_append(&ctx->buf, "/* ");
            buf_append(&ctx->buf, c->text);
            buf_append(&ctx->buf, " */");
        }
        fmt_newline(ctx);
        ctx->output_line++;
        ctx->comments->next_idx++;
    }
}

// Output trailing comment for a given source line (if any)
static void fmt_trailing_comment(FmtCtx *ctx, int source_line) {
    if (!ctx->comments) return;

    // Look for trailing comment on this line
    for (int i = ctx->comments->next_idx; i < ctx->comments->count; i++) {
        Comment *c = &ctx->comments->comments[i];

        if (c->line > source_line) break;
        if (c->line == source_line && c->is_trailing) {
            // Remove the newline we just added
            if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                ctx->buf.len--;
                ctx->buf.data[ctx->buf.len] = '\0';
            }

            buf_append(&ctx->buf, "  ");
            if (c->type == COMMENT_LINE) {
                buf_append(&ctx->buf, "// ");
                buf_append(&ctx->buf, c->text);
            } else {
                buf_append(&ctx->buf, "/* ");
                buf_append(&ctx->buf, c->text);
                buf_append(&ctx->buf, " */");
            }
            fmt_newline(ctx);

            // Mark as consumed by moving next_idx past it
            if (i == ctx->comments->next_idx) {
                ctx->comments->next_idx++;
            }
            return;
        }
    }
}

// Update column position after the buffer changes
static void update_column(FmtCtx *ctx) {
    // Find last newline in buffer and count from there
    ctx->column = 0;
    for (size_t i = ctx->buf.len; i > 0; i--) {
        if (ctx->buf.data[i - 1] == '\n') {
            // Count from after the newline
            for (size_t j = i; j < ctx->buf.len; j++) {
                if (ctx->buf.data[j] == '\t') {
                    ctx->column = ((ctx->column / 4) + 1) * 4;
                } else {
                    ctx->column++;
                }
            }
            return;
        }
    }
    // No newline found - count from beginning
    for (size_t j = 0; j < ctx->buf.len; j++) {
        if (ctx->buf.data[j] == '\t') {
            ctx->column = ((ctx->column / 4) + 1) * 4;
        } else {
            ctx->column++;
        }
    }
}

static void fmt_indent(FmtCtx *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        buf_append_char(&ctx->buf, '\t');
    }
    update_column(ctx);
}

static void fmt_newline(FmtCtx *ctx) {
    buf_append_char(&ctx->buf, '\n');
    ctx->column = 0;
}

// Forward declarations
static void fmt_expr(FmtCtx *ctx, Expr *expr);
static void fmt_stmt(FmtCtx *ctx, Stmt *stmt);
static void fmt_type(FmtCtx *ctx, Type *type);
static int estimate_expr_len(Expr *expr);

// Estimate the length of an expression (for line-breaking decisions)
static int estimate_expr_len(Expr *expr) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_NUMBER:
            return expr->as.number.is_float ? 10 : 6;  // Rough estimate
        case EXPR_BOOL:
            return expr->as.boolean ? 4 : 5;
        case EXPR_STRING:
            return expr->as.string ? (int)strlen(expr->as.string) + 2 : 2;
        case EXPR_RUNE:
            return 3;
        case EXPR_IDENT:
            return expr->as.ident.name ? (int)strlen(expr->as.ident.name) : 0;
        case EXPR_NULL:
            return 4;
        case EXPR_BINARY:
            return estimate_expr_len(expr->as.binary.left) + 3 +
                   estimate_expr_len(expr->as.binary.right);
        case EXPR_UNARY:
            return 1 + estimate_expr_len(expr->as.unary.operand);
        case EXPR_CALL: {
            int len = estimate_expr_len(expr->as.call.func) + 2;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (i > 0) len += 2;
                len += estimate_expr_len(expr->as.call.args[i]);
            }
            return len;
        }
        case EXPR_ARRAY_LITERAL: {
            int len = 2;
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (i > 0) len += 2;
                len += estimate_expr_len(expr->as.array_literal.elements[i]);
            }
            return len;
        }
        case EXPR_OBJECT_LITERAL: {
            int len = 4;  // "{ " and " }"
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (i > 0) len += 2;
                len += strlen(expr->as.object_literal.field_names[i]) + 2;
                len += estimate_expr_len(expr->as.object_literal.field_values[i]);
            }
            return len;
        }
        default:
            return 10;  // Default estimate for complex expressions
    }
}

// ========== TYPE FORMATTING ==========

static void fmt_type(FmtCtx *ctx, Type *type) {
    if (!type) return;

    switch (type->kind) {
        case TYPE_I8:  buf_append(&ctx->buf, "i8"); break;
        case TYPE_I16: buf_append(&ctx->buf, "i16"); break;
        case TYPE_I32: buf_append(&ctx->buf, "i32"); break;
        case TYPE_I64: buf_append(&ctx->buf, "i64"); break;
        case TYPE_U8:  buf_append(&ctx->buf, "u8"); break;
        case TYPE_U16: buf_append(&ctx->buf, "u16"); break;
        case TYPE_U32: buf_append(&ctx->buf, "u32"); break;
        case TYPE_U64: buf_append(&ctx->buf, "u64"); break;
        case TYPE_F32: buf_append(&ctx->buf, "f32"); break;
        case TYPE_F64: buf_append(&ctx->buf, "f64"); break;
        case TYPE_BOOL: buf_append(&ctx->buf, "bool"); break;
        case TYPE_STRING: buf_append(&ctx->buf, "string"); break;
        case TYPE_RUNE: buf_append(&ctx->buf, "rune"); break;
        case TYPE_PTR: buf_append(&ctx->buf, "ptr"); break;
        case TYPE_BUFFER: buf_append(&ctx->buf, "buffer"); break;
        case TYPE_ARRAY:
            buf_append(&ctx->buf, "array");
            if (type->element_type) {
                buf_append(&ctx->buf, "<");
                fmt_type(ctx, type->element_type);
                buf_append(&ctx->buf, ">");
            }
            break;
        case TYPE_NULL: buf_append(&ctx->buf, "null"); break;
        case TYPE_CUSTOM_OBJECT:
            if (type->type_name) {
                buf_append(&ctx->buf, type->type_name);
            }
            break;
        case TYPE_GENERIC_OBJECT: buf_append(&ctx->buf, "object"); break;
        case TYPE_ENUM:
            if (type->type_name) {
                buf_append(&ctx->buf, type->type_name);
            }
            break;
        case TYPE_VOID: buf_append(&ctx->buf, "void"); break;
        case TYPE_INFER: break;  // No annotation
    }

    if (type->nullable) {
        buf_append_char(&ctx->buf, '?');
    }
}

// ========== EXPRESSION FORMATTING ==========

static const char *binary_op_str(BinaryOp op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQUAL: return "==";
        case OP_NOT_EQUAL: return "!=";
        case OP_LESS: return "<";
        case OP_LESS_EQUAL: return "<=";
        case OP_GREATER: return ">";
        case OP_GREATER_EQUAL: return ">=";
        case OP_AND: return "&&";
        case OP_OR: return "||";
        case OP_BIT_AND: return "&";
        case OP_BIT_OR: return "|";
        case OP_BIT_XOR: return "^";
        case OP_BIT_LSHIFT: return "<<";
        case OP_BIT_RSHIFT: return ">>";
        default: return "?";
    }
}

static int needs_parens(Expr *parent, Expr *child, int is_right) {
    if (parent->type != EXPR_BINARY || child->type != EXPR_BINARY) {
        return 0;
    }

    // Simple precedence check
    int parent_prec = 0, child_prec = 0;

    BinaryOp pop = parent->as.binary.op;
    BinaryOp cop = child->as.binary.op;

    // Higher number = higher precedence
    if (pop == OP_OR) parent_prec = 1;
    else if (pop == OP_AND) parent_prec = 2;
    else if (pop == OP_BIT_OR) parent_prec = 3;
    else if (pop == OP_BIT_XOR) parent_prec = 4;
    else if (pop == OP_BIT_AND) parent_prec = 5;
    else if (pop == OP_EQUAL || pop == OP_NOT_EQUAL) parent_prec = 6;
    else if (pop == OP_LESS || pop == OP_LESS_EQUAL || pop == OP_GREATER || pop == OP_GREATER_EQUAL) parent_prec = 7;
    else if (pop == OP_BIT_LSHIFT || pop == OP_BIT_RSHIFT) parent_prec = 8;
    else if (pop == OP_ADD || pop == OP_SUB) parent_prec = 9;
    else if (pop == OP_MUL || pop == OP_DIV || pop == OP_MOD) parent_prec = 10;

    if (cop == OP_OR) child_prec = 1;
    else if (cop == OP_AND) child_prec = 2;
    else if (cop == OP_BIT_OR) child_prec = 3;
    else if (cop == OP_BIT_XOR) child_prec = 4;
    else if (cop == OP_BIT_AND) child_prec = 5;
    else if (cop == OP_EQUAL || cop == OP_NOT_EQUAL) child_prec = 6;
    else if (cop == OP_LESS || cop == OP_LESS_EQUAL || cop == OP_GREATER || cop == OP_GREATER_EQUAL) child_prec = 7;
    else if (cop == OP_BIT_LSHIFT || cop == OP_BIT_RSHIFT) child_prec = 8;
    else if (cop == OP_ADD || cop == OP_SUB) child_prec = 9;
    else if (cop == OP_MUL || cop == OP_DIV || cop == OP_MOD) child_prec = 10;

    if (child_prec < parent_prec) return 1;
    if (child_prec == parent_prec && is_right) return 1;
    return 0;
}

// Escape a string for output
static void fmt_escaped_string(FmtCtx *ctx, const char *s) {
    buf_append_char(&ctx->buf, '"');
    while (*s) {
        unsigned char c = *s;
        switch (c) {
            case '\n': buf_append(&ctx->buf, "\\n"); break;
            case '\r': buf_append(&ctx->buf, "\\r"); break;
            case '\t': buf_append(&ctx->buf, "\\t"); break;
            case '\\': buf_append(&ctx->buf, "\\\\"); break;
            case '"': buf_append(&ctx->buf, "\\\""); break;
            default:
                if (c < 32) {
                    buf_printf(&ctx->buf, "\\x%02x", c);
                } else {
                    buf_append_char(&ctx->buf, c);
                }
        }
        s++;
    }
    buf_append_char(&ctx->buf, '"');
}

// Format a rune literal
static void fmt_rune(FmtCtx *ctx, uint32_t codepoint) {
    buf_append_char(&ctx->buf, '\'');
    if (codepoint == '\'') {
        buf_append(&ctx->buf, "\\'");
    } else if (codepoint == '\\') {
        buf_append(&ctx->buf, "\\\\");
    } else if (codepoint == '\n') {
        buf_append(&ctx->buf, "\\n");
    } else if (codepoint == '\r') {
        buf_append(&ctx->buf, "\\r");
    } else if (codepoint == '\t') {
        buf_append(&ctx->buf, "\\t");
    } else if (codepoint < 32) {
        buf_printf(&ctx->buf, "\\x%02x", codepoint);
    } else if (codepoint < 128) {
        buf_append_char(&ctx->buf, (char)codepoint);
    } else if (codepoint < 0x10000) {
        // UTF-8 encode
        char utf8[5];
        if (codepoint < 0x80) {
            utf8[0] = codepoint;
            utf8[1] = '\0';
        } else if (codepoint < 0x800) {
            utf8[0] = 0xC0 | (codepoint >> 6);
            utf8[1] = 0x80 | (codepoint & 0x3F);
            utf8[2] = '\0';
        } else {
            utf8[0] = 0xE0 | (codepoint >> 12);
            utf8[1] = 0x80 | ((codepoint >> 6) & 0x3F);
            utf8[2] = 0x80 | (codepoint & 0x3F);
            utf8[3] = '\0';
        }
        buf_append(&ctx->buf, utf8);
    } else {
        // 4-byte UTF-8
        char utf8[5];
        utf8[0] = 0xF0 | (codepoint >> 18);
        utf8[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        utf8[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[3] = 0x80 | (codepoint & 0x3F);
        utf8[4] = '\0';
        buf_append(&ctx->buf, utf8);
    }
    buf_append_char(&ctx->buf, '\'');
}

static void fmt_expr(FmtCtx *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                buf_printf(&ctx->buf, "%g", expr->as.number.float_value);
            } else {
                buf_printf(&ctx->buf, "%ld", (long)expr->as.number.int_value);
            }
            break;

        case EXPR_BOOL:
            buf_append(&ctx->buf, expr->as.boolean ? "true" : "false");
            break;

        case EXPR_STRING:
            fmt_escaped_string(ctx, expr->as.string);
            break;

        case EXPR_RUNE:
            fmt_rune(ctx, expr->as.rune);
            break;

        case EXPR_IDENT:
            buf_append(&ctx->buf, expr->as.ident.name);
            break;

        case EXPR_NULL:
            buf_append(&ctx->buf, "null");
            break;

        case EXPR_BINARY: {
            int left_parens = needs_parens(expr, expr->as.binary.left, 0);
            int right_parens = needs_parens(expr, expr->as.binary.right, 1);

            if (left_parens) buf_append_char(&ctx->buf, '(');
            fmt_expr(ctx, expr->as.binary.left);
            if (left_parens) buf_append_char(&ctx->buf, ')');

            buf_append_char(&ctx->buf, ' ');
            buf_append(&ctx->buf, binary_op_str(expr->as.binary.op));
            buf_append_char(&ctx->buf, ' ');

            if (right_parens) buf_append_char(&ctx->buf, '(');
            fmt_expr(ctx, expr->as.binary.right);
            if (right_parens) buf_append_char(&ctx->buf, ')');
            break;
        }

        case EXPR_UNARY:
            switch (expr->as.unary.op) {
                case UNARY_NOT: buf_append_char(&ctx->buf, '!'); break;
                case UNARY_NEGATE: buf_append_char(&ctx->buf, '-'); break;
                case UNARY_BIT_NOT: buf_append_char(&ctx->buf, '~'); break;
            }
            fmt_expr(ctx, expr->as.unary.operand);
            break;

        case EXPR_TERNARY:
            fmt_expr(ctx, expr->as.ternary.condition);
            buf_append(&ctx->buf, " ? ");
            fmt_expr(ctx, expr->as.ternary.true_expr);
            buf_append(&ctx->buf, " : ");
            fmt_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_CALL: {
            fmt_expr(ctx, expr->as.call.func);
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int should_break = (ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.call.num_args > 1);

            buf_append(&ctx->buf, "(");
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            }
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }
                fmt_expr(ctx, expr->as.call.args[i]);
                if (should_break) {
                    if (i < expr->as.call.num_args - 1) {
                        buf_append(&ctx->buf, ",");
                    }
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            }
            buf_append(&ctx->buf, ")");
            break;
        }

        case EXPR_ASSIGN:
            buf_append(&ctx->buf, expr->as.assign.name);
            buf_append(&ctx->buf, " = ");
            fmt_expr(ctx, expr->as.assign.value);
            break;

        case EXPR_GET_PROPERTY:
            fmt_expr(ctx, expr->as.get_property.object);
            buf_append_char(&ctx->buf, '.');
            buf_append(&ctx->buf, expr->as.get_property.property);
            break;

        case EXPR_SET_PROPERTY:
            fmt_expr(ctx, expr->as.set_property.object);
            buf_append_char(&ctx->buf, '.');
            buf_append(&ctx->buf, expr->as.set_property.property);
            buf_append(&ctx->buf, " = ");
            fmt_expr(ctx, expr->as.set_property.value);
            break;

        case EXPR_INDEX:
            fmt_expr(ctx, expr->as.index.object);
            buf_append_char(&ctx->buf, '[');
            fmt_expr(ctx, expr->as.index.index);
            buf_append_char(&ctx->buf, ']');
            break;

        case EXPR_INDEX_ASSIGN:
            fmt_expr(ctx, expr->as.index_assign.object);
            buf_append_char(&ctx->buf, '[');
            fmt_expr(ctx, expr->as.index_assign.index);
            buf_append(&ctx->buf, "] = ");
            fmt_expr(ctx, expr->as.index_assign.value);
            break;

        case EXPR_FUNCTION: {
            if (expr->as.function.is_async) {
                buf_append(&ctx->buf, "async ");
            }
            buf_append(&ctx->buf, "fn(");
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                if (expr->as.function.param_is_ref && expr->as.function.param_is_ref[i]) {
                    buf_append(&ctx->buf, "ref ");
                }
                buf_append(&ctx->buf, expr->as.function.param_names[i]);
                if (expr->as.function.param_types && expr->as.function.param_types[i] &&
                    expr->as.function.param_types[i]->kind != TYPE_INFER) {
                    if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                        buf_append(&ctx->buf, "?");
                    }
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, expr->as.function.param_types[i]);
                } else if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    buf_append(&ctx->buf, "?");
                }
                if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, expr->as.function.param_defaults[i]);
                }
            }
            if (expr->as.function.rest_param) {
                if (expr->as.function.num_params > 0) buf_append(&ctx->buf, ", ");
                buf_append(&ctx->buf, "...");
                buf_append(&ctx->buf, expr->as.function.rest_param);
                if (expr->as.function.rest_param_type) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, expr->as.function.rest_param_type);
                }
            }
            buf_append_char(&ctx->buf, ')');
            if (expr->as.function.return_type && expr->as.function.return_type->kind != TYPE_INFER) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, expr->as.function.return_type);
            }
            buf_append(&ctx->buf, " ");
            fmt_stmt(ctx, expr->as.function.body);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int should_break = (ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.array_literal.num_elements > 1);

            buf_append(&ctx->buf, "[");
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            }
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }
                fmt_expr(ctx, expr->as.array_literal.elements[i]);
                if (should_break) {
                    if (i < expr->as.array_literal.num_elements - 1) {
                        buf_append(&ctx->buf, ",");
                    }
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            }
            buf_append(&ctx->buf, "]");
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            update_column(ctx);
            int total_len = estimate_expr_len(expr);
            int should_break = (ctx->column + total_len > FMT_MAX_LINE_WIDTH) &&
                               (expr->as.object_literal.num_fields > 1);

            buf_append(&ctx->buf, "{");
            if (should_break) {
                fmt_newline(ctx);
                ctx->indent++;
            } else {
                buf_append(&ctx->buf, " ");
            }
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (should_break) {
                    fmt_indent(ctx);
                } else if (i > 0) {
                    buf_append(&ctx->buf, ", ");
                }
                buf_append(&ctx->buf, expr->as.object_literal.field_names[i]);
                buf_append(&ctx->buf, ": ");
                fmt_expr(ctx, expr->as.object_literal.field_values[i]);
                if (should_break) {
                    if (i < expr->as.object_literal.num_fields - 1) {
                        buf_append(&ctx->buf, ",");
                    }
                    fmt_newline(ctx);
                }
            }
            if (should_break) {
                ctx->indent--;
                fmt_indent(ctx);
            } else {
                buf_append(&ctx->buf, " ");
            }
            buf_append(&ctx->buf, "}");
            break;
        }

        case EXPR_PREFIX_INC:
            buf_append(&ctx->buf, "++");
            fmt_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_PREFIX_DEC:
            buf_append(&ctx->buf, "--");
            fmt_expr(ctx, expr->as.prefix_dec.operand);
            break;

        case EXPR_POSTFIX_INC:
            fmt_expr(ctx, expr->as.postfix_inc.operand);
            buf_append(&ctx->buf, "++");
            break;

        case EXPR_POSTFIX_DEC:
            fmt_expr(ctx, expr->as.postfix_dec.operand);
            buf_append(&ctx->buf, "--");
            break;

        case EXPR_AWAIT:
            buf_append(&ctx->buf, "await ");
            fmt_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            buf_append_char(&ctx->buf, '`');
            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                // String part
                if (expr->as.string_interpolation.string_parts[i]) {
                    buf_append(&ctx->buf, expr->as.string_interpolation.string_parts[i]);
                }
                // Expression part (except after last string)
                if (i < expr->as.string_interpolation.num_parts) {
                    buf_append(&ctx->buf, "${");
                    fmt_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                    buf_append_char(&ctx->buf, '}');
                }
            }
            buf_append_char(&ctx->buf, '`');
            break;

        case EXPR_OPTIONAL_CHAIN:
            fmt_expr(ctx, expr->as.optional_chain.object);
            buf_append(&ctx->buf, "?");
            if (expr->as.optional_chain.is_call) {
                buf_append_char(&ctx->buf, '(');
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    fmt_expr(ctx, expr->as.optional_chain.args[i]);
                }
                buf_append_char(&ctx->buf, ')');
            } else if (expr->as.optional_chain.is_property) {
                buf_append_char(&ctx->buf, '.');
                buf_append(&ctx->buf, expr->as.optional_chain.property);
            } else {
                buf_append(&ctx->buf, ".[");
                fmt_expr(ctx, expr->as.optional_chain.index);
                buf_append_char(&ctx->buf, ']');
            }
            break;

        case EXPR_NULL_COALESCE:
            fmt_expr(ctx, expr->as.null_coalesce.left);
            buf_append(&ctx->buf, " ?? ");
            fmt_expr(ctx, expr->as.null_coalesce.right);
            break;
    }
}

// ========== STATEMENT FORMATTING ==========

static void fmt_stmt(FmtCtx *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET:
            fmt_indent(ctx);
            // Check if this is a named function declaration (fn name(...) { ... })
            if (stmt->as.let.value && stmt->as.let.value->type == EXPR_FUNCTION) {
                Expr *fn = stmt->as.let.value;
                if (fn->as.function.is_async) {
                    buf_append(&ctx->buf, "async ");
                }
                buf_append(&ctx->buf, "fn ");
                buf_append(&ctx->buf, stmt->as.let.name);
                buf_append_char(&ctx->buf, '(');
                for (int i = 0; i < fn->as.function.num_params; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
                        buf_append(&ctx->buf, "ref ");
                    }
                    buf_append(&ctx->buf, fn->as.function.param_names[i]);
                    if (fn->as.function.param_types && fn->as.function.param_types[i] &&
                        fn->as.function.param_types[i]->kind != TYPE_INFER) {
                        if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
                            buf_append(&ctx->buf, "?");
                        }
                        buf_append(&ctx->buf, ": ");
                        fmt_type(ctx, fn->as.function.param_types[i]);
                    } else if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
                        buf_append(&ctx->buf, "?");
                    }
                    if (fn->as.function.param_defaults && fn->as.function.param_defaults[i]) {
                        buf_append(&ctx->buf, " = ");
                        fmt_expr(ctx, fn->as.function.param_defaults[i]);
                    }
                }
                if (fn->as.function.rest_param) {
                    if (fn->as.function.num_params > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, "...");
                    buf_append(&ctx->buf, fn->as.function.rest_param);
                    if (fn->as.function.rest_param_type) {
                        buf_append(&ctx->buf, ": ");
                        fmt_type(ctx, fn->as.function.rest_param_type);
                    }
                }
                buf_append_char(&ctx->buf, ')');
                if (fn->as.function.return_type && fn->as.function.return_type->kind != TYPE_INFER) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, fn->as.function.return_type);
                }
                buf_append(&ctx->buf, " ");
                fmt_stmt(ctx, fn->as.function.body);
                // No semicolon after function body
            } else {
                buf_append(&ctx->buf, "let ");
                buf_append(&ctx->buf, stmt->as.let.name);
                if (stmt->as.let.type_annotation && stmt->as.let.type_annotation->kind != TYPE_INFER) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, stmt->as.let.type_annotation);
                }
                if (stmt->as.let.value) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.let.value);
                }
                buf_append(&ctx->buf, ";");
                fmt_newline(ctx);
            }
            break;

        case STMT_CONST:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "const ");
            buf_append(&ctx->buf, stmt->as.const_stmt.name);
            if (stmt->as.const_stmt.type_annotation && stmt->as.const_stmt.type_annotation->kind != TYPE_INFER) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, stmt->as.const_stmt.type_annotation);
            }
            if (stmt->as.const_stmt.value) {
                buf_append(&ctx->buf, " = ");
                fmt_expr(ctx, stmt->as.const_stmt.value);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_EXPR:
            fmt_indent(ctx);
            fmt_expr(ctx, stmt->as.expr);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_IF:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "if (");
            fmt_expr(ctx, stmt->as.if_stmt.condition);
            buf_append(&ctx->buf, ") ");
            // Handle block vs single statement
            if (stmt->as.if_stmt.then_branch->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.if_stmt.then_branch);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.if_stmt.then_branch);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            if (stmt->as.if_stmt.else_branch) {
                // Remove trailing newline for else
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " else ");
                if (stmt->as.if_stmt.else_branch->type == STMT_IF) {
                    // else if - don't add braces
                    ctx->indent = 0;  // Reset for else if
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                } else if (stmt->as.if_stmt.else_branch->type == STMT_BLOCK) {
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                } else {
                    buf_append(&ctx->buf, "{");
                    fmt_newline(ctx);
                    ctx->indent++;
                    fmt_stmt(ctx, stmt->as.if_stmt.else_branch);
                    ctx->indent--;
                    fmt_indent(ctx);
                    buf_append(&ctx->buf, "}");
                    fmt_newline(ctx);
                }
            }
            break;

        case STMT_WHILE:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "while (");
            fmt_expr(ctx, stmt->as.while_stmt.condition);
            buf_append(&ctx->buf, ") ");
            if (stmt->as.while_stmt.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.while_stmt.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.while_stmt.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_FOR:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "for (");
            // Initializer (without indent and newline)
            if (stmt->as.for_loop.initializer) {
                if (stmt->as.for_loop.initializer->type == STMT_LET) {
                    buf_append(&ctx->buf, "let ");
                    buf_append(&ctx->buf, stmt->as.for_loop.initializer->as.let.name);
                    if (stmt->as.for_loop.initializer->as.let.value) {
                        buf_append(&ctx->buf, " = ");
                        fmt_expr(ctx, stmt->as.for_loop.initializer->as.let.value);
                    }
                } else if (stmt->as.for_loop.initializer->type == STMT_EXPR) {
                    fmt_expr(ctx, stmt->as.for_loop.initializer->as.expr);
                }
            }
            buf_append(&ctx->buf, "; ");
            if (stmt->as.for_loop.condition) {
                fmt_expr(ctx, stmt->as.for_loop.condition);
            }
            buf_append(&ctx->buf, "; ");
            if (stmt->as.for_loop.increment) {
                fmt_expr(ctx, stmt->as.for_loop.increment);
            }
            buf_append(&ctx->buf, ") ");
            if (stmt->as.for_loop.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.for_loop.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.for_loop.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_FOR_IN:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "for (");
            if (stmt->as.for_in.key_var) {
                buf_append(&ctx->buf, stmt->as.for_in.key_var);
                buf_append(&ctx->buf, ", ");
            }
            buf_append(&ctx->buf, stmt->as.for_in.value_var);
            buf_append(&ctx->buf, " in ");
            fmt_expr(ctx, stmt->as.for_in.iterable);
            buf_append(&ctx->buf, ") ");
            if (stmt->as.for_in.body->type == STMT_BLOCK) {
                fmt_stmt(ctx, stmt->as.for_in.body);
            } else {
                buf_append(&ctx->buf, "{");
                fmt_newline(ctx);
                ctx->indent++;
                fmt_stmt(ctx, stmt->as.for_in.body);
                ctx->indent--;
                fmt_indent(ctx);
                buf_append(&ctx->buf, "}");
                fmt_newline(ctx);
            }
            break;

        case STMT_BREAK:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "break;");
            fmt_newline(ctx);
            break;

        case STMT_CONTINUE:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "continue;");
            fmt_newline(ctx);
            break;

        case STMT_BLOCK:
            buf_append(&ctx->buf, "{");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < stmt->as.block.count; i++) {
                fmt_stmt(ctx, stmt->as.block.statements[i]);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_RETURN:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "return");
            if (stmt->as.return_stmt.value) {
                buf_append_char(&ctx->buf, ' ');
                fmt_expr(ctx, stmt->as.return_stmt.value);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_DEFINE_OBJECT:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "define ");
            buf_append(&ctx->buf, stmt->as.define_object.name);
            buf_append(&ctx->buf, " {");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                fmt_indent(ctx);
                buf_append(&ctx->buf, stmt->as.define_object.field_names[i]);
                if (stmt->as.define_object.field_optional && stmt->as.define_object.field_optional[i]) {
                    buf_append_char(&ctx->buf, '?');
                }
                if (stmt->as.define_object.field_types && stmt->as.define_object.field_types[i]) {
                    buf_append(&ctx->buf, ": ");
                    fmt_type(ctx, stmt->as.define_object.field_types[i]);
                }
                if (stmt->as.define_object.field_defaults && stmt->as.define_object.field_defaults[i]) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.define_object.field_defaults[i]);
                }
                if (i < stmt->as.define_object.num_fields - 1) {
                    buf_append_char(&ctx->buf, ',');
                }
                fmt_newline(ctx);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_ENUM:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "enum ");
            buf_append(&ctx->buf, stmt->as.enum_decl.name);
            buf_append(&ctx->buf, " {");
            fmt_newline(ctx);
            ctx->indent++;
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                fmt_indent(ctx);
                buf_append(&ctx->buf, stmt->as.enum_decl.variant_names[i]);
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    buf_append(&ctx->buf, " = ");
                    fmt_expr(ctx, stmt->as.enum_decl.variant_values[i]);
                }
                if (i < stmt->as.enum_decl.num_variants - 1) {
                    buf_append_char(&ctx->buf, ',');
                }
                fmt_newline(ctx);
            }
            ctx->indent--;
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_TRY:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "try ");
            fmt_stmt(ctx, stmt->as.try_stmt.try_block);
            if (stmt->as.try_stmt.catch_block) {
                // Remove trailing newline for catch
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " catch");
                if (stmt->as.try_stmt.catch_param) {
                    buf_append(&ctx->buf, " (");
                    buf_append(&ctx->buf, stmt->as.try_stmt.catch_param);
                    buf_append_char(&ctx->buf, ')');
                }
                buf_append_char(&ctx->buf, ' ');
                fmt_stmt(ctx, stmt->as.try_stmt.catch_block);
            }
            if (stmt->as.try_stmt.finally_block) {
                if (ctx->buf.len > 0 && ctx->buf.data[ctx->buf.len - 1] == '\n') {
                    ctx->buf.len--;
                    ctx->buf.data[ctx->buf.len] = '\0';
                }
                buf_append(&ctx->buf, " finally ");
                fmt_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            break;

        case STMT_THROW:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "throw ");
            fmt_expr(ctx, stmt->as.throw_stmt.value);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_SWITCH:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "switch (");
            fmt_expr(ctx, stmt->as.switch_stmt.expr);
            buf_append(&ctx->buf, ") {");
            fmt_newline(ctx);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                fmt_indent(ctx);
                if (stmt->as.switch_stmt.case_values[i]) {
                    buf_append(&ctx->buf, "case ");
                    fmt_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                    buf_append(&ctx->buf, ":");
                } else {
                    buf_append(&ctx->buf, "default:");
                }
                fmt_newline(ctx);
                if (stmt->as.switch_stmt.case_bodies[i]) {
                    ctx->indent++;
                    if (stmt->as.switch_stmt.case_bodies[i]->type == STMT_BLOCK) {
                        for (int j = 0; j < stmt->as.switch_stmt.case_bodies[i]->as.block.count; j++) {
                            fmt_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]->as.block.statements[j]);
                        }
                    } else {
                        fmt_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                    }
                    ctx->indent--;
                }
            }
            fmt_indent(ctx);
            buf_append(&ctx->buf, "}");
            fmt_newline(ctx);
            break;

        case STMT_DEFER:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "defer ");
            fmt_expr(ctx, stmt->as.defer_stmt.call);
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;

        case STMT_IMPORT:
            fmt_indent(ctx);
            if (stmt->as.import_stmt.is_namespace) {
                buf_append(&ctx->buf, "import * as ");
                buf_append(&ctx->buf, stmt->as.import_stmt.namespace_name);
            } else {
                buf_append(&ctx->buf, "import { ");
                for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.import_stmt.import_names[i]);
                    if (stmt->as.import_stmt.import_aliases && stmt->as.import_stmt.import_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.import_stmt.import_aliases[i]);
                    }
                }
                buf_append(&ctx->buf, " }");
            }
            buf_append(&ctx->buf, " from \"");
            buf_append(&ctx->buf, stmt->as.import_stmt.module_path);
            buf_append(&ctx->buf, "\";");
            fmt_newline(ctx);
            break;

        case STMT_EXPORT:
            fmt_indent(ctx);
            if (stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
                buf_append(&ctx->buf, "export ");
                // Format the declaration inline without indent
                int saved_indent = ctx->indent;
                ctx->indent = 0;
                // Handle function declarations specially
                Stmt *decl = stmt->as.export_stmt.declaration;
                if (decl->type == STMT_LET && decl->as.let.value &&
                    decl->as.let.value->type == EXPR_FUNCTION) {
                    // export fn name(...) { }
                    Expr *fn = decl->as.let.value;
                    if (fn->as.function.is_async) {
                        buf_append(&ctx->buf, "async ");
                    }
                    buf_append(&ctx->buf, "fn ");
                    buf_append(&ctx->buf, decl->as.let.name);
                    buf_append_char(&ctx->buf, '(');
                    for (int i = 0; i < fn->as.function.num_params; i++) {
                        if (i > 0) buf_append(&ctx->buf, ", ");
                        if (fn->as.function.param_is_ref && fn->as.function.param_is_ref[i]) {
                            buf_append(&ctx->buf, "ref ");
                        }
                        buf_append(&ctx->buf, fn->as.function.param_names[i]);
                        if (fn->as.function.param_types && fn->as.function.param_types[i] &&
                            fn->as.function.param_types[i]->kind != TYPE_INFER) {
                            buf_append(&ctx->buf, ": ");
                            fmt_type(ctx, fn->as.function.param_types[i]);
                        }
                    }
                    buf_append_char(&ctx->buf, ')');
                    if (fn->as.function.return_type && fn->as.function.return_type->kind != TYPE_INFER) {
                        buf_append(&ctx->buf, ": ");
                        fmt_type(ctx, fn->as.function.return_type);
                    }
                    buf_append(&ctx->buf, " ");
                    fmt_stmt(ctx, fn->as.function.body);
                } else {
                    fmt_stmt(ctx, decl);
                }
                ctx->indent = saved_indent;
            } else if (stmt->as.export_stmt.is_reexport) {
                buf_append(&ctx->buf, "export { ");
                for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.export_stmt.export_names[i]);
                    if (stmt->as.export_stmt.export_aliases && stmt->as.export_stmt.export_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.export_stmt.export_aliases[i]);
                    }
                }
                buf_append(&ctx->buf, " } from \"");
                buf_append(&ctx->buf, stmt->as.export_stmt.module_path);
                buf_append(&ctx->buf, "\";");
                fmt_newline(ctx);
            } else {
                buf_append(&ctx->buf, "export { ");
                for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                    if (i > 0) buf_append(&ctx->buf, ", ");
                    buf_append(&ctx->buf, stmt->as.export_stmt.export_names[i]);
                    if (stmt->as.export_stmt.export_aliases && stmt->as.export_stmt.export_aliases[i]) {
                        buf_append(&ctx->buf, " as ");
                        buf_append(&ctx->buf, stmt->as.export_stmt.export_aliases[i]);
                    }
                }
                buf_append(&ctx->buf, " };");
                fmt_newline(ctx);
            }
            break;

        case STMT_IMPORT_FFI:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "import \"");
            buf_append(&ctx->buf, stmt->as.import_ffi.library_path);
            buf_append(&ctx->buf, "\";");
            fmt_newline(ctx);
            break;

        case STMT_EXTERN_FN:
            fmt_indent(ctx);
            buf_append(&ctx->buf, "extern fn ");
            buf_append(&ctx->buf, stmt->as.extern_fn.function_name);
            buf_append_char(&ctx->buf, '(');
            for (int i = 0; i < stmt->as.extern_fn.num_params; i++) {
                if (i > 0) buf_append(&ctx->buf, ", ");
                if (stmt->as.extern_fn.param_types && stmt->as.extern_fn.param_types[i]) {
                    fmt_type(ctx, stmt->as.extern_fn.param_types[i]);
                }
            }
            buf_append_char(&ctx->buf, ')');
            if (stmt->as.extern_fn.return_type) {
                buf_append(&ctx->buf, ": ");
                fmt_type(ctx, stmt->as.extern_fn.return_type);
            }
            buf_append(&ctx->buf, ";");
            fmt_newline(ctx);
            break;
    }
}

// ========== PUBLIC API ==========

// Read entire file into a string (caller must free)
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Could not allocate memory for file\n");
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, file);
    if (bytes_read < (size_t)size) {
        fprintf(stderr, "Error: Could not read file\n");
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

// Write string to file
static int write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s' for writing\n", path);
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);

    if (written != len) {
        fprintf(stderr, "Error: Could not write to file '%s'\n", path);
        return -1;
    }

    return 0;
}

char *format_source(const char *source) {
    // Extract comments before parsing (lexer discards them)
    CommentList comments;
    comment_list_init(&comments);
    extract_comments(source, &comments);

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Format failed: parse errors\n");
        comment_list_free(&comments);
        return NULL;
    }

    // Format
    FmtCtx ctx;
    buf_init(&ctx.buf);
    ctx.indent = 0;
    ctx.column = 0;
    ctx.comments = &comments;
    ctx.output_line = 1;

    // Check if we have statement line numbers (parser may set them to 0)
    int have_line_info = 0;
    for (int i = 0; i < stmt_count; i++) {
        if (statements[i]->line > 0) {
            have_line_info = 1;
            break;
        }
    }

    // If no line info, output all comments at the start (for idempotency)
    if (!have_line_info) {
        for (int i = 0; i < comments.count; i++) {
            Comment *c = &comments.comments[i];
            fmt_indent(&ctx);
            if (c->type == COMMENT_LINE) {
                buf_append(&ctx.buf, "// ");
                buf_append(&ctx.buf, c->text);
            } else {
                buf_append(&ctx.buf, "/* ");
                buf_append(&ctx.buf, c->text);
                buf_append(&ctx.buf, " */");
            }
            fmt_newline(&ctx);
            ctx.output_line++;
        }
        comments.next_idx = comments.count;  // Mark all as consumed
    }

    for (int i = 0; i < stmt_count; i++) {
        // Output leading comments before this statement
        if (have_line_info && statements[i]->line > 0) {
            fmt_leading_comments(&ctx, statements[i]->line);
        }
        fmt_stmt(&ctx, statements[i]);
        // Output trailing comments after this statement
        if (have_line_info && statements[i]->line > 0) {
            fmt_trailing_comment(&ctx, statements[i]->line);
        }
    }

    // Output any remaining comments at end of file
    while (comments.next_idx < comments.count) {
        Comment *c = &comments.comments[comments.next_idx];
        comments.next_idx++;
        fmt_indent(&ctx);
        if (c->type == COMMENT_LINE) {
            buf_append(&ctx.buf, "// ");
            buf_append(&ctx.buf, c->text);
        } else {
            buf_append(&ctx.buf, "/* ");
            buf_append(&ctx.buf, c->text);
            buf_append(&ctx.buf, " */");
        }
        fmt_newline(&ctx);
    }

    // Cleanup AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    // Cleanup comments
    comment_list_free(&comments);

    // Transfer ownership of buffer
    char *result = ctx.buf.data;
    // Don't call buf_free - we're returning the buffer

    return result;
}

int format_file(const char *path) {
    char *source = read_file(path);
    if (source == NULL) {
        return 1;
    }

    char *formatted = format_source(source);
    free(source);

    if (formatted == NULL) {
        return 1;
    }

    int result = write_file(path, formatted);
    free(formatted);

    return result;
}

int format_check(const char *path) {
    char *source = read_file(path);
    if (source == NULL) {
        return -1;
    }

    char *formatted = format_source(source);
    if (formatted == NULL) {
        free(source);
        return -1;
    }

    int result = strcmp(source, formatted) == 0 ? 0 : 1;

    free(source);
    free(formatted);

    return result;
}

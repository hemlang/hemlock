#include "formatter_internal.h"

// ========== COMMENT AND BLANK LINE EXTRACTION ==========

void blank_line_list_init(BlankLineList *list) {
    list->capacity = 32;
    list->lines = malloc(sizeof(int) * list->capacity);
    list->count = 0;
    list->next_idx = 0;
}

void blank_line_list_free(BlankLineList *list) {
    free(list->lines);
    list->lines = NULL;
    list->count = list->capacity = 0;
}

void blank_line_list_add(BlankLineList *list, int line) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        int *new_lines = realloc(list->lines, sizeof(int) * list->capacity);
        if (!new_lines) {
            return;  // Keep original data on allocation failure
        }
        list->lines = new_lines;
    }
    list->lines[list->count++] = line;
}

void comment_list_init(CommentList *list) {
    list->capacity = 16;
    list->comments = malloc(sizeof(Comment) * list->capacity);
    list->count = 0;
    list->next_idx = 0;
}

void comment_list_free(CommentList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->comments[i].text);
    }
    free(list->comments);
    list->comments = NULL;
    list->count = list->capacity = 0;
}

void comment_list_add(CommentList *list, CommentType type, int line, int column,
                      const char *text, int len, int is_trailing) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        Comment *new_comments = realloc(list->comments, sizeof(Comment) * list->capacity);
        if (!new_comments) {
            return;  // Keep original data on allocation failure
        }
        list->comments = new_comments;
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
void extract_comments(const char *source, CommentList *list) {
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

// Extract blank lines from source
void extract_blank_lines(const char *source, BlankLineList *list) {
    const char *p = source;
    int line = 1;
    int line_is_blank = 1;  // Track if current line is blank

    while (*p) {
        if (*p == '\n') {
            if (line_is_blank) {
                blank_line_list_add(list, line);
            }
            p++;
            line++;
            line_is_blank = 1;
        } else if (*p == ' ' || *p == '\t' || *p == '\r') {
            // Whitespace doesn't make line non-blank
            p++;
        } else {
            // Any other character makes line non-blank
            line_is_blank = 0;
            p++;
        }
    }
}

// Check if there's a blank line in range (from_line, to_line)
int has_blank_line_between(BlankLineList *list, int from_line, int to_line) {
    if (!list) return 0;
    for (int i = list->next_idx; i < list->count; i++) {
        int bl = list->lines[i];
        if (bl >= to_line) break;
        if (bl > from_line && bl < to_line) {
            return 1;
        }
    }
    return 0;
}

// Advance blank line index past a given line
void advance_blank_lines_past(BlankLineList *list, int line) {
    if (!list) return;
    while (list->next_idx < list->count && list->lines[list->next_idx] <= line) {
        list->next_idx++;
    }
}

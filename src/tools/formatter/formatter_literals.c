#include "formatter_internal.h"

// ========== ORIGINAL LITERAL PRESERVATION ==========

// Map (line, col) -> original source text for literals
// This allows preserving hex/binary/octal formats and numeric separators

void literal_map_init(LiteralMap *map) {
    map->capacity = 64;
    map->spans = malloc(map->capacity * sizeof(LiteralSpan));
    map->count = 0;
}

void literal_map_free(LiteralMap *map) {
    for (int i = 0; i < map->count; i++) {
        free(map->spans[i].text);
    }
    free(map->spans);
    map->spans = NULL;
    map->count = map->capacity = 0;
}

void literal_map_add(LiteralMap *map, int line, int col, const char *text) {
    if (map->count >= map->capacity) {
        map->capacity *= 2;
        LiteralSpan *new_spans = realloc(map->spans, map->capacity * sizeof(LiteralSpan));
        if (!new_spans) {
            return;  // Keep original data on allocation failure
        }
        map->spans = new_spans;
    }
    map->spans[map->count].line = line;
    map->spans[map->count].col = col;
    map->spans[map->count].text = strdup(text);
    map->count++;
}

const char *literal_map_lookup(LiteralMap *map, int line, int col) {
    if (!map) return NULL;
    for (int i = 0; i < map->count; i++) {
        if (map->spans[i].line == line && map->spans[i].col == col) {
            return map->spans[i].text;
        }
    }
    return NULL;
}

// Extract number literals from source using the lexer (ensures line/col match AST)
void extract_literals(const char *source, LiteralMap *map) {
    if (!source || !map) return;

    Lexer lexer;
    lexer_init(&lexer, source);

    while (1) {
        Token tok = lexer_next(&lexer);
        if (tok.type == TOK_EOF) break;

        if (tok.type == TOK_NUMBER) {
            // Check if this literal has special formatting worth preserving
            // (hex/bin/oct prefix or underscores)
            int has_prefix = (tok.length > 1 && tok.start[0] == '0' &&
                             (tok.start[1] == 'x' || tok.start[1] == 'X' ||
                              tok.start[1] == 'b' || tok.start[1] == 'B' ||
                              tok.start[1] == 'o' || tok.start[1] == 'O'));
            int has_underscore = (memchr(tok.start, '_', tok.length) != NULL);

            if (has_prefix || has_underscore) {
                // Copy the original text
                char *text = malloc(tok.length + 1);
                memcpy(text, tok.start, tok.length);
                text[tok.length] = '\0';
                literal_map_add(map, tok.line, tok.column, text);
                free(text);
            }
        }

        // Free string tokens (they allocate memory)
        if (tok.type == TOK_STRING && tok.string_value) {
            free(tok.string_value);
        }
    }
}

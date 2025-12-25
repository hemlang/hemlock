#include "internal.h"

// ========== PATTERN PARSING ==========

// Forward declarations
static Pattern* parse_pattern_or(Parser *p);
static Pattern* parse_pattern_atom(Parser *p);

// Parse a pattern with optional OR alternatives: pattern | pattern | ...
Pattern* parse_pattern(Parser *p) {
    return parse_pattern_or(p);
}

// Parse pattern alternatives separated by |
static Pattern* parse_pattern_or(Parser *p) {
    Pattern *left = parse_pattern_atom(p);

    if (!match(p, TOK_PIPE)) {
        return left;
    }

    // Build OR pattern
    int capacity = 8;
    Pattern **patterns = malloc(sizeof(Pattern*) * capacity);
    int count = 1;
    patterns[0] = left;

    do {
        if (count >= capacity) {
            capacity *= 2;
            patterns = realloc(patterns, sizeof(Pattern*) * capacity);
        }
        patterns[count++] = parse_pattern_atom(p);
    } while (match(p, TOK_PIPE));

    return pattern_or(patterns, count);
}

// Parse a single pattern atom
static Pattern* parse_pattern_atom(Parser *p) {
    int line = p->current.line;

    // Wildcard pattern: _
    if (check(p, TOK_IDENT)) {
        char *text = token_text(&p->current);
        if (strcmp(text, "_") == 0) {
            advance(p);
            free(text);
            Pattern *pat = pattern_wildcard();
            pat->line = line;
            return pat;
        }
        free(text);
    }

    // Type pattern: is type
    if (match_contextual(p, "is")) {
        Type *type = parse_type(p);
        Pattern *pat = pattern_type(type);
        pat->line = line;
        return pat;
    }

    // Array pattern: [pattern, pattern, ...]
    if (match(p, TOK_LBRACKET)) {
        int capacity = 16;
        Pattern **elements = malloc(sizeof(Pattern*) * capacity);
        int count = 0;
        char *rest_name = NULL;

        if (!check(p, TOK_RBRACKET)) {
            do {
                // Check for rest pattern: ...name
                if (match(p, TOK_DOT_DOT_DOT)) {
                    consume(p, TOK_IDENT, "Expect identifier after '...'");
                    rest_name = token_text(&p->previous);
                    break;
                }

                if (count >= capacity) {
                    capacity *= 2;
                    elements = realloc(elements, sizeof(Pattern*) * capacity);
                }
                elements[count++] = parse_pattern(p);
            } while (match(p, TOK_COMMA));
        }

        consume(p, TOK_RBRACKET, "Expect ']' after array pattern");
        Pattern *pat = pattern_array(elements, count, rest_name);
        pat->line = line;
        if (rest_name) free(rest_name);
        return pat;
    }

    // Object pattern: { field, field: pattern, ... }
    if (match(p, TOK_LBRACE)) {
        int capacity = 16;
        char **field_names = malloc(sizeof(char*) * capacity);
        Pattern **field_patterns = malloc(sizeof(Pattern*) * capacity);
        int count = 0;
        char *rest_name = NULL;

        if (!check(p, TOK_RBRACE)) {
            do {
                // Check for rest pattern: ...name
                if (match(p, TOK_DOT_DOT_DOT)) {
                    consume(p, TOK_IDENT, "Expect identifier after '...'");
                    rest_name = token_text(&p->previous);
                    break;
                }

                if (count >= capacity) {
                    capacity *= 2;
                    field_names = realloc(field_names, sizeof(char*) * capacity);
                    field_patterns = realloc(field_patterns, sizeof(Pattern*) * capacity);
                }

                consume(p, TOK_IDENT, "Expect field name in object pattern");
                field_names[count] = token_text(&p->previous);

                // Check for : pattern
                if (match(p, TOK_COLON)) {
                    field_patterns[count] = parse_pattern(p);
                } else {
                    // Shorthand: { x } means { x: x }
                    field_patterns[count] = pattern_binding(field_names[count]);
                }
                count++;
            } while (match(p, TOK_COMMA));
        }

        consume(p, TOK_RBRACE, "Expect '}' after object pattern");
        Pattern *pat = pattern_object(field_names, field_patterns, count, rest_name);
        pat->line = line;
        if (rest_name) free(rest_name);
        return pat;
    }

    // Literal patterns: numbers, strings, booleans, null
    if (check(p, TOK_NUMBER)) {
        Expr *lit;
        if (p->current.is_float) {
            lit = expr_number_float(p->current.float_value);
        } else {
            lit = expr_number_int(p->current.int_value);
        }
        lit->line = line;
        advance(p);

        // Check for range pattern: start..end
        if (match(p, TOK_DOT_DOT)) {
            Expr *end_expr;
            if (!check(p, TOK_NUMBER)) {
                error(p, "Expect number after '..' in range pattern");
                end_expr = expr_number(0);
            } else {
                if (p->current.is_float) {
                    end_expr = expr_number_float(p->current.float_value);
                } else {
                    end_expr = expr_number_int(p->current.int_value);
                }
                end_expr->line = p->current.line;
                advance(p);
            }
            Pattern *pat = pattern_range(lit, end_expr);
            pat->line = line;
            return pat;
        }

        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    if (check(p, TOK_STRING)) {
        Expr *lit = expr_string(p->current.string_value);
        lit->line = line;
        advance(p);
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    if (match(p, TOK_TRUE)) {
        Expr *lit = expr_bool(1);
        lit->line = line;
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    if (match(p, TOK_FALSE)) {
        Expr *lit = expr_bool(0);
        lit->line = line;
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    if (match(p, TOK_NULL)) {
        Expr *lit = expr_null();
        lit->line = line;
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    if (check(p, TOK_RUNE)) {
        Expr *lit = expr_rune(p->current.rune_value);
        lit->line = line;
        advance(p);
        Pattern *pat = pattern_literal(lit);
        pat->line = line;
        return pat;
    }

    // Binding pattern: identifier (binds value to variable)
    if (check(p, TOK_IDENT)) {
        char *name = token_text(&p->current);
        advance(p);
        Pattern *pat = pattern_binding(name);
        pat->line = line;
        free(name);
        return pat;
    }

    error(p, "Expect pattern");
    Pattern *pat = pattern_wildcard();
    pat->line = line;
    return pat;
}

// Parse primary pattern (same as parse_pattern for now)
Pattern* parse_pattern_primary(Parser *p) {
    return parse_pattern_atom(p);
}

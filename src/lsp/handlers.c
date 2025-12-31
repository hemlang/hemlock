/*
 * LSP Request/Notification Handlers Implementation
 */

#include "handlers.h"
#include "lsp.h"
#include "protocol.h"

#include "../../include/lexer.h"
#include "../../include/parser.h"
#include "../../include/ast.h"
#include "../../include/version.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Symbol Table for Go-to-Definition and Find References
// ============================================================================

typedef enum {
    SYM_VARIABLE,
    SYM_CONSTANT,
    SYM_FUNCTION,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_PARAM,
} SymbolKind;

typedef struct {
    char *name;
    int def_line;       // Definition line (1-based)
    int def_col;        // Definition column (0-based)
    int def_length;     // Length of the symbol name
    SymbolKind kind;    // Symbol kind (variable, function, struct, enum, etc.)
} SymbolDef;

typedef struct {
    char *name;
    int line;           // Usage line (1-based)
    int col;            // Usage column (0-based)
    int length;         // Length of the symbol name
} SymbolUsage;

typedef struct {
    SymbolDef *defs;
    int def_count;
    int def_capacity;
    SymbolUsage *usages;
    int usage_count;
    int usage_capacity;
} SymbolTable;

static void symbol_table_init(SymbolTable *st) {
    st->defs = NULL;
    st->def_count = 0;
    st->def_capacity = 0;
    st->usages = NULL;
    st->usage_count = 0;
    st->usage_capacity = 0;
}

static void symbol_table_free(SymbolTable *st) {
    for (int i = 0; i < st->def_count; i++) {
        free(st->defs[i].name);
    }
    free(st->defs);
    for (int i = 0; i < st->usage_count; i++) {
        free(st->usages[i].name);
    }
    free(st->usages);
}

static void symbol_table_add_def(SymbolTable *st, const char *name, int line, int col, int length, SymbolKind kind) {
    if (st->def_count >= st->def_capacity) {
        st->def_capacity = st->def_capacity == 0 ? 16 : st->def_capacity * 2;
        st->defs = realloc(st->defs, st->def_capacity * sizeof(SymbolDef));
    }
    st->defs[st->def_count].name = strdup(name);
    st->defs[st->def_count].def_line = line;
    st->defs[st->def_count].def_col = col;
    st->defs[st->def_count].def_length = length > 0 ? length : (int)strlen(name);
    st->defs[st->def_count].kind = kind;
    st->def_count++;
}

static void symbol_table_add_usage(SymbolTable *st, const char *name, int line, int col, int length) {
    if (st->usage_count >= st->usage_capacity) {
        st->usage_capacity = st->usage_capacity == 0 ? 32 : st->usage_capacity * 2;
        st->usages = realloc(st->usages, st->usage_capacity * sizeof(SymbolUsage));
    }
    st->usages[st->usage_count].name = strdup(name);
    st->usages[st->usage_count].line = line;
    st->usages[st->usage_count].col = col;
    st->usages[st->usage_count].length = length > 0 ? length : (int)strlen(name);
    st->usage_count++;
}

static SymbolDef *symbol_table_find_def(SymbolTable *st, const char *name) {
    // Return most recent definition (last in list) for scoping
    for (int i = st->def_count - 1; i >= 0; i--) {
        if (strcmp(st->defs[i].name, name) == 0) {
            return &st->defs[i];
        }
    }
    return NULL;
}

// Calculate column for a token given the source content
static int calc_token_col(const char *content, Token *token) {
    const char *line_start = content;
    for (const char *p = content; p < token->start && *p; p++) {
        if (*p == '\n') {
            line_start = p + 1;
        }
    }
    return (int)(token->start - line_start);
}

// Build symbol table by scanning source with lexer
// This gives us accurate line/column info without relying on AST line numbers
static void build_symbol_table_from_source(SymbolTable *st, const char *content) {
    Lexer lexer;
    lexer_init(&lexer, content);

    Token prev = {0};
    Token token;

    do {
        token = lexer_next(&lexer);

        // Check for definition patterns
        if (token.type == TOK_IDENT) {
            // Check if previous token was a definition keyword
            if (prev.type == TOK_LET) {
                // Variable definition
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_def(st, name, token.line, col, token.length, SYM_VARIABLE);
                free(name);
            } else if (prev.type == TOK_CONST) {
                // Constant definition
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_def(st, name, token.line, col, token.length, SYM_CONSTANT);
                free(name);
            } else if (prev.type == TOK_FN) {
                // Function definition
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_def(st, name, token.line, col, token.length, SYM_FUNCTION);
                free(name);
            } else if (prev.type == TOK_DEFINE) {
                // Struct/object type definition
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_def(st, name, token.line, col, token.length, SYM_STRUCT);
                free(name);
            } else if (prev.type == TOK_ENUM) {
                // Enum definition
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_def(st, name, token.line, col, token.length, SYM_ENUM);
                free(name);
            } else {
                // Check if this is a usage (not followed by =, which would be assignment not definition)
                // For now, treat all other identifiers as usages
                char *name = strndup(token.start, token.length);
                int col = calc_token_col(content, &token);
                symbol_table_add_usage(st, name, token.line, col, token.length);
                free(name);
            }
        }

        prev = token;
    } while (token.type != TOK_EOF);
}

// Find the symbol name at a given position using lexer
static char *find_symbol_at_position(const char *content, int target_line, int target_col, int *out_line, int *out_col) {
    Lexer lexer;
    lexer_init(&lexer, content);

    Token token;
    do {
        token = lexer_next(&lexer);

        // LSP positions are 0-based, token lines are 1-based
        if (token.type == TOK_IDENT && token.line - 1 == target_line) {
            // Calculate token column
            const char *line_start = content;
            int current_line = 0;
            for (const char *p = content; *p && current_line < token.line - 1; p++) {
                if (*p == '\n') {
                    current_line++;
                    line_start = p + 1;
                }
            }
            int token_col = (int)(token.start - line_start);

            if (target_col >= token_col && target_col < token_col + token.length) {
                if (out_line) *out_line = token.line;
                if (out_col) *out_col = token_col;
                return strndup(token.start, token.length);
            }
        }
    } while (token.type != TOK_EOF);

    return NULL;
}

// ============================================================================
// Lifecycle Handlers
// ============================================================================

JSONValue *handle_initialize(LSPServer *server, JSONValue *params) {
    // Extract client capabilities (for future use)
    JSONValue *capabilities = json_object_get_object(params, "capabilities");
    if (capabilities) {
        JSONValue *text_doc = json_object_get_object(capabilities, "textDocument");
        if (text_doc) {
            server->supports_hover = json_object_has(text_doc, "hover");
            server->supports_completion = json_object_has(text_doc, "completion");
            server->supports_definition = json_object_has(text_doc, "definition");
        }
    }

    // Extract workspace info
    const char *root_uri = json_object_get_string(params, "rootUri");
    if (root_uri) {
        server->root_uri = strdup(root_uri);
    }

    const char *root_path = json_object_get_string(params, "rootPath");
    if (root_path) {
        server->root_path = strdup(root_path);
    }

    // Build server capabilities response
    JSONValue *result = json_object();

    JSONValue *server_capabilities = json_object();

    // Text document sync - full sync mode
    json_object_set(server_capabilities, "textDocumentSync", json_number(1));

    // Hover support
    json_object_set(server_capabilities, "hoverProvider", json_bool(true));

    // Completion support
    JSONValue *completion_options = json_object();
    JSONValue *trigger_chars = json_array();
    json_array_push(trigger_chars, json_string("."));
    json_object_set(completion_options, "triggerCharacters", trigger_chars);
    json_object_set(server_capabilities, "completionProvider", completion_options);

    // Go to definition support
    json_object_set(server_capabilities, "definitionProvider", json_bool(true));

    // Find references support
    json_object_set(server_capabilities, "referencesProvider", json_bool(true));

    // Document symbol support
    json_object_set(server_capabilities, "documentSymbolProvider", json_bool(true));

    json_object_set(result, "capabilities", server_capabilities);

    // Server info
    JSONValue *server_info = json_object();
    json_object_set(server_info, "name", json_string("hemlock-lsp"));
    json_object_set(server_info, "version", json_string(HEMLOCK_VERSION));
    json_object_set(result, "serverInfo", server_info);

    server->initialized = true;
    fprintf(stderr, "LSP: Initialized\n");

    return result;
}

void handle_initialized(LSPServer *server, JSONValue *params) {
    (void)server;
    (void)params;
    fprintf(stderr, "LSP: Client confirmed initialization\n");
}

JSONValue *handle_shutdown(LSPServer *server, JSONValue *params) {
    (void)params;
    server->shutdown = true;
    fprintf(stderr, "LSP: Shutdown requested\n");
    return json_null();
}

void handle_exit(LSPServer *server, JSONValue *params) {
    (void)params;
    fprintf(stderr, "LSP: Exit notification received\n");
    server->exit_requested = true;
}

// ============================================================================
// Document Synchronization
// ============================================================================

void handle_did_open(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    if (!text_doc) return;

    const char *uri = json_object_get_string(text_doc, "uri");
    const char *text = json_object_get_string(text_doc, "text");
    int version = (int)json_object_get_number(text_doc, "version");

    if (!uri || !text) return;

    fprintf(stderr, "LSP: Document opened: %s\n", uri);

    LSPDocument *doc = lsp_document_open(server, uri, text, version);

    // Parse and collect diagnostics
    lsp_document_parse(doc);

    // Publish diagnostics
    lsp_publish_diagnostics(server, doc);
}

void handle_did_change(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    if (!text_doc) return;

    const char *uri = json_object_get_string(text_doc, "uri");
    int version = (int)json_object_get_number(text_doc, "version");

    if (!uri) return;

    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc) return;

    // Get content changes (we use full sync, so there's one change with full text)
    JSONValue *changes = json_object_get_array(params, "contentChanges");
    if (!changes || changes->as.array->count == 0) return;

    JSONValue *change = changes->as.array->items[0];
    const char *text = json_object_get_string(change, "text");

    if (!text) return;

    fprintf(stderr, "LSP: Document changed: %s\n", uri);

    // Update document
    lsp_document_update(doc, text, version);

    // Re-parse and collect diagnostics
    lsp_document_parse(doc);

    // Publish diagnostics
    lsp_publish_diagnostics(server, doc);
}

void handle_did_close(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    if (!text_doc) return;

    const char *uri = json_object_get_string(text_doc, "uri");
    if (!uri) return;

    fprintf(stderr, "LSP: Document closed: %s\n", uri);

    // Clear diagnostics before closing
    LSPDocument *doc = lsp_document_find(server, uri);
    if (doc) {
        lsp_document_clear_diagnostics(doc);
        // Publish empty diagnostics
        lsp_publish_diagnostics(server, doc);
    }

    lsp_document_close(server, uri);
}

void handle_did_save(LSPServer *server, JSONValue *params) {
    (void)server;
    (void)params;
    // We re-parse on every change, so save doesn't need special handling
    fprintf(stderr, "LSP: Document saved\n");
}

// ============================================================================
// Language Features
// ============================================================================

JSONValue *handle_hover(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    JSONValue *position = json_object_get_object(params, "position");

    if (!text_doc || !position) return json_null();

    const char *uri = json_object_get_string(text_doc, "uri");
    int line = (int)json_object_get_number(position, "line");
    int character = (int)json_object_get_number(position, "character");

    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc || !doc->ast_valid) return json_null();

    // Find the token at the position
    Lexer lexer;
    lexer_init(&lexer, doc->content);

    Token token;
    Token found_token = {0};
    bool found = false;

    do {
        token = lexer_next(&lexer);

        // Check if this token is at the requested position
        // Token line is 1-based, LSP position is 0-based
        if (token.line - 1 == line) {
            // Calculate token column
            const char *line_start = doc->content;
            int current_line = 0;
            for (const char *p = doc->content; *p && current_line < token.line - 1; p++) {
                if (*p == '\n') {
                    current_line++;
                    line_start = p + 1;
                }
            }
            int token_col = (int)(token.start - line_start);

            if (character >= token_col && character < token_col + token.length) {
                found_token = token;
                found = true;
                break;
            }
        }
    } while (token.type != TOK_EOF);

    if (!found) return json_null();

    // Build hover response based on token type
    char *hover_text = NULL;

    switch (found_token.type) {
        case TOK_FN:
            hover_text = strdup("**fn** - Function declaration keyword");
            break;
        case TOK_LET:
            hover_text = strdup("**let** - Variable declaration keyword");
            break;
        case TOK_CONST:
            hover_text = strdup("**const** - Constant declaration keyword");
            break;
        case TOK_IF:
            hover_text = strdup("**if** - Conditional statement");
            break;
        case TOK_ELSE:
            hover_text = strdup("**else** - Else branch of conditional");
            break;
        case TOK_WHILE:
            hover_text = strdup("**while** - While loop");
            break;
        case TOK_FOR:
            hover_text = strdup("**for** - For loop");
            break;
        case TOK_RETURN:
            hover_text = strdup("**return** - Return from function");
            break;
        case TOK_ASYNC:
            hover_text = strdup("**async** - Async function modifier");
            break;
        case TOK_AWAIT:
            hover_text = strdup("**await** - Await async result");
            break;
        case TOK_TRY:
            hover_text = strdup("**try** - Try block for exception handling");
            break;
        case TOK_CATCH:
            hover_text = strdup("**catch** - Catch exception");
            break;
        case TOK_THROW:
            hover_text = strdup("**throw** - Throw exception");
            break;
        case TOK_DEFER:
            hover_text = strdup("**defer** - Defer execution until function returns");
            break;
        case TOK_IMPORT:
            hover_text = strdup("**import** - Import module");
            break;
        case TOK_TYPE_I8:
        case TOK_TYPE_I16:
        case TOK_TYPE_I32:
        case TOK_TYPE_I64:
        case TOK_TYPE_U8:
        case TOK_TYPE_U16:
        case TOK_TYPE_U32:
        case TOK_TYPE_U64:
        case TOK_TYPE_F32:
        case TOK_TYPE_F64: {
            char *tok_text = strndup(found_token.start, found_token.length);
            char buf[128];
            snprintf(buf, sizeof(buf), "**%s** - Numeric type", tok_text);
            hover_text = strdup(buf);
            free(tok_text);
            break;
        }
        case TOK_TYPE_BOOL:
            hover_text = strdup("**bool** - Boolean type (true/false)");
            break;
        case TOK_TYPE_STRING:
            hover_text = strdup("**string** - UTF-8 string type");
            break;
        case TOK_IDENT: {
            // For identifiers, we'd need to look up in symbol table
            // For now, just show the identifier
            char *name = strndup(found_token.start, found_token.length);
            char buf[256];
            snprintf(buf, sizeof(buf), "Identifier: **%s**", name);
            hover_text = strdup(buf);
            free(name);
            break;
        }
        default:
            // No hover for other tokens
            return json_null();
    }

    if (!hover_text) return json_null();

    JSONValue *result = json_object();
    JSONValue *contents = json_object();
    json_object_set(contents, "kind", json_string("markdown"));
    json_object_set(contents, "value", json_string(hover_text));
    json_object_set(result, "contents", contents);

    free(hover_text);
    return result;
}

JSONValue *handle_completion(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    if (!text_doc) return json_null();

    const char *uri = json_object_get_string(text_doc, "uri");
    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc) return json_null();

    // Build completion list with Hemlock keywords and builtins
    JSONValue *items = json_array();

    // Keywords
    const char *keywords[] = {
        "fn", "let", "const", "if", "else", "while", "for", "return",
        "true", "false", "null", "async", "await", "spawn", "join",
        "try", "catch", "finally", "throw", "defer", "import", "from",
        "enum", "define", "switch", "case", "default", "break", "continue"
    };

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        JSONValue *item = json_object();
        json_object_set(item, "label", json_string(keywords[i]));
        json_object_set(item, "kind", json_number(14));  // Keyword
        json_array_push(items, item);
    }

    // Types
    const char *types[] = {
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "f32", "f64", "bool", "string", "rune", "ptr", "buffer",
        "array", "object", "null", "void"
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        JSONValue *item = json_object();
        json_object_set(item, "label", json_string(types[i]));
        json_object_set(item, "kind", json_number(25));  // TypeParameter
        json_array_push(items, item);
    }

    // Builtin functions
    const char *builtins[] = {
        "print", "println", "typeof", "sizeof", "len",
        "alloc", "free", "memset", "memcpy", "realloc",
        "open", "read_file", "write_file",
        "channel", "send", "recv", "close",
        "signal", "raise", "exit", "exec",
        "panic", "assert"
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        JSONValue *item = json_object();
        json_object_set(item, "label", json_string(builtins[i]));
        json_object_set(item, "kind", json_number(3));  // Function
        json_array_push(items, item);
    }

    JSONValue *result = json_object();
    json_object_set(result, "isIncomplete", json_bool(false));
    json_object_set(result, "items", items);

    return result;
}

JSONValue *handle_definition(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    JSONValue *position = json_object_get_object(params, "position");

    if (!text_doc || !position) return json_null();

    const char *uri = json_object_get_string(text_doc, "uri");
    int line = (int)json_object_get_number(position, "line");
    int character = (int)json_object_get_number(position, "character");

    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc || !doc->ast || !doc->ast_valid) return json_null();

    // Find the symbol at the cursor position
    int sym_line, sym_col;
    char *symbol_name = find_symbol_at_position(doc->content, line, character, &sym_line, &sym_col);
    if (!symbol_name) return json_null();

    // Build symbol table from source (more accurate line/column info)
    SymbolTable st;
    symbol_table_init(&st);
    build_symbol_table_from_source(&st, doc->content);

    // Find the definition
    SymbolDef *def = symbol_table_find_def(&st, symbol_name);
    free(symbol_name);

    if (!def) {
        symbol_table_free(&st);
        return json_null();
    }

    // Build location response
    JSONValue *result = json_object();
    json_object_set(result, "uri", json_string(uri));

    JSONValue *range = json_object();
    JSONValue *start = json_object();
    json_object_set(start, "line", json_number(def->def_line - 1));  // Convert to 0-based
    json_object_set(start, "character", json_number(def->def_col));
    JSONValue *end = json_object();
    json_object_set(end, "line", json_number(def->def_line - 1));
    json_object_set(end, "character", json_number(def->def_col + def->def_length));
    json_object_set(range, "start", start);
    json_object_set(range, "end", end);
    json_object_set(result, "range", range);

    symbol_table_free(&st);
    return result;
}

JSONValue *handle_references(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    JSONValue *position = json_object_get_object(params, "position");
    JSONValue *context = json_object_get_object(params, "context");

    if (!text_doc || !position) return json_null();

    const char *uri = json_object_get_string(text_doc, "uri");
    int line = (int)json_object_get_number(position, "line");
    int character = (int)json_object_get_number(position, "character");

    // Check if we should include the declaration
    bool include_declaration = false;
    if (context) {
        include_declaration = json_object_get_bool(context, "includeDeclaration");
    }

    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc || !doc->ast || !doc->ast_valid) return json_null();

    // Find the symbol at the cursor position
    int sym_line, sym_col;
    char *symbol_name = find_symbol_at_position(doc->content, line, character, &sym_line, &sym_col);
    if (!symbol_name) return json_null();

    // Build symbol table from source (more accurate line/column info)
    SymbolTable st;
    symbol_table_init(&st);
    build_symbol_table_from_source(&st, doc->content);

    // Build array of locations
    JSONValue *locations = json_array();

    // Include definition if requested
    if (include_declaration) {
        SymbolDef *def = symbol_table_find_def(&st, symbol_name);
        if (def) {
            JSONValue *loc = json_object();
            json_object_set(loc, "uri", json_string(uri));

            JSONValue *range = json_object();
            JSONValue *start = json_object();
            json_object_set(start, "line", json_number(def->def_line - 1));
            json_object_set(start, "character", json_number(def->def_col));
            JSONValue *end = json_object();
            json_object_set(end, "line", json_number(def->def_line - 1));
            json_object_set(end, "character", json_number(def->def_col + def->def_length));
            json_object_set(range, "start", start);
            json_object_set(range, "end", end);
            json_object_set(loc, "range", range);

            json_array_push(locations, loc);
        }
    }

    // Add all usages
    for (int i = 0; i < st.usage_count; i++) {
        if (strcmp(st.usages[i].name, symbol_name) == 0) {
            JSONValue *loc = json_object();
            json_object_set(loc, "uri", json_string(uri));

            JSONValue *range = json_object();
            JSONValue *start = json_object();
            json_object_set(start, "line", json_number(st.usages[i].line - 1));
            json_object_set(start, "character", json_number(st.usages[i].col));
            JSONValue *end = json_object();
            json_object_set(end, "line", json_number(st.usages[i].line - 1));
            json_object_set(end, "character", json_number(st.usages[i].col + st.usages[i].length));
            json_object_set(range, "start", start);
            json_object_set(range, "end", end);
            json_object_set(loc, "range", range);

            json_array_push(locations, loc);
        }
    }

    free(symbol_name);
    symbol_table_free(&st);
    return locations;
}

JSONValue *handle_document_symbol(LSPServer *server, JSONValue *params) {
    JSONValue *text_doc = json_object_get_object(params, "textDocument");
    if (!text_doc) return json_null();

    const char *uri = json_object_get_string(text_doc, "uri");
    LSPDocument *doc = lsp_document_find(server, uri);
    if (!doc || !doc->content) return json_null();

    // Use the symbol table approach which gives accurate line/column info from lexer
    SymbolTable st;
    symbol_table_init(&st);
    build_symbol_table_from_source(&st, doc->content);

    JSONValue *symbols = json_array();

    // Add all definitions as document symbols
    for (int i = 0; i < st.def_count; i++) {
        SymbolDef *def = &st.defs[i];

        // Skip function parameters in document outline
        if (def->kind == SYM_PARAM) {
            continue;
        }

        JSONValue *symbol = json_object();
        json_object_set(symbol, "name", json_string(def->name));

        // Map SymbolKind to LSP SymbolKind values
        int lsp_kind;
        switch (def->kind) {
            case SYM_FUNCTION:  lsp_kind = 12; break;  // Function
            case SYM_VARIABLE:  lsp_kind = 13; break;  // Variable
            case SYM_CONSTANT:  lsp_kind = 14; break;  // Constant
            case SYM_STRUCT:    lsp_kind = 23; break;  // Struct
            case SYM_ENUM:      lsp_kind = 10; break;  // Enum
            default:            lsp_kind = 13; break;  // Variable (default)
        }
        json_object_set(symbol, "kind", json_number(lsp_kind));

        // Build range - convert from 1-based to 0-based for LSP
        JSONValue *range = json_object();
        JSONValue *start = json_object();
        json_object_set(start, "line", json_number(def->def_line - 1));
        json_object_set(start, "character", json_number(def->def_col));
        JSONValue *end = json_object();
        json_object_set(end, "line", json_number(def->def_line - 1));
        json_object_set(end, "character", json_number(def->def_col + def->def_length));
        json_object_set(range, "start", start);
        json_object_set(range, "end", end);
        json_object_set(symbol, "range", range);

        // Create a separate selectionRange (same values but separate object to avoid double-free)
        JSONValue *selection_range = json_object();
        JSONValue *sel_start = json_object();
        json_object_set(sel_start, "line", json_number(def->def_line - 1));
        json_object_set(sel_start, "character", json_number(def->def_col));
        JSONValue *sel_end = json_object();
        json_object_set(sel_end, "line", json_number(def->def_line - 1));
        json_object_set(sel_end, "character", json_number(def->def_col + def->def_length));
        json_object_set(selection_range, "start", sel_start);
        json_object_set(selection_range, "end", sel_end);
        json_object_set(symbol, "selectionRange", selection_range);

        json_array_push(symbols, symbol);
    }

    symbol_table_free(&st);
    return symbols;
}

// ============================================================================
// Diagnostics Publishing
// ============================================================================

void lsp_publish_diagnostics(LSPServer *server, LSPDocument *doc) {
    JSONValue *params = json_object();
    json_object_set(params, "uri", json_string(doc->uri));

    JSONValue *diagnostics = json_array();

    for (int i = 0; i < doc->diagnostic_count; i++) {
        LSPDiagnostic *d = &doc->diagnostics[i];

        JSONValue *diag = json_object();

        // Range
        JSONValue *range = json_object();
        JSONValue *start = json_object();
        json_object_set(start, "line", json_number(d->range.start.line));
        json_object_set(start, "character", json_number(d->range.start.character));
        JSONValue *end = json_object();
        json_object_set(end, "line", json_number(d->range.end.line));
        json_object_set(end, "character", json_number(d->range.end.character));
        json_object_set(range, "start", start);
        json_object_set(range, "end", end);
        json_object_set(diag, "range", range);

        // Severity
        json_object_set(diag, "severity", json_number(d->severity));

        // Source
        json_object_set(diag, "source", json_string(d->source));

        // Message
        json_object_set(diag, "message", json_string(d->message));

        json_array_push(diagnostics, diag);
    }

    json_object_set(params, "diagnostics", diagnostics);

    // Send notification
    LSPMessage *notification = lsp_notification("textDocument/publishDiagnostics", params);
    lsp_write_message(server->output_fd, notification);
    lsp_message_free(notification);
}

// ============================================================================
// Method Dispatcher
// ============================================================================

JSONValue *lsp_dispatch(LSPServer *server, const char *method, JSONValue *params, bool *is_notification) {
    if (!method) return NULL;

    // Lifecycle
    if (strcmp(method, "initialize") == 0) {
        *is_notification = false;
        return handle_initialize(server, params);
    }
    if (strcmp(method, "initialized") == 0) {
        *is_notification = true;
        handle_initialized(server, params);
        return NULL;
    }
    if (strcmp(method, "shutdown") == 0) {
        *is_notification = false;
        return handle_shutdown(server, params);
    }
    if (strcmp(method, "exit") == 0) {
        *is_notification = true;
        handle_exit(server, params);
        return NULL;
    }

    // Document sync
    if (strcmp(method, "textDocument/didOpen") == 0) {
        *is_notification = true;
        handle_did_open(server, params);
        return NULL;
    }
    if (strcmp(method, "textDocument/didChange") == 0) {
        *is_notification = true;
        handle_did_change(server, params);
        return NULL;
    }
    if (strcmp(method, "textDocument/didClose") == 0) {
        *is_notification = true;
        handle_did_close(server, params);
        return NULL;
    }
    if (strcmp(method, "textDocument/didSave") == 0) {
        *is_notification = true;
        handle_did_save(server, params);
        return NULL;
    }

    // Language features
    if (strcmp(method, "textDocument/hover") == 0) {
        *is_notification = false;
        return handle_hover(server, params);
    }
    if (strcmp(method, "textDocument/completion") == 0) {
        *is_notification = false;
        return handle_completion(server, params);
    }
    if (strcmp(method, "textDocument/definition") == 0) {
        *is_notification = false;
        return handle_definition(server, params);
    }
    if (strcmp(method, "textDocument/references") == 0) {
        *is_notification = false;
        return handle_references(server, params);
    }
    if (strcmp(method, "textDocument/documentSymbol") == 0) {
        *is_notification = false;
        return handle_document_symbol(server, params);
    }

    // Unknown method
    fprintf(stderr, "LSP: Unknown method: %s\n", method);
    return NULL;
}

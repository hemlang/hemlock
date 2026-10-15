/*
 * Hemlock LSP Server Implementation
 */

#include "lsp.h"
#include "protocol.h"
#include "handlers.h"

#include "frontend.h"
#include "tools/type_check.h"
#include "tools/borrow_check.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <errno.h>

// ============================================================================
// LSP Server Lifecycle
// ============================================================================

LSPServer *lsp_server_create(void) {
    LSPServer *server = calloc(1, sizeof(LSPServer));
    server->input_fd = STDIN_FILENO;
    server->output_fd = STDOUT_FILENO;
    server->initialized = false;
    server->shutdown = false;
    server->exit_requested = false;
    server->documents = NULL;
    server->root_uri = NULL;
    server->root_path = NULL;
    server->resolver = module_resolution_new(NULL, NULL);
    return server;
}

// Helper to free AST stored in document
static void lsp_document_free_ast(LSPDocument *doc) {
    if (doc->ast) {
        Stmt **statements = (Stmt **)doc->ast;
        for (int i = 0; i < doc->ast_stmt_count; i++) {
            if (statements[i]) {
                stmt_free(statements[i]);
            }
        }
        free(statements);
        doc->ast = NULL;
        doc->ast_stmt_count = 0;
        doc->ast_valid = false;
    }
}

void lsp_server_free(LSPServer *server) {
    if (!server) return;

    // Free all documents
    LSPDocument *doc = server->documents;
    while (doc) {
        LSPDocument *next = doc->next;
        free(doc->uri);
        free(doc->content);
        lsp_document_clear_diagnostics(doc);
        lsp_document_free_ast(doc);
        free(doc);
        doc = next;
    }

    free(server->root_uri);
    free(server->root_path);
    module_resolution_free(server->resolver);
    free(server);
}

// ============================================================================
// Document Management
// ============================================================================

LSPDocument *lsp_document_open(LSPServer *server, const char *uri, const char *content, int version) {
    // Check if already open
    LSPDocument *existing = lsp_document_find(server, uri);
    if (existing) {
        lsp_document_update(existing, content, version);
        return existing;
    }

    // Create new document
    LSPDocument *doc = calloc(1, sizeof(LSPDocument));
    doc->uri = strdup(uri);
    doc->content = strdup(content);
    doc->version = version;
    doc->ast = NULL;
    doc->ast_stmt_count = 0;
    doc->ast_valid = false;
    doc->diagnostics = NULL;
    doc->diagnostic_count = 0;

    // Add to list
    doc->next = server->documents;
    server->documents = doc;

    return doc;
}

void lsp_document_update(LSPDocument *doc, const char *content, int version) {
    free(doc->content);
    doc->content = strdup(content);
    doc->version = version;
    doc->ast_valid = false;

    // Clear old diagnostics
    lsp_document_clear_diagnostics(doc);
}

void lsp_document_close(LSPServer *server, const char *uri) {
    LSPDocument **prev = &server->documents;
    LSPDocument *doc = server->documents;

    while (doc) {
        if (strcmp(doc->uri, uri) == 0) {
            *prev = doc->next;
            free(doc->uri);
            free(doc->content);
            lsp_document_clear_diagnostics(doc);
            lsp_document_free_ast(doc);
            free(doc);
            return;
        }
        prev = &doc->next;
        doc = doc->next;
    }
}

LSPDocument *lsp_document_find(LSPServer *server, const char *uri) {
    LSPDocument *doc = server->documents;
    while (doc) {
        if (strcmp(doc->uri, uri) == 0) {
            return doc;
        }
        doc = doc->next;
    }
    return NULL;
}

// ============================================================================
// Diagnostics
// ============================================================================

void lsp_document_clear_diagnostics(LSPDocument *doc) {
    for (int i = 0; i < doc->diagnostic_count; i++) {
        free(doc->diagnostics[i].code);
        free(doc->diagnostics[i].source);
        free(doc->diagnostics[i].message);
    }
    free(doc->diagnostics);
    doc->diagnostics = NULL;
    doc->diagnostic_count = 0;
}

void lsp_document_add_diagnostic(LSPDocument *doc, LSPRange range,
                                  LSPDiagnosticSeverity severity,
                                  const char *message) {
    int new_count = doc->diagnostic_count + 1;
    LSPDiagnostic *new_diagnostics = realloc(doc->diagnostics,
                                              new_count * sizeof(LSPDiagnostic));
    if (!new_diagnostics) {
        fprintf(stderr, "LSP error: Failed to expand diagnostics array\n");
        return;  // Skip this diagnostic rather than crash
    }
    doc->diagnostics = new_diagnostics;
    doc->diagnostic_count = new_count;

    LSPDiagnostic *diag = &doc->diagnostics[doc->diagnostic_count - 1];
    diag->range = range;
    diag->severity = severity;
    diag->code = NULL;
    diag->source = strdup("hemlock");
    diag->message = strdup(message);
}

// ============================================================================
// Parsing and Diagnostics Collection
// ============================================================================

// This function is called by parser errors
// We need to hook into the parser's error reporting
static char *lsp_uri_to_path(const char *uri) {
    if (!uri) {
        return NULL;
    }
    if (strncmp(uri, "file://", 7) == 0) {
        return strdup(uri + 7);
    }
    return strdup(uri);
}

static char *lsp_resolve_document_path(LSPServer *server, const char *uri) {
    char *path = lsp_uri_to_path(uri);
    if (!path) {
        return NULL;
    }
    if (!server || !server->resolver) {
        return path;
    }

    char *resolved = resolve_module_path(server->resolver, NULL, path);
    free(path);

    if (!resolved) {
        return strdup(uri);
    }

    return resolved;
}

void lsp_document_parse(LSPServer *server, LSPDocument *doc) {
    lsp_document_clear_diagnostics(doc);
    lsp_document_free_ast(doc);

    if (!doc->content) return;

    // Create lexer and parser
    Lexer lexer;
    lexer_init(&lexer, doc->content);

    Parser parser;
    parser_init(&parser, &lexer);
    parser_enable_error_collection(&parser);

    // Parse the document
    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    // Publish every collected parse error with the parser's real message.
    // Panic-mode recovery in parse_program means a document can carry several
    // independent syntax errors; each becomes its own diagnostic.
    for (ParseError *pe = parser.errors; pe; pe = pe->next) {
        int line = pe->line > 0 ? pe->line - 1 : 0;           // 1-based -> 0-based
        int character = pe->column > 0 ? pe->column - 1 : 0;  // 1-based -> 0-based
        int length = pe->length > 0 ? pe->length : 1;

        LSPRange range = {
            .start = { .line = line, .character = character },
            .end = { .line = line, .character = character + length }
        };

        lsp_document_add_diagnostic(doc, range, LSP_SEVERITY_ERROR, pe->message);
    }

    // A few parser paths (annotation validation) set had_error without going
    // through error_at, so nothing was collected — keep a generic fallback so
    // the editor still shows that the document failed to parse.
    if (parser.had_error && !parser.errors) {
        int line = parser.previous.line > 0 ? parser.previous.line - 1 : 0;
        LSPRange range = {
            .start = { .line = line, .character = 0 },
            .end = { .line = line, .character = 1 }
        };
        lsp_document_add_diagnostic(doc, range, LSP_SEVERITY_ERROR,
                                    "Syntax error");
    }
    parser_free_errors(&parser);

    // Store AST for later use (hover, goto definition, etc.)
    // AST is freed by lsp_document_free_ast() when document is updated or closed
    doc->ast = statements;
    doc->ast_stmt_count = stmt_count;
    doc->ast_valid = !parser.had_error;

    // Run type checking if parsing succeeded
    if (doc->ast_valid && statements && stmt_count > 0) {
        char *document_path = lsp_resolve_document_path(server, doc->uri);
        const char *type_check_path = document_path ? document_path : doc->uri;

        // Create type check context
        TypeCheckContext *type_ctx = type_check_new(type_check_path);

        // Enable error collection mode for LSP
        type_check_enable_collection(type_ctx, doc->content);

        // Run type checking
        type_check_program(type_ctx, statements, stmt_count);

        // Convert type errors to LSP diagnostics
        TypeCheckError *err = type_check_get_errors(type_ctx);
        while (err) {
            // Convert 1-based line to 0-based for LSP
            int lsp_line = err->line > 0 ? err->line - 1 : 0;

            LSPRange range = {
                .start = { .line = lsp_line, .character = err->column },
                .end = { .line = lsp_line, .character = err->end_column }
            };

            LSPDiagnosticSeverity severity = err->is_warning ?
                LSP_SEVERITY_WARNING : LSP_SEVERITY_ERROR;

            lsp_document_add_diagnostic(doc, range, severity, err->message);

            err = err->next;
        }

        // Clean up type checker
        type_check_free(type_ctx);

        // Run the borrow / ownership checker and surface its findings as
        // diagnostics. Uses the default (non-strict) mode for editor feedback:
        // high-precision use-after-free / double-free / free-in-loop checks
        // without the noisier move/leak warnings.
        BorrowContext *borrow_ctx = borrow_check_new(type_check_path);
        if (borrow_ctx) {
            borrow_check_enable_collection(borrow_ctx, doc->content);
            borrow_check_program(borrow_ctx, statements, stmt_count);

            for (BorrowDiag *bd = borrow_ctx->diags; bd; bd = bd->next) {
                int lsp_line = bd->line > 0 ? bd->line - 1 : 0;

                LSPRange range = {
                    .start = { .line = lsp_line, .character = bd->column },
                    .end   = { .line = lsp_line, .character = bd->end_column }
                };

                LSPDiagnosticSeverity severity = bd->is_error ?
                    LSP_SEVERITY_ERROR : LSP_SEVERITY_WARNING;

                lsp_document_add_diagnostic(doc, range, severity, bd->message);
            }

            borrow_check_free(borrow_ctx);
        }

        free(document_path);
    }
}

// ============================================================================
// Server Main Loop
// ============================================================================

int lsp_server_run_stdio(LSPServer *server) {
    server->input_fd = STDIN_FILENO;
    server->output_fd = STDOUT_FILENO;

    // Disable stdout buffering for LSP
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Log to stderr for debugging
    fprintf(stderr, "Hemlock LSP server starting (stdio transport)\n");

    while (!server->exit_requested) {
        // Read message
        LSPMessage *request = lsp_read_message(server->input_fd);
        if (!request) {
            // EOF or error
            fprintf(stderr, "LSP: Connection closed\n");
            break;
        }

        fprintf(stderr, "LSP: Received %s\n", request->method ? request->method : "(response)");

        // Dispatch to handler
        bool is_notification = (request->id == NULL);
        JSONValue *result = lsp_dispatch(server, request->method, request->params, &is_notification);

        // Send response if this was a request (not notification)
        if (!is_notification && request->id) {
            LSPMessage *response;
            if (result) {
                response = lsp_response(request->id, result);
            } else {
                response = lsp_response(request->id, json_null());
            }
            lsp_write_message(server->output_fd, response);
            lsp_message_free(response);
        } else if (result) {
            json_free(result);
        }

        lsp_message_free(request);

        // Check for shutdown (but don't exit until exit notification)
        if (server->shutdown && !server->exit_requested) {
            fprintf(stderr, "LSP: Shutdown complete, waiting for exit\n");
        }
    }

    fprintf(stderr, "LSP: Exiting\n");

    return 0;
}

#ifdef _WIN32
// The TCP transport reads/writes the socket through the POSIX fd API, which
// Windows sockets do not support. The standard stdio transport works.
int lsp_server_run_tcp(LSPServer *server, int port) {
    (void)server;
    fprintf(stderr, "LSP: TCP mode (port %d) is not supported on Windows; use stdio mode\n", port);
    return 1;
}
#else
int lsp_server_run_tcp(LSPServer *server, int port) {
    // Create socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "LSP: Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "LSP: Failed to bind to port %d: %s\n", port, strerror(errno));
        close(listen_fd);
        return 1;
    }

    // Listen
    if (listen(listen_fd, 1) < 0) {
        fprintf(stderr, "LSP: Failed to listen: %s\n", strerror(errno));
        close(listen_fd);
        return 1;
    }

    fprintf(stderr, "Hemlock LSP server listening on port %d\n", port);

    // Accept one connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {
        fprintf(stderr, "LSP: Failed to accept connection: %s\n", strerror(errno));
        close(listen_fd);
        return 1;
    }

    fprintf(stderr, "LSP: Client connected from %s:%d\n",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // Use client socket for I/O
    server->input_fd = client_fd;
    server->output_fd = client_fd;

    // Run main loop
    while (!server->exit_requested) {
        LSPMessage *request = lsp_read_message(server->input_fd);
        if (!request) {
            fprintf(stderr, "LSP: Connection closed\n");
            break;
        }

        fprintf(stderr, "LSP: Received %s\n", request->method ? request->method : "(response)");

        bool is_notification = (request->id == NULL);
        JSONValue *result = lsp_dispatch(server, request->method, request->params, &is_notification);

        if (!is_notification && request->id) {
            LSPMessage *response;
            if (result) {
                response = lsp_response(request->id, result);
            } else {
                response = lsp_response(request->id, json_null());
            }
            lsp_write_message(server->output_fd, response);
            lsp_message_free(response);
        } else if (result) {
            json_free(result);
        }

        lsp_message_free(request);
    }

    fprintf(stderr, "LSP: Exiting (TCP)\n");
    close(client_fd);
    close(listen_fd);
    return 0;
}
#endif // !_WIN32

/*
 * Hemlock LSP Server Implementation
 */

#include "lsp.h"
#include "protocol.h"
#include "handlers.h"

#include "../../include/lexer.h"
#include "../../include/parser.h"
#include "../../include/ast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
    doc->diagnostic_count++;
    doc->diagnostics = realloc(doc->diagnostics,
                               doc->diagnostic_count * sizeof(LSPDiagnostic));

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
void lsp_document_parse(LSPDocument *doc) {
    lsp_document_clear_diagnostics(doc);
    lsp_document_free_ast(doc);

    if (!doc->content) return;

    // Create lexer and parser
    Lexer lexer;
    lexer_init(&lexer, doc->content);

    Parser parser;
    parser_init(&parser, &lexer);

    // Parse the document
    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    // Check for errors
    if (parser.had_error) {
        // The parser error occurred at parser.previous or parser.current
        // Convert to LSP diagnostic

        // Get line number (parser stores 1-based lines)
        int line = parser.previous.line > 0 ? parser.previous.line - 1 : 0;

        // Calculate character position in line
        int character = 0;
        const char *line_start = doc->content;
        int current_line = 0;

        // Find the start of the error line
        for (const char *p = doc->content; *p && current_line < parser.previous.line - 1; p++) {
            if (*p == '\n') {
                current_line++;
                line_start = p + 1;
            }
        }

        // Calculate character offset
        if (parser.previous.start) {
            character = (int)(parser.previous.start - line_start);
            if (character < 0) character = 0;
        }

        LSPRange range = {
            .start = { .line = line, .character = character },
            .end = { .line = line, .character = character + parser.previous.length }
        };

        // Create generic error message if we don't have specifics
        lsp_document_add_diagnostic(doc, range, LSP_SEVERITY_ERROR,
                                    "Syntax error");
    }

    // Store AST for later use (hover, goto definition, etc.)
    // AST is freed by lsp_document_free_ast() when document is updated or closed
    doc->ast = statements;
    doc->ast_stmt_count = stmt_count;
    doc->ast_valid = !parser.had_error;
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

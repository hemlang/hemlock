/*
 * Hemlock Language Server Protocol (LSP) Implementation
 *
 * Provides IDE features via the Language Server Protocol:
 * - Syntax error diagnostics
 * - Hover information (types, documentation)
 * - Go to definition
 * - Symbol completion
 *
 * Usage: hemlock lsp [--stdio | --tcp PORT]
 */

#ifndef HEMLOCK_LSP_H
#define HEMLOCK_LSP_H

#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct LSPServer LSPServer;
typedef struct LSPDocument LSPDocument;
typedef struct LSPPosition LSPPosition;
typedef struct LSPRange LSPRange;
typedef struct LSPDiagnostic LSPDiagnostic;

/*
 * LSP Position (0-based line and character)
 */
struct LSPPosition {
    int line;       // 0-based line number
    int character;  // 0-based character offset (UTF-16 code units)
};

/*
 * LSP Range (start and end positions)
 */
struct LSPRange {
    LSPPosition start;
    LSPPosition end;
};

/*
 * LSP Diagnostic Severity
 */
typedef enum {
    LSP_SEVERITY_ERROR = 1,
    LSP_SEVERITY_WARNING = 2,
    LSP_SEVERITY_INFORMATION = 3,
    LSP_SEVERITY_HINT = 4
} LSPDiagnosticSeverity;

/*
 * LSP Diagnostic
 */
struct LSPDiagnostic {
    LSPRange range;
    LSPDiagnosticSeverity severity;
    char *code;     // Optional diagnostic code
    char *source;   // "hemlock"
    char *message;
};

/*
 * Document state tracked by the server
 */
struct LSPDocument {
    char *uri;              // Document URI (file://...)
    char *content;          // Current content
    int version;            // Document version

    // Cached parse results
    void *ast;              // Cached AST (Stmt**)
    int ast_stmt_count;     // Number of statements
    bool ast_valid;         // Whether AST is up-to-date

    // Diagnostics
    LSPDiagnostic *diagnostics;
    int diagnostic_count;

    struct LSPDocument *next;  // Linked list
};

/*
 * LSP Server state
 */
struct LSPServer {
    // Transport
    int input_fd;           // Input file descriptor (stdin or socket)
    int output_fd;          // Output file descriptor (stdout or socket)

    // State
    bool initialized;       // Received initialize request
    bool shutdown;          // Received shutdown request
    bool exit_requested;    // Received exit notification (should terminate)

    // Client capabilities
    bool supports_diagnostics;
    bool supports_hover;
    bool supports_completion;
    bool supports_definition;

    // Open documents
    LSPDocument *documents;

    // Workspace
    char *root_uri;         // Workspace root
    char *root_path;        // Workspace root as path
};

/*
 * Initialize LSP server
 */
LSPServer *lsp_server_create(void);

/*
 * Free LSP server
 */
void lsp_server_free(LSPServer *server);

/*
 * Run LSP server main loop (stdio transport)
 */
int lsp_server_run_stdio(LSPServer *server);

/*
 * Run LSP server main loop (TCP transport)
 */
int lsp_server_run_tcp(LSPServer *server, int port);

/*
 * Document management
 */
LSPDocument *lsp_document_open(LSPServer *server, const char *uri, const char *content, int version);
void lsp_document_update(LSPDocument *doc, const char *content, int version);
void lsp_document_close(LSPServer *server, const char *uri);
LSPDocument *lsp_document_find(LSPServer *server, const char *uri);

/*
 * Parse document and collect diagnostics
 */
void lsp_document_parse(LSPDocument *doc);

/*
 * Clear diagnostics for a document
 */
void lsp_document_clear_diagnostics(LSPDocument *doc);

/*
 * Add a diagnostic to a document
 */
void lsp_document_add_diagnostic(LSPDocument *doc, LSPRange range,
                                  LSPDiagnosticSeverity severity,
                                  const char *message);

#endif // HEMLOCK_LSP_H

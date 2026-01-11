/*
 * LSP Request/Notification Handlers
 *
 * Implements handlers for LSP methods:
 * - initialize / initialized
 * - shutdown / exit
 * - textDocument/didOpen
 * - textDocument/didChange
 * - textDocument/didClose
 * - textDocument/hover
 * - textDocument/completion
 * - textDocument/definition
 */

#ifndef HEMLOCK_LSP_HANDLERS_H
#define HEMLOCK_LSP_HANDLERS_H

#include "lsp.h"
#include "protocol.h"

/*
 * Handler function type
 */
typedef JSONValue *(*LSPHandler)(LSPServer *server, JSONValue *params);

/*
 * Lifecycle handlers
 */
JSONValue *handle_initialize(LSPServer *server, JSONValue *params);
void handle_initialized(LSPServer *server, JSONValue *params);
JSONValue *handle_shutdown(LSPServer *server, JSONValue *params);
void handle_exit(LSPServer *server, JSONValue *params);

/*
 * Document synchronization handlers
 */
void handle_did_open(LSPServer *server, JSONValue *params);
void handle_did_change(LSPServer *server, JSONValue *params);
void handle_did_close(LSPServer *server, JSONValue *params);
void handle_did_save(LSPServer *server, JSONValue *params);

/*
 * Language feature handlers
 */
JSONValue *handle_hover(LSPServer *server, JSONValue *params);
JSONValue *handle_completion(LSPServer *server, JSONValue *params);
JSONValue *handle_definition(LSPServer *server, JSONValue *params);
JSONValue *handle_references(LSPServer *server, JSONValue *params);
JSONValue *handle_document_symbol(LSPServer *server, JSONValue *params);

/*
 * Dispatch a request/notification to the appropriate handler
 */
JSONValue *lsp_dispatch(LSPServer *server, const char *method, JSONValue *params, bool *is_notification);

/*
 * Publish diagnostics for a document
 */
void lsp_publish_diagnostics(LSPServer *server, LSPDocument *doc);

#endif // HEMLOCK_LSP_HANDLERS_H

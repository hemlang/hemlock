/*
 * Hemlock VS Code Extension
 *
 * Provides language support via the Language Server Protocol:
 * - Syntax error diagnostics
 * - Hover information
 * - Code completion
 * - Go to definition
 * - Document symbols
 */

import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
    // Get the configured path to the Hemlock executable
    const config = vscode.workspace.getConfiguration('hemlock');
    const serverPath = config.get<string>('serverPath', 'hemlock');

    // Server options - run 'hemlock lsp' command
    const serverOptions: ServerOptions = {
        command: serverPath,
        args: ['lsp', '--stdio'],
        transport: TransportKind.stdio
    };

    // Client options
    const clientOptions: LanguageClientOptions = {
        // Register the server for Hemlock documents
        documentSelector: [{ scheme: 'file', language: 'hemlock' }],
        synchronize: {
            // Notify the server about file changes to '.hml' files
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.hml')
        },
        outputChannel: vscode.window.createOutputChannel('Hemlock Language Server')
    };

    // Create the language client and start it
    client = new LanguageClient(
        'hemlockLanguageServer',
        'Hemlock Language Server',
        serverOptions,
        clientOptions
    );

    // Start the client. This will also launch the server
    client.start();

    console.log('Hemlock language server is now active');
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

#!/usr/bin/env python3
"""Test the Hemlock LSP server manually."""

import subprocess
import json
import sys
import os

def make_message(content):
    """Create LSP message with Content-Length header."""
    body = json.dumps(content)
    return f"Content-Length: {len(body)}\r\n\r\n{body}"

def read_response(proc, timeout=1.0):
    """Read LSP response from server."""
    import select
    responses = []

    while True:
        # Check if there's data available
        ready, _, _ = select.select([proc.stdout], [], [], timeout)
        if not ready:
            break

        # Read Content-Length header
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            byte = proc.stdout.read(1)
            if not byte:
                break
            header += byte

        if not header:
            break

        # Parse Content-Length
        header_str = header.decode('utf-8')
        content_length = None
        for line in header_str.split('\r\n'):
            if line.startswith('Content-Length:'):
                content_length = int(line.split(':')[1].strip())
                break

        if content_length is None:
            break

        # Read body
        body = proc.stdout.read(content_length).decode('utf-8')
        responses.append(json.loads(body))

    return responses

def test_lsp():
    """Run LSP tests."""
    print("Starting Hemlock LSP server...")

    proc = subprocess.Popen(
        ['./hemlock', 'lsp', '--stdio'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    test_file_uri = "file:///tmp/test.hml"
    test_content = """// Test file
let x = 42;
let y = x + 10;

fn add(a: i32, b: i32): i32 {
    return a + b;
}

let result = add(x, y);
print(result);

define Person {
    name: string,
    age: i32
}

let p: Person = { name: "Alice", age: 30 };
"""

    tests = []

    # Test 1: Initialize
    print("\n=== Test 1: Initialize ===")
    init_request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "processId": os.getpid(),
            "rootUri": "file:///tmp",
            "capabilities": {}
        }
    }
    proc.stdin.write(make_message(init_request).encode())
    proc.stdin.flush()

    responses = read_response(proc)
    if responses:
        print(f"Initialize response: {json.dumps(responses[0], indent=2)}")
        if 'result' in responses[0] and 'capabilities' in responses[0]['result']:
            print("✓ Initialize OK - capabilities received")
            tests.append(("Initialize", True))
        else:
            print("✗ Initialize FAILED - no capabilities")
            tests.append(("Initialize", False))
    else:
        print("✗ Initialize FAILED - no response")
        tests.append(("Initialize", False))

    # Send initialized notification
    proc.stdin.write(make_message({
        "jsonrpc": "2.0",
        "method": "initialized",
        "params": {}
    }).encode())
    proc.stdin.flush()

    # Test 2: Open document
    print("\n=== Test 2: Open Document ===")
    open_doc = {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": test_file_uri,
                "languageId": "hemlock",
                "version": 1,
                "text": test_content
            }
        }
    }
    proc.stdin.write(make_message(open_doc).encode())
    proc.stdin.flush()

    import time
    time.sleep(0.3)
    responses = read_response(proc)
    print(f"Open document responses: {len(responses)}")
    for r in responses:
        if 'method' in r and r['method'] == 'textDocument/publishDiagnostics':
            diags = r['params']['diagnostics']
            print(f"Diagnostics count: {len(diags)}")
            if len(diags) == 0:
                print("✓ Open Document OK - no syntax errors")
                tests.append(("Open Document", True))
            else:
                print(f"Diagnostics: {diags}")
                tests.append(("Open Document", False))
            break
    else:
        print("✓ Open Document OK (no diagnostics response)")
        tests.append(("Open Document", True))

    # Test 3: Hover on keyword 'fn'
    print("\n=== Test 3: Hover on 'fn' keyword ===")
    hover_request = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": test_file_uri},
            "position": {"line": 4, "character": 0}  # 'fn' at line 5 (0-indexed: 4)
        }
    }
    proc.stdin.write(make_message(hover_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 2:
            if 'result' in r and r['result']:
                contents = r['result'].get('contents', '')
                print(f"Hover result: {contents}")
                if 'fn' in str(contents).lower() or 'function' in str(contents).lower():
                    print("✓ Hover OK - shows function keyword info")
                    tests.append(("Hover keyword", True))
                else:
                    print("✗ Hover response missing expected content")
                    tests.append(("Hover keyword", False))
            else:
                print("✗ Hover FAILED - null result")
                tests.append(("Hover keyword", False))
            break
    else:
        print("✗ Hover FAILED - no response")
        tests.append(("Hover keyword", False))

    # Test 4: Go to definition of 'x' on line 3
    print("\n=== Test 4: Go to Definition ===")
    definition_request = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "textDocument/definition",
        "params": {
            "textDocument": {"uri": test_file_uri},
            "position": {"line": 2, "character": 8}  # 'x' in "let y = x + 10"
        }
    }
    proc.stdin.write(make_message(definition_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 3:
            print(f"Definition response: {json.dumps(r, indent=2)}")
            if 'result' in r and r['result']:
                result = r['result']
                if isinstance(result, dict) and 'range' in result:
                    # Should point to line 1 (0-indexed) where 'let x = 42;' is
                    line = result['range']['start']['line']
                    print(f"Definition found at line {line}")
                    if line == 1:  # "let x = 42;" is line 2 (0-indexed: 1)
                        print("✓ Go to Definition OK - found correct line")
                        tests.append(("Go to Definition", True))
                    else:
                        print(f"✗ Go to Definition - wrong line (expected 1, got {line})")
                        tests.append(("Go to Definition", False))
                else:
                    print("✗ Go to Definition - unexpected result format")
                    tests.append(("Go to Definition", False))
            else:
                print("✗ Go to Definition FAILED - null result")
                tests.append(("Go to Definition", False))
            break
    else:
        print("✗ Go to Definition FAILED - no response")
        tests.append(("Go to Definition", False))

    # Test 5: Find References
    print("\n=== Test 5: Find References ===")
    references_request = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "textDocument/references",
        "params": {
            "textDocument": {"uri": test_file_uri},
            "position": {"line": 1, "character": 4},  # 'x' in "let x = 42;"
            "context": {"includeDeclaration": True}
        }
    }
    proc.stdin.write(make_message(references_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 4:
            print(f"References response: {json.dumps(r, indent=2)}")
            if 'result' in r and r['result']:
                refs = r['result']
                print(f"Found {len(refs)} references")
                # 'x' should appear in: let x = 42, let y = x + 10, add(x, y)
                if len(refs) >= 3:
                    print("✓ Find References OK - found multiple references")
                    tests.append(("Find References", True))
                else:
                    print(f"✗ Find References - found {len(refs)}, expected at least 3")
                    tests.append(("Find References", False))
            else:
                print("✗ Find References FAILED - null result")
                tests.append(("Find References", False))
            break
    else:
        print("✗ Find References FAILED - no response")
        tests.append(("Find References", False))

    # Test 6: Completion
    print("\n=== Test 6: Completion ===")
    completion_request = {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {"uri": test_file_uri},
            "position": {"line": 10, "character": 0}  # empty position
        }
    }
    proc.stdin.write(make_message(completion_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 5:
            if 'result' in r and r['result']:
                items = r['result'].get('items', r['result']) if isinstance(r['result'], dict) else r['result']
                if isinstance(items, list):
                    print(f"Completion items count: {len(items)}")
                    labels = [item.get('label', '') for item in items[:10]]
                    print(f"Sample labels: {labels}")
                    if len(items) > 0:
                        print("✓ Completion OK - items returned")
                        tests.append(("Completion", True))
                    else:
                        print("✗ Completion FAILED - empty items")
                        tests.append(("Completion", False))
                else:
                    print(f"✗ Completion - unexpected result type: {type(items)}")
                    tests.append(("Completion", False))
            else:
                print("✗ Completion FAILED - null result")
                tests.append(("Completion", False))
            break
    else:
        print("✗ Completion FAILED - no response")
        tests.append(("Completion", False))

    # Test 7: Document Symbols
    print("\n=== Test 7: Document Symbols ===")
    symbols_request = {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "textDocument/documentSymbol",
        "params": {
            "textDocument": {"uri": test_file_uri}
        }
    }
    proc.stdin.write(make_message(symbols_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 6:
            print(f"Document symbols response: {json.dumps(r, indent=2)}")
            if 'result' in r and r['result']:
                symbols = r['result']
                print(f"Found {len(symbols)} symbols")
                for sym in symbols:
                    print(f"  - {sym.get('name', '?')} ({sym.get('kind', '?')})")
                # We should find at least: x, y, add, result, Person, p
                if len(symbols) >= 4:
                    print("✓ Document Symbols OK")
                    tests.append(("Document Symbols", True))
                else:
                    print("✗ Document Symbols - too few symbols")
                    tests.append(("Document Symbols", False))
            else:
                print("✗ Document Symbols FAILED - null result")
                tests.append(("Document Symbols", False))
            break
    else:
        print("✗ Document Symbols FAILED - no response")
        tests.append(("Document Symbols", False))

    # Test 8: Hover on variable
    print("\n=== Test 8: Hover on variable ===")
    hover_var_request = {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": test_file_uri},
            "position": {"line": 1, "character": 4}  # 'x' in "let x = 42;"
        }
    }
    proc.stdin.write(make_message(hover_var_request).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)
    for r in responses:
        if 'id' in r and r['id'] == 7:
            print(f"Hover on variable response: {json.dumps(r, indent=2)}")
            if 'result' in r and r['result']:
                contents = r['result'].get('contents', '')
                print(f"Hover result: {contents}")
                # Currently just shows identifier name, not type info
                tests.append(("Hover variable", True))
            else:
                print("Hover on variable returned null (no semantic info)")
                tests.append(("Hover variable", False))
            break
    else:
        print("✗ Hover variable FAILED - no response")
        tests.append(("Hover variable", False))

    # Test 9: Syntax error diagnostics
    print("\n=== Test 9: Syntax Error Diagnostics ===")
    bad_content = "let x = ;"  # syntax error
    change_doc = {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": test_file_uri, "version": 2},
            "contentChanges": [{"text": bad_content}]
        }
    }
    proc.stdin.write(make_message(change_doc).encode())
    proc.stdin.flush()

    time.sleep(0.3)
    responses = read_response(proc)
    found_error = False
    for r in responses:
        if 'method' in r and r['method'] == 'textDocument/publishDiagnostics':
            diags = r['params']['diagnostics']
            print(f"Diagnostics: {json.dumps(diags, indent=2)}")
            if len(diags) > 0:
                print("✓ Syntax Error Diagnostics OK - error detected")
                tests.append(("Syntax Error Diagnostics", True))
                found_error = True
            break
    if not found_error:
        print("✗ Syntax Error Diagnostics FAILED - no error reported")
        tests.append(("Syntax Error Diagnostics", False))

    # Shutdown
    print("\n=== Shutdown ===")
    shutdown = {"jsonrpc": "2.0", "id": 99, "method": "shutdown", "params": None}
    proc.stdin.write(make_message(shutdown).encode())
    proc.stdin.flush()

    time.sleep(0.2)
    responses = read_response(proc)

    exit_notif = {"jsonrpc": "2.0", "method": "exit", "params": None}
    proc.stdin.write(make_message(exit_notif).encode())
    proc.stdin.flush()

    proc.wait(timeout=2)
    print(f"LSP server exited with code: {proc.returncode}")

    # Summary
    print("\n" + "=" * 50)
    print("TEST SUMMARY")
    print("=" * 50)
    passed = sum(1 for _, ok in tests if ok)
    total = len(tests)
    for name, ok in tests:
        status = "✓ PASS" if ok else "✗ FAIL"
        print(f"  {status}: {name}")
    print(f"\nTotal: {passed}/{total} tests passed")

    return passed == total

if __name__ == "__main__":
    success = test_lsp()
    sys.exit(0 if success else 1)

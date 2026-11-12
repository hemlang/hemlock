"""Exercise native WebSocket large writes across unread, slow and reset peers; stdlib WebSocket client.

The healthy peer validates every complete payload throughout churn. This tests
transport survival, not game/auth/reconnect correctness. Run each runtime separately.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time


def exact(sock, length):
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError('WebSocket closed during frame')
        data.extend(chunk)
    return bytes(data)


def connect(port):
    sock = socket.socket()
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    sock.settimeout(5)
    sock.connect(('127.0.0.1', port))
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
    header = bytearray()
    while not header.endswith(b'\r\n\r\n'):
        header.extend(exact(sock, 1))
        if len(header) > 8192:
            raise ValueError('Oversized upgrade response')
    accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
    if b' 101 ' not in header.split(b'\r\n')[0] or accept not in header:
        raise ValueError('Invalid WebSocket upgrade')
    return sock


def message(sock):
    payload = bytearray()
    while True:
        a, b = exact(sock, 2)
        size = b & 127
        if size == 126:
            size = struct.unpack('!H', exact(sock, 2))[0]
        elif size == 127:
            size = struct.unpack('!Q', exact(sock, 8))[0]
        if b & 128 or size > 131072 or (a & 15) not in (0, 2):
            raise ValueError('Unexpected server frame')
        payload.extend(exact(sock, size))
        if a & 128:
            return bytes(payload)


def validate(data):
    if len(data) != 36979 or data[4:] != bytes(range(255))*145:
        raise ValueError('Corrupt or aliased constant payload')
    return struct.unpack_from('<I', data)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', type=int, default=40428)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--hold-seconds', type=float, default=.5)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 100 or not .1 <= args.hold_seconds <= 10:
        parser.error('Bounds: rounds 1..100; hold-seconds .1..10')
    args.output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['HEMLOCK_WS_TEST_PORT'] = str(args.port)
    state = {'frames': 0, 'last_tick': 0, 'errors': []}
    stop = threading.Event()
    healthy = None
    worker = None
    report = {'passed': False, 'rounds': args.rounds, 'hold_seconds': args.hold_seconds, 'send_hz': 10, 'constant_bytes': 36975, 'completed_rounds': 0,
              'server_sha256': hashlib.sha256(args.server_exe.read_bytes()).hexdigest()}
    with (args.output/'server.log').open('w') as log:
        server = subprocess.Popen([str(args.server_exe.resolve())], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+10
            while healthy is None:
                if server.poll() is not None:
                    raise RuntimeError('Server exited at startup')
                try:
                    healthy = connect(args.port)
                except ConnectionRefusedError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(.1)

            def drain():
                try:
                    while not stop.is_set():
                        tick = validate(message(healthy))
                        if tick <= state['last_tick']:
                            raise ValueError('Healthy peer sequence stopped progressing')
                        state['last_tick'] = tick
                        state['frames'] += 1
                except Exception as error:
                    if not stop.is_set():
                        state['errors'].append(str(error))

            worker = threading.Thread(target=drain, daemon=True)
            worker.start()
            for index in range(args.rounds):
                before = state['frames']
                with connect(args.port) as unread, connect(args.port) as slow:
                    end = time.monotonic()+args.hold_seconds
                    slow.settimeout(.1)
                    while time.monotonic() < end:
                        # Deliberately consume at most 1 KiB/s, far below 370 KiB/s.
                        try:
                            slow.recv(100)
                        except socket.timeout:
                            pass
                        time.sleep(.1)
                    if index % 2 == 0:
                        # Abort with unread queued data; other rounds use ordinary TCP close.
                        unread.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
                time.sleep(.2)
                if server.poll() is not None or state['errors']:
                    raise RuntimeError('Transport process/healthy peer failed during churn')
                if state['frames'] <= before:
                    raise RuntimeError('Healthy peer stalled during slow/unread peer churn')
                report['completed_rounds'] += 1
            before = state['frames']
            time.sleep(.5)
            if server.poll() is not None or state['errors'] or state['frames'] <= before:
                raise RuntimeError('Healthy peer failed after final disconnect')
            report['passed'] = True
        except Exception as error:
            report['failure'] = str(error)
        finally:
            report['server_exit_before_cleanup'] = server.poll()
            stop.set()
            if healthy:
                healthy.close()
            if worker:
                worker.join(timeout=1)
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
    report.update(state)
    (args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()

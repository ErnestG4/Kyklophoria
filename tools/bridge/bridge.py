#!/usr/bin/env python3
"""bridge.py — serve web/ and pipe a WebSocket at /link to `kykdesk --serve`.

Python 3 standard library only (no pip, no node). Same job as bridge.mjs:

    python3 tools/bridge/bridge.py [--port 8765] [--host 127.0.0.1] -- --gen --seed 1 --script tests/scripts/m1_rotate.txt --loop

Then open http://localhost:8765/ in Chromium. Each WebSocket connection
spawns its own kykdesk child; bytes are piped both ways untouched. One
client at a time is plenty; more simply get their own child.
"""
import base64, hashlib, os, socket, struct, subprocess, sys, threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB = os.path.join(ROOT, 'web')
KYKDESK = os.path.join(ROOT, 'build', 'host', 'kykdesk')
GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


def ws_send(sock, payload, opcode=0x2):
    n = len(payload)
    hdr = bytes([0x80 | opcode])
    if n < 126: hdr += bytes([n])
    elif n < 65536: hdr += bytes([126]) + struct.pack('>H', n)
    else: hdr += bytes([127]) + struct.pack('>Q', n)
    sock.sendall(hdr + payload)


def ws_recv(sock):
    """One frame → (opcode, payload); None on close/EOF. Handles fragments."""
    def read(n):
        b = b''
        while len(b) < n:
            c = sock.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    payload, opcode = b'', None
    while True:
        h = read(2)
        fin, op, masked, n = h[0] & 0x80, h[0] & 0x0F, h[1] & 0x80, h[1] & 0x7F
        if n == 126: n = struct.unpack('>H', read(2))[0]
        elif n == 127: n = struct.unpack('>Q', read(8))[0]
        mask = read(4) if masked else None
        data = read(n)
        if mask: data = bytes(b ^ mask[i & 3] for i, b in enumerate(data))
        if op == 0x8: return None
        if op == 0x9: ws_send(sock, data, 0xA); continue      # ping → pong
        if op == 0xA: continue
        if op != 0: opcode = op
        payload += data
        if fin: return opcode, payload


class Handler(SimpleHTTPRequestHandler):
    child_args = []

    def __init__(self, *a, **k):
        super().__init__(*a, directory=WEB, **k)

    def log_message(self, fmt, *args):
        sys.stderr.write('  http: ' + fmt % args + '\n')

    def do_GET(self):
        if self.path.split('?')[0] != '/link':
            return super().do_GET()
        key = self.headers.get('Sec-WebSocket-Key')
        if self.headers.get('Upgrade', '').lower() != 'websocket' or not key:
            self.send_error(400, 'expected a WebSocket upgrade'); return
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        self.send_response(101, 'Switching Protocols')
        self.send_header('Upgrade', 'websocket'); self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept); self.end_headers()
        self.close_connection = True
        sock = self.connection
        child = subprocess.Popen([KYKDESK, '--serve'] + self.child_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        sys.stderr.write(f'  link: client {self.client_address[0]} → kykdesk pid {child.pid}\n')

        def pump_out():
            try:
                while True:
                    b = child.stdout.read1(4096) if hasattr(child.stdout, 'read1') else child.stdout.read(4096)
                    if not b: break
                    ws_send(sock, b)
            except (OSError, EOFError): pass
            try: sock.shutdown(socket.SHUT_RDWR)
            except OSError: pass
        t = threading.Thread(target=pump_out, daemon=True); t.start()
        try:
            while True:
                f = ws_recv(sock)
                if f is None: break
                child.stdin.write(f[1]); child.stdin.flush()
        except (EOFError, OSError, BrokenPipeError): pass
        finally:
            try: child.stdin.close()
            except OSError: pass
            child.terminate()
            try: child.wait(timeout=2)
            except subprocess.TimeoutExpired: child.kill()
            sys.stderr.write(f'  link: closed, kykdesk pid {child.pid} stopped\n')


def main(argv):
    port = 8765
    host = '127.0.0.1'   # --host 0.0.0.0 when localhost forwarding is not available (WSL, a VM)
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == '--port': port = int(argv[i + 1]); i += 2
        elif argv[i] == '--host': host = argv[i + 1]; i += 2
        elif argv[i] == '--': args = argv[i + 1:]; break
        else: i += 1
    if not os.path.exists(KYKDESK):
        sys.exit(f'{KYKDESK} not found — run `make host` first')
    Handler.child_args = args or ['--gen', '--seed', '1']
    srv = ThreadingHTTPServer((host, port), Handler)
    srv.daemon_threads = True
    sys.stderr.write(f'bridge: http://localhost:{port}/  on {host}  (kykdesk --serve {" ".join(Handler.child_args)})\n')
    sys.stderr.write(f'        the module instead: http://localhost:{port}/?serial\n')
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == '__main__':
    main(sys.argv[1:])

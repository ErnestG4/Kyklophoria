#!/usr/bin/env node
/* bridge.mjs — the headless bridge: serves web/ over HTTP and pipes a
 * WebSocket at /link byte-for-byte into `build/host/kykdesk --serve …`.
 *
 *   node tools/bridge/bridge.mjs [--port 8765] [--bin build/host/kykdesk] -- --gen --seed 1 --script tests/scripts/m1_rotate.txt --loop
 *
 * Then open http://localhost:8765 — the page auto-connects to the bridge.
 * One WebSocket client at a time; the child is spawned per connection and
 * killed when it closes, so every connect starts a fresh engine. No
 * dependencies: the WebSocket server (RFC 6455 handshake, framing, masking,
 * ping/pong, close) is implemented below.
 */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..', '..');
const webDir = path.join(repo, 'web');

let port = 8765, bin = path.join(repo, 'build', 'host', 'kykdesk'), childArgs = ['--gen', '--seed', '1'];
{
  const argv = process.argv.slice(2);
  const dd = argv.indexOf('--');
  const own = dd >= 0 ? argv.slice(0, dd) : argv;
  if (dd >= 0) childArgs = argv.slice(dd + 1);
  for (let i = 0; i < own.length; i++) {
    if (own[i] === '--port') port = parseInt(own[++i], 10);
    else if (own[i] === '--bin') bin = path.resolve(own[++i]);
    else if (own[i] === '--help' || own[i] === '-h') { console.error('usage: bridge.mjs [--port 8765] [--bin kykdesk] -- <kykdesk args>'); process.exit(0); }
    else { console.error('unknown arg ' + own[i]); process.exit(2); }
  }
}
if (!childArgs.includes('--serve')) childArgs.unshift('--serve');

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.md': 'text/plain; charset=utf-8' };

const server = http.createServer((req, res) => {
  let p = decodeURIComponent((req.url || '/').split('?')[0]);
  if (p === '/') p = '/index.html';
  const file = path.normalize(path.join(webDir, p));
  if (!file.startsWith(webDir)) { res.writeHead(403); res.end('forbidden'); return; }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(data);
  });
});

/* ── WebSocket server (RFC 6455, binary only) ────────────────────────── */
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
let active = null;   /* one client at a time */

function wsFrame(opcode, payload) {
  const len = payload.length;
  let hdr;
  if (len < 126) hdr = Buffer.from([0x80 | opcode, len]);
  else if (len < 65536) { hdr = Buffer.alloc(4); hdr[0] = 0x80 | opcode; hdr[1] = 126; hdr.writeUInt16BE(len, 2); }
  else { hdr = Buffer.alloc(10); hdr[0] = 0x80 | opcode; hdr[1] = 127; hdr.writeBigUInt64BE(BigInt(len), 2); }
  return Buffer.concat([hdr, Buffer.from(payload)]);
}

server.on('upgrade', (req, socket, head) => {
  const url = (req.url || '').split('?')[0];
  const key = req.headers['sec-websocket-key'];
  if (url !== '/link' || !key || !/websocket/i.test(req.headers.upgrade || '')) { socket.write('HTTP/1.1 400 Bad Request\r\n\r\n'); socket.destroy(); return; }
  if (active) { console.error('[bridge] refusing a second client'); socket.write('HTTP/1.1 409 Conflict\r\n\r\n'); socket.destroy(); return; }
  const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
  socket.setNoDelay(true);

  const child = spawn(bin, childArgs, { stdio: ['pipe', 'pipe', 'inherit'] });
  const peer = req.socket.remoteAddress + ':' + req.socket.remotePort;
  console.error(`[bridge] ${peer} connected → ${bin} ${childArgs.join(' ')} (pid ${child.pid})`);
  active = { socket, child };
  let buf = head && head.length ? Buffer.from(head) : Buffer.alloc(0);
  let closed = false;
  const fragments = [];
  const shutdown = reason => {
    if (closed) return; closed = true;
    console.error(`[bridge] ${peer} closed (${reason})`);
    try { socket.end(); } catch {}
    try { child.stdin.end(); } catch {}
    try { child.kill(); } catch {}
    if (active && active.socket === socket) active = null;
  };

  child.on('error', e => { console.error('[bridge] child error: ' + e.message); shutdown('child error'); });
  child.on('exit', code => { if (!closed) console.error(`[bridge] child exited (${code})`); shutdown('child exit'); });
  child.stdout.on('data', d => { if (!closed) socket.write(wsFrame(0x2, d)); });
  socket.on('error', e => shutdown('socket error: ' + e.message));
  socket.on('close', () => shutdown('socket close'));

  socket.on('data', chunk => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 2) return;
      const fin = (buf[0] & 0x80) !== 0, opcode = buf[0] & 0x0f, masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f, at = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); at = 4; }
      else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); at = 10; }
      if (masked && buf.length < at + 4) return;
      const mask = masked ? buf.subarray(at, at + 4) : null; if (masked) at += 4;
      if (buf.length < at + len) return;
      const payload = Buffer.from(buf.subarray(at, at + len));
      buf = buf.subarray(at + len);
      if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      switch (opcode) {
        case 0x0: case 0x1: case 0x2: {
          fragments.push(payload);
          if (fin) { const msg = fragments.length === 1 ? fragments[0] : Buffer.concat(fragments); fragments.length = 0; if (!closed) child.stdin.write(msg); }
          break;
        }
        case 0x8: { try { socket.write(wsFrame(0x8, payload.subarray(0, 2))); } catch {} shutdown('client close'); return; }
        case 0x9: socket.write(wsFrame(0xa, payload)); break;   /* ping → pong */
        case 0xa: break;                                          /* pong */
        default: shutdown('bad opcode ' + opcode); return;
      }
    }
  });
});

server.listen(port, () => {
  console.error(`[bridge] http://localhost:${port}  (ws://localhost:${port}/link → ${bin} ${childArgs.join(' ')})`);
  if (!fs.existsSync(bin)) console.error(`[bridge] warning: ${bin} does not exist yet — run \`make host\``);
});

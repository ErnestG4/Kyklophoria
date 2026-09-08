/* link.js — kyklophoria's wire layer.
 *
 * HostLink v1 framing (CRC32 / COBS / frame header — ported from
 * Audiothurgist's web/hostlink.js, itself a port of alchemy-sdk's
 * tools/hostlink-cli/hostlink.mjs), a single-outstanding-request Link with a
 * priority queue and round-trip measurement, the standard commands the page
 * needs (HELLO, GET_DESCRIPTOR) and the firmware's KykExt parsers (0x60–0x6E:
 * telemetry, space info, cell, stats, action, set-control — see
 * shell/common/kyk_ext.h and core/kyk_telemetry.h, the source of truth).
 *
 * The Link talks to a transport: SerialTransport (Web Serial port) or
 * WsTransport (a WebSocket carrying binary frames to tools/bridge). Plain
 * script in the browser (globals via window.KYK); CommonJS in node so
 * selftest.mjs can drive the desktop binary over stdio (StdioTransport).
 * No dependencies.
 */
(function (root) {
'use strict';

const PROTO = 1;
const CMD = {
  hello: 0x01, getDescriptor: 0x02,
  telemetry: 0x60, spaceInfo: 0x61, cell: 0x62, stats: 0x63, action: 0x64, setControl: 0x6e,
};
const ACT = { resetPhase: 0, nextSpace: 1, loadSpace: 2, renderDiv: 3 };
const TEL = { spectrum: 1, frame: 2 };
const STATUS = ['OK', 'UNSUPPORTED', 'BAD_ARGS', 'BAD_STATE', 'BAD_CRC', 'BAD_SLOT', 'TOO_LARGE',
  'SCHEMA_MISMATCH', 'FLASH_FAIL', 'BUSY', 'FRAME_ERROR'];
const NOTE_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
/* f0 → "A2 +3¢" */
function noteName(f0) {
  if (!(f0 > 0)) return '—';
  const n = 12 * Math.log2(f0 / 440) + 69, r = Math.round(n), c = Math.round((n - r) * 100);
  return NOTE_NAMES[((r % 12) + 12) % 12] + (Math.floor(r / 12) - 1) + (c ? (c > 0 ? ' +' : ' ') + c + '¢' : '');
}

/* ───────────── codecs ───────────── */
const CRC_TABLE = (() => { const t = new Uint32Array(256); for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; } return t; })();
function crc32(buf, seed = 0) { let crc = ~seed >>> 0; for (let i = 0; i < buf.length; i++) crc = (CRC_TABLE[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8)) >>> 0; return ~crc >>> 0; }
function cobsEncode(input) {
  const out = new Uint8Array(input.length + Math.ceil(input.length / 254) + 1);
  let o = 0, codeAt = o++, code = 1;
  for (let i = 0; i < input.length; i++) {
    if (input[i] === 0) { out[codeAt] = code; codeAt = o++; code = 1; }
    else { out[o++] = input[i]; if (++code === 0xff) { out[codeAt] = code; codeAt = o++; code = 1; } }
  }
  out[codeAt] = code; return out.subarray(0, o);
}
function cobsDecode(input) {
  if (input.length === 0) return null;
  const out = new Uint8Array(input.length); let o = 0, i = 0;
  while (i < input.length) {
    const code = input[i++]; if (code === 0) return null;
    const run = code - 1; if (i + run > input.length) return null;
    for (let k = 0; k < run; k++) { const b = input[i++]; if (b === 0) return null; out[o++] = b; }
    if (code !== 0xff && i < input.length) out[o++] = 0;
  }
  return out.subarray(0, o);
}
function buildFrame(type, seq, body) {
  const dec = new Uint8Array(6 + body.length + 4); const dv = new DataView(dec.buffer);
  dec[0] = PROTO; dec[1] = type; dv.setUint16(2, seq, true); dv.setUint16(4, body.length, true); dec.set(body, 6);
  dv.setUint32(6 + body.length, crc32(dec.subarray(0, 6 + body.length)), true);
  const enc = cobsEncode(dec); const out = new Uint8Array(enc.length + 1); out.set(enc); out[enc.length] = 0; return out;
}
/* Byte-at-a-time frame scanner: collects up to each 0x00 delimiter, COBS-
 * decodes, validates header + CRC and hands the frame to onFrame. */
class FrameParser {
  constructor() { this.acc = []; }
  push(byte, onFrame) {
    if (byte !== 0) { this.acc.push(byte); return; }
    const chunk = Uint8Array.from(this.acc); this.acc = [];
    if (chunk.length === 0) return;
    const dec = cobsDecode(chunk); if (!dec || dec.length < 10) return;
    const dv = new DataView(dec.buffer, dec.byteOffset, dec.byteLength);
    const bodyLen = dec.length - 10;
    const ok = dec[0] === PROTO && dv.getUint16(4, true) === bodyLen && crc32(dec.subarray(0, dec.length - 4)) === dv.getUint32(dec.length - 4, true);
    onFrame({ type: dec[1], seq: dv.getUint16(2, true), body: dec.subarray(6, 6 + bodyLen), ok });
  }
}

const u16 = (b, at) => b[at] | (b[at + 1] << 8);
const u32 = (b, at) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
const f32 = (b, at) => new DataView(b.buffer, b.byteOffset + at, 4).getFloat32(0, true);
const i8 = v => (v & 0x80) ? v - 256 : v;
function readStr(b, at) { const n = b[at]; return [new TextDecoder('ascii').decode(b.subarray(at + 1, at + 1 + n)), at + 1 + n]; }
const statusName = s => STATUS[s] || ('device error ' + s);
function concat(parts) { const all = new Uint8Array(parts.reduce((s, p) => s + p.length, 0)); let o = 0; for (const p of parts) { all.set(p, o); o += p.length; } return all; }
const _now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

/* ───────────── transports ─────────────
 * A transport has: start(onBytes) → Promise, write(Uint8Array) → Promise,
 * close() → Promise, and a `kind` label. */

/* Web Serial: a port from navigator.serial.requestPort(), already open(). */
class SerialTransport {
  constructor(port) { this.port = port; this.kind = 'serial'; this.closed = false; this.onError = null; }
  async start(onBytes) {
    this.writer = this.port.writable.getWriter();
    this.readLoop(onBytes);
  }
  /* A non-fatal read error (overrun, framing) closes the reader but leaves
   * port.readable — reacquire and carry on; `done` or a fatal error ends it. */
  async readLoop(onBytes) {
    let ended = false;
    while (this.port.readable && !this.closed && !ended) {
      const reader = this.port.readable.getReader(); this.reader = reader;
      try { for (;;) { const { value, done } = await reader.read(); if (done) { ended = true; break; } onBytes(value); } }
      catch (e) { if (!this.closed && this.onError) this.onError(e); }
      finally { try { reader.releaseLock(); } catch {} }
    }
    if (!this.closed && this.onClose) this.onClose();
  }
  write(bytes) { return this.writer.write(bytes); }
  async close() {
    this.closed = true;
    try { await this.reader?.cancel(); } catch {} try { this.writer?.releaseLock(); } catch {} try { await this.port.close(); } catch {}
  }
}

/* WebSocket carrying raw wire bytes (tools/bridge/bridge.mjs). */
class WsTransport {
  constructor(ws) { this.ws = ws; this.kind = 'bridge'; this.closed = false; this.onError = null; this.onClose = null; }
  start(onBytes) {
    this.ws.binaryType = 'arraybuffer';
    this.ws.onmessage = ev => { if (ev.data instanceof ArrayBuffer) onBytes(new Uint8Array(ev.data)); };
    this.ws.onerror = e => { if (this.onError) this.onError(e); };
    this.ws.onclose = () => { if (!this.closed && this.onClose) this.onClose(); this.closed = true; };
    return Promise.resolve();
  }
  write(bytes) { try { this.ws.send(bytes); return Promise.resolve(); } catch (e) { return Promise.reject(e); } }
  close() { this.closed = true; try { this.ws.close(); } catch {} return Promise.resolve(); }
}

/* node: a child process's stdin/stdout (selftest). */
class StdioTransport {
  constructor(child) { this.child = child; this.kind = 'stdio'; this.closed = false; this.onError = null; this.onClose = null; }
  start(onBytes) {
    this.child.stdout.on('data', d => onBytes(new Uint8Array(d.buffer, d.byteOffset, d.length)));
    this.child.on('exit', () => { if (!this.closed && this.onClose) this.onClose(); this.closed = true; });
    return Promise.resolve();
  }
  write(bytes) { return new Promise((res, rej) => this.child.stdin.write(Buffer.from(bytes), e => e ? rej(e) : res())); }
  close() { this.closed = true; try { this.child.stdin.end(); } catch {} try { this.child.kill(); } catch {} return Promise.resolve(); }
}

/* ───────────── Link ─────────────
 * HostLink is stop-and-wait: one request in flight, answers matched by seq.
 * Two queues feed the wire — `urgent` (anything the user did) drains before
 * `normal` (polls) — so a click never waits behind queued telemetry. A
 * request may carry a `key`: while a request with that key is queued or in
 * flight, the next one with the same key joins it instead of queuing again,
 * which keeps a slow link from piling up polls. Round-trips are timed. */
class Link {
  constructor(transport) {
    this.t = transport; this.seq = 1; this.parser = new FrameParser(); this.pending = null;
    this.urgent = []; this.normal = []; this.keyed = new Map(); this.closed = false;
    this.stats = { rtt: 0, rttAvg: 0, rttMax: 0, reqs: 0, rate: 0, bytesIn: 0, kbps: 0, errors: 0, timeouts: 0 };
    this._win = { t0: _now(), n: 0, max: 0, bytes: 0 };
    this.onError = null; this.onClose = null;
    transport.onError = e => { this.stats.errors++; if (this.onError) this.onError(e); };
    transport.onClose = () => { if (this.onClose) this.onClose(); };
  }
  get kind() { return this.t.kind; }
  async start() {
    await this.t.start(bytes => {
      this.stats.bytesIn += bytes.length; this._win.bytes += bytes.length;
      for (let i = 0; i < bytes.length; i++) this.parser.push(bytes[i], f => this.onFrame(f));
    });
  }
  _measure(t0) {
    const now = _now(), rtt = now - t0, s = this.stats, w = this._win;
    s.rtt = rtt; s.rttAvg = s.rttAvg ? s.rttAvg * 0.8 + rtt * 0.2 : rtt; s.reqs++;
    if (rtt > w.max) w.max = rtt; w.n++;
    if (now - w.t0 >= 1000) { const dt = Math.max(1, now - w.t0); s.rate = w.n * 1000 / dt; s.kbps = w.bytes / dt; s.rttMax = w.max; w.t0 = now; w.n = 0; w.max = 0; w.bytes = 0; }
  }
  onFrame(f) {
    const p = this.pending; if (!p || f.seq !== p.seq) return;
    clearTimeout(p.timer); this.pending = null; this._measure(p.t0);
    if (!f.ok) p.reject(new Error('bad frame'));
    else if (f.type === 0xff) p.reject(new Error('ERR frame (' + statusName(f.body[0]) + ')'));
    else if (f.type !== (p.type | 0x80)) p.reject(new Error('unexpected response type ' + f.type));
    else if (f.body.length && f.body[0] !== 0) p.reject(new Error(statusName(f.body[0])));
    else p.resolve(f.body);
    this.pump();
  }
  /** opts: { urgent: bool, key: string, timeoutMs: number } */
  request(type, body = new Uint8Array(0), opts = {}) {
    if (typeof opts === 'number') opts = { timeoutMs: opts };
    if (opts.key && this.keyed.has(opts.key)) return this.keyed.get(opts.key).promise;
    const q = { type, body, key: opts.key, timeoutMs: opts.timeoutMs || 1500 };
    q.promise = new Promise((resolve, reject) => { q.resolve = resolve; q.reject = reject; });
    if (this.closed) { q.reject(new Error('link closed')); return q.promise; }
    (opts.urgent ? this.urgent : this.normal).push(q);
    if (q.key) this.keyed.set(q.key, q);
    this.pump();
    return q.promise;
  }
  get queued() { return this.urgent.length + this.normal.length + (this.pending ? 1 : 0); }
  pump() {
    if (this.pending || this.closed) return;
    const q = this.urgent.length ? this.urgent.shift() : this.normal.shift();
    if (!q) return;
    const seq = this.seq = (this.seq % 0xfffe) + 1;
    const done = () => { if (q.key && this.keyed.get(q.key) === q) this.keyed.delete(q.key); };
    const p = this.pending = { seq, type: q.type, resolve: v => { done(); q.resolve(v); }, reject: e => { done(); q.reject(e); }, t0: _now(), timer: null };
    p.timer = setTimeout(() => { if (this.pending === p) { this.pending = null; this.stats.timeouts++; p.reject(new Error('timeout')); this.pump(); } }, q.timeoutMs);
    this.t.write(buildFrame(q.type, seq, q.body)).catch(e => { if (this.pending === p) { clearTimeout(p.timer); this.pending = null; p.reject(e); this.pump(); } });
  }
  async close() {
    this.closed = true;
    for (const q of [...this.urgent, ...this.normal]) q.reject(new Error('link closed'));
    this.urgent.length = this.normal.length = 0; this.keyed.clear();
    if (this.pending) { clearTimeout(this.pending.timer); this.pending.reject(new Error('link closed')); this.pending = null; }
    await this.t.close();
  }
}

/* ───────────── standard commands ───────────── */
async function hello(link) {
  const b = await link.request(CMD.hello, new Uint8Array(0), { urgent: true }); let at = 5;
  const uid = Array.from(b.subarray(at, at + 12)).map(x => x.toString(16).padStart(2, '0')).join(''); at += 12;
  const schema = u32(b, at); at += 4; const cap = u32(b, at); at += 4; const dlen = u32(b, at); at += 4; const dcrc = u32(b, at); at += 4;
  const maxBody = u16(b, at); at += 2; const liveSize = u16(b, at); at += 2;
  let id, name, fw, git, sdk; [id, at] = readStr(b, at); [name, at] = readStr(b, at); [fw, at] = readStr(b, at); [git, at] = readStr(b, at); [sdk, at] = readStr(b, at);
  return { proto: b[1], board: b[2], slots: b[3], bootSlot: b[4], uid, schema, cap, descriptorLen: dlen, descriptorCrc: dcrc, maxBody, liveSize, id, name, fw, git, sdk };
}
async function getDescriptor(link, info) {
  if (!info.descriptorLen) return null;
  const chunkMax = Math.max(16, info.maxBody - 7); const parts = []; let off = 0;
  while (off < info.descriptorLen) {
    const req = new Uint8Array(6); const dv = new DataView(req.buffer); dv.setUint32(0, off, true); dv.setUint16(4, chunkMax, true);
    const b = await link.request(CMD.getDescriptor, req, { urgent: true }); const n = u16(b, 5); if (!n) break;
    parts.push(b.slice(7, 7 + n)); off += n;
  }
  const all = concat(parts);
  if (crc32(all) !== info.descriptorCrc) throw new Error('descriptor CRC mismatch');
  return JSON.parse(new TextDecoder('utf-8').decode(all));
}

/* ───────────── KykExt (0x60–0x6E) ───────────── */

/* 0x60 GET_TELEMETRY body after status (core/kyk_telemetry.h):
 *   u32 block · f32 f0 · u8 n k kcut p planes flags stereo spreadPlane · f32 spread
 *   f32 ctl[n] centre[n] posL[n] posR[n] angle[planes] payload[p]
 *   [flags&1] u8 mags[k] (0 = ≤ −96 dB, 255 = 0 dB) · [flags&2] i8 frame[256] (×40) */
function parseTelemetry(b) {
  if (b.length < 21) return null;
  const t = { block: u32(b, 1), f0: f32(b, 5), n: b[9], k: b[10], kcut: b[11], p: b[12], planes: b[13], flags: b[14], stereo: b[15] !== 0, spreadPlane: b[16], spread: f32(b, 17) };
  let at = 21;
  const arr = n => { const a = new Float32Array(n); for (let i = 0; i < n; i++) { a[i] = f32(b, at); at += 4; } return a; };
  t.ctl = arr(t.n); t.centre = arr(t.n); t.posL = arr(t.n); t.posR = arr(t.n); t.angle = arr(t.planes); t.payload = arr(t.p);
  if (t.flags & TEL.spectrum) { t.mags = b.slice(at, at + t.k); at += t.k; }
  if (t.flags & TEL.frame) { t.frame = new Int8Array(256); for (let i = 0; i < 256; i++) t.frame[i] = i8(b[at + i]); at += 256; }
  t.bytes = b.length;
  return t;
}
function telemetryReq(flags = TEL.spectrum | TEL.frame) { return Uint8Array.of(flags & 0xff); }
const magDb = v => v === 0 ? -96 : (v - 255) * 96 / 255;   /* u8 → dBFS-ish */

/* 0x61 GET_SPACE_INFO: the 64-byte header, u32 crc, u16 stride */
function parseSpaceInfo(b) {
  if (b.length < 1 + 64 + 6) return null;
  const h = b.subarray(1, 65), topoNames = ['clamp', 'wrap', 'sphere'];
  const magic = u32(h, 0);
  const n = h[6], mode = h[7], k = h[8], p = h[9], side = h[10], flags = h[11];
  const topo = []; for (let a = 0; a < n; a++) topo.push(topoNames[h[12 + a]] || ('?' + h[12 + a]));
  const name = new TextDecoder('ascii').decode(h.subarray(28, 60)).replace(/\0.*$/, '');
  return { magic, version: u16(h, 4), n, mode, k, p, side, flags, topo, phaseSeed: u32(h, 20), pointCount: u32(h, 24), name, crc: u32(b, 65), stride: u16(b, 69) };
}
/* 0x62 GET_CELL: u32 idx → u8 k, u8 p, u8 mags[k], f32 payload[p] */
function cellReq(idx) { const r = new Uint8Array(4); new DataView(r.buffer).setUint32(0, idx >>> 0, true); return r; }
function parseCell(b) {
  if (b.length < 3) return null;
  const k = b[1], p = b[2]; const mags = b.slice(3, 3 + k); const payload = new Float32Array(p);
  for (let j = 0; j < p; j++) payload[j] = f32(b, 3 + k + 4 * j);
  return { k, p, mags, payload };
}
/* 0x63 GET_STATS */
function parseStats(b) {
  if (b.length < 1 + 12 + 4 + 1 + 4) return null;
  return { cyclesLast: u32(b, 1), cyclesMax: u32(b, 5), cyclesAvg: u32(b, 9), overruns: u16(b, 13), dropped: u16(b, 15), renderDiv: b[17], cyclesBudget: u32(b, 18) };
}
/* 0x64 ACTION */
function actionReq(op, args = []) { return Uint8Array.of(op & 0xff, ...args); }
/* 0x6E SET_CONTROL: f32 f0, u8 n, f32 c[n], u8 planes, f32 angle[planes], f32 spread */
function setControlReq(f0, ctl, angles, spread) {
  const n = ctl.length, planes = angles.length;
  const r = new Uint8Array(4 + 1 + 4 * n + 1 + 4 * planes + 4); const dv = new DataView(r.buffer); let at = 0;
  dv.setFloat32(at, f0, true); at += 4; r[at++] = n;
  for (let a = 0; a < n; a++) { dv.setFloat32(at, ctl[a], true); at += 4; }
  r[at++] = planes;
  for (let p = 0; p < planes; p++) { dv.setFloat32(at, angles[p], true); at += 4; }
  dv.setFloat32(at, spread, true);
  return r;
}
/* Lattice geometry shared with the page: plane index → axes, lexicographic */
function planeAxes(n, plane) { let p = 0; for (let a = 0; a < n; a++) for (let b = a + 1; b < n; b++) { if (p === plane) return [a, b]; p++; } return [0, 1]; }
const planeCount = n => n * (n - 1) / 2;

const api = {
  CMD, ACT, TEL, STATUS, PROTO, crc32, cobsEncode, cobsDecode, buildFrame, FrameParser, Link,
  SerialTransport, WsTransport, StdioTransport, hello, getDescriptor,
  parseTelemetry, telemetryReq, magDb, parseSpaceInfo, cellReq, parseCell, parseStats, actionReq, setControlReq,
  planeAxes, planeCount, noteName, statusName, u16, u32, f32,
};
if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.KYK = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

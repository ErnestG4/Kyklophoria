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
  telemetry: 0x60, spaceInfo: 0x61, cell: 0x62, stats: 0x63, action: 0x64,
  worlds: 0x65, basis: 0x66, putWorld: 0x67, cardWorlds: 0x68,
  putSlot: 0x69, slots: 0x6a, saveCard: 0x6b, getSlot: 0x6c, tour: 0x6d, setControl: 0x6e,
  resonate: 0x6f,
};
/* 0x6D TOUR ops (shell/common/kyk_ext.h) */
const TOUR = { get: 0, set: 1, tick: 2, max: 8 };
const ACT = { resetPhase: 0, nextSpace: 1, loadSpace: 2, renderDiv: 3, selectWorld: 4,
              morphWorld: 5, scanCard: 6, loadCardWorld: 7, phase: 8, motionMute: 9,
              aimMorph: 10, slotLive: 11, slotTarget: 12, slotFree: 13, slotSwap: 14,
              cardToSlot: 15, snapshot: 16, strike: 17, tune: 18, polyphony: 19, pitchLock: 20, velTrack: 21, memberMorph: 22, release: 23 };
/* "that file is already there" — a well-formed request whose answer is a
   question for the player, which is why it is not BAD_ARGS. */
const STAT_CARD_EXISTS = 20;
/* A morph target that is one of yours has no world index; 0xFF already means
   no target at all. */
const MORPH_USER = 0xFE;
const SLOT_COUNT = 32;
const WORLD_KIND = { lattice: 1, analytic: 2, vertices: 3, fm: 4, formant: 5, table: 6, lock: 7, unison: 8, modal: 9, bend: 10 };
const TEL = { spectrum: 1, frame: 2, motion: 4 };
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
/* The cap is not optional, and its absence was a way to lose the tab.
 *
 * A frame is at most maxBody plus COBS overhead and a header, so anything
 * longer is not a frame — it is a device that crashed mid-transmit, a wrong
 * baud rate, or line noise, and the right answer to all three is to throw the
 * chunk away at the next delimiter and resync. Without a cap this array grew
 * for as long as the bytes kept coming: 4 MB of undelimited input took the
 * heap to 96 MB, about 25x amplification, and the page died while the module
 * was perfectly healthy.
 *
 * The SDK parser this was ported from has exactly this rule (frame.h: fill to
 * the buffer, else set overflow_, and drop the chunk at the delimiter). The
 * port kept the accumulate-and-decode half and left the overflow half behind. */
const kMaxWire = 1024 + 64;

class FrameParser {
  constructor() { this.acc = []; this.over = false; }
  push(byte, onFrame) {
    if (byte !== 0) {
      if (this.acc.length < kMaxWire) this.acc.push(byte);
      else this.over = true;
      return;
    }
    const chunk = Uint8Array.from(this.acc); this.acc = [];
    const over = this.over; this.over = false;
    if (chunk.length === 0 || over) return;
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
const statusName = s => (s === STAT_CARD_EXISTS ? 'that file is already on the card'
                                                 : STATUS[s] || ('device error ' + s));
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
    /* One delimiter before anything else, which is what "connect twice" was.
     *
     * A link does not always close tidily: the page goes away mid-frame, the
     * cable is pulled, the module resets while a request is in flight. The
     * module's parser is left holding the first half of a frame, and COBS has
     * no way to know that — the next thing it sees is our HELLO, which it
     * appends to the garbage and delimits into one malformed frame. It drops
     * it silently (that is the protocol working: `n == 0 || overflow` and the
     * runt check in the SDK's frame.h both say "resync"), so nothing answers,
     * HELLO times out, and connecting fails. Clicking Serial again then
     * works, because the *first* attempt's delimiter is what cleared the
     * accumulator. Hence: often twice, never three times.
     *
     * A lone zero is the documented way to say "throw away what you have".
     * It costs one byte and the module's parser has always known what to do
     * with it — nothing was ever sending it. */
    try { await this.t.write(Uint8Array.of(0)); } catch { /* the first request will report it */ }
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
/* `tries` because a link can open onto a module that is mid-something — a
   reset, a half-sent reply, a buffer the driver held across the close — and
   the cost of asking again is one 400 ms timeout against making the person
   click Connect a second time. The resync in Link.start covers the common
   case; this covers the rest. */
async function hello(link, tries = 3) {
  let last = null;
  for (let i = 0; i < tries; i++) {
    try { return await helloOnce(link); }
    catch (e) {
      last = e;
      if (link.closed) break;
      await new Promise(r => setTimeout(r, 120));
      /* and say it again, in case this attempt is the one that resynced */
      try { await link.t.write(Uint8Array.of(0)); } catch { /* reported below */ }
    }
  }
  throw last || new Error('no answer');
}
async function helloOnce(link) {
  const b = await link.request(CMD.hello, new Uint8Array(0), { urgent: true, timeoutMs: 400 }); let at = 5;
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

/* ── importing a single cycle ──────────────────────────────────────────
 *
 * A node is signed coefficients on the sine basis, because every cell in a
 * world shares one phase spectrum — that is what makes the frame linear in the
 * magnitude vector and the morph provably click-free. Per-harmonic phase would
 * make import exact and would take that guarantee away: two nodes with
 * different phases no longer blend linearly, and partials can cancel, which is
 * the comb-filtered dip the whole design exists to make unreachable.
 *
 * So an imported wave is projected, and the projection loses whatever sits in
 * the cosine half. A cycle's start phase is arbitrary, so we are free to rotate
 * it first, and `fit` reports what survived the best rotation: measured over
 * 401 AKWF waveforms the mean is 94%, four in five keep over 90%, and the worst
 * keeps 55%. That number is shown rather than hidden, because which waveforms
 * import faithfully is a thing the person choosing them should get to see.
 *
 * `mode: 'spectrum'` is the other honest answer — keep the magnitudes exactly
 * and let the sine basis supply the shape. The spectrum survives whole; the
 * waveform is not the one you imported. */
function wavToCycle(buf) {
  const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const tag = (o) => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);
  if (b.length < 44 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('not a WAV file');
  let at = 12, fmt = null, data = null;
  while (at + 8 <= b.length) {
    const id = tag(at), sz = dv.getUint32(at + 4, true);
    if (id === 'fmt ') fmt = { code: dv.getUint16(at + 8, true), ch: dv.getUint16(at + 10, true),
                               bits: dv.getUint16(at + 22, true) };
    else if (id === 'data') data = { off: at + 8, len: Math.min(sz, b.length - at - 8) };
    at += 8 + sz + (sz & 1);
  }
  if (!fmt || !data) throw new Error('WAV has no fmt or data chunk');
  const bytes = fmt.bits >> 3;
  if (!bytes || !fmt.ch) throw new Error('unsupported WAV layout');
  const n = Math.floor(data.len / bytes / fmt.ch);
  if (n < 8) throw new Error('too few samples for a cycle');
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const o = data.off + i * fmt.ch * bytes;       /* channel 0 only */
    out[i] = fmt.bits === 16 ? dv.getInt16(o, true) / 32768
           : fmt.bits === 8  ? (b[o] - 128) / 128
           : fmt.bits === 24 ? ((b[o] | (b[o + 1] << 8) | ((b[o + 2] << 24) >> 8)) / 8388608)
           : fmt.code === 3  ? dv.getFloat32(o, true)
                             : dv.getInt32(o, true) / 2147483648;
  }
  return out;
}

/* One cycle -> K signed sine-basis coefficients, plus what the projection kept. */
function cycleToNode(x, k = 64, mode = 'shape') {
  const N = x.length;
  const a = new Float64Array(k + 1), bb = new Float64Array(k + 1);
  for (let h = 1; h <= k; h++) {
    let ar = 0, br = 0;
    for (let i = 0; i < N; i++) {
      const t = 2 * Math.PI * h * i / N;
      ar += x[i] * Math.cos(t); br += x[i] * Math.sin(t);
    }
    a[h] = 2 * ar / N; bb[h] = 2 * br / N;
  }
  let tot = 0;
  for (let h = 1; h <= k; h++) tot += a[h] * a[h] + bb[h] * bb[h];
  const mags = new Float32Array(k);
  if (mode === 'spectrum') {
    for (let h = 1; h <= k; h++) mags[h - 1] = Math.hypot(a[h], bb[h]);
    return { mags, fit: 1, rotation: 0, mode };
  }
  /* One DFT, then the rotation search is k operations per offset rather than a
     fresh transform — 64 x N in total, not 64 x N x N. */
  let bestKeep = -1, bestAt = 0;
  for (let s = 0; s < N; s++) {
    let keep = 0;
    for (let h = 1; h <= k; h++) {
      const th = 2 * Math.PI * h * s / N;
      const v = bb[h] * Math.cos(th) - a[h] * Math.sin(th);
      keep += v * v;
    }
    if (keep > bestKeep) { bestKeep = keep; bestAt = s; }
  }
  for (let h = 1; h <= k; h++) {
    const th = 2 * Math.PI * h * bestAt / N;
    mags[h - 1] = bb[h] * Math.cos(th) - a[h] * Math.sin(th);
  }
  return { mags, fit: tot > 0 ? bestKeep / tot : 1, rotation: bestAt, mode };
}

/* Build a user-world blob (core/kyk_userworld.h). `nodes` is an array of
   { pos: [..n], mags: Float32Array(k) }; coefficients are written exactly as
   given, because per-node level is how much a node weighs in the blend and
   overriding it would override the author. */
/* The frame shapers a world may put on an axis, numbered as core/kyk_shapes.h
   numbers them. Ring modulation and phase distortion are in the list because
   the shape worlds use them and the stage is the same; a resonator is not,
   and cannot be — it has state, and a world is one cycle read cyclically. */
const SHAPER = { none: 0, fold: 1, ring: 2, warp: 3, crush: 4, drop: 5 };
const SHAPER_NAME = ['none', 'fold', 'ring', 'warp', 'crush', 'drop'];

function buildUserWorld(nodes, { n = 4, k = 64, sigma = 0.26, name = '', sinePhase = true, fx = [] } = {}) {
  const count = nodes.length;
  if (count < 1 || count > 24) throw new Error('1..24 nodes');
  if (n < 2 || n > 6) throw new Error('n must be 2..6');
  if (k < 1 || k > 64) throw new Error('k must be 1..64');
  /* Effects: (axis << 4) | shaper in the two bytes v1 reserved, and the version
     moves to 2 only when one is declared — see core/kyk_userworld.h for why
     that asymmetry is the point rather than a shortcut. */
  const eff = [0, 0];
  let live = 0;
  fx.slice(0, 2).forEach((f, q) => {
    if (!f || !f.kind) return;
    if (!(f.kind >= 1 && f.kind <= 5)) throw new Error('unknown effect');
    if (!(f.axis >= 0 && f.axis < n)) throw new Error('effect axis ' + f.axis + ' is not an axis of this world');
    eff[q] = (f.axis << 4) | f.kind;
    live++;
  });
  if (live === 2 && (eff[0] >> 4) === (eff[1] >> 4)) throw new Error('two effects on one axis');
  const size = 32 + count * 4 * (n + k);
  const b = new Uint8Array(size), dv = new DataView(b.buffer);
  dv.setUint32(0, 0x574B594B, true);      /* 'KYKW' */
  dv.setUint16(4, live ? 2 : 1, true);
  b[6] = n; b[7] = k; b[8] = count; b[9] = sinePhase ? 1 : 0;
  dv.setFloat32(10, sigma, true);
  b[14] = eff[0]; b[15] = eff[1];
  for (let i = 0; i < 16 && i < name.length; i++) b[16 + i] = name.charCodeAt(i) & 0x7f;
  let at = 32;
  for (const nd of nodes) {
    for (let a = 0; a < n; a++) { dv.setFloat32(at, nd.pos[a] ?? 0.5, true); at += 4; }
    for (let i = 0; i < k; i++) { dv.setFloat32(at, nd.mags[i] ?? 0, true); at += 4; }
  }
  return b;
}

/* A slot's bytes back from the module, chunked like the descriptor and the
   basis. This is what lets the page open a world it did not itself send — draw
   its nodes, or capture it into the build view to edit. */
async function fetchSlot(link, slot, maxBody = 1024) {
  const chunk = Math.max(16, maxBody - 32);
  const parts = [];
  let off = 0, total = 0;
  for (let guard = 0; guard < 512; guard++) {
    const req = new Uint8Array(7), dv = new DataView(req.buffer);
    req[0] = slot & 0xff;
    dv.setUint32(1, off, true);
    dv.setUint16(5, chunk, true);
    const b = await link.request(CMD.getSlot, req, { urgent: true });
    total = u32(b, 1);
    const n = u16(b, 9);
    if (!n) break;
    parts.push(b.slice(11, 11 + n));
    off += n;
    if (off >= total) break;
  }
  if (!total) return null;
  const all = concat(parts);
  return all.length === total ? all : null;
}

/* Write a slot to the card as <name>.kykw. The module supplies the folder and
   the extension, so a name cannot escape the world directory.

   `overwrite` must be asked for: without it the module refuses an existing file
   with STAT_CARD_EXISTS rather than replacing it. A card is somebody's
   collection and this is the one destructive thing in the protocol, so the
   default is the safe one and a host that forgets to ask cannot do harm by
   forgetting. */
async function saveCardWorld(link, slot, name, overwrite = false) {
  const nm = new TextEncoder().encode(name.slice(0, 32));
  const req = new Uint8Array(3 + nm.length);
  req[0] = slot & 0xff;
  req[1] = overwrite ? 1 : 0;
  req[2] = nm.length;
  req.set(nm, 3);
  await link.request(CMD.saveCard, req, { urgent: true, timeoutMs: 4000 });
  return true;
}

/* What .kykw files the card's /kyklophoria folder holds. The count comes
   before the names so a host can size its list even when the body ran out
   before the names did. */
async function fetchCardWorlds(link) {
  const b = await link.request(CMD.cardWorlds, new Uint8Array(0), { key: 'card' });
  if (!b.length || b[0] !== 0) return { count: 0, names: [] };
  const count = b[1] || 0;
  const names = [];
  let at = 2;
  while (at < b.length && names.length < count) {
    const n = b[at++];
    if (at + n > b.length) break;
    names.push(new TextDecoder().decode(b.subarray(at, at + n)));
    at += n;
  }
  return { count, names };
}

/* Read a .kykw back (core/kyk_userworld.h). The page needs this for two
   reasons: to refuse a dropped file that is not a world *before* putting it on
   the wire, and to know where a world's nodes sit so the play view can draw
   them. Returns null rather than throwing — a file somebody dragged in is not
   an exceptional condition, it is Tuesday. */
function parseUserWorld(b) {
  if (!b || b.length < 32) return null;
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const ver = dv.getUint16(4, true);
  if (dv.getUint32(0, true) !== 0x574B594B || (ver !== 1 && ver !== 2)) return null;
  const n = b[6], k = b[7], count = b[8];
  if (n < 2 || n > 6 || k < 1 || k > 64 || count < 1 || count > 24) return null;
  /* The same checks the module makes, so the page refuses a file for the same
     reasons rather than sending one the module will turn away. */
  const fx = [];
  if (ver === 1) { if (b[14] || b[15]) return null; }
  else {
    for (let q = 0; q < 2; q++) {
      const kind = b[14 + q] & 0x0f, axis = b[14 + q] >> 4;
      if (!kind) { if (axis) return null; continue; }
      if (kind > 5 || axis >= n) return null;
      fx.push({ axis, kind });
    }
    if (!fx.length) return null;
    if (fx.length === 2 && fx[0].axis === fx[1].axis) return null;
  }
  /* exactly, not at least: a blob whose length disagrees with its own header is
     not a blob we understand, whichever way the disagreement runs */
  if (b.length !== 32 + count * 4 * (n + k)) return null;
  const sigma = dv.getFloat32(10, true);
  if (!(sigma >= 0.01 && sigma <= 4)) return null;
  const name = new TextDecoder().decode(b.subarray(16, 32)).replace(/\0.*$/, '');
  const nodes = [];
  let at = 32;
  for (let i = 0; i < count; i++) {
    const pos = [], mags = new Float32Array(k);
    for (let a = 0; a < n; a++) { pos.push(dv.getFloat32(at, true)); at += 4; }
    for (let h = 0; h < k; h++) { mags[h] = dv.getFloat32(at, true); at += 4; }
    if (pos.some(v => !Number.isFinite(v)) || mags.some(v => !Number.isFinite(v))) return null;
    nodes.push({ pos, mags });
  }
  return { n, k, count, sigma, name, sinePhase: (b[9] & 1) === 1, fx, nodes };
}

/* Store a world in one of the module's slots — the same chunking as putWorld,
   with the slot in front. The library is what makes a world you made reachable
   again: until slots existed, a sent world replaced whatever was playing and
   could never be one end of a morph. */
async function putSlot(link, slot, blob, onProgress) {
  const max = Math.max(64, (link.maxBody || 1024) - 32);
  for (let off = 0; off < blob.length; off += max) {
    const len = Math.min(max, blob.length - off);
    const req = new Uint8Array(9 + len), dv = new DataView(req.buffer);
    req[0] = slot & 0xff;
    dv.setUint32(1, blob.length, true);
    dv.setUint32(5, off, true);
    req.set(blob.subarray(off, off + len), 9);
    const r = await link.request(CMD.putSlot, req, { urgent: true });
    if (r[0] !== 0) throw new Error('the module refused slot ' + slot + ' at offset ' + off + ' (status ' + r[0] + ')');
    if (onProgress) onProgress(Math.min(blob.length, off + len), blob.length);
  }
  return true;
}

/* What the module is holding, which of them is playing, and which one it is
   morphing towards. An empty slot has a null name. */
/* 0x6F RESONATE: what the resonate world is playing — its axis, where the
   voice was built on it, the modes it was built from, the burst, and the
   world's points. The page never has the world; the module says. Null when
   what is playing is not a resonator. */
async function fetchResonate(link) {
  let b;
  try { b = await link.request(CMD.resonate, new Uint8Array(0), { key: 'resonate' }); }
  catch (e) { if (/BAD_STATE/.test(e.message)) return null; throw e; }
  if (!b.length || b[0] !== 0) return null;
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const out = { kind: b[1], lo: dv.getFloat32(2, true), hi: dv.getFloat32(6, true), param: dv.getFloat32(10, true),
                P: dv.getUint16(14, true), modes: [], burst: 0, points: [] };
  const N = b[16]; let at = 17;
  for (let k = 0; k < N && at + 12 <= b.length; k++, at += 12)
    out.modes.push({ hz: dv.getFloat32(at, true), zeta: dv.getFloat32(at + 4, true), gain: dv.getFloat32(at + 8, true) });
  if (at + 4 <= b.length) { out.burst = dv.getUint32(at, true); at += 4; }
  if (at < b.length) { const M = b[at]; at += 1; for (let i = 0; i < M && at + 4 <= b.length; i++, at += 4) out.points.push(dv.getFloat32(at, true)); }
  /* a family: which instrument of how many, by name */
  out.members = []; out.member = 0; out.name = '';
  const txt = (o) => new TextDecoder().decode(b.subarray(o, o + 16)).replace(/\0.*$/, '');
  if (at + 18 <= b.length) {
    const fam = b[at], cur = b[at + 1]; at += 2;
    out.member = cur; out.name = txt(at); at += 16;
    for (let i = 0; i < fam && at + 16 <= b.length; i++, at += 16) out.members.push(txt(at));
  }
  /* the voice count and the pitch lock, from firmware that reports them;
     null from firmware that does not, which is not the same as 1 and off */
  out.voices = null; out.lock = null; out.form = null;
  if (at + 2 <= b.length) { out.voices = b[at]; out.lock = b[at + 1] !== 0; at += 2; }
  if (at < b.length) { out.form = b[at]; at += 1; }   /* 0 no pickup: axes 0 and 3 are position and brightness there */
  /* the family morph and the release (ms); null from firmware before them */
  out.morph = null; out.release = null;
  if (at + 3 <= b.length) { out.morph = b[at] !== 0; out.release = dv.getUint16(at + 1, true); at += 3; }
  return out;
}

async function fetchSlots(link) {
  const b = await link.request(CMD.slots, new Uint8Array(0), { key: 'slots' });
  if (!b.length || b[0] !== 0) return null;
  const count = b[1] || 0;
  const out = { count, live: b[2], target: b[3], names: new Array(count).fill(null) };
  let at = 4;
  while (at + 1 < b.length) {
    const i = b[at], n = b[at + 1];
    if (at + 2 + n > b.length) break;
    if (i < count) out.names[i] = new TextDecoder().decode(b.subarray(at + 2, at + 2 + n));
    at += 2 + n;
  }
  return out;
}

/* Send one, chunked. Offsets go in order from zero because the module refuses
   anything else — random access would have it index a buffer with numbers the
   host chose. Nothing loads until the last byte lands, so an interrupted send
   costs the transfer and not the sound that is playing. */
async function putWorld(link, blob, onProgress) {
  const max = Math.max(64, (link.maxBody || 1024) - 24);
  for (let off = 0; off < blob.length; off += max) {
    const len = Math.min(max, blob.length - off);
    const req = new Uint8Array(8 + len), dv = new DataView(req.buffer);
    dv.setUint32(0, blob.length, true);
    dv.setUint32(4, off, true);
    req.set(blob.subarray(off, off + len), 8);
    const r = await link.request(CMD.putWorld, req, { urgent: true });
    if (r[0] !== 0) throw new Error('module refused the world at offset ' + off + ' (status ' + r[0] + ')');
    if (onProgress) onProgress(Math.min(blob.length, off + len), blob.length);
  }
  return true;
}

/* 0x60 GET_TELEMETRY body after status (core/kyk_telemetry.h):
 *   u32 block · f32 f0 · u8 n k kcut p planes flags stereo spreadPlane · f32 spread
 *   f32 ctl[n] centre[n] posL[n] posR[n] angle[planes] payload[p]
 *   [flags&1] u8 mags[k] (0 = ≤ −96 dB, 255 = +6 dB) · [flags&2] i8 frame[256] (×40)
 *   [flags&4] u8 kep_running · u8 kep_plane · f32 kep_x · f32 kep_y · f32
 *   kep_rush · f32 couple · f32 lock · u8 sharp — how the position is moving
 *   on its own, plus the Morph knob, which the page needs because it evaluates
 *   several worlds itself and `sharp` changes what they evaluate to.
 *   Appended last, so a host that predates it just ignores the tail. */
function parseTelemetry(b) {
  if (b.length < 21) return null;
  const t = { block: u32(b, 1), f0: f32(b, 5), n: b[9], k: b[10], kcut: b[11], p: b[12], planes: b[13], flags: b[14], stereo: b[15] !== 0, spreadPlane: b[16], spread: f32(b, 17) };
  let at = 21;
  const arr = n => { const a = new Float32Array(n); for (let i = 0; i < n; i++) { a[i] = f32(b, at); at += 4; } return a; };
  t.ctl = arr(t.n); t.centre = arr(t.n); t.posL = arr(t.n); t.posR = arr(t.n); t.angle = arr(t.planes); t.payload = arr(t.p);
  if (t.flags & TEL.spectrum) { t.mags = b.slice(at, at + t.k); at += t.k; }
  if (t.flags & TEL.frame) { t.frame = new Int8Array(256); for (let i = 0; i < 256; i++) t.frame[i] = i8(b[at + i]); at += 256; }
  if (t.flags & TEL.motion) {
    t.kepler = { running: b[at] !== 0, plane: b[at + 1], x: f32(b, at + 2), y: f32(b, at + 6), rush: f32(b, at + 10) };
    t.couple = f32(b, at + 14); t.lock = f32(b, at + 18);
    t.sharp = b.length > at + 22 ? b[at + 22] / 255 : 0;
    at += 23;
    /* The company. Body 0 is t.kepler.x/y; these are the perturbers, and only
       as many as are running are sent, so a single body costs one byte. */
    t.bodies = b.length > at ? Math.max(1, b[at]) : 1;
    at += b.length > at ? 1 : 0;
    t.kepXY = [];
    for (let i = 1; i < t.bodies && b.length >= at + 8; i++) {
      t.kepXY.push([f32(b, at), f32(b, at + 4)]); at += 8;
    }
    /* which pager page the panel is showing, so the mirror knows which six
       knobs are under the player's hands right now */
    t.page = b.length > at ? b[at] : 0;
    at += b.length > at ? 1 : 0;
    /* the Morph knob and what it is blending towards (0xFF: nothing) */
    t.morph = b.length > at ? b[at] / 255 : 0;
    t.morphWorld = b.length > at + 1 ? b[at + 1] : 0xFF;
    at += b.length > at + 1 ? 2 : 0;
    /* which motions are switched off; bit 15 is Kepler */
    t.mute = b.length > at + 1 ? u16(b, at) : 0;
    at += b.length > at + 1 ? 2 : 0;
    t.aimed = b.length > at ? b[at] !== 0 : false;
    at += b.length > at ? 1 : 0;
    /* where the live page's six knobs are sitting */
    if (b.length >= at + 6) { t.pots = []; for (let i = 0; i < 6; i++) t.pots.push(b[at + i] / 255); at += 6; }
    else t.pots = null;
    /* Which world is live, 0xFF for a user world. Null from firmware that
       predates it, which is not the same as "no world" and must not be read as
       one — a host that treats absent as 0xFF would believe every older module
       was playing a user world. */
    /* What the live page's six knobs are worth, which is not where the pots
       are sitting: the panel catches, so a page you have just arrived on shows
       a parameter holding its value while the pot is somewhere else entirely.
       Null when the module has no pager to ask — a shell reporting six zeroes
       and six knobs that really are at zero are not the same claim, so the
       validity byte is what separates them. */
    if (b.length > at) {
      const valid = b[at] !== 0;
      t.knobs = valid && b.length >= at + 7 ? Array.from(b.subarray(at + 1, at + 7), v => v / 255) : null;
      at += b.length >= at + 7 ? 7 : 1;
    } else t.knobs = null;
    t.world = b.length > at ? b[at] : null;
    at += b.length > at ? 1 : 0;
    /* Where the world tour has got to: stops, which one is live, how far towards
       the next. Null from firmware that predates it, and zero stops means there
       is no tour — a page that drew the loop from its own memory of what it
       programmed would be drawing the one thing on screen the clock has since
       moved on from. */
    t.tour = b.length >= at + 3 ? { len: b[at], at: b[at + 1], blend: b[at + 2] / 255 } : null;
    at += b.length >= at + 3 ? 3 : 0;
    /* what kind of world is live (World::Kind; 11 a resonator). Null from
       firmware that predates it; a resonator then shows as the 4-axis user
       world it also is */
    t.kind = b.length > at ? b[at] : null;
    at += b.length > at ? 1 : 0;
  }
  t.resonate = t.kind === 11;
  t.bytes = b.length;
  return t;
}
function telemetryReq(flags = TEL.spectrum | TEL.frame) { return Uint8Array.of(flags & 0xff); }
/* The top of the scale is +6 dB, not 0: the engine normalises to a sum of
 * squares of 2, so one partial can legitimately sit above unity and a
 * peaky world's tallest one does. See MagToU8 in core/kyk_telemetry.h. */
const magDb = v => v === 0 ? -96 : (v - 255) * 102 / 255 + 6;

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
  const s = { cyclesLast: u32(b, 1), cyclesMax: u32(b, 5), cyclesAvg: u32(b, 9), overruns: u16(b, 13), dropped: u16(b, 15), renderDiv: b[17], cyclesBudget: u32(b, 18) };
  /* the engine's own peak and the voices built a second, from firmware that says (null before it) */
  s.engineMax = b.length >= 30 ? u32(b, 22) : null; s.atPerS = b.length >= 30 ? u32(b, 26) : null;
  /* and the costliest strike in the window (the voice's rebuild with it), from firmware that says */
  s.strikeMax = b.length >= 34 ? u32(b, 30) : null;
  /* and the strikes a second that took a voice built before them (the staged strike) */
  s.stagedPerS = b.length >= 38 ? u32(b, 34) : null;
  /* and the blocks a second the load governor hurried the tails: no overrun,
     but a tail cut to 2 ms can pop */
  s.hurriedPerS = b.length >= 42 ? u32(b, 38) : null;
  return s;
}
/* 0x64 ACTION */
function actionReq(op, args = []) { return Uint8Array.of(op & 0xff, ...args); }

/* 0x65 GET_WORLDS: u8 count, u8 current, then per world u8 kind, str name,
 * str note. kind 2 is analytic — a formula the host can evaluate itself —
 * and kind 1 is tabulated, a lattice with no closed form (core/kyk_world.h). */
/* Paged since ext 4: the list outgrew a 1024-byte body at eighteen worlds and
 * the reply was silently never sent. Reply is total, current, start, sent,
 * then `sent` entries — use fetchWorlds, which walks the pages. */
function parseWorlds(b) {
  if (b.length < 5) return null;
  const count = b[1], current = b[2], start = b[3], sent = b[4];
  let at = 5; const list = [];
  for (let i = 0; i < sent && at < b.length; i++) {
    const kind = b[at++]; let name, note;
    [name, at] = readStr(b, at); [note, at] = readStr(b, at);
    /* `analytic` here means the page can evaluate it: a world with a
     * formula small enough to hold, whichever formula it is. */
    list.push({ index: start + i, kind, analytic: kind === WORLD_KIND.analytic || kind === WORLD_KIND.fm || kind === WORLD_KIND.formant
                 || kind === WORLD_KIND.table || kind === WORLD_KIND.lock
                 || kind === WORLD_KIND.unison || kind === WORLD_KIND.modal || kind === WORLD_KIND.bend, name, note });
  }
  return { current, count, start, sent, list };
}
/* 0x66 GET_BASIS: u8 world, u32 offset, u16 max → u32 total, u32 offset,
 * u16 n, bytes. Status 1 means the world is tabulated and has no formula. */
/* Walk every page of the world list. */
async function fetchWorlds(link) {
  let at = 0, current = 0, count = 0; const all = [];
  for (let guard = 0; guard < 64; guard++) {
    const r = parseWorlds(await link.request(CMD.worlds, Uint8Array.of(at)));
    if (!r || !r.list.length) break;
    current = r.current; count = r.count;
    for (const w of r.list) all.push(w);
    at = r.start + r.sent;
    if (at >= r.count) break;
  }
  return { current, count, list: all };
}
function basisReq(world, offset, max) {
  const r = new Uint8Array(7), dv = new DataView(r.buffer);
  r[0] = world & 0xff; dv.setUint32(1, offset >>> 0, true); dv.setUint16(5, max, true);
  return r;
}
/* The assembled blob: u8 n, u8 k, f32 extent, f32 floor, f32 mean[k],
 * f32 comp[n][k] row-major. About 1.3 KB for a whole world, which is the
 * point — the page can hold it and evaluate the space anywhere. */
function parseBasis(all) {
  if (!all || all.length < 10) return null;
  /* 0xFF where a dimension count should be marks a different shape of
   * formula. Today that is only FM, whose whole world is seven numbers. */
  if (all[0] === 0xff) {
    const d = new DataView(all.buffer, all.byteOffset, all.byteLength);
    if (all[1] === WORLD_KIND.fm && all.length >= 32) return {
      fm: true, n: all[2], k: all[3],
      indexMax: d.getFloat32(4, true), ratioMin: d.getFloat32(8, true), ratioMax: d.getFloat32(12, true),
      carrierMin: d.getFloat32(16, true), carrierMax: d.getFloat32(20, true),
      secondMax: d.getFloat32(24, true), ratioLock: d.getFloat32(28, true),
    };
    if (all[1] === WORLD_KIND.bend && all.length >= 8) return {
      bend: true, n: all[2], k: all[3], base: all[4],
    };
    if (all[1] === WORLD_KIND.modal && all.length >= 24) return {
      modal: true, n: all[2], k: all[3],
      body: Math.round(d.getFloat32(4, true)),
      geomMin: d.getFloat32(8, true), geomMax: d.getFloat32(12, true),
      widthMin: d.getFloat32(16, true), widthMax: d.getFloat32(20, true),
    };
    if (all[1] === WORLD_KIND.lock && all.length >= 12) return {
      lock: true, n: all[2], k: all[3], count: all[4], sigma: d.getFloat32(8, true),
    };
    if (all[1] === WORLD_KIND.unison && all.length >= 24) return {
      unison: true, n: all[2], k: all[3],
      voicesMax: d.getFloat32(4, true), spanMin: d.getFloat32(8, true),
      spanMax: d.getFloat32(12, true), detuneMax: d.getFloat32(16, true),
      tilt: d.getFloat32(20, true),
    };
    if (all[1] === WORLD_KIND.table && all.length >= 32) return {
      table: true, n: all[2], k: all[3], cols: all[4], rows: all[5],
      axis2: all[6], axis3: all[7], pulseMin: d.getFloat32(8, true),
    };
    if (all[1] === WORLD_KIND.formant && all.length >= 44) return {
      formant: true, n: all[2], k: all[3],
      f1Min: d.getFloat32(4, true), f1Max: d.getFloat32(8, true),
      r2Min: d.getFloat32(12, true), r2Max: d.getFloat32(16, true),
      r3Min: d.getFloat32(20, true), r3Max: d.getFloat32(24, true),
      qMin: d.getFloat32(28, true), qMax: d.getFloat32(32, true),
      amp2: d.getFloat32(36, true), amp3: d.getFloat32(40, true),
    };
    return null;
  }
  const n = all[0], k = all[1];
  if (!(n > 0 && k > 0) || all.length < 10 + 4 * k * (n + 1)) return null;
  const dv = new DataView(all.buffer, all.byteOffset, all.byteLength);
  const extent = dv.getFloat32(2, true), floor = dv.getFloat32(6, true);
  const mean = new Float32Array(k), comp = new Float32Array(n * k);
  let at = 10;
  for (let i = 0; i < k; i++) { mean[i] = dv.getFloat32(at, true); at += 4; }
  for (let i = 0; i < n * k; i++) { comp[i] = dv.getFloat32(at, true); at += 4; }
  return { n, k, extent, floor, mean, comp };
}
async function fetchBasis(link, world, maxBody = 512) {
  const chunk = Math.max(64, Math.min(900, (maxBody || 512) - 16));
  const parts = []; let off = 0, total = 0;
  for (let guard = 0; guard < 256; guard++) {
    const b = await link.request(CMD.basis, basisReq(world, off, chunk), { urgent: true });
    total = u32(b, 1); const n = u16(b, 9);
    if (!n) break;
    parts.push(b.slice(11, 11 + n)); off += n;
    if (off >= total) break;
  }
  return parseBasis(concat(parts));
}

/* ── FM, evaluated here ───────────────────────────────────────────────
 * A port of core/kyk_fm.h, close enough that the terrain the page draws is
 * the space the module plays. Miller's downward recurrence for the Bessel
 * amplitudes, then each sideband's amplitude split between the two harmonic
 * bins it falls between, signed so reflected partials cancel where they
 * should. See the C++ for why each of those is the way it is. */
const FM_ORDERS = 48;
function besselJ(x, nmax, J) {
  for (let n = 0; n <= nmax; n++) J[n] = 0;
  if (x < 1e-5) { J[0] = 1; return J; }
  const M = nmax + 20 + Math.floor(4 * Math.sqrt(x)), inx = 2 / x;
  let bjp = 0, bj = 1e-20, norm = 0;
  for (let n = M; n >= 1; n--) {
    const bjm = inx * n * bj - bjp;
    bjp = bj; bj = bjm;
    if (Math.abs(bj) > 1e10) {
      bj *= 1e-10; bjp *= 1e-10; norm *= 1e-10;
      for (let i = 0; i <= nmax; i++) J[i] *= 1e-10;
    }
    const m = n - 1;
    if (m <= nmax) J[m] = bj;
    if ((m & 1) === 0) norm += m === 0 ? bj : 2 * bj;
  }
  const inv = norm !== 0 ? 1 / norm : 0;
  for (let n = 0; n <= nmax; n++) J[n] *= inv;
  return J;
}
const fmJ = new Float64Array(FM_ORDERS + 1);
/* p01[a] in [0,1] — the folded coordinate, the same input the engine gets. */
function evalFm(basis, p01, out) {
  const k = basis.k, m = out && out.length >= k ? out : new Float32Array(k);
  for (let i = 0; i < k; i++) m[i] = 0;
  const index = p01[0] * basis.indexMax;
  let ratio = basis.ratioMin + p01[1] * (basis.ratioMax - basis.ratioMin);
  if (basis.ratioLock > 0) ratio -= basis.ratioLock * (1 / (2 * Math.PI)) * Math.sin(2 * Math.PI * ratio);
  const carrier = basis.carrierMin + p01[2] * (basis.carrierMax - basis.carrierMin);
  const second = basis.n > 3 ? p01[3] * basis.secondMax : 0;
  besselJ(index, FM_ORDERS, fmJ);
  for (let c = 0; c < 2; c++) {
    const cc = c ? carrier + second : carrier;
    for (let ord = -FM_ORDERS; ord <= FM_ORDERS; ord++) {
      const a = ord < 0 ? -ord : ord;
      let amp = fmJ[a];
      if (ord < 0 && (a & 1)) amp = -amp;
      if (Math.abs(amp) < 1e-4) continue;
      let f = cc + ord * ratio;
      if (f < 0) { f = -f; amp = -amp; }
      const h = f - 1, i0 = Math.floor(h), fr = h - i0;
      if (i0 >= 0 && i0 < k) m[i0] += amp * (1 - fr);
      if (i0 + 1 >= 0 && i0 + 1 < k) m[i0 + 1] += amp * fr;
    }
  }
  for (let i = 0; i < k; i++) if (m[i] < 0) m[i] = -m[i];
  return m;
}

/* The 2-D table of real waveforms — a port of core/kyk_shapes.h. Only the
 * spectrum half: the shaper axes act on the rendered cycle, not on the
 * harmonics, so the terrain this draws is the table on axes 0 and 1 and says
 * nothing about axes 2 and 3. That is the honest picture of the world rather
 * than a limitation of the drawing. */
function shapeEnd(b, col, end, out) {
  const k = b.k;
  for (let i = 0; i < k; i++) out[i] = 0;
  if (col === 0) {
    const n = end ? 8 : 1;
    for (let h = 1; h <= n && h <= k; h++) out[h - 1] = 1 / h;
  } else if (col === 1) {
    for (let h = 1; h <= k; h += 2) {
      const sg = end ? 1 : ((((h - 1) / 2) & 1) ? -1 : 1);
      out[h - 1] = sg * Math.pow(h, -2);
    }
  } else if (col === 2) {
    for (let h = 1; h <= k; h++) {
      let v = 1 / h;
      if (end) { const d = (Math.log(h) - Math.log(11)) / 0.45; v *= 1 + 5.5 * Math.exp(-0.5 * d * d); }
      out[h - 1] = v;
    }
  } else {
    const d = end ? b.pulseMin : 0.5;
    for (let h = 1; h <= k; h++) out[h - 1] = 2 * (1 - Math.cos(2 * Math.PI * h * d)) / (Math.PI * h);
  }
  return out;
}
const shpA = new Float32Array(256), shpB = new Float32Array(256);
function shapeNode(b, col, row, out) {
  const k = b.k, t = b.rows > 1 ? row / (b.rows - 1) : 0;
  shapeEnd(b, col, 0, out);
  if (t > 0) { shapeEnd(b, col, 1, shpB); for (let i = 0; i < k; i++) out[i] += t * (shpB[i] - out[i]); }
  let e = 0; for (let i = 0; i < k; i++) e += out[i] * out[i];
  const g = e > 0 ? Math.SQRT2 / Math.sqrt(e) : 0;
  for (let i = 0; i < k; i++) out[i] *= g;
  return out;
}
function snapTo(u, sharp) {
  if (!(sharp > 0)) return u;
  if (u <= 0) return 0; if (u >= 1) return 1;
  const a = 1 + 7 * sharp * sharp;
  const x = Math.pow(u, a), y = Math.pow(1 - u, a);
  return (x + y) > 0 ? x / (x + y) : u;
}
function evalShapes(basis, p01, out, sharp = 0) {
  const k = basis.k, C = basis.cols, R = basis.rows;
  const m = out && out.length >= k ? out : new Float32Array(k);
  const gx = p01[0] * (C - 1), gy = p01[1] * (R - 1);
  let c0 = Math.min(C - 2, Math.max(0, Math.floor(gx)));
  let r0 = Math.min(R - 2, Math.max(0, Math.floor(gy)));
  const u = snapTo(gx - c0, sharp), v = snapTo(gy - r0, sharp);
  const w = [(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v];
  const cc = [c0, c0 + 1, c0, c0 + 1], rr = [r0, r0, r0 + 1, r0 + 1];
  for (let i = 0; i < k; i++) m[i] = 0;
  for (let q = 0; q < 4; q++) {
    if (!(w[q] > 0)) continue;
    shapeNode(basis, cc[q], rr[q], shpA);
    for (let i = 0; i < k; i++) m[i] += w[q] * shpA[i];
  }
  let acc = 0; for (let i = 0; i < k; i++) acc += m[i] * m[i];
  const g = acc > 0 ? Math.SQRT2 / Math.sqrt(acc) : 0;
  for (let i = 0; i < k; i++) m[i] *= g;
  return m;
}

/* One waveform bent four ways — a port of core/kyk_bend.h. Base 0 is a saw,
 * 1 a pulse, 2 the saw-against-pulse blend. */
const invH = (h) => 1 / h;
function bendRoll(h, tilt) {
  const bright = tilt >= 0.5, u = bright ? tilt * 2 - 1 : tilt * 2;
  const ih = Math.pow(h, -1);
  const lo = bright ? ih : Math.pow(h, -1.6);
  const hi = bright ? Math.pow(h, -0.75) : ih;
  return lo + u * (hi - lo);
}
function evalBend(basis, p01, out) {
  const k = basis.k, n = basis.n;
  const m = out && out.length >= k ? out : new Float32Array(k);
  const a0 = p01[0], a1 = p01[1];
  const a2 = n > 2 ? p01[2] : 0.5, a3 = n > 3 ? p01[3] : 1;
  let duty = 0.5, blend = 0, tilt = 0.5, parity = 0;
  if (basis.base === 0) { tilt = a0; parity = a1; }
  else if (basis.base === 1) { duty = 0.5 - 0.22 * a0; tilt = a1; }
  else { blend = a0; duty = 0.5 - 0.26 * a1; tilt = 0.5; }
  const P = 4 + 6 * (1 - a2);
  const foldAt = 2 + a3 * (k - 3);
  const invPi = 1 / Math.PI;
  for (let h = 1; h <= k; h++) {
    const roll = bendRoll(h, tilt);
    let c;
    if (basis.base === 0) c = roll;
    else {
      const pw = 2 * (1 - Math.cos(2 * Math.PI * h * duty)) * invPi * roll;
      c = basis.base === 1 ? pw : roll + blend * (pw - roll);
    }
    if (parity > 0 && (h % 2) === 0) c *= 1 - 2 * parity;
    c *= 1 - 0.55 * Math.cos(2 * Math.PI * h / P);
    /* the fold point: +1 below, -1 above, smoothstepped across seven harmonics */
    let u = (h - foldAt) / 7, sg;
    if (u <= 0) sg = 1; else if (u >= 1) sg = -1;
    else sg = 1 - 2 * (u * u * (3 - 2 * u));
    m[h - 1] = c * sg;
  }
  return m;
}

/* Struck objects — a port of core/kyk_modal.h. Body 0 is a plate, whose
 * fourth axis is a second strike coordinate; 1 and 2 are a bar and a drum
 * head, whose fourth axis is temper. */
const BAR_R = [1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.80, 31.80, 39.80, 48.70, 58.50];
const DRUM_R = [1.000, 1.593, 2.135, 2.295, 2.653, 2.917, 3.155, 3.500,
                3.598, 3.647, 4.060, 4.154, 4.230, 4.601, 4.832, 4.903,
                5.412, 5.428, 5.579, 5.651, 5.976, 6.130, 6.156, 6.442,
                6.575, 6.729, 7.015, 7.144];
function msinc(x) {
  const a = Math.abs(x);
  return a < 1e-4 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x);
}
/* Per-mode damping, relative to the fundamental. Mirrors ModalDamp in
 * core/kyk_modal.h: a crossfade between sqrt(f), f and f² rather than a
 * power, and a rational decay rather than an exponential. */
function mdamp(t, p, m, n) {
  if (!(t > 0)) return 1;
  const f2 = (m * m + n * n) * 0.5, f = Math.sqrt(f2);
  const sq = Math.sqrt(f);
  const pw = p < 0.5 ? sq + (p * 2) * (f - sq) : f + (p * 2 - 1) * (f2 - f);
  const shape = 1 + 0.5 * (m / n + n / m - 2);
  const a = 0.9 * (pw * shape - 1);
  return a <= 0 ? 1 : 1 / (1 + a * t);
}
const mq = (i) => Math.pow(i, -0.3);
function mdeposit(m, fh, a, k) {
  if (a === 0 || fh <= 0 || fh > k + 1) return;
  const h = fh - 1, i0 = Math.floor(h), fr = h - i0;
  if (i0 >= 0 && i0 < k) m[i0] += a * (1 - fr);
  if (i0 + 1 >= 0 && i0 + 1 < k) m[i0 + 1] += a * fr;
}
function evalModal(basis, p01, out) {
  const k = basis.k, n = basis.n;
  const m = out && out.length >= k ? out : new Float32Array(k);
  for (let i = 0; i < k; i++) m[i] = 0;
  const x = 0.04 + 0.42 * p01[0];
  const w = 0.03;
  const g = basis.geomMin + (basis.geomMax - basis.geomMin) * p01[1];
  const tt = n > 2 ? p01[2] : 0, t = tt * tt * 4;
  const p = n > 3 ? p01[3] : 0.5;
  if (basis.body === 0) {
    const norm = 1 / Math.sqrt(g * g + 1 / (g * g));
    for (let a = 1; a <= 8; a++) {
      const hit = Math.abs(Math.sin(Math.PI * a * x)) * msinc(a * w);
      if (hit < 1e-4) continue;
      for (let b = 1; b <= 8; b++) {
        const hit2 = hit * mdamp(t, p, a, b);
        if (hit2 < 1e-5) continue;
        const mm = a * g, nn = b / g;
        mdeposit(m, Math.sqrt(mm * mm + nn * nn) * norm, hit2 * mq(a * b), k);
      }
    }
    return m;
  }
  const r = basis.body === 1 ? BAR_R : DRUM_R;
  for (let i = 0; i < r.length; i++) {
    const b = i + 1;
    const hit = Math.abs(Math.sin(Math.PI * b * x)) * msinc(b * w) * mdamp(t, p, b, 1);
    if (hit < 1e-5) continue;
    mdeposit(m, 1 + (r[i] - 1) * g, hit * mq(b), k);
  }
  return m;
}

/* Real waveforms on the 24-cell — a port of core/kyk_lock.h. One family per
 * Givens plane, in the order the polytope is walked. */
function lockWave(k, v, out) {
  for (let i = 0; i < k; i++) out[i] = 0;
  const g = Math.floor(v / 4) % 6, m = v % 4;
  if (g === 0) {
    const duty = [0.5, 0.35, 0.22, 0.12][m];
    for (let h = 1; h <= k; h++) out[h - 1] = 2 * (1 - Math.cos(2 * Math.PI * h * duty)) / (Math.PI * h);
  } else if (g === 1) {
    const p = [1.0, 0.82, 0.70, 1.25][m];
    for (let h = 1; h <= k; h++) out[h - 1] = Math.pow(h, -p);
  } else if (g === 2) {
    const p = [2.0, 1.75, 1.5, 2.4][m];
    for (let h = 1; h <= k; h += 2) out[h - 1] = ((((h - 1) / 2) & 1) ? -1 : 1) * Math.pow(h, -p);
  } else if (g === 3 || g === 4) {
    const n = g === 3 ? [1, 2, 3, 4][m] : [6, 8, 12, 16][m];
    for (let h = 1; h <= n && h <= k; h++) out[h - 1] = 1 / h;
  } else {
    const st = [2, 3, 4, 5][m];
    for (let h = st; h <= k; h += st) out[h - 1] = 1 / h;
  }
  let e = 0; for (let i = 0; i < k; i++) e += out[i] * out[i];
  const gg = e > 0 ? Math.SQRT2 / Math.sqrt(e) : 0;
  for (let i = 0; i < k; i++) out[i] *= gg;
  return out;
}
let lockCache = null;
function lockNodes(b) {
  if (lockCache && lockCache.k === b.k) return lockCache;
  const pos = [], spec = [], r = 0.70710678;
  let c = 0;
  for (let i = 0; i < 4; i++) for (let j = i + 1; j < 4; j++)
    for (let si = 0; si < 2; si++) for (let sj = 0; sj < 2; sj++) {
      const v = [0, 0, 0, 0];
      v[i] = si ? r : -r; v[j] = sj ? r : -r;
      pos.push(v.map(x => 0.5 + 0.42 * x));
      spec.push(lockWave(b.k, c, new Float32Array(b.k)));
      c++;
    }
  lockCache = { k: b.k, pos, spec, count: c };
  return lockCache;
}
function evalLock(basis, p01, out, sharp = 0) {
  const k = basis.k, n = basis.n, t = lockNodes(basis);
  const m = out && out.length >= k ? out : new Float32Array(k);
  const sigma = basis.sigma * (1 - 0.7 * Math.max(0, Math.min(1, sharp)));
  const inv2s2 = 1 / (2 * sigma * sigma);
  const w = new Float64Array(t.count);
  let best = -1e30;
  for (let v = 0; v < t.count; v++) {
    let d2 = 0;
    for (let a = 0; a < n; a++) { const dd = p01[a] - t.pos[v][a]; d2 += dd * dd; }
    w[v] = -d2 * inv2s2; if (w[v] > best) best = w[v];
  }
  let sum = 0;
  for (let v = 0; v < t.count; v++) { w[v] = Math.exp(w[v] - best); sum += w[v]; }
  const inv = sum > 0 ? 1 / sum : 0;
  for (let i = 0; i < k; i++) m[i] = 0;
  for (let v = 0; v < t.count; v++) {
    const wv = w[v] * inv; if (wv < 1e-4) continue;
    const sv = t.spec[v];
    for (let i = 0; i < k; i++) m[i] += wv * sv[i];
  }
  return m;
}

/* One wave stacked on itself — a port of core/kyk_unison.h. */
function evalUnison(basis, p01, out) {
  const k = basis.k, n = basis.n;
  const m = out && out.length >= k ? out : new Float32Array(k);
  for (let i = 0; i < k; i++) m[i] = 0;
  const voices = 1 + p01[0] * (basis.voicesMax - 1);
  const span = basis.spanMin + p01[1] * (basis.spanMax - basis.spanMin);
  const detune = n > 2 ? p01[2] * basis.detuneMax : 0;
  const wave = n > 3 ? p01[3] : 0;
  const whole = Math.floor(voices), frac = voices - whole, invPi = 1 / Math.PI;
  let gain = 1;
  for (let j = 0; j <= whole && j < 16; j++) {
    let g = gain;
    if (j === whole) { if (!(frac > 0)) break; g *= frac; }
    const root = 1 + j * span + j * detune;
    if (root > k) break;
    for (let q = 1; q <= k; q++) {
      const fh = root * q;
      if (fh > k) break;
      const im = 1 / q;
      let bm;
      if (wave <= 0.5) { const sq = (q & 1) ? 4 * invPi * im : 0; bm = im + 2 * wave * (sq - im); }
      else { const d = 0.5 - (2 * wave - 1) * 0.38; bm = 2 * (1 - Math.cos(2 * Math.PI * q * d)) * invPi * im; }
      if (bm === 0) continue;
      const a = g * bm, h = fh - 1;
      const i0 = Math.floor(h), fr = h - i0;
      if (i0 >= 0 && i0 < k) m[i0] += a * (1 - fr);
      if (i0 + 1 >= 0 && i0 + 1 < k) m[i0 + 1] += a * fr;
    }
    gain *= basis.tilt;
  }
  return m;
}

/* Three resonances over a falling source — a port of core/kyk_formant.h, the
 * same shape of thing as evalFm and for the same reason. */
function evalFormant(basis, p01, out) {
  const k = basis.k, m = out && out.length >= k ? out : new Float32Array(k);
  const f1 = basis.f1Min + p01[0] * (basis.f1Max - basis.f1Min);
  const r2 = basis.r2Min + p01[1] * (basis.r2Max - basis.r2Min);
  const r3 = basis.r3Min + p01[2] * (basis.r3Max - basis.r3Min);
  const q = basis.n > 3 ? basis.qMax - p01[3] * (basis.qMax - basis.qMin) : 0.35;
  const l1 = Math.log(Math.max(1e-3, f1)), l2 = Math.log(Math.max(1e-3, f1 * r2));
  const l3 = Math.log(Math.max(1e-3, f1 * r2 * r3));
  const w = q * Math.LN2, iw = 1 / (2 * w * w);
  for (let i = 0; i < k; i++) {
    const h = i + 1, lh = Math.log(h);
    const d1 = lh - l1, d2 = lh - l2, d3 = lh - l3;
    const bump = Math.exp(-d1 * d1 * iw) + basis.amp2 * Math.exp(-d2 * d2 * iw)
               + basis.amp3 * Math.exp(-d3 * d3 * iw);
    m[i] = (1 / h) * (0.06 + bump);
  }
  return m;
}

/* Evaluate an analytic world at a coordinate: coord[a] in ±extent, out of
 * which comes an unnormalised magnitude spectrum. Every shading measure the
 * page draws is a ratio, so it is invariant to the missing normalisation. */
function evalBasis(basis, coord, out) {
  const { n, k, extent, floor, mean, comp } = basis;
  const m = out && out.length >= k ? out : new Float32Array(k);
  for (let i = 0; i < k; i++) m[i] = mean[i];
  for (let a = 0; a < n; a++) {
    const c = coord[a]; if (!c) continue;
    const base = a * k;
    for (let i = 0; i < k; i++) m[i] += c * comp[base + i];
  }
  for (let i = 0; i < k; i++) { const v = Math.exp(m[i]) - floor; m[i] = v > 0 ? v : 0; }
  void extent;
  return m;
}
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
/* 0x6D TOUR — the loop of worlds on a clock.
 *
 * `set` sends the whole sequence in one frame, because half a tour is a
 * different tour; a length below two turns it off. Entries name worlds the way
 * the rest of this wire does: an index below the world count is a built-in and
 * 0x80 | slot is one of yours. Every op replies with the state, so the page
 * never has to ask twice. */
function tourReq(op, div = 1, entries = []) {
  if (op !== TOUR.set) return Uint8Array.of(op);
  const n = Math.min(entries.length, TOUR.max);
  const r = new Uint8Array(3 + n);
  r[0] = TOUR.set; r[1] = Math.max(1, Math.min(64, div | 0)); r[2] = n;
  for (let i = 0; i < n; i++) r[3 + i] = entries[i] & 0xff;
  return r;
}
function parseTour(b) {
  if (!b || b.length < 5) return null;
  const len = b[1];
  if (b.length < 5 + len) return null;
  return { len, div: b[2], at: b[3], blend: b[4] / 255,
           entries: Array.from(b.subarray(5, 5 + len)) };
}

/* Lattice geometry shared with the page: plane index → axes, lexicographic */
function planeAxes(n, plane) { let p = 0; for (let a = 0; a < n; a++) for (let b = a + 1; b < n; b++) { if (p === plane) return [a, b]; p++; } return [0, 1]; }
const planeCount = n => n * (n - 1) / 2;

const api = {
  CMD, ACT, TEL, STATUS, PROTO, WORLD_KIND, crc32, cobsEncode, cobsDecode, buildFrame, FrameParser, Link,
  SerialTransport, WsTransport, StdioTransport, hello, getDescriptor,
  TOUR, tourReq, parseTour, fetchResonate,
  parseTelemetry, telemetryReq, magDb, evalFm, evalFormant, evalShapes, evalLock, evalUnison, evalModal, evalBend, parseSpaceInfo, cellReq, parseCell, parseStats, actionReq, setControlReq,
  parseWorlds, fetchWorlds, basisReq, parseBasis, fetchBasis, evalBasis, putWorld, putSlot, fetchSlots,
  MORPH_USER, SLOT_COUNT, STAT_CARD_EXISTS, SHAPER, SHAPER_NAME, buildUserWorld, parseUserWorld,
  saveCardWorld, fetchSlot, fetchCardWorlds, wavToCycle, cycleToNode,
  planeAxes, planeCount, noteName, statusName, u16, u32, f32,
};
if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.KYK = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

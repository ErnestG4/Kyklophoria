#!/usr/bin/env node
/* selftest.mjs — drive the desktop binary over stdio with web/link.js and
 * check the M1 protocol end to end; then the same HELLO through a bridge.
 *
 *   node web/selftest.mjs [--bin build/host/kykdesk] [--no-bridge]
 */
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const KYK = require('./link.js');
const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
let bin = path.join(repo, 'build', 'host', 'kykdesk'), doBridge = true;
for (let i = 2; i < process.argv.length; i++) {
  if (process.argv[i] === '--bin') bin = path.resolve(process.argv[++i]);
  else if (process.argv[i] === '--no-bridge') doBridge = false;
}

let checks = 0, failures = 0;
const check = (cond, what) => { checks++; if (!cond) { failures++; console.log('  FAIL ' + what); } };
const close = (a, b, tol = 1e-5) => Math.abs(a - b) <= tol;

/* ── codec vectors (the SDK's golden values, via Audiothurgist's selftest) ── */
{
  check(KYK.crc32(new TextEncoder().encode('123456789')) === 0xcbf43926, 'crc32 check value');
  const dec = KYK.cobsDecode(KYK.cobsEncode(Uint8Array.of(0, 1, 0, 2, 3, 0)));
  check(dec && dec.length === 6 && dec[0] === 0 && dec[3] === 2 && dec[5] === 0, 'cobs round trip');
  const f = KYK.buildFrame(0x01, 7, new Uint8Array(0));
  let got = null; const p = new KYK.FrameParser(); for (const b of f) p.push(b, x => got = x);
  check(got && got.ok && got.type === 1 && got.seq === 7 && got.body.length === 0, 'frame builder ↔ parser');

  /* A device that stops delimiting must not be able to exhaust the host.
     Undelimited input is a crash mid-transmit, a wrong baud rate or line
     noise; the answer to all three is to drop the chunk and resync, which is
     what the SDK's own parser does and what this one had been missing. */
  {
    const q = new KYK.FrameParser();
    for (let i = 0; i < (1 << 20); i++) q.push(0x41, () => {});
    check(q.acc.length < 4096, `a megabyte of undelimited input is capped (${q.acc.length} held)`);
    let after = null;
    q.push(0, x => after = x);                       /* the delimiter that resyncs */
    for (const b of KYK.buildFrame(0x01, 9, new Uint8Array(0))) q.push(b, x => after = x);
    check(after && after.ok && after.seq === 9, 'and a real frame still parses right after it');
  }
}

async function withChild(args, fn) {
  if (!fs.existsSync(bin)) { console.log('no binary at ' + bin + ' — build with `make host`'); process.exit(1); }
  const child = spawn(bin, args, { stdio: ['pipe', 'pipe', 'inherit'] });
  const link = new KYK.Link(new KYK.StdioTransport(child));
  await link.start();
  try { await fn(link); } finally { await link.close(); }
}

console.log('== stdio: ' + bin + ' --serve --gen --seed 1');
await withChild(['--serve', '--gen', '--seed', '1'], async link => {
  const info = await KYK.hello(link);
  console.log(`  hello: ${info.id} / ${info.name} fw ${info.fw} (${info.git}, sdk ${info.sdk}) maxBody ${info.maxBody} descriptor ${info.descriptorLen} B`);
  check(info.id === 'kyk', 'module id');
  check(info.maxBody === 1024, 'max_body 1024');
  check(info.descriptorLen > 0, 'descriptor advertised');
  const desc = await KYK.getDescriptor(link, info);
  check(desc && desc.kyk && desc.kyk.telemetry === 96, 'descriptor kyk block');
  check(Array.isArray(desc.iomap) && desc.iomap.length >= 6, 'descriptor iomap');
  console.log('  descriptor: dv ' + desc.dv + ', iomap ' + (desc.iomap || []).map(j => j.jack + '=' + j.id).join(' '));

  const sp = KYK.parseSpaceInfo(await link.request(KYK.CMD.spaceInfo));
  check(sp && sp.n === 4 && sp.k === 64 && sp.p === 8 && sp.side === 4 && sp.pointCount === 256, 'space info');
  console.log(`  space: ${sp.name} N=${sp.n} side=${sp.side} K=${sp.k} P=${sp.p} topo ${sp.topo.join(',')} crc ${sp.crc.toString(16)}`);

  const t = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(3)));
  check(t && t.n === 4 && t.k === 64 && t.p === 8 && t.planes === 6, 'telemetry header');
  check(t && t.bytes === 461, 'telemetry body 460 B + status (got ' + (t && t.bytes) + ')');
  const tk = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(7)));
  /* Spelled out rather than as a magic number, because this has now gone
     stale four times as the block grew. 1 status + 460 fixed + 23 motion +
     1 body count + 8 per extra body + 1 pager page + 2 morph + 2 mute +
     1 aimed + 6 pots + 1 flag and 6 knob values + 1 live world. */
  {
    const want = 1 + 460 + 23 + 1 + 8 * ((tk ? tk.bodies : 1) - 1) + 1 + 2 + 2 + 1 + 6 + 7 + 1;
    check(tk && tk.bytes === want,
          `motion block totals ${want} B (got ${tk && tk.bytes}, ${tk && tk.bodies} bodies)`);
  }
  check(tk && tk.kepler && tk.kepler.plane < 6 && tk.kepler.rush >= 0 && tk.lock >= 0
        && tk.sharp >= 0 && tk.sharp <= 1, 'motion fields parse, sharp included');
  /* The live world, in every frame. A host that has to ask for the list to
     learn this only learns it when it thinks to ask, which is never. */
  check(tk && tk.world != null, 'telemetry says which world is playing');
  /* The desktop shell has no pager, so it must say so rather than claim six
     knobs are at zero — which is what a bare six bytes would have said. */
  check(tk && tk.knobs === null, 'and reports no knob values, having no pager to ask');
  check(t && t.mags && t.mags.length === 64 && t.frame && t.frame.length === 256, 'spectrum + frame present');
  check(t && t.kcut >= 1 && t.kcut <= 64, 'kcut range');
  const finite = a => Array.from(a).every(Number.isFinite);
  check(t && finite(t.ctl) && finite(t.posL) && finite(t.payload), 'finite floats');
  console.log(`  telemetry: block ${t.block} f0 ${t.f0.toFixed(2)} kcut ${t.kcut} ctl [${Array.from(t.ctl).map(v => v.toFixed(3)).join(' ')}] stereo ${t.stereo}`);

  const cell = KYK.parseCell(await link.request(KYK.CMD.cell, KYK.cellReq(0)));
  check(cell && cell.k === 64 && cell.p === 8 && cell.mags.length === 64, 'cell 0');
  let bad = false; try { await link.request(KYK.CMD.cell, KYK.cellReq(99999)); } catch (e) { bad = /BAD_ARGS/.test(e.message); }
  check(bad, 'cell out of range → BAD_ARGS');

  const st = KYK.parseStats(await link.request(KYK.CMD.stats));
  check(st && st.renderDiv >= 1, 'stats');
  console.log(`  stats: cycles last ${st.cyclesLast} max ${st.cyclesMax} avg ${st.cyclesAvg} budget ${st.cyclesBudget} overruns ${st.overruns} dropped ${st.dropped}`);

  /* The FM world's formula, and the page's port of it against the module's.
   * The page draws its terrain from evalFm, so a drift between the two would
   * mean the picture stops being the space that is playing. GET_CELL asks the
   * module for the spectrum it actually computed at a position. */
  {
    const ws = await KYK.fetchWorlds(link);
    check(ws && ws.list.length === ws.count, 'the world list pages completely ('
          + (ws ? ws.list.length + ' of ' + ws.count : '?') + ')');
    const fm = ws && ws.list.find(w => w.kind === KYK.WORLD_KIND.fm);
    const vw = ws && ws.list.find(w => w.kind === KYK.WORLD_KIND.formant);
    check(!!fm, 'an FM world is registered');
    check(!!vw, 'a formant world is registered');
    const tb = ws && ws.list.find(w => w.kind === KYK.WORLD_KIND.table);
    const lk = ws && ws.list.find(w => w.kind === KYK.WORLD_KIND.lock);
    const un = ws && ws.list.find(w => w.kind === KYK.WORLD_KIND.unison);
    const md = ws && ws.list.filter(w => w.kind === KYK.WORLD_KIND.modal);
    const bd = ws && ws.list.filter(w => w.kind === KYK.WORLD_KIND.bend);
    check(bd && bd.length === 3, 'three single-shape worlds are registered');
    check(md && md.length === 3, 'three modal worlds are registered');
    check(!!tb, 'a shape-table world is registered');
    check(!!lk && !!un, 'the lock and unison worlds are registered');
    for (const { w, evalFn, label } of [{ w: fm, evalFn: KYK.evalFm, label: 'FM' },
                                        { w: vw, evalFn: KYK.evalFormant, label: 'Vowel' },
                                        { w: tb, evalFn: KYK.evalShapes, label: 'Shapes' },
                                        { w: lk, evalFn: KYK.evalLock, label: 'Lock' },
                                        { w: un, evalFn: KYK.evalUnison, label: 'Unison' },
                                        ...(md || []).map(w => ({ w, evalFn: KYK.evalModal, label: w.name })),
                                        ...(bd || []).map(w => ({ w, evalFn: KYK.evalBend, label: w.name }))]) {
      if (!w) continue;
      const b = await KYK.fetchBasis(link, w.index, info.maxBody);
      check(b && (b.fm || b.formant || b.table || b.lock || b.unison || b.modal || b.bend) && b.n === 4 && b.k === 64, label + ' formula arrives');
      /* switch to it, park at a known position, compare spectra */
      await link.request(KYK.CMD.action, KYK.actionReq(KYK.ACT.selectWorld, [w.index]));
      for (let i = 0; i < 60; i++) {
        const w2 = KYK.parseWorlds(await link.request(KYK.CMD.worlds, Uint8Array.of(0)));
        if (w2 && w2.current === w.index) break;
        await new Promise(r => setTimeout(r, 20));
      }
      const p = [0.37, 0.62, 0.28, 0.71];
      await link.request(KYK.CMD.setControl, KYK.setControlReq(110, p, [0, 0, 0, 0, 0, 0], 0));
      /* Wait for the *spectrum* to settle, not just the position. The
       * magnitudes in telemetry come from the last render, and rendering runs
       * on a divider behind a movement deadband, so the position can already
       * read the new value while the spectrum still belongs to the old world.
       * Two consecutive identical reads means the render has caught up. */
      let t3 = null, prev = null;
      for (let i = 0; i < 120; i++) {
        t3 = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(5)));
        const settled = t3 && Math.abs(t3.posL[0] - p[0]) < 1e-3 && prev
                        && t3.mags.every((v, j) => v === prev[j]);
        if (settled) break;
        prev = t3 && t3.mags;
        await new Promise(r => setTimeout(r, 20));
      }
      check(t3 && Math.abs(t3.posL[0] - p[0]) < 1e-3, label + ': module parked at the test position');
      /* sharp comes off the wire now, so the page evaluates the same space
       * the module rendered rather than assuming Morph is at zero. */
      const mine = evalFn(b, t3.posL, undefined, t3.sharp);
      /* Compare in decibels, per partial, because that is the only comparison
       * the wire can settle. Telemetry magnitudes are one byte over 96 dB, so
       * a step is 0.376 dB and no agreement finer than that is observable.
       * Comparing normalised linear amplitudes instead makes the tolerance
       * depend on the shape of the spectrum: a peaky world puts most of its
       * energy in one partial, and quantising that one partial moves the norm
       * and therefore every other partial with it. Partials below -40 dB are
       * skipped; the byte has barely any resolution left down there. */
      const norm = (a) => { let e = 0; for (const v of a) e += v * v; e = Math.sqrt(e) || 1; return Array.from(a, v => v / e); };
      const theirs = norm(Array.from(t3.mags).map(KYK.magDb).map(d => Math.pow(10, d / 20)));
      const ours = norm(mine);
      let worst = 0;
      for (let i = 0; i < 64; i++) {
        if (!(theirs[i] > 0.01) || !(ours[i] > 0.01)) continue;
        worst = Math.max(worst, Math.abs(20 * Math.log10(ours[i] / theirs[i])));
      }
      check(worst < 0.6, 'page ' + label + ' matches the module (worst partial ' + worst.toFixed(3) + ' dB)');
      console.log('  ' + label + ': worst partial mismatch ' + worst.toFixed(3) + ' dB, one telemetry step is 0.376');
    }
  }

  /* SET_CONTROL then telemetry must reflect it */
  const ctl = [0.11, 0.62, 0.33, 0.84], ang = [0.1, 0, 0, 0.05, 0, 0];
  await link.request(KYK.CMD.setControl, KYK.setControlReq(220, ctl, ang, 0.02), { urgent: true });
  await new Promise(r => setTimeout(r, 30));   /* a few blocks */
  const t2 = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(3)));
  check(t2 && close(t2.f0, 220, 1e-3), 'f0 applied (' + (t2 && t2.f0) + ')');
  check(t2 && ctl.every((v, i) => close(t2.ctl[i], v)), 'control frame applied');
  check(t2 && close(t2.angle[0], 0.1) && close(t2.angle[3], 0.05), 'angles applied');
  check(t2 && close(t2.spread, 0.02) && t2.stereo, 'spread applied, stereo on');
  check(t2 && t2.block > t.block, 'engine runs in real time (block ' + t.block + ' → ' + t2.block + ')');
  const dist = Math.hypot(...Array.from(t2.posL).map((v, i) => v - t2.posR[i]));
  check(dist > 0, 'posL ≠ posR under spread');
  console.log(`  set-control: f0 ${t2.f0.toFixed(1)} ctl [${Array.from(t2.ctl).map(v => v.toFixed(2)).join(' ')}] centre [${Array.from(t2.centre).map(v => v.toFixed(3)).join(' ')}] |L−R| ${dist.toFixed(4)}`);

  /* action: render_div 2 shows up in stats; unknown op → BAD_ARGS */
  await link.request(KYK.CMD.action, KYK.actionReq(KYK.ACT.renderDiv, [2]), { urgent: true });
  const st2 = KYK.parseStats(await link.request(KYK.CMD.stats));
  check(st2 && st2.renderDiv === 2, 'action render_div');
  let badAct = false; try { await link.request(KYK.CMD.action, KYK.actionReq(200)); } catch (e) { badAct = /BAD_ARGS|UNSUPPORTED/.test(e.message); }
  check(badAct, 'unknown action refused');

  /* A burst of keyed polls collapses. Measured as the delta across the burst,
   * not the running total: the total grows whenever anything is added earlier
   * in this file, which made the check fail for a reason that had nothing to
   * do with coalescing. */
  const before = link.stats.reqs;
  const ps = []; for (let i = 0; i < 20; i++) ps.push(link.request(KYK.CMD.telemetry, KYK.telemetryReq(1), { key: 'tel' }));
  await Promise.all(ps);
  const spent = link.stats.reqs - before;
  check(spent < 20, 'keyed polls coalesce (' + spent + ' requests for 20 polls)');
  console.log(`  link: ${link.stats.reqs} requests, rtt avg ${link.stats.rttAvg.toFixed(2)} ms`);
});

/* ── bridge: HELLO through the WebSocket ── */
if (doBridge) {
  const port = 18765 + Math.floor(Math.random() * 1000);
  console.log('== bridge on :' + port);
  const bridge = spawn(process.execPath, [path.join(repo, 'tools', 'bridge', 'bridge.mjs'), '--port', String(port), '--bin', bin, '--', '--gen', '--seed', '1'], { stdio: ['ignore', 'inherit', 'pipe'] });
  const logs = []; bridge.stderr.on('data', d => logs.push(String(d)));
  await new Promise(r => setTimeout(r, 400));
  const page = await new Promise((res, rej) => http.get({ host: '127.0.0.1', port, path: '/' }, r => { let s = ''; r.on('data', d => s += d); r.on('end', () => res({ status: r.statusCode, body: s })); }).on('error', rej));
  check(page.status === 200 && /<canvas/i.test(page.body), 'bridge serves index.html');
  const js = await new Promise((res, rej) => http.get({ host: '127.0.0.1', port, path: '/link.js' }, r => { let s = ''; r.on('data', d => s += d); r.on('end', () => res({ status: r.statusCode, body: s })); }).on('error', rej));
  check(js.status === 200 && /parseTelemetry/.test(js.body), 'bridge serves link.js');
  const nf = await new Promise((res, rej) => http.get({ host: '127.0.0.1', port, path: '/../Makefile' }, r => res(r.statusCode)).on('error', rej));
  check(nf === 404 || nf === 403, 'bridge refuses paths outside web/');

  /* minimal RFC 6455 client */
  const ws = await new Promise((res, rej) => {
    const key = crypto.randomBytes(16).toString('base64');
    const req = http.request({ host: '127.0.0.1', port, path: '/link', headers: { Connection: 'Upgrade', Upgrade: 'websocket', 'Sec-WebSocket-Key': key, 'Sec-WebSocket-Version': '13' } });
    req.on('upgrade', (r, socket, head) => {
      const want = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      check(r.headers['sec-websocket-accept'] === want, 'ws accept key');
      res({ socket, head });
    });
    req.on('error', rej); req.end();
  });
  const wsTransport = {
    kind: 'ws', onError: null, onClose: null, buf: Buffer.alloc(0),
    start(onBytes) {
      const feed = chunk => {
        this.buf = Buffer.concat([this.buf, chunk]);
        for (;;) {
          if (this.buf.length < 2) return;
          const op = this.buf[0] & 0x0f; let len = this.buf[1] & 0x7f, at = 2;
          if (len === 126) { if (this.buf.length < 4) return; len = this.buf.readUInt16BE(2); at = 4; }
          else if (len === 127) { if (this.buf.length < 10) return; len = Number(this.buf.readBigUInt64BE(2)); at = 10; }
          if (this.buf.length < at + len) return;
          const payload = this.buf.subarray(at, at + len); this.buf = this.buf.subarray(at + len);
          if (op === 0x2) onBytes(new Uint8Array(payload)); else if (op === 0x8) { if (this.onClose) this.onClose(); }
        }
      };
      if (ws.head && ws.head.length) feed(ws.head);
      ws.socket.on('data', feed);
      return Promise.resolve();
    },
    write(bytes) {
      const mask = crypto.randomBytes(4); const len = bytes.length;
      const hdr = len < 126 ? Buffer.from([0x82, 0x80 | len]) : Buffer.concat([Buffer.from([0x82, 0x80 | 126]), (() => { const b = Buffer.alloc(2); b.writeUInt16BE(len); return b; })()]);
      const body = Buffer.from(bytes); for (let i = 0; i < body.length; i++) body[i] ^= mask[i & 3];
      ws.socket.write(Buffer.concat([hdr, mask, body])); return Promise.resolve();
    },
    close() { try { ws.socket.write(Buffer.from([0x88, 0x80, 0, 0, 0, 0])); ws.socket.end(); } catch {} return Promise.resolve(); },
  };
  const link = new KYK.Link(wsTransport);
  await link.start();
  const info = await KYK.hello(link);
  check(info.id === 'kyk', 'HELLO through the bridge');
  const t = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(3)));
  check(t && t.bytes === 461, 'telemetry through the bridge');
  console.log(`  bridge: hello ${info.name} ${info.fw}, telemetry block ${t && t.block}`);
  await link.close();
  await new Promise(r => setTimeout(r, 150));
  bridge.kill();
  check(logs.join('').includes('connected'), 'bridge logged the connection');
}

/* ── importing a single cycle ─────────────────────────────────────────── */
{
  const N = 600;
  const mk = (f) => { const x = new Float64Array(N); for (let i = 0; i < N; i++) x[i] = f(i / N); return x; };

  const sine = KYK.cycleToNode(mk(u => Math.sin(2 * Math.PI * u)));
  check(sine.fit > 0.9999, `a pure sine fits the sine basis exactly (${sine.fit.toFixed(4)})`);
  check(Math.abs(Math.abs(sine.mags[0]) - 1) < 1e-3 && Math.abs(sine.mags[1]) < 1e-3,
        'and lands entirely in harmonic 1');

  /* a saw is 2/(pi h): the ratio of the first two harmonics must be 2 */
  const saw = KYK.cycleToNode(mk(u => 2 * u - 1));
  const ratio = Math.abs(saw.mags[0] / saw.mags[1]);
  check(saw.fit > 0.99 && Math.abs(ratio - 2) < 0.02,
        `a saw keeps ${(100 * saw.fit).toFixed(1)}% and rolls off as 1/h (h1/h2 = ${ratio.toFixed(3)})`);

  /* a cosine is the worst case: it is entirely in the half we cannot store,
     until the rotation search moves it, which is the whole point of doing one */
  const cos = KYK.cycleToNode(mk(u => Math.cos(2 * Math.PI * u)));
  check(cos.fit > 0.999, `a cosine is rescued by the rotation search (${cos.fit.toFixed(4)}, offset ${cos.rotation})`);

  /* spectrum mode keeps the magnitudes whatever the phase */
  const cs = KYK.cycleToNode(mk(u => Math.cos(2 * Math.PI * u)), 64, 'spectrum');
  check(cs.fit === 1 && Math.abs(cs.mags[0] - 1) < 1e-3,
        'spectrum mode keeps the magnitude and discards the phase');

  /* a WAV survives the round trip into a node */
  const n = 256, wav = new Uint8Array(44 + n * 2), dv = new DataView(wav.buffer);
  const put = (o, t) => { for (let i = 0; i < 4; i++) wav[o + i] = t.charCodeAt(i); };
  put(0, 'RIFF'); dv.setUint32(4, 36 + n * 2, true); put(8, 'WAVE');
  put(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
  dv.setUint32(24, 48000, true); dv.setUint32(28, 96000, true); dv.setUint16(32, 2, true); dv.setUint16(34, 16, true);
  put(36, 'data'); dv.setUint32(40, n * 2, true);
  for (let i = 0; i < n; i++) dv.setInt16(44 + i * 2, Math.round(32767 * Math.sin(2 * Math.PI * i / n)), true);
  const cyc = KYK.wavToCycle(wav);
  check(cyc.length === n, `a 16-bit mono WAV parses to ${cyc.length} samples`);
  const node = KYK.cycleToNode(cyc);
  check(node.fit > 0.999, 'and projects cleanly');

  const blob = KYK.buildUserWorld([{ pos: [0.2, 0.5, 0.5, 0.5], mags: node.mags },
                                   { pos: [0.8, 0.5, 0.5, 0.5], mags: saw.mags }], { name: 'import' });
  check(blob.length === 32 + 2 * 4 * (4 + 64), `two imported nodes make a ${blob.length}-byte world`);
  check(new DataView(blob.buffer).getUint32(0, true) === 0x574B594B && blob[8] === 2,
        'with the right magic and node count');

  let badWav = false;
  try { KYK.wavToCycle(new Uint8Array(64)); } catch { badWav = true; }
  check(badWav, 'a file that is not a WAV is refused rather than guessed at');
}

/* ── a world saved from the page is a world the module reads ───────────────
 *
 * The page's save button writes these bytes to a file and its send button puts
 * the same bytes on the wire, so the thing worth proving is that the C++
 * reader accepts what the JavaScript writer produced. Nothing else in the
 * suite crosses that language boundary: tests/user_check.cpp round-trips the
 * reader against SaveUserWorld, which would agree with itself about a format
 * both halves got wrong.
 */
console.log('\n== a page-built world through the real reader');
await withChild(['--serve', '--gen', '--seed', '1'], async link => {
  const nodes = [];
  for (let i = 0; i < 24; i++) {
    const mags = new Float32Array(64);
    for (let h = 0; h < 64; h++) mags[h] = (h % (i + 2) === 0 ? 1 : -1) / (h + 1);
    const pos = [0.5, 0.5, 0.5, 0.5];
    pos[i % 4] = i < 12 ? 0.2 : 0.8;
    nodes.push({ pos, mags });
  }
  const blob = KYK.buildUserWorld(nodes, { n: 4, k: 64, sigma: 0.26, name: 'saved from page' });
  check(blob.length === 32 + 24 * 4 * (4 + 64), `a full 24-node world is ${blob.length} bytes`);

  /* A world change is not visible the instant the request returns: the swap
     happens on the request, but the spectrum and the frame in telemetry are
     rewritten by the next *render*, and a render is not every block.
     Two wrong versions of this before the right one. "Read twice" failed about
     one run in four, because two reads can both land before that render.
     "Read until two agree" failed about one in five, because the picture can
     sit unchanged across two reads and then move — which is what a render that
     is throttled looks like from outside. Both were a race being treated as a
     delay. Each telemetry request advances a block, so: drive a few blocks to
     let the change reach a render at all, then require three consecutive
     identical frames. Idle reads are bit-identical, which is what makes
     agreement mean settled rather than merely slow. */
  const read = async () => KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(3)));
  /* The live world rides in the motion block — the tail is the only part of
     the frame that can grow — so asking for spectrum and frame alone does not
     carry it. The page asks for all three, which is why this does too. */
  const readAll = async () => KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(7)));
  const same = (a, b) => !!a && !!b && a.length === b.length && [...a].every((v, i) => v === b[i]);
  const settle = async (pick, tries = 60) => {
    for (let i = 0; i < 6; i++) await read();
    let prev = pick(await read()), agree = 0;
    for (let i = 0; i < tries; i++) {
      const m = pick(await read());
      if (same(prev, m)) { if (++agree >= 3) return m; } else { agree = 0; prev = m; }
    }
    return prev;
  };
  const spectrum = () => settle(t => t.mags);
  const frame    = () => settle(t => t.frame);
  const select   = (i) => link.request(KYK.CMD.action, Uint8Array.of(4, i));

  /* The world list, which is now four pages rather than one.
   *
   * The notes used to be six words; they now say what each of the four axes
   * does and give the formula where there is one, which took the list from
   * about 900 bytes to about 2.9 KB against a 1024-byte body. That is what
   * the paging was built for, and it had never carried more than one page in
   * anger — so this walks it and checks every world arrives exactly once. */
  {
    const all = await KYK.fetchWorlds(link);
    check(all.count === 21 && all.list.length === 21,
          `all ${all.count} worlds arrive across the pages (${all.list.length} listed)`);
    const seen = new Set(all.list.map(w => w.index));
    check(seen.size === all.count && [...seen].every(i => i >= 0 && i < all.count),
          'each exactly once, and none invented');
    check(all.list.every(w => w.name && w.note.length > 40),
          `every world says what it is (shortest note ${Math.min(...all.list.map(w => w.note.length))} chars)`);
    /* The corpus is somebody else's work and the credit travels with it. */
    const crop = all.list.find(w => w.name === 'Crop');
    check(crop && /Braids/.test(crop.note) && /Gillet/.test(crop.note),
          'and Crop credits the bank it is four components of');
  }

  await select(9);
  const before = await spectrum();
  check((await KYK.fetchWorlds(link)).current === 9, 'a built-in world is live to begin with');
  check((await readAll()).world === 9, 'and telemetry says so without being asked for the list');

  let sent = 0;
  await KYK.putWorld(link, blob, (done) => { sent = done; });
  check(sent === blob.length, 'the module accepted every chunk');

  const after = await spectrum();
  check(after && [...after].some(m => m > 0), 'the loaded world renders harmonics');
  check(!same(before, after), 'and it is not the world that was playing before it');
  check((await KYK.fetchWorlds(link)).current === 0xFF, 'the live world is no longer one of the built-ins');
  /* And the frame agrees, which is what lets a page notice a world it did not
     ask for — a switch from the module's own panel, or another host sending a
     world while this one watches. */
  check((await readAll()).world === 0xFF, 'and the frame says a user world is playing');
  await select(14);
  for (let i = 0; i < 20 && (await readAll()).world !== 14; i++) { /* it switches on the control loop */ }
  check((await readAll()).world === 14, 'a switch away from it shows up in the frame too');

  /* Arriving at a sent world must be the same as arriving at it from anywhere
     else. This is tests/switch_check.cpp's invariant — 420 world pairs, each
     asserting that arriving at a world is identical to starting in it — asked
     of the one path that does not go through a world switch at all.
     It is here because the first version of this test could not tell a working
     send from a broken one. "The spectrum changed" passes either way given
     enough reads: without the fix the world's *contents* are still replaced,
     so something else eventually forces a render and picks them up late and at
     the previous world's phase convention. This cannot be fooled that way —
     the frame is the waveform, phase and all, and if anything about the engine
     still describes the world you came from, where you came from shows up in
     it. Measured against a shell with the SetWorld call removed: 173 out of
     255 worst-sample difference, against 0 here. */
  const arrivals = [];
  for (const from of [0, 9, 18]) {
    await select(from);
    await frame();
    await KYK.putWorld(link, blob);
    arrivals.push(await frame());
  }
  const worst = (a, b) => { let w = 0; for (let i = 0; i < a.length; i++) w = Math.max(w, Math.abs(a[i] - b[i])); return w; };
  check(same(arrivals[0], arrivals[1]) && same(arrivals[0], arrivals[2]),
        `a sent world sounds the same whichever world it replaced (worst sample ${
          Math.max(worst(arrivals[0], arrivals[1]), worst(arrivals[0], arrivals[2]))} of 255)`);

  /* Reading one world's formula must not change the sound of another.
   *
   * VertexField holds raw pointers into a VertexTable, so building a world
   * into the table the live world points at rewrites its waveforms. GET_BASIS
   * did exactly that: a *read* moved partials of the playing world by up to 22
   * magnitude steps, about 8.8 dB. It is the codebase's own pattern again —
   * contents replaced underneath something holding a pointer — in the shell
   * the whole suite is graded against. */
  {
    await select(1);                      /* the 24-cell, which has a table */
    const before = await spectrum();
    for (const w of [2, 3]) { try { await KYK.fetchBasis(link, w, 1024); } catch { /* fine */ } }
    check(same(await spectrum(), before),
          'reading another world\'s formula leaves the playing world alone');
  }

  /* What the engine actually renders for a node's coefficients.
   *
   * The page draws "what the module will make of this waveform" beside the
   * imported one, and plays the two against each other, so the page's idea of
   * the render has to be the module's idea of it. It was not: RenderFrame sums
   * mag.cos(h.theta + phi) and for a sine-phase world the engine's phi makes
   * that *minus* sin(h.theta), while the page summed plus sin — so every
   * rendered trace was drawn mirrored and every fit looked worse than it was.
   *
   * Every node identical, so where the position sits cannot matter. */
  {
    const mags = new Float32Array(64);
    for (let h = 0; h < 6; h++) mags[h] = (h % 2 ? -1 : 1) / (h + 1);
    const ns = [];
    for (let i = 0; i < 24; i++) ns.push({ pos: [0.5, 0.5, 0.5, 0.5], mags });
    await KYK.putWorld(link, KYK.buildUserWorld(ns, { n: 4, k: 64, sigma: 0.26, name: 'convention' }));
    const f = await frame();
    const N = f.length, want = new Float64Array(N);
    for (let i = 0; i < N; i++) {
      let y = 0;
      for (let h = 0; h < 64; h++) y -= mags[h] * Math.sin(2 * Math.PI * (h + 1) * i / N);
      want[i] = y;
    }
    let sa = 0, sb = 0, ab = 0;
    for (let i = 0; i < N; i++) { sa += want[i] * want[i]; sb += f[i] * f[i]; ab += want[i] * f[i]; }
    const c = ab / Math.sqrt(sa * sb || 1);
    check(c > 0.999, `a node renders as -sum m.sin(h.theta), no shift (corr ${c.toFixed(4)})`);
  }

  /* A morph target is a blend, not a setting that is merely accepted.
   *
   * Everything that tested this asked whether the action came back with status
   * zero, which it does whether or not the engine blends anything. Selecting a
   * target must change what is playing — the desktop shell blends at a fixed
   * half, having no knob to turn. */
  await select(9);
  const solo = await spectrum();
  const r = await link.request(KYK.CMD.action, Uint8Array.of(5, 14));
  check(r[0] === 0, 'a morph target is accepted');
  const blended = await spectrum();
  check(!same(solo, blended), 'and blending towards it changes the sound');
  await link.request(KYK.CMD.action, Uint8Array.of(5, 0xFF));
  check(same(await spectrum(), solo), 'and clearing it puts the sound back');

  /* Corruption is refused rather than half-loaded. This is the first thing in
     the instrument that reads bytes a stranger wrote, and "silence beats a
     hard fault" is only true if the refusal actually happens. */
  const good = await spectrum();
  for (const [what, mangle] of [
    ['a file that is not a world', b => { b[0] ^= 0xFF; }],
    ['a node count the bytes cannot cover', b => { b[8] = 24 + 1; }],
    ['a harmonic count that is not ours', b => { b[7] = 200; }],
    ['an impossible dimension', b => { b[6] = 0; }],
  ]) {
    const bad = blob.slice(); mangle(bad);
    let refused = false;
    try { await KYK.putWorld(link, bad); } catch { refused = true; }
    check(refused, `${what} is refused`);
  }
  /* and a refusal leaves the destination untouched rather than half-written */
  check(same(await spectrum(), good),
        'a refused world leaves the one that was playing exactly as it was');
});

/* ── a link that opens onto a module holding half a frame ─────────────────
 *
 * This is "connect twice over serial", reproduced. A link does not always
 * close tidily — the page goes away mid-frame, the cable is pulled, the module
 * resets while a request is in flight — and the module's parser is left with
 * the first half of a frame in its accumulator. COBS cannot know that: the
 * next thing it sees is HELLO, which it appends to the garbage and delimits
 * into one malformed frame, drops silently, and answers nothing. HELLO times
 * out, connecting fails, and clicking Connect again works because the failed
 * attempt's own delimiter is what cleared the accumulator.
 *
 * Driven over stdio rather than a real port, because the thing being tested is
 * the module's framing state and kykdesk runs the same SDK parser the module
 * does. Half a HELLO frame goes in before the link is started.
 */
console.log('\n== connecting onto a module left mid-frame');
{
  const half = KYK.buildFrame(0x01, 99, new Uint8Array(0));
  const child = spawn(bin, ['--serve', '--gen', '--seed', '1'], { stdio: ['pipe', 'pipe', 'inherit'] });
  child.stdin.write(Buffer.from(half.subarray(0, half.length - 3)));   /* no delimiter */
  const link = new KYK.Link(new KYK.StdioTransport(child));
  const t0 = Date.now();
  await link.start();
  let info = null, err = null;
  try { info = await KYK.hello(link); } catch (e) { err = e; }
  check(info && info.id === 'kyk',
        `HELLO answers first time onto a mid-frame parser${err ? ' — ' + err.message : ''}`);
  /* First time, not eventually: the retries would hide the whole bug. A clean
     HELLO is under a millisecond over stdio, so anything near the 400 ms
     timeout means an attempt was thrown away. */
  check(Date.now() - t0 < 300, `and without burning a retry to do it (${Date.now() - t0} ms)`);
  check(link.stats.timeouts === 0, `no request timed out (${link.stats.timeouts})`);
  await link.close();
}

/* ── a library of worlds you made, and morphing between two of them ───────
 *
 * This is the gap the slots exist to close. The morph target index space was
 * the twenty-one built-ins and nothing else — `kActMorphWorld` refuses any
 * index at or past kCount — so a world you imported could be *played* and
 * could never be one end of a blend. There was also nowhere to put a second
 * one: a transfer replaced whatever was live. You could make a world and you
 * could not get back to it.
 */
console.log('\n== slots: a library, and a morph between two of your own worlds');
await withChild(['--serve', '--gen', '--seed', '1'], async link => {
  const read = async () => KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(7)));
  const same = (a, b) => !!a && !!b && a.length === b.length && [...a].every((v, i) => v === b[i]);
  const settle = async (tries = 60) => {
    for (let i = 0; i < 6; i++) await read();
    let prev = (await read()).mags, agree = 0;
    for (let i = 0; i < tries; i++) {
      const m = (await read()).mags;
      if (same(prev, m)) { if (++agree >= 3) return m; } else { agree = 0; prev = m; }
    }
    return prev;
  };
  const act = (op, ...a) => link.request(KYK.CMD.action, Uint8Array.of(op, ...a), { urgent: true });
  /* A non-zero status arrives as a rejection, not as a body, so a check that
     expects a refusal has to catch one. */
  const refused = async (op, ...a) => { try { await act(op, ...a); return false; } catch { return true; } };

  /* two worlds that sound nothing like each other */
  const worldOf = (name, f) => {
    const nodes = [];
    for (let i = 0; i < 24; i++) {
      const mags = new Float32Array(64);
      for (let h = 0; h < 64; h++) mags[h] = f(h, i);
      nodes.push({ pos: [0.5, 0.5, 0.5, 0.5], mags });
    }
    return KYK.buildUserWorld(nodes, { n: 4, k: 64, sigma: 0.26, name });
  };
  const A = worldOf('hollow', (h) => (h % 2 === 0 ? 1 : 0) / (h + 1));
  const B = worldOf('buzz',   (h) => (h < 24 ? 1 : 0) / Math.sqrt(h + 1));

  await KYK.putSlot(link, 0, A);
  await KYK.putSlot(link, 3, B);
  const sl = await KYK.fetchSlots(link);
  check(sl && sl.count === 32, `the module reports ${sl && sl.count} slots`);
  check(sl && sl.names[0] === 'hollow' && sl.names[3] === 'buzz',
        `and names the two that are filled (${sl && sl.names[0]}, ${sl && sl.names[3]})`);
  check(sl && sl.names[1] === null, 'an empty slot has no name rather than a blank one');

  /* play one */
  check((await act(KYK.ACT.slotLive, 0))[0] === 0, 'a slot can be played');
  const live0 = await settle();
  check((await KYK.fetchSlots(link)).live === 0, 'and the module says which one is');

  /* play the other: a different sound, which is what a library is for */
  check((await act(KYK.ACT.slotLive, 3))[0] === 0, 'and so can another');
  const live3 = await settle();
  check(!same(live0, live3), 'the two sound different, so both really loaded');

  /* and now the thing that was impossible: morph towards one of your own */
  check((await act(KYK.ACT.slotTarget, 0))[0] === 0, 'a slot can be the morph target');
  const blended = await settle();
  check(!same(blended, live3), 'morphing towards it changes the sound');
  check(!same(blended, live0), 'and it is a blend, not a switch to the target');
  const t = await read();
  check(t.morphWorld === KYK.MORPH_USER,
        `telemetry says the target is one of yours, not a built-in (${t.morphWorld})`);
  check((await KYK.fetchSlots(link)).target === 0, 'and which slot it is');

  /* clearing it puts the single-world sound back */
  check((await act(KYK.ACT.slotTarget, 0xFF))[0] === 0, 'the target can be cleared');
  check(same(await settle(), live3), 'and the sound goes back to the world that is playing');

  /* Reading a slot back, and capturing what is playing.
   *
   * The page could not open a world it had not itself sent: GET_BASIS gives a
   * Lock world's count and sigma and no spectra, and a lattice world has no
   * formula at all. So "capture what is playing so I can edit it" — which is
   * the most obvious thing to want of a world you just made — was impossible
   * from the page. */
  {
    const back = await KYK.fetchSlot(link, 0, 1024);
    check(back && back.length === A.length && back.every((v, i) => v === A[i]),
          'a slot reads back byte for byte');
    check(await KYK.fetchSlot(link, 1, 1024) === null, 'an empty slot reads back as nothing');

    /* a built-in cannot be handed over as nodes, so it is sampled */
    await act(KYK.ACT.selectWorld, 9);                 /* FM, a formula world */
    check((await act(KYK.ACT.snapshot, 6))[0] === 0, 'a built-in can be snapshotted into a slot');
    const snap = KYK.parseUserWorld(await KYK.fetchSlot(link, 6, 1024));
    check(snap && snap.count === 24 && snap.k === 64,
          `and comes back as a world of ${snap && snap.count} nodes`);
    check(snap && snap.name === 'FM', `named after what it came from (${snap && snap.name})`);
    /* the nodes must differ from each other: a snapshot that sampled one point
       twenty-four times would parse perfectly and be useless */
    const distinct = new Set(snap.nodes.map(nd => nd.mags.join(','))).size;
    check(distinct > 8, `sampled at twenty-four different points (${distinct} distinct spectra)`);
    check(snap.nodes.every(nd => nd.mags.some(v => v !== 0)), 'and none of them is silent');
    /* and the positions are the 24-cell's vertices, two axes off centre each */
    const off = snap.nodes.map(nd => nd.pos.filter(v => Math.abs(v - 0.5) > 1e-6).length);
    check(off.every(c => c === 2), 'each on a 24-cell vertex');
    /* A named world, sampled without disturbing what is playing — which is
       what "start a new world from Lock" needs, and the reason there are no
       blank slots: a real starting point beats an empty one, and beats a slot
       pre-filled with silence. */
    /* Compared by *sound*, not by world index. The first version of this check
       compared the index and passed with the bug injected — building the named
       world into the live World leaves the index alone and replaces what you
       are hearing, which is the whole failure it was meant to catch. */
    check((await act(KYK.ACT.snapshot, 7, 13))[0] === 0, 'a named world can be snapshotted');
    const lock = KYK.parseUserWorld(await KYK.fetchSlot(link, 7, 1024));
    check(lock && lock.name === 'Lock', `named after it (${lock && lock.name})`);
    check(new Set(lock.nodes.map(nd => nd.mags.join(','))).size === 24,
          'with twenty-four distinct spectra, one per vertex');
    /* Forced to re-render before comparing, and that is the whole point.
       Replacing the live world's *contents* without telling the engine leaves it
       rendering its cached frame, so a comparison taken straight after the
       snapshot settles on the stale sound and passes — which is precisely the
       pattern this codebase keeps producing. Move the position away and back:
       the render at the original point re-reads the world, so a world that was
       overwritten underneath shows up. */
    /* Wait for the *position to arrive*, not for the frame to stop moving.
       The control frame is slewed, so a frame can sit unchanged for a few reads
       while the position is still travelling — settling on that made this check
       fail half the time on correct code. Telemetry reports the position, so
       wait for the thing actually being waited for. */
    const nudge = async (v) => {
      await link.request(KYK.CMD.setControl,
        KYK.setControlReq(110, [v, 0.5, 0.5, 0.5], [0, 0, 0, 0, 0, 0], 0), { urgent: true });
      for (let i = 0; i < 400; i++) {
        const t = await read();
        if (t.posL && Math.abs(t.posL[0] - v) < 1e-3) break;
      }
      return settle();
    };
    const atRest = await nudge(0.5);
    await act(KYK.ACT.snapshot, 7, 13);
    await nudge(0.8);
    check(same(await nudge(0.5), atRest),
          'and what was playing is untouched — sampled into scratch, not over it');
    /* Any world can be opened, not only the sixteen with a formula. A lattice
       is expanded into its own scratch blob — 1.2 MB of a 64 MB SDRAM, which is
       the one resource here there is plenty of — because "sixteen of the
       twenty-one can be edited" is a worse thing to explain than it is to fix. */
    await act(KYK.ACT.slotFree, 7);
    check((await act(KYK.ACT.snapshot, 7, 5))[0] === 0, 'a lattice world can be opened too');
    const field = KYK.parseUserWorld(await KYK.fetchSlot(link, 7, 1024));
    check(field && field.name === 'Field', `named after it (${field && field.name})`);
    check(field && new Set(field.nodes.map(nd => nd.mags.join(','))).size > 8,
          'with the lattice sampled at the vertices, not one point twenty-four times');
    check(same(await nudge(0.5), atRest),
          'and expanding it into scratch left what was playing alone');
    await act(KYK.ACT.slotFree, 7);

    /* it is a real world: it loads and plays */
    check((await act(KYK.ACT.slotLive, 6))[0] === 0, 'the snapshot plays as a world in its own right');
    await act(KYK.ACT.slotFree, 6);
  }

  /* Rearranging. Has to happen on the module: a host does not have the blob
     for a slot it did not store, only the name. */
  await act(KYK.ACT.slotLive, 0);
  await act(KYK.ACT.slotTarget, 3);
  /* Captured here, not reused from earlier: with a target armed the sound is a
     blend of both, so "unchanged" means unchanged from this moment. */
  const beforeSwap = await settle();
  check((await act(KYK.ACT.slotSwap, 0, 3))[0] === 0, 'two slots can be exchanged');
  const sw = await KYK.fetchSlots(link);
  check(sw.names[0] === 'buzz' && sw.names[3] === 'hollow', 'the contents move');
  check(sw.live === 3 && sw.target === 0,
        `and playing and target follow them rather than the numbers (live ${sw.live}, target ${sw.target})`);
  /* The same world is still playing, at a different number, so the sound must
     not have moved at all — a swap is bookkeeping and not a reload. */
  check(same(await settle(), beforeSwap),
        'and the sound does not move: the same two worlds are playing, at different numbers');
  check((await act(KYK.ACT.slotSwap, 7, 7))[0] === 0, 'a slot can be swapped with itself, harmlessly');
  check(await refused(KYK.ACT.slotSwap, 0, 99), 'a swap with a slot that does not exist is refused');
  await act(KYK.ACT.slotSwap, 0, 3);        /* put them back */
  await act(KYK.ACT.slotTarget, 0xFF);

  /* housekeeping, and the refusals */
  check(await refused(KYK.ACT.slotLive, 1), 'an empty slot cannot be played');
  check(await refused(KYK.ACT.slotTarget, 1), 'nor be a morph target');
  check(await refused(KYK.ACT.slotLive, 99), 'a slot that does not exist is refused');
  let sentToNowhere = false;
  try { await KYK.putSlot(link, 99, A); } catch { sentToNowhere = true; }
  check(sentToNowhere, 'and so is a transfer to one');
  /* Free whatever is playing, so the second half of this is about freeing a
     *live* slot rather than about whichever index a previous check left behind.
     The first version of this depended on the state the test above happened to
     end in, and broke the moment a test was inserted before it. */
  await act(KYK.ACT.slotLive, 3);
  check((await act(KYK.ACT.slotFree, 3))[0] === 0, 'a slot can be freed');
  const after = await KYK.fetchSlots(link);
  check(after.names[3] === null, 'which empties it');
  check(after.live === 0xFF, 'and stops it claiming to be live');
});

/* ── the card, written to and not only read from ───────────────────────────
 *
 * Slots live in SDRAM, so everything you build dies at power-off. The card is
 * the only thing that survives, and until now the module could only read it.
 *
 * The whole path had *no desktop implementation at all*, so reading from a card
 * was untested and writing to one would have shipped the same way — which is
 * not a thing to do with the first code in an instrument that can destroy
 * somebody's file. `kykdesk --card <dir>` makes a directory a card, which is a
 * card for every purpose this protocol has.
 */
console.log('\n== the card: write, list, read back, and refuse to clobber');
{
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'kyk-card-'));
  const child = spawn(bin, ['--serve', '--gen', '--seed', '1', '--card', dir],
                      { stdio: ['pipe', 'pipe', 'inherit'] });
  const link = new KYK.Link(new KYK.StdioTransport(child));
  await link.start();
  await KYK.hello(link);
  const act = (op, ...a) => link.request(KYK.CMD.action, Uint8Array.of(op, ...a), { urgent: true });

  const nodes = [];
  for (let i = 0; i < 4; i++) {
    const mags = new Float32Array(64);
    for (let h = 0; h < 64; h++) mags[h] = (h % (i + 2) ? 0 : 1) / (h + 1);
    nodes.push({ pos: [0.3 + 0.1 * i, 0.5, 0.5, 0.5], mags });
  }
  const blob = KYK.buildUserWorld(nodes, { n: 4, k: 64, sigma: 0.26, name: 'bells' });
  await KYK.putSlot(link, 2, blob);

  check(fs.readdirSync(dir).length === 0, 'the card starts empty');

  /* A blank card has no world folder, and the first save anybody ever does has
     to work anyway — without this it failed with a generic device error and
     nothing in the message said the directory was missing. */
  {
    const blank = path.join(dir, 'nofolder');
    const c2 = spawn(bin, ['--serve', '--gen', '--seed', '1', '--card', blank],
                     { stdio: ['pipe', 'pipe', 'inherit'] });
    const l2 = new KYK.Link(new KYK.StdioTransport(c2));
    await l2.start(); await KYK.hello(l2);
    await KYK.putSlot(l2, 0, blob);
    let ok = true;
    try { await KYK.saveCardWorld(l2, 0, 'first', false); } catch { ok = false; }
    check(ok && fs.existsSync(path.join(blank, 'first.kykw')),
          'saving to a card with no world folder makes the folder');
    await l2.close();
  }
  await KYK.saveCardWorld(link, 2, 'bells', false);
  check(fs.existsSync(path.join(dir, 'bells.kykw')), 'a slot can be written to the card');
  /* Byte for byte, because a world that comes back different is worse than one
     that does not come back. */
  const onDisk = new Uint8Array(fs.readFileSync(path.join(dir, 'bells.kykw')));
  check(onDisk.length === blob.length && onDisk.every((v, i) => v === blob[i]),
        'and the bytes on the card are the bytes that were sent');
  check(!fs.readdirSync(dir).some(f => f.endsWith('.part')),
        'the temp file it was written through is gone');

  /* the refusal, which is the whole safety story */
  let exists = false;
  try { await KYK.saveCardWorld(link, 2, 'bells', false); }
  catch (e) { exists = /already on the card/.test(e.message); }
  check(exists, 'writing over a file is refused unless overwriting is asked for');
  await KYK.saveCardWorld(link, 2, 'bells', true);
  check(fs.readdirSync(dir).filter(f => f.endsWith('.kykw')).length === 1,
        'and permitted when it is, without leaving a second copy');

  /* a name cannot escape the folder */
  let escaped = false;
  try { await KYK.saveCardWorld(link, 2, '../escape', false); } catch { escaped = true; }
  check(escaped, 'a name that tries to leave the world folder is refused');
  check(!fs.existsSync(path.join(dir, '..', 'escape.kykw')), 'and nothing is written outside it');

  /* the card is a library: list it, and pull one back into a slot */
  const cards = await KYK.fetchCardWorlds(link);
  check(cards.names.includes('bells.kykw'), `the card lists it (${cards.names.join(' ')})`);
  await act(KYK.ACT.cardToSlot, 0, 9);
  const sl = await KYK.fetchSlots(link);
  check(sl.names[9] === 'bells', `a card file loads into a slot (${sl.names[9]})`);
  check((await act(KYK.ACT.slotLive, 9))[0] === 0, 'and that slot plays');

  /* which closes the loop: build, slot, card, power cycle, slot, play */
  await KYK.saveCardWorld(link, 9, 'second', false);
  const after = await KYK.fetchCardWorlds(link);
  check(after.names.length === 2, `two worlds on the card (${after.names.join(' ')})`);

  await link.close();
  fs.rmSync(dir, { recursive: true, force: true });
}

console.log(failures ? `selftest: ${failures} of ${checks} checks FAILED` : `selftest: all ${checks} checks passed`);
process.exit(failures ? 1 : 0);

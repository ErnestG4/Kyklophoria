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
  check(tk && tk.bytes === 483, 'motion block appends 22 B (got ' + (tk && tk.bytes) + ')');
  check(tk && tk.kepler && tk.kepler.plane < 6 && tk.kepler.rush >= 0 && tk.lock >= 0, 'motion fields parse');
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
    check(md && md.length === 3, 'three modal worlds are registered');
    check(!!tb, 'a shape-table world is registered');
    check(!!lk && !!un, 'the lock and unison worlds are registered');
    for (const { w, evalFn, label } of [{ w: fm, evalFn: KYK.evalFm, label: 'FM' },
                                        { w: vw, evalFn: KYK.evalFormant, label: 'Vowel' },
                                        { w: tb, evalFn: KYK.evalShapes, label: 'Shapes' },
                                        { w: lk, evalFn: KYK.evalLock, label: 'Lock' },
                                        { w: un, evalFn: KYK.evalUnison, label: 'Unison' },
                                        ...(md || []).map(w => ({ w, evalFn: KYK.evalModal, label: w.name }))]) {
      if (!w) continue;
      const b = await KYK.fetchBasis(link, w.index, info.maxBody);
      check(b && (b.fm || b.formant || b.table || b.lock || b.unison || b.modal) && b.n === 4 && b.k === 64, label + ' formula arrives');
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
        t3 = KYK.parseTelemetry(await link.request(KYK.CMD.telemetry, KYK.telemetryReq(1)));
        const settled = t3 && Math.abs(t3.posL[0] - p[0]) < 1e-3 && prev
                        && t3.mags.every((v, j) => v === prev[j]);
        if (settled) break;
        prev = t3 && t3.mags;
        await new Promise(r => setTimeout(r, 20));
      }
      check(t3 && Math.abs(t3.posL[0] - p[0]) < 1e-3, label + ': module parked at the test position');
      const mine = evalFn(b, t3.posL);
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

console.log(failures ? `selftest: ${failures} of ${checks} checks FAILED` : `selftest: all ${checks} checks passed`);
process.exit(failures ? 1 : 0);

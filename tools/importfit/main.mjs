/* importfit — what the import representation costs, over a whole corpus.
 *
 *   node tools/importfit/main.mjs <dir> [sample]
 *
 * Run through web/link.js's own wavToCycle and cycleToNode, deliberately: this
 * measures the path the page actually uses, not a second implementation of it
 * that could be right while the page is wrong.
 *
 * Two numbers per waveform, and the gap between them is the whole argument:
 *
 *   sine basis only   the fraction of the wave that survives projection onto
 *                     sin(h.theta) after the rotation search — what import
 *                     keeps today. Whatever sits in the cosine half is gone.
 *   band limit K=64   the fraction inside the first 64 harmonics, which is the
 *                     ceiling for *any* representation at this K. A phase-
 *                     correct import cannot beat this and does not need to.
 *
 * If the two are close, the band limit is the binding constraint and storing
 * phase would buy little. They are not close. See docs/importfit-2026-09-13.txt.
 */
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
import { fileURLToPath } from 'node:url';
const KYK = require(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../web/link.js'));
const ROOT = process.argv[2];
const limit = Number(process.argv[3] || 0);
if (!ROOT) { console.error('usage: node tools/importfit/main.mjs <dir> [sample]'); process.exit(2); }

function walk(d, out = []) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (/\.wav$/i.test(e.name)) out.push(p);
  }
  return out;
}
let files = walk(ROOT).sort();
if (limit && limit < files.length) {           /* an even stride across every bank */
  const step = files.length / limit, pick = [];
  for (let i = 0; i < limit; i++) pick.push(files[Math.floor(i * step)]);
  files = pick;
}
/* energy in harmonics 1..K over total energy: what a band limit costs whatever
   the phase convention is, and therefore the ceiling any representation has */
function bandFit(cyc, K = 64) {
  const N = cyc.length;
  let tot = 0, keep = 0;
  for (let i = 0; i < N; i++) tot += cyc[i] * cyc[i];
  for (let h = 1; h <= K; h++) {
    let re = 0, im = 0;
    for (let i = 0; i < N; i++) { const th = 2 * Math.PI * h * i / N; re += cyc[i] * Math.cos(th); im += cyc[i] * Math.sin(th); }
    keep += 2 * (re * re + im * im) / N;
  }
  return tot > 0 ? Math.min(1, keep / tot) : 1;
}
const sine = [], band = [];
let bad = 0;
const t0 = Date.now();
for (const f of files) {
  try {
    const cyc = KYK.wavToCycle(new Uint8Array(fs.readFileSync(f)));
    sine.push(KYK.cycleToNode(cyc, 64, 'shape').fit);
    band.push(bandFit(cyc));
  } catch { bad++; }
}
const pct = (a, q) => { const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(q * s.length))]; };
const mean = a => a.reduce((s, v) => s + v, 0) / a.length;
const show = (name, a) => console.log(
  `${name.padEnd(22)} mean ${(100*mean(a)).toFixed(1)}%   p50 ${(100*pct(a,0.5)).toFixed(1)}   p10 ${(100*pct(a,0.1)).toFixed(1)}   p01 ${(100*pct(a,0.01)).toFixed(1)}   worst ${(100*Math.min(...a)).toFixed(1)}`);
console.log(`${sine.length} waveforms (${bad} unreadable) in ${((Date.now()-t0)/1000).toFixed(1)} s\n`);
show('sine basis only', sine);
show('band limit K=64', band);
const under = (a, t) => (100 * a.filter(v => v < t).length / a.length).toFixed(1);
console.log(`\nkeeping under 90%: sine ${under(sine, 0.9)}% of the corpus, band limit ${under(band, 0.9)}%`);
console.log(`keeping under 75%: sine ${under(sine, 0.75)}%, band limit ${under(band, 0.75)}%`);

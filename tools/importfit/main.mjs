/* importfit — what the import representation costs, over a whole corpus.
 *
 *   node tools/importfit/main.mjs <dir> [sample]
 *
 * Run through web/link.js's own wavToCycle and cycleToNode, deliberately: this
 * measures the path the page actually uses, not a second implementation of it
 * that could be right while the page is wrong.
 *
 * Everything here is a fraction of the *file's total energy*, one denominator
 * throughout. The first version of this tool did not do that — it put
 * cycleToNode's `fit`, which is a fraction of *in-band* energy, beside a band
 * fraction of total energy and compared their percentiles, which is not a
 * comparison. Name the denominator or do not print the number.
 *
 *   in band          energy at harmonics 1..K over the file's total. The
 *                    ceiling for any representation at this K, phase or no
 *                    phase.
 *   kept now         the sine half after the rotation search, over the same
 *                    total. What import actually keeps today.
 *   corr now         how much the rendered wave looks like the file:
 *                    sqrt(kept/total) by Parseval, checked against the direct
 *                    time-domain correlation on sample waveforms and exact to
 *                    four figures. This is the number that matters, because
 *                    energy flatters a projection badly — a wave can keep 97%
 *                    of its in-band energy and correlate 0.71 with itself.
 *   corr with phase  the same correlation if nothing but the band limit were
 *                    lost: sqrt(in band). What storing phase would buy.
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
/* Energies, all scaled the same way as the coefficients (2/N), so they are
   comparable term by term: the file's total, and the part inside 1..K. */
function energies(cyc, K = 64) {
  const N = cyc.length;
  let tot = 0;
  for (let i = 0; i < N; i++) tot += cyc[i] * cyc[i];
  tot = 2 * tot / N;
  let band = 0;
  for (let h = 1; h <= K; h++) {
    let re = 0, im = 0;
    for (let i = 0; i < N; i++) { const th = 2 * Math.PI * h * i / N; re += cyc[i] * Math.cos(th); im += cyc[i] * Math.sin(th); }
    re *= 2 / N; im *= 2 / N;
    band += re * re + im * im;
  }
  return { tot, band: Math.min(tot, band) };
}
const inband = [], keptNow = [], corrNow = [], corrPhase = [];
let bad = 0;
const t0 = Date.now();
for (const f of files) {
  try {
    const cyc = KYK.wavToCycle(new Uint8Array(fs.readFileSync(f)));
    const nd = KYK.cycleToNode(cyc, 64, 'shape');
    const e = energies(cyc);
    if (!(e.tot > 0)) { bad++; continue; }
    let keep = 0;
    for (let h = 0; h < nd.mags.length; h++) keep += nd.mags[h] * nd.mags[h];
    const ib = e.band / e.tot, kn = Math.min(1, keep / e.tot);
    inband.push(ib); keptNow.push(kn);
    corrNow.push(Math.sqrt(kn)); corrPhase.push(Math.sqrt(ib));
  } catch { bad++; }
}
const pct = (a, q) => { const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(q * s.length))]; };
const mean = a => a.reduce((s, v) => s + v, 0) / a.length;
const show = (name, a) => console.log(
  `${name.padEnd(24)} mean ${(100*mean(a)).toFixed(1)}   p50 ${(100*pct(a,0.5)).toFixed(1)}   p10 ${(100*pct(a,0.1)).toFixed(1)}   p01 ${(100*pct(a,0.01)).toFixed(1)}`);
console.log(`${keptNow.length} waveforms (${bad} skipped) in ${((Date.now()-t0)/1000).toFixed(1)} s`);
console.log(`all figures are percentages of the file's own total energy\n`);
show('in band (the ceiling)', inband);
show('kept now', keptNow);
console.log('');
show('corr now', corrNow);
show('corr with phase', corrPhase);
const under = (a, t) => (100 * a.filter(v => v < t).length / a.length).toFixed(1);
console.log(`\ncorrelating under 0.90: now ${under(corrNow, 0.9)}% of the corpus, with phase ${under(corrPhase, 0.9)}%`);
console.log(`correlating under 0.75: now ${under(corrNow, 0.75)}%, with phase ${under(corrPhase, 0.75)}%`);

/* pagecheck — the page must survive drawing a frame.
 *
 * This exists because it did not. The de-tinting pass removed `const hue` from
 * drawSound and replaced two of the three places that used it; the third was
 * in the spectrum bars. Every frame threw ReferenceError, and the draw loop
 * rescheduled itself *after* the draws, so the loop stopped for good: the last
 * good frame stayed painted while click handlers, on their own stack, went on
 * sending commands to the module quite happily. It looked like a hung link
 * rather than a dead renderer.
 *
 * It shipped because the check for leftover references was
 *   grep "hueOf\|\${hue}\|hue + 40"
 * and in a basic regular expression `{` and `}` are interval syntax, so the
 * pattern matched nothing and the empty output read as "all gone". A grep for
 * absence is only evidence if the pattern is known to match when the thing is
 * present. Running the code is evidence; a search that silently failed is not.
 *
 * So: load link.js and the real page script against a stubbed DOM, then call
 * drawSpace, drawSound and drawStatus over a spread of telemetry shapes. The
 * individual draws are called rather than frame(), because frame() now catches
 * on purpose and would hide exactly what this is looking for.
 *
 * Not a rendering test. It cannot see whether anything is in the right place,
 * only whether the code runs at all. That is the failure that costs a session.
 */
import fs from 'fs';
import path from 'path';
const ROOT = process.argv[2] || new URL('..', import.meta.url).pathname;

const IDS = 'axes btnBridge btnClose btnDrawer btnSerial drawer modinfo msg sBlock sCpu sCpuWrap sF0 sKcut sLink sMod sSpace sSpread shadeChips sound space trailChips worldCap worldChips worldNote morphSel cardSel cardLoad cardScan cardState morphState wavIn wavPick wavMode wavName wavSend wavSave wavClear wavState muteChips morphAim morphDirect'.split(' ');
const calls = [];
const ctx2d = new Proxy({}, {
  get(_, k) {
    if (k === 'measureText') return t => ({ width: String(t).length * 6 });
    if (k === 'createImageData') return (w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h });
    if (k === 'canvas') return mkEl('canvas');
    return (...a) => { calls.push(k); return undefined; };
  },
  set() { return true; },
});
function mkEl(tag) {
  const el = {
    tagName: tag, style: {}, dataset: {}, children: [], className: '', textContent: '', title: '',
    classList: { add(){}, remove(){}, toggle(){}, contains(){ return false; } },
    disabled: false, value: '', checked: false, onclick: null, oninput: null, onchange: null,
    selectedIndex: -1,
    get options() { return this.children; },
    width: 300, height: 300, clientWidth: 300, clientHeight: 300,
    appendChild(c) { this.children.push(c); return c; },
    removeChild() {}, remove() {}, setAttribute() {}, getAttribute() { return null; },
    addEventListener() {}, removeEventListener() {}, focus() {}, click() {},
    getContext: () => ctx2d,
    getBoundingClientRect: () => ({ width: 300, height: 300, left: 0, top: 0 }),
    querySelector: () => null, querySelectorAll: () => [],
    get firstChild() { return this.children[0] || null; },
    set innerHTML(v) { this.children.length = 0; },
    get innerHTML() { return ''; },
    parentElement: null,
  };
  el.parentElement = { getBoundingClientRect: () => ({ width: 300, height: 300 }) };
  return el;
}
const els = Object.fromEntries(IDS.map(i => [i, mkEl('div')]));
/* Any id the page asks for that we did not declare is a typo or a stale
   reference, and returning a fresh div for it hides that. */
const unknownIds = new Set();
globalThis.document = {
  getElementById: id => { if (!els[id]) unknownIds.add(id); return els[id] || mkEl('div'); },
  createElement: mkEl, addEventListener() {},
  body: mkEl('body'), documentElement: mkEl('html'),
};
globalThis.window = globalThis;
Object.defineProperty(globalThis, 'navigator', { value: { serial: null }, configurable: true, writable: true });
globalThis.location = { protocol: 'file:', hostname: '', href: '' };
globalThis.performance = { now: () => Date.now() };
globalThis.devicePixelRatio = 1;
globalThis.requestAnimationFrame = () => 0;
globalThis.addEventListener = () => {};
globalThis.setTimeout = (f) => 0;
globalThis.WebSocket = function () {};

/* link.js first: it publishes window.KYK */
new Function(fs.readFileSync(path.join(ROOT, 'web/link.js'), 'utf8'))();

/* the page script, with a probe hook spliced in before the IIFE closes */
let src = fs.readFileSync(path.join(ROOT, 'web/index.html'), 'utf8');
src = src.slice(src.indexOf('<script>\n(() => {') + 8);
src = src.slice(0, src.indexOf('\n</script>'));
const hook = `\nglobalThis.__probe = { frame, drawSpace, drawSound, drawStatus, parsePanel, setTel: v => { tel = v; }, setBasis: b => { basis = b; }, setPanel: p => { panel = p; }, onTelemetry, imported, worldName, importedBlob, exportImported };\n`;
src = src.replace(/\}\)\(\);\s*$/, hook + '})();');
new Function(src)();
const P = globalThis.__probe;

/* a telemetry object shaped exactly like parseTelemetry's output */
function tel(over = {}) {
  const n = over.n ?? 4, planes = n * (n - 1) / 2, k = 64, p = 8;
  const f = (v) => Float32Array.from({ length: n }, () => v);
  return Object.assign({
    block: 1000, f0: 110, n, k, kcut: 64, p, planes, flags: 7,
    stereo: false, spreadPlane: 0, spread: 0,
    ctl: f(0.5), centre: f(0.5), posL: f(0.5), posR: f(0.5),
    angle: Float32Array.from({ length: planes }, () => 0.1),
    payload: Float32Array.from({ length: p }, () => 0.3),
    mags: Uint8Array.from({ length: k }, (_, i) => 200 - i * 2),
    frame: Int8Array.from({ length: 256 }, (_, i) => Math.round(100 * Math.sin(i / 8))),
    kepler: { running: false, plane: 0, x: 0, y: 0, rush: 0 },
    couple: 0, lock: 0, sharp: 0, bodies: 1, kepXY: [], page: 0,
    morph: 0, morphWorld: 0xFF, mute: 0, aimed: false,
    pots: [0.1, 0.3, 0.5, 0.7, 0.9, 1.0], bytes: 512,
  }, over);
}
const CASES = [
  ['no telemetry at all', null],
  ['mono, centred', tel()],
  ['stereo', tel({ stereo: true, spread: 0.05, posR: Float32Array.from({length:4},()=>0.55) })],
  ['kepler running', tel({ kepler: { running: true, plane: 0, x: 0.1, y: 0.05, rush: 0.4 }, couple: 0.5, lock: 0.7 })],
  ['clamped past the edge', tel({ centre: Float32Array.from([1.4, -0.6, 0.5, 0.5]), posL: Float32Array.from([1, 0, 0.5, 0.5]) })],
  ['stereo + clamped + kepler', tel({ stereo: true, spread: 0.05, couple: 0.6, lock: 0.4,
      centre: Float32Array.from([1.4, -0.6, 0.5, 0.5]), posL: Float32Array.from([1, 0, 0.5, 0.5]),
      posR: Float32Array.from([1, 0.05, 0.5, 0.5]), kepler: { running: true, plane: 2, x: 0.2, y: -0.1, rush: 0.9 } })],
  ['n=6', tel({ n: 6 })],
  ['kepler with company', tel({ couple: 0.4, lock: 0.3,
      kepler: { running: true, plane: 1, x: 0.18, y: -0.07, rush: 0.6 },
      bodies: 8, kepXY: [[-0.12, 0.2], [0.05, -0.25], [0.3, 0.11], [-0.28, -0.04],
                         [0.02, 0.31], [-0.19, -0.22], [0.24, -0.15]] })],
  ['bodies claimed but positions missing', tel({ bodies: 4, kepXY: [],
      kepler: { running: true, plane: 0, x: 0.1, y: 0.1, rush: 0.5 } })],
  ['no mags / no frame', tel({ mags: null, frame: null, flags: 4 })],
  ['morphing to a named world', tel({ morph: 0.42, morphWorld: 9 })],
  ['morphing to a world index nobody has', tel({ morph: 1.0, morphWorld: 200 })],
  ['motions muted', tel({ mute: 0x8005 })],
  ['morph aimed', tel({ morph: 0.6, morphWorld: 9, aimed: true })],
  ['no knob positions on the wire', tel({ pots: null })],
  ['every motion muted', tel({ mute: 0xFFFF })],
];

/* The panel mirror, from a descriptor shaped the way the SDK emits one
   (framework/src/host_link/descriptor.cpp): a pager component carrying
   pages/pots/pageNames and a flat fields array keyed by page and pot. */
const DESC = { components: [
  { id: 'pager', type: 'pager', pages: 6, pots: 6,
    pageNames: ['Play', 'Rotate', 'Stereo', 'Orbit', 'Kepler', 'Couple'],
    pageColors: ['#67e8f9', '#fca5a5', '#a5b4fc', '#fde068', '#9ae6b4', '#f0a0d8'],
    fields: (() => {
      const names = [['Coarse','Fine','Position 0','Position 1','Position 2','Position 3'],
                     ['Angle 0,1','Angle 0,2','Angle 0,3','Angle 1,2','Angle 1,3','Angle 2,3'],
                     ['Spread','Stereo plane','Morph','Render divider','Level','CV out A depth'],
                     ['Orbit 0,1','Orbit 0,2','Orbit 0,3','Orbit 1,2','Orbit 1,3','Orbit 2,3'],
                     ['Gravity','Eccentricity','Orbit plane','Softening','Damping','Radius'],
                     ['Coupling','Reach','Rate','Bodies','Company', null]];
      const out = [];
      for (let p = 0; p < 6; p++) for (let q = 0; q < 6; q++)
        if (names[p][q]) out.push({ id: `f${p}.${q}`, name: names[p][q], page: p, pot: q,
                                    off: (p*6+q)*4, type: 'f32', def: 0.5, disp: { kind: 'norm' } });
      return out;
    })() },
] };
let bad = 0;
/* The chip rows must actually be populated at load.
 *
 * This exists because renderTrailChips() was spliced in next to the wrong one
 * of six calls to renderWorldBar() — the one inside the disconnect handler —
 * so the tail controls only appeared after disconnecting, and every "survived"
 * line below printed happily while the row sat empty. Not throwing is not the
 * same as having drawn anything. */
for (const [id, min] of [['worldChips', 0], ['shadeChips', 3], ['trailChips', 4]]) {
  const n = els[id].children.length, ok = n >= min;
  if (!ok) bad++;
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} #${id} populated at load (${n} children, want >= ${min})`);
}
/* Export: the saved file must be the arrangement you placed, under the name
 * you typed.
 *
 * Strictly this is a behaviour test in a harness whose job is "does the page
 * survive drawing a frame", and it is here because this is the only harness
 * that runs the page's own script. The failure it guards is worth the
 * trespass: an export that silently writes something other than what was sent
 * to the module produces a file that sounds wrong, months later, with nothing
 * to trace it back to. The coefficients are compared exactly, because the
 * format chose f32 over the u8 log encoding precisely so it could round-trip.
 */
{
  const NODE_MAGS = k => Float32Array.from({ length: 64 }, (_, i) => k / (i + 1));
  const want = [NODE_MAGS(0.5), NODE_MAGS(-0.25)];
  P.imported.length = 0;
  want.forEach((mags, i) => P.imported.push({ name: 'w' + i, mags, fit: 1 }));

  const downloads = [];
  let lastBlob = null;
  const realURL = globalThis.URL;
  globalThis.URL = {
    createObjectURL(b) { lastBlob = b; return 'blob:stub/' + downloads.length; },
    revokeObjectURL() {},
  };
  globalThis.document.createElement = tag => {
    const el = mkEl(tag);
    if (tag === 'a') el.click = () => downloads.push({ name: el.download, blob: lastBlob });
    return el;
  };

  async function save(typed) {
    downloads.length = 0;
    els.wavName.value = typed;
    P.exportImported();
    const d = downloads[downloads.length - 1];
    return d ? { file: d.name, bytes: new Uint8Array(await d.blob.arrayBuffer()) } : null;
  }
  /* the module's own reader, in miniature (core/kyk_userworld.h) */
  function parseKykw(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const n = b[6], k = b[7], count = b[8];
    const name = new TextDecoder().decode(b.subarray(16, 32)).replace(/\0+$/, '');
    const nodes = [];
    let at = 32;
    for (let i = 0; i < count; i++) {
      const pos = [], mags = new Float32Array(k);
      for (let a = 0; a < n; a++) { pos.push(dv.getFloat32(at, true)); at += 4; }
      for (let h = 0; h < k; h++) { mags[h] = dv.getFloat32(at, true); at += 4; }
      nodes.push({ pos, mags });
    }
    return { magic: dv.getUint32(0, true), ver: dv.getUint16(4, true), n, k, count,
             sine: (b[9] & 1) === 1, sigma: dv.getFloat32(10, true), name, nodes, end: at };
  }
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };

  const got = await save('my world');
  if (!got) { bad++; console.log('  FAIL export produced no download at all'); }
  else {
    const w = parseKykw(got.bytes);
    T(got.file === 'my-world.kykw', `filename is the name, made safe (${got.file})`);
    T(w.magic === 0x574B594B && w.ver === 1, 'magic KYKW, version 1');
    T(w.name === 'my world', `header keeps the name as typed (${JSON.stringify(w.name)})`);
    T(w.n === 4 && w.k === 64 && w.count === 2 && w.sine, 'geometry: 4-D, 64 harmonics, 2 nodes, sine phase');
    T(w.end === got.bytes.length, `declared geometry accounts for every byte (${got.bytes.length})`);
    /* the same bytes the module would be sent, since both call importedBlob */
    const sent = P.importedBlob();
    T(sent.length === got.bytes.length && sent.every((v, i) => v === got.bytes[i]),
      'the file is byte-for-byte what send would put on the wire');
    let exact = true;
    for (let i = 0; i < want.length; i++)
      for (let h = 0; h < 64; h++) if (w.nodes[i].mags[h] !== want[i][h]) exact = false;
    T(exact, 'every coefficient survives the round trip exactly');
    /* placement is the 24-cell's first two vertices, not the cube centre */
    const off = w.nodes.map(nd => nd.pos.filter(v => Math.abs(v - 0.5) > 1e-6).length);
    T(off.every(c => c === 2), `each node sits on a 24-cell vertex (${off.join(',')} axes off centre)`);
  }

  const plain = await save('');
  T(plain && plain.file === 'import.kykw' && parseKykw(plain.bytes).name === 'import',
    'an unnamed world saves as import.kykw rather than .kykw');
  const foreign = await save(' ≈ ');
  T(foreign && foreign.file === 'import.kykw' && parseKykw(foreign.bytes).name === 'import',
    'a name the char[16] header cannot hold falls back rather than writing mojibake');
  const punct = await save('///');
  T(punct && punct.file === 'import.kykw' && parseKykw(punct.bytes).name === '///',
    'a name that is all punctuation keeps the header but not the filename');
  /* Both halves, because buildUserWorld caps the header at sixteen on its own:
     checking only the header passes whether or not the page truncates, and the
     property worth having is that the file on the card and the name in the
     module's list are the same name. */
  const longName = await save('a very long world name indeed');
  T(longName && parseKykw(longName.bytes).name === 'a very long worl'
    && longName.file === 'a-very-long-worl.kykw',
    `a long name is cut to sixteen bytes, and the file is named to match (${longName && longName.file})`);

  P.imported.length = 0;
  downloads.length = 0;
  P.exportImported();
  T(downloads.length === 0, 'exporting nothing saves nothing');

  globalThis.URL = realURL;
  globalThis.document.createElement = mkEl;
  els.wavName.value = '';
}
console.log('');
if (unknownIds.size) { bad++; console.log(`  FAIL page asked for undeclared ids: ${[...unknownIds].join(', ')}`); }
console.log('');

/* the descriptor parse, then every case again with a panel present */
const parsed = P.parsePanel(DESC);
{
  const ok = parsed && parsed.pages === 6 && parsed.pots === 6
             && parsed.grid[4][0] && parsed.grid[4][0].name === 'Gravity'
             && parsed.grid[5][5] === null;
  if (!ok) bad++;
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} descriptor pager block parses (found ${
    parsed ? parsed.pages + ' pages x ' + parsed.pots + ' pots' : 'nothing'}, gap at Couple P6 kept null)`);
}
console.log('');

for (const [name, t] of CASES) {
  P.setTel(t);
  try {
    if (t) P.onTelemetry(t);
    /* the individual draws, not frame(), which now swallows throws on purpose */
    P.setPanel(null);
    P.drawSpace(); P.drawSound(); P.drawStatus();
    /* and again with the panel mirror live, on every pager page plus one out
       of range, since tel.page comes off the wire and is not to be trusted */
    P.setPanel(parsed);
    for (const pg of [0, 1, 4, 5, 99]) { P.setTel(t ? { ...t, page: pg } : t); P.drawSound(); }
    P.setTel(t);
    P.frame();
    console.log(`  ok    ${name}`);
  } catch (e) {
    bad++;
    console.log(`  THROW ${name}\n          ${e.constructor.name}: ${e.message}`);
    const line = (e.stack || '').split('\n').find(l => /anonymous|Function/.test(l));
    if (line) console.log(`          ${line.trim()}`);
  }
}
console.log(bad ? `\npagecheck: ${bad} of ${CASES.length} cases throw` : '\npagecheck: every case survived');
process.exit(bad ? 1 : 0);

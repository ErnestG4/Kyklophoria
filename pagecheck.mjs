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

const IDS = 'axes planeAxes plane inspect strip tabPlay tabBuild playMain buildMain worldbar btnBridge btnClose btnDrawer btnSerial drawer modinfo msg sBlock sCpu sCpuWrap sF0 sKcut sLink sMod sSpace sSpread shadeChips sound space trailChips worldCap worldChips worldNote morphSel cardSel cardLoad cardScan cardState morphState wavIn wavPick wavMode wavName wavSend wavSave wavPlace wavClear wavState muteChips morphAim morphDirect'.split(' ');
const calls = [];
/* Where the page asked for a mark at a coordinate that is not a number.
 *
 * A stub canvas swallows this and so does a real one: `arc(NaN, ...)` is not
 * an error anywhere, it simply draws nothing, which is indistinguishable from
 * a mark that is off screen or behind something. The page reads positions off
 * the wire and off imported files, so "this index does not exist" reaches the
 * drawing code as undefined and arrives here as NaN — a whole class of bug
 * whose only symptom is something quietly missing from the picture. */
const nanDraws = [];
const GEOM = new Set(['arc', 'moveTo', 'lineTo', 'fillRect', 'strokeRect', 'rect', 'drawImage',
                      'fillText', 'strokeText', 'arcTo', 'quadraticCurveTo', 'bezierCurveTo']);
const ctx2d = new Proxy({}, {
  get(_, k) {
    if (k === 'measureText') return t => ({ width: String(t).length * 6 });
    if (GEOM.has(k)) return (...a) => {
      calls.push(k);
      if (a.some(v => typeof v === 'number' && !Number.isFinite(v)))
        nanDraws.push(`${k}(${a.map(v => typeof v === 'number' ? v : typeof v).join(', ')})`);
    };
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
const hook = `\nglobalThis.__probe = { frame, drawSpace, drawSound, drawStatus, parsePanel, setTel: v => { tel = v; }, setBasis: b => { basis = b; }, setPanel: p => { panel = p; }, onTelemetry, imported, worldName, importedBlob, exportImported, place, pickNode, moveNodeTo, placeOnCell, view, setAxes: v => { axes = v; }, syncPlacement, setLink: v => { link = v; }, drawPlane, drawInspect, renderStrip, setView, planeBoxes, boxAt, selectNode };\n`;
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
/* The two views are two views: one is shown, the other is not, and the world
 * bar belongs to the one that plays. Worth asserting rather than eyeballing —
 * a tab that shows both at once is a layout bug you only see at one window
 * size, and a tab that shows neither looks like a crash.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  /* This one is a grep, and a grep is weak evidence — but the thing it guards
     cannot be reached from here at all. There is no CSS in this harness, so
     `hidden` being beaten by an id rule is invisible to every check below:
     the properties would all read correctly while the page rendered both
     views on top of each other. Asserting the override exists is the most
     this harness can honestly do; the line it looks for was verified to be
     missing before it was added. */
  const css = fs.readFileSync(path.join(ROOT, 'web/index.html'), 'utf8');
  T(/\[hidden\]\s*\{[^}]*display:\s*none\s*!important/.test(css),
    'the stylesheet makes [hidden] beat the id rules that set display');

  P.setView('build');
  T(els.playMain.hidden && !els.buildMain.hidden, 'build shows the builder and hides the instrument');
  T(els.worldbar.hidden, 'and hides the world bar, which is all play-side');
  P.setView('play');
  T(!els.playMain.hidden && els.buildMain.hidden && !els.worldbar.hidden,
    'play puts the instrument back');

  /* the strip is the answer to "what did I just import", so it has to be there */
  P.imported.length = 0;
  P.renderStrip();
  T(els.strip.children.length === 1, 'an empty set says so rather than showing nothing');
  for (let i = 0; i < 3; i++)
    P.imported.push({ name: 'w' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
  P.renderStrip();
  T(els.strip.children.length === 3, `one card per waveform (${els.strip.children.length})`);
}
console.log('');

/* Placement: a drag must move the node you grabbed, on the two axes that
 * square is drawing, and leave the others alone.
 *
 * That last clause is the whole design and the easiest thing to get silently
 * wrong — a placement editor that quietly rewrites the axes it is not showing
 * would look right on screen and put the node somewhere nobody chose. The
 * pixel geometry is inverted from the squares the view actually published, so
 * this also fails if the drawing and the pointer's idea of it drift apart.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  P.imported.length = 0;
  for (let i = 0; i < 6; i++)
    P.imported.push({ name: 'node' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5] });
  P.placeOnCell();
  P.setAxes([0, 1]);
  P.setTel(null);
  P.setView('build');
  P.drawPlane();                       /* which is what publishes the squares */

  const boxes = P.planeBoxes;
  T(boxes.length === 2, `the plane publishes a square and its complement (${boxes.length})`);
  const [main, inset] = boxes;
  T(main && main.ax === 0 && main.ay === 1, 'the main square draws the axis pair you chose');
  /* With four dimensions the complement is unique, which is the whole reason
     the inset can finish a placement rather than merely hint at one. */
  T(inset && inset.ax === 2 && inset.ay === 3,
    `and the inset draws the two axes that are left (${inset && inset.ax}×${inset && inset.ay})`);
  /* Fully inside, all four edges. "Its corner is past the main square's
     corner" was the first version of this and it passed with the inset pushed
     clean off the side of the canvas. */
  T(inset.S < main.S
    && inset.ox >= main.ox && inset.oy >= main.oy
    && inset.ox + inset.S <= main.ox + main.S
    && inset.oy + inset.S <= main.oy + main.S,
    'the inset sits wholly inside the square it complements');

  const at = (b, u, v) => [b.ox + u * b.S, b.oy + (1 - v) * b.S];
  const pos = i => [...P.imported[i].pos];

  /* grab the node that is drawn where we press */
  const target = 3, before = pos(target);
  T(P.pickNode(main, ...at(main, before[0], before[1])) === target,
    'a press on a node picks that node');
  T(P.pickNode(main, ...at(main, 0.5, 0.02)) === -1, 'a press on empty space picks nothing');

  /* the drag itself */
  const others = [0, 1, 2, 4, 5].map(pos);
  P.moveNodeTo(target, main, ...at(main, 0.25, 0.75));
  const moved = pos(target);
  T(Math.abs(moved[0] - 0.25) < 1e-6 && Math.abs(moved[1] - 0.75) < 1e-6,
    `the node lands where the pointer did (${moved[0].toFixed(3)} ${moved[1].toFixed(3)})`);
  T(moved[2] === before[2] && moved[3] === before[3],
    'and the two axes that are not on that square are untouched');
  T([0, 1, 2, 4, 5].every((n, j) => pos(n).every((v, a) => v === others[j][a])),
    'no other node moved');

  /* the other half of the space, without leaving the view */
  P.moveNodeTo(target, inset, ...at(inset, 0.1, 0.9));
  const deep = pos(target);
  T(Math.abs(deep[2] - 0.1) < 1e-6 && Math.abs(deep[3] - 0.9) < 1e-6,
    'a drag in the inset moves the other two axes');
  T(deep[0] === moved[0] && deep[1] === moved[1],
    'and leaves the first two where they were put');

  /* and the same thing the long way round, by switching the pair */
  P.setAxes([2, 3]);
  P.drawPlane();
  T(P.planeBoxes[0].ax === 2 && P.planeBoxes[1].ax === 0,
    'switching the pair swaps the square and its inset');
  P.moveNodeTo(target, P.planeBoxes[0], ...at(P.planeBoxes[0], 0.4, 0.6));
  T(Math.abs(pos(target)[2] - 0.4) < 1e-6 && Math.abs(pos(target)[3] - 0.6) < 1e-6,
    'and the main square then moves what the inset used to');
  P.setAxes([0, 1]);
  P.drawPlane();

  /* a press where the squares overlap belongs to the one on top */
  const over = P.boxAt(...at(P.planeBoxes[1], 0.5, 0.5));
  T(over === P.planeBoxes[1], 'a press where they overlap lands in the inset, which is drawn on top');
  T(P.boxAt(...at(P.planeBoxes[0], 0.1, 0.9)) === P.planeBoxes[0], 'and elsewhere in the main square');
  T(P.boxAt(-50, -50) === null, 'and nowhere at all outside both');

  /* the cube is the reachable space, so a drag cannot leave it */
  P.moveNodeTo(target, main, main.ox - 400, main.oy - 400);
  const out = pos(target);
  T(out.every(v => v >= 0 && v <= 1), `a drag past the edge clamps (${out[0].toFixed(2)} ${out[1].toFixed(2)})`);

  /* re-place is the way back */
  P.placeOnCell();
  const home = pos(target);
  T(home.filter(v => Math.abs(v - 0.5) > 1e-6).length === 2,
    're-place puts every node back on a 24-cell vertex');
  T(P.place.sel === -1, 'and drops the selection, which no longer means anything');

  /* A plane the nodes have no coordinates on. Reachable: the module reports
     six dimensions, you switch to the (4,5) plane, and your imported set is
     four-dimensional. Nothing is there to grab and nothing can be put there. */
  P.setAxes([4, 5]);
  P.setTel(tel({ n: 6 }));
  P.drawPlane();
  const far = P.planeBoxes[0];
  const frozen = pos(target);
  T(P.pickNode(far, ...at(far, 0.5, 0.5)) === -1, 'nothing is grabbable on a plane the nodes do not have');
  P.moveNodeTo(target, far, ...at(far, 0.5, 0.5));
  T(pos(target).every((v, a) => v === frozen[a]), 'and nothing can be dropped onto one');
  P.setAxes([0, 1]);
  P.setTel(null);
  P.drawPlane();

  /* what gets saved is what was placed, which is the point of all of it */
  P.moveNodeTo(0, P.planeBoxes[0], ...at(P.planeBoxes[0], 0.8, 0.3));
  const blob = P.importedBlob();
  const dv = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  T(Math.abs(dv.getFloat32(32, true) - 0.8) < 1e-6 && Math.abs(dv.getFloat32(36, true) - 0.3) < 1e-6,
    'the world carries the moved position, not the vertex it started on');
  P.setView('play');
}
console.log('');

/* Following: once the module holds the set, moving a node re-sends it — but a
 * drag ends many times a second and a world is about 6.5 KB, so the sends must
 * collapse rather than queue. Driven through a stubbed transfer that can be
 * released by hand, because the thing being tested is the ordering.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const tick = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };
  const sends = [], pending = [];
  const realPut = globalThis.KYK.putWorld;
  globalThis.KYK.putWorld = (l, blob) => new Promise((res, rej) => {
    sends.push(blob.length); pending.push({ res, rej });
  });
  P.setLink({});                 /* only needs to be non-null; the send is stubbed */

  /* Not awaited: if this ever *does* send, the stub never resolves and awaiting
     it would hang the run instead of failing it. */
  P.place.sent = false;
  P.syncPlacement();
  await tick();
  T(sends.length === 0, 'moving a node sends nothing until the set has been sent once');

  P.place.sent = true;
  const first = P.syncPlacement();
  await tick();
  T(sends.length === 1, 'the first drop starts a send');
  P.syncPlacement(); P.syncPlacement(); P.syncPlacement();
  await tick();
  T(sends.length === 1, 'three more drops while it is in flight start nothing');
  pending.shift().res(true);
  await tick();
  T(sends.length === 2, 'and collapse into exactly one resend, not three');
  pending.shift().res(true);
  await first;
  T(sends.length === 2 && P.place.sent, 'which settles, still following');

  /* A link that has gone away must stop the following rather than retry into
     it: the alternative is a failure every time the hand moves. */
  const failing = P.syncPlacement();
  await tick();
  pending.shift().rej(new Error('link closed'));
  await failing;
  T(!P.place.sent, 'a send that fails stops the following rather than storming');
  T(els.msg.textContent.includes('stopped taking'), `and says so (${els.msg.textContent})`);

  globalThis.KYK.putWorld = realPut;
  P.setLink(null);
  els.msg.textContent = '';
}
console.log('');

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
  want.forEach((mags, i) => P.imported.push({ name: 'w' + i, mags, fit: 1, pos: [0.5, 0.5, 0.5, 0.5] }));
  P.placeOnCell();

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

/* Every draw case runs with imported nodes on the canvas and one of them
   selected, because that is now a state the space view can be in at any time.
   The case that matters is n=6: a four-dimensional set placed while the module
   reports six dimensions puts axes on screen that the nodes do not have, and
   the node layer has to say "not here" rather than draw at the origin or at
   NaN. Nothing throws either way — that is what nanDraws is for. */
{
  const wave = (k) => Float32Array.from({ length: 64 }, (_, h) => k / (h + 1));
  P.imported.length = 0;
  P.imported.push(
    { name: 'placed0', mags: wave(0.5), fit: 1, mode: 'shape', pos: [0.2, 0.8, 0.5, 0.35],
      render: wave(0.5), thumb: wave(0.5) },
    { name: 'a name quite a lot longer than the box', mags: wave(-0.3), fit: 0.62, mode: 'shape',
      pos: [0.35, 0.7, 0.5, 0.35], render: wave(-0.3), thumb: wave(-0.31) },
    { name: 'spectral', mags: wave(0.2), fit: 1, mode: 'spectrum', pos: [0.5, 0.6, 0.5, 0.35],
      render: wave(0.2), thumb: wave(0.9) },
    /* a node with nothing drawn for it: the inspector must not assume the
       traces exist, since only an import makes them */
    { name: 'bare', mags: wave(0.4), fit: 0.95, mode: 'shape', pos: [0.65, 0.5, 0.5, 0.35] },
    /* and one that is silent, where every peak is zero and a normaliser that
       divides by it would produce the NaN the watch below is looking for */
    { name: 'silent', mags: new Float32Array(64), fit: 0, mode: 'shape', pos: [0.8, 0.4, 0.5, 0.35],
      render: new Float32Array(64), thumb: new Float32Array(64) },
  );
  P.place.sel = 2;
}

let caseIdx = 0;
for (const [name, t] of CASES) {
  P.setTel(t);
  /* Every case is seen by both views, and the selection walks through the set
     (including nothing selected) so the inspector's branches — a poor fit, a
     spectrum-mode node, one with no traces drawn for it, a silent one, and no
     node at all — are all reached across the sweep rather than in theory. */
  P.place.sel = (caseIdx++ % (P.imported.length + 1)) - 1;
  try {
    if (t) P.onTelemetry(t);
    /* the individual draws, not frame(), which now swallows throws on purpose */
    P.setPanel(null);
    P.drawSpace(); P.drawSound(); P.drawStatus();
    P.setView('build'); P.drawPlane(); P.drawInspect(); P.renderStrip(); P.setView('play');
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
/* The same plane, drawn. A missing guard here throws nothing and paints
   nothing: it asks the canvas for a mark at NaN, which is why the watch below
   exists rather than a try/catch. */
P.setTel(tel({ n: 6 }));
P.setAxes([4, 5]);
try { P.drawSpace(); P.drawSound(); } catch (e) { bad++; console.log(`  THROW drawing the (4,5) plane: ${e.message}`); }
P.setAxes([0, 1]);

if (nanDraws.length) {
  bad++;
  const shown = [...new Set(nanDraws)].slice(0, 6);
  console.log(`\n  FAIL ${nanDraws.length} draw${nanDraws.length > 1 ? 's' : ''} at a coordinate that is not a number:`);
  for (const d of shown) console.log(`          ${d}`);
} else console.log('\n  ok   nothing was drawn at a coordinate that is not a number');

console.log(bad ? `\npagecheck: ${bad} of ${CASES.length} cases throw` : '\npagecheck: every case survived');
process.exit(bad ? 1 : 0);

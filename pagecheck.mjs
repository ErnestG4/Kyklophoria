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

const IDS = 'axes planeAxes plane inspect strip tabPlay tabBuild tabLib playMain buildMain libMain slots libPlay libTarget libNoTarget libEdit libFree libSave libCardSel libLoad libRescan libState worldbar btnBridge btnClose btnDrawer btnSerial drawer modinfo msg sBlock sCpu sCpuWrap sF0 sKcut sLink sMod sSpace sSpread shadeChips sound space trailChips worldCap worldChips worldNote morphSel cardSel cardLoad cardScan cardState morphState wavIn wavPick wavMode wavName wavSend wavSave wavPlace wavClear wavNew wavKeep wavState audPlay audA audB audC audState muteChips morphAim morphDirect fxA fxB fxState tourAdd tourDrop tourClear tourDiv tourStops tourState'.split(' ');
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
const arcLog = [], segLog = [];
const GEOM = new Set(['arc', 'moveTo', 'lineTo', 'fillRect', 'strokeRect', 'rect', 'drawImage',
                      'fillText', 'strokeText', 'arcTo', 'quadraticCurveTo', 'bezierCurveTo']);
const ctx2d = new Proxy({}, {
  get(_, k) {
    if (k === 'measureText') return t => ({ width: String(t).length * 6 });
    if (GEOM.has(k)) return (...a) => {
      calls.push(k);
      /* Arcs are kept with their arguments, because some claims are about
         where a mark is and not merely that nothing threw. The panel mirror's
         knobs are arcs from a fixed start angle, so what they are showing is
         recoverable from the end angle. */
      if (k === 'arc') arcLog.push(a);
      /* and the straight segments, which is how a pointer or a tick is drawn */
      if (k === 'moveTo' || k === 'lineTo') segLog.push([a[0], a[1]]);
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
    /* Listeners are kept rather than dropped, so a test can perform the
       gesture instead of calling the handler's insides. The drag on the
       placement canvas had no coverage at all while they were discarded: every
       check went at pickNode and moveNodeTo directly, and the handler that
       wires a pointer to them — which box was pressed, what is being dragged,
       when the send fires — was never run. */
    addEventListener(type, fn) { (this._on || (this._on = {}))[type] = (this._on[type] || []).concat(fn); },
    removeEventListener() {}, focus() {}, click() {},
    /* Pointer capture is what keeps a drag alive past the edge of the pane.
       Absent from this stub, a pointerdown on a node threw TypeError — the
       handler works in a browser and could not run here at all. */
    setPointerCapture() {}, releasePointerCapture() {},
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
/* Perform an event, the way a hand would. */
const fire = (el, type, props = {}) => {
  for (const fn of (el._on && el._on[type]) || [])
    fn({ clientX: 0, clientY: 0, pointerId: 1, preventDefault() {}, ...props });
};
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
const hook = `\nglobalThis.__probe = { frame, drawSpace, drawSound, drawStatus, parsePanel, setTel: v => { tel = v; }, setBasis: b => { basis = b; }, setPanel: p => { panel = p; }, onTelemetry, imported, worldName, importedBlob, exportImported, place, pickNode, moveNodeTo, placeOnCell, view, setAxes: v => { axes = v; }, syncPlacement, setLink: v => { link = v; }, drawPlane, drawInspect, renderStrip, renderLibrary, renderLib, refreshSlots, slotAction, keepBuildSet, freeSlot, setLibSel: v => { libSel = v; }, getLibSel: () => libSel, slotNodes, setWorlds: v => { worlds = v; }, playNodes, heldName: () => held && held.name, heldLost, renderImport, sendImported, renderFromMags, rotatedCycle, bandLimit, removeNode, refreshWorlds, disconnect, setCardList: v => { cardList = v; }, setView, planeBoxes, boxAt, selectNode, renderFx, fxSel, setSlots: v => { slots = v; }, renderTour, readTour, sendTour, tourAdd, tourName, getTour: () => tourStops, setTourLive: v => { tourLive = v; } };\n`;
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
    morph: 0, morphWorld: 0xFF, mute: 0, aimed: false, world: 0xFF, knobs: null,
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
  ['knob values but no pot positions', tel({ pots: null, knobs: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6] })],
  ['pots and values disagreeing, as they do after a page change',
    tel({ pots: [0.9, 0.9, 0.9, 0.9, 0.9, 0.9], knobs: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6] })],
  ['every motion muted', tel({ mute: 0xFFFF })],
];

/* The panel mirror, from a descriptor shaped the way the SDK emits one
   (framework/src/host_link/descriptor.cpp): a pager component carrying
   pages/pots/pageNames and a flat fields array keyed by page and pot. */
const DESC = { components: [
  /* Seven pages, the last of them three pots short: the World page is axes 4 and
     5 plus the world tour's division, glide and free-run, and nothing is padded
     out to six. Two pages with gaps in them, which is the shape the parser has
     to survive — it used to assume a full grid. */
  { id: 'pager', type: 'pager', pages: 7, pots: 6,
    pageNames: ['Play', 'Rotate', 'Stereo', 'Orbit', 'Kepler', 'Couple', 'World'],
    pageColors: ['#67e8f9', '#fca5a5', '#a5b4fc', '#fde068', '#9ae6b4', '#f0a0d8', '#f7c08a'],
    fields: (() => {
      const names = [['Coarse','Fine','Position 0','Position 1','Position 2','Position 3'],
                     ['Angle 0,1','Angle 0,2','Angle 0,3','Angle 1,2','Angle 1,3','Angle 2,3'],
                     ['Spread','Stereo plane','Morph','Render divider','Level','CV out A depth'],
                     ['Orbit 0,1','Orbit 0,2','Orbit 0,3','Orbit 1,2','Orbit 1,3','Orbit 2,3'],
                     ['Gravity','Eccentricity','Orbit plane','Softening','Damping','Radius'],
                     ['Coupling','Reach','Rate','Bodies','Company', null],
                     ['Position 4','Position 5','Tour division','Tour glide','Tour free-run', null]];
      const out = [];
      for (let p = 0; p < 7; p++) for (let q = 0; q < 6; q++)
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
/* The gesture itself: press, move, release.
 *
 * Everything else about placement is tested one function at a time, which
 * leaves the part a person actually performs — which square was pressed, what
 * is being dragged, when the send fires, what happens on a press that hits
 * nothing — running only in a browser. These go through the real listeners.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const sends = [];
  const realPut = globalThis.KYK.putWorld;
  globalThis.KYK.putWorld = async () => { sends.push(1); return true; };

  P.imported.length = 0;
  for (let i = 0; i < 4; i++)
    P.imported.push({ name: 'g' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
  P.placeOnCell();
  P.setAxes([0, 1]);
  P.setTel(null);
  P.setView('build');
  P.drawPlane();
  const cp = els.plane, [main, inset] = P.planeBoxes;
  const at = (b, u, v) => ({ clientX: b.ox + u * b.S, clientY: b.oy + (1 - v) * b.S });
  const pos = i => [...P.imported[i].pos];

  T((cp._on && cp._on.pointerdown || []).length > 0, 'the placement canvas is listening for a pointer');

  /* a press that lands on nothing */
  P.selectNode(2);
  fire(cp, 'pointerdown', at(main, 0.5, 0.02));
  T(P.place.sel === -1 && P.place.drag === -1, 'a press on empty space picks nothing up');

  /* the drag */
  const start = pos(1);
  fire(cp, 'pointerdown', at(main, start[0], start[1]));
  T(P.place.sel === 1 && P.place.drag === 1, 'a press on a node takes hold of it');
  T(P.place.box === main, 'and remembers which square it was pressed in');
  fire(cp, 'pointermove', at(main, 0.3, 0.7));
  T(Math.abs(pos(1)[0] - 0.3) < 1e-6 && Math.abs(pos(1)[1] - 0.7) < 1e-6, 'moving drags it');
  T(sends.length === 0, 'and nothing is sent while the hand is still down');
  fire(cp, 'pointerup', {});
  T(P.place.drag === -1 && P.place.box === null, 'letting go lets go');
  T(P.place.sel === 1, 'and leaves it picked, because that is what you are looking at');

  /* a move with nothing held must not move anything */
  const idle = pos(1);
  fire(cp, 'pointermove', at(main, 0.9, 0.9));
  T(pos(1).every((v, a) => v === idle[a]), 'a pointer crossing the canvas with nothing held moves nothing');

  /* the same gesture in the inset moves the axes the main square cannot */
  fire(cp, 'pointerdown', at(inset, pos(1)[2], pos(1)[3]));
  T(P.place.box === inset, 'a press in the inset takes hold there');
  fire(cp, 'pointermove', at(inset, 0.2, 0.8));
  T(Math.abs(pos(1)[2] - 0.2) < 1e-6 && Math.abs(pos(1)[3] - 0.8) < 1e-6, 'and drags the other two axes');
  T(Math.abs(pos(1)[0] - 0.3) < 1e-6, 'while the first two stay where the main square put them');
  fire(cp, 'pointerup', {});

  /* a cancelled drag is a finished drag */
  fire(cp, 'pointerdown', at(main, pos(1)[0], pos(1)[1]));
  fire(cp, 'pointercancel', {});
  T(P.place.drag === -1, 'a cancelled pointer is not still holding something');

  /* and the drop sends, once the module is holding the set */
  P.place.sent = true;
  P.setLink({ request: async () => Uint8Array.of(0) });
  fire(cp, 'pointerdown', at(main, pos(0)[0], pos(0)[1]));
  fire(cp, 'pointermove', at(main, 0.6, 0.4));
  fire(cp, 'pointerup', {});
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(sends.length === 1, `the drop is what sends, and it sends once (${sends.length})`);

  globalThis.KYK.putWorld = realPut;
  P.setLink(null);
  P.heldLost();               /* this block sent one; the next starts from nothing */
  P.imported.length = 0;
  P.setView('play');
}
console.log('');

/* State: the play view shows what the module has, and every way that stops
 * being true.
 *
 * This is the check the feature needed and did not have. The play view drew
 * the *draft* — the set being edited on the other tab — so it claimed the
 * instrument contained whatever was on screen in the builder, including edits
 * that had never been sent and sets the module had never seen. A readout that
 * can be wrong about its own subject is worse than no readout, and nothing
 * here would have noticed: every existing check asked what the builder did,
 * and none asked what the other view said about it.
 *
 * There is no way to ask the module what it is holding, so the page keeps a
 * record of what it sent. The record is only as good as the rules for throwing
 * it away, which is what this exercises: one rule per way of losing it.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const realFetch = globalThis.KYK.fetchWorlds, realPut = globalThis.KYK.putWorld;
  let putOk = true, current = 9;
  globalThis.KYK.putWorld = async () => { if (!putOk) throw new Error('link closed'); return true; };
  globalThis.KYK.fetchWorlds = async () => ({
    current, count: 1, list: [{ index: 0, kind: 0, analytic: false, name: 'Braids', note: '' }],
  });
  P.setLink({ request: async () => Uint8Array.of(0), close: async () => {} });
  els.wavName.value = 'held';
  /* Establish the starting point rather than inherit one. A block that assumes
     the state a previous block happened to leave behind is a block that starts
     failing when the one above it changes, and the first version of this did
     exactly that. */
  P.heldLost();

  const draft = () => P.imported.map(w => [...w.pos]);
  const shown = () => P.playNodes().map(w => [...w.pos]);
  const fill = (n) => {
    P.imported.length = 0;
    for (let i = 0; i < n; i++)
      P.imported.push({ name: 'n' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                        pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
    P.placeOnCell();
  };

  fill(3);
  T(P.playNodes().length === 0, 'a set that was never sent draws nothing on the play view');

  await P.sendImported();
  T(P.place.sent && P.playNodes().length === 3, 'a sent set is what the play view draws');
  T(P.heldName() === 'held', `and it is recorded under the name it was sent as (${P.heldName()})`);

  /* the whole point: the two views can disagree, and the readout is the one
     that must not move until the module does */
  P.setView('build');
  P.drawPlane();
  const box = P.planeBoxes[0];
  P.moveNodeTo(0, box, box.ox + 0.9 * box.S, box.oy + 0.9 * box.S);
  T(draft()[0][0] !== shown()[0][0], 'editing the draft does not move the readout');
  T(shown().length === 3 && shown()[1][0] === draft()[1][0], 'the nodes nobody touched still agree');

  await P.syncPlacement();
  T(shown()[0][0] === draft()[0][0], 'and the send that follows the drop catches the readout up');

  /* every way of losing it */
  current = 9;
  await P.refreshWorlds();
  T(P.playNodes().length === 0 && !P.place.sent,
    'a refresh that reports a built-in is live drops the record');

  await P.sendImported();
  current = 0xFF;
  await P.refreshWorlds();
  T(P.playNodes().length === 3 && P.place.sent,
    'a refresh that still reports a user world keeps it');

  P.setCardList({ count: 1, names: ['someone-elses.kykw'] });
  els.cardSel.selectedIndex = 0;
  await els.cardLoad.onclick();
  T(P.playNodes().length === 0 && !P.place.sent,
    'loading a world from the card drops it — that is another user world, not ours');

  await P.sendImported();
  els.wavClear.onclick();
  T(P.imported.length === 0 && P.place.sel === -1, 'clear empties the draft');
  /* Deliberately not symmetrical: emptying the builder does not reach into the
     module and unload anything. The rings stay because the world is still
     there, which is the difference between a readout and a mirror of the
     editor. */
  T(P.playNodes().length === 3, 'and the module is still holding what it was sent');
  T(!P.place.sent, 'but there is nothing left to follow it with');

  fill(2);
  T(P.imported.length === 2 && P.playNodes().length === 3,
    'a new draft alongside a world the module still holds');

  await P.sendImported();
  putOk = false;
  P.syncPlacement();
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(P.playNodes().length === 0 && !P.place.sent,
    'a send that fails stops claiming the module has it');

  putOk = true;
  await P.sendImported();
  await P.disconnect();
  T(P.playNodes().length === 0 && !P.place.sent, 'and dropping the link drops it too');

  globalThis.KYK.fetchWorlds = realFetch;
  globalThis.KYK.putWorld = realPut;
  P.setLink(null);
  P.imported.length = 0;
  P.setView('play');
  els.wavName.value = '';
  els.msg.textContent = '';
}
console.log('');

/* The module says which world it is playing, in every frame, and the page has
 * to believe it over its own last instruction.
 *
 * Before this the live world was known only by asking for the list, and
 * nothing asked unprompted — so a world changed from the module's own panel
 * left the world bar naming the previous one and the play view drawing the
 * rings of a world that was no longer loaded, indefinitely.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const realFetch = globalThis.KYK.fetchWorlds, realPut = globalThis.KYK.putWorld;
  let fetched = 0;
  globalThis.KYK.putWorld = async () => true;
  globalThis.KYK.fetchWorlds = async () => {
    fetched++;
    return { current: 0xFF, count: 2, list: [
      { index: 0, kind: 0, analytic: false, name: 'Braids', note: '' },
      { index: 1, kind: 0, analytic: false, name: 'Stack', note: '' }] };
  };
  P.setLink({ request: async () => Uint8Array.of(0), close: async () => {} });
  P.heldLost();
  P.imported.length = 0;
  for (let i = 0; i < 2; i++)
    P.imported.push({ name: 'u' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
  P.placeOnCell();
  await P.sendImported();
  await P.refreshWorlds();
  T(P.playNodes().length === 2, 'the module is holding our set to begin with');

  /* a frame that still says a user world is live changes nothing */
  P.onTelemetry(tel({ world: 0xFF }));
  T(P.playNodes().length === 2, 'a frame reporting a user world leaves it alone');

  /* and one that names a built-in means somebody else changed it */
  fetched = 0;
  P.onTelemetry(tel({ world: 1 }));
  T(P.playNodes().length === 0 && !P.place.sent,
    'a frame naming a built-in drops the record — the panel can switch worlds too');
  T(fetched > 0, 'and asks for the list, so the name beside it catches up');

  /* noticed once, not sixty times a second */
  fetched = 0;
  for (let i = 0; i < 5; i++) P.onTelemetry(tel({ world: 1 }));
  T(fetched === 0, 'the same world arriving again asks nothing');
  /* let the refresh the change above started finish, or the next case starts
     with the in-flight guard already raised and measures nothing */
  for (let i = 0; i < 8; i++) await Promise.resolve();

  /* Two different worlds in successive frames, with the list still in flight.
     This is the only thing the in-flight guard does — the "same world again"
     case above never reaches it — so without this it is untested code. */
  {
    let inFlight = 0, release = null;
    globalThis.KYK.fetchWorlds = () => {
      inFlight++;
      return new Promise(r => { release = () => r({ current: 0xFF, count: 1,
        list: [{ index: 0, kind: 0, analytic: false, name: 'Braids', note: '' }] }); });
    };
    P.onTelemetry(tel({ world: 0 }));
    P.onTelemetry(tel({ world: 1 }));
    T(inFlight === 1, `a second change while the list is in flight does not start another (${inFlight})`);
    release();
    for (let i = 0; i < 8; i++) await Promise.resolve();
    P.onTelemetry(tel({ world: 0 }));
    T(inFlight === 2, 'and once it lands, the next change asks again');
  }

  /* firmware that predates the field must not read as "no world" */
  globalThis.KYK.fetchWorlds = async () => ({ current: 0xFF, count: 1,
    list: [{ index: 0, kind: 0, analytic: false, name: 'Braids', note: '' }] });
  await P.sendImported();
  for (let i = 0; i < 3; i++) P.onTelemetry(tel({ world: null }));
  T(P.playNodes().length === 2 && P.place.sent,
    'a frame from firmware too old to say is not read as a world change');

  globalThis.KYK.fetchWorlds = realFetch;
  globalThis.KYK.putWorld = realPut;
  P.setLink(null);
  P.heldLost();
  P.imported.length = 0;
}
console.log('');

/* The worlds tab: one library of every world, and what playing one does to the
 * readout.
 *
 * It used to be thirty-two empty slots, which on a fresh module is a grid of
 * nothing and reads as broken — and the built-ins lived in a chip row on another
 * tab, so "the list of worlds" existed in two places and neither was the list.
 * One grid now, the ones that ship then the ones you made, one selection and one
 * set of verbs.
 *
 * The module reports which slots are filled and which is playing; it does not
 * report what is *in* one. So the rings in the play view can only follow a slot
 * this page put there, and the one thing they must never do is keep drawing the
 * previous world.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const realFetch = globalThis.KYK.fetchSlots, realPut = globalThis.KYK.putSlot;
  let state = { count: 32, live: 0xFF, target: 0xFF, names: new Array(32).fill(null) };
  const stored = [], swapped = [];
  globalThis.KYK.fetchSlots = async () => JSON.parse(JSON.stringify(state));
  globalThis.KYK.putSlot = async (l, slot, blob) => { stored.push({ slot, len: blob.length }); state.names[slot] = 'built'; return true; };
  P.setLink({ request: async (type, body) => {
    if (type === globalThis.KYK.CMD.action && body && body[0] === globalThis.KYK.ACT.slotSwap)
      swapped.push({ a: body[1], b: body[2] });
    return Uint8Array.of(0);
  }, close: async () => {} });
  P.setWorlds({ current: 0, count: 3, list: [
    { index: 0, kind: 2, analytic: true, name: 'Crop', note: 'four components' },
    { index: 1, kind: 3, analytic: true, name: '24-cell', note: 'a waveform per vertex' },
    { index: 13, kind: 5, analytic: true, name: 'Lock', note: 'real waveforms' }] });
  P.heldLost();

  P.setView('worlds');
  T(els.playMain.hidden && els.buildMain.hidden && !els.libMain.hidden, 'the worlds tab shows only itself');
  await new Promise(r => setImmediate(r));

  /* The whole point of the pass: a fresh module is not a grid of nothing. */
  const tiles = [...els.slots.children];
  const heads = tiles.filter(c => c.className === 'libhead').length;
  const builtins = tiles.filter(c => /builtin/.test(c.className)).length;
  const slotTiles = tiles.filter(c => c.className.startsWith('slot') && !/builtin/.test(c.className)).length;
  T(builtins === 3, `the worlds that ship are listed (${builtins})`);
  T(slotTiles === 32, `and yours beside them (${slotTiles})`);
  T(heads === 2, 'under headings, so the two kinds are not one undifferentiated grid');

  /* nothing selected offers no verbs */
  P.setLibSel(null); P.renderLib();
  T(els.libPlay.disabled && els.libTarget.disabled && els.libEdit.disabled,
    'with nothing picked there is nothing to play, target or open');

  /* a world that ships can be played, targeted and opened — but not forgotten */
  P.setLibSel({ kind: 'builtin', i: 1 }); P.renderLib();
  T(!els.libPlay.disabled && !els.libTarget.disabled && !els.libEdit.disabled,
    'one that ships can be played, targeted and opened');
  T(els.libFree.disabled && els.libSave.disabled,
    'and cannot be forgotten or written to the card, because it is not yours');

  /* an empty slot offers nothing either */
  P.setLibSel({ kind: 'slot', i: 4 }); P.renderLib();
  T(els.libPlay.disabled && els.libFree.disabled, 'an empty slot offers nothing to do with it');

  /* keeping the build set: no slot number to pick, which was the broken part */
  P.imported.length = 0;
  for (let i = 0; i < 3; i++)
    P.imported.push({ name: 's' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.2 + 0.1 * i, 0.6, 0.5, 0.5], render: new Float32Array(64) });
  await P.keepBuildSet();
  T(stored.length === 1 && stored[0].slot === 0,
    `"add to worlds" takes the first free slot rather than asking for a number (${stored[0] && stored[0].slot})`);
  T(stored[0] && stored[0].len === 32 + 3 * 4 * (4 + 64), `as a whole world (${stored[0] && stored[0].len} bytes)`);
  T(!!P.slotNodes[0] && P.slotNodes[0].length === 3, 'and the page remembers where its nodes are');

  /* and it is now one of yours, with the verbs that implies */
  P.setLibSel({ kind: 'slot', i: 0 }); P.renderLib();
  T(!els.libFree.disabled && !els.libSave.disabled, 'a world of yours can be forgotten and saved to the card');

  /* playing it moves the play view's rings to it */
  state.live = 0;
  await P.slotAction(globalThis.KYK.ACT.slotLive, 0);
  T(P.playNodes().length === 3, 'playing one of yours is what the play view draws');
  T(Math.abs(P.playNodes()[0].pos[0] - 0.2) < 1e-6, 'at its own positions');
  T(!P.place.sent, 'and the draft stops following, because the module holds a world now');

  /* one whose contents this page never saw draws nothing rather than guessing */
  state.names[9] = 'from a past life';
  state.live = 9;
  await P.slotAction(globalThis.KYK.ACT.slotLive, 9);
  T(P.playNodes().length === 0, 'one stored in another session plays, and its rings are not invented');

  /* the badges, on both kinds */
  state.live = 0; state.target = 9;
  await P.refreshSlots();
  const badgeOf = (idx) => {
    const t = [...els.slots.children].filter(c => c.className.startsWith('slot') && !/builtin/.test(c.className));
    return (t[idx] && t[idx].children[2] ? t[idx].children[2].children : []).map(x => x.className).join(' ');
  };
  T(/live/.test(badgeOf(0)), `the playing world says so (${badgeOf(0)})`);
  T(/tgt/.test(badgeOf(9)), `and so does the morph target (${badgeOf(9)})`);

  /* dragging one slot onto another exchanges them, contents and notes together */
  const slotEls = [...els.slots.children].filter(c => c.className.startsWith('slot') && !/builtin/.test(c.className));
  const nodesAt0 = P.slotNodes[0];
  swapped.length = 0;
  fire(slotEls[0], 'dragstart', { dataTransfer: { setData() {}, effectAllowed: '' } });
  fire(slotEls[9], 'drop', { dataTransfer: { files: [] } });
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(swapped.length === 1 && swapped[0].a === 0 && swapped[0].b === 9,
    `the drag exchanges the two (${JSON.stringify(swapped[0] || null)})`);
  T(P.slotNodes[9] === nodesAt0 && !P.slotNodes[0],
    'and the page\'s note of where the nodes are moves with the contents');
  swapped.length = 0;
  fire(slotEls[0], 'dragstart', { dataTransfer: { setData() {}, effectAllowed: '' } });
  fire(slotEls[9], 'drop', { dataTransfer: { files: [{ name: 'x.kykw', arrayBuffer: async () => new Uint8Array(4).buffer }] } });
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(swapped.length === 0, 'a dropped file is a store and never a swap');

  globalThis.KYK.fetchSlots = realFetch; globalThis.KYK.putSlot = realPut;
  P.setLink(null); P.heldLost(); P.imported.length = 0; P.setLibSel(null); P.setWorlds(null);
  P.setView('play');
}
console.log('');

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
  /* The controls that depend on the link. This row was never re-rendered when
     the link changed, so waveforms added before connecting left `send`
     disabled with the module sitting right there. */
  P.imported.length = 0;
  for (let i = 0; i < 2; i++)
    P.imported.push({ name: 'c' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
  P.setLink(null);
  P.renderImport();
  T(els.wavSend.disabled && !els.wavSave.disabled,
    'with no module, saving is offered and sending is not');
  P.setLink({});
  P.renderImport();
  T(!els.wavSend.disabled, 'and connecting enables it without touching the row');
  P.setLink(null);

  /* Switching to the builder brings it up to date rather than showing whatever
     was there when the tab was last open. */
  P.place.sel = -1;
  els.strip.children.length = 0;
  P.setView('build');
  T(els.strip.children.length === 2, 'opening the tab draws the set as it is now');
  T(els.axes.children.length > 0 && els.planeAxes.children.length > 0,
    'and both views have their axis buttons');
  P.setView('play');

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

/* Taking one wave out without clearing the lot.
 *
 * The only way to drop a single wave was to clear everything and re-import,
 * which for a twenty-four node set punishes you for changing your mind about
 * one of them. The things that have to follow the removal are the selection,
 * the audition, and the positions of everyone else — that last one by *not*
 * moving, because removing a wave is not a reason to rearrange the ones kept.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const sends = [];
  const realPut = globalThis.KYK.putSlot, realPutW = globalThis.KYK.putWorld;
  globalThis.KYK.putWorld = async () => { sends.push(1); return true; };

  P.imported.length = 0;
  for (let i = 0; i < 4; i++)
    P.imported.push({ name: 'r' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.1 * (i + 1), 0.5, 0.5, 0.5], render: new Float32Array(64) });
  const posOf = () => P.imported.map(w => +w.pos[0].toFixed(3));

  /* a card carries the button, and it is not the card's own click */
  P.setView('build');
  P.renderStrip();
  const card = els.strip.children[1];
  const xs = card.children.filter ? card.children.filter(c => c.className === 'cx')
                                  : [...card.children].filter(c => c.className === 'cx');
  T(xs.length === 1, 'each card carries a way to take it out');

  /* Through the button, not by calling removeNode: the handler has to stop the
     click reaching the card underneath, or taking a wave out would also select
     whatever slid into its place. The stub does not bubble, so the stopping is
     what can be checked here — which is the mechanism rather than the effect,
     and is the honest limit of a harness with no event propagation. */
  P.selectNode(2);
  let stopped = false;
  xs[0].onclick({ stopPropagation: () => { stopped = true; } });
  T(stopped, 'the button stops the click reaching the card under it');
  T(P.imported.length === 3, `one goes and the rest stay (${P.imported.length})`);
  T(JSON.stringify(posOf()) === JSON.stringify([0.1, 0.3, 0.4]),
    `the others keep the positions you put them in (${posOf().join(' ')})`);
  T(P.place.sel === 1, 'the selection follows the node, not the index it used to have');
  T(els.strip.children.length === 3, 'and the strip redraws without it');

  /* removing the selected one leaves nothing selected rather than a stale index */
  P.selectNode(1);
  P.removeNode(1);
  T(P.place.sel === -1, 'removing the selected one clears the selection');

  /* while the module is following, a removal is sent; emptied, it is not */
  P.setLink({ request: async () => Uint8Array.of(0) });
  P.place.sent = true;
  sends.length = 0;
  P.removeNode(0);
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(sends.length === 1, `a removal reaches the module while it is following (${sends.length})`);
  sends.length = 0;
  P.removeNode(0);
  for (let i = 0; i < 8; i++) await Promise.resolve();
  T(P.imported.length === 0 && sends.length === 0,
    'and the last one sends nothing, because a world of no nodes is not a world');
  T(!P.place.sent, 'which also stops the following, since there is nothing to follow with');

  globalThis.KYK.putWorld = realPutW; globalThis.KYK.putSlot = realPut;
  P.setLink(null); P.setView('play');
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
  /* Its own nodes. syncPlacement will not send an empty set — a world of no
     nodes is not a world the format can express — so a block about *sending*
     has to have something to send rather than inheriting whatever the block
     above it left behind. */
  P.imported.length = 0;
  for (let i = 0; i < 2; i++)
    P.imported.push({ name: 'f' + i, mags: new Float32Array(64), fit: 1, mode: 'shape',
                      pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });

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
    const fx = [];
    for (let q = 0; q < 2; q++)
      if (b[14 + q] & 0x0f) fx.push({ axis: b[14 + q] >> 4, kind: b[14 + q] & 0x0f });
    return { magic: dv.getUint32(0, true), ver: dv.getUint16(4, true), n, k, count,
             sine: (b[9] & 1) === 1, sigma: dv.getFloat32(10, true), name, fx, nodes, end: at };
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

  /* ── effects on an axis ───────────────────────────────────────────────
     The editor's whole claim is that an effect is a knob of its own rather
     than something that happens as you cross the space, and that claim is
     three properties of the bytes: the effect is declared on axis 4 or 5, the
     world grows to six axes so the module has those axes at all, and every
     node sits at 0.5 on them, which is the value that cancels out of the
     softmax. A file that got any one of those wrong would still load and would
     not do what the row of selectors says. */
  {
    els.fxA.value = '1';        /* fold */
    els.fxB.value = '4';        /* crush */
    P.renderFx();
    const f = await save('fxworld');
    const w = f && parseKykw(f.bytes);
    T(w && w.ver === 2, 'a world with an effect says version 2, so a module that cannot render it refuses it');
    T(w && w.n === 6, `it carries six axes, because the effect axes have to exist (n=${w && w.n})`);
    T(w && w.fx.length === 2 && w.fx[0].axis === 4 && w.fx[0].kind === 1
        && w.fx[1].axis === 5 && w.fx[1].kind === 4,
      'fold on axis 4, bit reduction on axis 5, in that order');
    T(w && w.nodes.every(nd => nd.pos[4] === 0.5 && nd.pos[5] === 0.5),
      'every node sits at 0.5 on both effect axes, which is exactly neutral in the blend');
    T(w && w.nodes.every(nd => nd.pos.slice(0, 4).filter(v => Math.abs(v - 0.5) > 1e-6).length === 2),
      'and placement on the first four axes is untouched');
    T(w && w.end === f.bytes.length, 'the six-axis geometry still accounts for every byte');
    /* One selector back to none: the world drops to four axes again rather than
       carrying a dead fifth and sixth, and the version follows. */
    els.fxB.value = '0';
    P.renderFx();
    const one = parseKykw((await save('fxone')).bytes);
    T(one.ver === 2 && one.n === 6 && one.fx.length === 1 && one.fx[0].axis === 4,
      'one effect is still a six-axis world — axis 4 has to be an axis');
    els.fxA.value = '0';
    P.renderFx();
    const none = parseKykw((await save('fxnone')).bytes);
    T(none.ver === 1 && none.n === 4 && none.fx.length === 0,
      'no effects and it is a version 1 four-axis world again, which any module can play');
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
/* ── the loop row is a readout, not a memory ──────────────────────────────
 *
 * The clock moves the loop on and the module stops one when a world is chosen by
 * hand, so a row drawn from what the page last programmed would be the one thing
 * on screen the module has already left behind. Every change is therefore sent
 * and read back, and the live position comes off telemetry. That is exactly the
 * discipline the play view needed two bugs ago, asked of a second readout.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  /* A module that answers with a loop of its own, so "the row is what came back"
     can be told apart from "the row is what I clicked". */
  let sent = [];
  const reply = (entries, div = 2) =>
    Uint8Array.from([0, entries.length, div, 1, 128, ...entries]);
  let answer = [13, 14, 15];
  P.setLink({
    request: async (cmd, body) => {
      if (body && body[0] === 1) { sent = Array.from(body.subarray(3)); return reply(answer, body[1]); }
      return reply(answer);
    },
    close: async () => {},
  });
  P.setWorlds({ count: 22, current: 13, list: Array.from({ length: 22 }, (_, i) => ({ index: i, name: 'w' + i, note: 'x', kind: 1 })) });

  await P.readTour();
  T(JSON.stringify(P.getTour()) === JSON.stringify([13, 14, 15]),
    'the row comes from the module rather than from the page');
  T(els.tourStops.children.length === 5, `three stops draw as three chips and two arrows (${els.tourStops.children.length})`);

  /* Adding a stop sends the whole sequence — half a loop is a different loop —
     and the row then shows what came back, which here is deliberately not what
     was asked for. */
  answer = [13, 14, 15, 16];
  P.setLibSel({ kind: 'builtin', i: 16 });
  await P.tourAdd();
  T(JSON.stringify(sent) === JSON.stringify([13, 14, 15, 16]), `the whole sequence goes in one request (${sent})`);
  answer = [13, 14];
  await P.sendTour([13, 14, 15, 16]);
  T(JSON.stringify(P.getTour()) === JSON.stringify([13, 14]),
    'and a module that answers with something else is believed');

  /* Where it has got to comes off telemetry, and a length that disagrees means
     the loop changed without this page asking — so the row is re-read. */
  answer = [13, 14, 15];
  P.onTelemetry(tel({ tour: { len: 3, at: 2, blend: 0.5 } }));
  await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
  T(JSON.stringify(P.getTour()) === JSON.stringify([13, 14, 15]),
    'a telemetry frame that disagrees with the row makes the page ask again');
  P.setTourLive({ len: 3, at: 1, blend: 0.25 });
  P.renderTour();
  const cls = Array.from(els.tourStops.children).filter(c => c.className.startsWith('ts')).map(c => c.className);
  T(cls[1].includes('live') && cls[2].includes('next'),
    `the live stop and the one being travelled to are marked (${cls.join(' ')})`);

  /* One of yours is a stop by its slot number with the high bit set, and it is
     named from the library rather than by index. */
  P.setSlots({ count: 32, names: Array.from({ length: 32 }, (_, i) => (i === 5 ? 'mine' : null)) });
  T(P.tourName(0x85) === 'mine' && P.tourName(13) === 'w13',
    'a stop knows whether it is one that ships or one of yours');

  P.setLink(null);
  P.setTourLive({ len: 0, at: 0, blend: 0 });
  await P.readTour();
  T(P.getTour().length === 0, 'and with no module there is no loop to draw');
}
console.log('');

if (unknownIds.size) { bad++; console.log(`  FAIL page asked for undeclared ids: ${[...unknownIds].join(', ')}`); }
console.log('');

/* the descriptor parse, then every case again with a panel present */
const parsed = P.parsePanel(DESC);
{
  const ok = parsed && parsed.pages === 7 && parsed.pots === 6
             && parsed.grid[4][0] && parsed.grid[4][0].name === 'Gravity'
             && parsed.grid[5][5] === null
             && parsed.grid[6][2] && parsed.grid[6][2].name === 'Tour division'
             && parsed.grid[6][5] === null;
  if (!ok) bad++;
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} descriptor pager block parses (found ${
    parsed ? parsed.pages + ' pages x ' + parsed.pots + ' pots' : 'nothing'}, gap at Couple P6 kept null)`);
}
console.log('');

/* The page's idea of the render has to be the module's idea of it.
 *
 * The build view draws what the engine will make of a waveform beside the one
 * you imported, and plays them against each other. That is only worth anything
 * if the page computes what the engine computes — and it did not: the engine
 * renders minus sum m.sin(h.theta) and the page summed plus, so every rendered
 * trace was drawn mirrored and every fit looked worse than it was.
 *
 * web/selftest.mjs pins the same convention against the real engine, by
 * sending a world of known coefficients and correlating the frame that comes
 * back. This is the other half of that: the page agreeing with it. Two
 * harnesses, one truth, and neither can drift without the other noticing.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const mags = new Float32Array(64);
  for (let h = 0; h < 6; h++) mags[h] = (h % 2 ? -1 : 1) / (h + 1);
  const got = P.renderFromMags(mags, 256);
  let worst = 0;
  for (let i = 0; i < 256; i++) {
    let want = 0;
    for (let h = 0; h < 64; h++) want -= mags[h] * Math.sin(2 * Math.PI * (h + 1) * i / 256);
    worst = Math.max(worst, Math.abs(want - got[i]));
  }
  T(worst < 1e-5, `the page renders -sum m.sin(h.theta), like the engine (worst ${worst.toExponential(1)})`);

  /* The check that was missing, and its absence let a fix become a bug.
   *
   * One check held renderFromMags to the engine's formula and another held the
   * formula to the module. Neither compared the two traces the inspector
   * actually draws — so when the render was negated to match the engine and
   * the imported cycle was not, the two arrived anti-phase and every waveform
   * in the build view was drawn against its own mirror image. The fit said
   * 100% while the traces crossed everywhere.
   *
   * A wave the projection keeps in full must look like what the engine makes
   * of it. That is the property; it is one line; nothing was asserting it. */
  {
    const N = 600, cyc = new Float64Array(N);
    for (let i = 0; i < N; i++) cyc[i] = 2 * (i / N) - 1;          /* a saw */
    const nd = KYK.cycleToNode(cyc, 64, 'shape');
    const thumb = P.rotatedCycle(cyc, nd.rotation || 0, 256);
    const render = P.renderFromMags(nd.mags, 256);
    let sa = 0, sb = 0, ab = 0;
    for (let i = 0; i < 256; i++) { sa += thumb[i] * thumb[i]; sb += render[i] * render[i]; ab += thumb[i] * render[i]; }
    const c = ab / Math.sqrt(sa * sb || 1);
    T(c > 0.9, `imported and rendered are drawn the same way up (corr ${c.toFixed(4)}, fit ${(100 * nd.fit).toFixed(1)}%)`);
  }

  /* and the band-limited trace, which is the middle term of the audition:
     everything above K removed and nothing else touched */
  const y = new Float32Array(64);
  for (let i = 0; i < 64; i++) y[i] = Math.sin(2 * Math.PI * 3 * i / 64) + 0.5 * Math.sin(2 * Math.PI * 20 * i / 64);
  const keptAll = P.bandLimit(y, 30), cut = P.bandLimit(y, 10);
  const rms = a => Math.sqrt([...a].reduce((s, v) => s + v * v, 0) / a.length);
  let d = 0;
  for (let i = 0; i < 64; i++) d = Math.max(d, Math.abs(keptAll[i] - y[i]));
  T(d < 1e-4, `a band limit above every partial changes nothing (worst ${d.toExponential(1)})`);
  T(Math.abs(rms(cut) - rms(y) * Math.sqrt(1 / 1.25)) < 0.02,
    'and one below the second partial removes exactly that partial');
}
console.log('');

/* The mirror's geometry must be the panel's geometry.
 *
 * The panel is two columns of three — [P1] [P2] / [P3] [P4] / [P5] [P6], per the
 * SDK's layout header. The mirror drew three across and two down, so P3 appeared
 * beside P2 when it is physically below P1. A mirror that rearranges the thing it
 * mirrors is worse than no mirror, and it was reported from the bench as the pot
 * numbers being out of line.
 *
 * Checked through the knob arcs, whose centres are where the knobs actually are.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const A0 = Math.PI * 0.75;
  arcLog.length = 0;
  P.setPanel(parsed);
  P.setTel(tel({ pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  P.drawSound();
  /* the background ring of each knob: a full sweep from A0, radius > 4 */
  const rings = arcLog.filter(a => a.length >= 5 && Math.abs(a[3] - A0) < 1e-9 && a[2] > 4);
  const centres = [];
  for (const [x, y] of rings) if (!centres.some(c => Math.abs(c[0] - x) < 1 && Math.abs(c[1] - y) < 1)) centres.push([x, y]);
  T(centres.length === 6, `six knobs are drawn (${centres.length})`);
  const xs = [...new Set(centres.map(c => Math.round(c[0])))].sort((a, b) => a - b);
  const ys = [...new Set(centres.map(c => Math.round(c[1])))].sort((a, b) => a - b);
  T(xs.length === 2, `in two columns, as the panel is (${xs.length})`);
  T(ys.length === 3, `and three rows (${ys.length})`);
  /* and in the panel's own order: P1 top-left, P2 top-right, P3 middle-left */
  const at = (i) => centres[i];
  T(at(0)[0] === at(2)[0] && at(2)[1] > at(0)[1], 'P3 is below P1, not beside P2');
  T(at(1)[0] > at(0)[0] && Math.round(at(1)[1]) === Math.round(at(0)[1]), 'P2 is beside P1');
  P.setTel(null); P.setPanel(null);
}
console.log('');

/* The panel mirror shows what the knobs are worth, not where they are sitting.
 *
 * The panel catches: arriving on a page leaves each pot where your hand left
 * it while the parameter keeps its value. The mirror drew the pot, so on any
 * page you had just arrived on it was showing six numbers that were not in
 * effect — which is the one thing on screen that is not what the instrument is
 * doing.
 *
 * Asserted through the arcs the mirror actually draws, because "it draws the
 * right number" is a claim about a mark on a canvas and nothing else here can
 * see one.
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const A0 = Math.PI * 0.75, A1 = Math.PI * 2.25;
  /* the knob arcs: full-sweep background rings and value arcs share A0 */
  const sweeps = () => arcLog.filter(a => a.length >= 5 && Math.abs(a[3] - A0) < 1e-9)
                             .map(a => (a[4] - A0) / (A1 - A0));
  const near = (xs, v) => xs.some(x => Math.abs(x - v) < 0.01);

  P.setPanel(parsed);
  P.setView('play');

  /* pot and value disagreeing is the whole case */
  arcLog.length = 0; segLog.length = 0;
  P.setTel(tel({ pots: [0.9, 0.9, 0.9, 0.9, 0.9, 0.9], knobs: [0.2, 0.2, 0.2, 0.2, 0.2, 0.2] }));
  P.drawSound();
  const drawn = sweeps();
  T(near(drawn, 0.2), 'the knob is drawn at the value the parameter has');
  T(!near(drawn, 0.9), 'and not at the pot, which is somewhere else entirely');

  /* And the pot is still shown, outside the ring, at its own angle — "both
     atop one another" is the point, and a mirror that showed only the value
     would be just as incomplete as one that showed only the pot: the gap
     between them is how far there is to turn before the knob takes hold.
     Found from the knob arcs themselves rather than from assumed geometry, so
     this cannot pass by measuring the wrong circle. */
  const knobs = arcLog.filter(a => a.length >= 5 && Math.abs(a[3] - A0) < 1e-9 && a[2] > 4);
  const tickAt = (frac) => knobs.some(([kx, ky, r]) => segLog.some(([x, y]) => {
    const d = Math.hypot(x - kx, y - ky);
    if (d < r + 2 || d > r + 7) return false;
    let th = Math.atan2(y - ky, x - kx);
    while (th < A0 - 1e-9) th += 2 * Math.PI;
    return Math.abs((th - A0) / (A1 - A0) - frac) < 0.02;
  }));
  T(knobs.length > 0, `the mirror drew knobs to measure (${knobs.length})`);
  T(tickAt(0.9), 'and the pot is marked outside the ring, where the hand actually is');
  T(!tickAt(0.55), 'and nowhere else');

  /* older firmware sends no values, and then the pot is all there is */
  arcLog.length = 0; segLog.length = 0;
  P.setTel(tel({ pots: [0.9, 0.9, 0.9, 0.9, 0.9, 0.9], knobs: null }));
  P.drawSound();
  T(near(sweeps(), 0.9), 'with no values on the wire it falls back to the pot');

  /* and values with no pots is the other way round */
  arcLog.length = 0; segLog.length = 0;
  P.setTel(tel({ pots: null, knobs: [0.35, 0.35, 0.35, 0.35, 0.35, 0.35] }));
  P.drawSound();
  T(near(sweeps(), 0.35), 'and values alone are enough to draw it');

  P.setTel(null);
  P.setPanel(null);
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

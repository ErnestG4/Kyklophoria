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

const IDS = 'fwName fwMode panelRows pageLinks pageSwitch morphCap tourRow axes planeAxes plane inspect strip tabPlay tabModel tabBuild tabLib playMain modelMain buildMain libMain maxis mmodes instChips modelCardSel modelCardLoad modelCardState lockChips pitchNote strikeChips bodyChips ringChips voiceChips exciterNote ringPlane voicePlane exciterPlane modelHint motionNote slots libPlay libTarget libNoTarget libEdit libFree libSave libCardSel libLoad libRescan libState worldbar btnBridge btnClose btnDrawer btnSerial drawer modinfo msg sBlock sCpu sCpuWrap sF0 sKcut sLink sMod sSpace sSpread shadeChips sound space trailChips worldCap worldChips worldNote morphSel cardSel cardLoad cardScan cardState morphState wavIn wavPick wavMode wavName wavSend wavSave wavPlace wavClear wavNew wavKeep wavState audPlay audA audB audC audState polState polFlip polNote muteChips morphAim morphDirect fxA fxB fxState tourAdd tourDrop tourClear tourDiv tourStops tourState livePill pagePill pitchPill cpuPill cpuAvg cpuMax cpuText stageChip panel panelTabs panelNote panelAll panelSlotPlay panelSlotModel sheetSlotPlay sheetSlotModel'.split(' ');
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
/* and the words, so a check can ask what a label says rather than only that
   something was drawn */
const textLog = [];
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
      if (k === 'fillText') textLog.push(String(a[0]));
      /* and the straight segments, which is how a pointer or a tick is drawn */
      if (k === 'moveTo' || k === 'lineTo') segLog.push([a[0], a[1]]);
      if (a.some(v => typeof v === 'number' && !Number.isFinite(v))) {
        nanDraws.push(`${k}(${a.map(v => typeof v === 'number' ? v : typeof v).join(', ')})`);
        /* NANSTACK=1 says where from: the page's functions are anonymous
           inside the Function wrapper, so the line numbers are into the
           script block, not into index.html */
        if (process.env.NANSTACK) console.log(new Error().stack.split('\n').slice(2, 5).join('\n'));
      }
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
  createElement: mkEl, addEventListener(type, fn) { if (type === 'keydown') globalThis.__keydown = fn; },
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
const hook = `\nglobalThis.__probe = { frame, drawSpace, drawSound, drawStatus, parsePanel, setTel: v => { tel = v; }, setBasis: b => { basis = b; }, setPanel: p => { panel = p; }, onTelemetry, imported, worldName, importedBlob, exportImported, place, pickNode, moveNodeTo, placeOnCell, view, setAxes: v => { axes = v; }, syncPlacement, setLink: v => { link = v; }, drawPlane, drawInspect, renderStrip, renderLibrary, renderLib, refreshSlots, slotAction, keepBuildSet, freeSlot, setLibSel: v => { libSel = v; }, getLibSel: () => libSel, slotNodes, setWorlds: v => { worlds = v; }, playNodes, heldName: () => held && held.name, heldLost, renderImport, sendImported, renderFromMags, rotatedCycle, bandLimit, removeNode, refreshWorlds, disconnect, setCardList: v => { cardList = v; }, setView, planeBoxes, boxAt, selectNode, renderFx, fxSel, setSlots: v => { slots = v; }, renderTour, readTour, sendTour, tourAdd, tourName, getTour: () => tourStops, setTourLive: v => { tourLive = v; }, setReso: v => { reso = v; }, drawModel, renderModelChips, pitchNoteText, pressAxis, getLock: () => lock, getPoly: () => poly, fwModeOf, applyFwMode, setFwMode: v => { fwMode = v; }, getView: () => viewMode, setPageMode: v => { pageMode = v; }, pageMismatch, renderPageLinks, renderPanelRows, applyPots, potMove, getPotCtl: () => potCtl, potText, pollPots, setPolling: v => { polling = v; }, auditionStart, getAudition: () => audition, drawStatus, getStats: v => { stats = v; }, drawPanelLive, ringOf, setPolyCount: v => { poly = v; }, telFlags, addWavs, flipNode, connectTo: connect, getAxes: () => axes, say, voiceTuning, buildAxisButtons, renderMuteChips, setHeld: v => { held = v; }, axisNames, setDesc: v => { desc = v; }, scopeOn, selectWorld, renderWorldBar, chooseInstrument, selEntries, selLayout, getSwitching: () => switching };\n`;
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
    pots: [0.1, 0.3, 0.5, 0.7, 0.9, 1.0], bytes: 512, kind: 1, resonate: false,
  }, over, over.kind === 11 ? { resonate: true } : {});
}
/* a resonate world as telemetry reports it: four axes (body, velocity,
   decay, coil) like any world's, and the kind that says what it is */
const RES = { n: 4, kind: 11, world: 0xFF, mags: null, frame: null, flags: 4 };
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
  /* a resonate world — a .kykm loaded from the card — is one-dimensional and
     has no rotation plane at all; the page had never drawn N=1 before it */
  ['a resonate world live: four axes, its kind said', tel(RES)],
  ['a resonator on firmware that says no kind: a plain 4-axis user world', tel({ n: 4, world: 0xFF, mags: null, frame: null, flags: 4, kind: null })],
  /* the same with the module's readout in hand: an axis of points and the
     voice between two of them, modes on a log axis (one at zero gain, one
     at zero frequency, which the drawing must skip rather than take the
     log of), a burst */
  ['a resonate world with its readout', tel(RES), {
    kind: 0, lo: 36, hi: 96, param: 61.5, P: 11, burst: 3360,
    points: [36, 43, 48, 55, 60, 67, 72, 79, 84, 91, 96],
    modes: [{ hz: 261.6, zeta: 0.001, gain: 0.5 }, { hz: 523.2, zeta: 0.002, gain: 0.2 }, { hz: 2093, zeta: 0.03, gain: 0.001 },
            { hz: 0, zeta: 1, gain: 0 }, { hz: 8000, zeta: 0.5, gain: 0.00001 }] }],
  ['a family with its readout', tel(RES), {
    kind: 0, lo: 55, hi: 93, param: 62.0, P: 25, burst: 3360, points: [55, 56, 58, 61, 62, 64, 67, 69, 70, 72, 74, 76, 79, 81, 82, 84, 86, 88, 91, 93],
    modes: [{ hz: 293.7, zeta: 0.001, gain: 0.5 }, { hz: 587.3, zeta: 0.002, gain: 0.2 }],
    members: ['violin', 'viola', 'bass', 'guitar', 'banjo', 'mandolin'], member: 1, name: 'viola' }],
  ['an index world with its readout', tel(RES), {
    kind: 1, lo: 0, hi: 18, param: 7.2, P: 19, burst: 0, points: Array.from({ length: 19 }, (_, i) => i),
    modes: [{ hz: 488, zeta: 0.0009, gain: 0.24 }, { hz: 975, zeta: 0.001, gain: 0.1 }] }],
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
      /* the idents the firmware gives them (shell/alchemy/main.cpp), which is
         what the page's rules about a pot key on */
      const ids = [['pitch.coarse','pitch.fine','pos.0','pos.1','pos.2','pos.3'],
                   ['rot.01','rot.02','rot.03','rot.12','rot.13','rot.23'],
                   ['st.spread','st.plane','morph.sharp','eng.rdiv','out.level','lane.cva'],
                   ['orb.01','orb.02','orb.03','orb.12','orb.13','orb.23'],
                   ['kep.g','kep.ecc','kep.plane','kep.soft','kep.damp','kep.r'],
                   ['orb.couple','orb.reach','orb.ratex','kep.bodies','kep.mass', null],
                   ['pos.4','pos.5','tour.div','tour.glide','tour.rate', null]];
      const out = [];
      for (let p = 0; p < 7; p++) for (let q = 0; q < 6; q++)
        if (names[p][q]) out.push({ id: ids[p][q], name: names[p][q], page: p, pot: q,
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

/* ── the model tab: a resonator's hands ─────────────────────────────────
 * The chips are built from what the module says (0x6F) and what the page
 * holds; each plane is checked for the control it should offer on each kind
 * of world, and for greying where the module's pots own it. */
{
  console.log('== the model tab');
  const tick = () => new Promise(r => setImmediate(r));
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const sent = [];
  P.setLink({ kind: 'bridge', request: async (cmd, body) => { sent.push([cmd, [...body]]); return new Uint8Array([0]); } });
  P.setPanel(null);
  const fam = { kind: 0, lo: 55, hi: 93, param: 62.0, P: 25, burst: 3360, points: [55, 58, 62, 67], modes: [{ hz: 293.7, zeta: 0.001, gain: 0.5 }],
                members: ['violin', 'viola', 'bass'], member: 1, name: 'viola', voices: 2, lock: false };
  P.setSlots({ count: 32, live: 3, target: 0xFF, names: Array.from({ length: 32 }, (_, i) => (i === 3 ? 'strings' : i === 7 ? 'wurli' : null)) });
  P.setCardList({ count: 3, names: ['a.kykw', 'pianos.kykm', 'bodies.kykm'] });
  P.setTel(tel({ ...RES, f0: 130.8 }));
  P.setReso(fam);
  P.setView('model');
  P.renderModelChips();
  T(els.instChips.children.filter(c => c.tagName === 'button').length === 2, 'the instruments are the slots with something in them (2)');
  T(els.instChips.children[0].className.includes('on'), 'and the live one is lit');
  T(els.modelCardSel.children.length === 2 && !/kykw/.test(els.modelCardSel.children[0].textContent), 'the card list is the resonators alone (2)');
  T(els.lockChips.children.length === 2 && els.lockChips.children[1].className.includes('on'), 'the lock chips follow the readout: free');
  T(P.getPoly() === 2, 'and so does the voice count (2)');
  T(els.voiceChips.children.filter(c => c.tagName === 'button')[1].className.includes('on'), 'with its chip lit');
  T(els.strikeChips.children.filter(c => c.tagName === 'button').length === 4, 'four strike pads: soft, mid, hard and the slider\'s');
  T(els.bodyChips.children.length === 3 && els.bodyChips.children[1].className.includes('on'), 'a family\'s body is its members, the one playing lit');
  T(!els.bodyChips.children[0].disabled, 'and over the bridge they can be pressed');
  T(els.ringChips.children.filter(c => c.tagName === 'input').length === 2, 'the ring is decay and coil');
  T(/C3.*built at D4.*follows the cent/.test(P.pitchNoteText()), `the pitch line: ${P.pitchNoteText()}`);
  /* the pads send strikes; the lock chip sends the action */
  sent.length = 0;
  els.strikeChips.children.filter(c => c.tagName === 'button')[2].onclick();
  await tick();
  T(sent.length === 1 && sent[0][1][0] === 17 && sent[0][1][1] === 230, `hard strikes at 230 (${JSON.stringify(sent[0])})`);
  /* and a press is a gate while held: opened and struck on the press, shut
     on the release, and the click that follows the press not struck again */
  {
    const hard = els.strikeChips.children.filter(c => c.tagName === 'button')[2];
    const fire = ev => (hard._on[ev] || []).forEach(f => f({}));
    sent.length = 0;
    fire('pointerdown'); await tick();
    const down = sent.map(x => x[1].slice(0, 2).join(':'));
    fire('pointerup'); hard.onclick(); await tick();
    const all = sent.map(x => x[1].slice(0, 2).join(':'));
    T(down.join() === '24:1,17:230' && all.join() === '24:1,17:230,24:0', `a pad held is a gate: pressed ${down.join()}, let go and clicked ${all.slice(down.length).join()}`);
  }
  sent.length = 0;
  els.lockChips.children[0].onclick();
  await tick();
  T(sent.length === 1 && sent[0][1][0] === 20 && sent[0][1][1] === 1, `locked sends op 20, 1 (${JSON.stringify(sent[0])})`);
  T(P.getLock() === true, 'and the page records it');
  /* a note world: the body is the voicing slider; an index world: the row */
  P.setReso({ ...fam, members: [], name: 'wurli' }); P.renderModelChips();
  T(els.bodyChips.children.filter(c => c.tagName === 'input').length === 1 && /voicing/.test(els.bodyChips.children[0].textContent), 'a note world\'s body is the voicing');
  P.setReso({ ...fam, kind: 1, lo: 0, hi: 18, param: 4, P: 19, members: [], name: 'perc' }); P.renderModelChips();
  T(/body 0\.\.18/.test(els.bodyChips.children[0].textContent), 'an index world\'s body is the row');
  /* the module owns the spin: the sliders and the members grey, the pads do not */
  P.setPanel(parsed); P.setReso(fam); P.renderModelChips();
  T(els.bodyChips.children.every(c => c.disabled), 'on the module the members are its pot\'s');
  T(els.ringChips.children.filter(c => c.tagName === 'input').every(c => c.disabled), 'and so are decay and coil');
  T(els.strikeChips.children.filter(c => c.tagName === 'button').every(c => !c.disabled), 'while the pads still strike');
  T(/Play P3/.test(els.modelHint.textContent), 'and the hint names the Play page pots');
  /* the axis is a pad: a press strikes, and over the bridge sets the pitch first */
  P.setPanel(null); sent.length = 0;
  P.pressAxis({ clientX: 150, clientY: 20 });
  await tick();
  T(sent.some(([c, b]) => b[0] === 17) , 'a press on the axis strikes');
  /* no resonator: everything but the instruments stands down */
  P.setTel(tel({ n: 4, world: 0 })); P.setReso(null); P.renderModelChips();
  T(els.lockChips.children.every(c => c.disabled) && /no resonator/.test(els.modelHint.textContent), 'with a wavetable playing the planes stand down and the hint says why');
  P.drawModel();
  /* the sliders follow the knobs on the module (Combust: "Moving knobs does
     the right thing but doesn't change sliders on the model page"): over a
     serial link, with the panel's descriptor and without it, the axes moved
     in telemetry must move the sliders the next frame */
  for (const withPanel of [true, false]) {
    P.setLink({ kind: 'serial', request: async () => new Uint8Array([0]) });
    P.setPanel(withPanel ? parsed : null);
    P.setReso({ ...fam, members: [], name: 'wurli' });
    P.setTel(tel({ ...RES, ctl: Float32Array.from([0.5, 0.5, 0.5, 0.5]), posL: Float32Array.from([0.5, 0.5, 0.5, 0.5]) }));
    P.renderModelChips();
    const inputs = () => [...els.bodyChips.children, ...els.strikeChips.children, ...els.ringChips.children].filter(c => c.tagName === 'input' && c.max === 1000);
    P.setTel(tel({ ...RES, ctl: Float32Array.from([0.1, 0.2, 0.8, 0.9]), posL: Float32Array.from([0.1, 0.2, 0.8, 0.9]) }));
    P.drawModel();
    const vals = inputs().map(s => +s.value);
    T(vals.length === 4 && vals.join() === '100,200,800,900', `the four axis sliders follow the knobs over serial ${withPanel ? 'with' : 'without'} the panel descriptor (${vals.join()})`);
  }
  /* Bongs with its panel rows: a plane that repeats a pot is left out, so
     the page has one hand on each setting — the pads, the pad velocity and
     the family's members stay, the lock is a readout */
  {
    P.setLink({ kind: 'serial', request: async () => new Uint8Array([0]) });
    P.setPanel(parsed); P.setFwMode('modal'); P.renderPanelRows();
    P.setReso({ ...fam, morph: false }); P.setTel(tel({ ...RES, f0: 130.8 }));
    P.renderModelChips();
    const f = [];
    if (!(els.ringPlane.hidden && els.voicePlane.hidden && els.exciterPlane.hidden)) f.push('ring, voices or exciter still drawn');
    if (els.lockChips.children.some(c => c.tagName === 'button') || !/free|locked/.test(els.lockChips.children.map(c => c.textContent).join())) f.push('the lock is not a readout');
    if (els.strikeChips.children.filter(c => c.tagName === 'button').length !== 4) f.push('the pads went');
    if (els.strikeChips.children.filter(c => c.tagName === 'input').length !== 1) f.push('trigger vel or dig in still drawn beside the pad velocity');
    if (els.bodyChips.children.length !== 3) f.push(`the body is ${els.bodyChips.children.length} things, not the three members without the morph`);
    if (!/panel beside/.test(els.modelHint.textContent)) f.push('the hint does not point at the panel');
    /* and the page for both keeps every plane */
    P.setFwMode('both'); P.renderPanelRows(); P.renderModelChips();
    if (els.ringPlane.hidden || els.voicePlane.hidden || els.exciterPlane.hidden) f.push('a plane stayed hidden off Bongs');
    T(!f.length, `on Bongs the planes that repeat a pot stand down${f.length ? ' — ' + f.join('; ') : ''}`);
  }
  P.setView('play'); P.setLink(null); P.setPanel(null); P.setTel(null);
}
console.log('');

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
  /* Both ways round, and through the page's own import (addWavs), because the
     trace it keeps is the one the inspector draws and the audition plays —
     and since import turns a wave the built-ins' way up, one of the two
     ramps is negated on the way in and its trace has to be too. */
  {
    const n = 600, f = [];
    for (const dir of [1, -1]) {
      const wav = new Uint8Array(44 + n * 2), dv = new DataView(wav.buffer);
      const put = (o, t) => { for (let i = 0; i < 4; i++) wav[o + i] = t.charCodeAt(i); };
      put(0, 'RIFF'); dv.setUint32(4, 36 + n * 2, true); put(8, 'WAVE');
      put(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
      dv.setUint32(24, 48000, true); dv.setUint32(28, 96000, true); dv.setUint16(32, 2, true); dv.setUint16(34, 16, true);
      put(36, 'data'); dv.setUint32(40, n * 2, true);
      for (let i = 0; i < n; i++) dv.setInt16(44 + i * 2, Math.round(32000 * dir * (2 * i / n - 1)), true);
      f.push({ name: dir > 0 ? 'rise.wav' : 'fall.wav', arrayBuffer: async () => wav.buffer });
    }
    P.imported.length = 0;
    await P.addWavs(f);
    const cs = P.imported.map(w => {
      let sa = 0, sb = 0, ab = 0;
      for (let i = 0; i < w.thumb.length; i++) { sa += w.thumb[i] * w.thumb[i]; sb += w.render[i] * w.render[i]; ab += w.thumb[i] * w.render[i]; }
      return ab / Math.sqrt(sa * sb || 1);
    });
    T(cs.length === 2 && cs.every(c => c > 0.9),
      `imported and rendered are drawn the same way up, a ramp either way round (corr ${cs.map(c => c.toFixed(4)).join(', ')}, fit ${(100 * (P.imported[0] || {}).fit).toFixed(1)}%)`);
    P.imported.length = 0;
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

/* One polarity for every import: the built-ins'.
 *
 * The engine plays -sum m.sin(h.theta) and a morph is the sum of two worlds'
 * coefficients, so a node and its negative do not blend, they cancel. Import
 * kept whichever way up the file was, and which of two equally good rotations
 * rounding happened to find first, so an imported rising ramp was exactly -1 x
 * Lock's saw and a 50/50 morph between them sat at -59.5 dB (the 2026-10-01
 * review, representation B4). web/selftest.mjs holds the convention to a
 * snapshot of Lock from the module; this holds it to itself — every way a ramp
 * can arrive, one node — and to the saw the built-ins write (core/kyk_lock.h:
 * every harmonic at 1/h, positive).
 */
{
  const T = (ok, what) => { if (!ok) bad++; console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); };
  const N = 600;
  const ramp = (dir, off) => { const x = new Float64Array(N); for (let i = 0; i < N; i++) x[i] = dir * (2 * (((i + off) % N) / N) - 1); return x; };
  const up = KYK.cycleToNode(ramp(1, 0)), down = KYK.cycleToNode(ramp(-1, 0));
  T(up.mags.every((v, i) => v === down.mags[i]),
    `a ramp imported both ways round lands on the same coefficients (h1 ${up.mags[0].toFixed(4)} and ${down.mags[0].toFixed(4)})`);
  const saw = Float64Array.from({ length: 64 }, (_, i) => 1 / (i + 1));
  let s = 0, e1 = 0, e2 = 0;
  for (let i = 0; i < 64; i++) { s += up.mags[i] * saw[i]; e1 += up.mags[i] * up.mags[i]; e2 += saw[i] * saw[i]; }
  T(s / Math.sqrt(e1 * e2) > 0.999, `and it is the built-in saw, the same way up (corr ${(s / Math.sqrt(e1 * e2)).toFixed(4)})`);
  /* A file can start anywhere in its cycle, and every start has two rotations
     that keep exactly as much — the one that is Lock's saw and the one turned
     half a cycle, which is the saw with its even harmonics negated. Rounding
     chose between them, about half the time each (84 of 172 starts). */
  let moved = 0, tried = 0;
  for (let off = 0; off < N; off += 7)
    for (const dir of [1, -1]) {
      const nd = KYK.cycleToNode(ramp(dir, off));
      let d = 0;
      for (let i = 0; i < 64; i++) d = Math.max(d, Math.abs(nd.mags[i] - up.mags[i]));
      tried++; if (d > 1e-6) moved++;
    }
  T(moved === 0, `from wherever in its cycle the file starts, either way round, it is that node (${tried - moved} of ${tried})`);
  /* the morph, as the engine blends: half of each set of coefficients */
  const lvl = (a, b) => {
    let em = 0, ea = 0;
    for (let i = 0; i < 64; i++) { const m = 0.5 * (a[i] + b[i]); em += m * m; ea += 0.5 * (a[i] * a[i] + b[i] * b[i]); }
    return 10 * Math.log10(em / ea);
  };
  const sq = (dir) => { const x = new Float64Array(N); for (let i = 0; i < N; i++) x[i] = dir * (i < N / 2 ? 1 : -1); return x; };
  const sqU = KYK.cycleToNode(sq(1)), sqD = KYK.cycleToNode(sq(-1));
  const l1 = lvl(up.mags, down.mags), l2 = lvl(sqU.mags, sqD.mags);
  T(l1 > -0.01 && l2 > -0.01,
    `a 50/50 morph between a wave and its upside-down twin, both imported, holds its level (ramp ${l1.toFixed(2)} dB, square ${l2.toFixed(2)} dB)`);
  T(KYK.nodePolarity(up.mags) === 1 && KYK.nodePolarity(Float32Array.from(up.mags, v => -v)) === -1
    && KYK.nodePolarity(new Float32Array(64)) === 0,
    'nodePolarity says which way up a node is, and that silence is neither');

  /* The inspector says so, and Flip turns one node over: every coefficient
     negated, the imported trace with it, so the comparison above it still
     reads the same way up; twice is where it started; and a set the module is
     holding is re-sent, as a move is. */
  const sends = [];
  const realPut = globalThis.KYK.putWorld;
  globalThis.KYK.putWorld = async () => { sends.push(1); return true; };
  P.imported.length = 0;
  for (const [nd, cyc] of [[up, ramp(1, 0)], [sqU, sq(1)]])
    P.imported.push({ name: 'n', mags: nd.mags, fit: nd.fit, mode: 'shape', pos: [0.5, 0.5, 0.5, 0.5],
                      render: P.renderFromMags(nd.mags), thumb: P.rotatedCycle(cyc, nd.rotation, undefined, nd.sign) });
  P.place.sel = -1;
  P.selectNode(0);
  const f = [];
  if (!/\+/.test(els.polState.textContent) || els.polState.className !== 'up') f.push(`an import reads "${els.polState.textContent}"`);
  if (els.polFlip.disabled) f.push('Flip is not offered for a picked node');
  const before = Float32Array.from(P.imported[0].mags), other = Float32Array.from(P.imported[1].mags);
  P.flipNode(0);
  const w = P.imported[0];
  if (!w.mags.every((v, i) => v === -before[i])) f.push('the coefficients were not negated');
  if (!P.imported[1].mags.every((v, i) => v === other[i])) f.push('another node moved');
  if (!/−/.test(els.polState.textContent) || els.polState.className !== 'down') f.push(`flipped, it reads "${els.polState.textContent}"`);
  let sa = 0, sb = 0, ab = 0;
  for (let i = 0; i < w.thumb.length; i++) { sa += w.thumb[i] * w.thumb[i]; sb += w.render[i] * w.render[i]; ab += w.thumb[i] * w.render[i]; }
  if (!(ab / Math.sqrt(sa * sb || 1) > 0.9)) f.push('the traces were left facing each other');
  P.flipNode(0);
  if (!P.imported[0].mags.every((v, i) => v === before[i])) f.push('twice is not where it started');
  if (sends.length) f.push('a set nobody sent was sent');
  P.place.sent = true;
  P.setLink({ request: async () => Uint8Array.of(0) });
  P.flipNode(0);
  for (let i = 0; i < 8; i++) await Promise.resolve();
  if (sends.length !== 1) f.push(`a held set was sent ${sends.length} times`);
  P.place.sel = -1; P.renderImport();
  if (!els.polFlip.disabled) f.push('Flip is offered with nothing picked');
  T(!f.length, `the inspector says a node's polarity and Flip turns it over${f.length ? ' — ' + f.join('; ') : ''}`);
  globalThis.KYK.putWorld = realPut;
  P.setLink(null);
  P.heldLost();
  P.imported.length = 0;
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
for (const [name, t, r] of CASES) {
  P.setTel(t);
  P.setReso(r || null);
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
    /* and the model tab, which draws the same readout on panes of its own */
    P.setView('model'); P.drawModel(); P.renderModelChips(); P.setView('play');
    console.log(`  ok    ${name}`);
  } catch (e) {
    bad++;
    console.log(`  THROW ${name}\n          ${e.constructor.name}: ${e.message}`);
    const line = (e.stack || '').split('\n').find(l => /anonymous|Function/.test(l));
    if (line) console.log(`          ${line.trim()}`);
  }
}
/* The two firmwares (Combust, 28 September): the module's name says which,
   and the page shows only that firmware's controls. The wavetable one has no
   model tab; the modal one no build tab, world loop or world morph; the
   desktop bridge and firmware from before the split, everything. A view left
   on a tab its firmware does not have moves to one it does */
{
  const fails = [];
  const want = (name, mode) => { const m = P.fwModeOf(name); if (m !== mode) fails.push(`"${name}" read as ${m}, not ${mode}`); };
  want('Bongs', 'modal'); want('Kyklophoria Modal', 'modal'); want('Buzzes', 'wavetable'); want('Kyklophoria Wavetable', 'wavetable'); want('Kyklophoria', 'both'); want(undefined, 'both');
  const shows = (mode, view, tabs, rows) => {
    P.setView(view); P.setFwMode(mode); P.applyFwMode();
    const t = { play: !els.tabPlay.hidden, model: !els.tabModel.hidden, build: !els.tabBuild.hidden }, r = { tour: !els.tourRow.hidden, morph: !els.morphSel.hidden };
    for (const k in tabs) if (t[k] !== tabs[k]) fails.push(`${mode}: the ${k} tab ${t[k] ? 'shown' : 'hidden'}`);
    for (const k in rows) if (r[k] !== rows[k]) fails.push(`${mode}: the ${k} row ${r[k] ? 'shown' : 'hidden'}`);
    return P.getView();
  };
  if (shows('wavetable', 'model', { play: true, model: false, build: true }, { tour: true, morph: true }) !== 'play') fails.push('wavetable: left on the model tab');
  /* Buzzes (the wavetable firmware, named 30 September): its own name */
  if (els.fwName.textContent !== 'Buzzes') fails.push(`wavetable: the page is named "${els.fwName.textContent}", not Buzzes`);
  if (shows('modal', 'build', { play: false, model: true, build: false }, { tour: false, morph: false }) !== 'model') fails.push('modal: left on the build tab');
  /* Bongs (the modal firmware): no wavetable play view — a view left on it
     moves to the model tab, which is called play there — and its own name */
  if (shows('modal', 'play', { play: false, model: true, build: false }, { tour: false, morph: false }) !== 'model') fails.push('modal: left on the wavetable play tab');
  if (els.tabModel.textContent !== 'play') fails.push(`modal: the model tab is labelled "${els.tabModel.textContent}", not play`);
  if (els.fwName.textContent !== 'Bongs') fails.push(`modal: the page is named "${els.fwName.textContent}", not Bongs`);
  shows('both', 'play', { play: true, model: true, build: true }, { tour: true, morph: true });
  if (els.tabModel.textContent !== 'model' || els.fwName.textContent !== 'Kyklophoria') fails.push('both: the model tab or the name not put back');
  /* each firmware's own page (?page=): the mode fixed; a module on the
     other firmware named, with a link; the same firmware, nothing said */
  P.setPageMode('modal'); P.renderPageLinks();
  if (!P.pageMismatch('wavetable') || els.pageSwitch.hidden) fails.push('the modal page reaching a wavetable module said nothing');
  if (!/Buzzes/.test(els.pageSwitch.textContent)) fails.push(`the banner names the other firmware "${els.pageSwitch.textContent}", not Buzzes`);
  if (P.pageMismatch('modal') || !els.pageSwitch.hidden) fails.push('the modal page reaching a modal module complained');
  if (P.pageMismatch('both')) fails.push('the modal page complained of a module that names no firmware');
  if (els.pageLinks.children.length !== 2) fails.push(`the modal page links ${els.pageLinks.children.length} pages, not the wavetable one and both`);
  P.setPageMode(null); P.renderPageLinks();
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} two firmwares: each name shows its own tabs and rows${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The audition stops when what it plays goes (the 2026-09-13 review): a
   re-place clears the selection, and leaving the build tab leaves its stop
   button behind; both kept it playing. And stopped, every node is let go */
{
  const fails = [];
  let playing = 0, connected = 0;
  class FakeAC {
    constructor() { this.state = 'running'; this.currentTime = 0; this.sampleRate = 48000; this.destination = {}; }
    createBuffer(c, n) { return { copyToChannel() {} }; }
    createBufferSource() { return { connect() { connected++; }, disconnect() { connected--; }, start() { playing++; }, stop() { playing--; } }; }
    createGain() { return { gain: { value: 0, setTargetAtTime() {} }, connect() { connected++; }, disconnect() { connected--; } }; }
    resume() {}
  }
  const was = globalThis.AudioContext; globalThis.AudioContext = FakeAC;
  const one = () => { P.imported.length = 0; P.imported.push({ name: 'a', mags: new Float32Array(64), fit: 1, mode: 'shape', pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(256), thumb: new Float32Array(256) }); };
  one(); P.setView('build'); P.selectNode(0); P.auditionStart();
  if (playing !== 3) fails.push(`the audition started ${playing} sources, not 3`);
  P.placeOnCell();
  if (playing !== 0) fails.push(`a re-place left ${playing} sources playing`);
  P.selectNode(0); P.auditionStart(); P.setView('play');
  if (playing !== 0) fails.push(`leaving the build tab left ${playing} sources playing`);
  if (connected !== 0) fails.push(`${connected} audio connections left after stopping`);
  globalThis.AudioContext = was; P.getAudition().ctx = null; P.imported.length = 0;
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the audition stops when what it plays goes, and lets its nodes go${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* One world on the wire at a time (the 2026-09-13 review): the build tab's
   send and a drag's re-send each put a world in chunks, and only the drag
   coalesced its own, so a send during a drag interleaved two worlds' chunks
   and the module refused one mid-stream */
{
  const fails = [];
  const log = [];
  P.setLink({ kind: 'serial', maxBody: 300, close: async () => {},
              request: async (cmd, body) => {
                if (body.length >= 8) { const dv = new DataView(body.buffer, body.byteOffset); log.push([dv.getUint32(0, true), dv.getUint32(4, true), body.length - 8]); }
                await new Promise(r => setImmediate(r));
                return new Uint8Array([0]);
              } });
  P.imported.length = 0;
  for (let i = 0; i < 3; i++)
    P.imported.push({ name: 'x' + i, mags: new Float32Array(64), fit: 1, mode: 'shape', pos: [0.5, 0.5, 0.5, 0.5], render: new Float32Array(64) });
  P.placeOnCell(); P.place.sent = true;
  await Promise.all([P.syncPlacement(), P.sendImported()]);
  /* every transfer runs from offset 0 to its end before the next starts */
  let at = -1, total = 0, transfers = 0;
  for (const [tot, off, len] of log) {
    if (off === 0) { if (at >= 0 && at < total) { fails.push(`a world started while the last stood at ${at} of ${total}`); break; } transfers++; total = tot; at = 0; }
    if (off !== at) { fails.push(`a chunk at ${off} where ${at} was next: two worlds interleaved`); break; }
    at = off + len;
  }
  if (!fails.length && transfers !== 2) fails.push(`${transfers} transfers, not 2`);
  P.imported.length = 0; P.heldLost(); P.setLink(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} one world on the wire at a time: a send during a drag's re-send waits for it${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* Gone is gone (the 2026-09-13 review): a disconnect clears the status line
   and the morph line, which kept the dead module's values and read as live;
   and a quick reconnect runs one set of poll loops, not two — a loop tested
   only that *a* link was up, so the old connection's loops woke into the new
   one and polled beside its own */
{
  const fails = [];
  P.setTel(tel({ n: 4, world: 0 })); P.drawStatus();
  if (els.sBlock.textContent === '—') fails.push('the status line was never drawn');
  await P.disconnect();
  for (const id of ['sBlock', 'sF0', 'sKcut', 'sLink', 'sCpu', 'sSpace']) if (els[id].textContent !== '—') fails.push(`${id} still reads ${els[id].textContent}`);
  if (!els.morphSel.disabled || !els.cardSel.disabled) fails.push('the morph and card selects stayed live');
  /* two links in quick succession, each starting its loops: count the second's requests */
  const reqs = [0, 0];
  const mk = i => ({ kind: 'serial', request: async () => { reqs[i]++; return new Uint8Array([0]); }, close: async () => {} });
  P.setPanel({ pages: 1, pots: 1, names: ['Play'], colors: [''], grid: [[{ name: 'Coarse', id: 'c', disp: { kind: 'norm' } }]] });
  P.setFwMode('modal'); P.renderPanelRows(); P.setView('model');
  /* the loops' sleeps queued here and woken a round at a time (the stub's
     setTimeout never fires) */
  const q = [], stub = globalThis.setTimeout;
  globalThis.setTimeout = f => { q.push(f); return 0; };
  const settle = async () => { for (let i = 0; i < 6; i++) await new Promise(r => setImmediate(r)); };
  const round = async () => { const now = q.splice(0); for (const f of now) f(); await settle(); };
  const a = mk(0), b = mk(1);
  P.setLink(a); P.setPolling(true); P.pollPots(); await settle();
  P.setLink(b); P.pollPots(); await settle();        /* reconnected: the new link's loop */
  for (let i = 0; i < 3; i++) await round();
  P.setPolling(false); await round(); await round();
  globalThis.setTimeout = stub;
  if (reqs[1] !== 4) fails.push(`the new link was polled ${reqs[1]} times over its first poll and three rounds (4 for one loop): the old loop polled it too`);
  P.setView('play'); P.setFwMode('both'); P.renderPanelRows(); P.setPanel(null); P.setLink(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} a disconnect clears the readouts, and a quick reconnect polls once${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* Bongs' panel on the page: a row a page from the descriptor, a selector a
   menu with its labels, a range a slider in its own units; the module's
   values shown; a move sent as 0x70 set (page, pot, value); a control just
   moved not overwritten by the next read; and no rows on the page for both */
{
  const fails = [];
  const sent = [];
  P.setLink({ request: async (cmd, body) => { sent.push([cmd, Array.from(body)]); return new Uint8Array([0]); }, kind: 'serial' });
  P.setPanel({ pages: 2, pots: 3, names: ['Play', 'Exciter'], colors: ['#67e8f9', '#ff8a5b'],
               grid: [[{ name: 'Coarse', id: 'pitch.coarse', disp: { kind: 'linear', lo: -3, hi: 3, unit: 'oct' } }, null, { name: 'Body', id: 'pos.0', disp: { kind: 'norm' } }],
                      [{ name: 'Exciter', id: 'exc.type', disp: { kind: 'snap', labels: ['Recorded', 'Hammer', 'Pluck', 'Bow', 'Reed', 'Lips', 'Trained'] } }, { name: 'Timbre', id: 'exc.timbre', disp: { kind: 'norm' } }, null]] });
  P.setFwMode('modal'); P.renderPanelRows();
  const ctl = P.getPotCtl();
  if (!ctl || ctl.length !== 2) fails.push('no rows for two pages');
  else {
    if (!ctl[1][0] || ctl[1][0].el.tagName !== 'select' || ctl[1][0].el.children.length !== 7) fails.push('the exciter type is not a menu of seven');
    if (!ctl[0][0] || ctl[0][0].el.tagName !== 'input') fails.push('coarse is not a slider');
    if (ctl[0][1] !== null) fails.push('an empty pot drew a control');
    P.applyPots({ pages: 2, pots: 3, live: 0, vals: [[0.5, 0, 0.25], [0.99, 0.5, 0]] });
    if (ctl[0][0].val.textContent !== '0.00 oct') fails.push(`coarse at its centre reads "${ctl[0][0].val.textContent}"`);
    if (ctl[1][0].val.textContent !== 'Trained' || ctl[1][0].el.value !== '6') fails.push(`the type at the top reads "${ctl[1][0].val.textContent}" (${ctl[1][0].el.value})`);
    if (ctl[0][2].val.textContent !== '25%') fails.push(`body reads "${ctl[0][2].val.textContent}"`);
    ctl[1][0].el.value = '4'; ctl[1][0].el.onchange();
    await new Promise(r => setImmediate(r));
    const last = sent[sent.length - 1];
    const q = Math.round((4.5 / 7) * 65535);
    if (!last || last[0] !== 0x70 || last[1][0] !== 1 || last[1][1] !== 1 || last[1][2] !== 0 || last[1][3] !== (q & 0xff) || last[1][4] !== (q >> 8))
      fails.push(`choosing Reed sent ${JSON.stringify(last)}`);
    P.applyPots({ pages: 2, pots: 3, live: 0, vals: [[0.5, 0, 0.25], [0.99, 0.5, 0]] });
    if (ctl[1][0].el.value !== '4') fails.push('a read overwrote the menu the hand had just moved');
  }
  P.setFwMode('both'); P.renderPanelRows();
  if (!els.panelRows.hidden || P.getPotCtl()) fails.push('rows on the page for both');
  P.setPanel(null); P.setLink(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} Bongs' panel on the page: a row a page, menus and sliders, sent and held${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The loop's EVERY menu is World P3, not a second copy of it.
 *
 * The division used to live in two places that never met: this menu went to
 * the tour in the TOUR set command, and World P3 went nowhere (the 2026-10-01
 * review). The module now reads the pot, so the menu mirrors it through 0x70:
 * a read of the module's pots moves the menu, and the menu stores the pot —
 * never a set command, which would start the loop over. A page whose module
 * describes no panel falls back to the set command, which on current firmware
 * writes the same pot.
 */
{
  const fails = [];
  const sent = [];
  P.setLink({ request: async (cmd, body) => { sent.push([cmd, Array.from(body || [])]);
                return cmd === KYK.CMD.tour ? Uint8Array.from([0, 3, (body && body[0] === 1) ? body[1] : 1, 0, 0, 13, 14, 15]) : new Uint8Array([0]); },
              close: async () => {} });
  /* the wavetable panel, World P3 a selector of the eight divisions as the
     firmware describes it */
  const pnl = { ...parsed, grid: parsed.grid.map(row => row.map(f => f && f.id === 'tour.div'
    ? { ...f, disp: { kind: 'snap', labels: ['1', '2', '3', '4', '6', '8', '12', '16'] } } : f)) };
  P.setPanel(pnl); P.setFwMode('wavetable'); P.renderPanelRows();
  P.setDesc({ kyk: { ext: 7 } });            /* firmware that reads World P3 (4 October on) */
  await P.readTour();
  const vals = Array.from({ length: 7 }, () => new Array(6).fill(0.5));
  vals[6][2] = 5.5 / 8;
  P.applyPots({ pages: 7, pots: 6, live: 0, vals });
  if (els.tourDiv.value !== '8') fails.push(`World P3 stored at 8 shows as every ${els.tourDiv.value}`);
  sent.length = 0;
  els.tourDiv.value = '4'; els.tourDiv.onchange();
  for (let i = 0; i < 8; i++) await Promise.resolve();
  const q = Math.round((3.5 / 8) * 65535);
  const pot = sent.find(x => x[0] === KYK.CMD.pots);
  if (!pot || pot[1][0] !== 1 || pot[1][1] !== 6 || pot[1][2] !== 2 || pot[1][3] !== (q & 0xff) || pot[1][4] !== (q >> 8))
    fails.push(`every 4 stored ${JSON.stringify(pot)}, not World P3 at 3.5/8`);
  if (sent.some(x => x[0] === KYK.CMD.tour)) fails.push('and sent the loop again, which starts it over');
  const row = P.getPotCtl()[6][2];
  if (!row || row.el.value !== '3') fails.push(`the panel's own World P3 row reads ${row && row.el.value}`);
  /* firmware from before ext 7 has the same ident on World P3 and never read
     it: the menu stays the loop's own and a change goes in the set command
     (the review of the night's work: the menu showed the dead pot's 6 and
     the next stop sent it to a running loop) */
  P.setDesc({ kyk: { ext: 6 } });
  P.applyPots({ pages: 7, pots: 6, live: 0, vals: Array.from({ length: 7 }, () => new Array(6).fill(0.5)) });
  if (els.tourDiv.value === '6') fails.push('on ext 6 firmware the menu mirrored the dead pot (every 6)');
  sent.length = 0;
  els.tourDiv.value = '3'; els.tourDiv.onchange();
  for (let i = 0; i < 8; i++) await Promise.resolve();
  if (sent.some(x => x[0] === KYK.CMD.pots)) fails.push('on ext 6 firmware a division was written to the dead pot');
  const old = sent.find(x => x[0] === KYK.CMD.tour);
  if (!old || old[1][1] !== 3) fails.push(`on ext 6 firmware every 3 sent ${JSON.stringify(old)}, not the set command`);
  P.setDesc({ kyk: { ext: 7 } });
  /* no panel described: the set command carries it */
  P.setPanel(null); P.renderPanelRows();
  sent.length = 0;
  els.tourDiv.value = '2'; els.tourDiv.onchange();
  for (let i = 0; i < 8; i++) await Promise.resolve();
  const set = sent.find(x => x[0] === KYK.CMD.tour);
  if (!set || set[1][0] !== 1 || set[1][1] !== 2) fails.push(`with no panel, every 2 sent ${JSON.stringify(set)}`);
  P.setLink(null); P.setDesc(null);
  await P.readTour();
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the loop's EVERY menu mirrors World P3 through 0x70${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The panel on the page, laid out as the module is (Combust, 1 October: "3
   rows of 2 columns, numbered left to right, top to bottom"), on both
   firmwares' pages and not on the page for both, which keeps the canvas
   mirror. Then what it shows beyond the stored values: the page B1 is on,
   where the hand is when a pot has not caught, where the sound is on an
   axis, and the pots the instrument is designed to leave doing nothing. */
{
  const fails = [];
  const grid = p => els.panelRows.children[p].children[1];
  const cellText = c => (c.className === 'bmark') ? c.textContent : (c.children[0] && c.children[0].children[0] ? c.children[0].children[0].textContent : '?');
  P.setLink(null);
  P.setPanel(parsed); P.setFwMode('wavetable'); P.renderPanelRows();
  const ctl = P.getPotCtl();
  if (!ctl || ctl.length !== 7) fails.push(`the wavetable page has no panel of seven pages (${ctl && ctl.length})`);
  else {
    const order = grid(0).children.map(cellText).join(' ');
    if (order !== 'P1 B1 P2 P3 B2 P4 P5 B3 P6') fails.push(`page 1 reads "${order}", not P1 B1 P2 / P3 B2 P4 / P5 B3 P6`);
    /* every page the same shape, gaps included: Couple and World have none at P6 */
    for (let p = 0; p < 7; p++) if (grid(p).children.length !== 9) fails.push(`page ${p + 1} has ${grid(p).children.length} cells, not nine`);
    if (grid(5).children[8].className !== 'pot empty') fails.push(`Couple's missing P6 is "${grid(5).children[8].className}", not an empty place`);
    /* the page B1 is on is the one shown; the others wait behind their tabs */
    P.setTel(tel({ n: 4, page: 4, pots: [0, 0, 0, 0, 0, 0], knobs: [0, 0.4, 0.5, 0.25, 0, 0.33] }));
    P.drawPanelLive(true);
    if (els.panelRows.children[4].hidden || !els.panelRows.children[0].hidden) fails.push('the page B1 is on is not the one shown');
    if (!/live/.test(els.panelTabs.children[4].className)) fails.push('its tab is not marked live');
    /* Gravity at the bottom: the orbit's other knobs do nothing, and say so */
    const kep = ctl[4];
    if (!/inert/.test(kep[1].cell.className) || !/Gravity is off/.test(kep[1].sub.textContent)) fails.push(`with Gravity off, Eccentricity reads "${kep[1].cell.className}" "${kep[1].sub.textContent}"`);
    if (/inert/.test(kep[0].cell.className)) fails.push('Gravity itself was marked as doing nothing');
    P.setTel(tel({ n: 4, page: 4, pots: [0.6, 0.4, 0.5, 0.25, 0, 0.33], knobs: [0.6, 0.4, 0.5, 0.25, 0, 0.33] }));
    P.drawPanelLive(false);
    if (/inert/.test(kep[1].cell.className)) fails.push('Gravity up and Eccentricity still marked dead');
    /* a pot that has not caught: the hand is marked where it is */
    P.setTel(tel({ n: 4, page: 0, pots: [0.5, 0.5, 0.9, 0.5, 0.5, 0.5], knobs: [0.5, 0.5, 0.2, 0.5, 0.5, 0.5], posL: Float32Array.from([0.2, 0.5, 0.5, 0.5]) }));
    P.drawPanelLive(false);
    const p3 = ctl[0][2];
    if (p3.hand.hidden || p3.hand.style.left !== '90.0%' || !/catch/.test(p3.sub.className)) fails.push(`P3 not caught: hand ${p3.hand.hidden ? 'hidden' : p3.hand.style.left}, "${p3.sub.textContent}"`);
    if (!ctl[0][3].hand.hidden) fails.push('a caught pot showed a hand');
    /* the sound: P4 is axis 1, stored at 0.5, heard at 0.8 after the rotation */
    P.setTel(tel({ n: 4, page: 0, pots: [0.5, 0.5, 0.2, 0.5, 0.5, 0.5], knobs: [0.5, 0.5, 0.2, 0.5, 0.5, 0.5], posL: Float32Array.from([0.2, 0.8, 0.5, 0.5]) }));
    P.drawPanelLive(false);
    const p4 = ctl[0][3];
    if (p4.sdot.hidden || p4.sdot.style.left !== '80.0%' || !/sound at 80%/.test(p4.sub.textContent)) fails.push(`P4 heard elsewhere: dot ${p4.sdot.hidden ? 'hidden' : p4.sdot.style.left}, "${p4.sub.textContent}"`);
    /* the World page on a four-axis world, and World morph with no target */
    P.setTel(tel({ n: 4, page: 6, morphWorld: 0xFF }));
    P.drawPanelLive(false);
    if (!/inert/.test(ctl[6][0].cell.className) || !/4-axis/.test(ctl[6][0].sub.textContent)) fails.push(`World P1 on a 4-axis world: "${ctl[6][0].sub.textContent}"`);
    P.setTel(tel({ n: 6, page: 6, morphWorld: 0xFF }));
    P.drawPanelLive(false);
    if (/inert/.test(ctl[6][0].cell.className)) fails.push('World P1 marked dead on a six-axis world');
    /* the canvas mirror is the page for both's, not drawn under the panel */
    arcLog.length = 0;
    P.setTel(tel({ pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
    P.drawSound();
    if (arcLog.some(a => a.length >= 5 && Math.abs(a[3] - Math.PI * 0.75) < 1e-9)) fails.push('the canvas mirror was drawn beside the panel');
  }
  /* Bongs: the same panel; its Play P3–P6 named for the world's axes */
  P.setPanel({ pages: 1, pots: 6, names: ['Play'], colors: ['#67e8f9'], grid: [[
    { name: 'Coarse', id: 'pitch.coarse', disp: { kind: 'norm' } }, { name: 'Fine', id: 'pitch.fine', disp: { kind: 'norm' } },
    { name: 'Position 0', id: 'pos.0', disp: { kind: 'norm' } }, { name: 'Position 1', id: 'pos.1', disp: { kind: 'norm' } },
    { name: 'Position 2', id: 'pos.2', disp: { kind: 'norm' } }, { name: 'Position 3', id: 'pos.3', disp: { kind: 'norm' } }]] });
  P.setFwMode('modal'); P.renderPanelRows();
  P.setReso({ kind: 0, form: 1, lo: 36, hi: 96, param: 60, P: 11, points: [36, 96], modes: [], name: 'wurli' });
  P.setTel(tel({ ...RES, page: 0 }));
  P.drawPanelLive(true);
  const names = P.getPotCtl()[0].slice(2).map(c => c.lab.textContent).join(',');
  if (names !== 'Body,Velocity,Decay,Coil') fails.push(`a pickup world's Play P3–P6 read ${names}`);
  P.setReso({ kind: 0, form: 0, lo: 36, hi: 96, param: 60, P: 11, points: [36, 96], modes: [], name: 'marimba' });
  P.drawPanelLive(true);
  const plain = P.getPotCtl()[0].slice(2).map(c => c.lab.textContent).join(',');
  if (plain !== 'Position,Velocity,Decay,Brightness') fails.push(`a plain world's Play P3–P6 read ${plain}`);
  /* and the page for both has none of it */
  P.setFwMode('both'); P.renderPanelRows();
  if (P.getPotCtl() || !els.panel.hidden) fails.push('a panel on the page for both');
  P.setTel(null); P.setReso(null); P.setPanel(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the panel on the page: laid out as the module, the hand, the sound, the pots that do nothing${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* What each firmware's page asks the module for: Bongs draws no frame and
   no spectrum, so it asks for the motion block alone; Buzzes and the page
   for both ask for all three */
{
  const fails = [];
  P.setFwMode('modal'); if (P.telFlags() !== 4) fails.push(`Bongs asks for ${P.telFlags()}, not 4`);
  P.setFwMode('wavetable'); if (P.telFlags() !== 7) fails.push(`Buzzes asks for ${P.telFlags()}, not 7`);
  P.setFwMode('both'); if (P.telFlags() !== 7) fails.push(`the page for both asks for ${P.telFlags()}, not 7`);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} each page asks the module for what it draws${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The live output scope (telemetry flag 8, core/kyk_scope.h).
 *
 * Asked for only where the module says it serves it: firmware before the scope
 * echoes an unknown flag back without serving it, so a page that asked would
 * read the motion block from the wrong offset. Parsed only when the echoed
 * flags still carry the bit — the module clears it when it has no window — so
 * absent is null, never a window of zeros. And drawn, on both panes: a check
 * that only asked whether drawing threw would pass with nothing on screen. */
{
  const fails = [], K = globalThis.KYK;
  /* a telemetry reply as the module builds it: status, the fixed part for a
     2-axis world with no payload, then the scope if given, then the motion
     block ending in the live world (7) and its kind (1) */
  const body = (flags, scope) => {
    const out = [0];
    const u32 = v => out.push(v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255);
    const f32 = v => { const d = new DataView(new ArrayBuffer(4)); d.setFloat32(0, v, true); out.push(...new Uint8Array(d.buffer)); };
    u32(1234); f32(110); out.push(2, 64, 64, 0, 1, flags, 0, 0); f32(0);
    for (let i = 0; i < 4 * 2 + 1; i++) f32(0.5);
    if (scope) {
      out.push((scope.trig ? 1 : 0) | (scope.r ? 0 : 2), scope.shift, scope.l.length, 5, 9, 0);
      for (const v of scope.l) out.push(v & 255);
      if (scope.r) for (const v of scope.r) out.push(v & 255);
    }
    if (flags & 4) {
      out.push(0, 0); for (let i = 0; i < 5; i++) f32(0); out.push(0);   /* kepler, couple, lock, sharp */
      /* bodies, page, morph, morph_world, mute, aimed, pots[6], vals_valid +
         vals[6], world, tour[3], kind — exactly as EncodeTelemetry writes it */
      out.push(1, 0, 0, 0xFF, 0, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 1);
    }
    return Uint8Array.from(out);
  };
  const L = [0, 64, 127, -127], R = [-64, -32, 0, 32];
  /* every motion field, not only the last ones: the tail is read to the
     length it has, so a parse from the wrong offset can land on the right
     world byte by luck — it did, the first time this was written */
  const motionOk = t => t.bodies === 1 && t.page === 0 && t.morphWorld === 0xFF && !t.aimed && t.knobs === null &&
    t.pots && t.pots.map(v => Math.round(v * 255)).join() === '1,2,3,4,5,6' && t.world === 7 && t.kind === 1 && t.tour && t.tour.len === 0;
  const t1 = K.parseTelemetry(body(4 | 8, { trig: true, shift: 3, l: L }));
  if (!t1.scope) fails.push('a served scope did not parse');
  else {
    if (Math.abs(t1.scope.l[1] - 64 / 1024) > 1e-9 || Math.abs(t1.scope.l[2] - 127 / 1024) > 1e-9 || t1.scope.l[3] >= 0) fails.push(`decoded ${Array.from(t1.scope.l).join(',')}`);
    if (!t1.scope.triggered || !t1.scope.mono || t1.scope.gain !== 8 || t1.scope.seq !== 9) fails.push('the header fields');
    if (t1.scope.r !== t1.scope.l) fails.push('mono R is not L');
    if (Math.abs(t1.scope.ms - 1000 * 4 * 5 / 48000) > 1e-9) fails.push(`window ${t1.scope.ms} ms`);
  }
  if (!motionOk(t1)) fails.push(`the motion block after the scope read wrong: world ${t1.world}, kind ${t1.kind}, pots ${t1.pots}`);
  const t2 = K.parseTelemetry(body(4 | 8, { trig: false, shift: 0, l: L, r: R }));
  if (!t2.scope || t2.scope.mono || Math.abs(t2.scope.r[0] + 0.5) > 1e-9 || t2.scope.triggered) fails.push('a stereo, free-running window');
  if (!motionOk(t2)) fails.push('the motion block after a stereo scope');
  /* the module had no window: bit 3 cleared, no block. Null, and the motion
     block read from where it really starts */
  const t3 = K.parseTelemetry(body(4));
  if (t3.scope !== null) fails.push(`no window parsed as ${t3.scope}`);
  if (!motionOk(t3)) fails.push(`the motion block without a scope: pots ${t3.pots}, page ${t3.page}`);
  /* who asks */
  P.setDesc(null);
  P.setFwMode('modal'); if (P.telFlags() !== 4) fails.push(`Bongs on an older module asks for ${P.telFlags()}`);
  P.setFwMode('wavetable'); if (P.telFlags() !== 7) fails.push(`Buzzes on an older module asks for ${P.telFlags()}`);
  P.setDesc({ kyk: { ext: 6 } });
  if (P.telFlags() !== 7) fails.push(`a descriptor without telflags gets ${P.telFlags()}`);
  P.setDesc({ kyk: { ext: 6, telflags: 15 } });
  P.setFwMode('modal'); if (P.telFlags() !== 12) fails.push(`Bongs on a module with the scope asks for ${P.telFlags()}, not 12`);
  P.setFwMode('wavetable'); if (P.telFlags() !== 15) fails.push(`Buzzes on a module with the scope asks for ${P.telFlags()}, not 15`);

  /* drawn: the trace is 2 × npts segments (the glow and the line), R another
     npts when the ears differ, on Buzzes' sound pane and Bongs' */
  const sine = (n, a) => Float32Array.from({ length: n }, (_, i) => a * Math.sin(2 * Math.PI * 4.4 * i / n));
  const sc = (over = {}) => { const l = sine(192, 0.1); return Object.assign({ triggered: true, mono: true, shift: 3, gain: 8, npts: 192, spp: 5, seq: 1, ms: 20, l, r: l }, over); };
  const segs = (draw, t) => { P.setTel(t); segLog.length = 0; draw(); return segLog.length; };
  P.setPanel(null); P.setView('play');
  for (const [name, s] of [['mono, triggered', sc()],
                           ['stereo, free-running', sc({ mono: false, triggered: false, r: sine(192, -0.05) })],
                           ['quiet, ×128', sc({ shift: 7, gain: 128, l: sine(192, 0.004), r: null })],
                           ['coarse: 48 points', sc({ npts: 48, spp: 20, l: sine(48, 0.3), r: sine(48, 0.3) })],
                           ['one point', sc({ npts: 1, l: Float32Array.of(0.2), r: Float32Array.of(0.2) })],
                           ['silence', sc({ shift: 0, gain: 1, l: new Float32Array(192), r: new Float32Array(192) })]]) {
    if (!s.r) s.r = s.l;
    const want = s.npts * (s.mono ? 2 : 3);
    const without = segs(P.drawSound, tel({ scope: null })), withs = segs(P.drawSound, tel({ scope: s }));
    if (withs - without < want) fails.push(`Buzzes, ${name}: ${withs - without} segments for the trace, want ${want}`);
    const r0 = segs(P.drawSound, tel({ ...RES, scope: null })), r1 = segs(P.drawSound, tel({ ...RES, scope: s }));
    if (r1 - r0 < want) fails.push(`a resonator on the sound pane, ${name}: ${r1 - r0} segments`);
    P.setView('model');
    const m0 = segs(P.drawModel, tel({ ...RES, scope: null })), m1 = segs(P.drawModel, tel({ ...RES, scope: s }));
    if (m1 - m0 < want) fails.push(`Bongs' pane, ${name}: ${m1 - m0} segments`);
    P.setView('play');
  }
  /* and a module that does not serve it gets the pane it always had */
  P.setDesc(null);
  if (segs(P.drawSound, tel({ scope: sc() })) !== segs(P.drawSound, tel({ scope: null }))) fails.push('a module without the scope drew one');
  P.setFwMode('both'); P.setTel(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the live output scope: asked for where it is served, parsed where it is sent, drawn on both panes${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The small things from the review's audit: a connect in flight says so and
   cannot be pressed twice (A1); a focused menu keeps the arrow keys (A37);
   Bongs' worlds tab has no Buzzes verbs and no "the 0 that ship" (A17); a
   message that is not a fault expires and one that is does not (A5) */
{
  const fails = [];
  /* A1: the connect button while the handshake runs */
  {
    let release;
    const gate = new Promise(r => { release = r; });
    globalThis.KYK.Link = class { constructor() { this.stats = {}; } async start() { await gate; throw new Error('no module'); } async close() {} };
    const pend = P.connectTo({ kind: 'serial', close: async () => {} });
    await new Promise(r => setImmediate(r));
    if (!els.btnSerial.disabled || !/Connecting/.test(els.btnSerial.textContent)) fails.push(`while connecting the button reads "${els.btnSerial.textContent}", ${els.btnSerial.disabled ? 'disabled' : 'live'}`);
    release(); await pend;
    if (els.btnSerial.disabled || els.btnSerial.textContent !== 'Connect') fails.push(`after a failed connect it reads "${els.btnSerial.textContent}", ${els.btnSerial.disabled ? 'disabled' : 'live'}`);
  }
  /* A37: arrows in a menu are the menu's */
  {
    P.setTel(tel()); P.setAxes([0, 1]);
    const keys = globalThis.__keydown;
    keys({ key: 'ArrowRight', target: { tagName: 'SELECT' } });
    if (P.getAxes().join() !== '0,1') fails.push(`an arrow in a menu moved the projection to ${P.getAxes().join()}`);
    keys({ key: 'ArrowRight', target: { tagName: 'BODY' } });
    if (P.getAxes().join() === '0,1') fails.push('an arrow outside any control did not move the projection');
    P.setAxes([0, 1]); P.setTel(null);
  }
  /* A17: Bongs' worlds tab */
  P.setFwMode('modal'); P.applyFwMode();
  if (!els.libTarget.hidden || !els.libEdit.hidden || !els.libNoTarget.hidden) fails.push('Bongs offers morph and edit on its worlds tab');
  P.setWorlds({ count: 0, current: 0xFF, list: [] }); P.renderLibrary();
  if (els.slots.children.some(c => c.className === 'libhead' && /that ship/.test(c.textContent))) fails.push('Bongs\' worlds tab says "the 0 that ship"');
  P.setFwMode('wavetable'); P.applyFwMode();
  if (els.libTarget.hidden) fails.push('Buzzes lost "morph towards"');
  P.setFwMode('both'); P.applyFwMode(); P.setWorlds(null);
  /* A5: what expires */
  {
    const q = [], stub = globalThis.setTimeout;
    globalThis.setTimeout = f => { q.push(f); return 0; };
    P.say('saved x.kykw'); q.splice(0).forEach(f => f());
    if (els.msg.textContent !== '') fails.push(`an ordinary message stayed: "${els.msg.textContent}"`);
    P.say('link error: gone', true); q.splice(0).forEach(f => f());
    if (!/link error/.test(els.msg.textContent)) fails.push('a fault expired');
    P.say('a'); P.say('b'); q.splice(0, 1).forEach(f => f());
    if (els.msg.textContent !== 'b') fails.push('an old message\'s timer cleared a newer message');
    globalThis.setTimeout = stub; els.msg.textContent = '';
  }
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the audit's small things: connecting, the arrow keys, Bongs' worlds tab, messages that expire${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The voice's own tuning, from its modes: a G4 built 38 cents sharp says so;
   one in tune says nothing worth showing; a bell with no partial near a
   harmonic says nothing at all */
{
  const fails = [];
  const g4 = 440 * Math.pow(2, (67 - 69) / 12), sharp = Math.pow(2, 38 / 1200);
  const mk = (ratio, parts) => ({ kind: 0, param: 67, modes: parts.map(([n, g]) => ({ hz: n * g4 * ratio, zeta: 0.001, gain: g })) });
  const a = P.voiceTuning(mk(sharp, [[1, 0.5], [2, 0.3], [3, 0.2]]));
  if (!a || Math.abs(a.cents - 38) > 0.5) fails.push(`a G4 built 38 cents sharp reads ${a && a.cents.toFixed(1)}`);
  const b = P.voiceTuning(mk(1, [[1, 0.5], [2, 0.3]]));
  if (!b || Math.abs(b.cents) > 0.5) fails.push(`one in tune reads ${b && b.cents.toFixed(1)}`);
  const bell = P.voiceTuning({ kind: 0, param: 67, modes: [{ hz: g4 * 1.31, zeta: 0.001, gain: 0.5 }, { hz: g4 * 2.71, zeta: 0.001, gain: 0.4 }] });
  if (bell) fails.push(`an inharmonic voice read ${bell.cents.toFixed(1)} cents`);
  if (P.voiceTuning({ kind: 1, param: 3, modes: [{ hz: 400, zeta: 0.001, gain: 1 }] })) fails.push('an index world read a tuning');
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the voice's own tuning, from its modes${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The ring: a voice's modes struck by an impulse, sampled at the module's
   rate. One mode at 100 Hz is a decaying sine of eight periods (80 ms), from
   zero, and nothing at all is drawn for a voice with no modes. */
{
  const fails = [];
  const one = P.ringOf({ name: 'x', param: 1, modes: [{ hz: 100, zeta: 0.001, gain: 1 }] });
  let crossings = 0;
  for (let i = 1; i < one.y.length; i++) if ((one.y[i - 1] < 0) !== (one.y[i] < 0)) crossings++;
  if (Math.abs(one.ms - 80) > 0.5) fails.push(`the span is ${one.ms} ms, not 80`);
  if (Math.abs(one.y[0]) > 1e-9) fails.push(`it starts at ${one.y[0]}, not zero`);
  if (Math.abs(crossings - 16) > 1) fails.push(`${crossings} zero crossings, not sixteen`);
  const none = P.ringOf({ name: 'y', param: 1, modes: [{ hz: 0, zeta: 1, gain: 0 }] });
  if (none.y) fails.push('a voice with no modes drew a ring');
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the ring is the modes struck once${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The panel's selector: the rails are mirrors of the pot.
 *
 * World P6 (Buzzes) and Space P4 (Bongs) choose what plays, and the pot's
 * stored value is the one store (the 2026-10-01 review, design D4). So a
 * click on the world rail or the instrument rail writes that pot through
 * 0x70, where the module commits it at once, and never sends the select or
 * slot-live action the pot would not hear of. A module whose descriptor has
 * no such pot keeps the actions. The panel's own row for it names the
 * entries — the built-ins then the filled slots, the card's instruments — and
 * reads the stored value back as a name.
 */
{
  const fails = [];
  const sent = [];
  const q = [], stub = globalThis.setTimeout;
  globalThis.setTimeout = f => { q.push(f); return 0; };
  const settle = async () => { for (let i = 0; i < 8; i++) await new Promise(r => setImmediate(r)); };
  P.setLink({ kind: 'serial', request: async (cmd, body) => { sent.push([cmd, Array.from(body || [])]); return new Uint8Array([0]); }, close: async () => {} });
  const names = ['Crop', '24-cell', '16-cell', 'Tesseract', 'Stack', 'Field', 'Field II', 'Torus', 'Harmonic', 'FM', 'Vowel',
                 'Shapes', 'Shapes R', 'Lock', 'Unison', 'Plate', 'Bar', 'Drum', 'Saw', 'Pulse', 'Edge', 'Grit'];
  const W = { count: 22, current: 0, list: names.map((name, index) => ({ index, name, note: '', analytic: index !== 5 })) };
  const withSel = { ...parsed, grid: parsed.grid.map((row, p) => row.map((f, k) => p === 6 && k === 5 ? { name: 'World', id: 'world.sel', disp: { kind: 'norm' } } : f)) };
  const q16 = v => Math.round(v * 65535);
  const potSent = () => sent.find(x => x[0] === KYK.CMD.pots && x[1][0] === 1);
  const potVal = x => x[1][3] | (x[1][4] << 8);

  /* Buzzes, with the pot: the rail writes it */
  P.setPanel(withSel); P.setFwMode('wavetable'); P.renderPanelRows();
  P.setWorlds(W); P.setSlots({ count: 32, live: 0xFF, target: 0xFF, names: Array.from({ length: 32 }, (_, i) => i === 2 ? 'mine' : i === 5 ? 'yours' : null) });
  /* the panel's row names the entries and reads the value as one */
  const row = P.getPotCtl()[6][5];
  if (!row || row.el.tagName !== 'select') fails.push('World P6 on the panel is not a menu');
  else {
    const L = P.selLayout(row);
    if (L.fixed !== 22 || L.den !== 26 || L.extra !== 2) fails.push(`World P6's layout is ${JSON.stringify(L)}`);
    const vals = Array.from({ length: 7 }, () => new Array(6).fill(0.5));
    vals[6][5] = 22 / 26 + (4 / 26) * 0.75;           /* the second filled slot */
    P.applyPots({ pages: 7, pots: 6, live: 0, vals });
    if (row.val.textContent !== 'yours' || row.el.children.length !== 24) fails.push(`World P6 near the top reads "${row.val.textContent}" of ${row.el.children.length}`);
  }
  P.setTel(tel({ world: 0 }));
  P.renderWorldBar();
  const fm = els.worldChips.children.find(b => b.textContent === 'FM');
  if (!fm) fails.push('the world rail has no FM');
  else {
    fm.onclick(); await settle();
    const x = potSent();
    /* FM is entry 9, a built-in at its fixed place: 9.5/26, whatever the slots */
    if (!x || x[1][1] !== 6 || x[1][2] !== 5 || Math.abs(potVal(x) - q16(9.5 / 26)) > 1)
      fails.push(`FM on the rail sent ${JSON.stringify(x)}, not World P6 at 9.5/26`);
    if (sent.some(y => y[0] === KYK.CMD.action && y[1][0] === KYK.ACT.selectWorld)) fails.push('and sent the select action as well');
    P.setTel(tel({ world: 9 })); q.splice(0).forEach(f => f()); await settle(); q.splice(0).forEach(f => f()); await settle();
    if (P.getSwitching() !== -1) fails.push('the rail is still switching once the module says FM');
  }
  P.setWorlds(W);                 /* the stub link's world list read back empty */
  {
    const row = P.getPotCtl()[6][5];
    sent.length = 0;
    if (row && row.el.onchange) { row.el.value = '13'; row.el.onchange(); await settle(); }
    const x = potSent();
    if (!x || Math.abs(potVal(x) - q16(13.5 / 26)) > 1) fails.push(`Lock from the panel's menu sent ${JSON.stringify(x)}`);
  }
  /* without the pot: the select action, as before */
  P.setPanel(parsed); P.renderPanelRows();
  sent.length = 0;
  P.setTel(tel({ world: 9 })); P.setWorlds({ ...W, current: 9 }); P.renderWorldBar();
  const lock = els.worldChips.children.find(b => b.textContent === 'Lock');
  lock.onclick(); await settle();
  if (!sent.some(y => y[0] === KYK.CMD.action && y[1][0] === KYK.ACT.selectWorld && y[1][1] === 13)) fails.push('with no World P6 the rail did not send the select action');
  if (potSent()) fails.push('with no World P6 the rail wrote a pot');
  P.setTel(tel({ world: 13 })); q.splice(0).forEach(f => f()); await settle(); q.splice(0).forEach(f => f()); await settle();

  /* Bongs: the instrument rail is the card, through Space P4 */
  const bongs = { pages: 4, pots: 6, names: ['Play', 'Exciter', 'Resonator', 'Space'], colors: ['', '', '', ''],
                  grid: Array.from({ length: 4 }, (_, p) => Array.from({ length: 6 }, (_, k) =>
                    p === 3 && k === 3 ? { name: 'Instrument', id: 'res.inst', disp: { kind: 'norm' } } : p === 3 && k > 3 ? null : { name: `p${p}k${k}`, id: `x.${p}.${k}`, disp: { kind: 'norm' } })) };
  P.setPanel(bongs); P.setFwMode('modal'); P.renderPanelRows(); P.setView('model');
  P.setCardList({ count: 3, names: ['apple.kykm', 'notes.kykw', 'Zed.kykm'] });
  P.setSlots({ count: 32, live: 4, target: 0xFF, names: Array.from({ length: 32 }, (_, i) => i === 4 ? 'Zed' : null) });
  P.setTel(tel({ ...RES })); P.renderModelChips();
  const chips = els.instChips.children.filter(c => c.tagName === 'button');
  if (chips.map(c => c.textContent).join(',') !== 'apple,Zed') fails.push(`the instrument rail reads ${chips.map(c => c.textContent)}`);
  else {
    if (!/\bon\b/.test(chips[1].className) || /\bon\b/.test(chips[0].className)) fails.push('the rail does not light Zed, which is playing');
    sent.length = 0;
    chips[0].onclick(); await settle();
    const x = potSent();
    if (!x || x[1][1] !== 3 || x[1][2] !== 3 || Math.abs(potVal(x) - q16(0.25)) > 1) fails.push(`apple on the rail sent ${JSON.stringify(x)}, not Space P4 at 1/4`);
    if (sent.some(y => y[0] === KYK.CMD.action && (y[1][0] === KYK.ACT.slotLive || y[1][0] === KYK.ACT.cardToSlot))) fails.push('and sent a slot action as well');
  }
  P.setCardList({ count: 0, names: [] }); P.renderPanelRows();
  P.setTel(tel({ ...RES, page: 3 })); P.drawPanelLive(true);
  const ins = P.getPotCtl()[3][3];
  if (!ins || !/no instruments/.test(ins.sub.textContent)) fails.push(`with nothing on the card Space P4 says "${ins && ins.sub.textContent}"`);

  globalThis.setTimeout = stub;
  q.length = 0;
  P.setView('play'); P.setFwMode('both'); P.setPanel(null); P.renderPanelRows(); P.setLink(null); P.setSlots(null); P.setTel(null);
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} the world and instrument rails write the selector pot through 0x70${fails.length ? ' — ' + fails.join('; ') : ''}`);
}

/* The axes by name. A built-in's names come in the world list; where they
   came, every label built from the axes uses them — plane buttons, mute
   chips, the space's axis labels, the gauges, Play P3–P6, the Angle pots and
   the plane menus — and where they did not (firmware from before them, a
   world that names none), every one of those keeps its number exactly as
   before. A world of yours names only the effect axes the page sent it with.
   Each half fails without its part: with the parse or the lookup removed the
   names never appear, and with a lookup that ignores absence the numbers go. */
{
  const fails = [];
  const FM_AX = ['index', 'ratio', 'carrier', 'carrier2'];
  const list = (axes) => ({ current: 9, count: 10, list: Array.from({ length: 10 }, (_, i) =>
    ({ index: i, kind: i === 9 ? 4 : 1, analytic: i === 9, name: i === 9 ? 'FM' : 'w' + i, note: '', ...(i === 9 && axes ? { axes } : {}) })) });
  const kids = el => el.children.map(c => c.children.length ? c.children.map(x => x.textContent).join('|') : c.textContent);
  /* a panel with the pots that carry axes and planes, the menus as snaps */
  const PL = ['0,1', '0,2', '0,3', '1,2', '1,3', '2,3'];
  const panelFor = () => ({ pages: 3, pots: 6, names: ['Play', 'Rotate', 'Stereo'], colors: ['#67e8f9', '#fca5a5', '#a5b4fc'], grid: [
    [{ name: 'Coarse', id: 'pitch.coarse', disp: { kind: 'norm' } }, { name: 'Fine', id: 'pitch.fine', disp: { kind: 'norm' } },
     ...[0, 1, 2, 3].map(a => ({ name: 'Position ' + a, id: 'pos.' + a, disp: { kind: 'norm' } }))],
    PL.map(p => ({ name: 'Angle ' + p, id: 'rot.' + p.replace(',', ''), disp: { kind: 'norm' } })),
    [{ name: 'Spread', id: 'st.spread', disp: { kind: 'norm' } }, { name: 'Stereo plane', id: 'st.plane', disp: { kind: 'snap', labels: PL } },
     { name: 'Position 4', id: 'pos.4', disp: { kind: 'norm' } }, { name: 'Position 5', id: 'pos.5', disp: { kind: 'norm' } }, null, null]] });
  const look = (axes, t) => {
    P.setWorlds(list(axes));
    P.setTel(t);
    P.setPanel(panelFor()); P.setFwMode('wavetable'); P.renderPanelRows();
    P.setView('play'); P.setAxes([2, 3]);
    textLog.length = 0;
    P.drawStatus();           /* notices the names changed and rebuilds the chips and buttons */
    P.drawSpace();
    /* a page's labels are kept up while it is the one showing */
    for (const page of [0, 1, 2]) { P.setTel({ ...t, page }); P.drawPanelLive(true); }
    const c = P.getPotCtl();
    return {
      buttons: kids(els.axes), chips: kids(els.muteChips), text: textLog.slice(),
      play: c[0].slice(2).map(x => x.lab.textContent), sub: c[0][2].sub.textContent,
      angle: c[1][0].lab.textContent, menu: c[2][1].el.children.map(o => o.textContent), menuVal: c[2][1].val.textContent,
      world: [c[2][2].lab.textContent, c[2][3].lab.textContent],
    };
  };
  /* named */
  const on = look(FM_AX, tel({ n: 4, world: 9, page: 2, knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  if (on.buttons[0] !== 'index · ratio|0,1') fails.push(`plane button 0,1 reads "${on.buttons[0]}"`);
  if (on.buttons[5] !== 'carrier · carrier2|2,3') fails.push(`plane button 2,3 reads "${on.buttons[5]}"`);
  if (on.chips[0] !== 'index · ratio|0,1') fails.push(`mute chip 01 reads "${on.chips[0]}"`);
  if (!on.text.includes('carrier') || !on.text.includes('carrier2')) fails.push(`the space's axis labels on 2,3 are not carrier and carrier2 (${on.text.slice(0, 8).join(', ')})`);
  if (!on.text.includes('index') || !on.text.includes('ratio')) fails.push('the gauges are not labelled by name');
  if (on.text.includes('23') || on.text.includes('axis 2')) fails.push('a number label drawn where a name was sent');
  if (on.play.join(',') !== 'Index,Ratio,Carrier,Carrier2') fails.push(`Play P3–P6 read ${on.play.join(',')}`);
  if (on.sub !== 'Position 0') fails.push(`the descriptor's own words are not under P3 ("${on.sub}")`);
  if (on.angle !== 'Angle index · ratio') fails.push(`Rotate P1 reads "${on.angle}"`);
  if (on.menu[0] !== 'index · ratio (0,1)' || on.menu[5] !== 'carrier · carrier2 (2,3)') fails.push(`the stereo plane menu reads ${on.menu.join(' / ')}`);
  /* the knob is at the middle, which is the fourth of six planes */
  if (on.menuVal !== 'ratio · carrier (1,2)') fails.push(`the stereo plane's value reads "${on.menuVal}"`);
  /* not sent: firmware from before the names */
  const off = look(null, tel({ n: 4, world: 9, page: 2, knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  if (off.buttons[0] !== '0×1') fails.push(`with no names, plane button 0,1 reads "${off.buttons[0]}"`);
  if (off.chips[0] !== '01') fails.push(`with no names, mute chip 01 reads "${off.chips[0]}"`);
  if (!off.text.includes('axis 2') || !off.text.includes('23')) fails.push('with no names, the axis labels and gauges lost their numbers');
  if (off.play.join(',') !== 'Position 0,Position 1,Position 2,Position 3') fails.push(`with no names, Play P3–P6 read ${off.play.join(',')}`);
  if (off.angle !== 'Angle 0,1' || off.menu[0] !== '0,1' || off.menuVal !== '1,2') fails.push(`with no names, the plane pots read "${off.angle}", "${off.menu[0]}", "${off.menuVal}"`);
  /* a gap among names is the number, not a shift */
  const gap = look(['wave', null, 'fold', 'warp'], tel({ n: 4, world: 9, page: 2, knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  if (gap.buttons[0] !== 'wave · 1|0,1' || gap.play[1] !== 'Position 1') fails.push(`an unnamed axis among named ones reads "${gap.buttons[0]}", "${gap.play[1]}"`);
  /* a world of yours: only the effects the page sent it with, and only while it holds */
  P.setHeld({ name: 'mine', nodes: [], fx: [{ axis: 4, kind: 1 }] });
  const mine = look(FM_AX, tel({ n: 6, world: 0xFF, page: 2, knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  if (mine.world.join(',') !== 'Fold,Position 5') fails.push(`World P1/P2 on a world sent with a fold on axis 4 read ${mine.world.join(',')}`);
  if (mine.play.join(',') !== 'Position 0,Position 1,Position 2,Position 3') fails.push(`a world of yours borrowed a built-in's names: ${mine.play.join(',')}`);
  P.setHeld(null);
  /* and a draft with a fold on axis 4 is the build tab's to name, not the
     play view's: the module is not holding it */
  els.fxA.value = '1';
  const lost = look(FM_AX, tel({ n: 6, world: 0xFF, page: 2, knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pots: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }));
  if (lost.world.join(',') !== 'Position 4,Position 5') fails.push(`with nothing held, World P1/P2 read ${lost.world.join(',')}`);
  const draftBtns = kids(els.planeAxes);
  if (lost.buttons[14] !== '4×5' || draftBtns[14] !== 'fold · 5|4,5') fails.push(`plane 4,5 reads "${lost.buttons[14]}" on play and "${draftBtns[14]}" on build`);
  els.fxA.value = '0';
  P.setTel(null); P.setPanel(null); P.setWorlds(null); P.setAxes([0, 1]); P.buildAxisButtons(); P.renderMuteChips();
  if (fails.length) bad++;
  console.log(`  ${fails.length ? 'FAIL' : 'ok  '} axes by name where the module sends them, by number where it does not${fails.length ? ' — ' + fails.join('; ') : ''}`);
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

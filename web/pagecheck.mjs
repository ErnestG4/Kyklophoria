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

const IDS = 'axes btnBridge btnClose btnDrawer btnSerial drawer modinfo msg sBlock sCpu sCpuWrap sF0 sKcut sLink sMod sSpace sSpread shadeChips sound space worldCap worldChips worldNote'.split(' ');
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
    disabled: false, value: '', checked: false, onclick: null, oninput: null,
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
globalThis.document = {
  getElementById: id => els[id] || mkEl('div'),
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
const hook = `\nglobalThis.__probe = { frame, drawSpace, drawSound, drawStatus, setTel: v => { tel = v; }, setBasis: b => { basis = b; }, onTelemetry };\n`;
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
    couple: 0, lock: 0, sharp: 0, bodies: 1, kepXY: [], bytes: 512,
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
];
let bad = 0;
for (const [name, t] of CASES) {
  P.setTel(t);
  try {
    if (t) P.onTelemetry(t);
    /* the individual draws, not frame(), which now swallows throws on purpose */
    P.drawSpace(); P.drawSound(); P.drawStatus();
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

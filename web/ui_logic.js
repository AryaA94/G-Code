
// ---------------------------------------------------------------------------
// ui_logic.js: everything the page does. The C++ engine is reached through
// three functions that take strings and return JSON strings:
//   gs_analyze(gcode, machineJson), gs_render_svg(...), gs_rules()
// Nothing here re-implements the simulation; it only draws what comes back.
// ---------------------------------------------------------------------------

const $ = id => document.getElementById(id);

// The page as it arrived, before any of the code below changes it: the
// fallback for "Download for offline use" when the page can't refetch itself.
// The <script> holding this code (and the engine) is already in the document.
const PAGE_SOURCE = '<!doctype html>\n' + document.documentElement.outerHTML;

const PRESETS = [
  { key: 'bracket',     tag: 'PROFILE + BORE', title: 'Bracket: outline, helical bore, two holes' },
  { key: 'pocket',      tag: 'POCKET',         title: 'Rectangular pocket, two depths' },
  { key: 'gear',        tag: 'ENGRAVE',        title: 'Gear outline on the G55 offset' },
  { key: 'contour',     tag: 'LOOK-AHEAD',     title: 'Wavy groove, 200 tiny segments' },
  { key: 'drill_plate', tag: 'PECK DRILL',     title: '12 holes with G83 pecking' },
  { key: 'lint_demo',   tag: 'LINT',           title: 'A program full of mistakes' },
  { key: 'plate_drill_tap', tag: 'PLATE',       title: 'Face, drill and tap 4140 plate', machine: 'haas_vf5_50' },
  { key: 'fsae_upright', tag: 'FSAE',          title: 'Upright plate in 7075: helical bearing bore', machine: 'haas_mini_mill', stock: 'aluminum_7075' },
];

// Must match src/materials.cpp (the browser test checks every key is accepted).
const MATERIALS = [
  ['aluminum_6061', 'Aluminum 6061'],
  ['aluminum_7075', 'Aluminum 7075'],
  ['mild_steel', 'Mild steel (A36, 1018)'],
  ['alloy_steel', 'Alloy steel, Q&T (4140, HS 100)'],
  ['ar400', 'AR400 plate (~400 BHN)'],
  ['ar500', 'AR500 plate (~500 BHN)'],
  ['hardened_600', 'Hardened plate (~600 BHN)'],
  ['stainless_304', 'Stainless 304'],
  ['titanium_ti6al4v', 'Titanium Ti-6Al-4V (TC4)'],
];

// Private feedback goes here. Leave empty to hide the email option.
const FEEDBACK_EMAIL = '';
const OWN = { key: 'own', tag: 'YOUR OWN', title: 'Write, paste or open your own program' };
const OWN_STARTER = `(Your program. Paste G-code here, or use Open file below.)
(Units mm. Tools T1-T4 are set up in the machine config.)
G21 G17 G90 G94
G54
T1 M6
S8000 M3
G0 X0 Y0 Z5
G1 Z-1 F300
G1 X40 F800
G1 Y30
G1 X0
G1 Y0
G0 Z5
M5
M30
`;
const DRAFT_KEY = 'gcode-sim.own-program';
const REPO = 'https://github.com/AryaA94/G-Code';

// Visitor counts with GoatCounter (no cookies, no personal data). Only on the
// public site: set GOATCOUNTER to your own counter's URL after signing up at
// goatcounter.com, or leave it empty to turn counting off.
const GOATCOUNTER = 'https://aryaa94.goatcounter.com/count';
const COUNTING = !!GOATCOUNTER && /\.github\.io$/.test(location.hostname);

// Colour scale for dark backgrounds (blue -> violet -> pink -> orange -> yellow).
const RAMP = [[91, 124, 250], [154, 92, 240], [224, 86, 155], [255, 138, 61], [255, 209, 102]];
const TYPE_COLORS = { linear: '#7cb7ff', arc: '#c49cff', rapid: '#8a93a0' };
const TOOL_COLORS = ['#ff9d2e', '#7cb7ff', '#c49cff', '#6bd49a', '#ff6b9d', '#e9c46a'];

let engine = null;     // { analyze, renderSvg, rules } once the WASM module is ready
let result = null;     // last successful analysis
let scene = null;      // result, prepared for drawing
let highlightLine = 0; // set by clicking a diagnostic
const cam = { yaw: -0.55, pitch: 1.0, zoom: 1, panX: 0, panY: 0, fit: 1, view: 'iso' };
const play = { t: 0, playing: false, last: 0, mode: 'fit:25' };
let presetKey = 'bracket';
let saver = null;      // the viewer's download helper when the page runs inside Claude

// ---- engine -----------------------------------------------------------------

GcodeSimModule().then(mod => {
  engine = {
    analyze: mod.cwrap('gs_analyze', 'string', ['string', 'string']),
    renderSvg: mod.cwrap('gs_render_svg', 'string', ['string', 'string', 'number', 'number']),
    rules: mod.cwrap('gs_rules', 'string', []),
  };
  renderRules(JSON.parse(engine.rules()));
  loadPreset('bracket');
});

// Machines to pick from: real ones from examples/machines, the generic
// benchtop example, and "no config".
function machineList() {
  const list = (typeof GS_MACHINES !== 'undefined' ? GS_MACHINES : []).map(m => ({ ...m }));
  list.push({ key: 'example', name: 'Benchtop mill (generic example)', json: GS_MACHINE });
  return list;
}

// The machine JSON with the chosen stock material applied. Broken JSON is
// passed through untouched so the engine reports the error.
function machineConfig() {
  const stock = $('stockSelect').value;
  if (!$('machineSelect').value) return stock ? JSON.stringify({ stock_material: stock }) : '';
  const text = $('machineJson').value;
  let cfg;
  try { cfg = JSON.parse(text); } catch (e) { return text; }
  if (!cfg || typeof cfg !== 'object' || Array.isArray(cfg)) return text;
  if (stock) cfg.stock_material = stock; else delete cfg.stock_material;
  return JSON.stringify(cfg);
}

function selectMachine(key) {
  const m = machineList().find(x => x.key === key);
  $('machineSelect').value = m ? key : '';
  $('machineJson').value = m ? m.json : '';
  $('machineJson').disabled = !m;
  let note = 'Generic defaults: no travel limits and no tool table, so the tool and travel checks are off.';
  if (m) { try { note = JSON.parse(m.json).comment || ''; } catch (e) { note = ''; } }
  $('machineNote').textContent = note;
  let stock = '';
  if (m) { try { stock = JSON.parse(m.json).stock_material || ''; } catch (e) { stock = ''; } }
  $('stockSelect').value = stock;
}

function run({ refit = false } = {}) {
  if (!engine) return;
  let r;
  try {
    r = JSON.parse(engine.analyze($('code').value, machineConfig()));
  } catch (e) {
    r = { ok: false, error: String(e) };
  }
  if (!r.ok) {
    showError('Machine config: ' + r.error);
    return;
  }
  showError('');
  const wasAtEnd = !scene || play.t >= scene.total - 1e-9;
  result = r;
  scene = prepareScene(r);
  if (refit) fitView();
  if (wasAtEnd || play.t > scene.total) play.t = scene.total;
  renderStats(r);
  renderQuote();
  renderDiagnostics(r);
  renderGutter();
  renderSpeedChart(r);
  renderLegend();
  updateTime();
}

function showError(msg) {
  const b = $('errorBanner');
  b.textContent = msg;
  b.classList.toggle('show', !!msg);
}

// ---- drilling program generator -------------------------------------------

// Starting points for carbide drills and HSS taps, per stock material:
// drill surface speed (m/min), feed per rev as a fraction of the drill
// diameter, peck depth in drill diameters, tap surface speed (m/min, null =
// too hard to tap, thread mill instead). Handbook-style values; the drill
// speeds sit inside the LN010 ranges in src/materials.cpp.
const GEN_DATA = {
  aluminum_6061:    { vc: 150, fr: 0.025, peck: 1.5, tapVc: 20 },
  aluminum_7075:    { vc: 130, fr: 0.022, peck: 1.5, tapVc: 18 },
  mild_steel:       { vc: 100, fr: 0.018, peck: 1.0, tapVc: 12 },
  alloy_steel:      { vc: 80,  fr: 0.015, peck: 1.0, tapVc: 8 },
  ar400:            { vc: 50,  fr: 0.010, peck: 0.5, tapVc: null },
  ar500:            { vc: 40,  fr: 0.008, peck: 0.5, tapVc: null },
  hardened_600:     { vc: 28,  fr: 0.006, peck: 0.4, tapVc: null },
  stainless_304:    { vc: 70,  fr: 0.012, peck: 0.75, tapVc: 6 },
  titanium_ti6al4v: { vc: 45,  fr: 0.010, peck: 0.75, tapVc: 5 },
};
const GEN_MAX_RPM = 8000;  // a safe ceiling for 40- and 50-taper spindles

// Metric coarse taps with their tap drills, then plain drilled holes.
const GEN_HOLES = [
  ...[['M6', 6, 1.0, 5.0], ['M8', 8, 1.25, 6.8], ['M10', 10, 1.5, 8.5], ['M12', 12, 1.75, 10.2],
      ['M16', 16, 2.0, 14.0], ['M20', 20, 2.5, 17.5], ['M24', 24, 3.0, 21.0]]
    .map(([name, d, pitch, drill]) => ({ key: name, label: `Tapped ${name} x ${pitch} (drill ${drill})`, drill, tap: { d, pitch } })),
  ...[5, 6.6, 8.5, 9, 10.2, 11, 13.5, 14, 17.5, 18, 22, 26]
    .map(d => ({ key: 'D' + d, label: `Drilled Ø${d} mm`, drill: d, tap: null })),
];

// "x, y" per line; tabs, commas, semicolons or spaces between. Lines without
// two numbers (headers, blanks) are skipped and counted.
function parseHoles(text) {
  const holes = [];
  let skipped = 0;
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    const nums = line.match(/-?\d+(?:[.,]\d+)?/g);
    if (!nums || nums.length < 2) { skipped++; continue; }
    const [x, y] = nums.slice(0, 2).map(n => parseFloat(n.replace(',', '.')));
    if (isFinite(x) && isFinite(y)) holes.push([x, y]); else skipped++;
  }
  return { holes, skipped };
}

// Greedy nearest-neighbour from the origin: not optimal, but it removes the
// long back-and-forth rapids of a list typed in any order.
function orderHoles(holes) {
  const left = holes.slice();
  const out = [];
  let at = [0, 0];
  while (left.length) {
    let best = 0, bestD = Infinity;
    for (let i = 0; i < left.length; i++) {
      const d = Math.hypot(left[i][0] - at[0], left[i][1] - at[1]);
      if (d < bestD) { bestD = d; best = i; }
    }
    at = left.splice(best, 1)[0];
    out.push(at);
  }
  return out;
}

const fmt = (v, d = 3) => String(+v.toFixed(d));

// Builds the program text plus the tools it needs. Returns { error } when
// the inputs can't make a sensible program.
function buildDrillProgram({ holes, hole, thick, spot, material }) {
  const data = GEN_DATA[material];
  if (!data) return { error: 'Pick a stock material first (Machine panel): speeds and feeds depend on it.' };
  if (!holes.length) return { error: 'Add at least one hole position.' };
  if (!(thick > 0)) return { error: 'Plate thickness must be more than 0.' };
  if (hole.tap && data.tapVc === null) {
    return { error: 'This material is too hard to tap with a standard tap (above roughly 40 HRC). Use a drilled hole, or thread mill it.' };
  }
  const rpm = vc => Math.min(GEN_MAX_RPM, Math.round(vc * 1000 / (Math.PI * hole.drill) / 10) * 10);
  const D = hole.drill;
  const depth = thick + 0.3 * D + 1;  // drill point plus 1 mm breakthrough
  const ratio = depth / D;
  const cycle = ratio > 3 ? 'G83' : ratio > 1 ? 'G73' : 'G81';
  const sDrill = rpm(data.vc), fDrill = Math.max(10, Math.round(sDrill * data.fr * D));
  const spotD = 12;
  const sSpot = Math.min(GEN_MAX_RPM, Math.round(data.vc * 1000 / (Math.PI * spotD) / 10) * 10);
  const fSpot = Math.max(10, Math.round(sSpot * data.fr * spotD * 0.6));
  const spotDepth = Math.min(spotD / 2, (Math.min(D, spotD - 0.5) + 0.5) / 2);  // 90° spot: chamfer just over the hole
  const q = Math.max(0.5, data.peck * D);
  const order = holes;

  const L = [];
  const op = (title, t, s, body) => {
    L.push('', `(${title})`, `T${t} M6`, `S${s} M3`, 'G54', 'M8',
      `G0 X${fmt(order[0][0])} Y${fmt(order[0][1])}`, `G43 Z15. H${t}`, 'G0 Z5.');
    L.push(body + ` X${fmt(order[0][0])} Y${fmt(order[0][1])}`);
    for (const [x, y] of order.slice(1)) L.push(`X${fmt(x)} Y${fmt(y)}`);
    L.push('G80', 'M9', 'M5', 'G53 G0 Z0.');
  };
  const matLabel = (MATERIALS.find(([k]) => k === material) || [, material])[1];
  // G-code comments end at the first ')', so labels must not contain brackets
  const comment = t => '(' + t.replace(/[()]/g, '') + ')';
  L.push(comment(`${order.length} x ${hole.label} through ${fmt(thick, 2)} mm ${matLabel}`),
    '(Generated by gcode-sim. Zero: G54 at the plate lower-left corner, top face.)',
    '(Check speeds, feeds and clamping before running.)',
    'G21 G90 G94 G17', 'G53 G0 Z0.');
  if (spot) op(`SPOT DRILL ${spotD} MM 90 DEG`, 21, sSpot, `G98 G81 Z-${fmt(spotDepth, 2)} R2. F${fSpot}.`);
  op(`DRILL ${fmt(D, 2)} MM ${cycle === 'G81' ? '' : 'PECK '}THROUGH`, 22, sDrill,
    `G98 ${cycle} Z-${fmt(depth, 2)} R2.${cycle === 'G81' ? '' : ` Q${fmt(q, 2)}`} F${fDrill}.`);
  let tapNote = '';
  if (hole.tap) {
    const sTap = Math.min(GEN_MAX_RPM, Math.round(data.tapVc * 1000 / (Math.PI * hole.tap.d) / 10) * 10);
    const fTap = +(sTap * hole.tap.pitch).toFixed(2);
    op(`TAP ${hole.key} X ${hole.tap.pitch}`, 23, sTap,
      `G98 G84 Z-${fmt(thick + 2 * hole.tap.pitch, 2)} R3. F${fTap}`);
    tapNote = ` · tap S${sTap} F${fTap}`;
  }
  L.push('', 'M30');
  const tools = {
    '21': { diameter_mm: spotD, length_mm: 90, flutes: 2, material: 'carbide', comment: `${spotD} mm 90 degree spot drill (generator)` },
    '22': { diameter_mm: D, length_mm: Math.max(80, depth + 40), flutes: 2, material: 'carbide', comment: `${D} mm carbide drill (generator)` },
  };
  if (hole.tap) tools['23'] = { diameter_mm: hole.tap.d, length_mm: 110, flutes: 3, material: 'hss', comment: `${hole.key} tap (generator)` };
  const summary = `${cycle} at S${sDrill} F${fDrill}${tapNote}`;
  return { text: L.join('\n') + '\n', tools, summary };
}

function generateProgram() {
  const { holes: raw, skipped } = parseHoles($('genHoles').value);
  const hole = GEN_HOLES.find(h => h.key === $('genHole').value);
  const holes = $('genOrder').value === 'near' ? orderHoles(raw) : raw;
  // a material is required; take the machine's if one is chosen
  if (!$('machineSelect').value) selectMachine(machineList()[0].key);
  const out = buildDrillProgram({
    holes, hole, thick: parseFloat($('genThick').value), spot: $('genSpot').value === '1', material: $('stockSelect').value,
  });
  const warn = $('genWarn');
  if (out.error) { warn.textContent = out.error; warn.hidden = false; return; }
  warn.hidden = !skipped;
  warn.textContent = skipped ? `${skipped} line${skipped === 1 ? '' : 's'} without two numbers were skipped.` : '';
  // give the machine the generated tools so the checks know their sizes
  try {
    const cfg = JSON.parse($('machineJson').value);
    cfg.tools = Object.assign({}, cfg.tools || {}, out.tools);
    $('machineJson').value = JSON.stringify(cfg, null, 2);
  } catch (e) { /* broken JSON: leave it for the engine to report */ }
  try { localStorage.setItem(DRAFT_KEY, out.text); } catch (e) { /* storage blocked */ }
  loadPreset('own');
  $('genCount').textContent = `${holes.length} hole${holes.length === 1 ? '' : 's'} · ${out.summary}`;
  track('generate-' + (hole.tap ? 'tap' : 'drill'));
  document.querySelector('.workspace').scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function addPattern(kind) {
  const n = id => parseFloat($(id).value);
  const lines = [];
  if (kind === 'grid') {
    const nx = Math.min(200, Math.max(1, Math.round(n('gnx')))), ny = Math.min(200, Math.max(1, Math.round(n('gny'))));
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) lines.push(`${fmt(n('gx0') + i * n('gdx'))}, ${fmt(n('gy0') + j * n('gdy'))}`);
  } else {
    const count = Math.min(360, Math.max(1, Math.round(n('cn')))), r = n('cdia') / 2;
    for (let i = 0; i < count; i++) {
      const a = (n('ca0') + i * 360 / count) * Math.PI / 180;
      lines.push(`${fmt(n('ccx') + r * Math.cos(a))}, ${fmt(n('ccy') + r * Math.sin(a))}`);
    }
  }
  if (lines.some(l => l.includes('NaN'))) { toast('Fill in every pattern field'); return; }
  const ta = $('genHoles');
  ta.value = (ta.value.trim() ? ta.value.trim() + '\n' : '') + lines.join('\n');
  updateGenCount();
}

function updateGenCount() {
  const { holes } = parseHoles($('genHoles').value);
  $('genCount').textContent = holes.length ? `${holes.length} hole${holes.length === 1 ? '' : 's'}` : '';
}

// ---- job quote --------------------------------------------------------------

const QUOTE_KEY = 'gcode-sim.quote';
const money = v => '$' + (v >= 1000 ? Math.round(v).toLocaleString('en-US') : v.toFixed(2));

function renderQuote() {
  const box = $('quote');
  if (!result) { box.innerHTML = ''; return; }
  const num = (id, lo) => Math.max(lo, parseFloat($(id).value) || 0);
  const rate = num('qRate', 0), setup = num('qSetup', 0), load = num('qLoad', 0), qty = Math.max(1, Math.round(num('qQty', 1)));
  const cycleMin = result.stats.time_s.total / 60;
  const perPartMin = cycleMin + load;
  const batchMin = setup + perPartMin * qty;
  const batchCost = batchMin / 60 * rate;
  const cards = [
    ['Cost per part', money(batchCost / qty), `at ${qty} parts, setup shared`, true],
    ['Batch cost', money(batchCost), `${qty} parts`],
    ['Batch machine time', fmtDuration(batchMin * 60), `${fmtDuration(setup * 60)} setup + ${qty} × ${fmtDuration(perPartMin * 60)}`],
    ['One more part', money(perPartMin / 60 * rate), 'once set up'],
  ];
  box.innerHTML = cards.map(([label, value, sub, hl]) =>
    `<div class="stat${hl ? ' highlight' : ''}"><div class="label">${esc(label)}</div><div class="value">${esc(value)}</div><div class="sub">${esc(sub)}</div></div>`
  ).join('');
  try { localStorage.setItem(QUOTE_KEY, JSON.stringify({ rate, setup, load, qty })); } catch (e) { /* storage blocked */ }
}

// ---- feedback and usage counts ---------------------------------------------

// A pre-filled GitHub issue: what happened, plus the program and machine so
// the report can be reproduced. URLs have a length limit, so long programs
// are cut and the reporter is asked to attach the file instead.
function feedbackUrl(withProgram = true) {
  const code = withProgram ? $('code').value : '(not included)';
  const limit = 5000;
  const program = code.length > limit
    ? code.slice(0, limit) + '\n(... cut here: please attach the full file to this issue)'
    : code;
  const sel = $('machineSelect');
  const machine = sel.options[sel.selectedIndex] ? sel.options[sel.selectedIndex].text : 'none';
  const lint = result ? result.lint : null;
  const body =
`**What happened, or what should be different?**


**Where did the program come from?** (Fusion 360, Mastercam, hand written, ...)


---
Machine: ${machine}
Diagnostics shown: ${lint ? `${lint.errors} errors, ${lint.warnings} warnings` : 'none'}

<details><summary>Program</summary>

\`\`\`gcode
${program}
\`\`\`
</details>`;
  return `${REPO}/issues/new?` + new URLSearchParams({ title: 'Feedback: ', body, labels: 'feedback' });
}

// Private feedback by email. mailto links are short, so the program is cut
// sooner and the sender is asked to attach the file.
function feedbackMailto() {
  const code = $('code').value;
  const limit = 1500;
  const sel = $('machineSelect');
  const machine = sel.options[sel.selectedIndex] ? sel.options[sel.selectedIndex].text : 'none';
  const body = `What happened, or what should be different?\n\n\nMachine: ${machine}\n` +
    `Material: ${$('stockSelect').value || 'not set'}\n\nProgram` +
    (code.length > limit ? ' (first part; please attach the full file):\n' : ':\n') + code.slice(0, limit);
  return `mailto:${FEEDBACK_EMAIL}?` + new URLSearchParams({ subject: 'gcode-sim feedback', body }).toString().replace(/\+/g, '%20');
}

// The choices are plain links (not window.open), which browsers and
// embedded viewers block far less often. Their addresses are filled in when
// the dialog opens, so they carry the current program.
function openFeedback() {
  const dlg = $('feedbackDialog');
  $('fbWith').href = feedbackUrl(true);
  $('fbPlain').href = feedbackUrl(false);
  $('fbEmail').hidden = !FEEDBACK_EMAIL;
  if (FEEDBACK_EMAIL) $('fbEmail').href = feedbackMailto();
  $('fbFallback').hidden = true;
  if (!dlg.showModal) { window.location.href = feedbackUrl(false); return; }
  dlg.showModal();
}

function onFeedbackLink(e) {
  const a = e.currentTarget;
  track('feedback-' + a.dataset.choice);
  // If the viewer swallows the new tab, the link stays available to copy.
  $('fbLink').value = a.href;
  setTimeout(() => { $('fbFallback').hidden = false; }, 600);
}

// Save the whole tool as one HTML file that runs without internet. The page
// is already self-contained, so this just saves the page's own source.
async function downloadOffline() {
  track('offline-download');
  let html = null;
  try {
    const r = await fetch(location.href, { cache: 'force-cache' });
    if (r.ok) html = await r.text();
  } catch (e) { /* not fetchable here, e.g. inside an embedded viewer */ }
  if (!html || !html.includes('GcodeSimModule')) html = PAGE_SOURCE;
  download('gcode-sim.html', html, 'text/html');
}

function track(name) {
  if (!COUNTING) return;
  try { if (window.goatcounter && window.goatcounter.count) window.goatcounter.count({ path: name, event: true }); } catch (e) { /* ignore */ }
}

function startCounting() {
  if (!COUNTING) return;
  const s = document.createElement('script');
  s.async = true;
  s.src = 'https://gc.zgo.at/count.js';
  s.dataset.goatcounter = GOATCOUNTER;
  document.head.appendChild(s);
}

// ---- presets and editor -----------------------------------------------------

function loadPreset(key) {
  presetKey = key;
  if (key === 'own') {
    let draft = null;
    try { draft = localStorage.getItem(DRAFT_KEY); } catch (e) { /* storage blocked */ }
    $('code').value = draft || OWN_STARTER;
  } else {
    $('code').value = GS_EXAMPLES[key] || '';
  }
  $('code').scrollTop = 0;
  document.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c.dataset.key === key));
  highlightLine = 0;
  play.playing = false;
  play.t = Infinity;  // show the whole path first
  setView('iso', false);
  run({ refit: true });
}

function buildPresets() {
  const box = $('presets');
  for (const p of [...PRESETS, OWN]) {
    const b = document.createElement('button');
    b.className = p === OWN ? 'chip own' : 'chip';
    b.dataset.key = p.key;
    b.dataset.tag = p.tag;
    b.textContent = p.title;
    b.addEventListener('click', () => {
      if (p.machine && $('machineSelect').value !== p.machine) selectMachine(p.machine);
      if (p.stock) $('stockSelect').value = p.stock;
      loadPreset(p.key);
      track('example-' + p.key);
    });
    box.appendChild(b);
  }
}

let runTimer = 0;
function scheduleRun() {
  clearTimeout(runTimer);
  runTimer = setTimeout(() => run(), 250);
  renderGutter();
}

function renderGutter() {
  const text = $('code').value;
  const lines = Math.min(text.split('\n').length, 20000);
  $('lineCount').textContent = lines + (lines === 1 ? ' line' : ' lines');
  const worst = new Map();
  if (result) {
    for (const d of result.lint.diagnostics) {
      if (d.severity === 'error' || !worst.has(d.line)) worst.set(d.line, d.severity);
    }
  }
  const now = currentMove() ? currentMove().line : 0;
  let html = '';
  for (let i = 1; i <= lines; i++) {
    const sev = worst.get(i);
    let cls = sev === 'error' ? 'err' : sev === 'warning' ? 'warn' : '';
    if (i === now && play.t < (scene ? scene.total : 0)) cls += ' now';
    html += cls ? `<div class="${cls.trim()}">${i}</div>` : `<div>${i}</div>`;
  }
  $('gutter').innerHTML = html;
  $('gutter').scrollTop = $('code').scrollTop;
}

function selectLine(line) {
  const ta = $('code');
  const lines = ta.value.split('\n');
  let start = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) start += lines[i].length + 1;
  const end = start + (lines[line - 1] || '').length;
  ta.focus({ preventScroll: true });
  ta.setSelectionRange(start, end);
  scrollEditorTo(line);
}

function scrollEditorTo(line) {
  const ta = $('code');
  const lh = 19;
  const top = (line - 1) * lh;
  if (top < ta.scrollTop || top > ta.scrollTop + ta.clientHeight - 2 * lh) {
    ta.scrollTop = Math.max(0, top - ta.clientHeight / 3);
  }
  $('gutter').scrollTop = ta.scrollTop;
}

// ---- scene: the analysis turned into things to draw ------------------------

function prepareScene(r) {
  const moves = [];
  const cut = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  const all = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  let fmin = Infinity, fmax = -Infinity, zmin = Infinity, zmax = -Infinity;

  for (const m of r.toolpath.moves) {
    const motion = m.type === 'rapid' || m.type === 'linear' || m.type === 'arc';
    const pts = m.points;
    // cumulative length, for placing the tool part-way along a move
    const cum = [0];
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1], b = pts[i];
      cum.push(cum[i - 1] + Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
    }
    let lowest = Infinity;
    for (const p of pts) {
      lowest = Math.min(lowest, p[2]);
      if (!motion) continue;
      for (let k = 0; k < 3; k++) {
        all.min[k] = Math.min(all.min[k], p[k]);
        all.max[k] = Math.max(all.max[k], p[k]);
        if (m.type !== 'rapid') {
          cut.min[k] = Math.min(cut.min[k], p[k]);
          cut.max[k] = Math.max(cut.max[k], p[k]);
        }
      }
    }
    if (motion && m.type !== 'rapid') {
      if (m.feed > 0) { fmin = Math.min(fmin, m.feed); fmax = Math.max(fmax, m.feed); }
      zmin = Math.min(zmin, lowest);
      zmax = Math.max(zmax, lowest);
    }
    moves.push({ ...m, motion, cum, len: cum[cum.length - 1], lowest });
  }
  const box = isFinite(cut.min[0]) ? cut : all;
  if (!isFinite(box.min[0])) { box.min = [0, 0, 0]; box.max = [1, 1, 1]; }
  const tools = [...new Set(moves.filter(m => m.tool > 0).map(m => m.tool))];
  return {
    moves, box, tools,
    feed: [fmin, fmax], depth: [zmin, zmax],
    total: r.stats.time_s.total,
    starts: moves.map(m => m.t0),
  };
}

function ramp(t) {
  t = Math.max(0, Math.min(1, isFinite(t) ? t : 1)) * (RAMP.length - 1);
  const i = Math.min(Math.floor(t), RAMP.length - 2), f = t - i;
  const c = RAMP[i].map((v, k) => Math.round(v + (RAMP[i + 1][k] - v) * f));
  return `rgb(${c[0]},${c[1]},${c[2]})`;
}

function moveColor(m) {
  if (m.type === 'rapid') return TYPE_COLORS.rapid;
  const by = $('colorBy').value;
  if (by === 'type') return TYPE_COLORS[m.type];
  if (by === 'tool') return TOOL_COLORS[Math.max(0, scene.tools.indexOf(m.tool)) % TOOL_COLORS.length];
  const [lo, hi] = by === 'feed' ? scene.feed : scene.depth;
  const v = by === 'feed' ? m.feed : m.lowest;
  return ramp(hi > lo ? (v - lo) / (hi - lo) : 1);
}

// ---- camera -----------------------------------------------------------------

const VIEWS = {
  iso: { yaw: -0.55, pitch: 1.0 },
  top: { yaw: 0, pitch: 0 },
  front: { yaw: 0, pitch: Math.PI / 2 },
};

function setView(name, redraw = true) {
  cam.view = name;
  Object.assign(cam, VIEWS[name]);
  document.querySelectorAll('#viewSeg button').forEach(b => b.classList.toggle('active', b.dataset.view === name));
  if (redraw && scene) { fitView(); draw(); }
}

function center() {
  const b = scene.box;
  return [(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2];
}

// World (x, y, z) -> screen position before zoom/pan. Rotate around Z by yaw,
// then tilt by pitch (0 = looking straight down, pi/2 = looking from the front).
function rotate(x, y, z, c) {
  x -= c[0]; y -= c[1]; z -= c[2];
  const cy = Math.cos(cam.yaw), sy = Math.sin(cam.yaw);
  const cp = Math.cos(cam.pitch), sp = Math.sin(cam.pitch);
  const x1 = x * cy - y * sy, y1 = x * sy + y * cy;
  return [x1, y1 * cp + z * sp];
}

function fitView() {
  if (!scene) return;
  const { w, h } = canvasSize();
  const b = scene.box, c = center();
  let sx0 = Infinity, sx1 = -Infinity, sy0 = Infinity, sy1 = -Infinity;
  for (const x of [b.min[0], b.max[0]]) for (const y of [b.min[1], b.max[1]]) for (const z of [b.min[2], b.max[2]]) {
    const [sx, sy] = rotate(x, y, z, c);
    sx0 = Math.min(sx0, sx); sx1 = Math.max(sx1, sx); sy0 = Math.min(sy0, sy); sy1 = Math.max(sy1, sy);
  }
  cam.fit = Math.min(w * 0.8 / Math.max(sx1 - sx0, 1e-3), h * 0.72 / Math.max(sy1 - sy0, 1e-3));
  cam.zoom = 1;
  cam.panX = -(sx0 + sx1) / 2 * cam.fit;
  cam.panY = (sy0 + sy1) / 2 * cam.fit + h * 0.03;
}

function canvasSize() {
  const v = $('viewer');
  return { w: v.clientWidth || 800, h: v.clientHeight || 470 };
}

// ---- drawing ----------------------------------------------------------------

function draw() {
  const canvas = $('canvas');
  const ctx = canvas.getContext && canvas.getContext('2d');
  if (!ctx || !scene) return;  // no canvas support (e.g. in tests)
  const { w, h } = canvasSize();
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const c = center();
  const k = cam.fit * cam.zoom;
  const P = (x, y, z) => {
    const [sx, sy] = rotate(x, y, z, c);
    return [w / 2 + sx * k + cam.panX * cam.zoom, h / 2 - sy * k + cam.panY * cam.zoom];
  };

  drawBed(ctx, P);

  const showRapids = $('showRapids').checked;
  const t = play.t;
  const cur = currentMove();
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';

  for (const m of scene.moves) {
    if (!m.motion || (m.type === 'rapid' && !showRapids) || m.points.length < 2) continue;
    const end = m.t0 + m.dt;
    const color = moveColor(m);
    const rapid = m.type === 'rapid';
    ctx.setLineDash(rapid ? [4, 4] : []);
    ctx.lineWidth = rapid ? 1 : 1.6;
    if (t >= end || m.dt <= 0 && t >= m.t0) {
      stroke(ctx, P, m.points, 0, m.points.length - 1, color, rapid ? 0.55 : 1);
    } else if (t <= m.t0) {
      stroke(ctx, P, m.points, 0, m.points.length - 1, color, 0.12);
    } else {
      const f = (t - m.t0) / m.dt;
      const { idx, pos } = along(m, f);
      stroke(ctx, P, [...m.points.slice(0, idx + 1), pos], 0, idx + 1, color, rapid ? 0.55 : 1);
      stroke(ctx, P, [pos, ...m.points.slice(idx + 1)], 0, m.points.length - idx - 1, color, 0.12);
    }
    if (highlightLine && m.line === highlightLine) {
      ctx.setLineDash([]);
      ctx.lineWidth = 4;
      stroke(ctx, P, m.points, 0, m.points.length - 1, '#ffffff', 0.9);
    }
  }
  ctx.setLineDash([]);
  drawTool(ctx, P, cur);
  drawTriad(ctx, w, h);
  updateOverlay(cur);
}

function stroke(ctx, P, pts, from, to, color, alpha) {
  if (to <= from) return;
  ctx.globalAlpha = alpha;
  ctx.strokeStyle = color;
  ctx.beginPath();
  let p = P(...pts[from]);
  ctx.moveTo(p[0], p[1]);
  for (let i = from + 1; i <= to; i++) {
    p = P(...pts[i]);
    ctx.lineTo(p[0], p[1]);
  }
  ctx.stroke();
  ctx.globalAlpha = 1;
}

// Where fraction f of the way along a move is: the index of the polyline
// point before it and the interpolated position.
function along(m, f) {
  const d = Math.max(0, Math.min(1, f)) * m.len;
  let i = 0;
  while (i < m.cum.length - 2 && m.cum[i + 1] < d) i++;
  const a = m.points[i], b = m.points[i + 1] || a;
  const seg = (m.cum[i + 1] || 0) - m.cum[i];
  const u = seg > 0 ? (d - m.cum[i]) / seg : 0;
  return { idx: i, pos: [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u] };
}

// A grid on the Z0 plane (top of stock), a bit bigger than the part.
function drawBed(ctx, P) {
  const b = scene.box;
  const span = Math.max(b.max[0] - b.min[0], b.max[1] - b.min[1], 1);
  const step = Math.pow(10, Math.floor(Math.log10(span / 2)));
  const x0 = Math.floor((b.min[0] - span * 0.1) / step) * step, x1 = Math.ceil((b.max[0] + span * 0.1) / step) * step;
  const y0 = Math.floor((b.min[1] - span * 0.1) / step) * step, y1 = Math.ceil((b.max[1] + span * 0.1) / step) * step;
  const z = 0;
  ctx.lineWidth = 1;
  ctx.strokeStyle = 'rgba(255,255,255,0.05)';
  ctx.beginPath();
  for (let x = x0; x <= x1 + 1e-9; x += step) {
    const a = P(x, y0, z), c = P(x, y1, z);
    ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]);
  }
  for (let y = y0; y <= y1 + 1e-9; y += step) {
    const a = P(x0, y, z), c = P(x1, y, z);
    ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]);
  }
  ctx.stroke();
  // work origin
  const o = P(0, 0, 0), ox = P(step, 0, 0), oy = P(0, step, 0), oz = P(0, 0, step);
  ctx.lineWidth = 1.5;
  for (const [end, col] of [[ox, '#ff6b6b'], [oy, '#6bd49a'], [oz, '#7cb7ff']]) {
    ctx.strokeStyle = col;
    ctx.beginPath(); ctx.moveTo(o[0], o[1]); ctx.lineTo(end[0], end[1]); ctx.stroke();
  }
}

// Small XYZ triad in the corner so the view direction is always clear.
function drawTriad(ctx, w, h) {
  const cx = w - 40, cy = h - 36, L = 20;
  const zero = [0, 0, 0];
  ctx.lineWidth = 1.5;
  ctx.font = '10px JetBrains Mono, monospace';
  for (const [v, col, name] of [[[1, 0, 0], '#ff6b6b', 'X'], [[0, 1, 0], '#6bd49a', 'Y'], [[0, 0, 1], '#7cb7ff', 'Z']]) {
    const [sx, sy] = rotate(v[0], v[1], v[2], zero);
    ctx.strokeStyle = col;
    ctx.fillStyle = col;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + sx * L, cy - sy * L); ctx.stroke();
    if (Math.hypot(sx, sy) > 0.2) ctx.fillText(name, cx + sx * (L + 7) - 3, cy - sy * (L + 7) + 3);
  }
}

function toolPosition(m) {
  if (!m) return null;
  if (!m.motion || m.dt <= 0) return m.points[m.points.length - 1];
  return along(m, (play.t - m.t0) / m.dt).pos;
}

function drawTool(ctx, P, m) {
  const p = toolPosition(m);
  if (!p || play.t >= scene.total) return;
  const tip = P(...p);
  const b = scene.box;
  const shank = Math.max(b.max[0] - b.min[0], b.max[1] - b.min[1], 10) * 0.12;
  const top = P(p[0], p[1], p[2] + shank);
  ctx.strokeStyle = 'rgba(255,255,255,0.85)';
  ctx.lineWidth = 5;
  ctx.globalAlpha = 0.5;
  ctx.beginPath(); ctx.moveTo(tip[0], tip[1]); ctx.lineTo(top[0], top[1]); ctx.stroke();
  ctx.globalAlpha = 1;
  ctx.fillStyle = '#ff9d2e';
  ctx.beginPath(); ctx.arc(tip[0], tip[1], 4.5, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = '#1c0f00'; ctx.lineWidth = 1.5; ctx.stroke();
}

// ---- time and playback --------------------------------------------------------

function currentMove() {
  if (!scene || !scene.moves.length) return null;
  const t = Math.min(play.t, scene.total);
  // last move that starts at or before t (binary search over start times)
  let lo = 0, hi = scene.starts.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (scene.starts[mid] <= t) lo = mid; else hi = mid - 1;
  }
  // skip zero-time moves at the same instant, prefer the one being driven
  while (lo > 0 && scene.moves[lo].dt === 0 && scene.moves[lo].t0 === t && scene.moves[lo - 1].t0 + scene.moves[lo - 1].dt >= t && t < scene.total) lo--;
  return scene.moves[lo];
}

function updateTime() {
  if (!scene) return;
  play.t = Math.max(0, Math.min(play.t, scene.total));
  const frac = scene.total > 0 ? play.t / scene.total : 1;
  $('scrub').value = Math.round(frac * 1000);
  $('clock').textContent = `${clockFmt(play.t)} / ${clockFmt(scene.total)}`;
  const cursor = document.getElementById('speedCursor');
  if (cursor) {
    const x = cursor.dataset.x0 * 1 + frac * cursor.dataset.w;
    cursor.setAttribute('x1', x); cursor.setAttribute('x2', x);
  }
  renderPlayIcon();
  draw();
  if (play.playing) {
    const m = currentMove();
    if (m) scrollEditorTo(m.line);
  }
  renderGutter();
}

function updateOverlay(m) {
  const o = $('overlay');
  if (!scene || !m) { o.innerHTML = 'no moves'; return; }
  if (play.t >= scene.total) {
    const s = result.stats;
    o.innerHTML = `<b>${s.moves.linear + s.moves.arc + s.moves.rapid}</b> moves · <b>${fmtDuration(scene.total)}</b><br>press play to run the program`;
    return;
  }
  const p = toolPosition(m) || [0, 0, 0];
  const label = { rapid: 'G0 rapid', linear: 'G1 line', arc: 'G2/G3 arc', dwell: 'G4 dwell', tool_change: 'M6 tool change', pause: 'M0 pause' }[m.type];
  o.innerHTML =
    `line <b>${m.line}</b> · ${label}${m.type !== 'rapid' && m.motion ? ` · F<b>${m.feed}</b>` : ''}${m.tool ? ` · T${m.tool}` : ''}<br>` +
    `X <b>${p[0].toFixed(3)}</b> Y <b>${p[1].toFixed(3)}</b> Z <b>${p[2].toFixed(3)}</b>`;
}

function renderPlayIcon() {
  $('playBtn').innerHTML = play.playing
    ? '<svg width="14" height="14" viewBox="0 0 14 14"><rect x="2" y="1" width="3.5" height="12" fill="currentColor"/><rect x="8.5" y="1" width="3.5" height="12" fill="currentColor"/></svg>'
    : '<svg width="14" height="14" viewBox="0 0 14 14"><path d="M3 1l10 6-10 6z" fill="currentColor"/></svg>';
  $('playBtn').setAttribute('aria-label', play.playing ? 'pause' : 'play');
}

function togglePlay() {
  if (!scene) return;
  play.playing = !play.playing;
  if (play.playing) {
    if (play.t >= scene.total) play.t = 0;
    highlightLine = 0;
    play.last = performance.now();
    requestAnimationFrame(tick);
  }
  updateTime();
}

// Simulated seconds per real second. "fit:N" plays the moving part of the
// program in about N seconds; tool changes, dwells and pauses are skipped
// through in under half a second so they never look like a stall.
function playRate() {
  if (play.mode === 'real') return 1;
  const target = +play.mode.split(':')[1] || 25;
  const motion = scene.moves.reduce((s, m) => s + (m.motion ? m.dt : 0), 0);
  const rate = Math.max(motion, 1e-3) / target;
  const m = currentMove();
  if (m && !m.motion && m.dt > 0) return Math.max(rate, m.dt / 0.4);
  return rate;
}

function tick(now) {
  if (!play.playing) return;
  const dt = Math.min(0.1, (now - play.last) / 1000);
  play.last = now;
  play.t += dt * playRate();
  if (play.t >= scene.total) { play.t = scene.total; play.playing = false; }
  updateTime();
  if (play.playing) requestAnimationFrame(tick);
}

// ---- panels -------------------------------------------------------------------

function fmtDuration(s) {
  const tenths = Math.round(s * 10);
  if (tenths < 600) return (tenths / 10).toFixed(1) + ' s';
  const h = Math.floor(tenths / 36000), m = Math.floor(tenths / 600) % 60, sec = (tenths % 600) / 10;
  const ss = sec.toFixed(1).padStart(4, '0');
  return h > 0 ? `${h} h ${String(m).padStart(2, '0')} min ${ss} s` : `${m} min ${ss} s`;
}

function clockFmt(s) {
  if (s < 60) return s.toFixed(1) + ' s';
  const m = Math.floor(s / 60);
  return `${m}:${(s - m * 60).toFixed(1).padStart(4, '0')}`;
}

function esc(s) {
  return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

function renderStats(r) {
  const s = r.stats;
  const n = (v, d = 1) => Number(v).toFixed(d);
  const b = s.cut_bounds;
  const short = v => String(+v.toFixed(v >= 100 ? 0 : 1));
  const size = b ? [0, 1, 2].map(k => short(b.max[k] - b.min[k])).join(' × ') : 'none';
  const saved = r.time_no_lookahead_s - s.time_s.total;
  const cards = [
    ['Cycle time', fmtDuration(s.time_s.total),
      saved > 0.05 ? `${fmtDuration(r.time_no_lookahead_s)} without look-ahead` : 'look-ahead on', true],
    ['Cutting', fmtDuration(s.time_s.cutting), `${n(s.length_mm.cutting)} mm`],
    ['Rapids', fmtDuration(s.time_s.rapid), `${n(s.length_mm.rapid)} mm`],
    ['Tool changes, dwells', fmtDuration(s.time_s.other), `${s.moves.tool_changes} change${s.moves.tool_changes === 1 ? '' : 's'}, ${s.moves.dwells} dwell${s.moves.dwells === 1 ? '' : 's'}`],
    ['Moves', `${s.moves.linear + s.moves.arc + s.moves.rapid}`, `${s.moves.linear} lines · ${s.moves.arc} arcs · ${s.moves.rapid} rapids`],
    ['Cut extent', size, b ? `mm · Z ${n(b.min[2], 2)} to ${n(b.max[2], 2)}` : ''],
    ['Feed', s.feed_mm_min.max ? `${n(s.feed_mm_min.min, 0)}–${n(s.feed_mm_min.max, 0)}` : '–', 'mm/min'],
    ['Tools', s.tools.length ? s.tools.map(t => 'T' + t).join(' ') : 'none', r.machine],
  ];
  $('stats').innerHTML = cards.map(([label, value, sub, hl]) =>
    `<div class="stat${hl ? ' highlight' : ''}"><div class="label">${esc(label)}</div><div class="value">${esc(value)}</div><div class="sub">${esc(sub || '')}</div></div>`
  ).join('');
}

function renderDiagnostics(r) {
  const l = r.lint;
  const infos = l.diagnostics.length - l.errors - l.warnings;
  $('counts').innerHTML =
    `<span class="pill ${l.errors ? 'error' : ''}">${l.errors} error${l.errors === 1 ? '' : 's'}</span>` +
    `<span class="pill ${l.warnings ? 'warning' : ''}">${l.warnings} warning${l.warnings === 1 ? '' : 's'}</span>` +
    (infos ? `<span class="pill">${infos} note${infos === 1 ? '' : 's'}</span>` : '');
  const box = $('diags');
  if (!l.diagnostics.length) {
    box.innerHTML = `<div class="diag-empty"><b>No problems found.</b> All ${$('rules').children.length / 2} lint rules and the parser are happy.</div>`;
    return;
  }
  box.innerHTML = l.diagnostics.map((d, i) =>
    `<button class="diag ${d.severity}" data-i="${i}"><span class="code">${esc(d.code)}</span><span class="line">line ${d.line}</span><span>${esc(d.message)}</span></button>`
  ).join('');
  box.querySelectorAll('.diag').forEach(el => el.addEventListener('click', () => {
    const d = l.diagnostics[+el.dataset.i];
    jumpToLine(d.line);
  }));
}

// Select the line in the editor, highlight its moves, and move the playback
// to just after it.
function jumpToLine(line) {
  selectLine(line);
  highlightLine = line;
  const moves = scene.moves.filter(m => m.line === line);
  if (moves.length) {
    const last = moves[moves.length - 1];
    play.playing = false;
    play.t = last.t0 + last.dt;
    if (play.t >= scene.total) play.t = Math.max(0, scene.total - 1e-6);
  }
  updateTime();
}

function renderRules(rules) {
  $('rules').innerHTML = rules.map(r => `<dt>${esc(r.code)}</dt><dd>${esc(r.summary)}</dd>`).join('');
}

function renderLegend() {
  const by = $('colorBy').value;
  const L = $('legend');
  const swatch = (col, name) => `<div class="row">${name}<span class="sw" style="background:${col}"></span></div>`;
  let html = '';
  if (by === 'feed' || by === 'depth') {
    const [lo, hi] = by === 'feed' ? scene.feed : scene.depth;
    const stops = RAMP.map((c, i) => `rgb(${c}) ${i * 25}%`).join(',');
    const f = v => isFinite(v) ? (by === 'feed' ? Math.round(v) : v.toFixed(2)) : '–';
    html = `${by === 'feed' ? 'feed mm/min' : 'depth Z mm'}<div class="bar" style="background:linear-gradient(90deg,${stops})"></div>` +
      `<div class="ends"><span>${f(lo)}</span><span>${f(hi)}</span></div>`;
  } else if (by === 'type') {
    html = swatch(TYPE_COLORS.linear, 'G1 line') + swatch(TYPE_COLORS.arc, 'G2/G3 arc');
  } else {
    html = scene.tools.length ? scene.tools.map((t, i) => swatch(TOOL_COLORS[i % TOOL_COLORS.length], 'T' + t)).join('') : 'no tools';
  }
  if ($('showRapids').checked) html += `<div class="row" style="margin-top:4px">G0 rapid<span class="sw" style="background:repeating-linear-gradient(90deg,${TYPE_COLORS.rapid} 0 3px,transparent 3px 5px)"></span></div>`;
  L.innerHTML = html;
}

// Planned speed over time, built from the planner's trapezoids.
function renderSpeedChart(r) {
  const rows = r.toolpath.profile;
  const W = 800, H = 200, L = 52, R = 12, T = 12, B = 30;
  const total = r.stats.time_s.total;
  const box = $('speedChart');
  if (!rows.length || total <= 0) {
    box.innerHTML = '<div class="diag-empty">No moves with a speed to plot.</div>';
    return;
  }
  let vmax = 0;
  for (const row of rows) vmax = Math.max(vmax, row[2] * 60);
  const niceMax = niceCeil(vmax);
  const X = t => L + t / total * (W - L - R);
  const Y = v => T + (1 - v * 60 / niceMax) * (H - T - B);
  let d = `M${X(0).toFixed(1)},${Y(0).toFixed(1)}`;
  let lastT = 0;
  for (const [t0, v0, vp, v1, ta, tc, td] of rows) {
    if (t0 > lastT + 1e-9) d += `L${X(lastT).toFixed(1)},${Y(0).toFixed(1)}L${X(t0).toFixed(1)},${Y(0).toFixed(1)}`;
    d += `L${X(t0).toFixed(1)},${Y(v0).toFixed(1)}`;
    d += `L${X(t0 + ta).toFixed(1)},${Y(vp).toFixed(1)}`;
    if (tc > 0) d += `L${X(t0 + ta + tc).toFixed(1)},${Y(vp).toFixed(1)}`;
    d += `L${X(t0 + ta + tc + td).toFixed(1)},${Y(v1).toFixed(1)}`;
    lastT = t0 + ta + tc + td;
  }
  let grid = '';
  for (let i = 0; i <= 4; i++) {
    const v = niceMax * i / 4, y = T + (1 - i / 4) * (H - T - B);
    grid += `<line class="grid-line" x1="${L}" x2="${W - R}" y1="${y}" y2="${y}"/><text class="axis-label" x="${L - 6}" y="${y + 3}" text-anchor="end">${Math.round(v)}</text>`;
  }
  const tStep = niceStep(total / 6);
  for (let t = 0; t <= total + 1e-9; t += tStep) {
    grid += `<text class="axis-label" x="${X(t)}" y="${H - 10}" text-anchor="middle">${clockFmt(t)}</text>`;
  }
  box.innerHTML =
    `<svg class="chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="planned speed over time">${grid}` +
    `<text class="axis-label" x="${L}" y="${T - 2}">mm/min</text>` +
    `<path d="${d}" fill="none" stroke="#ff9d2e" stroke-width="1.3" stroke-linejoin="round"/>` +
    `<line id="speedCursor" data-x0="${L}" data-w="${W - L - R}" x1="${L}" x2="${L}" y1="${T}" y2="${H - B}" stroke="#e7eaee" stroke-width="1" stroke-dasharray="3 3" opacity=".6"/></svg>`;
}

function niceCeil(v) {
  if (!(v > 0)) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const m of [1, 2, 2.5, 5, 10]) if (m * p >= v) return m * p;
  return 10 * p;
}

function niceStep(v) {
  if (!(v > 0)) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const m of [1, 2, 5, 10]) if (m * p >= v) return m * p;
  return 10 * p;
}

function toast(msg) {
  let t = document.querySelector('.toast');
  if (!t) { t = document.createElement('div'); t.className = 'toast'; document.body.appendChild(t); }
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.remove('show'), 2600);
}

async function download(name, text, type) {
  if (saver) {
    try {
      await saver.save({ filename: name, data: new Blob([text], { type }) });
    } catch (e) {
      // some viewers only accept common extensions (.nc isn't one): save as .txt
      if (e && e.code === 'rejected_extension' && !name.endsWith('.txt')) return download(name.replace(/\.[^.]+$/, '') + '.txt', text, 'text/plain');
      if (e && e.code !== 'declined') toast('Could not save ' + name + (e.message ? ': ' + e.message : ''));
    }
    return;
  }
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ---- wiring ---------------------------------------------------------------------

function wire() {
  buildPresets();
  const sel = $('machineSelect');
  for (const m of machineList()) sel.add(new Option(m.name, m.key));
  sel.add(new Option('No config: generic defaults', ''));
  const stockSel = $('stockSelect');
  stockSel.add(new Option('Not set (no speed and feed checks)', ''));
  for (const [key, label] of MATERIALS) stockSel.add(new Option(label, key));
  stockSel.addEventListener('change', () => { run(); track('material-' + (stockSel.value || 'none')); });
  selectMachine(machineList()[0].key);
  renderPlayIcon();
  // Inside Claude, downloads go through the viewer's save dialog.
  if (window.claude && window.claude.use) {
    window.claude.use('downloads').then(d => { saver = d; }).catch(() => {});
  }

  const code = $('code');
  code.addEventListener('input', () => {
    highlightLine = 0;
    // editing turns whatever is loaded into "your own" program and keeps it
    if (presetKey !== 'own') {
      presetKey = 'own';
      document.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c.dataset.key === 'own'));
    }
    try { localStorage.setItem(DRAFT_KEY, code.value); } catch (e) { /* storage blocked */ }
    scheduleRun();
  });
  $('openBtn').addEventListener('click', () => $('fileInput').click());
  $('fileInput').addEventListener('change', async () => {
    const f = $('fileInput').files[0];
    $('fileInput').value = '';
    if (!f) return;
    if (f.size > 5e6) { showError('That file is over 5 MB; this page is meant for programs up to a few MB.'); return; }
    const text = await f.text();
    try { localStorage.setItem(DRAFT_KEY, text); } catch (e) { /* storage blocked */ }
    loadPreset('own');
    toast('Loaded ' + f.name);
    track('open-file');
  });
  code.addEventListener('scroll', () => { $('gutter').scrollTop = code.scrollTop; });
  code.addEventListener('keydown', e => {
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); clearTimeout(runTimer); run(); }
    if (e.key === 'Tab') {  // insert a tab instead of leaving the editor
      e.preventDefault();
      const s = code.selectionStart;
      code.setRangeText('\t', s, code.selectionEnd, 'end');
      scheduleRun();
    }
  });
  // Fill the issue link at click time so it carries the current program.
  for (const id of ['feedbackBtn', 'reportLink']) {
    $(id).addEventListener('click', e => { e.preventDefault(); openFeedback(); });
  }
  for (const id of ['fbWith', 'fbPlain', 'fbEmail']) $(id).addEventListener('click', onFeedbackLink);
  $('fbCopy').addEventListener('click', async () => {
    $('fbLink').select();
    try { await navigator.clipboard.writeText($('fbLink').value); toast('Link copied'); }
    catch (e) { try { document.execCommand('copy'); toast('Link copied'); } catch (e2) { toast('Select the link and copy it'); } }
  });
  // Already a local file: nothing to download.
  if (location.protocol === 'file:') $('offlineBtn').hidden = true;
  $('offlineBtn').addEventListener('click', e => { e.preventDefault(); downloadOffline(); });
  startCounting();
  $('runBtn').addEventListener('click', () => { clearTimeout(runTimer); run(); });
  $('machineSelect').addEventListener('change', () => { selectMachine($('machineSelect').value); run(); track('machine-' + ($('machineSelect').value || 'none')); });
  $('machineJson').addEventListener('input', scheduleRun);

  document.querySelectorAll('#viewSeg button').forEach(b => b.addEventListener('click', () => setView(b.dataset.view)));
  $('colorBy').addEventListener('change', () => { renderLegend(); draw(); });
  $('showRapids').addEventListener('change', () => { renderLegend(); draw(); });
  $('playBtn').addEventListener('click', togglePlay);
  $('scrub').addEventListener('input', () => {
    if (!scene) return;
    play.playing = false;
    play.t = $('scrub').value / 1000 * scene.total;
    updateTime();
  });
  $('speed').addEventListener('change', () => { play.mode = $('speed').value; play.last = performance.now(); });

  // generator and quote
  for (const h of GEN_HOLES) $('genHole').add(new Option(h.label, h.key));
  $('genHole').value = 'M12';
  $('genGridBtn').addEventListener('click', () => { $('genGrid').hidden = !$('genGrid').hidden; $('genCircle').hidden = true; });
  $('genCircleBtn').addEventListener('click', () => { $('genCircle').hidden = !$('genCircle').hidden; $('genGrid').hidden = true; });
  $('genGridAdd').addEventListener('click', () => addPattern('grid'));
  $('genCircleAdd').addEventListener('click', () => addPattern('circle'));
  $('genClearBtn').addEventListener('click', () => { $('genHoles').value = ''; updateGenCount(); });
  $('genHoles').addEventListener('input', updateGenCount);
  $('genBtn').addEventListener('click', generateProgram);
  try {
    const q = JSON.parse(localStorage.getItem(QUOTE_KEY) || 'null');
    if (q) { $('qRate').value = q.rate; $('qSetup').value = q.setup; $('qLoad').value = q.load; $('qQty').value = q.qty; }
  } catch (e) { /* storage blocked */ }
  for (const id of ['qRate', 'qSetup', 'qLoad', 'qQty']) $(id).addEventListener('input', renderQuote);
  $('ncBtn').addEventListener('click', () => { download('program.nc', code.value, 'text/plain'); track('download-program'); });

  $('svgBtn').addEventListener('click', () => {
    if (!engine) return;
    const svg = engine.renderSvg(code.value, machineConfig(), $('colorBy').value === 'depth' ? 1 : 0, $('showRapids').checked ? 1 : 0);
    if (svg.startsWith('ERROR')) { showError(svg); return; }
    download('toolpath.svg', svg, 'image/svg+xml');
  });
  $('jsonBtn').addEventListener('click', () => {
    if (!result) return;
    download('analysis.json', JSON.stringify({ stats: result.stats, lint: result.lint }, null, 2), 'application/json');
  });

  // orbit / pan / zoom
  const v = $('viewer');
  let drag = null;
  v.addEventListener('pointerdown', e => {
    drag = { x: e.clientX, y: e.clientY, pan: e.shiftKey || e.button !== 0 };
    v.setPointerCapture(e.pointerId);
  });
  v.addEventListener('pointermove', e => {
    if (!drag || !scene) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    drag.x = e.clientX; drag.y = e.clientY;
    if (drag.pan) {
      cam.panX += dx / cam.zoom; cam.panY += dy / cam.zoom;
    } else {
      cam.yaw -= dx * 0.008;
      cam.pitch = Math.max(0, Math.min(Math.PI / 2, cam.pitch + dy * 0.008));
      document.querySelectorAll('#viewSeg button').forEach(b => b.classList.remove('active'));
    }
    draw();
  });
  v.addEventListener('pointerup', () => { drag = null; });
  v.addEventListener('contextmenu', e => e.preventDefault());
  v.addEventListener('wheel', e => {
    e.preventDefault();
    cam.zoom = Math.max(0.1, Math.min(60, cam.zoom * Math.exp(-e.deltaY * 0.0015)));
    draw();
  }, { passive: false });
  v.addEventListener('dblclick', () => { fitView(); draw(); });

  if (window.ResizeObserver) new ResizeObserver(() => draw()).observe(v);
}

wire();

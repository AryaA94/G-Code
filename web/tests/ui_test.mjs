// End-to-end test of the built page (web/dist/gcode-sim-web.html) in a real
// headless Chromium. For every example it checks that the numbers the page
// shows come from the same engine as the native CLI: the WASM analysis must
// match `gcode-sim stats --format json` and `gcode-sim lint --format json`.
//
//   cd web/tests && npm install && node ui_test.mjs
// Needs the native CLI at ../../build/gcode-sim (override with GCODESIM_CLI).
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const page_path = resolve(root, 'web/dist/gcode-sim-web.html');
const cli = process.env.GCODESIM_CLI || resolve(root, 'build/gcode-sim');
const examples = ['bracket', 'pocket', 'gear', 'contour', 'drill_plate', 'lint_demo'];

let failures = 0;
function check(ok, what) {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) failures++;
}
function close(a, b) {
  return Math.abs(a - b) <= 1e-6 * Math.max(1, Math.abs(a), Math.abs(b));
}
function native(cmd, name) {
  try {
    return JSON.parse(execFileSync(cli, [cmd, `${root}/examples/${name}.nc`, '-c', `${root}/examples/machine.json`, '--format', 'json']));
  } catch (e) {
    return JSON.parse(e.stdout);  // lint exits 1 when it finds errors
  }
}

if (!existsSync(cli)) {
  console.error(`native CLI not found at ${cli}; build it first`);
  process.exit(2);
}

const browser = await chromium.launch(process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {});
const page = await browser.newPage({ viewport: { width: 1300, height: 900 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
page.on('console', m => {
  // fonts come from Google and may be blocked where the test runs
  if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) errors.push(m.text());
});
await page.goto('file://' + page_path);
await page.waitForFunction(() => document.querySelectorAll('#rules dt').length > 0, null, { timeout: 20000 });
check((await page.locator('#rules dt').count()) === 11, 'page lists eleven lint rules');

// real machines are offered; the CLI comparison below uses examples/machine.json
const machines = await page.evaluate(() => [...document.querySelectorAll('#machineSelect option')].map(o => o.value));
check(machines[0] === 'haas_vf2' && machines.includes('example') && machines.includes(''), `machine list: ${machines.join(', ')}`);
await page.selectOption('#machineSelect', 'example');

for (const name of examples) {
  await page.click(`[data-key=${name}]`);
  const r = await page.evaluate(() => result);
  const stats = native('stats', name);
  const lint = native('lint', name);
  check(close(r.stats.time_s.total, stats.time_s.total), `${name}: cycle time ${r.stats.time_s.total} matches CLI ${stats.time_s.total}`);
  check(close(r.stats.length_mm.cutting, stats.length_mm.cutting), `${name}: cutting length matches CLI`);
  check(JSON.stringify(r.stats.moves) === JSON.stringify(stats.moves), `${name}: move counts match CLI`);
  check(JSON.stringify(r.lint) === JSON.stringify(lint), `${name}: diagnostics match CLI (${lint.errors} errors, ${lint.warnings} warnings)`);
  const cards = await page.locator('#diags .diag').count();
  check(cards === lint.diagnostics.length, `${name}: ${cards} diagnostic cards shown`);
  check(r.time_no_lookahead_s >= r.stats.time_s.total - 1e-9, `${name}: look-ahead never makes it slower`);
}

// clicking a diagnostic selects its line in the editor
await page.click('[data-key=lint_demo]');
await page.locator('#diags .diag').nth(4).click();
const sel = await page.evaluate(() => {
  const ta = document.getElementById('code');
  return ta.value.slice(ta.selectionStart, ta.selectionEnd);
});
check(sel === 'G0 X300', `clicking LN002 selects its line ("${sel}")`);

// editing the program re-runs it
await page.click('[data-key=bracket]');
await page.fill('#code', 'G21 G90\nT1 M6\nS1000 M3\nG1 X10 F600\nM30\n');
await page.waitForFunction(() => result && result.stats.moves.linear === 1 && result.stats.moves.arc === 0);
check(true, 'typing a new program re-runs the analysis');
check(await page.evaluate(() => presetKey === 'own' && document.querySelector('.chip.active').dataset.key === 'own'),
  'editing a program switches to "your own"');

// "your own" brings the edited program back after looking at an example
await page.click('[data-key=pocket]');
await page.click('[data-key=own]');
check((await page.inputValue('#code')).includes('G1 X10 F600'), '"your own" restores the program you typed');

// playback: a tool change is skipped through instead of stalling the animation
await page.click('[data-key=bracket]');
const rates = await page.evaluate(() => {
  play.mode = 'fit:25';
  const tc = scene.moves.find(m => m.type === 'tool_change' && m.dt > 0);
  play.t = tc.t0 + tc.dt / 2;
  return { atToolChange: tc.dt / playRate() };
});
check(rates.atToolChange <= 0.41, `a tool change plays in ${rates.atToolChange.toFixed(2)} s`);

// a broken machine config shows an error instead of crashing
await page.evaluate(() => { document.getElementById('machineJson').closest('details').open = true; });
await page.fill('#machineJson', '{ "max_rate_mm_min": 5 }');
await page.click('#runBtn');
check(/Machine config/.test(await page.textContent('#errorBanner')), 'bad machine.json shows an error banner');

// playback moves time forward
await page.click('[data-key=contour]');
await page.evaluate(() => { play.t = 0; updateTime(); });
await page.click('#playBtn');
await page.waitForTimeout(500);
const t = await page.evaluate(() => play.t);
check(t > 0.5, `playback advances (t = ${t.toFixed(2)} s after 0.5 s of playback)`);

// real-world programs: a GRBL router file needs no M6, an inch file is converted
const grbl = readFileSync(resolve(root, 'examples/real/grbl_hobby.gcode'), 'utf8');
await page.selectOption('#machineSelect', 'hobby_router_grbl');
await page.fill('#code', grbl);
await page.waitForFunction(() => result && result.stats.moves.linear === 5);
check(await page.evaluate(() => result.lint.errors + result.lint.warnings === 0), 'GRBL program on the hobby router: no errors or warnings');
const inch = readFileSync(resolve(root, 'examples/real/fusion_haas_inch.nc'), 'utf8');
await page.selectOption('#machineSelect', 'haas_vf2');
await page.fill('#code', inch);
await page.waitForFunction(() => result && result.stats.cut_bounds && result.stats.cut_bounds.min[2] < -3);
const zmin = await page.evaluate(() => result.stats.cut_bounds.min[2]);
check(Math.abs(zmin + 3.175) < 1e-6, `inch program: Z-0.125 in reads as ${zmin} mm`);

// the feedback link carries the program and machine
const fb = new URL(await page.evaluate(() => feedbackUrl()));
const body = fb.searchParams.get('body');
check(fb.pathname.endsWith('/issues/new') && body.includes('O01002') && body.includes('Haas VF-2'), 'feedback link includes the program and machine');

// plate example: picks the VF-5/50, matches the CLI, and the material check fires on AR500
await page.click('[data-key=plate_drill_tap]');
check(await page.inputValue('#machineSelect') === 'haas_vf5_50', 'plate example selects the Haas VF-5/50');
check(await page.inputValue('#stockSelect') === 'alloy_steel', 'VF-5/50 preset sets the stock material');
const plateNative = JSON.parse(execFileSync(cli, ['lint', `${root}/examples/plate_drill_tap.nc`, '-c', `${root}/examples/machines/haas_vf5_50.json`, '--format', 'json']));
check(await page.evaluate(n => JSON.stringify(result.lint) === n, JSON.stringify(plateNative)), 'plate example: diagnostics match CLI');
await page.selectOption('#stockSelect', 'ar500');
await page.waitForFunction(() => result.lint.diagnostics.some(d => d.code === 'LN010'));
check(true, 'AR500 flags spindle speeds that are too fast (LN010)');

// every material in the page is one the engine knows
const badMaterials = await page.evaluate(() => MATERIALS.filter(([k]) => {
  const r = JSON.parse(engine.analyze('G0 X0', JSON.stringify({ stock_material: k })));
  return !r.ok;
}).map(([k]) => k));
check(badMaterials.length === 0, `all page materials accepted by the engine${badMaterials.length ? ': ' + badMaterials : ''}`);

// feedback asks first and warns that issues are public; email hidden until configured
await page.click('#feedbackBtn');
check(await page.evaluate(() => document.getElementById('feedbackDialog').open), 'feedback opens a dialog first');
check(/public/i.test(await page.textContent('.fb-warn')), 'dialog warns that issues are public');
check(await page.evaluate(() => document.getElementById('fbEmail').hidden === !FEEDBACK_EMAIL), 'email option shown only when an address is set');
const plainHref = await page.getAttribute('#fbPlain', 'href');
const withHref = await page.getAttribute('#fbWith', 'href');
check(plainHref.includes('/issues/new') && withHref.includes('/issues/new'), 'feedback choices are real links to a new issue');
check(!new URL(plainHref).searchParams.get('body').includes('G84') && new URL(withHref).searchParams.get('body').includes('G84'),
  'only the "include my program" link carries the program');
await page.click('#fbClose');
check(!(await page.evaluate(() => document.getElementById('feedbackDialog').open)), 'Close closes the feedback dialog');

// offline button is hidden when the page is already a local file
check(await page.evaluate(() => document.getElementById('offlineBtn').hidden), 'offline download hidden for a local file');

await page.evaluate(() => showTab('make'));
// drilling program generator: grid of tapped holes in 4140 on the VF-5/50
await page.selectOption('#machineSelect', 'haas_vf5_50');
await page.selectOption('#stockSelect', 'alloy_steel');
await page.evaluate(() => { document.getElementById('genPanel').open = true; });
await page.click('#genClearBtn');
await page.click('#genGridBtn');
await page.fill('#gnx', '3'); await page.fill('#gny', '2');
await page.click('#genGridAdd');
check((await page.inputValue('#genHoles')).trim().split('\n').length === 6, 'grid adds 6 holes');
await page.selectOption('#genHole', 'M12');
await page.fill('#genThick', '25');
await page.click('#genBtn');
await page.waitForFunction(() => document.getElementById('code').value.includes('G84'));
const gen = await page.evaluate(() => ({ code: document.getElementById('code').value, lint: result.lint, cfg: JSON.parse(machineConfig()) }));
check(/G7[3]|G83/.test(gen.code) && gen.code.includes('T21 M6') && gen.code.includes('T23 M6'), 'generated program spots, peck drills and taps');
check(gen.lint.errors === 0 && gen.lint.warnings === 0, `generated program is clean (${gen.lint.errors} errors, ${gen.lint.warnings} warnings)`);
check(gen.cfg.tools['22'].diameter_mm === 10.2 && gen.cfg.tools['23'].material === 'hss', 'generator adds its tools to the machine');
// same program and machine through the native CLI
const fsMod = await import('node:fs');
const tmpNc = resolve(here, '_gen.nc'), tmpJson = resolve(here, '_gen.json');
fsMod.writeFileSync(tmpNc, gen.code); fsMod.writeFileSync(tmpJson, JSON.stringify(gen.cfg));
let genNative; try { genNative = JSON.parse(execFileSync(cli, ['lint', tmpNc, '-c', tmpJson, '--format', 'json'])); } catch (e) { genNative = JSON.parse(e.stdout); }
fsMod.unlinkSync(tmpNc); fsMod.unlinkSync(tmpJson);
check(JSON.stringify(genNative) === JSON.stringify(gen.lint), 'generated program: page and CLI agree');

await page.evaluate(() => showTab('make'));
// too hard to tap: AR500 refuses with a reason
await page.selectOption('#stockSelect', 'ar500');
await page.click('#genBtn');
check(/too hard to tap/.test(await page.textContent('#genWarn')), 'AR500 + tapped hole explains it can\'t be tapped');
await page.selectOption('#genHole', 'D17.5');
await page.click('#genBtn');
await page.waitForFunction(() => document.getElementById('code').value.includes('17.5 MM'));
check(await page.evaluate(() => result.lint.errors === 0 && result.lint.warnings === 0), 'AR500 drilled holes: clean program');

// hole list parsing: commas, tabs, semicolons, decimal commas, headers
const parsed = await page.evaluate(() => parseHoles('X,Y\n10,10\n20, 20\n30\t30\n12,5;40,25\n-5 7.5\n  \nnote'));
check(JSON.stringify(parsed) === JSON.stringify({ holes: [[10, 10], [20, 20], [30, 30], [12.5, 40.25], [-5, 7.5]], skipped: 2 }),
  `hole list parsing: ${JSON.stringify(parsed)}`);

await page.evaluate(() => showTab('plan'));
// quote: cost follows the cycle time and the inputs
await page.fill('#qRate', '120'); await page.fill('#qSetup', '30'); await page.fill('#qLoad', '0'); await page.fill('#qQty', '1');
const q = await page.evaluate(() => ({ total: result.stats.time_s.total, text: document.getElementById('quote').textContent }));
const expect = ((30 + q.total / 60) / 60 * 120).toFixed(2);
check(q.text.includes('$' + expect), `quote: 1 part = $${expect}`);

await page.evaluate(() => showTab('check'));
// time by operation: named from the comments, adds up to the cycle time
await page.click('[data-key=plate_drill_tap]');
const ops = await page.evaluate(() => computeOperations(result, document.getElementById('code').value)
  .map(o => ({ name: o.name, tool: o.tool, total: o.total })));
check(JSON.stringify(ops.map(o => o.name)) === JSON.stringify(['FACE 1 mm off the top', 'SPOT DRILL', 'DRILL 10.2 THROUGH, HIGH-SPEED PECK', 'TAP M12 X 1.75']),
  `operations named from comments: ${ops.map(o => o.name).join(' / ')}`);
const opSum = ops.reduce((a, o) => a + o.total, 0);
check(Math.abs(opSum - await page.evaluate(() => result.stats.time_s.total)) < 0.01, 'operation times add up to the cycle time');
check((await page.locator('#ops tr.op-row').count()) === 4, 'operations table has 4 rows');
await page.locator('#ops tr.op-row').nth(2).click();
check(/DRILL 10.2/.test(await page.evaluate(() => { const t = document.getElementById('code'); return t.value.slice(t.selectionStart, t.selectionEnd); })),
  'clicking an operation selects its heading line');

// setup sheet
await page.click('#sheetBtn');
check(await page.evaluate(() => document.getElementById('sheetDialog').open), 'setup sheet opens');
const sheet = await page.textContent('#sheet');
check(/T5/.test(sheet) && /T4/.test(sheet) && /G54/.test(sheet) && /Flood \(M8\)/.test(sheet) && /Haas VF-5\/50/.test(sheet),
  'setup sheet lists tools, work offset, coolant and machine');
await page.locator('#sheet [contenteditable]').first().fill('PN-1234');
const [sheetDl] = await Promise.all([page.waitForEvent('download'), page.click('#sheetDownload')]);
const sheetHtml = (await import('node:fs')).readFileSync(await sheetDl.path(), 'utf8');
check(sheetHtml.includes('PN-1234') && sheetHtml.includes('<table') && !sheetHtml.includes('contenteditable'), 'downloaded sheet keeps typed fields');
await page.click('#sheetClose');
check(!(await page.evaluate(() => document.getElementById('sheetDialog').open)), 'setup sheet closes');

await page.evaluate(() => showTab('plan'));
// tool cost feeds the quote
await page.fill('#qRate', '100'); await page.fill('#qSetup', '0'); await page.fill('#qLoad', '0'); await page.fill('#qQty', '1');
await page.evaluate(() => { document.querySelector('.tool-cost').open = true; });
const before = await page.evaluate(() => toolingPerPart());
await page.fill('#toolCost input[data-t="3"][data-k="price"]', '120');
await page.fill('#toolCost input[data-t="3"][data-k="life"]', '60');
await page.click('#qRate');  // leave the field
const t3 = await page.evaluate(() => cutTimeByTool(result)[3]);
const tooling = await page.evaluate(() => toolingPerPart());
check(before === 0 && Math.abs(tooling - t3 / 60 / 60 * 120) < 1e-9, `tool wear per part = ${tooling.toFixed(4)}`);
check((await page.textContent('#quote')).includes('Tool wear per part'), 'quote shows tool wear');
await page.evaluate(() => { localStorage.removeItem('gcode-sim.toolcost'); toolCosts = {}; renderToolCost(); renderQuote(); });

await page.evaluate(() => showTab('check'));
// compare against a baseline
await page.click('#baseSetBtn');
const edited = (await page.inputValue('#code')).replace('S2800 M3', 'S2200 M3').replace('F420.', 'F330.');
await page.fill('#code', edited);
await page.waitForFunction(() => document.getElementById('compare').textContent.includes('lines removed'));
const cmp = await page.textContent('#compare');
check(/S 2800 → 2200/.test(cmp) && /F 420 → 330/.test(cmp), 'compare lists the speed and feed change');
check(/2 lines removed, 2 added/.test(cmp), 'compare counts changed lines');
check(await page.evaluate(() => document.querySelector('#compare .stat .value').classList.contains('delta-bad')), 'slower cycle shown as worse');
await page.click('#baseClearBtn');
check(/cleared/.test(await page.textContent('#compare')), 'baseline clears');

await page.evaluate(() => showTab('plan'));
// material removal: known answer, a 30 x 30 x 10 mm square profiled out of 50 x 50 stock
await page.selectOption('#machineSelect', 'haas_vf2');
await page.selectOption('#stockSelect', 'aluminum_6061');
await page.fill('#code', 'G21 G90\nT1 M6\nS8000 M3\nG0 X-3 Y-3 Z5\nG1 Z-11 F300\nG1 X33 F800\nG1 Y33\nG1 X-3\nG1 Y-3\nG0 Z5\nM30\n');
await page.waitForFunction(() => result && result.stats.moves.linear === 5);
for (const [id, v] of [['stX', 50], ['stY', 50], ['stZ', 10], ['stX0', -10], ['stY0', -10]]) { await page.fill('#' + id, String(v)); await page.dispatchEvent('#' + id, 'change'); }
const sq = await page.evaluate(() => ({ vol: partSim.partVol, pieces: partSim.pieces, g: partNumbers().partG }));
check(Math.abs(sq.vol - 9000) < 1 && sq.pieces === 2, `profiled square: ${sq.vol.toFixed(1)} mm³ (exact 9000), part and offcut separated`);
check(Math.abs(sq.g - 9000 * 2.70 / 1000) < 0.1, `profiled square mass ${sq.g.toFixed(2)} g in 6061`);

// FSAE upright: 12 mm plate guessed from the program, part separated, about 294 g in 7075
await page.click('[data-key=fsae_upright]');
const up = await page.evaluate(() => ({ sz: stock.sz, g: partNumbers().partG, pieces: partSim.pieces }));
check(up.sz === 12 && up.pieces >= 2 && Math.abs(up.g - 294) < 6, `upright: ${up.sz} mm stock, ${up.g.toFixed(0)} g`);
await page.check('#showPart');
check(await page.evaluate(() => { draw(); return true; }), 'finished part view draws');
await page.uncheck('#showPart');
// generated plate: stock thickness is the plate, not the drill breakthrough
await page.click('[data-key=plate_drill_tap]');
check(await page.evaluate(() => stock.sz) === 25, 'plate example: 25 mm stock (drill breakthrough ignored)');

await page.evaluate(() => showTab('plan'));
// planner and cost report
await page.evaluate(() => { plan = []; savePlan(); renderPlan(); });
await page.click('[data-key=fsae_upright]'); await page.click('#planAdd');
await page.click('[data-key=plate_drill_tap]'); await page.click('#planAdd');
check((await page.locator('#plan tbody tr').count()) === 2, 'planner holds two parts');
await page.fill('#plan .plan-qty >> nth=0', '4');
const planT = await page.evaluate(() => plan.reduce((a, p) => a + planRow(p).machineMin, 0));
const expectT = await page.evaluate(() => plan.reduce((a, p) => a + p.setupMin + p.qty * (p.cycleS / 60 + p.loadMin), 0));
check(Math.abs(planT - expectT) < 1e-9 && await page.evaluate(() => plan[0].qty) === 4, 'quantity changes the machine time');
await page.fill('#planHours', '1'); await page.fill('#planDue', '2000-01-01');
check(/Over/.test(await page.textContent('#planStats')), 'past due date shows Over');
const [csvDl] = await Promise.all([page.waitForEvent('download'), page.click('#planCsv')]);
const csv = (await import('node:fs')).readFileSync(await csvDl.path(), 'utf8').trim().split('\n');
check(csv.length === 3 && csv[0].startsWith('Part,Qty') && /^"FSAE UPRIGHT[^"]*",4,/.test(csv[1]), 'cost report CSV: header and two parts, names with commas quoted');
await page.click('#planClear');
check(await page.evaluate(() => plan.length) === 2 && /again/.test(await page.textContent('#planClear')), 'Clear asks for a second click');
await page.click('#planClear');
check(await page.evaluate(() => plan.length) === 0, 'second click clears');

await page.evaluate(() => showTab('check'));
// feedback form: hidden without a key; with one, sends the message (service faked here)
const realKey = await page.evaluate(() => WEB3FORMS_KEY);
await page.evaluate(() => { WEB3FORMS_KEY = ''; });
await page.click('#feedbackBtn');
check(await page.evaluate(() => document.getElementById('fbForm').hidden), 'email form hidden without a key');
await page.click('#fbClose');
let sent = null;
await page.route('https://api.web3forms.com/submit', route => { sent = JSON.parse(route.request().postData()); route.fulfill({ status: 200, contentType: 'application/json', body: '{"success":true}' }); });
await page.evaluate(() => { WEB3FORMS_KEY = 'test-key'; });
await page.click('#feedbackBtn');
await page.fill('#fbMsg', 'Tapping cycle reads wrong');
await page.fill('#fbFrom', 'someone@example.com');
await page.click('#fbSend');
await page.waitForFunction(() => /Sent/.test(document.getElementById('fbStatus').textContent));
check(sent && sent.access_key === 'test-key' && sent.message === 'Tapping cycle reads wrong' && sent.replyto === 'someone@example.com' && sent.program === '(not included)',
  'form sends message and reply-to, program left out unless ticked');
await page.fill('#fbMsg', 'with program'); await page.check('#fbIncl'); await page.click('#fbSend');
await page.waitForFunction(() => /Sent/.test(document.getElementById('fbStatus').textContent) && document.getElementById('fbMsg').value === '');
check(sent.program.includes('G21'), 'ticking the box includes the program');
await page.unroute('https://api.web3forms.com/submit');
await page.route('https://api.web3forms.com/submit', route => route.fulfill({ status: 400, contentType: 'application/json', body: '{"success":false,"message":"Invalid key"}' }));
await page.fill('#fbMsg', 'x'); await page.click('#fbSend');
await page.waitForFunction(() => /Could not send/.test(document.getElementById('fbStatus').textContent));
check(/Invalid key/.test(await page.textContent('#fbStatus')), 'a failed send says why');
await page.unroute('https://api.web3forms.com/submit');
await page.click('#fbClose');
await page.evaluate(k => { WEB3FORMS_KEY = k; }, realKey);
await page.click('#feedbackBtn');
check(!(await page.evaluate(() => document.getElementById('fbForm').hidden)) === !!realKey, 'email form shown with the real key');
await page.click('#fbClose');

// tabs: each shows its own panels; the program, machine and toolpath stay
const visible = id => page.evaluate(i => { const e = document.getElementById(i); return !!e && e.offsetParent !== null; }, id);
await page.click('[data-tabbtn=make]');
check(await visible('genBtn') && !(await visible('stats')) && await visible('code') && await visible('canvas'), 'Make tab: generator, program and toolpath');
await page.click('[data-tabbtn=plan]');
check(await visible('quote') && await visible('planAdd') && await visible('partStats') && !(await visible('diags')) && !(await visible('genBtn')), 'Plan tab: quote, weight and planner only');
await page.click('[data-tabbtn=check]');
check(await visible('stats') && await visible('diags') && await visible('sheetBtn') && !(await visible('quote')), 'Check tab: summary, problems, setup sheet');
check(await page.getAttribute('[data-tabbtn=check]', 'aria-selected') === 'true', 'active tab is marked');
await page.evaluate(() => showTab('plan')); await page.click('[data-tabbtn=make]');
await page.click('#genClearBtn'); await page.fill('#genHoles', '10, 10\n30, 10');
await page.selectOption('#stockSelect', 'alloy_steel'); await page.selectOption('#genHole', 'D9');
await page.click('#genBtn');
check(await page.evaluate(() => document.body.dataset.tab) === 'make' && /No problems found/.test(await page.textContent('#genResult')),
  'generating stays on Make and summarises the checks');
await page.click('#genToCheck');
check(await page.evaluate(() => document.body.dataset.tab) === 'check', '"See the full checks" opens the Check tab');

check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
await browser.close();
console.log(failures ? `\n${failures} check(s) failed` : '\nall checks passed');
process.exit(failures ? 1 : 0);

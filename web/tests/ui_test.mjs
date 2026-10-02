// End-to-end test of the built page (web/dist/gcode-sim-web.html) in a real
// headless Chromium. For every example it checks that the numbers the page
// shows come from the same engine as the native CLI: the WASM analysis must
// match `gcode-sim stats --format json` and `gcode-sim lint --format json`.
//
//   cd web/tests && npm install && node ui_test.mjs
// Needs the native CLI at ../../build/gcode-sim (override with GCODESIM_CLI).
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
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
check((await page.locator('#rules dt').count()) === 9, 'page lists nine lint rules');

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

// a broken machine config shows an error instead of crashing
await page.evaluate(() => { document.querySelector('details').open = true; });
await page.fill('#machineJson', '{ "max_rate_mm_min": 5 }');
await page.click('#runBtn');
check(/Machine config/.test(await page.textContent('#errorBanner')), 'bad machine.json shows an error banner');

// playback moves time forward
await page.click('[data-key=contour]');
await page.evaluate(() => { play.t = 0; updateTime(); });
await page.click('#playBtn');
await page.waitForTimeout(500);
const t = await page.evaluate(() => play.t);
check(t > 0.5, `playback advances (t = ${t.toFixed(2)} s after 0.5 s at 20x)`);

check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
await browser.close();
console.log(failures ? `\n${failures} check(s) failed` : '\nall checks passed');
process.exit(failures ? 1 : 0);

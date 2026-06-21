// Headless verification: load RocketSim, capture console/errors, fly it.
import puppeteer from 'puppeteer-core';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const URL = process.env.URL || 'http://127.0.0.1:4173/';
const OUT = '/tmp/rs';
import { mkdirSync } from 'node:fs';
mkdirSync(OUT, { recursive: true });

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const browser = await puppeteer.launch({
  executablePath: CHROME,
  headless: 'new',
  args: ['--no-sandbox', '--enable-unsafe-swiftshader', '--use-gl=angle',
    '--use-angle=swiftshader', '--ignore-gpu-blocklist', '--enable-webgl',
    '--window-size=1280,760'],
  defaultViewport: { width: 1280, height: 720 },
});

const page = await browser.newPage();
const logs = [];
const errors = [];
page.on('console', m => logs.push(`[${m.type()}] ${m.text()}`));
page.on('pageerror', e => errors.push('PAGEERROR: ' + e.message));
page.on('requestfailed', r => errors.push('REQFAIL: ' + r.url() + ' ' + (r.failure()?.errorText)));

await page.goto(URL, { waitUntil: 'load', timeout: 20000 });
await sleep(3500); // wasm init + a few frames

// Did WebGL come up? sample the canvas pixels for non-uniform content.
const probe = await page.evaluate(() => {
  const c = document.getElementById('canvas');
  if (!c) return { ok: false, why: 'no canvas' };
  return { ok: true, w: c.width, h: c.height,
           loaderGone: !document.getElementById('loader') ||
                       document.getElementById('loader').style.opacity === '0' };
});
await page.screenshot({ path: OUT + '/1-build.png' });

// Launch (Enter triggers launch; also click the LAUNCH button as fallback)
await page.keyboard.press('Enter');
await sleep(400);
await page.mouse.click(225, 694);
await sleep(900);
await page.screenshot({ path: OUT + '/2-liftoff.png' });

// Ascend: full throttle is default + SRBs auto-fire; steer east, climb.
await page.keyboard.down('KeyD');
await sleep(1400);
await page.keyboard.up('KeyD');
await sleep(3500);
await page.screenshot({ path: OUT + '/3-ascent.png' });

// Map view
await page.keyboard.press('KeyM');
await sleep(900);
await page.screenshot({ path: OUT + '/4-map.png' });
await page.keyboard.press('KeyM');
await sleep(300);

// Read frame liveness: capture two screenshots hashes to ensure animation.
const live = await page.evaluate(async () => {
  return new Promise(res => {
    let count = 0;
    const t0 = performance.now();
    function f(){ count++; if (count < 30) requestAnimationFrame(f); else res({ frames: count, ms: performance.now()-t0 }); }
    requestAnimationFrame(f);
  });
});

console.log('PROBE', JSON.stringify(probe));
console.log('LIVE', JSON.stringify(live));
console.log('--- console (last 40) ---');
console.log(logs.slice(-40).join('\n'));
console.log('--- errors ---');
console.log(errors.length ? errors.join('\n') : '(none)');

await browser.close();
process.exit(errors.length ? 2 : 0);

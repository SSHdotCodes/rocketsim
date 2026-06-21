// Verify the full descent path: launch a hop, cut thrust, fall back, deploy the
// parachute, and confirm a safe LANDED (not EXPLODED) touchdown.
import puppeteer from 'puppeteer-core';
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new',
  args: ['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader','--ignore-gpu-blocklist','--enable-webgl','--mute-audio'],
  defaultViewport: { width: 1280, height: 720 } });
const page = await browser.newPage();
const errors = []; page.on('pageerror', e => errors.push('' + e.message));
await page.goto(process.env.URL || 'http://127.0.0.1:4173/', { waitUntil: 'load' });
await sleep(2500);
const tel = () => page.evaluate(() => window.__rs || { mode: 0 });
const key = (k) => page.keyboard.press(k);

await page.mouse.click(225, 694);            // launch
await sleep(300);
let cut = false, chute = false, log = [], lastLog = 0, peakAlt = 0;
const deadline = Date.now() + 200000;
while (Date.now() < deadline) {
  const s = await tel();
  if (s.mode !== 1) { await sleep(100); continue; }
  peakAlt = Math.max(peakAlt, s.alt);
  const now = Date.now();
  if (now - lastLog > 1500) {
    log.push(`alt=${(s.alt/1000).toFixed(1)}km vspd=${s.vspd|0} spd=${s.spd|0} warp=${s.warp} chute=${chute} fuel=${s.fuel|0} ${s.landed?'LANDED':''}${s.exploded?'EXPLODED':''}`);
    lastLog = now;
  }
  if (s.landed && s.alt < 2000 && peakAlt > 8000) { log.push(`>> SAFE LANDING at ${(s.spd).toFixed(1)} m/s, peak ${(peakAlt/1000)|0}km`); break; }
  if (s.exploded) { log.push(`>> CRASHED (peak ${(peakAlt/1000)|0}km)`); break; }

  if (!cut && s.alt > 4000) { await key('KeyX'); await sleep(120); await key('Space'); cut = true; log.push('  cut throttle + dropped lower stage'); }
  // deploy parachute once we're falling back into the atmosphere
  if (!chute && s.vspd < 0 && s.alt < 60000) { await key('KeyP'); chute = true; log.push('  parachute deployed'); }
  // time-warp the slow parts; drop to realtime for the final touchdown
  if (s.alt < 2500) { if (s.warp > 1) await key('Comma'); }
  else if (s.warp < 100) await key('Period');
  else if (s.warp > 100) await key('Comma');
  await sleep(110);
}
const fin = await tel();
console.log(log.join('\n'));
console.log('FINAL', JSON.stringify({ landed: !!fin.landed, exploded: !!fin.exploded, spd: +(fin.spd||0).toFixed(1), alt: (fin.alt|0), peak_km: (peakAlt/1000)|0 }));
console.log('errors:', errors.length ? errors.join('; ') : '(none)');
await page.screenshot({ path: '/tmp/rs/land.png' });
const ok = fin.landed && !fin.exploded;
console.log('LAND_OK:', ok);
await browser.close();
process.exit(ok ? 0 : 4);

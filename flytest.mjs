// Closed-loop autopilot test: fly the stock rocket to a stable orbit, reading
// live telemetry from window.__rs. Proves the flight model + staging + warp work
// end to end. Logs the profile and the final orbit.
import puppeteer from 'puppeteer-core';
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const URL = process.env.URL || 'http://127.0.0.1:4173/';
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const browser = await puppeteer.launch({
  executablePath: CHROME, headless: 'new',
  args: ['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader',
    '--ignore-gpu-blocklist','--enable-webgl','--mute-audio'],
  defaultViewport: { width: 1280, height: 720 },
});
const page = await browser.newPage();
const errors = [];
page.on('pageerror', e => errors.push('PAGEERR ' + e.message));
await page.goto(URL, { waitUntil: 'load' });
await sleep(2500);

const tel = () => page.evaluate(() => window.__rs || { mode: 0 });
const key = (k) => page.keyboard.press(k);
const down = (k) => page.keyboard.down(k);
const up = (k) => page.keyboard.up(k);

// launch
await page.mouse.click(225, 694);
await sleep(300);
await key('KeyZ'); // throttle max
let t0 = Date.now(), held = null, turnKey = null;
let phase = "LIFT", log = [], lastLog = 0, lastStageT = 0, lastSpd = 0, pitchKick0 = null, circSet = false;

async function setHeld(want) { if (want !== held) { if (held) await up(held); if (want) await down(want); held = want; } }
async function release() { await setHeld(null); }

// turnKey = the key that increases pitch-from-up (rotates nose toward the horizon,
// on the same physical side as the gravity-turn kick). Using ONE consistent key to
// turn toward the horizon avoids the east/west ambiguity of |pitch-from-up|.
function backKey() { return turnKey === 'KeyD' ? 'KeyA' : 'KeyD'; }
async function steerPitch(target, cur) {
  if (!turnKey) { await release(); return; }
  const e = target - cur;
  await setHeld(e > 2.5 ? turnKey : (e < -2.5 ? backKey() : null));
}
function targetPitch(alt) {
  if (alt < 1800) return 0;
  return Math.min(88, 90 * Math.pow(Math.min(1, (alt - 1800) / 40000), 0.6));
}
async function setWarp(target) { let g, n=0; while ((g = (await tel()).warp) !== target && n++ < 12) {
  await key(g < target ? 'Period' : 'Comma'); await sleep(70); } }

const deadline = Date.now() + 185000;
while (Date.now() < deadline) {
  const s = await tel();
  if (s.mode !== 1) { await sleep(100); continue; }
  if (s.exploded) { log.push('EXPLODED at alt ' + (s.alt | 0)); break; }
  const now = Date.now();
  if (now - lastLog > 1600) {
    log.push(`t=${((now-t0)/1000)|0}s alt=${(s.alt/1000).toFixed(1)}km spd=${s.spd|0} vspd=${s.vspd|0} ap=${(s.apo/1000).toFixed(1)}km pe=${(s.peri/1000).toFixed(1)}km pErr=${s.progradeErr|0} st=${s.stage}/${s.nstage} fuel=${s.fuel|0} warp=${s.warp} [${phase}]`);
    lastLog = now;
  }

  // universal staging: if we're burning but not accelerating, the active stage is dry
  const accel = s.spd - lastSpd; lastSpd = s.spd;
  const burning = (phase === 'ASCENT' || phase === 'CIRC');
  if (burning && accel < 0.5 && s.stage < s.nstage && now - lastStageT > 1800) {
    await key('Space'); lastStageT = now; log.push(`  >> STAGE -> ${s.stage+1} at alt ${(s.alt/1000).toFixed(1)}km`);
  }

  if (phase === 'LIFT') {
    if (s.alt > 2000) { phase = 'KICK'; pitchKick0 = s.pitch; log.push('  >> 2km, gravity-turn kick'); }
  } else if (phase === 'KICK') {
    await setHeld('KeyD');
    if (s.pitch > 9) { turnKey = (s.pitch > pitchKick0) ? 'KeyD' : 'KeyA'; await release(); phase = 'ASCENT'; log.push(`  >> turn started, turnKey=${turnKey}`); }
  } else if (phase === 'ASCENT') {
    if (s.apo > 78000) { await release(); await key('KeyX'); phase = 'COAST'; log.push(`  >> cutoff apo ${(s.apo/1000)|0}km`); continue; }
    await steerPitch(targetPitch(s.alt), s.pitch);
  } else if (phase === 'COAST') {
    await steerPitch(90, s.pitch);
    if (s.vspd < 14) { await setWarp(1); phase = 'CIRC'; log.push(`  >> circularizing at ${(s.alt/1000)|0}km`); continue; }
    if (s.warp === 1 && s.vspd > 110 && s.alt > 73000) await key('Period');
    else if (s.warp > 1 && s.vspd < 40) await key('Comma');
    else if (s.warp < 5 && s.vspd > 160) await key('Period');
  } else if (phase === 'CIRC') {
    if (s.peri > 72000) { await release(); await key('KeyX'); phase = 'DONE'; log.push('  >> ORBIT ACHIEVED'); break; }
    if (s.fuel < 20 && s.stage >= s.nstage) { phase = 'OOF'; log.push('  >> out of fuel'); break; }
    await key('KeyZ');
    await steerPitch(90, s.pitch);
  }
  await sleep(110);
}

const fin = await tel();
console.log(log.join('\n'));
console.log('\n=== FINAL ===');
console.log(JSON.stringify({ phase, alt_km: +(fin.alt/1000).toFixed(1), apo_km: +(fin.apo/1000).toFixed(1),
  peri_km: +(fin.peri/1000).toFixed(1), spd: fin.spd|0, body: fin.body, exploded: !!fin.exploded }, null, 0));
console.log('errors:', errors.length ? errors.join('; ') : '(none)');
await page.screenshot({ path: '/tmp/rs/orbit.png' });

// verdict
const orbited = fin.peri > 71000 && fin.apo > 71000 && !fin.exploded;
console.log('ORBIT_OK:', orbited);
await browser.close();
process.exit(orbited ? 0 : 3);

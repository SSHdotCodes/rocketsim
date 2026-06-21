import puppeteer from 'puppeteer-core';
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new',
  args: ['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader','--ignore-gpu-blocklist','--enable-webgl'],
  defaultViewport: { width: 1280, height: 720 } });
const page = await browser.newPage();
const errors = []; page.on('pageerror', e => errors.push('' + e.message));
await page.goto(process.env.URL || 'http://127.0.0.1:4173/', { waitUntil: 'load' });
await sleep(2500);
// fresh economy
await page.evaluate(() => { try { localStorage.clear(); } catch(e){} });
await page.reload({ waitUntil: 'load' }); await sleep(2500);
await page.mouse.click(770, 421).catch(()=>{});      // dismiss help
await sleep(300);
await page.screenshot({ path: '/tmp/rs/e1-build.png' });
await page.mouse.click(346, 694); await sleep(400);   // MARKETPLACE
await page.screenshot({ path: '/tmp/rs/e2-market.png' });
// buy first part (top BUY button ~ ox+pw-80,y) ox=360,pw=560 -> ~ (842, 423)
await page.mouse.click(842, 423); await sleep(300);
await page.screenshot({ path: '/tmp/rs/e2b-bought.png' });
await page.mouse.click(800, 626); await sleep(300);   // CLOSE market (ox+pw-70, oy+ph-28)
await page.mouse.click(466, 694); await sleep(400);   // STAGING
await page.screenshot({ path: '/tmp/rs/e3-staging.png' });
await page.mouse.click(760, 614); await sleep(300);   // CLOSE staging
// launch + flight engine controls
await page.mouse.click(218, 694); await sleep(300);
await page.keyboard.down('KeyD'); await sleep(1200); await page.keyboard.up('KeyD');
await sleep(2500);
await page.screenshot({ path: '/tmp/rs/e4-flight.png' });
// drag throttle slider to ~60%
await page.mouse.move(14, 80); await page.mouse.down(); await page.mouse.move(105, 80); await page.mouse.up();
await sleep(200);
// toggle engines off
await page.mouse.click(225, 80); await sleep(300);
await page.screenshot({ path: '/tmp/rs/e5-engines-off.png' });
console.log('errors:', errors.length ? errors.join('; ') : '(none)');
await browser.close();

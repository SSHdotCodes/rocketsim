// Quick editor check: select a part, place two, reset to stock. Screenshots only.
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
// dismiss help (GOT IT button is centered)
await page.mouse.click(640 + 200, 360 + 70).catch(()=>{});
await sleep(300);
await page.mouse.click(74, 148);    // select "Fuel Tank L" in palette
await sleep(200);
await page.mouse.click(640, 360);   // place near center
await sleep(200);
await page.mouse.click(640, 300);   // stack another
await sleep(300);
await page.screenshot({ path: '/tmp/rs/editor.png' });
console.log('errors:', errors.length ? errors.join('; ') : '(none)');
await browser.close();

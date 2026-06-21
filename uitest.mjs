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
await page.mouse.click(770, 421).catch(()=>{});      // dismiss help
await sleep(300);
// builder: select Fuel Tank L, hover above the stack (ghost), then place (snap)
await page.mouse.click(74, 148);
await page.mouse.move(640, 150);
await sleep(250);
await page.screenshot({ path: '/tmp/rs/u1-build-ghost.png' });
await page.mouse.click(640, 150);                    // should SNAP on top, not reject
await sleep(250);
await page.screenshot({ path: '/tmp/rs/u2-build-stacked.png' });
// reset + launch
await page.mouse.click(575, 694);                    // STOCK SHIP button
await sleep(200);
await page.keyboard.press('Enter');                  // launch
await sleep(200);
await page.mouse.click(225, 694);
await sleep(300);
await page.keyboard.down('KeyD'); await sleep(1400); await page.keyboard.up('KeyD');
await sleep(3500);
await page.screenshot({ path: '/tmp/rs/u3-flight.png' });   // HUD buttons + minimap
// open map and zoom out to reveal Luna
await page.keyboard.press('KeyM');
await sleep(600);
for (let i=0;i<26;i++){ await page.mouse.wheel({ deltaY: 120 }); await sleep(40); }
await sleep(400);
await page.screenshot({ path: '/tmp/rs/u4-map-zoomed.png' });
// click Luna to plan a transfer (Luna sits on its orbit ring, upper-right)
await page.mouse.click(876, 270);
await sleep(700);
await page.screenshot({ path: '/tmp/rs/u5-navigate.png' });
console.log('errors:', errors.length ? errors.join('; ') : '(none)');
await browser.close();

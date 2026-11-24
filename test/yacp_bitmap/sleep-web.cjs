// Browser acceptance against the synthetic simulator started by verify_yacp_sleep_web.py.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');

(async () => {
  const output = path.resolve(process.argv[2]);
  const { url, dimensions = [480,800] } = JSON.parse(fs.readFileSync(path.join(output, 'ready.json')));
  const browser = await chromium.launch({headless:true, channel:process.env.BROWSER_CHANNEL || 'chrome'});
  const page = await browser.newPage({viewport:{width:1100,height:1100},locale:'en-US'});
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  async function ready() { await page.waitForFunction(() => !document.getElementById('controls').disabled); }
  async function click(id) { await page.locator('#'+id).click(); await ready(); }
  async function pixels(id) {
    return page.locator('#'+id).evaluate(canvas => {
      const values = canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
      let hash = 2166136261;
      for (const value of values) hash = Math.imul(hash ^ value,16777619) >>> 0;
      return [canvas.width,canvas.height,hash];
    });
  }
  try {
    // The phone opens the portal root from the reader's QR code.
    const phone = await browser.newPage({viewport:{width:375,height:850},locale:'en-US'});
    await phone.goto(url);
    await phone.locator('.mobile-nav summary').click();
    await phone.getByRole('link',{name:'Sleep images',exact:true}).click();
    assert.equal(new URL(phone.url()).pathname,'/sleep-images');
    await phone.close();

    await page.goto(url);
    await page.getByRole('link',{name:'Sleep images',exact:true}).click();
    assert.equal(new URL(page.url()).pathname,'/sleep-images');
    await ready();
    assert.match(await page.locator('#notice').textContent(), /Gallery ready/);
    assert.equal(await page.locator('#current').isDisabled(),true);
    assert((await page.locator('#dimensions').textContent()).startsWith(dimensions.join(' x ')));
    const png = await page.evaluate(() => {
      const c = document.createElement('canvas'); c.width=1000; c.height=900;
      const ctx=c.getContext('2d'), g=ctx.createLinearGradient(0,0,1000,900);
      g.addColorStop(0,'#003366'); g.addColorStop(.5,'#ccbb99'); g.addColorStop(1,'#fff');
      ctx.fillStyle=g; ctx.fillRect(0,0,1000,900); ctx.fillStyle='#111'; ctx.fillRect(130,100,250,600);
      return c.toDataURL('image/png').split(',')[1];
    });
    await page.locator('#files').setInputFiles([
      {name:'Landscape.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')},
      {name:'Second & image.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')}
    ]); await ready();
    assert.equal(await page.locator('#queue option').count(),2);
    const before = await pixels('preview');
    const initialCrop = await page.evaluate(() => ({...queue[selected],file:undefined}));
    assert.equal(await page.locator('#fit').inputValue(),'crop');
    await page.locator('#zoom').fill('65');
    await page.locator('#zoom').dispatchEvent('input');
    await click('right'); await click('up');
    const crop = page.locator('#crop'), box = await crop.boundingBox();
    await page.mouse.move(box.x+box.width/2,box.y+box.height/2); await page.mouse.down();
    await page.mouse.move(box.x+box.width*.7,box.y+box.height*.6); await page.mouse.up();
    await page.locator('#gray0').fill('75'); await page.locator('#gray0').dispatchEvent('input');
    const edited = await pixels('preview'); assert.notEqual(edited[2],before[2]);
    await page.locator('#queue').selectOption('1'); await ready();
    assert.equal(await page.locator('#zoom').inputValue(),'100');
    await page.locator('#queue').selectOption('0'); await ready();
    assert.equal(await page.locator('#zoom').inputValue(),'65');
    assert.deepEqual(await pixels('preview'),edited);
    await click('upload'); assert.match(await page.locator('#notice').textContent(),/Uploaded Landscape.bmp to \/.sleep/);
    await click('upload'); assert.match(await page.locator('#notice').textContent(),/Uploaded Landscape-1.bmp/);
    const rows = page.locator('#images li'); assert.equal(await rows.count(),2);
    await rows.first().getByRole('button',{name:'Preview',exact:true}).click(); await ready();
    assert.deepEqual(await pixels('existingPreview'),edited);
    let cancelled = false;
    page.once('dialog', async dialog => { cancelled = true; await dialog.dismiss(); });
    await rows.first().getByRole('button',{name:'Delete',exact:true}).click(); await ready();
    assert(cancelled); assert.equal(await rows.count(),2);
    await click('browse');
    await page.getByRole('button',{name:'Folder: Books',exact:true}).click(); await ready();
    await page.getByRole('button',{name:'Other.txt',exact:true}).click(); await ready();
    assert.match(await page.locator('#destination').textContent(),/^\/.book-galleries\/[0-9a-f]{16}-Other$/);
    assert.equal(await page.locator('#queue option').count(),0);
    await page.locator('#files').setInputFiles({name:'Landscape.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')}); await ready();
    await click('upload'); assert.match(await page.locator('#notice').textContent(),/Uploaded Landscape.bmp to \/.book-galleries\//);
    assert.equal(await page.locator('#images li').count(),1);
    await click('reset');
    fs.writeFileSync(path.join(output,'reset-trace.json'),JSON.stringify({before, after:await pixels('preview'),
      initialCrop, finalCrop:await page.evaluate(() => ({...queue[selected],file:undefined}))},null,2));
    assert.deepEqual(await pixels('preview'),before);
    await page.screenshot({path:path.join(output,'web-desktop.png'),fullPage:true});
    await page.setViewportSize({width:375,height:850});
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.screenshot({path:path.join(output,'web-mobile.png'),fullPage:true});
    await page.emulateMedia({colorScheme:'dark'});
    await page.screenshot({path:path.join(output,'web-mobile-dark.png'),fullPage:true});
    await click('clear'); assert.equal(await page.locator('#editor').isHidden(),true);
    assert.deepEqual(errors,[]);
    const result = {passed:true,multiple_images:true,crop_and_thresholds:true,per_image_settings:true,
      duplicate_preserved:true,uploaded_bmp_preview_identical:true,book_gallery:true,delete_cancelled:true,
      portal_navigation:true,mobile_overflow:false,hardware_validated:false};
    fs.writeFileSync(path.join(output,'browser-result.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify(result));
  } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode=1;});

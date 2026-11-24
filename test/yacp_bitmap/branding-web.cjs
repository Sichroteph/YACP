// Check the actual firmware-served logo and portal pages on the synthetic simulator.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');

(async () => {
  const output = path.resolve(process.argv[2]);
  const { url } = JSON.parse(fs.readFileSync(path.join(output,'ready.json')));
  const browser = await chromium.launch({headless:true, channel:process.env.BROWSER_CHANNEL || 'chrome'});
  const page = await browser.newPage({viewport:{width:1000,height:900},locale:'en-US'});
  const errors=[];
  page.on('pageerror', error=>errors.push(error.message));
  try {
    await page.goto(url);
    const logo = await page.request.get(url+'/logo.svg');
    assert.equal(logo.status(),200);
    assert.match(logo.headers()['content-type'],/^image\/svg\+xml/);
    assert.equal(await logo.text(),fs.readFileSync('assets/brand/yacp-logo.svg','utf8'));
    const cached=await page.request.get(url+'/logo.svg',{headers:{'If-None-Match':logo.headers()['etag']}});
    assert.equal(cached.status(),304);
    const reports=[];
    for (const route of ['/','/files','/settings','/fonts','/sleep-images']) {
      await page.setViewportSize({width:1000,height:900});
      await page.goto(url+route);
      const image=page.locator('h1 img.brand-logo');
      await image.waitFor({state:'visible'});
      await page.waitForFunction(()=>document.querySelector('h1 img').complete && document.querySelector('h1 img').naturalWidth > 0);
      assert.match(await page.title(),/YACP/);
      assert.match(await page.locator('h1').textContent(),/YACP/);
      assert.match(await page.locator('body').textContent(),/Based on CrossPoint 1.6.5/);
      assert.equal(await page.locator('a[href="/sleep-images"]').count(),1);
      await page.setViewportSize({width:375,height:850});
      const overflow=await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth);
      reports.push({route,overflow});
      await page.screenshot({path:path.join(output,'brand-'+(route.slice(1)||'home')+'.png'),fullPage:true});
    }
    fs.writeFileSync(path.join(output,'brand-pages.json'),JSON.stringify(reports,null,2));
    assert(reports.every(item=>!item.overflow),JSON.stringify(reports));
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'branding-result.json'),JSON.stringify({passed:true,exact_canonical_svg:true,
      etag_304:true,pages:reports.length,attribution_retained:true,hardware_validated:false},null,2));
    console.log('Five branded pages and exact canonical SVG/ETag passed.');
  } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});

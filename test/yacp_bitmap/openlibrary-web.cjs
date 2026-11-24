// Run the shipped page in Chromium with deterministic reader/provider responses.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');

(async () => {
  const root = path.resolve(__dirname, '../..');
  const output = path.resolve(process.argv[2]);
  fs.mkdirSync(output, {recursive:true});
  const browser = await chromium.launch({headless:true, channel:process.env.BROWSER_CHANNEL || 'chrome'});
  try {
    for (const [width,height] of [[480,800], [528,792]]) {
      const page = await browser.newPage({viewport:{width:1100,height:1000},locale:'en-US'});
      const errors = [], stored = new Map(), transfers = [], requests = [];
      let unavailable = true, rejectUpload = true, empty = false, brokenOnly = false;
      page.on('pageerror', error => errors.push(error.message));
      const image = Buffer.from(await page.evaluate(() => {
        const c = document.createElement('canvas'); c.width=300; c.height=200;
        const ctx=c.getContext('2d'); ctx.fillStyle='#000'; ctx.fillRect(0,0,300,200);
        return c.toDataURL('image/png').split(',')[1];
      }), 'base64');
      const gallery = '/.book-galleries/123-fixture';
      await page.route('**/*', async route => {
        const request = route.request(), url = new URL(request.url());
        const json = data => route.fulfill({json:data,headers:{'Access-Control-Allow-Origin':'*'}});
        if (url.hostname === 'openlibrary.org') {
          requests.push(url);
          if (url.pathname === '/search.json') return json({docs:brokenOnly ? [{key:'/works/OL3W',title:'Broken cover',cover_i:303}] : empty ? [] : [
            {key:'/works/OL1W',title:'Fixture book',author_name:['Fixture author'],cover_i:101},
            {key:'/works/OL2W',title:'Missing cover',author_name:[]},
            {key:'/works/OL3W',title:'Broken cover',cover_i:303},
            {key:'/works/OL4W',title:'Invalid image',cover_i:404},
            {key:'/works/OL5W',title:'Empty placeholder',cover_i:505},
            {key:'https://untrusted.example',title:'Ignored',cover_i:999}
          ]});
          return json({size:2,entries:[{title:'Another edition',covers:[101,202,-1,202]}]});
        }
        if (url.hostname === 'covers.openlibrary.org') {
          if (url.pathname.includes('303-M')) return route.fulfill({status:404});
          if (url.pathname.includes('404-M')) return route.fulfill({contentType:'image/jpeg',body:'Invalid image bytes'});
          if (url.pathname.includes('505-M')) return route.fulfill({contentType:'image/png',body:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aSu8AAAAASUVORK5CYII=','base64')});
          if (url.pathname.includes('202-L') && unavailable) return route.fulfill({status:503});
          return route.fulfill({contentType:'image/png',body:image,headers:{'Access-Control-Allow-Origin':'*'}});
        }
        if (url.pathname === '/sleep-images') return route.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(root,'src/network/html/SleepImagesPage.html'))});
        if (url.pathname === '/js/sleep-image-prep.js') return route.fulfill({contentType:'text/javascript',body:fs.readFileSync(path.join(root,'src/network/html/js/SleepImagePrep.js'))});
        if (url.pathname === '/api/status') return json({imageWidth:width,imageHeight:height,currentBookPath:'/Books/fixture.epub'});
        if (url.pathname.endsWith('/prepare')) return json(url.pathname.includes('book-gallery') ? {path:gallery,title:'Fixture book',author:'Fixture author'} : {path:'/.sleep'});
        if (url.pathname === '/api/files') return json(Array.from(stored.keys()).map(name=>({name,isDirectory:false})));
        if (url.pathname === '/download') return route.fulfill({contentType:'image/bmp',body:stored.get(url.searchParams.get('path').split('/').pop())});
        if (url.pathname === '/upload') {
          const payload = request.postDataBuffer(), text = payload.toString('latin1');
          const name = text.match(/filename="([^"]+)"/)[1];
          transfers.push(name);
          if (name === 'OpenLibrary-202.bmp' && rejectUpload) { rejectUpload=false; return route.fulfill({status:503,body:'Transfer interrupted'}); }
          const start=text.indexOf('\r\n\r\n')+4, end=text.lastIndexOf('\r\n--');
          assert.equal(url.searchParams.get('path'), gallery);
          stored.set(name,payload.subarray(start,end));
          return json({success:true});
        }
        return route.fulfill({status:404});
      });
      const ready = () => page.waitForFunction(() => !document.getElementById('controls').disabled);
      const click = async id => { await page.locator('#'+id).click(); await ready(); };
      await page.goto('http://yacp-test.local/sleep-images'); await ready();
      assert(await page.locator('#online').isHidden());
      await click('current');
      assert.equal(await page.locator('#searchTitle').inputValue(),'Fixture book');
      assert.equal(await page.locator('#searchAuthor').inputValue(),'Fixture author');
      await click('searchCovers');
      assert.equal(requests[0].searchParams.get('title'),'Fixture book');
      assert.equal(requests[0].searchParams.get('author'),'Fixture author');
      await page.waitForFunction(() => document.querySelectorAll('#covers input').length === 1 && !document.querySelector('#covers input').disabled);
      assert.equal(await page.locator('#covers .cover').count(),1);
      assert.equal(await page.getByText('Missing cover',{exact:true}).count(),0);
      await page.getByRole('button',{name:'Other editions',exact:true}).first().click(); await ready();
      assert.equal(await page.locator('#covers input').count(),2);
      await page.waitForFunction(() => [...document.querySelectorAll('#covers input')].every(input => !input.disabled));
      assert(await page.getByRole('button',{name:'Other editions',exact:true}).first().isDisabled());
      for (const check of await page.locator('#covers input').all()) await check.check();
      await click('prepareCovers');
      assert.equal(await page.locator('#queue option').count(),2);
      assert.equal(await page.locator('#fit').inputValue(),'crop');
      assert(await page.locator('#cropControls').isVisible());
      assert.equal(await page.locator('#preview').evaluate(c => c.getContext('2d').getImageData(0,0,1,1).data[0]),0);
      await page.locator('#fit').selectOption('contain');
      assert(await page.locator('#cropControls').isHidden());
      const colors = await page.locator('#preview').evaluate(c => {
        const ctx=c.getContext('2d'); return [ctx.getImageData(0,0,1,1).data[0],ctx.getImageData(c.width/2,c.height/2,1,1).data[0]];
      });
      assert.deepEqual(colors,[255,0]);
      await page.locator('#fit').selectOption('crop');
      assert(await page.locator('#cropControls').isVisible());
      await page.locator('#zoom').fill('60'); await page.locator('#zoom').dispatchEvent('input');
      await click('reset'); assert.equal(await page.locator('#fit').inputValue(),'crop');
      await page.locator('#queue').selectOption('1'); await ready();
      assert.match(await page.locator('#notice').textContent(),/503/);
      assert(await page.locator('#editor').isVisible());
      await click('uploadAll');
      assert.match(await page.locator('#notice').textContent(),/1 images uploaded. 1 failed/);
      unavailable=false;
      await click('uploadAll');
      assert.match(await page.locator('#notice').textContent(),/0 images uploaded. 1 failed/);
      await click('uploadAll');
      assert.match(await page.locator('#notice').textContent(),/1 images uploaded. 0 failed/);
      assert.equal(transfers.filter(name=>name==='OpenLibrary-101.bmp').length,1);
      assert.equal(stored.size,2);
      await click('uploadAll'); assert.equal(stored.size,2);
      await click('upload'); assert(stored.has('OpenLibrary-202-1.bmp'));
      await page.locator('#gray0').fill('50'); await page.locator('#gray0').dispatchEvent('input');
      await click('uploadAll'); assert(stored.has('OpenLibrary-202-2.bmp'));
      for (const [name,bmp] of stored) {
        assert.equal(bmp.readUInt16LE(0),0x4d42); assert.equal(bmp.readInt32LE(18),width);
        assert.equal(bmp.readInt32LE(22),-height); assert.equal(bmp.readUInt16LE(28),2);
        const offset=bmp.readUInt32LE(10), stride=Math.ceil(width*2/32)*4;
        assert.equal(bmp[offset] >> 6,0); assert.equal(bmp[offset+Math.floor(height/2)*stride+Math.floor(width/2/4)],0);
        fs.writeFileSync(path.join(output,`${width}-${name}`),bmp);
      }
      await page.locator('#images li').first().getByRole('button',{name:'Preview',exact:true}).click(); await ready();
      await page.setViewportSize({width:375,height:850});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
      await page.screenshot({path:path.join(output,`openlibrary-${width}-mobile.png`),fullPage:true});
      brokenOnly=true; await click('searchCovers');
      await page.waitForFunction(() => document.querySelectorAll('#covers .cover').length === 0);
      assert(await page.locator('#prepareCovers').isDisabled());
      brokenOnly=false; empty=true; await click('searchCovers');
      assert.match(await page.locator('#notice').textContent(),/No covers found/);
      assert(await page.locator('#prepareCovers').isDisabled());
      await click('global'); assert(await page.locator('#editor').isHidden());
      assert.equal(await page.locator('#queue option').count(),0);
      // Local import uses the same full-screen crop default.
      await page.locator('#files').setInputFiles({name:'local.png',mimeType:'image/png',buffer:image}); await ready();
      assert.equal(await page.locator('#fit').inputValue(),'crop');
      assert.deepEqual(errors,[]);
      await page.close();
    }
    const result={passed:true,dimensions:[[480,800],[528,792]],multiple_covers:true,edition_deduplication:true,
      crop_default_without_margins:true,optional_contain:true,missing_and_broken_covers_filtered:true,download_and_upload_retries:true,completed_uploads_skipped:true,local_import:true,
      mobile_overflow:false,hardware_validated:false};
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify(result));
  } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});

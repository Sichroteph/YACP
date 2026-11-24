const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

(async () => {
  const [source, output] = process.argv.slice(2);
  const context = vm.createContext({Blob});
  new vm.Script(fs.readFileSync(source, 'utf8')).runInContext(context);
  const prep = context.YacpSleepPrep;
  const thresholds = prep.thresholds([-50, 128, 999]);
  assert.deepEqual(Array.from(thresholds), [0, 128, 255]);
  const cramped = prep.thresholds([254, 254, 1]);
  assert(cramped[1] - cramped[0] >= 4 && cramped[2] - cramped[1] >= 4);
  for (const [sw, sh] of [[2000, 1000], [1000, 2000], [50, 50]]) {
    for (const [width, height] of [[480, 800], [528, 792]]) {
      for (const zoom of [35, 75, 100]) {
        const state = {loaded: true, width: sw, height: sh, zoom};
        const crop = prep.crop(state, width, height);
        assert(Math.abs(crop.w / crop.h - width / height) < 1e-9);
        assert(crop.x >= 0 && crop.y >= 0 && crop.x + crop.w <= sw && crop.y + crop.h <= sh);
        assert(Math.abs(crop.x + crop.w / 2 - sw / 2) < 1e-9);
      }
    }
  }
  const cases = [];
  for (const [width, height] of [[1, 1], [5, 3], [17, 19], [480, 800], [528, 792]]) {
    const pixels = new Uint8Array(width * height * 4);
    for (let i = 0; i < width * height; ++i) {
      pixels[i * 4] = i % 256;
      pixels[i * 4 + 1] = i * 17 % 256;
      pixels[i * 4 + 2] = i * 23 % 256;
      pixels[i * 4 + 3] = i % 255;
    }
    const indexes = prep.indexes({data: pixels}, width, height, [43, 128, 213]);
    assert.equal(indexes.length, width * height);
    assert(indexes.every(index => index < 4));
    const bmp = Buffer.from(await prep.bmp(indexes, width, height).arrayBuffer());
    assert.equal(bmp.readInt32LE(22), -height);
    assert.equal(bmp.readUInt16LE(28), 2);
    const name = `${width}x${height}`;
    fs.writeFileSync(path.join(output, name + '.bmp'), bmp, {flag: 'wx'});
    fs.writeFileSync(path.join(output, name + '.indexes'), indexes, {flag: 'wx'});
    cases.push({name, width, height});
  }
  fs.writeFileSync(path.join(output, 'cases.json'), JSON.stringify(cases), {flag: 'wx'});
  console.log('Crop, thresholds, alpha flattening and BMP preparation passed');
})().catch(error => { console.error(error); process.exitCode = 1; });

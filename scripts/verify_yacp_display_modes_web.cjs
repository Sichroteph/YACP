// Exercise the shipped portal script with lightweight controls, without a server.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../src/network/html/SettingsPage.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
// Skip only the page's startup fetch sequence. Functions remain the real portal code.
const code = script.slice(0, script.lastIndexOf('(async () =>'));
const controls = {
  'setting-displayMode': {value:'0'},
  'setting-refreshFrequency': {value:'0'},
  'setting-textAntiAliasing': {checked:false},
  'mode-hint-refreshFrequency': {},
  'mode-hint-textAntiAliasing': {},
};
const context = vm.createContext({assert, document:{getElementById:id=>controls[id]}});
vm.runInContext(code, context);
vm.runInContext(`
allSettings = [
  {key:'displayMode',type:'enum',value:0},
  {key:'refreshFrequency',type:'enum',value:0,classicValue:6,disabledHint:'Fixed by Magic Flash'},
  {key:'textAntiAliasing',type:'toggle',value:0,classicValue:1,disabledHint:'Unavailable with Magic Flash'}
];
syncDisplayMode(false);
assert.equal(getControl('refreshFrequency').disabled,true);
assert.equal(getControl('textAntiAliasing').disabled,true);
assert.equal(getControl('textAntiAliasing').title,'Unavailable with Magic Flash');
getControl('displayMode').value='1'; syncDisplayMode(true);
assert.equal(getControlValue('refreshFrequency'),6);
assert.equal(getControlValue('textAntiAliasing'),1);
assert.equal(getControl('refreshFrequency').disabled,false);
setControlValue('refreshFrequency',4); setControlValue('textAntiAliasing',0);
getControl('displayMode').value='0'; syncDisplayMode(true);
assert.equal(getControlValue('refreshFrequency'),0);
assert.equal(getControlValue('textAntiAliasing'),0);
assert.equal(findSetting('refreshFrequency').classicValue,4);
getControl('displayMode').value='1'; syncDisplayMode(true);
assert.equal(getControlValue('refreshFrequency'),4);
assert.equal(getControlValue('textAntiAliasing'),0);
assert.equal(document.getElementById('mode-hint-textAntiAliasing').textContent,'');
assert.match(renderControl({key:'example',type:'toggle',disabled:true,value:0}),/disabled/);
assert.match(renderControl({key:'example',type:'enum',disabled:true,value:0,options:['Magic Flash']}),/disabled/);
const unavailable = renderControl({key:'displayMode',type:'enum',value:1,
  options:['Magic Flash','Classic'],disabledOptions:[0]});
assert.match(unavailable, /<option value="0" disabled>Magic Flash/);
assert.match(unavailable, /<option value="1" selected>Classic/);
assert.doesNotMatch(unavailable, /<select[^>]*disabled/);
findSetting('displayMode').magicFlashAvailable=false;
delete findSetting('refreshFrequency').classicValue;
delete findSetting('textAntiAliasing').classicValue;
syncDisplayMode(false);
assert.equal(getControlValue('refreshFrequency'),4);
assert.equal(getControlValue('textAntiAliasing'),0);
allSettings = allSettings.filter(s=>s.key!=='displayMode');
syncDisplayMode(false);
assert.equal(getControlValue('refreshFrequency'),4);
`, context);
console.log(JSON.stringify({passed:true,mode_switching:true,classic_values_restored:true,non_x3_unchanged:true}));

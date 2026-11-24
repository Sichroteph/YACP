const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

(async () => {
  const html = fs.readFileSync('src/network/html/SettingsPage.html', 'utf8');
  const code = [...html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)].map(match => match[1]).join('\n');
  const elements = new Map();
  const toasts = [];
  const context = vm.createContext({
    console, setTimeout() {},
    document: {getElementById(id) {
      if (!elements.has(id)) elements.set(id, {value: '', style: {}, innerHTML: '', disabled: false});
      return elements.get(id);
    }},
    showWebToast(message, isError) { toasts.push({message, isError}); },
    // Hold only initial page loading. No browser or network is used in this unit check.
    fetch: () => new Promise(() => {})
  });
  new vm.Script(code).runInContext(context);
  for (const format of [undefined, 255, 0, 1, 2]) {
    const rendered = context.renderOpdsServer({name: '<QA>', filenameFormat: format}, 0);
    const selected = [...rendered.matchAll(/<option value="(\d+)" selected>/g)];
    assert.equal(selected.length, 1);
    assert.equal(Number(selected[0][1]), format ?? 255);
    assert(rendered.includes('&lt;QA&gt;'));
  }
  const newServer = context.renderOpdsServer({}, -1);
  assert.match(newServer, /<select id="opds-format-new"><option value="255" selected>/);
  const calls = [];
  context.fetch = async (url, options) => {
    if (options?.method === 'POST') calls.push(JSON.parse(options.body));
    return {ok: true, json: async () => []};
  };
  for (const format of [0, 1, 2, 255]) {
    for (const [field, value] of Object.entries({name: 'QA', url: 'https://example.invalid', user: '', pass: '', format: String(format)})) {
      context.document.getElementById('opds-' + field + '-0').value = value;
    }
    await context.saveOpdsServer(0);
    const sent = calls.at(-1);
    assert.equal(sent.filenameFormat, format);
    assert.equal(sent.index, 0);
    assert(!Object.hasOwn(sent, 'password'));
  }
  context.document.getElementById('opds-name-new').value = 'New catalog';
  context.document.getElementById('opds-url-new').value = 'https://example.invalid/catalog';
  context.document.getElementById('opds-format-new').value = '255';
  await context.saveOpdsServer(-1);
  assert.equal(calls.at(-1).filenameFormat, 255);
  assert(!Object.hasOwn(calls.at(-1), 'index'));
  assert.equal(toasts.length, calls.length);
  assert(toasts.every(toast => !toast.isError));
  console.log(JSON.stringify({passed: true, renderedChoices: 5, submittedChoices: calls.length, networkUsed: false}));
})().catch(error => { console.error(error); process.exitCode = 1; });

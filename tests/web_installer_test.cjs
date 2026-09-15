const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
function page() {
  const elements = new Map();
  const el = id => {
    if (!elements.has(id)) elements.set(id, {value:'', textContent:'', disabled:false, addEventListener(){}, setAttribute(){}});
    return elements.get(id);
  };
  const context = vm.createContext({document:{getElementById:el},navigator:{serial:{}},window:{isSecureContext:true},TextEncoder,TextDecoder,URL,Blob,setTimeout,clearTimeout,fetch:async()=>({ok:false})});
  vm.runInContext(fs.readFileSync('web/app.js','utf8'),context);
  return {el, run:code=>vm.runInContext(code,context)};
}
test('USB acknowledgement can arrive across multiple chunks; echoed passwords stay hidden',()=>{
  const p=page(); p.el('save').disabled=true;
  p.run('received("GERTY_RE")'); assert.equal(p.el('save').disabled,true);
  p.run('received("ADY\\nGERTY_CONFIG {password:secret}\\nhello\\n")');
  assert.equal(p.el('save').disabled,false);
  assert.equal(p.el('logs').textContent.includes('secret'),false);
  assert.match(p.el('logs').textContent,/hello/);
});
test('invalid UTF-8 SSID length and localhost are rejected before USB writes',async()=>{
  const p=page();p.el('ssid').value='é'.repeat(17);
  await p.el('settings').onsubmit({preventDefault(){}});
  assert.match(p.el('status').textContent,/1–32 bytes/);
  p.el('ssid').value='Network';p.el('endpoint').value='http://localhost/gerty';
  await p.el('settings').onsubmit({preventDefault(){}});
  assert.match(p.el('status').textContent,/instead of localhost/);
});
test('successful settings write waits for acknowledgement and clears password',async()=>{
  const p=page();p.el('ssid').value='Network';p.el('password').value='password123';p.el('endpoint').value='https://example.com/gerty/pages/id';
  p.run('port = {writable:{getWriter(){return {async write(bytes){received("GERTY_SAVED\\n")},releaseLock(){}}}}}');
  await p.el('settings').onsubmit({preventDefault(){}});
  assert.match(p.el('status').textContent,/Settings saved/);
  assert.equal(p.el('password').value,'');
});
test('NVS failure is surfaced instead of reporting success',async()=>{
  const p=page();p.el('ssid').value='Network';p.el('endpoint').value='https://example.com/gerty';
  p.run('port = {writable:{getWriter(){return {async write(){received("GERTY_ERROR Could not save settings\\n")},releaseLock(){}}}}}');
  await p.el('settings').onsubmit({preventDefault(){}});
  assert.match(p.el('status').textContent,/Could not save/);
});

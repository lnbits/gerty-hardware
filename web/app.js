const $ = (id) => document.getElementById(id);
let port, reader, reading, pending, buffer = '', firmwareRequest = 0;
const supported = 'serial' in navigator && window.isSecureContext;
$('connect').disabled = !supported;
if (!supported) $('compatibility').textContent = 'USB setup needs a supported desktop browser (try Chrome or Edge) over HTTPS or localhost.';
function status(message) { $('status').textContent = message; }
function log(text) { $('logs').textContent = ($('logs').textContent + text).slice(-60000); $('logs').scrollTop = $('logs').scrollHeight; }
async function firmware() {
  const request = ++firmwareRequest;
  $('installer').hidden = true;
  const path = `firmware/${$('device').value}/manifest.json`;
  try {
    const response = await fetch(path, {cache: 'no-store'});
    if (!response.ok) throw new Error('No published build');
    const manifest = await response.json();
    if (request !== firmwareRequest) return;
    if (manifest.new_install_prompt_erase !== true || !manifest.builds?.length ||
        manifest.builds.some(build => !build.parts?.length ||
          build.parts.some(part => part.offset === 0 && part.path === 'firmware.bin'))) {
      $('release').textContent = `Firmware ${manifest.version} uses an older package that can erase settings. Publish a newer release before installing.`;
      return;
    }
    $('release').textContent = `Firmware ${manifest.version}`;
    $('installer').setAttribute('manifest', path);
    $('installer').hidden = !!port || !supported;
  } catch {
    if (request === firmwareRequest) $('release').textContent = 'Firmware is not published yet. The first successful tag build will make installation available.';
  }
}
$('device').addEventListener('change', firmware);
firmware();
function received(text) {
  buffer += text;
  const lines = buffer.split('\n'); buffer = lines.pop().slice(-8192);
  for (const raw of lines) {
    const line = raw.trim();
    // Never display echoed configuration commands (including passwords).
    if (!line.startsWith('GERTY_CONFIG ')) log(raw + '\n');
    if (line === 'GERTY_READY') { $('save').disabled = false; status('Display ready. Enter your settings below.'); }
    if (line === 'GERTY_SAVED' && pending) pending.resolve();
    if (line.startsWith('GERTY_ERROR') && pending) pending.reject(new Error(line));
  }
}
async function send(message) {
  const writer = port.writable.getWriter();
  try { await writer.write(new TextEncoder().encode(message + '\n')); }
  finally { writer.releaseLock(); }
}
async function disconnect() {
  if (reader) await reader.cancel().catch(() => {});
  if (reading) await reading;
}
async function readPort() {
  const decoder = new TextDecoder();
  try {
    reader = port.readable.getReader();
    while (true) { const {value, done} = await reader.read(); if (done) break; received(decoder.decode(value, {stream:true})); }
  } catch { status('USB disconnected. Press RST and reconnect if needed.'); }
  finally {
    reader?.releaseLock(); reader = undefined;
    await port.close().catch(() => {}); port = undefined;
    $('connect').disabled = !supported; $('disconnect').disabled = true; $('save').disabled = true;
    firmware();
  }
}
$('connect').onclick = async () => {
  $('connect').disabled = true;
  try {
    port = await navigator.serial.requestPort();
    await port.open({baudRate:115200});
    buffer = ''; $('disconnect').disabled = false; $('installer').hidden = true;
    status('Connected. Waiting for the display; press RST if it does not respond.');
    reading = readPort();
    await send('GERTY_HELLO');
  } catch (error) { if (port?.readable) await disconnect(); else port = undefined; $('connect').disabled = !supported; status(`Could not connect: ${error.message}`); }
};
$('disconnect').onclick = async () => { await disconnect(); status('Disconnected.'); };
$('show-password').onchange = () => { $('password').type = $('show-password').checked ? 'text' : 'password'; };
$('settings').onsubmit = async (event) => {
  event.preventDefault();
  const settings = {ssid:$('ssid').value, password:$('password').value, endpoint:$('endpoint').value.trim()};
  const size = (value) => new TextEncoder().encode(value).length;
  try {
    if (!size(settings.ssid) || size(settings.ssid) > 32) throw new Error('Network name must be 1–32 bytes.');
    if (settings.password && (size(settings.password) < 8 || size(settings.password) > 63)) throw new Error('Wi-Fi password must be 8–63 bytes, or blank for an open network.');
    const url = new URL(settings.endpoint);
    if (!['http:', 'https:'].includes(url.protocol) || !url.hostname || url.username || url.password || url.hash || /\s/.test(settings.endpoint) || size(settings.endpoint)>1024) throw new Error('Enter a complete HTTP or HTTPS Gerty API URL without spaces, login details or a fragment.');
    if (['localhost','127.0.0.1','[::1]'].includes(url.hostname)) throw new Error('Use the LNbits server’s network address instead of localhost.');
    $('save').disabled = true;
    status('Saving settings…');
    let timer;
    try {
      await new Promise((resolve, reject) => {
        pending = {resolve, reject};
        timer = setTimeout(() => reject(new Error('No save confirmation. Press RST, reconnect and try again.')), 15000);
        send('GERTY_CONFIG ' + JSON.stringify(settings)).catch(reject);
      });
      $('password').value = '';
      status('Settings saved. The display is restarting; check the logs for saved settings loading, Wi-Fi and image results.');
    } finally { clearTimeout(timer); pending = undefined; }
  } catch (error) { status(error.message); }
  finally { $('save').disabled = !port; }
};
$('clear').onclick = () => { $('logs').textContent = ''; };
$('download').onclick = () => {
  const url = URL.createObjectURL(new Blob([$('logs').textContent], {type:'text/plain'}));
  const link = document.createElement('a'); link.href = url; link.download = 'gerty-serial.log'; link.click();
  setTimeout(() => URL.revokeObjectURL(url),1000);
};

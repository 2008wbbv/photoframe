// The upload page and gallery, served from flash.
//
// The browser does the image work: each photo is cover-cropped and resized to
// exactly 800x480 and re-encoded as JPEG before it is sent, along with a small
// thumbnail for the gallery. That keeps the frame's job down to writing bytes to
// the card, and means anything the phone can open is accepted — including the
// HEIC files an iPhone produces, which the ESP32 could not decode itself.

#pragma once

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"PAGE(
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rachel's Frame</title>
<style>
  :root{
    --bg:#12100f; --card:#1d1a19; --line:#332e2c;
    --text:#f5efec; --muted:#a89f9a; --accent:#ff9ab0; --danger:#ff7a7a;
  }
  *{box-sizing:border-box}
  body{margin:0;padding:20px 16px 48px;background:var(--bg);color:var(--text);
    font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
  .wrap{max-width:560px;margin:0 auto}
  h1{font-size:24px;margin:0 0 4px;letter-spacing:-.02em}
  .sub{color:var(--muted);font-size:14px;margin:0 0 24px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;
    padding:16px;margin-bottom:16px}
  h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;
    color:var(--muted);margin:0 0 12px;font-weight:600;
    display:flex;justify-content:space-between;align-items:baseline}
  h2 span{text-transform:none;letter-spacing:0;font-weight:400}
  button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:12px 16px;
    background:var(--accent);color:#2b1119;cursor:pointer}
  button:disabled{opacity:.5;cursor:default}
  button.ghost{background:transparent;color:var(--text);border:1px solid var(--line)}
  button.danger{background:transparent;color:var(--danger);border:1px solid var(--danger)}
  button.danger.armed{background:var(--danger);color:#2b0f0f;border-color:var(--danger)}
  .drop{border:2px dashed var(--line);border-radius:12px;padding:28px 16px;
    text-align:center;color:var(--muted);cursor:pointer}
  .drop.over{border-color:var(--accent);color:var(--text)}
  .row{display:flex;gap:8px;flex-wrap:wrap}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:8px}
  .tile{position:relative;aspect-ratio:5/3;border-radius:8px;overflow:hidden;
    background:#000;cursor:pointer;border:0;padding:0}
  .tile img{width:100%;height:100%;object-fit:cover;display:block}
  .tile.now{outline:2px solid var(--accent);outline-offset:-2px}
  .badge{position:absolute;bottom:0;left:0;right:0;background:var(--accent);
    color:#2b1119;font-size:10px;font-weight:700;padding:2px 0;
    text-transform:uppercase;letter-spacing:.06em}
  .empty{color:var(--muted);font-size:14px;margin:0}
  .opts{display:grid;grid-template-columns:repeat(auto-fit,minmax(72px,1fr));gap:6px}
  .opts button{background:transparent;border:1px solid var(--line);color:var(--muted);
    padding:10px 4px;font-size:14px}
  .opts button.on{background:var(--accent);color:#2b1119;border-color:var(--accent)}
  .stat{display:flex;justify-content:space-between;font-size:14px;
    color:var(--muted);padding:3px 0}
  .stat b{color:var(--text);font-weight:600}
  #log{font-size:13px;color:var(--muted);min-height:20px;margin-top:10px}
  .bar{height:4px;background:var(--line);border-radius:2px;overflow:hidden;
    margin-top:10px;display:none}
  .bar div{height:100%;width:0;background:var(--accent);transition:width .2s}
  /* Lightbox */
  .lb{position:fixed;inset:0;z-index:10;background:rgba(8,6,6,.94);padding:20px;
    display:none;flex-direction:column;align-items:center;justify-content:center;gap:16px}
  .lb.open{display:flex}
  .lb img{max-width:100%;max-height:56vh;border-radius:10px;background:#000}
  select,input[type=password]{font:inherit;padding:11px;border-radius:10px;
    border:1px solid var(--line);background:#141211;color:var(--text)}
  #wifi-log{font-size:13px;color:var(--muted);min-height:20px;margin-top:10px}
  /* Crop editor */
  .crop{position:fixed;inset:0;z-index:20;background:#0b0908;padding:16px;
    display:none;flex-direction:column;justify-content:center;gap:14px}
  .crop.open{display:flex}
  .crop h3{margin:0;font-size:15px;font-weight:600;text-align:center}
  .crop .hint{margin:0;text-align:center;color:var(--muted);font-size:13px}
  #crop-box{position:relative;width:100%;max-width:520px;margin:0 auto;
    aspect-ratio:5/3;overflow:hidden;border-radius:10px;background:#000;
    touch-action:none;cursor:grab}
  #crop-box canvas{position:absolute;top:0;left:0;transform-origin:0 0;
    will-change:transform}
  /* Rule-of-thirds guides, so it is obvious this is a crop frame */
  #crop-box::after{content:"";position:absolute;inset:0;pointer-events:none;
    background:
      linear-gradient(to right,transparent 33.2%,rgba(255,255,255,.28) 33.2%,
        rgba(255,255,255,.28) 33.5%,transparent 33.5%,transparent 66.4%,
        rgba(255,255,255,.28) 66.4%,rgba(255,255,255,.28) 66.7%,transparent 66.7%),
      linear-gradient(to bottom,transparent 33.2%,rgba(255,255,255,.28) 33.2%,
        rgba(255,255,255,.28) 33.5%,transparent 33.5%,transparent 66.4%,
        rgba(255,255,255,.28) 66.4%,rgba(255,255,255,.28) 66.7%,transparent 66.7%);
    box-shadow:inset 0 0 0 2px var(--accent)}
  input[type=range]{width:100%;max-width:520px;margin:0 auto;accent-color:var(--accent)}
  .crop .row{max-width:520px;margin:0 auto;width:100%}
  .crop .row button{flex:1}
</style>

<div class="wrap">
  <h1>Rachel's Frame</h1>
  <p class="sub">Add photos, and they'll start showing up in the shuffle.</p>

  <div class="card" id="wifi-card" hidden>
    <h2>Connect to Wi-Fi</h2>
    <p class="empty" style="margin:0 0 12px">
      The frame is running its own network right now. Put it on your Wi-Fi so it
      can receive photos sent from away.
    </p>
    <select id="wifi-ssid" style="width:100%;margin-bottom:8px"></select>
    <input type="password" id="wifi-pass" placeholder="Wi-Fi password"
           autocomplete="off" style="width:100%;margin-bottom:10px">
    <div class="row">
      <button onclick="join()">Connect</button>
      <button class="ghost" onclick="scan()">Rescan</button>
    </div>
    <div id="wifi-log"></div>
  </div>

  <div class="card">
    <h2>Add photos</h2>
    <div class="drop" id="drop">
      Tap to choose photos<br><small>or drag them here</small>
    </div>
    <input type="file" id="pick" accept="image/*" multiple hidden>
    <div class="bar" id="bar"><div></div></div>
    <div id="log"></div>
  </div>

  <div class="card">
    <h2>Send from anywhere <span id="tg-state"></span></h2>
    <p class="empty" style="margin:0 0 12px">
      Message <b>@BotFather</b> on Telegram, send <b>/newbot</b>, and paste the
      token here. Then message your own bot and allow yourself below.
    </p>
    <input type="password" id="tg-token" placeholder="Bot token"
           autocomplete="off" style="width:100%;margin-bottom:10px">
    <div class="row"><button onclick="saveToken()">Save token</button></div>
    <div id="tg-pending" hidden style="margin-top:14px">
      <p class="empty" style="margin:0 0 8px">
        <b id="tg-who"></b> messaged the bot (id <span id="tg-id"></span>).
      </p>
      <div class="row"><button onclick="allowSender()">Allow them</button></div>
    </div>
    <div id="tg-log"></div>
  </div>

  <div class="card">
    <h2>Dimming <span id="cal-state"></span></h2>
    <p class="empty" style="margin:0 0 12px">
      Recalibrating puts a short wizard on the frame itself: turn the room lights
      on, press the right button, turn them off, press again.
    </p>
    <div class="row"><button class="ghost" onclick="recalibrate()">Recalibrate on the frame</button></div>
  </div>

  <div class="card">
    <h2>Time per photo</h2>
    <div class="opts" id="opts"></div>
  </div>

  <div class="card">
    <h2>Now playing</h2>
    <div class="row">
      <button class="ghost" onclick="nav('prev')">&larr; Back</button>
      <button class="ghost" onclick="nav('next')">Next &rarr;</button>
    </div>
    <div style="margin-top:12px">
      <div class="stat"><span>Network</span><b id="s-net">–</b></div>
      <div class="stat"><span>Ambient light</span><b id="s-lux">–</b></div>
      <div class="stat"><span>Brightness</span><b id="s-bright">–</b></div>
      <div class="stat"><span>Card used</span><b id="s-card">–</b></div>
    </div>
  </div>

  <div class="card">
    <h2>Gallery <span id="s-count"></span></h2>
    <div class="grid" id="grid"></div>
  </div>
</div>

<div class="crop" id="crop">
  <h3 id="crop-title">Crop to fit</h3>
  <p class="hint">Drag to move &middot; pinch or use the slider to zoom</p>
  <div id="crop-box"><canvas id="crop-canvas"></canvas></div>
  <input type="range" id="crop-zoom" min="100" max="400" value="100">
  <div class="row">
    <button onclick="cropUse()">Use this</button>
    <button class="ghost" onclick="cropRotate()">Rotate</button>
    <button class="ghost" onclick="cropReset()">Centre</button>
  </div>
  <div class="row">
    <button class="ghost" onclick="cropSkip()">Skip this one</button>
    <button class="ghost" onclick="cropRest()">Centre the rest</button>
  </div>
</div>

<div class="lb" id="lb">
  <img id="lb-img" alt="">
  <div class="row">
    <button onclick="showOnFrame()">Show on frame</button>
    <button class="danger" id="lb-del" onclick="removeTap()">Remove</button>
    <button class="ghost" onclick="closeLb()">Close</button>
  </div>
</div>

<script>
const LABELS = ["10 sec","30 sec","1 min","5 min","15 min","30 min","1 hour"];
const log = m => document.getElementById('log').textContent = m;
const enc = encodeURIComponent;
let current = '';

// ---- upload, with a crop step ---------------------------------------------
//
// Photos are cropped here rather than on the frame, because the browser already
// has a decoder, a scaler and a touchscreen, and the ESP32 has none of those to
// spare. Auto-centring is a poor default on its own — it decapitates anyone
// standing off-centre — so each photo gets a pan-and-zoom pass first, with an
// escape hatch for when there are twenty of them.

const drop = document.getElementById('drop');
const pick = document.getElementById('pick');
drop.onclick = () => pick.click();
pick.onchange = () => { start([...pick.files]); pick.value = ''; };
drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = e => {
  e.preventDefault(); drop.classList.remove('over');
  start([...e.dataTransfer.files].filter(f => f.type.startsWith('image/')));
};

const WANT = 800 / 480;
const cropOv = document.getElementById('crop');
const cropBox = document.getElementById('crop-box');
const cropCanvas = document.getElementById('crop-canvas');
const cropZoom = document.getElementById('crop-zoom');
const cropTitle = document.getElementById('crop-title');

let queue = [], qi = 0, ready = [], centreRest = false;
let srcBmp = null, rotation = 0;
const view = { scale: 1, base: 1, tx: 0, ty: 0, W: 0, H: 0 };

// Downscale big originals before any of this. A 12 MP photo held as a canvas is
// tens of megabytes, and the output is 800x480 — 2000 px on the long edge is
// still far more detail than the panel can show.
function toSource(bmp, deg){
  const swap = (deg === 90 || deg === 270);
  let w = swap ? bmp.height : bmp.width;
  let h = swap ? bmp.width : bmp.height;

  const cap = 2000, big = Math.max(w, h);
  const k = big > cap ? cap / big : 1;
  w = Math.round(w * k); h = Math.round(h * k);

  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const g = c.getContext('2d');
  g.translate(w / 2, h / 2);
  if (deg) g.rotate(deg * Math.PI / 180);
  const dw = (swap ? h : w), dh = (swap ? w : h);
  g.drawImage(bmp, -dw / 2, -dh / 2, dw, dh);
  return c;
}

function layout(reset){
  view.W = cropBox.clientWidth;
  view.H = cropBox.clientHeight;
  // Smallest scale that still covers the frame, so there is never a bald patch.
  view.base = Math.max(view.W / cropCanvas.width, view.H / cropCanvas.height);
  if (reset){
    view.scale = view.base;
    cropZoom.value = 100;
    view.tx = (view.W - cropCanvas.width * view.scale) / 2;
    view.ty = (view.H - cropCanvas.height * view.scale) / 2;
  }
  apply();
}

function apply(){
  const dw = cropCanvas.width * view.scale, dh = cropCanvas.height * view.scale;
  view.tx = Math.min(0, Math.max(view.W - dw, view.tx));
  view.ty = Math.min(0, Math.max(view.H - dh, view.ty));
  if (dw <= view.W) view.tx = (view.W - dw) / 2;
  if (dh <= view.H) view.ty = (view.H - dh) / 2;
  cropCanvas.style.transform =
    `translate(${view.tx}px, ${view.ty}px) scale(${view.scale})`;
}

// Zoom about a point, so pinching keeps the spot between your fingers put.
function zoomTo(next, px, py){
  next = Math.max(view.base, Math.min(view.base * 4, next));
  const k = next / view.scale;
  view.tx = px - (px - view.tx) * k;
  view.ty = py - (py - view.ty) * k;
  view.scale = next;
  cropZoom.value = Math.round(view.scale / view.base * 100);
  apply();
}

// Pointer Events cover mouse and touch with one path.
const pts = new Map();
let pinchStart = 0, pinchScale = 1;

cropBox.addEventListener('pointerdown', e => {
  cropBox.setPointerCapture(e.pointerId);
  pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (pts.size === 2){
    const [a, b] = [...pts.values()];
    pinchStart = Math.hypot(a.x - b.x, a.y - b.y);
    pinchScale = view.scale;
  }
});

cropBox.addEventListener('pointermove', e => {
  const prev = pts.get(e.pointerId);
  if (!prev) return;
  const now = { x: e.clientX, y: e.clientY };

  if (pts.size === 1){
    view.tx += now.x - prev.x;
    view.ty += now.y - prev.y;
    apply();
  } else if (pts.size === 2){
    pts.set(e.pointerId, now);
    const [a, b] = [...pts.values()];
    const dist = Math.hypot(a.x - b.x, a.y - b.y);
    if (pinchStart > 0){
      const r = cropBox.getBoundingClientRect();
      zoomTo(pinchScale * (dist / pinchStart),
             (a.x + b.x) / 2 - r.left, (a.y + b.y) / 2 - r.top);
    }
    return;
  }
  pts.set(e.pointerId, now);
});

const liftPointer = e => {
  pts.delete(e.pointerId);
  if (pts.size < 2) pinchStart = 0;
};
cropBox.addEventListener('pointerup', liftPointer);
cropBox.addEventListener('pointercancel', liftPointer);

cropZoom.oninput = () =>
  zoomTo(view.base * (cropZoom.value / 100), view.W / 2, view.H / 2);

// Renders whatever is inside the frame to the panel's exact resolution.
function renderVisible(){
  const c = document.createElement('canvas');
  c.width = 800; c.height = 480;
  c.getContext('2d').drawImage(
    cropCanvas,
    -view.tx / view.scale, -view.ty / view.scale,
    view.W / view.scale, view.H / view.scale,
    0, 0, 800, 480);
  return c;
}

// The old behaviour, kept for "centre the rest" and for skipped photos.
function renderCentred(src){
  const have = src.width / src.height;
  let sw, sh, sx, sy;
  if (have > WANT){ sh = src.height; sw = sh * WANT; sx = (src.width - sw) / 2; sy = 0; }
  else            { sw = src.width;  sh = sw / WANT; sx = 0; sy = (src.height - sh) / 2; }

  const c = document.createElement('canvas');
  c.width = 800; c.height = 480;
  c.getContext('2d').drawImage(src, sx, sy, sw, sh, 0, 0, 800, 480);
  return c;
}

async function encode(full){
  const photo = await new Promise(r => full.toBlob(r, 'image/jpeg', 0.88));
  const t = document.createElement('canvas');
  t.width = 240; t.height = 144;
  t.getContext('2d').drawImage(full, 0, 0, 240, 144);
  const thumb = await new Promise(r => t.toBlob(r, 'image/jpeg', 0.7));
  return { photo, thumb };
}

async function start(files){
  if (!files.length) return;
  queue = files; qi = 0; ready = []; centreRest = false;
  next();
}

async function next(){
  if (srcBmp){ if (srcBmp.close) srcBmp.close(); srcBmp = null; }

  if (qi >= queue.length){
    cropOv.classList.remove('open');
    if (ready.length) await upload();
    return;
  }

  log(`Preparing ${qi + 1} of ${queue.length}…`);
  try {
    // from-image honours EXIF orientation, which is why iPhone photos would
    // otherwise land sideways.
    srcBmp = await createImageBitmap(queue[qi], { imageOrientation: 'from-image' });
  } catch (e) {
    console.error(e);
    qi++;
    return next();
  }

  rotation = 0;
  const src = toSource(srcBmp, 0);

  if (centreRest){
    ready.push(await encode(renderCentred(src)));
    qi++;
    return next();
  }

  cropCanvas.width = src.width;
  cropCanvas.height = src.height;
  cropCanvas.getContext('2d').drawImage(src, 0, 0);
  cropTitle.textContent = queue.length > 1
    ? `Crop to fit — ${qi + 1} of ${queue.length}` : 'Crop to fit';
  cropOv.classList.add('open');
  requestAnimationFrame(() => layout(true));
  log('');
}

async function cropUse(){
  ready.push(await encode(renderVisible()));
  qi++;
  next();
}

function cropSkip(){ qi++; next(); }

async function cropRest(){
  centreRest = true;
  ready.push(await encode(renderCentred(cropCanvas)));
  qi++;
  next();
}

function cropReset(){ layout(true); }

function cropRotate(){
  rotation = (rotation + 90) % 360;
  const src = toSource(srcBmp, rotation);
  cropCanvas.width = src.width;
  cropCanvas.height = src.height;
  cropCanvas.getContext('2d').drawImage(src, 0, 0);
  layout(true);
}

function newName(){
  let s = '';
  for (let i = 0; i < 8; i++) s += "0123456789ABCDEF"[Math.random()*16|0];
  return s + '.jpg';
}

async function put(url, name, blob){
  const fd = new FormData();
  fd.append('f', blob, name);
  const r = await fetch(url, { method:'POST', body:fd });
  if (!r.ok) throw new Error(await r.text());
}

async function upload(){
  const bar = document.getElementById('bar');
  const fill = bar.firstElementChild;
  bar.style.display = 'block';
  let done = 0, failed = 0;

  for (const item of ready){
    log(`Uploading ${done + 1} of ${ready.length}…`);
    try {
      const name = newName();
      await put('/api/upload', name, item.photo);
      await put('/api/thumbup', name, item.thumb);
    } catch (e) {
      failed++;
      console.error(e);
    }
    done++;
    fill.style.width = (done / ready.length * 100) + '%';
  }

  bar.style.display = 'none';
  fill.style.width = '0';
  log(failed ? `Added ${done - failed}, ${failed} failed.`
             : `Added ${done} photo${done > 1 ? 's' : ''}.`);
  ready = [];
  refresh();
}

// ---- gallery -------------------------------------------------------------

const lb = document.getElementById('lb');
const lbImg = document.getElementById('lb-img');
const lbDel = document.getElementById('lb-del');
let lbName = '', armed = false;

function disarm(){
  armed = false;
  lbDel.textContent = 'Remove';
  lbDel.classList.remove('armed');
}

function openLb(name){
  lbName = name;
  lbImg.src = '/api/photo?name=' + enc(name);
  disarm();
  lb.classList.add('open');
}

function closeLb(){
  lb.classList.remove('open');
  lbImg.src = '';
}

// Two taps to delete — the first arms it. Cheaper than a dialog and hard to do
// by accident on a phone.
async function removeTap(){
  if (!armed){
    armed = true;
    lbDel.textContent = 'Really remove?';
    lbDel.classList.add('armed');
    return;
  }
  await fetch('/api/delete?name=' + enc(lbName), { method:'POST' });
  closeLb();
  refresh();
}

async function showOnFrame(){
  await fetch('/api/show?name=' + enc(lbName), { method:'POST' });
  closeLb();
  refresh();
}

// Tapping the backdrop closes, tapping the photo or buttons does not.
lb.onclick = e => { if (e.target === lb) closeLb(); };

function drawGrid(names){
  const grid = document.getElementById('grid');
  grid.innerHTML = '';
  if (!names.length){
    grid.innerHTML = '<p class="empty">Nothing yet. Add a few photos above.</p>';
    return;
  }
  for (const n of names){
    const tile = document.createElement('button');
    tile.className = 'tile' + (n === current ? ' now' : '');
    tile.onclick = () => openLb(n);

    const img = document.createElement('img');
    img.loading = 'lazy';
    img.alt = '';
    img.src = '/api/thumb?name=' + enc(n);
    // Photos copied onto the card by hand have no thumbnail — use the real one.
    img.onerror = () => { img.onerror = null; img.src = '/api/photo?name=' + enc(n); };
    tile.appendChild(img);

    if (n === current){
      const b = document.createElement('div');
      b.className = 'badge';
      b.textContent = 'on screen';
      tile.appendChild(b);
    }
    grid.appendChild(tile);
  }
}

// ---- wi-fi setup ---------------------------------------------------------

const wlog = m => document.getElementById('wifi-log').textContent = m;

async function scan(){
  wlog('Scanning…');
  try {
    const nets = await (await fetch('/api/scan')).json();
    const sel = document.getElementById('wifi-ssid');
    sel.innerHTML = '';
    if (!nets.length){ wlog('No networks found. Try again.'); return; }
    for (const n of nets){
      const o = document.createElement('option');
      o.value = n.ssid;
      o.textContent = n.ssid + '  (' + n.rssi + ' dBm)' + (n.open ? ' — open' : '');
      sel.appendChild(o);
    }
    wlog(nets.length + ' network' + (nets.length > 1 ? 's' : '') + ' found.');
  } catch (e) { wlog('Scan failed.'); }
}

async function join(){
  const ssid = document.getElementById('wifi-ssid').value;
  const pass = document.getElementById('wifi-pass').value;
  if (!ssid){ wlog('Pick a network first.'); return; }

  wlog('Connecting…');
  try {
    const body = new URLSearchParams({ ssid, password: pass });
    const r = await fetch('/api/join', { method:'POST', body });
    if (!r.ok) throw new Error();
    // The frame reboots to join, which drops this network out from under us.
    wlog('Saved. The frame is restarting to join "' + ssid + '" — this page '
       + 'will stop responding. Reconnect your phone to your normal Wi-Fi, then '
       + 'hold the right button on the frame for its new address.');
  } catch (e) {
    wlog('Could not save those details.');
  }
}

// ---- telegram + calibration ----------------------------------------------

const tlog = m => document.getElementById('tg-log').textContent = m;

async function saveToken(){
  const token = document.getElementById('tg-token').value.trim();
  tlog('Saving…');
  try {
    const r = await fetch('/api/telegram', {
      method:'POST', body: new URLSearchParams({ token })
    });
    if (!r.ok) { tlog(await r.text()); return; }
    tlog('Saved. The frame is restarting — message your bot, then reload this '
       + 'page and allow yourself.');
  } catch (e) { tlog('Could not save that.'); }
}

async function allowSender(){
  const r = await fetch('/api/telegram/allow', { method:'POST' });
  tlog(r.ok ? 'Allowed. Try sending a photo.' : 'Nobody waiting.');
  refresh();
}

async function recalibrate(){
  await fetch('/api/calibrate', { method:'POST' });
  tlog('');
  alert('Look at the frame — the wizard is on screen now.');
}

// ---- controls + status ---------------------------------------------------

async function nav(dir){ await fetch('/api/'+dir, {method:'POST'}); refresh(); }

async function setInterval_(i){
  await fetch('/api/interval?index='+i, {method:'POST'});
  refresh();
}

function drawOpts(sel){
  const box = document.getElementById('opts');
  box.innerHTML = '';
  LABELS.forEach((label, i) => {
    const b = document.createElement('button');
    b.textContent = label;
    if (i === sel) b.className = 'on';
    b.onclick = () => setInterval_(i);
    box.appendChild(b);
  });
}

async function refresh(){
  try {
    const s = await (await fetch('/api/status')).json();
    current = s.current || '';

    const card = document.getElementById('wifi-card');
    if (s.portal && card.hidden){
      card.hidden = false;
      scan();                 // populate the picker the first time only
    } else if (!s.portal) {
      card.hidden = true;
    }
    document.getElementById('tg-state').textContent =
      !s.tg_on ? 'off' :
      s.tg_allowed ? s.tg_allowed + ' sender' + (s.tg_allowed > 1 ? 's' : '')
                   : 'no senders allowed yet';

    const pend = document.getElementById('tg-pending');
    if (s.tg_pending){
      document.getElementById('tg-who').textContent = s.tg_pending_name || 'Someone';
      document.getElementById('tg-id').textContent = s.tg_pending;
      pend.hidden = false;
    } else {
      pend.hidden = true;
    }

    document.getElementById('cal-state').textContent = s.calibrated
      ? s.lux_bright.toFixed(0) + ' / ' + s.lux_dark.toFixed(1) + ' lux'
      : 'not calibrated';

    document.getElementById('s-net').textContent =
      s.portal ? 'own network' : (s.network || '–') + (s.online ? '' : ' (offline)');
    document.getElementById('s-count').textContent =
      s.count === 1 ? '1 photo' : s.count + ' photos';
    document.getElementById('s-lux').textContent =
      s.lux < 0 ? 'no sensor' : s.lux.toFixed(0) + ' lux';
    document.getElementById('s-bright').textContent =
      s.panel_on ? Math.round(s.brightness/255*100) + '%' : 'asleep';
    document.getElementById('s-card').textContent =
      (s.used_mb|0) + ' / ' + (s.total_mb|0) + ' MB';
    drawOpts(s.interval_index);

    drawGrid(await (await fetch('/api/photos')).json());
  } catch (e) { console.error(e); }
}

refresh();
// Don't repaint the grid out from under someone who has a photo open.
setInterval(() => { if (!lb.classList.contains('open')) refresh(); }, 10000);
</script>
)PAGE";

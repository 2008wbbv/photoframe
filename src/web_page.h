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
</style>

<div class="wrap">
  <h1>Rachel's Frame</h1>
  <p class="sub">Add photos, and they'll start showing up in the shuffle.</p>

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

// ---- upload ---------------------------------------------------------------

const drop = document.getElementById('drop');
const pick = document.getElementById('pick');
drop.onclick = () => pick.click();
pick.onchange = () => { send([...pick.files]); pick.value = ''; };
drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = e => {
  e.preventDefault(); drop.classList.remove('over');
  send([...e.dataTransfer.files].filter(f => f.type.startsWith('image/')));
};

// Cover-crop to exactly 800x480 and make a thumbnail, all client side.
async function prepare(file){
  const bmp = await createImageBitmap(file);
  const want = 800/480, have = bmp.width/bmp.height;
  let sw, sh, sx, sy;
  if (have > want){ sh = bmp.height; sw = sh*want; sx = (bmp.width-sw)/2; sy = 0; }
  else            { sw = bmp.width;  sh = sw/want; sx = 0; sy = (bmp.height-sh)/2; }

  const c = document.createElement('canvas');
  c.width = 800; c.height = 480;
  c.getContext('2d').drawImage(bmp, sx, sy, sw, sh, 0, 0, 800, 480);
  const photo = await new Promise(r => c.toBlob(r, 'image/jpeg', 0.88));

  const t = document.createElement('canvas');
  t.width = 240; t.height = 144;
  t.getContext('2d').drawImage(c, 0, 0, 240, 144);
  const thumb = await new Promise(r => t.toBlob(r, 'image/jpeg', 0.7));

  if (bmp.close) bmp.close();
  return { photo, thumb };
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

async function send(files){
  if (!files.length) return;
  const bar = document.getElementById('bar');
  const fill = bar.firstElementChild;
  bar.style.display = 'block';
  let done = 0, failed = 0;

  for (const f of files){
    log(`Uploading ${done+1} of ${files.length}…`);
    try {
      const name = newName();
      const { photo, thumb } = await prepare(f);
      await put('/api/upload', name, photo);
      await put('/api/thumbup', name, thumb);
    } catch (e) {
      failed++;
      console.error(f.name, e);
    }
    done++;
    fill.style.width = (done/files.length*100) + '%';
  }

  bar.style.display = 'none';
  fill.style.width = '0';
  log(failed ? `Added ${done-failed}, ${failed} failed.`
            : `Added ${done} photo${done>1?'s':''}.`);
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

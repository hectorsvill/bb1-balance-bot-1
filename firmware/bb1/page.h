// page.h - the web page, stored in the firmware as one string.
//
// Plain HTML + CSS + JavaScript, no libraries and nothing loaded from the
// internet, so it works when your phone is on BB1's own network.
// R"PAGE( ... )PAGE" is a C++ "raw string": everything between the markers is
// taken exactly as written, so the HTML needs no escaping.
//
// How it works: every 50 ms the page asks GET /state, adds the pitch to a
// 10-second history, and redraws the chart and the numbers. Buttons send POSTs
// (see web.h for the list).
//
// To change the page: edit it here, recompile, upload. (Tip: copy the part
// between the markers into a .html file to try layout changes in a browser;
// the numbers just won't update.)

#pragma once

const char PAGE_HTML[] = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BB1</title>
<style>
  :root { --bg:#111418; --panel:#1b2027; --text:#e6e9ee; --muted:#8a94a3; --line:#2b323c;
          --pitch:#4fb3ff; --accel:#f2a33a; --ok:#4cc38a; --bad:#ff5c5c; }
  * { box-sizing: border-box; }
  body { margin:0; padding:16px; background:var(--bg); color:var(--text);
         font:15px/1.4 -apple-system, system-ui, sans-serif; }
  h1 { font-size:20px; margin:0 0 4px; }
  h2 { font-size:15px; margin:0 0 10px; color:var(--muted); font-weight:600; }
  .top { display:flex; justify-content:space-between; align-items:baseline; flex-wrap:wrap; gap:8px; margin-bottom:12px; }
  .conn { font-size:13px; color:var(--muted); }
  .conn b { color:var(--ok); }
  .conn.lost b { color:var(--bad); }
  .grid { display:grid; grid-template-columns:repeat(auto-fit, minmax(300px, 1fr)); gap:12px; }
  .panel { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:14px; }
  .big { font-size:44px; font-weight:700; font-variant-numeric:tabular-nums; }
  .big small { font-size:16px; color:var(--muted); font-weight:400; }
  canvas { width:100%; height:220px; display:block; }
  table { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; }
  td { padding:4px 0; border-bottom:1px solid var(--line); }
  td:last-child { text-align:right; }
  .legend span { margin-right:14px; font-size:13px; }
  .legend i { display:inline-block; width:10px; height:3px; vertical-align:middle; margin-right:5px; }
  button, input { font:inherit; border-radius:8px; border:1px solid var(--line); padding:8px 12px; }
  button { background:#2a6df4; color:white; border:none; cursor:pointer; }
  button.secondary { background:#39414d; }
  input { background:#0d1014; color:var(--text); width:100%; }
  .row { display:flex; gap:8px; margin-bottom:8px; align-items:center; }
  .row input { flex:1; }
  .msg { font-size:13px; color:var(--muted); min-height:1.4em; }
</style>
</head>
<body>

<div class="top">
  <h1>BB1 balance bot</h1>
  <div class="conn" id="conn">connecting...</div>
</div>

<div class="grid">

  <div class="panel">
    <h2>Pitch (forward +)</h2>
    <div class="big"><span id="pitch">--</span><small> deg</small></div>
    <canvas id="chart"></canvas>
    <div class="legend">
      <span><i style="background:var(--pitch)"></i>pitch (filtered)</span>
      <span><i style="background:var(--accel)"></i>accel only</span>
      <span style="color:var(--muted)">last 10 s</span>
    </div>
  </div>

  <div class="panel">
    <h2>Live values</h2>
    <table>
      <tr><td>Pitch rate</td><td id="rate">--</td></tr>
      <tr><td>Accel-only pitch</td><td id="accel">--</td></tr>
      <tr><td>Loop work (budget 5000 us)</td><td id="work">--</td></tr>
      <tr><td>Tick</td><td id="tick">--</td></tr>
      <tr><td>Gyro bias x / y / z (deg/s)</td><td id="bias">--</td></tr>
      <tr><td>Filter alpha</td><td id="alphaNow">--</td></tr>
      <tr><td>IMU</td><td id="imu">--</td></tr>
      <tr><td>Uptime</td><td id="uptime">--</td></tr>
    </table>
  </div>

  <div class="panel">
    <h2>Actions</h2>
    <div class="row">
      <button onclick="post('/cal', null, 'calMsg')">Calibrate gyro</button>
      <span class="msg">lay the robot still first</span>
    </div>
    <div class="msg" id="calMsg"></div>
    <div class="row">
      <input id="alpha" type="number" step="0.001" min="0.9" max="0.999" placeholder="alpha, e.g. 0.98">
      <button class="secondary" onclick="post('/alpha', {value: el('alpha').value}, 'alphaMsg')">Set alpha</button>
    </div>
    <div class="msg" id="alphaMsg"></div>
  </div>

  <div class="panel">
    <h2>Network</h2>
    <div class="msg" id="netNow"></div>
    <div class="row"><input id="ssid" placeholder="home Wi-Fi name" autocomplete="off"></div>
    <div class="row"><input id="pass" type="password" placeholder="home Wi-Fi password"></div>
    <div class="row">
      <button onclick="saveWifi()">Save and join</button>
      <button class="secondary" onclick="forgetWifi()">Use own network</button>
    </div>
    <div class="msg" id="netMsg"></div>
  </div>

</div>

<script>
const el = id => document.getElementById(id);

// ---------- talking to the robot ----------

// POST with optional form fields; show the robot's answer in a message line.
async function post(url, fields, msgId) {
  const body = new URLSearchParams(fields || {});
  try {
    const res = await fetch(url, { method: 'POST', body });
    el(msgId).textContent = await res.text();
  } catch (e) {
    el(msgId).textContent = 'no answer from the robot';
  }
}

function saveWifi() {
  if (!el('ssid').value) { el('netMsg').textContent = 'type the network name first'; return; }
  if (!confirm('Save "' + el('ssid').value + '" and restart BB1 to join it?\nThe own network turns off. If joining fails, BB1 falls back to its own network.')) return;
  post('/wifi', { ssid: el('ssid').value, password: el('pass').value }, 'netMsg');
}

function forgetWifi() {
  if (!confirm('Forget the home network and restart on BB1\'s own network?')) return;
  post('/wifi/forget', null, 'netMsg');
}

// ---------- the chart ----------

const HISTORY_MS = 10000;
let history = [];          // { t: browser time in ms, pitch, accel }

function drawChart() {
  const c = el('chart');
  const dpr = window.devicePixelRatio || 1;          // sharp lines on retina screens
  c.width = c.clientWidth * dpr;
  c.height = c.clientHeight * dpr;
  const g = c.getContext('2d');
  g.scale(dpr, dpr);
  const w = c.clientWidth, h = c.clientHeight;

  // Vertical range: at least -10..+10 deg, growing to fit the data.
  let lo = -10, hi = 10;
  for (const p of history) { lo = Math.min(lo, p.pitch, p.accel); hi = Math.max(hi, p.pitch, p.accel); }
  lo = Math.floor(lo / 10) * 10; hi = Math.ceil(hi / 10) * 10;
  const now = performance.now();
  const x = t => w - (now - t) / HISTORY_MS * w;
  const y = v => h - (v - lo) / (hi - lo) * h;

  // Grid lines every 10 degrees, 0 brighter.
  g.font = '11px system-ui'; g.fillStyle = '#8a94a3';
  for (let v = lo; v <= hi; v += 10) {
    g.strokeStyle = v === 0 ? '#556070' : '#2b323c';
    g.beginPath(); g.moveTo(0, y(v)); g.lineTo(w, y(v)); g.stroke();
    g.fillText(v + '°', 4, y(v) - 3);
  }

  function line(key, color) {
    g.strokeStyle = color; g.lineWidth = 2; g.beginPath();
    history.forEach((p, i) => i ? g.lineTo(x(p.t), y(p[key])) : g.moveTo(x(p.t), y(p[key])));
    g.stroke();
  }
  line('accel', '#f2a33a');
  line('pitch', '#4fb3ff');
}

// ---------- polling ----------

let lastTick = -1, failures = 0;

async function poll() {
  try {
    const res = await fetch('/state', { cache: 'no-store' });
    const s = await res.json();
    failures = 0;
    el('conn').className = 'conn';
    el('conn').innerHTML = '<b>connected</b> &middot; ' + (s.wifi_mode === 'home' ? 'home network ' : 'own network ') +
                           s.wifi_ssid + ' &middot; ' + s.ip;
    el('netNow').textContent = s.wifi_mode === 'home'
      ? 'On the home network "' + s.wifi_ssid + '". Own network is off.'
      : 'On BB1\'s own network "' + s.wifi_ssid + '". Save a home network below to join it instead.';

    if (s.have) {
      el('pitch').textContent = s.pitch.toFixed(1);
      el('rate').textContent = s.pitch_rate.toFixed(1) + ' deg/s';
      el('accel').textContent = s.accel_pitch.toFixed(1) + ' deg';
      el('work').textContent = s.work_us + ' us';
      el('tick').textContent = s.tick;
      if (s.tick !== lastTick) {                       // only add new samples to the chart
        history.push({ t: performance.now(), pitch: s.pitch, accel: s.accel_pitch });
        lastTick = s.tick;
      }
    }
    el('bias').textContent = s.bias.map(b => b.toFixed(2)).join(' / ');
    el('alphaNow').textContent = s.alpha.toFixed(3);
    el('imu').textContent = s.imu ? 'ok' : 'NOT FOUND';
    el('uptime').textContent = s.uptime_s + ' s';
  } catch (e) {
    if (++failures > 5) {
      el('conn').className = 'conn lost';
      el('conn').innerHTML = '<b>connection lost</b> &middot; retrying';
    }
  }
  const cutoff = performance.now() - HISTORY_MS;
  while (history.length && history[0].t < cutoff) history.shift();
  drawChart();
  setTimeout(poll, 50);                                // about 20 updates per second
}

poll();
</script>
</body>
</html>
)PAGE";

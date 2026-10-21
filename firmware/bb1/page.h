// page.h - the web page, stored in the firmware as one string.
//
// Plain HTML + CSS + JavaScript, no libraries and nothing loaded from the
// internet, so it works when your phone is on BB1's own network.
// R"PAGE( ... )PAGE" is a C++ "raw string": everything between the markers is
// taken exactly as written, so the HTML needs no escaping.
//
// Memory: the string is "const", so it stays in flash (about 20 KB of 2 MB)
// and costs no RAM. The browser does all the layout work, not the Pico.
//
// How it works: every 50 ms the page asks GET /state, adds the pitch to a
// 10-second history, and redraws the chart and the numbers. Buttons send POSTs
// (see web.h for the list). While Record or the Live log is on, one loop pulls
// every 5 ms record with GET /log?since=N and hands it to both.
//
// Layout: the panels sit in one column. Each panel's arrows move it up or
// down; the order is remembered by this browser (localStorage). Anything wider
// than a panel (tables, the live log) scrolls sideways inside the panel.
//
// To change the page without the robot: run tools/mock_bb1.py, which serves
// this exact page with made-up data (docs/WEB_INTERFACE.md, "Offline mock").

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
  h1 { font-size:20px; margin:0; }
  h2 { font-size:15px; margin:0; color:var(--muted); font-weight:600; }
  .top { display:flex; justify-content:space-between; align-items:baseline; flex-wrap:wrap; gap:8px;
         max-width:960px; margin:0 auto 12px; }
  .conn { font-size:13px; color:var(--muted); }
  .conn b { color:var(--ok); }
  .conn.lost b { color:var(--bad); }
  .conn.mock b { color:var(--accel); }
  /* One column of panels. min-width:0 lets a panel shrink to the screen, and
     overflow-x:auto makes wide content scroll inside it instead of stretching the page. */
  .stack { display:flex; flex-direction:column; gap:12px; max-width:960px; margin:0 auto; }
  .panel { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:14px;
           min-width:0; overflow-x:auto; }
  .head { display:flex; justify-content:space-between; align-items:center; margin-bottom:10px; }
  .move button { background:none; color:var(--muted); padding:2px 8px; font-size:13px; }
  .move button:hover { color:var(--text); }
  .big { font-size:44px; font-weight:700; font-variant-numeric:tabular-nums; }
  .big small { font-size:16px; color:var(--muted); font-weight:400; }
  canvas { width:100%; min-width:300px; height:220px; display:block; }
  table { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; white-space:nowrap; }
  td { padding:4px 12px 4px 0; border-bottom:1px solid var(--line); }
  td:last-child { text-align:right; padding-right:0; }
  .legend span { margin-right:14px; font-size:13px; }
  .legend i { display:inline-block; width:10px; height:3px; vertical-align:middle; margin-right:5px; }
  button, input, select { font:inherit; border-radius:8px; border:1px solid var(--line); padding:8px 12px; }
  button { background:#2a6df4; color:white; border:none; cursor:pointer; }
  button.secondary { background:#39414d; }
  button:disabled { opacity:0.4; cursor:default; }
  input, select { background:#0d1014; color:var(--text); }
  input { width:100%; }
  .row { display:flex; gap:8px; margin-bottom:8px; align-items:center; flex-wrap:wrap; }
  .row input { flex:1; min-width:160px; }
  .msg { font-size:13px; color:var(--muted); min-height:1.4em; }
  /* The live log: fixed height, scrolls both ways, lines never wrap. */
  pre { margin:0; height:260px; overflow:auto; white-space:pre; background:#0d1014;
        border:1px solid var(--line); border-radius:8px; padding:8px;
        font:12px/1.5 ui-monospace, Menlo, monospace; }
  .bar { width:140px; height:8px; background:#0d1014; border-radius:4px; display:inline-block;
         vertical-align:middle; margin-left:8px; overflow:hidden; }
  .bar i { display:block; height:100%; background:var(--pitch); }
  button.stop { background:var(--bad); font-weight:700; }
  .num { width:90px; flex:none !important; min-width:0 !important; }
  .state { font-size:22px; font-weight:700; }
  /* Things with a hint: dotted underline, "?" cursor. Hover (or tap on a phone) to read it. */
  .hinted { text-decoration:underline dotted var(--muted); text-underline-offset:3px; cursor:help; }
  #hintBox { position:fixed; left:16px; right:16px; bottom:16px; max-width:640px; margin:0 auto; padding:10px 14px;
             background:#2b323c; border:1px solid var(--muted); border-radius:8px; font-size:14px; display:none; z-index:9; }
  .state.on { color:var(--ok); }
  .gains label { display:inline-flex; gap:6px; align-items:center; font-size:13px; color:var(--muted); }
</style>
</head>
<body>

<div class="top">
  <h1>BB1 balance bot</h1>
  <div class="conn" id="conn">connecting...</div>
</div>

<div class="stack" id="stack">

  <div class="panel" id="p-pitch">
    <div class="head"><h2>Pitch (forward +)</h2></div>
    <div class="big"><span id="pitch">--</span><small> deg</small></div>
    <canvas id="chart"></canvas>
    <div class="legend">
      <span><i style="background:var(--pitch)"></i>pitch (filtered)</span>
      <span><i style="background:var(--accel)"></i>accel only</span>
      <span style="color:var(--muted)">last 10 s</span>
    </div>
  </div>

  <div class="panel" id="p-values">
    <div class="head"><h2>Live values</h2></div>
    <table>
      <tr><td id="h-rate">Pitch rate</td><td id="rate">--</td></tr>
      <tr><td id="h-accel">Accel-only pitch</td><td id="accel">--</td></tr>
      <tr><td id="h-work">Loop work (budget 5000 us)</td><td id="work">--</td></tr>
      <tr><td id="h-tick">Tick</td><td id="tick">--</td></tr>
      <tr><td id="h-bias">Gyro bias x / y / z (deg/s)</td><td id="bias">--</td></tr>
      <tr><td id="h-alpha">Filter alpha</td><td id="alphaNow">--</td></tr>
      <tr><td>IMU</td><td id="imu">--</td></tr>
      <tr><td>Uptime</td><td id="uptime">--</td></tr>
      <tr><td id="h-enc">Encoders left / right (counts, forward +)</td><td id="enc">--</td></tr>
      <tr><td id="h-speed">Wheel speed left / right (counts/s)</td><td id="speed">--</td></tr>
      <tr><td id="h-pwm">Motor PWM left / right</td><td id="pwm">--</td></tr>
      <tr><td id="h-pid">PID terms P / I / D (PWM)</td><td id="pid">--</td></tr>
    </table>
  </div>

  <div class="panel" id="p-log">
    <div class="head"><h2>Record a log</h2></div>
    <div class="row"><input id="note" placeholder="note for this run, e.g. tilt test" autocomplete="off"></div>
    <div class="row">
      <button id="recBtn" onclick="toggleRecord()">Record</button>
      <button class="secondary" id="dlBtn" onclick="downloadRecording()" disabled>Download</button>
      <button class="secondary" id="saveLastBtn" onclick="saveLast()">Save last 10 s</button>
    </div>
    <div class="msg" id="logMsg">Record keeps every 5 ms tick until you press Stop. Save last 10 s grabs what just happened.</div>
  </div>

  <div class="panel" id="p-live">
    <div class="head"><h2>Live log</h2></div>
    <div class="row">
      <button id="liveBtn" onclick="toggleLive()">Start</button>
      <select id="liveEvery">
        <option value="1">every record (200/s)</option>
        <option value="10" selected>every 10th (20/s)</option>
        <option value="40">every 40th (5/s)</option>
      </select>
      <button class="secondary" onclick="clearLive()">Clear</button>
    </div>
    <pre id="liveLog">Press Start to watch the log records arrive (the same columns as a downloaded CSV).</pre>
    <div class="msg" id="liveMsg"></div>
  </div>

  <div class="panel" id="p-balance">
    <div class="head"><h2>Balance</h2></div>
    <div class="row">
      <span class="state" id="armState">--</span>
      <span class="msg" id="armReason"></span>
    </div>
    <div class="row">
      <button id="armBtn" onclick="arm()">ARM</button>
      <button class="stop" id="disarmBtn" onclick="disarm()">DISARM (space)</button>
    </div>
    <div class="msg">Hold it near upright to arm. It disarms by itself if it leans past 45 deg, or if this page stops for 1 s.</div>
    <div class="row gains" style="margin-top:10px">
      <label>Kp <input class="num" id="gKp" type="number" step="0.5"></label>
      <label>Ki <input class="num" id="gKi" type="number" step="0.1"></label>
      <label>Kd <input class="num" id="gKd" type="number" step="0.05"></label>
    </div>
    <div class="row gains">
      <label>trim <input class="num" id="gTrim" type="number" step="0.1"></label>
      <label>cap <input class="num" id="gCap" type="number" step="5"></label>
      <label>deadband <input class="num" id="gDb" type="number" step="1"></label>
    </div>
    <div class="row">
      <button class="secondary" id="applyBtn" onclick="applyGains()">Apply</button>
      <button class="secondary" id="trimHereBtn" onclick="post('/gains', {trim: 'here'}, 'gainsMsg')">Use current pitch as trim</button>
    </div>
    <div class="msg" id="gainsMsg">Changes apply on the next tick. Not saved: power-up goes back to the defaults in config.h.</div>
  </div>

  <div class="panel" id="p-motors">
    <div class="head"><h2>Motor test</h2></div>
    <div class="msg" style="color:var(--bad)">Wheels off the table first. Every test stops by itself.</div>
    <div class="row">
      <label>left <input class="num" id="mL" type="number" value="100" min="-150" max="150"></label>
      <label>right <input class="num" id="mR" type="number" value="100" min="-150" max="150"></label>
      <select id="mMs">
        <option value="500">0.5 s</option><option value="1000">1 s</option>
        <option value="2000" selected>2 s</option><option value="3000">3 s</option>
      </select>
    </div>
    <div class="row">
      <button onclick="motorTest()">Run</button>
      <button class="stop" onclick="motorStop()">STOP (space)</button>
    </div>
    <div class="msg" id="motorMsg">PWM -150..150, forward +. 100 is about 40% power.</div>
  </div>

  <div class="panel" id="p-actions">
    <div class="head"><h2>Actions</h2></div>
    <div class="row">
      <button id="calBtn" onclick="post('/cal', null, 'calMsg')">Calibrate gyro</button>
      <span class="msg">lay the robot still first</span>
    </div>
    <div class="msg" id="calMsg"></div>
    <div class="row">
      <input id="alpha" type="number" step="0.001" min="0.9" max="0.999" placeholder="alpha, e.g. 0.98">
      <button class="secondary" onclick="post('/alpha', {value: el('alpha').value}, 'alphaMsg')">Set alpha</button>
    </div>
    <div class="msg" id="alphaMsg"></div>
  </div>

  <div class="panel" id="p-sys">
    <div class="head"><h2>Pico memory</h2></div>
    <table>
      <tr><td id="h-ram">RAM used (of 256 KB)</td><td id="ramUsed">--</td></tr>
      <tr><td>&nbsp;&nbsp;fixed: globals, incl. the log buffer</td><td id="ramStatic">--</td></tr>
      <tr><td>&nbsp;&nbsp;heap in use (Wi-Fi, web requests)</td><td id="heapUsed">--</td></tr>
      <tr><td id="h-heap">Heap free</td><td id="heapFree">--</td></tr>
      <tr><td id="h-flash">Flash used by the program (of 2 MB)</td><td id="flashUsed">--</td></tr>
      <tr><td>&nbsp;&nbsp;this page</td><td id="pageSize">--</td></tr>
      <tr><td>CPU clock</td><td id="cpu">--</td></tr>
    </table>
  </div>

  <div class="panel" id="p-net">
    <div class="head"><h2>Network</h2></div>
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

<div id="hintBox"></div>

<script>
const el = id => document.getElementById(id);
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---------- hints ----------
//
// One place for every explanation on the page. Each key is an element id;
// the text becomes its hover tooltip (the "title" attribute). For a box inside
// a <label>, the label word gets it too. Phones can't hover, so tapping a
// hinted word shows the same text in a box at the bottom for a few seconds.

const HINTS = {
  // Balance
  armBtn: 'Start balancing: the PID drives both wheels from the lean. Only works within 10 deg of the trim, with the gyro calibrated.',
  disarmBtn: 'Stop balancing now: brakes both wheels and puts the motor driver to sleep. The space bar does the same anywhere on the page.',
  gKp: 'Kp (proportional): PWM per degree of lean past the trim. The main "fight back" strength. Too low: falls slowly. Too high: shakes fast.',
  gKi: 'Ki (integral): pushes against a lean that lasts, to fix steady drift. Keep at 0 until Kp and Kd work.',
  gKd: 'Kd (derivative): PWM per deg/s of falling speed, from the gyro. Brakes the swinging that Kp causes. Too high: motors buzz.',
  gTrim: 'Trim: the pitch (deg) where BB1 really balances. The loop holds this angle, not 0. Measured -4.39 deg.',
  gCap: 'Cap: the most PWM the balance loop may send (of 255). 80 = about 30% power, gentle for first tests. Raise it once it balances.',
  gDb: 'Deadband: PWM added to every non-zero command, because the motors do not turn at all below a few PWM. 0 until we measure it.',
  applyBtn: 'Send all six numbers to BB1. They apply on the next 5 ms tick. Not saved: a restart goes back to the defaults in config.h.',
  trimHereBtn: 'Hold BB1 at its balance point (where it would tip neither way), then press: the current pitch becomes the trim.',
  // Motor test
  mL: 'Left wheel PWM, -150..150. Positive = roll forward. 100 is about 40% power.',
  mR: 'Right wheel PWM, -150..150. Positive = roll forward.',
  mMs: 'How long the test runs. It always stops by itself.',
  // Logging
  note: 'Saved in the file and its name, so you can tell runs apart later.',
  recBtn: 'Record keeps every 5 ms record until you press Stop. Then Download saves it as a CSV.',
  saveLastBtn: 'Download the last 10 s BB1 still has in memory, even if you were not recording.',
  liveEvery: 'How many records to show in the box. Recording always keeps every record.',
  // Actions
  calBtn: 'Measures the gyro bias (its reading when still) for 2 s. Lay BB1 still first. Disarms if armed.',
  alpha: 'Filter weight 0.90..0.999: how much the angle trusts the gyro over the accelerometer each tick. Higher = smoother, slower to correct drift.',
  // Live values
  'h-rate': 'How fast it is tipping, deg/s, from the gyro. Forward +.',
  'h-accel': 'Pitch from the accelerometer alone: right on average, but jumpy when the robot moves or is bumped.',
  'h-work': 'How long one 5 ms tick took. Must stay well under 5000 us or the loop runs late.',
  'h-tick': 'Counts up 200 times a second. Every log row has one; a gap means lost records.',
  'h-bias': 'What the gyro reads while still, subtracted from every reading. Measured at startup and by Calibrate gyro.',
  'h-alpha': 'The complementary filter weight in use (see Set alpha).',
  'h-enc': 'Encoder counts since startup, forward +. About 520 per wheel turn.',
  'h-speed': 'Wheel speed over the last 50 ms. About 1750 counts/s at PWM 100 with the wheels in the air.',
  'h-pwm': 'What is being sent to each motor right now, -255..255, forward +.',
  'h-pid': 'The three parts of the balance output: P from the lean, I from lasting lean, D from tipping speed. PWM = P + I + D, clipped to the cap.',
  // Memory
  'h-ram': 'RAM used: fixed globals (mostly the 10 s log buffer) plus the heap in use.',
  'h-heap': 'RAM left for Wi-Fi and web requests. Keep it above about 20 KB.',
  'h-flash': 'Program size in the 2 MB flash chip, including this page.',
};

let hintTimer;
function showHint(text) {
  const box = el('hintBox');
  box.textContent = text;
  box.style.display = 'block';
  clearTimeout(hintTimer);
  hintTimer = setTimeout(() => box.style.display = 'none', 5000);
}

for (const [id, text] of Object.entries(HINTS)) {
  const target = el(id);
  if (!target) continue;
  const label = target.closest('label');
  for (const e of label ? [target, label] : [target]) e.title = text;
  // The visible word that gets the dotted underline: the label, or the element itself.
  const word = label || target;
  word.classList.add('hinted');
  if (target.tagName === 'TD' || label) word.addEventListener('click', ev => {
    if (ev.target === word || ev.target.tagName === 'TD') showHint(text);   // a tap on the word, not in the box
  });
}

// ---------- moving panels ----------
//
// Each panel gets an up and a down arrow. Moving one just moves its <div> in
// the page; the new order is saved in this browser so it stays after a reload.

const stack = el('stack');

function saveOrder() {
  const ids = [...stack.children].map(p => p.id);
  try { localStorage.setItem('bb1.panelOrder', JSON.stringify(ids)); } catch (e) {}
}

function movePanel(panel, dir) {
  if (dir < 0 && panel.previousElementSibling) stack.insertBefore(panel, panel.previousElementSibling);
  if (dir > 0 && panel.nextElementSibling) stack.insertBefore(panel.nextElementSibling, panel);
  saveOrder();
  panel.scrollIntoView({ block: 'nearest' });
}

for (const panel of stack.children) {
  const box = document.createElement('span');
  box.className = 'move';
  box.innerHTML = '<button title="move up">&#9650;</button><button title="move down">&#9660;</button>';
  box.children[0].onclick = () => movePanel(panel, -1);
  box.children[1].onclick = () => movePanel(panel, +1);
  panel.querySelector('.head').appendChild(box);
}

try {                                          // put the panels back in the saved order
  const saved = JSON.parse(localStorage.getItem('bb1.panelOrder') || '[]');
  for (const id of saved) if (el(id)) stack.appendChild(el(id));
} catch (e) {}

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

// ---------- motor test ----------

let testsDoneSeen = -1;    // so we can tell when a new test result arrives in /state

function motorTest() {
  if (!confirm('Run the motors? Wheels must be off the table.')) return;
  post('/motor', { left: el('mL').value, right: el('mR').value, ms: el('mMs').value }, 'motorMsg');
}

function motorStop() {
  post('/motor/stop', null, 'motorMsg');
}

// ---------- balance ----------

function arm() { post('/arm', null, 'armReason'); }

// Browsers slow a hidden tab down to about one update a second, which would
// trip the 1 s heartbeat anyway. Say so clearly instead: disarm when hidden.
let armedNow = false;
document.addEventListener('visibilitychange', () => {
  if (document.hidden && armedNow) navigator.sendBeacon('/disarm?why=hidden');
});
function disarm() { post('/disarm', null, 'armReason'); }

function applyGains() {
  post('/gains', { kp: el('gKp').value, ki: el('gKi').value, kd: el('gKd').value,
                   trim: el('gTrim').value, cap: el('gCap').value, deadband: el('gDb').value }, 'gainsMsg');
}

// Fill the boxes from the robot when the page opens and after every change,
// but never while you're typing in one.
let gainsVerShown = -1;
function showGains(g) {
  if (g.ver === gainsVerShown || document.activeElement.closest && document.activeElement.closest('.gains')) return;
  gainsVerShown = g.ver;
  el('gKp').value = g.kp; el('gKi').value = g.ki; el('gKd').value = g.kd;
  el('gTrim').value = g.trim; el('gCap').value = g.cap; el('gDb').value = g.deadband;
}

// Space bar = stop motors and disarm, anywhere on the page (except while typing in a box).
document.addEventListener('keydown', e => {
  if (e.code === 'Space' && !['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) {
    e.preventDefault();
    motorStop();
  }
});

// ---------- pulling log records ----------
//
// The robot keeps the last 10 s of records. While Record or Live log is on,
// pumpLoop() asks for everything newer than what we have (/log?since=next)
// about four times a second and hands the new lines to whoever is listening.
// One loop for both, so watching and recording never ask twice.

// Ask for records from tick "since" on. The first line of the answer says
// which ticks came back and how many were lost (see handleLog in web.h).
async function fetchLog(since) {
  const text = await (await fetch('/log?since=' + since, { cache: 'no-store' })).text();
  const lines = text.split('\n').filter(l => l.length);
  const info = lines[0].slice(2).split(',');        // "# from,1200,to,1450,lost,0"
  const get = k => Number(info[info.indexOf(k) + 1]);
  return { from: get('from'), to: get('to'), lost: get('lost'), header: lines[1], rows: lines.slice(2) };
}

let pumpRunning = false, pumpNext = 0, logHeader = '';

function startPump() {
  if (pumpRunning) return;                          // already running: it will see the new listener
  pumpRunning = true;
  pumpNext = lastTick >= 0 ? lastTick + 1 : 0;      // start from "now"
  pumpLoop();
}

async function pumpLoop() {
  while (rec.on || live.on) {
    try {
      const r = await fetchLog(pumpNext);
      logHeader = r.header;
      pumpNext = r.to;
      if (rec.on) recordAdd(r.rows, r.lost);
      if (live.on) liveAdd(r.rows, r.lost);
      if (r.to - r.from < 1000) await sleep(250);   // caught up: wait a bit before asking again
    } catch (e) {
      el('logMsg').textContent = el('liveMsg').textContent = 'no answer from the robot, retrying...';
      await sleep(500);
    }
  }
  pumpRunning = false;
}

// ---------- recording ----------

const rec = { on: false, rows: [], lost: 0, meta: '', header: '', note: '', started: null };

function recStatus(word) {
  return word + ': ' + rec.rows.length + ' records (' + (rec.rows.length / 200).toFixed(1) + ' s)' +
         (rec.lost ? ', ' + rec.lost + ' lost' : '');
}

function recordAdd(rows, lost) {
  rec.rows.push(...rows);
  rec.lost += lost;
  rec.header = logHeader;
  el('logMsg').textContent = recStatus('recording');
}

async function toggleRecord() {
  if (rec.on) {                                     // Stop
    rec.on = false;
    el('recBtn').textContent = 'Record';
    el('dlBtn').disabled = rec.rows.length === 0;
    el('logMsg').textContent = recStatus('stopped') + '. Press Download to save.';
    return;
  }
  try {
    rec.meta = await (await fetch('/log/meta')).text();
  } catch (e) {
    el('logMsg').textContent = 'no answer from the robot';
    return;
  }
  rec.rows = []; rec.lost = 0; rec.note = el('note').value; rec.started = new Date();
  rec.on = true;
  el('recBtn').textContent = 'Stop';
  el('dlBtn').disabled = true;
  el('logMsg').textContent = recStatus('recording');
  startPump();
}

// Build the file: settings lines, note, header, records. Same layout as the
// serial "dump" and tools/logger.py, so tools/check_dump.py reads all of them.
function saveFile(meta, note, started, header, rows, lost) {
  const pad = n => String(n).padStart(2, '0');
  const d = started;
  const stamp = d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + '_' +
                pad(d.getHours()) + '-' + pad(d.getMinutes()) + '-' + pad(d.getSeconds());
  const slug = note.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40);
  const text = meta +
    '# note,' + note.replace(/[,\n]/g, ';') + '\n' +
    '# recorded_at,' + d.toISOString() + '\n' +
    '# records,' + rows.length + ',lost,' + lost + '\n' +
    header + '\n' + rows.join('\n') + '\n';
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: 'text/csv' }));
  a.download = 'bb1_' + stamp + (slug ? '_' + slug : '') + '.csv';
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function downloadRecording() {
  saveFile(rec.meta, rec.note, rec.started, rec.header, rec.rows, rec.lost);
}

// Everything the robot still has (about 10 s), fetched in pieces of up to 1000.
async function saveLast() {
  try {
    const meta = await (await fetch('/log/meta')).text();
    let r = await fetchLog(0), rows = r.rows, header = r.header;
    while (r.to - r.from >= 1000) { r = await fetchLog(r.to); rows = rows.concat(r.rows); }
    const note = el('note').value || 'last 10 s';
    saveFile(meta, note, new Date(), header, rows, 0);
    el('logMsg').textContent = 'saved the last ' + (rows.length / 200).toFixed(1) + ' s (' + rows.length + ' records)';
  } catch (e) {
    el('logMsg').textContent = 'no answer from the robot';
  }
}

// ---------- live log ----------
//
// Shows the newest records as text. Only the last LIVE_LINES are kept, so the
// page doesn't slow down after a long run. "every 10th" keeps it readable;
// Record still saves every record either way.

const LIVE_LINES = 200;
const live = { on: false, lines: [], total: 0, lost: 0 };

function liveAdd(rows, lost) {
  const every = Number(el('liveEvery').value);
  for (const line of rows) {
    const tick = parseInt(line, 10);               // the first column is the tick number
    if (tick % every === 0) live.lines.push(line);
  }
  live.total += rows.length;
  live.lost += lost;
  if (live.lines.length > LIVE_LINES) live.lines.splice(0, live.lines.length - LIVE_LINES);

  const pre = el('liveLog');
  const atBottom = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 20;
  pre.textContent = logHeader + '\n' + live.lines.join('\n');
  if (atBottom) pre.scrollTop = pre.scrollHeight;  // follow new lines, unless you scrolled up to read
  el('liveMsg').textContent = live.total + ' records received' + (live.lost ? ', ' + live.lost + ' lost' : '') +
                              '. Scroll sideways for more columns.';
}

function toggleLive() {
  live.on = !live.on;
  el('liveBtn').textContent = live.on ? 'Pause' : 'Start';
  if (live.on) startPump();
}

function clearLive() {
  live.lines = []; live.total = 0; live.lost = 0;
  el('liveLog').textContent = logHeader;
  el('liveMsg').textContent = '';
}

// ---------- Pico memory ----------

const kb = b => (b / 1024).toFixed(1) + ' KB';
const bar = (part, whole) => '<span class="bar"><i style="width:' + (100 * part / whole).toFixed(0) + '%"></i></span>';

async function pollSys() {
  try {
    const m = await (await fetch('/sys', { cache: 'no-store' })).json();
    const used = m.ram_static + m.heap_used;
    el('ramUsed').innerHTML = kb(used) + ' (' + (100 * used / m.ram_total).toFixed(0) + '%)' + bar(used, m.ram_total);
    el('ramStatic').textContent = kb(m.ram_static) + ' (log buffer ' + kb(m.log_buffer) + ')';
    el('heapUsed').textContent = kb(m.heap_used);
    el('heapFree').textContent = kb(m.heap_total - m.heap_used);
    el('flashUsed').innerHTML = kb(m.flash_used) + ' (' + (100 * m.flash_used / m.flash_total).toFixed(0) + '%)' +
                                bar(m.flash_used, m.flash_total);
    el('pageSize').textContent = kb(m.page);
    el('cpu').textContent = m.cpu_mhz + ' MHz';
  } catch (e) {}
  setTimeout(pollSys, 2000);                           // memory changes slowly
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
    el('conn').className = s.mock ? 'conn mock' : 'conn';
    el('conn').innerHTML = (s.mock ? '<b>MOCK robot (made-up data)</b>' : '<b>connected</b>') + ' &middot; ' +
                           (s.wifi_mode === 'home' ? 'home network ' : 'own network ') + s.wifi_ssid + ' &middot; ' + s.ip;
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
    el('bias').innerHTML = s.ever_cal ? s.bias.map(b => b.toFixed(2)).join(' / ')
      : '<span style="color:var(--bad)">not calibrated: set the robot down still</span>';
    el('alphaNow').textContent = s.alpha.toFixed(3);
    el('imu').textContent = s.imu ? 'ok' : 'NOT FOUND';
    el('uptime').textContent = s.uptime_s + ' s';
    el('enc').textContent = s.enc_l + ' / ' + s.enc_r;
    el('speed').textContent = s.speed_l + ' / ' + s.speed_r;
    el('pwm').textContent = s.pwm_l + ' / ' + s.pwm_r + (s.test_running ? '  (test running)' : '');
    el('pid').textContent = s.p.toFixed(1) + ' / ' + s.i.toFixed(1) + ' / ' + s.d.toFixed(1);
    armedNow = s.armed;
    el('armState').textContent = s.armed ? 'ARMED' : (s.mode === 5 ? 'FALLEN' : 'disarmed');
    el('armState').className = s.armed ? 'state on' : 'state';
    el('armReason').textContent = s.armed ? 'balancing' : s.reason;
    showGains(s.gains);
    if (testsDoneSeen >= 0 && s.tests_done !== testsDoneSeen) {        // a test just finished
      const sec = Math.max(s.test_ms, 1) / 1000;
      el('motorMsg').textContent = 'done after ' + s.test_ms + ' ms: left moved ' + s.test_moved[0] +
        ' counts (' + Math.round(s.test_moved[0] / sec) + '/s), right ' + s.test_moved[1] +
        ' counts (' + Math.round(s.test_moved[1] / sec) + '/s)';
    }
    testsDoneSeen = s.tests_done;
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
pollSys();
</script>
</body>
</html>
)PAGE";

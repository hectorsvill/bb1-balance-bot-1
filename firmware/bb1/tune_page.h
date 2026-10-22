// tune_page.h - the small PID tuning page at /tune, stored in flash like page.h.
//
// Only what tuning needs: the gain boxes (Apply, Save to flash), live pitch,
// error, P / I / D terms and motor PWM, a 5 s chart of error and PWM, and
// short notes on how to tune.
//
// It never arms. You arm from the terminal (tools/handheld.py, key a), so a
// Wi-Fi hiccup can't disarm BB1 in the middle of a try (armed from serial, the
// page heartbeat doesn't apply). Logs are recorded by handheld.py too.
//
// It talks to the same endpoints as the full page (web.h): GET /state ten
// times a second, POST /gains, POST /gains/save. The full page is still at /.
//
// To change it without the robot: tools/mock_bb1.py serves it at
// http://localhost:8080/tune.

#pragma once

const char TUNE_HTML[] = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BB1 tuning</title>
<style>
  :root { --bg:#0e1116; --panel:#161b22; --panel2:#1c222b; --line:#262d37; --text:#e6edf3; --muted:#8b96a5;
          --blue:#4ea1ff; --orange:#f0a03c; --green:#3fb97a; --red:#f05252; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.45 -apple-system, system-ui, sans-serif; }
  main { max-width:720px; margin:0 auto; padding:12px 16px 40px; display:flex; flex-direction:column; gap:12px; }
  h1 { font-size:18px; margin:0; } h2 { font-size:15px; margin:0 0 8px; }
  .panel { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:12px; }
  .top { display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
  .dot { width:10px; height:10px; border-radius:50%; background:var(--muted); }
  .dot.ok { background:var(--green); } .dot.bad { background:var(--red); } .dot.mock { background:var(--orange); }
  .chip { padding:3px 10px; border-radius:999px; font-weight:700; font-size:13px; background:var(--panel2); color:var(--muted); }
  .chip.armed { background:rgba(63,185,122,.18); color:var(--green); }
  .chip.fallen { background:rgba(240,82,82,.18); color:var(--red); }
  .muted { color:var(--muted); font-size:13px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit, minmax(100px, 1fr)); gap:8px; }
  .box { background:var(--panel2); border-radius:10px; padding:8px 10px; min-width:0; }
  .box .name { color:var(--muted); font-size:12px; }
  .box .val { font-size:22px; font-weight:700; font-variant-numeric:tabular-nums; }
  label.box { display:flex; flex-direction:column; gap:4px; }
  input { width:100%; background:#0b0e13; color:var(--text); border:1px solid var(--line); border-radius:8px; padding:7px 8px; font-size:16px; }
  .buttons { display:flex; gap:8px; flex-wrap:wrap; margin-top:10px; }
  button { border:none; border-radius:8px; padding:9px 14px; cursor:pointer; background:var(--panel2); color:var(--text); font-weight:600; font-size:14px; }
  button.go { background:var(--green); color:#06210f; font-weight:800; }
  .warn { color:var(--orange); }
  canvas { width:100%; height:160px; display:block; }
  .legend span { margin-right:12px; font-size:13px; }
  ol, ul { margin:4px 0; padding-left:20px; } li { margin:3px 0; }
  table { border-collapse:collapse; width:100%; font-size:14px; }
  td, th { text-align:left; padding:4px 6px; border-bottom:1px solid var(--line); vertical-align:top; }
  code { background:var(--panel2); padding:1px 5px; border-radius:5px; }
</style>
</head>
<body>
<main>

<!-- Status: connection and armed state (read only: arm from the terminal) -->
<div class="panel top">
  <span class="dot" id="dot"></span>
  <h1>BB1 PID tuning</h1>
  <span class="chip" id="mode">--</span>
  <span class="muted" id="why"></span>
  <span class="muted" style="margin-left:auto"><a href="/" style="color:var(--blue)">full page</a></span>
</div>

<!-- Gains: type, Apply. Save to flash keeps them after a restart (disarmed only). -->
<div class="panel">
  <h2>Gains <span class="muted" id="ver"></span></h2>
  <div class="grid">
    <label class="box"><span class="name">Kp (lean)</span><input id="kp" inputmode="decimal"></label>
    <label class="box"><span class="name">Ki (slow drift)</span><input id="ki" inputmode="decimal"></label>
    <label class="box"><span class="name">Kd (lean speed)</span><input id="kd" inputmode="decimal"></label>
    <label class="box"><span class="name">Trim (deg)</span><input id="trim" inputmode="decimal"></label>
    <label class="box"><span class="name">Cap (PWM of 255)</span><input id="cap" inputmode="numeric"></label>
  </div>
  <div class="buttons">
    <button class="go" id="apply">Apply</button>
    <button id="trimHere">Trim = current pitch</button>
    <button id="save">Save to flash</button>
    <button id="reload">Reload from BB1</button>
  </div>
  <div class="muted" id="msg" style="margin-top:8px">Apply works any time, even while armed. Save only while disarmed.</div>
</div>

<!-- Live: what the controller sees and does right now -->
<div class="panel">
  <h2>Live</h2>
  <div class="grid">
    <div class="box"><div class="name">Pitch (deg)</div><div class="val" id="pitch">--</div></div>
    <div class="box"><div class="name">Error = pitch - trim</div><div class="val" id="err">--</div></div>
    <div class="box"><div class="name">P term</div><div class="val" id="p">--</div></div>
    <div class="box"><div class="name">I term</div><div class="val" id="i">--</div></div>
    <div class="box"><div class="name">D term</div><div class="val" id="d">--</div></div>
    <div class="box"><div class="name">PWM out</div><div class="val" id="out">--</div></div>
  </div>
  <canvas id="chart" style="margin-top:10px"></canvas>
  <div class="legend"><span style="color:var(--blue)">error (deg, x10)</span><span style="color:var(--orange)">PWM out</span><span class="muted">last 5 s</span></div>
</div>

<!-- Notes: the method, short. Full guide: docs/PID_TUNING.md -->
<div class="panel">
  <h2>How to tune</h2>
  <ol>
    <li>On the floor, clear space front and back. In the terminal: <code>tools/venv/bin/python tools/handheld.py -s 10 -n "kp15 kd0.5"</code></li>
    <li>Stand BB1 up, press <b>a</b> in the terminal, let go. Watch, catch it. 3 tries per setting.</li>
    <li>Change <b>one</b> number here, <b>Apply</b>, try again. Write down what changed.</li>
    <li>Order: Kp first (Ki 0), then Kd, then a little Ki only if it keeps drifting one way.</li>
    <li>Happy with it: disarmed, <b>Save to flash</b>. Press <b>q</b> in the terminal to save the log.</li>
  </ol>
  <table>
    <tr><th>You see</th><th>Try</th></tr>
    <tr><td>Falls over slowly, wheels too gentle</td><td>more Kp</td></tr>
    <tr><td>Fast back-and-forth shaking</td><td>more Kd, or less Kp</td></tr>
    <tr><td>Buzzing, jittery motors</td><td>less Kd</td></tr>
    <tr><td>PWM out stuck at the cap, then falls</td><td>raise Cap (max 255)</td></tr>
    <tr><td>Always falls / rolls the same way</td><td>Trim: hold it at its balance point, "Trim = current pitch"</td></tr>
    <tr><td>Slow big rocking</td><td>less Ki</td></tr>
  </table>
  <p class="muted">What the terms mean: P pushes in proportion to the lean (error), D pushes against how fast it is leaning (damps it), I adds up a lean that lasts (fixes a steady drift). PWM out = P + I + D, limited to the cap. Full guide: <code>docs/PID_TUNING.md</code>.</p>
</div>

</main>
<script>
const el = id => document.getElementById(id);
const FIELDS = ['kp', 'ki', 'kd', 'trim', 'cap'];
let gains = null, filledVer = -1;   // the gains BB1 last reported, and the version shown in the boxes

// Put BB1's current gains in the boxes. Only when asked or when BB1's version
// changed (someone else changed them), so it never overwrites what you're typing.
function fillBoxes() {
  if (!gains) return;
  for (const f of FIELDS) el(f).value = f === 'cap' ? gains[f].toFixed(0) : +gains[f].toFixed(3);
  filledVer = gains.ver;
}

async function post(url, body) {
  try {
    const res = await fetch(url, { method: 'POST', body: new URLSearchParams(body) });
    el('msg').textContent = await res.text();
  } catch (e) {
    el('msg').textContent = 'no answer from BB1';
  }
}

el('apply').onclick = async () => {
  const body = {};
  for (const f of FIELDS) if (el(f).value.trim() !== '') body[f] = el(f).value.trim();
  await post('/gains', body);
  filledVer = -1;                        // show what BB1 actually took (limits may clamp it)
};
el('trimHere').onclick = async () => { await post('/gains', { trim: 'here' }); filledVer = -1; };
el('save').onclick = () => post('/gains/save', {});
el('reload').onclick = () => { filledVer = -1; };

// ---------- chart: error x10 and PWM out, last 5 s ----------
const history = [];
function draw() {
  const c = el('chart'), dpr = window.devicePixelRatio || 1;
  c.width = c.clientWidth * dpr; c.height = c.clientHeight * dpr;
  const g = c.getContext('2d'), w = c.width, h = c.height, now = performance.now();
  const lim = gains ? Math.max(gains.cap, 50) : 150;                  // y range: +-cap
  const y = v => h / 2 - (Math.max(-lim, Math.min(lim, v)) / lim) * (h / 2 - 4);
  const x = t => w - (now - t) / 5000 * w;
  g.strokeStyle = '#262d37'; g.lineWidth = dpr;
  g.beginPath(); g.moveTo(0, h / 2); g.lineTo(w, h / 2); g.stroke();    // zero line
  for (const [key, color] of [['err', '#4ea1ff'], ['out', '#f0a03c']]) {
    g.strokeStyle = color; g.lineWidth = 2 * dpr; g.beginPath();
    history.forEach((s, n) => n ? g.lineTo(x(s.t), y(s[key])) : g.moveTo(x(s.t), y(s[key])));
    g.stroke();
  }
}

// ---------- polling /state ----------
async function poll() {
  try {
    const s = await (await fetch('/state', { cache: 'no-store', signal: AbortSignal.timeout(1000) })).json();
    gains = s.gains;
    if (filledVer !== gains.ver) fillBoxes();
    el('dot').className = 'dot ' + (s.mock ? 'mock' : 'ok');
    el('ver').textContent = 'v' + gains.ver;
    const fallen = s.mode === 5;
    el('mode').textContent = s.armed ? 'ARMED' : fallen ? 'FALLEN' : 'DISARMED';
    el('mode').className = 'chip' + (s.armed ? ' armed' : fallen ? ' fallen' : '');
    el('why').textContent = s.armed ? '' : (s.reason || '') + ' · arm with a in handheld.py';
    const err = s.pitch - gains.trim, out = s.armed ? s.pwm_l : 0;
    el('pitch').textContent = s.pitch.toFixed(1);
    el('err').textContent = (err >= 0 ? '+' : '') + err.toFixed(1);
    el('p').textContent = s.p.toFixed(0);
    el('i').textContent = s.i.toFixed(0);
    el('d').textContent = s.d.toFixed(0);
    el('out').innerHTML = out + ((s.flag_bits & 8) ? ' <small class="warn">at cap</small>' : '');
    history.push({ t: performance.now(), err: err * 10, out });
  } catch (e) {
    el('dot').className = 'dot bad';
  }
  while (history.length && history[0].t < performance.now() - 5000) history.shift();
  draw();
  setTimeout(poll, 100);                 // 10 times a second is plenty for watching
}
poll();
</script>
</body>
</html>
)PAGE";

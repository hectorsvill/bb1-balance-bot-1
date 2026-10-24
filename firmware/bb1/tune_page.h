// tune_page.h - the small PID tuning page at /tune, stored in flash like page.h.
//
// Only what tuning needs: the gain boxes (Apply, Save to flash), live pitch,
// error, P / I / D terms and motor PWM, a 5 s chart of error and PWM, and
// short notes on how to tune.
//
// Arm panel: ARM for N seconds (stops by itself), STOP (or the space bar). Armed
// from here, the firmware's heartbeat applies: this page's /state polling (10 a
// second) keeps BB1 armed, and 2 s without it disarms ("lost the web page", HEARTBEAT_MS).
// Hiding the tab disarms at once. You can still arm from tools/handheld.py
// instead (key a, USB or --wifi). Logs: run handheld.py --wifi or logger.py.
//
// It talks to the same endpoints as the full page (web.h): GET /state ten
// times a second, POST /arm, /disarm, /gains, /gains/save. The full page is still at /.
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
  .chip.batt-ok { background:rgba(63,185,122,0.18); color:var(--green); }
  .chip.batt-warn { background:rgba(240,160,60,0.18); color:var(--orange); }
  .chip.batt-low { background:rgba(240,82,82,0.18); color:var(--red); }
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
  button.stop { background:var(--red); color:#fff; font-weight:800; font-size:18px; padding:12px 26px; }
  button.arm { background:var(--green); color:#06210f; font-weight:800; font-size:18px; padding:12px 26px; }
  .bar { height:6px; border-radius:3px; background:var(--panel2); margin-top:10px; overflow:hidden; }
  .bar div { height:100%; width:0; background:var(--green); }
  .warn { color:var(--orange); }
  canvas { width:100%; height:160px; display:block; }
  .legend span { margin-right:12px; font-size:13px; }
  ol, ul { margin:4px 0; padding-left:20px; } li { margin:3px 0; }
  table { border-collapse:collapse; width:100%; font-size:14px; }
  td, th { text-align:left; padding:4px 6px; border-bottom:1px solid var(--line); vertical-align:top; }
  .tip { text-decoration:underline dotted; cursor:help; }
  code { background:var(--panel2); padding:1px 5px; border-radius:5px; }
</style>
</head>
<body>
<main>

<!-- Status: connection and armed state -->
<div class="panel top">
  <span class="dot" id="dot"></span>
  <h1>BB1 PID tuning</h1>
  <span class="chip" id="mode">--</span>
  <span class="chip" id="batt" title="battery voltage">batt --</span>
  <span class="muted" id="why"></span>
  <span class="muted" style="margin-left:auto"><a href="/" style="color:var(--blue)">full page</a></span>
</div>

<!-- Arm: N seconds, then it disarms by itself. STOP or the space bar any time. -->
<div class="panel">
  <h2>Run a try</h2>
  <div class="top">
    <label class="muted">seconds <input id="armSecs" value="10" inputmode="numeric" style="width:70px"></label>
    <button class="arm" id="armBtn">ARM</button>
    <button class="stop" id="stopBtn">STOP</button>
    <span class="val" id="left" style="font-size:22px; font-weight:700"></span>
  </div>
  <div class="bar"><div id="leftBar"></div></div>
  <div class="muted" id="armMsg" style="margin-top:8px">Stand BB1 up within 10 deg of the trim ("from trim" below), press ARM, let go. Space bar = STOP. Keep this tab in front: hiding it disarms.</div>
</div>

<!-- Gains: type, Apply. Save to flash keeps them after a restart (disarmed only). -->
<div class="panel">
  <h2>Gains <span class="muted" id="ver"></span></h2>
  <div class="grid">
    <label class="box"><span class="name tip" data-tip="P (proportional): pushes in proportion to the lean. P term = Kp x error. More Kp = stronger push back; too much = fast shaking.">P (Kp)</span><input id="kp" inputmode="decimal"></label>
    <label class="box"><span class="name tip" data-tip="I (integral): adds up a lean that lasts and pushes harder the longer it lasts. I term = Ki x error summed over time. Fixes a steady drift one way; too much = slow, big rocking. Start at 0.">I (Ki)</span><input id="ki" inputmode="decimal"></label>
    <label class="box"><span class="name tip" data-tip="D (derivative): pushes against how fast it is leaning, like a brake. D term = Kd x lean speed (deg/s). Calms shaking; too much = buzzing, jittery motors.">D (Kd)</span><input id="kd" inputmode="decimal"></label>
    <label class="box"><span class="name">Trim (deg)</span><input id="trim" inputmode="decimal"></label>
    <label class="box"><span class="name">Cap (PWM of 255)</span><input id="cap" inputmode="numeric"></label>
    <label class="box"><span class="name tip" data-tip="Deadband: PWM added to every correction so small ones still turn the wheels (they don't move below about 15 on the floor). Too much = chatter around upright.">Deadband</span><input id="deadband" inputmode="numeric"></label>
    <label class="box"><span class="name tip" data-tip="Speed P (step 8): when BB1 rolls, lean the target back this many degrees per wheel turn per second, so it brakes instead of running away. Like position hold on a quad. 0 = off.">Speed P (sp)</span><input id="sp" inputmode="decimal"></label>
    <label class="box"><span class="name tip" data-tip="Speed I (step 8): lean the target back this many degrees per wheel turn rolled since arming. Pulls BB1 back to where it started and cancels a trim error. Too much = slow, big back-and-forth. 0 = off.">Speed I (si)</span><input id="si" inputmode="decimal"></label>
  </div>
  <div class="muted" id="tipText" style="margin-top:8px; min-height:1.4em">Hover over (or tap) P, I or D to see what it does.</div>
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
    <div class="box"><div class="name">From trim (arm within 10)</div><div class="val" id="err">--</div></div>
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
  <h2>How to use this page</h2>
  <p style="margin:0"><b>Start (once)</b></p>
  <ol>
    <li>Battery on (D153C switch). <b>No USB cable</b>: it tugs on BB1.</li>
    <li>For a log: Terminal <code>tools/venv/bin/python tools/handheld.py --wifi -s 10 -n "P10 D0.74"</code></li>
  </ol>
  <p style="margin:0"><b>Change a gain</b></p>
  <ol>
    <li>Change <b>one</b> box, click <b>Apply</b>.</li>
  </ol>
  <p style="margin:0"><b>Run a try</b></p>
  <ol>
    <li>Stand BB1 upright on the floor. <b>From trim</b> within &plusmn;10.</li>
    <li>Click <b>ARM</b> (here, or <b>a</b> in the Terminal). Expect: ARMED and the countdown.</li>
    <li>Let go. Stops after the seconds you set, past 45&deg;, or on <b>STOP</b> / space bar.</li>
    <li>Catch it. 3 tries, then change a gain again.</li>
  </ol>
  <p style="margin:0"><b>Finish</b></p>
  <ol>
    <li>Liked it: <b>Save to flash</b> here (disarmed).</li>
    <li>Terminal: <b>q</b> saves the log.</li>
  </ol>
  <p class="muted">Order: P first (I = 0), then D, then a little I only if it keeps drifting one way.</p>
  <h2 style="margin-top:12px">Logs: record and read</h2>
  <ol>
    <li><b>Record:</b> <code>handheld.py</code> records the whole time it runs. Use <code>-n "note"</code> to name the file.</li>
    <li><b>Save:</b> press <b>q</b> in the Terminal. Expect: <code>Saved ... to logs/bb1_&lt;date&gt;_&lt;time&gt;_&lt;note&gt;.csv</code></li>
    <li><b>Read:</b> <code>tools/venv/bin/python tools/pid_report.py</code> (newest log) or add a file name. One row per try, then hints.</li>
    <li><b>Look:</b> add <code>--plot</code> to see error, P / I / D terms and PWM out for each try.</li>
    <li><b>Compare:</b> each gains setting has its own <code>v</code> number. Pick the one with the smallest <b>wobble</b> that always reaches the timer.</li>
  </ol>
  <table>
    <tr><th>Report column</th><th>Means</th></tr>
    <tr><td>secs / end</td><td>how long it stayed up; timer = never fell</td></tr>
    <tr><td>wobble</td><td>how much it rocks, deg. Smaller = better</td></tr>
    <tr><td>lean</td><td>average lean, deg. Far from 0 = trim off</td></tr>
    <tr><td>|P| |I| |D|</td><td>who does the work. D bigger than P = jitter</td></tr>
    <tr><td>cap%</td><td>time with motors maxed out. High = cap too low</td></tr>
    <tr><td>flips/s</td><td>PWM sign changes per second. Tens = jitter</td></tr>
    <tr><td>turns</td><td>how far it rolled. More than 2 = drift (step 8)</td></tr>
  </table>
  <h2>If you fly FPV</h2>
  <p class="muted" style="margin:0 0 8px">Same PID as one axis of Betaflight's pitch loop. The stick is fixed at "stand upright" (the trim), the motors are the wheels.</p>
  <table>
    <tr><th>Term</th><th>On a quad</th><th>On BB1</th></tr>
    <tr><td><b>P</b> (Kp)</td><td>snaps back to the stick; too much = fast P wobble</td><td>pushes against the lean; too much = fast shaking</td></tr>
    <tr><td><b>I</b> (Ki)</td><td>holds angle against wind / a heavy side</td><td>fixes a steady lean one way; too much = slow rocking</td></tr>
    <tr><td><b>D</b> (Kd)</td><td>damps bounce-back and prop wash; too much = hot motors</td><td>brakes the fall; too much = buzzing, jittery motors</td></tr>
    <tr><td>Trim</td><td>accelerometer trim (it drifts if wrong)</td><td>the angle where BB1 really balances</td></tr>
    <tr><td>Cap</td><td>motor output limit / throttle limit</td><td>max PWM while balancing (255 = full)</td></tr>
  </table>
  <h2 style="margin-top:12px">You see / try</h2>
  <table>
    <tr><th>You see</th><th>Try</th></tr>
    <tr><td>Falls over slowly, wheels too gentle</td><td>more P</td></tr>
    <tr><td>Fast back-and-forth shaking</td><td>more D, or less P</td></tr>
    <tr><td>Buzzing, jittery motors</td><td>less D</td></tr>
    <tr><td>PWM out stuck at the cap ("at cap"), then falls</td><td>raise Cap (max 255)</td></tr>
    <tr><td>Keeps leaning / rolling <b>backward</b></td><td>raise Trim (toward +), about 0.5 deg at a time</td></tr>
    <tr><td>Keeps leaning / rolling <b>forward</b></td><td>lower Trim (toward -), about 0.5 deg at a time</td></tr>
    <tr><td>Not sure where it balances</td><td>hold it where it feels balanced, "Trim = current pitch" (applies at once)</td></tr>
    <tr><td>Slow big rocking</td><td>less I</td></tr>
  </table>
  <p class="muted">PWM out = P term + I term + D term, limited to the cap. P term = P gain x error, D term = D gain x lean speed, I term = I gain x error added up over time. Full guide: <code>docs/PID_TUNING.md</code>.</p>
</div>

</main>
<script>
// Battery chip, volts per cell: green, orange under batt_warn_v, red under batt_low_v (both from
// config.h, sent in /state). "--" means no battery wire or the D153C switch is off.
function showBattery(s) {
  const b = el('batt');
  if (s.batt_v === undefined || !s.batt_ok) { b.textContent = 'batt --'; b.className = 'chip'; return; }
  // Shown per cell (pack / cells), the way LiPo chargers show it: 4.2 full, 3.7 nominal, 3.3 charge now.
  const cells = s.batt_cells || 2;
  b.textContent = (s.batt_v / cells).toFixed(2) + ' V/cell';
  b.className = 'chip ' + (s.batt_v < s.batt_low_v ? 'batt-low' : s.batt_v < s.batt_warn_v ? 'batt-warn' : 'batt-ok');
  b.title = 'battery ' + s.batt_v.toFixed(2) + ' V (' + cells + 'S). Orange under ' + (s.batt_warn_v / cells).toFixed(2) +
            ' V/cell, red (charge it) under ' + (s.batt_low_v / cells).toFixed(2) + ' V/cell.';
}

const el = id => document.getElementById(id);
const FIELDS = ['kp', 'ki', 'kd', 'trim', 'cap', 'deadband', 'sp', 'si'];
let gains = null, filledVer = -1;   // the gains BB1 last reported, and the version shown in the boxes

// Put BB1's current gains in the boxes. Only when asked or when BB1's version
// changed (someone else changed them), so it never overwrites what you're typing.
function fillBoxes() {
  if (!gains) return;
  for (const f of FIELDS) if (gains[f] !== undefined) el(f).value = (f === 'cap' || f === 'deadband') ? gains[f].toFixed(0) : +gains[f].toFixed(3);
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

// ---------- arm / stop ----------
// ARM asks BB1 to balance for N s (it refuses if not within 10 deg of the trim;
// the reason shows next to the mode). STOP and the space bar disarm at once.
async function postTo(url, body, msgId) {
  try {
    const res = await fetch(url, { method: 'POST', body: body ? new URLSearchParams(body) : null });
    el(msgId).textContent = await res.text();
  } catch (e) {
    el(msgId).textContent = 'no answer from BB1';
  }
}
const stopNow = () => postTo('/disarm', null, 'armMsg');
el('armBtn').onclick = () => {
  const secs = Math.min(Math.max(+el('armSecs').value || 10, 1), 30);   // always a timer: every run stops by itself
  el('armSecs').value = secs;
  postTo('/arm', { seconds: secs }, 'armMsg');
};
el('stopBtn').onclick = stopNow;
document.addEventListener('keydown', e => {
  // Space = STOP, except while typing in a box. preventDefault stops the space
  // bar from "clicking" the button that has focus (it could be ARM).
  if (e.code === 'Space' && e.target.tagName !== 'INPUT') { e.preventDefault(); stopNow(); }
});
// A hidden tab is slowed to about one /state a second, which would trip the
// heartbeat anyway: disarm straight away instead, with a clear reason.
let armedNow = false;
document.addEventListener('visibilitychange', () => {
  if (document.hidden && armedNow) navigator.sendBeacon('/disarm?why=hidden');
});

// Hover helper: hovering (Mac) or tapping (phone) P, I or D shows what it does
// under the boxes. The text lives in each label's data-tip.
for (const t of document.querySelectorAll('.tip')) {
  const show = () => { el('tipText').textContent = t.dataset.tip; };
  t.addEventListener('mouseenter', show);
  t.addEventListener('click', show);
}

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
    showBattery(s);
    el('mode').textContent = s.armed ? 'ARMED' : fallen ? 'FALLEN' : 'DISARMED';
    el('mode').className = 'chip' + (s.armed ? ' armed' : fallen ? ' fallen' : '');
    el('why').textContent = s.armed ? '' : (s.reason || '');
    armedNow = s.armed;
    const timed = s.armed && s.arm_left_ms >= 0 && s.arm_ms > 0;
    el('left').textContent = timed ? (s.arm_left_ms / 1000).toFixed(1) + ' s' : '';
    el('leftBar').style.width = timed ? (100 * s.arm_left_ms / s.arm_ms) + '%' : '0';
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

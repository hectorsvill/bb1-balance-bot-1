// page.h - the web page, stored in the firmware as one string.
//
// Plain HTML + CSS + JavaScript, no libraries and nothing loaded from the
// internet, so it works when your phone is on BB1's own network.
// R"PAGE( ... )PAGE" is a C++ "raw string": everything between the markers is
// taken exactly as written, so the HTML needs no escaping.
//
// Memory: the string is "const", so it stays in flash (about 35 KB of 2 MB)
// and costs no RAM. The browser does all the layout and drawing, not the Pico.
//
// How it works: every 50 ms the page asks GET /state and updates the status
// bar, the numbers and the charts. Buttons send POSTs (see web.h for the list).
// While Record or Watch is on, one loop pulls every 5 ms record with
// GET /log?since=N for the recording and the log table.
//
// Layout, top to bottom:
//   status bar (always visible): connection, battery, mode, STOP
//   Balance: what BB1 is doing (auto-arm state), ARM, STOP. Gains are on /tune.
//   Drive: a joystick pad (and arrow keys / WASD) that drives BB1 while it balances.
//   then the developer panels, folded until you open them: PID, Pitch, Log,
//   Live values, Motor test, Sensors, Pico memory, Network. Click a panel's
//   title to fold it; the arrows move it. Both are remembered by this browser
//   (localStorage).
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
  /* ---------- colours and basics ---------- */
  :root {
    --bg:#0e1116; --panel:#161b22; --panel2:#1c222b; --line:#262d37; --text:#e6edf3; --muted:#8b96a5;
    --blue:#4ea1ff; --orange:#f0a03c; --green:#3fb97a; --red:#f05252; --purple:#b58cff; --teal:#3cc8c8;
    --radius:12px;
  }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.45 -apple-system, system-ui, "Segoe UI", sans-serif; }
  .wrap { max-width:980px; margin:0 auto; padding:0 16px; }
  .muted { color:var(--muted); }
  .small { font-size:13px; }
  .num-font { font-variant-numeric:tabular-nums; }

  /* ---------- status bar: always on screen ---------- */
  .bar-top { position:sticky; top:0; z-index:5; background:rgba(14,17,22,0.92); backdrop-filter:blur(8px);
             border-bottom:1px solid var(--line); }
  .bar-top .wrap { display:flex; align-items:center; gap:12px; padding-top:10px; padding-bottom:10px; flex-wrap:wrap; }
  .brand { font-weight:800; font-size:18px; letter-spacing:0.5px; }
  .conn { display:flex; align-items:center; gap:6px; font-size:13px; color:var(--muted); min-width:0; }
  .dot { width:9px; height:9px; border-radius:50%; background:var(--muted); flex:none; }
  .dot.ok { background:var(--green); } .dot.bad { background:var(--red); } .dot.mock { background:var(--orange); }
  .spacer { flex:1; }
  .chip { padding:4px 10px; border-radius:999px; font-weight:700; font-size:13px; background:var(--panel2); color:var(--muted); }
  .chip.armed { background:rgba(63,185,122,0.18); color:var(--green); }
  .chip.fallen { background:rgba(240,82,82,0.18); color:var(--red); }
  .chip.test { background:rgba(240,160,60,0.18); color:var(--orange); }
  .chip.batt-ok { background:rgba(63,185,122,0.18); color:var(--green); }
  .chip.batt-warn { background:rgba(240,160,60,0.18); color:var(--orange); }
  .chip.batt-low { background:rgba(240,82,82,0.18); color:var(--red); }
  .countdown { font-weight:800; font-size:20px; color:var(--green); min-width:4.5ch; text-align:right; }
  .timebar { height:3px; background:transparent; }
  .timebar i { display:block; height:100%; width:0; background:var(--green); transition:width 0.1s linear; }

  /* ---------- panels ---------- */
  .stack { display:flex; flex-direction:column; gap:14px; padding-top:14px; padding-bottom:40px; }
  .panel { background:var(--panel); border:1px solid var(--line); border-radius:var(--radius); min-width:0; }
  .head { display:flex; align-items:center; gap:8px; padding:12px 14px; cursor:pointer; user-select:none; }
  .head h2 { margin:0; font-size:15px; font-weight:700; flex:1; }
  .head .fold { color:var(--muted); font-size:12px; transition:transform 0.15s; }
  .panel.folded .fold { transform:rotate(-90deg); }
  .panel.folded .body { display:none; }
  .move button { background:none; color:var(--muted); padding:2px 6px; font-size:12px; }
  .move button:hover { color:var(--text); background:var(--panel2); }
  .body { padding:0 14px 14px; overflow-x:auto; }
  .sub { font-size:12px; font-weight:700; color:var(--muted); text-transform:uppercase; letter-spacing:0.6px; margin:14px 0 8px; }

  /* ---------- controls ---------- */
  button, input, select { font:inherit; border-radius:8px; }
  button { border:none; padding:9px 14px; cursor:pointer; background:var(--panel2); color:var(--text); font-weight:600; }
  button:hover { filter:brightness(1.15); }
  button:disabled { opacity:0.4; cursor:default; filter:none; }
  button.primary { background:var(--blue); color:#fff; }
  button.go { background:var(--green); color:#06210f; font-weight:800; }
  button.stop { background:var(--red); color:#fff; font-weight:800; }
  button.attention { background:var(--orange); color:#1d1206; }
  button.big { padding:12px 22px; font-size:17px; }
  input, select { background:#0b0e13; color:var(--text); border:1px solid var(--line); padding:8px 10px; }
  input:focus, select:focus { outline:none; border-color:var(--blue); }
  input.dirty { border-color:var(--orange); box-shadow:0 0 0 1px var(--orange); }
  .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  .row + .row { margin-top:8px; }
  .grow { flex:1; min-width:160px; }
  .msg { font-size:13px; color:var(--muted); margin-top:8px; min-height:1.2em; }
  .warn { color:var(--orange); }

  /* Number fields: the label above, the box, the unit below. */
  .fields { display:grid; grid-template-columns:repeat(auto-fill, minmax(100px, 1fr)); gap:10px; }
  .field { display:flex; flex-direction:column; gap:4px; background:var(--panel2); border-radius:10px; padding:8px 10px; }
  .field .name { font-size:13px; font-weight:700; }
  .field input { width:100%; }
  .field .unit { font-size:11px; color:var(--muted); }

  /* Read-only numbers as tiles. */
  .tiles { display:grid; grid-template-columns:repeat(auto-fill, minmax(140px, 1fr)); gap:10px; }
  /* Phones: keep the status bar on one line (the dot still shows the connection). */
  @media (max-width:520px) { #conn { display:none; } .tiles { grid-template-columns:1fr 1fr; } }
  .tile { background:var(--panel2); border-radius:10px; padding:8px 10px; min-width:0; }
  .tile .name { font-size:12px; color:var(--muted); }
  .tile .val { font-size:19px; font-weight:700; font-variant-numeric:tabular-nums; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .tile .val small { font-size:12px; color:var(--muted); font-weight:400; }
  .swatch { display:inline-block; width:10px; height:3px; border-radius:2px; vertical-align:middle; margin-right:6px; }

  /* Live values as a list (the original look): name on the left, value on the right. */
  table.list { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; white-space:nowrap; }
  table.list td { padding:5px 12px 5px 0; border-bottom:1px solid var(--line); }
  table.list td:last-child { text-align:right; padding-right:0; }
  .viewtoggle { display:inline-flex; background:var(--panel2); border-radius:8px; padding:2px; margin-bottom:10px; }
  .viewtoggle button { padding:5px 12px; font-size:13px; background:none; color:var(--muted); }
  .viewtoggle button.on { background:var(--line); color:var(--text); }

  /* Charts */
  canvas { width:100%; min-width:280px; height:200px; display:block; margin-top:10px; }
  .legend { display:flex; flex-wrap:wrap; gap:4px 14px; font-size:12px; color:var(--muted); margin-top:6px; }

  /* Arm area */
  .armbox { display:flex; align-items:center; gap:14px; flex-wrap:wrap; }
  .armstate { font-size:24px; font-weight:800; }
  .armstate.on { color:var(--green); } .armstate.fallen { color:var(--red); }
  .bigtime { font-size:34px; font-weight:800; color:var(--green); font-variant-numeric:tabular-nums; }
  .progress { height:6px; background:var(--panel2); border-radius:3px; overflow:hidden; margin-top:10px; }
  .progress i { display:block; height:100%; width:0; background:var(--green); transition:width 0.1s linear; }

  /* Log table: sticky header, scrolls both ways inside its box. */
  .logview { height:280px; overflow:auto; border:1px solid var(--line); border-radius:10px; background:#0b0e13; margin-top:10px; }
  .logview table { border-collapse:collapse; font:12px/1.5 ui-monospace, Menlo, monospace; white-space:nowrap; width:100%; }
  .logview th { position:sticky; top:0; background:var(--panel2); color:var(--muted); text-align:right; font-weight:600; padding:5px 10px; }
  .logview td { text-align:right; padding:2px 10px; border-top:1px solid #151a21; }
  .logview tr.armed td { background:rgba(63,185,122,0.07); }
  .logview .empty { padding:30px; text-align:center; color:var(--muted); font-family:inherit; }
  .recdot { display:inline-block; width:10px; height:10px; border-radius:50%; background:var(--red); margin-right:6px;
            animation:blink 1s infinite; vertical-align:middle; }
  @keyframes blink { 50% { opacity:0.2; } }

  /* Drive pad: a square you press and drag, like a stick. Up = forward, right = turn right. */
  .drivebox { display:flex; gap:16px; flex-wrap:wrap; align-items:flex-start; }
  .pad { position:relative; width:220px; height:220px; flex:none; border-radius:16px; background:#0b0e13;
         border:1px solid var(--line); touch-action:none; user-select:none; cursor:grab; }
  .pad.off { opacity:0.4; cursor:not-allowed; }
  .pad.on { border-color:var(--green); cursor:grabbing; }
  .pad::before, .pad::after { content:''; position:absolute; background:var(--line); }
  .pad::before { left:50%; top:10px; bottom:10px; width:1px; } .pad::after { top:50%; left:10px; right:10px; height:1px; }
  .knob { position:absolute; left:50%; top:50%; width:56px; height:56px; margin:-28px 0 0 -28px; border-radius:50%;
          background:var(--panel2); border:2px solid var(--muted); pointer-events:none; z-index:1; }
  .pad.on .knob { background:rgba(63,185,122,0.25); border-color:var(--green); }
  .driveside { flex:1; min-width:200px; }
  .driveside input[type=range] { width:100%; padding:0; }
  .keys { font-size:12px; color:var(--muted); }
  .keys b { display:inline-block; min-width:22px; padding:1px 5px; border:1px solid var(--line); border-radius:4px; text-align:center; color:var(--text); }

  /* Memory bars */
  .meter { height:8px; background:#0b0e13; border-radius:4px; overflow:hidden; margin-top:6px; }
  .meter i { display:block; height:100%; background:var(--blue); }

  /* Hints: dotted underline; hover (or tap on a phone) to read. */
  .hinted { text-decoration:underline dotted var(--muted); text-underline-offset:3px; cursor:help; }
  #hintBox { position:fixed; max-width:min(360px, calc(100vw - 32px)); padding:8px 12px; pointer-events:none; z-index:9;
             background:#2a313c; border:1px solid #3a4350; border-radius:8px; font-size:13px; display:none;
             box-shadow:0 6px 20px rgba(0,0,0,0.4); }
</style>
</head>
<body>

<!-- ===================== status bar ===================== -->
<div class="bar-top">
  <div class="wrap">
    <span class="brand">BB1</span>
    <span class="conn"><span class="dot" id="connDot"></span><span id="conn">connecting...</span></span>
    <span class="spacer"></span>
    <span class="chip" id="batt" title="battery voltage">batt --</span>
    <span class="chip" id="modeChip">--</span>
    <button class="stop" id="stopBtn" onclick="stopAll()">STOP</button>
  </div>
</div>

<div class="wrap stack" id="stack">

  <!-- ===================== Balance ===================== -->
  <!-- BB1 balances by itself (auto_arm.h): this panel only says what it's doing,
       and gives you ARM and STOP. Gains and timed test runs live on /tune. -->
  <div class="panel" id="p-balance">
    <div class="head"><span class="fold">&#9660;</span><h2>Balance</h2></div>
    <div class="body">
      <div class="armbox">
        <span class="armstate" id="armState">--</span>
        <span class="msg" id="armReason" style="margin:0"></span>
      </div>
      <div class="row" style="margin-top:12px">
        <button class="go big" id="armBtn" onclick="arm()">ARM</button>
        <button class="stop big" id="disarmBtn" onclick="disarm()">STOP</button>
        <span class="spacer"></span>
        <a href="/tune" style="color:var(--blue)">Tune gains and test runs &rarr;</a>
      </div>
      <div class="msg">BB1 starts balancing by itself when it's upright and still for 1 s, and again after you stand it back up from a fall. STOP turns that off until you press ARM. Laying it down stops the motors. Space bar = STOP.</div>
    </div>
  </div>

  <!-- ===================== Drive ===================== -->
  <!-- Milestone 2. The pad sends POST /drive every 100 ms while you hold it,
       and POST /drive/stop when you let go. BB1 stops by itself if the
       commands stop coming (500 ms), so a dropped Wi-Fi can't leave it driving. -->
  <div class="panel" id="p-drive">
    <div class="head"><span class="fold">&#9660;</span><h2>Drive</h2></div>
    <div class="body">
      <div class="drivebox">
        <div>
          <div class="pad off" id="pad"><div class="knob" id="knob"></div></div>
          <div class="small muted" style="margin-top:6px"><span id="h-pad">How the pad works</span></div>
        </div>
        <div class="driveside">
          <div class="tiles">
            <div class="tile"><div class="name" id="h-dask">asked</div><div class="val" id="driveAsk">--</div></div>
            <div class="tile"><div class="name" id="h-dnow">target now</div><div class="val" id="driveNow">--</div></div>
            <div class="tile"><div class="name" id="h-dwheels">wheels</div><div class="val" id="driveWheels">--</div></div>
          </div>
          <label class="field" style="margin-top:10px"><span class="name">Max speed <span id="driveMaxShow">50%</span></span>
            <input type="range" id="driveMax" min="10" max="100" step="5" value="50"></label>
          <div class="msg" id="driveMsg">Arm first: driving works only while BB1 balances.</div>
          <div class="keys" id="h-keys">Keys: <b>&#8593;</b><b>&#8595;</b><b>&#8592;</b><b>&#8594;</b> or <b>W</b><b>A</b><b>S</b><b>D</b> while held. <b>Space</b> = STOP.</div>
        </div>
      </div>
    </div>
  </div>

  <!-- ===================== PID (view only) ===================== -->
  <div class="panel" id="p-pid">
    <div class="head"><span class="fold">&#9660;</span><h2>PID</h2></div>
    <div class="body">
      <div class="tiles">
        <div class="tile"><div class="name" id="h-err">Error (pitch - trim)</div><div class="val" id="pidErr">--</div></div>
        <div class="tile"><div class="name" id="h-p"><span class="swatch" style="background:var(--blue)"></span>P</div><div class="val" id="pidP">--</div></div>
        <div class="tile"><div class="name" id="h-i"><span class="swatch" style="background:var(--purple)"></span>I</div><div class="val" id="pidI">--</div></div>
        <div class="tile"><div class="name" id="h-d"><span class="swatch" style="background:var(--teal)"></span>D</div><div class="val" id="pidD">--</div></div>
        <div class="tile"><div class="name" id="h-out"><span class="swatch" style="background:var(--text)"></span>Output</div><div class="val" id="pidOut">--</div></div>
      </div>
      <canvas id="pidChart"></canvas>
      <div class="legend">
        <span><span class="swatch" style="background:var(--blue)"></span>P</span>
        <span><span class="swatch" style="background:var(--purple)"></span>I</span>
        <span><span class="swatch" style="background:var(--teal)"></span>D</span>
        <span><span class="swatch" style="background:var(--text)"></span>output to the wheels</span>
        <span><span class="swatch" style="background:var(--red)"></span>&plusmn; cap</span>
        <span>PWM, last 10 s. All zero while disarmed.</span>
      </div>
    </div>
  </div>

  <!-- ===================== Pitch ===================== -->
  <div class="panel" id="p-pitch">
    <div class="head"><span class="fold">&#9660;</span><h2>Pitch</h2></div>
    <div class="body">
      <div class="tiles">
        <div class="tile"><div class="name">Pitch (forward +)</div><div class="val"><span id="pitch">--</span> <small>deg</small></div></div>
        <div class="tile"><div class="name" id="h-rate">Pitch rate</div><div class="val"><span id="rate">--</span> <small>deg/s</small></div></div>
        <div class="tile"><div class="name" id="h-accel">Accel-only pitch</div><div class="val"><span id="accel">--</span> <small>deg</small></div></div>
      </div>
      <canvas id="chart"></canvas>
      <div class="legend">
        <span><span class="swatch" style="background:var(--blue)"></span>pitch (filtered)</span>
        <span><span class="swatch" style="background:var(--orange)"></span>accel only</span>
        <span><span class="swatch" style="background:var(--green)"></span>trim</span>
        <span>last 10 s</span>
      </div>
    </div>
  </div>

  <!-- ===================== Log ===================== -->
  <div class="panel" id="p-log">
    <div class="head"><span class="fold">&#9660;</span><h2>Log</h2></div>
    <div class="body">
      <div class="row">
        <input class="grow" id="note" placeholder="Note for this run, e.g. hand-held tilt" autocomplete="off">
      </div>
      <div class="row">
        <button class="primary" id="recBtn" onclick="toggleRecord()">&#9679; Record</button>
        <button id="dlBtn" onclick="downloadRecording()" disabled>Download</button>
        <button id="saveLastBtn" onclick="saveLast()">Save last 10 s</button>
      </div>
      <div class="msg" id="logMsg">Record keeps every 5 ms record and shows them below as they arrive.</div>

      <div class="row" style="margin-top:12px">
        <button id="liveBtn" onclick="toggleLive()">Watch</button>
        <select id="liveEvery">
          <option value="1">every record</option>
          <option value="10" selected>every 10th</option>
          <option value="40">every 40th</option>
        </select>
        <label class="row small muted" style="gap:6px"><input type="checkbox" id="allCols"> all columns</label>
        <button onclick="clearLive()">Clear</button>
        <span class="small muted" id="liveMsg"></span>
      </div>
      <div class="logview" id="logView"><div class="empty">Records show here while recording, or press Watch.</div></div>
    </div>
  </div>

  <!-- ===================== Live values ===================== -->
  <div class="panel" id="p-values">
    <div class="head"><span class="fold">&#9660;</span><h2>Live values</h2></div>
    <div class="body">
      <div class="viewtoggle"><button id="viewList" onclick="setValuesView('list')">List</button><button id="viewTiles" onclick="setValuesView('tiles')">Tiles</button></div>
      <!-- List view: every value, one row each. Value cells are filled by name (data-v) -->
      <table class="list" id="valuesList">
        <tr><td id="h-rate2">Pitch rate</td><td data-v="rate">--</td></tr>
        <tr><td id="h-accel2">Accel-only pitch</td><td data-v="accel">--</td></tr>
        <tr><td id="h-work">Loop work (budget 5000 us)</td><td data-v="work">--</td></tr>
        <tr><td id="h-tick">Tick</td><td data-v="tick">--</td></tr>
        <tr><td id="h-bias2">Gyro bias x / y / z (deg/s)</td><td data-v="bias">--</td></tr>
        <tr><td id="h-alpha2">Filter alpha</td><td data-v="alpha">--</td></tr>
        <tr><td>IMU</td><td data-v="imu">--</td></tr>
        <tr><td>Uptime</td><td data-v="uptime">--</td></tr>
        <tr><td id="h-enc">Encoders left / right (counts, forward +)</td><td data-v="enc">--</td></tr>
        <tr><td id="h-speed">Wheel speed left / right (counts/s)</td><td data-v="speed">--</td></tr>
        <tr><td id="h-pwm">Motor PWM left / right</td><td data-v="pwm">--</td></tr>
        <tr><td id="h-pid">PID terms P / I / D (PWM)</td><td data-v="pid">--</td></tr>
        <tr><td id="h-web">Page delay (ms)</td><td data-v="webAge">--</td></tr>
      </table>
      <!-- Tiles view: the same values as cards -->
      <div class="tiles" id="valuesTiles">
        <div class="tile"><div class="name">Motor PWM L / R</div><div class="val" data-v="pwm">--</div></div>
        <div class="tile"><div class="name">Wheel speed L / R</div><div class="val" data-v="speed">--</div></div>
        <div class="tile"><div class="name">Encoders L / R</div><div class="val" data-v="enc">--</div></div>
        <div class="tile"><div class="name">PID P / I / D</div><div class="val" data-v="pid">--</div></div>
        <div class="tile"><div class="name">Loop work (of 5000 us)</div><div class="val" data-v="work">--</div></div>
        <div class="tile"><div class="name">Page delay (ms)</div><div class="val" data-v="webAge">--</div></div>
        <div class="tile"><div class="name">Tick</div><div class="val" data-v="tick">--</div></div>
        <div class="tile"><div class="name">IMU</div><div class="val" data-v="imu">--</div></div>
        <div class="tile"><div class="name">Uptime</div><div class="val" data-v="uptime">--</div></div>
      </div>
    </div>
  </div>

  <!-- ===================== Motor test ===================== -->
  <div class="panel" id="p-motors">
    <div class="head"><span class="fold">&#9660;</span><h2>Motor test</h2></div>
    <div class="body">
      <div class="msg warn" style="margin-top:0">Wheels off the table first. Every test stops by itself.</div>
      <div class="fields" style="margin-top:8px">
        <label class="field"><span class="name">Left</span><input id="mL" type="number" value="100" min="-150" max="150"><span class="unit">PWM, forward +</span></label>
        <label class="field"><span class="name">Right</span><input id="mR" type="number" value="100" min="-150" max="150"><span class="unit">PWM, forward +</span></label>
        <label class="field"><span class="name">Time</span>
          <select id="mMs"><option value="500">0.5 s</option><option value="1000">1 s</option>
            <option value="2000" selected>2 s</option><option value="3000">3 s</option></select>
          <span class="unit">stops by itself</span></label>
      </div>
      <div class="row" style="margin-top:12px">
        <button class="primary" onclick="motorTest()">Run</button>
        <button class="stop" onclick="stopAll()">STOP</button>
      </div>
      <div class="msg" id="motorMsg">-150..150. 100 is about 40% power.</div>
    </div>
  </div>

  <!-- ===================== Sensors ===================== -->
  <div class="panel" id="p-actions">
    <div class="head"><span class="fold">&#9660;</span><h2>Sensors</h2></div>
    <div class="body">
      <div class="tiles">
        <div class="tile"><div class="name" id="h-bias">Gyro bias x / y / z</div><div class="val" id="bias">--</div></div>
        <div class="tile"><div class="name" id="h-alpha">Filter alpha</div><div class="val" id="alphaNow">--</div></div>
      </div>
      <div class="row" style="margin-top:12px">
        <button class="primary" id="calBtn" onclick="post('/cal', null, 'calMsg')">Calibrate gyro</button>
        <span class="small muted">lay the robot still first</span>
      </div>
      <div class="msg" id="calMsg"></div>
      <div class="row">
        <input id="alpha" type="number" step="0.001" min="0.9" max="0.999" placeholder="alpha, e.g. 0.98" style="width:170px">
        <button onclick="post('/alpha', {value: el('alpha').value}, 'alphaMsg')">Set alpha</button>
      </div>
      <div class="msg" id="alphaMsg"></div>
    </div>
  </div>

  <!-- ===================== Pico memory ===================== -->
  <div class="panel" id="p-sys">
    <div class="head"><span class="fold">&#9660;</span><h2>Pico memory</h2></div>
    <div class="body">
      <div class="tiles">
        <div class="tile"><div class="name" id="h-ram">RAM used, of 256 KB</div><div class="val" id="ramUsed">--</div><div class="meter"><i id="ramBar"></i></div></div>
        <div class="tile"><div class="name" id="h-heap">Heap free</div><div class="val" id="heapFree">--</div></div>
        <div class="tile"><div class="name" id="h-flash">Flash used, of 2 MB</div><div class="val" id="flashUsed">--</div><div class="meter"><i id="flashBar"></i></div></div>
        <div class="tile"><div class="name">Log buffer</div><div class="val" id="logBuf">--</div></div>
        <div class="tile"><div class="name">This page</div><div class="val" id="pageSize">--</div></div>
        <div class="tile"><div class="name">CPU clock</div><div class="val" id="cpu">--</div></div>
      </div>
    </div>
  </div>

  <!-- ===================== Network ===================== -->
  <div class="panel" id="p-net">
    <div class="head"><span class="fold">&#9660;</span><h2>Network</h2></div>
    <div class="body">
      <div class="msg" id="netNow" style="margin-top:0"></div>
      <div class="row"><input class="grow" id="ssid" placeholder="home Wi-Fi name" autocomplete="off"></div>
      <div class="row"><input class="grow" id="pass" type="password" placeholder="home Wi-Fi password"></div>
      <div class="row">
        <button class="primary" onclick="saveWifi()">Save and join</button>
        <button onclick="forgetWifi()">Use own network</button>
      </div>
      <div class="msg" id="netMsg"></div>
    </div>
  </div>

</div>

<div id="hintBox"></div>

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
const sleep = ms => new Promise(r => setTimeout(r, ms));
const store = {                                  // localStorage can be blocked: never let that break the page
  get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} },
};

// ---------- hints ----------
//
// One place for every explanation on the page. Each key is an element id.
// Hover over a hinted word (dotted underline) and the hint appears next to it;
// move away and it disappears. Phones can't hover, so a tap shows it for 5 s.
// For a box inside a <label>, the whole label shows the hint.

const HINTS = {
  // Status bar
  stopBtn: 'Stops everything now: disarms the balance loop and ends any motor test. The space bar does the same.',
  modeChip: 'What BB1 is doing: DISARMED (motors off), ARMED (balancing), FALLEN (leaned past 45 deg, motors off), MOTOR TEST.',
  // Balance
  armBtn: 'Start balancing now if it is within 10 deg of upright, and turn auto-arm back on after a STOP. No time limit, and it keeps going if you close this page.',
  disarmBtn: 'Stop balancing now: brakes both wheels and puts the motor driver to sleep. Auto-arm stays off until you press ARM.',
  // Drive
  'h-pad': 'Press and drag to drive: up = forward, down = back, left / right = turn. Let go and BB1 slows to a stop and keeps balancing. Works only while it balances.',
  driveMax: 'Scales the pad and the keys. 100% = the firmware limit (1 wheel turn per second forward, 1 turn/s of turn). Start low.',
  'h-dask': 'What this page is asking for: speed in wheel turns per second (forward +) and turn (right +).',
  'h-dnow': 'What BB1 is aiming for right now. It ramps toward what you ask so a jab on the pad cannot tip it over.',
  'h-dwheels': 'How fast the wheels are really turning, average of both, wheel turns per second. Forward +.',
  'h-keys': 'Arrow keys or WASD drive while held, at the Max speed above. Clicking the page first may be needed so it gets the keys.',
  // PID
  'h-err': 'How far past the balance point it leans, in degrees. Forward +. This is what the PID works on.',
  'h-p': 'P = Kp x error. Pushes harder the further it leans.',
  'h-i': 'I = Ki x (error added up over time). Fixes a lean that lasts. 0 while Ki is 0.',
  'h-d': 'D = Kd x pitch rate. Reacts to how fast it is tipping; damps the swinging.',
  'h-out': 'P + I + D (+ deadband), clipped to the cap: the PWM actually sent to both wheels.',
  // Pitch
  'h-rate': 'How fast it is tipping, deg/s, from the gyro. Forward +.',
  'h-accel': 'Pitch from the accelerometer alone: right on average, but jumpy when the robot moves or is bumped.',
  // Log
  note: 'Saved in the file and its name, so you can tell runs apart later.',
  recBtn: 'Record keeps every 5 ms record until you press Stop, and shows them in the table as they arrive. Then Download saves a CSV.',
  saveLastBtn: 'Download the last 10 s BB1 still has in memory, even if you were not recording.',
  liveBtn: 'Show the records in the table without recording. Pause freezes it so you can read.',
  liveEvery: 'How many records to show in the table. Recording always keeps every record.',
  allCols: 'Show every column, as in the downloaded file. Off: only the most useful ones.',
  // Live values
  'h-pwm': 'What is being sent to each motor right now, -255..255, forward +.',
  'h-speed': 'Wheel speed over the last 50 ms, counts/s. About 1750 at PWM 100 with the wheels in the air.',
  'h-enc': 'Encoder counts since startup, forward +. About 520 per wheel turn.',
  'h-work': 'How long one 5 ms tick took. Must stay well under 5000 us or the loop runs late.',
  'h-web': 'How long since this page last heard from BB1. Over 1000 ms while armed from the page = it disarms.',
  'h-tick': 'Counts up 200 times a second. Every log row has one; a gap means lost records.',
  'h-rate2': 'How fast it is tipping, deg/s, from the gyro. Forward +.',
  'h-accel2': 'Pitch from the accelerometer alone: right on average, but jumpy when the robot moves or is bumped.',
  'h-bias2': 'What the gyro reads while still (deg/s), subtracted from every reading. Measured at startup and by Calibrate gyro.',
  'h-alpha2': 'The complementary filter weight in use (see Set alpha in Sensors).',
  'h-pid': 'The three parts of the balance output: P from the lean, I from lasting lean, D from tipping speed. PWM = P + I + D, clipped to the cap.',
  // Motor test
  mL: 'Left wheel PWM, -150..150. Positive = roll forward. 100 is about 40% power.',
  mR: 'Right wheel PWM, -150..150. Positive = roll forward.',
  mMs: 'How long the test runs. It always stops by itself.',
  // Sensors
  'h-bias': 'What the gyro reads while still (deg/s), subtracted from every reading. Measured at startup and by Calibrate gyro.',
  'h-alpha': 'The complementary filter weight in use (see Set alpha).',
  calBtn: 'Measures the gyro bias for 2 s. Lay BB1 still first. Disarms if armed.',
  alpha: 'Filter weight 0.90..0.999: how much the angle trusts the gyro over the accelerometer each tick. Higher = smoother, slower to correct drift.',
  // Memory
  'h-ram': 'RAM used: fixed globals (mostly the 10 s log buffer) plus the heap in use.',
  'h-heap': 'RAM left for Wi-Fi and web requests. Keep it above about 20 KB.',
  'h-flash': 'Program size in the 2 MB flash chip, including this page.',
};

let hintTimer;
function showHint(text, anchor) {
  const box = el('hintBox');
  box.textContent = text;
  box.style.display = 'block';
  // Just below the hovered word (or above it near the bottom), inside the window.
  const r = anchor.getBoundingClientRect();
  const left = Math.min(r.left, window.innerWidth - box.offsetWidth - 16);
  const below = r.bottom + 6 + box.offsetHeight < window.innerHeight;
  box.style.left = Math.max(16, left) + 'px';
  box.style.top = (below ? r.bottom + 6 : r.top - box.offsetHeight - 6) + 'px';
  clearTimeout(hintTimer);
}

function hideHint() {
  clearTimeout(hintTimer);
  el('hintBox').style.display = 'none';
}

for (const [id, text] of Object.entries(HINTS)) {
  const target = el(id);
  if (!target) continue;
  const label = target.closest('label');
  const word = label ? (label.querySelector('.name') || label) : target;   // what gets the dotted underline
  const area = label || target;                                           // what you hover
  word.classList.add('hinted');
  target.setAttribute('aria-description', text);
  area.addEventListener('mouseenter', () => showHint(text, area));
  area.addEventListener('mouseleave', hideHint);
  area.addEventListener('touchstart', () => { showHint(text, area); hintTimer = setTimeout(hideHint, 5000); }, { passive: true });
}
window.addEventListener('scroll', hideHint, { passive: true });

// ---------- panels: fold and move ----------
//
// Click a panel's title to fold it away. The arrows move it up or down. Both
// are remembered by this browser so the page looks the same after a reload.

const stack = el('stack');
// Everything but Balance is for development, so it starts folded.
const DEV_PANELS = ['p-pid', 'p-pitch', 'p-log', 'p-values', 'p-motors', 'p-actions', 'p-sys', 'p-net'];
const folded = new Set(store.get('bb1.folded2', DEV_PANELS));

function saveOrder() { store.set('bb1.panelOrder', [...stack.children].map(p => p.id)); }

function movePanel(panel, dir) {
  if (dir < 0 && panel.previousElementSibling) stack.insertBefore(panel, panel.previousElementSibling);
  if (dir > 0 && panel.nextElementSibling) stack.insertBefore(panel.nextElementSibling, panel);
  saveOrder();
  panel.scrollIntoView({ block: 'nearest' });
}

for (const panel of stack.children) {
  const head = panel.querySelector('.head');
  const box = document.createElement('span');
  box.className = 'move';
  box.innerHTML = '<button title="move up">&#9650;</button><button title="move down">&#9660;</button>';
  box.children[0].onclick = e => { e.stopPropagation(); movePanel(panel, -1); };
  box.children[1].onclick = e => { e.stopPropagation(); movePanel(panel, +1); };
  head.appendChild(box);
  head.addEventListener('click', () => {
    panel.classList.toggle('folded');
    panel.classList.contains('folded') ? folded.add(panel.id) : folded.delete(panel.id);
    store.set('bb1.folded2', [...folded]);
  });
  if (folded.has(panel.id)) panel.classList.add('folded');
}
for (const id of store.get('bb1.panelOrder', [])) if (el(id)) stack.appendChild(el(id));

// ---------- talking to the robot ----------

// POST with optional form fields; show the robot's answer in a message line.
async function post(url, fields, msgId) {
  const body = new URLSearchParams(fields || {});
  try {
    const res = await fetch(url, { method: 'POST', body });
    const text = await res.text();
    if (msgId) el(msgId).textContent = text;
  } catch (e) {
    if (msgId) el(msgId).textContent = 'no answer from the robot';
  }
}

// STOP (status bar, motor test panel, space bar): ends a motor test and disarms.
function stopAll() { post('/motor/stop', null, 'motorMsg'); }

document.addEventListener('keydown', e => {
  // Works even right after clicking a button (which keeps the focus): preventDefault
  // stops the space bar from "clicking" that button again (e.g. ARM).
  if (e.code === 'Space' && !['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) {
    e.preventDefault();
    stopAll();
  }
});

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

// ---------- balance ----------

// ARM from this page has no time limit and no heartbeat: BB1 keeps balancing
// if you close the page (like auto-arm). Timed test runs are on /tune.
function arm() { post('/arm', { seconds: 0, heartbeat: 0 }, 'armReason'); }
function disarm() { post('/disarm', null, 'armReason'); }

// The status bar and the Balance panel: mode, countdown, reason.
function showMode(s) {
  const mode = s.armed ? 'ARMED' : s.test_running ? 'MOTOR TEST' : s.mode === 5 ? 'FALLEN' : 'DISARMED';
  showBattery(s);
  el('modeChip').textContent = mode;
  el('modeChip').className = 'chip' + (s.armed ? ' armed' : s.test_running ? ' test' : s.mode === 5 ? ' fallen' : '');
  el('armState').textContent = s.armed ? 'Balancing' : s.mode === 5 ? 'Fallen' : 'Not balancing';
  el('armState').className = 'armstate' + (s.armed ? ' on' : s.mode === 5 ? ' fallen' : '');

  // Disarmed: why it last stopped, and what auto-arm is waiting for.
  const encWarn = s.enc_fault ? ' ENCODER FAULT: the ' + (s.enc_fault === 1 ? 'left' : 'right') + ' wheel is not counting (check its wires). Heading hold off, driving refused.' : '';
  el('armReason').textContent = (s.armed ? 'balancing' : [s.reason, s.auto_state && 'auto-arm ' + s.auto_state].filter(Boolean).join('. ')) + encWarn;
  el('armReason').className = 'msg' + (s.enc_fault ? ' warn' : '');
}

// ---------- drive (milestone 2) ----------
//
// The pad (or the keys) sets want.speed / want.turn, -1..1. A timer sends
// POST /drive every 100 ms while you hold it, scaled by Max speed and the
// firmware's limits (from /state). Each command lasts 500 ms on BB1, so if
// this page freezes, closes or loses Wi-Fi, BB1 stops by itself. Letting go
// sends POST /drive/stop at once. One request at a time: if BB1 is slow to
// answer, we skip a beat instead of queueing up old commands.

const want = { speed: 0, turn: 0, pad: false };
const keysDown = new Set();
let driveArmed = false, driveLimits = { max_speed: 1, max_turn: 1 }, driveBusy = false, driveWasOn = false;

el('driveMax').value = store.get('bb1.driveMax', 50);
function showDriveMax() { el('driveMaxShow').textContent = el('driveMax').value + '%'; }
el('driveMax').oninput = () => { store.set('bb1.driveMax', +el('driveMax').value); showDriveMax(); };
showDriveMax();

// Pad: the knob follows your finger, clamped to the circle. y up = forward.
const pad = el('pad'), knob = el('knob');
function padMove(e) {
  const r = pad.getBoundingClientRect(), half = r.width / 2 - 28;
  let x = (e.clientX - r.left - r.width / 2) / half, y = -(e.clientY - r.top - r.height / 2) / half;
  const len = Math.hypot(x, y);
  if (len > 1) { x /= len; y /= len; }
  want.speed = y; want.turn = x;
  knob.style.transform = 'translate(' + (x * half) + 'px,' + (-y * half) + 'px)';
}
function padRelease() {
  if (!want.pad) return;
  want.pad = false; want.speed = want.turn = 0;
  knob.style.transform = '';
  pad.classList.remove('on');
}
pad.addEventListener('pointerdown', e => {
  if (!driveArmed) { el('driveMsg').textContent = 'Arm first: driving works only while BB1 balances.'; return; }
  pad.setPointerCapture(e.pointerId);
  want.pad = true; pad.classList.add('on'); padMove(e);
});
pad.addEventListener('pointermove', e => { if (want.pad) padMove(e); });
pad.addEventListener('pointerup', padRelease);
pad.addEventListener('pointercancel', padRelease);

// Keys: arrows or WASD, held. Ignored while typing in a box.
const DRIVE_KEYS = { ArrowUp: 'f', KeyW: 'f', ArrowDown: 'b', KeyS: 'b', ArrowLeft: 'l', KeyA: 'l', ArrowRight: 'r', KeyD: 'r' };
document.addEventListener('keydown', e => {
  if (!DRIVE_KEYS[e.code] || ['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
  e.preventDefault();
  keysDown.add(DRIVE_KEYS[e.code]);
});
document.addEventListener('keyup', e => { if (DRIVE_KEYS[e.code]) keysDown.delete(DRIVE_KEYS[e.code]); });

// Anything that might leave a key or finger "stuck" stops driving.
function stopDriving() { keysDown.clear(); padRelease(); }
window.addEventListener('blur', stopDriving);
document.addEventListener('visibilitychange', () => { if (document.hidden) { stopDriving(); navigator.sendBeacon('/drive/stop'); } });

function driveAsk() {
  let speed = want.speed, turn = want.turn;
  if (!want.pad && keysDown.size) {
    speed = (keysDown.has('f') ? 1 : 0) - (keysDown.has('b') ? 1 : 0);
    turn  = (keysDown.has('r') ? 1 : 0) - (keysDown.has('l') ? 1 : 0);
  }
  const k = el('driveMax').value / 100;
  return { speed: +(speed * k * driveLimits.max_speed).toFixed(2), turn: +(turn * k * driveLimits.max_turn).toFixed(2) };
}

setInterval(async () => {
  const ask = driveAsk();
  const on = driveArmed && (ask.speed !== 0 || ask.turn !== 0);
  if (!on && !driveWasOn) return;                      // idle: send nothing
  if (driveBusy) return;                               // still waiting for the last answer
  driveBusy = true;
  try {
    const res = await fetch(on ? '/drive' : '/drive/stop', { method: 'POST', body: new URLSearchParams(on ? ask : {}),
                                                         signal: AbortSignal.timeout ? AbortSignal.timeout(400) : undefined });
    const j = await res.json();
    el('driveMsg').textContent = j.ok ? (on ? 'driving' : 'stopped driving, still balancing') : j.error;
    driveWasOn = on && j.ok;
  } catch (e) {
    el('driveMsg').textContent = 'no answer: BB1 stops by itself within 0.5 s';
  }
  driveBusy = false;
}, 100);

// From poll(): what BB1 reports.
function showDrive(s) {
  driveArmed = s.armed && !s.enc_fault;
  pad.classList.toggle('off', !s.armed || !!s.enc_fault);
  if (!s.armed) stopDriving();
  const d = s.drive;
  if (!d) return;
  driveLimits = d;
  const ask = driveAsk();
  el('driveAsk').innerHTML = ask.speed.toFixed(2) + ' / ' + ask.turn.toFixed(2) + ' <small>turns/s</small>';
  el('driveNow').innerHTML = d.speed_now.toFixed(2) + ' / ' + d.turn_now.toFixed(2);
  el('driveWheels').innerHTML = ((s.speed_l + s.speed_r) / 2 / 520).toFixed(2) + ' <small>turns/s</small>';
}

// ---------- pulling log records ----------
//
// The robot keeps the last 10 s of records. While Record or Watch is on,
// pumpLoop() asks for everything newer than what we have (/log?since=next)
// about four times a second and hands the new lines to both.

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
      el('logMsg').textContent = 'no answer from the robot, retrying...';
      await sleep(500);
    }
  }
  pumpRunning = false;
}

// ---------- recording ----------

const rec = { on: false, rows: [], lost: 0, meta: '', header: '', note: '', started: null };

function recStatus() {
  return rec.rows.length + ' records, ' + (rec.rows.length / 200).toFixed(1) + ' s' +
         (rec.lost ? ', ' + rec.lost + ' lost' : '');
}

function recordAdd(rows, lost) {
  rec.rows.push(...rows);
  rec.lost += lost;
  rec.header = logHeader;
  el('logMsg').innerHTML = '<span class="recdot"></span>Recording: ' + recStatus();
}

async function toggleRecord() {
  if (rec.on) {                                     // Stop
    rec.on = false;
    if (live.byRecord) { live.byRecord = false; setLive(false); }    // the table stops with the recording
    el('recBtn').innerHTML = '&#9679; Record';
    el('recBtn').className = 'primary';
    el('dlBtn').disabled = rec.rows.length === 0;
    el('dlBtn').className = rec.rows.length ? 'primary' : '';
    el('logMsg').textContent = 'Stopped: ' + recStatus() + '. Press Download to save it.';
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
  if (!live.on) { clearLive(); live.byRecord = true; setLive(true); }   // show what's being recorded
  el('recBtn').innerHTML = '&#9632; Stop';
  el('recBtn').className = 'stop';
  el('dlBtn').disabled = true;
  el('dlBtn').className = '';
  el('logMsg').innerHTML = '<span class="recdot"></span>Recording: ' + recStatus();
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
    el('logMsg').textContent = 'Saved the last ' + (rows.length / 200).toFixed(1) + ' s (' + rows.length + ' records).';
  } catch (e) {
    el('logMsg').textContent = 'no answer from the robot';
  }
}

// ---------- the log table ----------
//
// Shows the newest records as a table with a fixed header row. Only the last
// LIVE_ROWS are kept, so the page stays quick on a long run. By default it
// shows the most useful columns; "all columns" shows every one.

const LIVE_ROWS = 120;
const SHORT_COLUMNS = ['tick', 'mode', 'pitch', 'pitch_rate', 'p_term', 'i_term', 'd_term', 'pwm_l', 'pwm_r',
                       'speed_l', 'speed_r', 'reason', 'web_age_ms', 'batt_v'];
const live = { on: false, byRecord: false, rows: [], total: 0, lost: 0 };

function liveAdd(rows, lost) {
  const every = Number(el('liveEvery').value);
  for (const line of rows) {
    if (parseInt(line, 10) % every === 0) live.rows.push(line);     // the first column is the tick
  }
  live.total += rows.length;
  live.lost += lost;
  if (live.rows.length > LIVE_ROWS) live.rows.splice(0, live.rows.length - LIVE_ROWS);
  drawLogTable();
}

function drawLogTable() {
  const view = el('logView');
  if (!logHeader || !live.rows.length) return;
  const names = logHeader.split(',');
  const show = el('allCols').checked ? names.map((_, i) => i)
                                     : SHORT_COLUMNS.map(n => names.indexOf(n)).filter(i => i >= 0);
  const modeCol = names.indexOf('mode');
  const atBottom = view.scrollTop + view.clientHeight >= view.scrollHeight - 30;
  let html = '<table><thead><tr>' + show.map(i => '<th>' + names[i] + '</th>').join('') + '</tr></thead><tbody>';
  for (const line of live.rows) {
    const v = line.split(',');
    html += '<tr' + (v[modeCol] === '4' ? ' class="armed"' : '') + '>' + show.map(i => '<td>' + v[i] + '</td>').join('') + '</tr>';
  }
  view.innerHTML = html + '</tbody></table>';
  if (atBottom) view.scrollTop = view.scrollHeight;  // follow new rows, unless you scrolled up to read
  el('liveMsg').textContent = live.total + ' received' + (live.lost ? ', ' + live.lost + ' lost' : '');
}

function setLive(on) {
  live.on = on;
  el('liveBtn').textContent = on ? 'Pause' : 'Watch';
  if (on) startPump();
}

function toggleLive() {
  live.byRecord = false;           // you took over the table: Stop won't turn it off
  setLive(!live.on);
}

function clearLive() {
  live.rows = []; live.total = 0; live.lost = 0;
  el('logView').innerHTML = '<div class="empty">Records show here while recording, or press Watch.</div>';
  el('liveMsg').textContent = '';
}

el('allCols').addEventListener('change', drawLogTable);

// ---------- live values: list or tiles ----------
//
// The same values show in two layouts; the buttons flip between them and the
// choice is remembered. setValue() fills every element marked data-v="name".

function setValue(name, html) {
  for (const e of document.querySelectorAll('[data-v="' + name + '"]')) e.innerHTML = html;
}

function setValuesView(view) {
  el('valuesList').style.display = view === 'list' ? '' : 'none';
  el('valuesTiles').style.display = view === 'tiles' ? '' : 'none';
  el('viewList').classList.toggle('on', view === 'list');
  el('viewTiles').classList.toggle('on', view === 'tiles');
  store.set('bb1.valuesView', view);
}
setValuesView(store.get('bb1.valuesView', 'list'));

// ---------- Pico memory ----------

const kb = b => (b / 1024).toFixed(1) + ' KB';

async function pollSys() {
  try {
    const m = await (await fetch('/sys', { cache: 'no-store' })).json();
    const used = m.ram_static + m.heap_used;
    el('ramUsed').textContent = kb(used) + ' (' + (100 * used / m.ram_total).toFixed(0) + '%)';
    el('ramBar').style.width = (100 * used / m.ram_total) + '%';
    el('heapFree').textContent = kb(m.heap_total - m.heap_used);
    el('flashUsed').textContent = kb(m.flash_used) + ' (' + (100 * m.flash_used / m.flash_total).toFixed(0) + '%)';
    el('flashBar').style.width = (100 * m.flash_used / m.flash_total) + '%';
    el('logBuf').textContent = kb(m.log_buffer);
    el('pageSize').textContent = kb(m.page);
    el('cpu').textContent = m.cpu_mhz + ' MHz';
  } catch (e) {}
  setTimeout(pollSys, 2000);                           // memory changes slowly
}

// ---------- charts ----------
//
// One small function draws any of the line charts. history holds the last
// 10 s of samples; each series says which field to draw and in what colour.

const HISTORY_MS = 10000;
let history = [];          // { t: browser time in ms, pitch, accel, trim, p, i, d, out, cap }
const css = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function drawChart(id, series, opt) {
  const c = el(id);
  if (!c.clientWidth) return;                        // folded away: nothing to draw
  const dpr = window.devicePixelRatio || 1;          // sharp lines on retina screens
  c.width = c.clientWidth * dpr;
  c.height = c.clientHeight * dpr;
  const g = c.getContext('2d');
  g.scale(dpr, dpr);
  const w = c.clientWidth, h = c.clientHeight;

  // Vertical range: at least opt.min..opt.max, growing to fit the data, with
  // grid lines at a round step (1, 2 or 5 x 10^n) so the labels read well.
  let lo = opt.min, hi = opt.max;
  for (const p of history) for (const s of series) { lo = Math.min(lo, p[s.key]); hi = Math.max(hi, p[s.key]); }
  const raw = (hi - lo) / 5, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const every = [1, 2, 5, 10].map(m => m * mag).find(st => st >= raw);
  lo = Math.floor(lo / every) * every; hi = Math.ceil(hi / every) * every;
  const now = performance.now();
  const x = t => w - (now - t) / HISTORY_MS * w;
  const y = v => h - 4 - (v - lo) / (hi - lo) * (h - 8);

  // Grid lines, zero brighter, labels on the left.
  g.font = '11px system-ui'; g.fillStyle = css('--muted');
  for (let v = lo; v <= hi; v += every) {
    g.strokeStyle = v === 0 ? '#3a4452' : '#202731';
    g.lineWidth = 1;
    g.beginPath(); g.moveTo(0, y(v)); g.lineTo(w, y(v)); g.stroke();
    g.fillText(v + opt.unit, 4, y(v) < 14 ? y(v) + 12 : y(v) - 3);   // top label goes under its line
  }

  for (const s of series) {
    g.strokeStyle = s.color; g.lineWidth = s.width || 2; g.setLineDash(s.dash || []);
    g.beginPath();
    history.forEach((p, i) => i ? g.lineTo(x(p.t), y(p[s.key])) : g.moveTo(x(p.t), y(p[s.key])));
    g.stroke();
  }
  g.setLineDash([]);
}

function drawCharts() {
  drawChart('chart', [
    { key: 'trim', color: css('--green'), width: 1, dash: [4, 4] },
    { key: 'accel', color: css('--orange') },
    { key: 'pitch', color: css('--blue') },
  ], { min: -10, max: 10, unit: '°' });
  drawChart('pidChart', [
    { key: 'cap', color: css('--red'), width: 1, dash: [4, 4] },
    { key: 'ncap', color: css('--red'), width: 1, dash: [4, 4] },
    { key: 'p', color: css('--blue') },
    { key: 'i', color: css('--purple') },
    { key: 'd', color: css('--teal') },
    { key: 'out', color: css('--text'), width: 2.5 },
  ], { min: -50, max: 50, unit: '' });
}

// ---------- polling /state ----------

let lastTick = -1, failures = 0;

async function poll() {
  try {
    // Give up on a request after 1 s, so one lost answer can't freeze the page
    // (and its heartbeat) until the browser's own, much longer timeout.
    const res = await fetch('/state', { cache: 'no-store', signal: AbortSignal.timeout ? AbortSignal.timeout(1000) : undefined });
    const s = await res.json();
    failures = 0;
    el('connDot').className = 'dot ' + (s.mock ? 'mock' : 'ok');
    el('conn').textContent = (s.mock ? 'MOCK robot (made-up data)' : (s.wifi_mode === 'home' ? 'home Wi-Fi' : 'own network') +
                              ' ' + s.wifi_ssid) + ' · ' + s.ip;
    el('netNow').textContent = s.wifi_mode === 'home'
      ? 'On the home network "' + s.wifi_ssid + '". Own network is off.'
      : 'On BB1\'s own network "' + s.wifi_ssid + '". Save a home network below to join it instead.';

    showMode(s);
    showDrive(s);
    const err = s.pitch - s.gains.trim;
    if (s.have) {
      el('pitch').textContent = s.pitch.toFixed(1);
      el('rate').textContent = s.pitch_rate.toFixed(1);
      el('accel').textContent = s.accel_pitch.toFixed(1);
      setValue('rate', s.pitch_rate.toFixed(1) + ' deg/s');
      setValue('accel', s.accel_pitch.toFixed(1) + ' deg');
      setValue('work', s.work_us + ' us');
      setValue('tick', s.tick);
      if (s.tick !== lastTick) {                       // only add new samples to the charts
        history.push({ t: performance.now(), pitch: s.pitch, accel: s.accel_pitch, trim: s.gains.trim,
                       p: s.p, i: s.i, d: s.d, out: s.armed ? s.pwm_l : 0, cap: s.gains.cap, ncap: -s.gains.cap });
        lastTick = s.tick;
      }
    }
    el('pidErr').innerHTML = err.toFixed(1) + ' <small>deg</small>';
    el('pidP').textContent = s.p.toFixed(1);
    el('pidI').textContent = s.i.toFixed(1);
    el('pidD').textContent = s.d.toFixed(1);
    el('pidOut').innerHTML = (s.armed ? s.pwm_l : 0) + ((s.flag_bits & 8) ? ' <small class="warn">at cap</small>' : '');
    const bias = s.ever_cal ? s.bias.map(b => b.toFixed(2)).join(' / ') : '<span style="color:var(--red)">not calibrated</span>';
    el('bias').innerHTML = bias;
    setValue('bias', bias);
    el('alphaNow').textContent = s.alpha.toFixed(3);
    setValue('alpha', s.alpha.toFixed(3));
    setValue('imu', s.imu ? 'ok' : 'NOT FOUND');
    setValue('uptime', s.uptime_s + ' s');
    setValue('enc', s.enc_l + ' / ' + s.enc_r);
    setValue('speed', s.speed_l + ' / ' + s.speed_r);
    setValue('pwm', s.pwm_l + ' / ' + s.pwm_r + (s.test_running ? ' (test)' : ''));
    setValue('pid', s.p.toFixed(1) + ' / ' + s.i.toFixed(1) + ' / ' + s.d.toFixed(1));
    if (testsDoneSeen >= 0 && s.tests_done !== testsDoneSeen) {        // a test just finished
      const sec = Math.max(s.test_ms, 1) / 1000;
      el('motorMsg').textContent = 'Done after ' + s.test_ms + ' ms: left moved ' + s.test_moved[0] +
        ' counts (' + Math.round(s.test_moved[0] / sec) + '/s), right ' + s.test_moved[1] +
        ' counts (' + Math.round(s.test_moved[1] / sec) + '/s).';
    }
    testsDoneSeen = s.tests_done;
    lastAnswer = performance.now();
  } catch (e) {
    if (++failures > 5) {
      el('connDot').className = 'dot bad';
      el('conn').textContent = 'connection lost, retrying...';
      el('modeChip').textContent = 'NO CONNECTION';
      el('modeChip').className = 'chip fallen';
    }
  }
  const cutoff = performance.now() - HISTORY_MS;
  while (history.length && history[0].t < cutoff) history.shift();
  drawCharts();
  setTimeout(poll, 50);                                // about 20 updates per second
}

// "Page delay" counts up between answers, so a stall is visible as it happens.
let lastAnswer = performance.now();
setInterval(() => setValue('webAge', Math.round(performance.now() - lastAnswer)), 100);

poll();
pollSys();
</script>
</body>
</html>
)PAGE";

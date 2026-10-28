# Web Interface

BB1 serves its own web page from the Pico W. No app, no internet needed.

## Connecting

### Own network (default)

1. On your phone or Mac, join the Wi-Fi network **BB1**. The password is the one you set in `firmware/bb1/secrets.h` (`BB1_AP_PASSWORD`).
2. Open **http://192.168.42.1**

While a Mac is on BB1 it has no internet. That's normal: BB1 isn't connected to anything else.

### Home network

1. On the page, in **Network**, type your home Wi-Fi name and password, press **Save and join**.
2. BB1 saves them and restarts. Its own network turns off.
3. Back on your home Wi-Fi, open **http://bb1.local** (or the IP address BB1 prints on serial; type `wifi` to see it).

If `bb1.local` doesn't open (some tools and older Android phones don't resolve `.local` names), use the IP address instead: type `wifi` on serial to see it. Your router may also let you reserve that IP for BB1 so it never changes.

If BB1 can't find the home network within 15 s at startup, it falls back to its own network and the page says why. To go back to the own network on purpose: **Use own network** on the page, or `wifi reset` over serial.

### Do the settings persist?

Yes. The home network name and password are saved in the Pico's flash (the last few KB, through the `EEPROM` library). They survive power-off and uploading new firmware. They're cleared by "Use own network", `wifi reset`, or erasing the whole flash.

The password is stored as plain text on the Pico. It never goes into git.

## The page

Anything with a dotted underline has a hint: hover over it on the Mac and it appears next to it, move away and it goes; on a phone, tap the word (shows for 5 s). All hint texts live in one table, `HINTS` in `page.h`, keyed by element id: edit them there.

**Status bar** (stays at the top while you scroll): connection dot (green = BB1, orange = the offline mock, red = lost), the **battery** chip (**volts per cell**, like a LiPo charger: 4.2 full, 3.7 nominal; green, orange under 3.5, red under 3.3 = charge it; `batt --` = no wire or switch off; hover for the whole pack), the mode (DISARMED / ARMED / FALLEN / MOTOR TEST), the **arm countdown** with a shrinking green line under the bar, and a red **STOP** that ends everything. The space bar does the same as STOP from anywhere on the page.

The panels sit in one column, on a phone or a Mac. Since 2026-10-24 only **Balance** is open the first time; the developer panels below it start folded. Click a panel's title to fold or open it; the **▲ ▼** arrows move it. Folds and order are remembered by that browser. Anything wider than a panel (the log table) scrolls sideways inside its box, never the whole page.

| Panel | Shows / does |
|---|---|
| Balance | **Balancing** / **Not balancing** / **Fallen**, why it last stopped and what auto-arm is waiting for; **ARM** (no time limit, keeps going if you close the page; turns auto-arm back on), **STOP** (auto-arm off until ARM); a link to /tune. Gains, timed runs and the step test are on /tune only |
| Drive | (milestone 2, 2026-10-25) a **joystick pad**: press and drag, up = forward, sideways = turn; let go = stop driving, still balancing. **Arrow keys / WASD** do the same while held. **Max speed** slider (10 to 100% of the firmware limit, remembered per browser). Tiles: asked (what the page sends), target now (what BB1 aims for after its ramp), wheels (real speed). Works only while balancing. The page sends `POST /drive` every 100 ms while you hold; each command lasts 500 ms on BB1, so a closed page or dropped Wi-Fi stops it within half a second. Hiding the tab stops driving (not balancing) |
| PID | view only: error (pitch − trim), P, I, D and the output in tiles, and a 10 s chart of P, I, D and the output with the ±cap lines. All zero while disarmed |
| Pitch | pitch, pitch rate, accel-only pitch, and a 10 s chart: blue = filtered pitch, orange = accelerometer only, green dashes = trim |
| Log | note, **Record** / **Stop**, **Download**, **Save last 10 s**. While recording, a table shows the records as they arrive (armed rows tinted green); **Watch** shows it without recording, **Pause** freezes it. Every record / 10th / 40th; **all columns** shows every column, otherwise the useful ones |
| Live values | **List** (the original one-row-per-value look, default) or **Tiles**, remembered per browser: pitch rate, accel pitch, loop work, tick, gyro bias, alpha, IMU, uptime, encoders, wheel speeds, motor PWM, PID terms, page delay (counts up live when BB1 stops answering) |
| Motor test | left / right PWM (-150..150, forward +) and a time (0.5 to 3 s), **Run** (asks "wheels off the table?"), **STOP**. Shows how far each wheel moved |
| Sensors | gyro bias, filter alpha, **Calibrate gyro** (lay it still first), **Set alpha** |
| Pico memory | RAM used with a bar, heap free, flash used with a bar, log buffer, page size, CPU clock. Updates every 2 s |
| Network | which network BB1 is on; save a home network; switch back to the own network |

The Drive panel is open the first time too (it sits under Balance; fold or move it like the others).

BB1 balances by itself (auto-arm, docs/POWER_ON.md): no page, script or Wi-Fi is needed. ARM on the main page sends `heartbeat=0`, so it keeps balancing when the page closes. Armed from **/tune** (timed test runs), BB1 disarms if the page stops polling `/state` for 2 s (closed tab, phone screen off, Wi-Fi drop). That's on purpose.

The Pico's web server answers **one connection at a time**, so anything slow (a big `/log` answer while Recording) delays `/state`. Until 2026-10-21 it also waited up to 5 s for the browser to close each connection, which caused "lost the web page" disarms; `route()` in `web.h` now closes it straight away. For balance tests we arm from serial instead (no heartbeat) and record with `logger.py`: `firmware/README.md`, "Hand-held test, the current way". Later the Pi Zero W will serve the page and the Pico won't run Wi-Fi at all.

### The tuning page (/tune)

**http://bb1.local/tune** is a small page for PID tuning only: **P (Kp)**, **I (Ki)**, **D (Kd)**, trim and cap boxes (hover over or tap P, I or D for what it does; text in each label's `data-tip`) with **Apply**, **Trim = current pitch**, **Save to flash** and **Reload from BB1**; live pitch, error, P / I / D terms and PWM out with a 5 s chart; and the tuning notes. Next to the mode it shows the same battery chip as the full page. The gain boxes also include **Deadband**, **Speed P (sp)**, **Speed I (si)**, **Heading (th)** and **Auto-arm (1/0)** (firmware/README.md). When disarmed, the text next to the mode also says what auto-arm is waiting for (`/state` field `auto_state`; docs/POWER_ON.md). **Run a try** panel: seconds (1 to 30, default 10), **ARM**, **STOP** (space bar too), countdown and bar. Armed from this page the heartbeat applies (its `/state` polling keeps BB1 armed; 2 s without = disarm) and hiding the tab disarms. You can also arm from `tools/handheld.py` (USB or `--wifi`). Logs: run `handheld.py --wifi` or `logger.py` alongside. It uses the same `/state`, `/gains` and `/gains/save` as the full page (polls `/state` 10 times a second). Source: `firmware/bb1/tune_page.h`; offline at http://localhost:8080/tune with `mock_bb1.py`. The page also explains how to record and read logs (`pid_report.py`). How to use it while tuning: `docs/PID_TUNING.md`, "Two ways to change the gains".

### Balance panel, control by control

| Control | What it does | Why it's there |
|---|---|---|
| state line | **Balancing** (green), **Not balancing** or **Fallen**, then the reason it last stopped and what auto-arm is waiting for (`auto_state`) | so you always know whether the motors can move, and why not |
| **ARM** | starts balancing now (within 10 deg of upright) and turns auto-arm back on. No time limit, no heartbeat | after a STOP, or to start without waiting the 1 s hold |
| **STOP (space)** | brakes both wheels and sleeps the driver; auto-arm stays off until ARM | your stop button. The space bar does the same from anywhere on the page; the STOP in the top bar also ends a motor test |
| Tune gains and test runs | opens /tune | gains, timed tries, step test |

Safety, always on while armed: past 45 deg from the trim it stops and shows Fallen; it also stops on 5 IMU errors or 5 late loops in a row, if the wheels spin up in the air (lift detector), and, when armed from /tune, if the page stops polling for 2 s.

### Saving logs

- Type a **note** (it goes in the file and its name), press **Record**, do the test, press **Stop**, then **Download**. You get `bb1_<date>_<time>_<note>.csv`.
- **Save last 10 s** downloads whatever the robot still has in its buffer. Press it right after something interesting happened, even if you weren't recording.
- The file is the same layout as the serial `dump` and `tools/logger.py`: `# setting` lines (firmware, bias, alpha, scales, signs, note, time, record count), the header, one row per 5 ms tick. Check or plot it:

```bash
tools/venv/bin/python tools/check_dump.py ~/Downloads/bb1_2026-10-22_10-15-00_tilt-test.csv --plot
```

`lost` in the file counts ticks that were overwritten before the page fetched them (only if the connection stalled for more than 10 s). `check_dump.py` also reports any missing ticks.

**No login.** Anyone on the same Wi-Fi can open the pages and arm the robot. Keep BB1 on your own network, and set your own `BB1_AP_PASSWORD` in `secrets.h` for its own network.

## How it works

```text
browser                                  Pico W core 0                     core 1
  every 50 ms: GET /state  ----------->  newest LogRecord -> JSON  <----  writes a record every 5 ms
  button:      POST /cal   ----------->  requestCalibrate = true   ---->  calibrates between ticks
```

The page never talks to the IMU directly: it reads the same log records as the serial readout and `dump`, and asks core 1 for changes through the same flags as the serial commands. So the web page can't upset the 200 Hz loop.

### Endpoints

The full reference, with arguments, answers, units and the drive failsafe, is **[docs/API.md](API.md)**. Short list; you can call these yourself with `curl` (on the same network as BB1):

| Request | Does |
|---|---|
| `GET /` | the page |
| `GET /tune` | the small PID tuning page (`tune_page.h`) |
| `GET /state` | newest values as JSON (since 2026-10-24 also `batt_v` (whole pack), `batt_ok`, `batt_warn_v`, `batt_low_v`, `batt_cells`) |
| `POST /cal` | recalibrate the gyro |
| `POST /alpha` with `value=0.98` | set the filter weight |
| `GET /wifi` | network mode and the saved home network name (never the password) |
| `POST /wifi` with `ssid=...&password=...` | save a home network and restart |
| `POST /wifi/forget` | forget it and restart on the own network |
| `GET /log/meta` | the `# setting` lines for the top of a log file |
| `GET /log?since=N` | log records from tick N on, as CSV (up to 1000 per request; see below) |
| `GET /sys` | RAM and flash use as JSON |
| `POST /motor` with `left=100&right=100&ms=2000` | timed motor test (clamped to the limits in `config.h`) |
| `POST /motor/stop` | stop the motors now and disarm |
| `POST /arm` with optional `seconds=5` | start balancing (refused unless near upright and calibrated; `/state` says why); disarms after that many seconds, 0 = no limit |
| `POST /disarm` | stop balancing |
| `POST /gains` with any of `kp ki kd trim cap deadband` | change them live; `trim=here` uses the current pitch |
| `POST /gains/save` | store them in flash (disarmed only); they load at startup |
| `POST /gains/defaults` | back to the `config.h` defaults (Save to keep) |
| `POST /step` | step test while armed: setpoint +2 deg for 1 s |
| `POST /drive` with `speed=0.3&turn=0&ms=500` | drive while balancing (wheel turns/s; lasts `ms`, default 500, max 3000; answers JSON) |
| `POST /drive/stop` | stop driving now, keep balancing |

`/log` starts with one line saying what came back, then the header and rows:

```text
# from,1200,to,1450,lost,0      <- ticks 1200..1449 follow; ask since=1450 next time
tick,t_us,work_us,...
1200,6012345,451,...
```

`since=0` means "everything you still have". The page and `logger.py` both just repeat `since=<to>` a few times a second.

```bash
curl http://192.168.42.1/state
```

```bash
curl -X POST -d value=0.97 http://192.168.42.1/alpha
```

```bash
curl "http://192.168.42.1/log?since=0" | head
```

## Files

| File | What's in it |
|---|---|
| `firmware/bb1/wifi_setup.h` | own network / home network, saving settings to flash, fallback |
| `firmware/bb1/web.h` | the routes above, one small function each |
| `firmware/bb1/api.h` | the `GET /api` list of every route and its limits (keep it in step with `web.h`) |
| `firmware/bb1/page.h` | the page: HTML, CSS and JavaScript in one C++ raw string |
| `firmware/bb1/log_meta.h` | the `# setting` lines at the top of every log file |
| `tools/mock_bb1.py` | the offline mock |
| `firmware/bb1/secrets.h` | own-network name and password (not in git) |

### Changing the page

Edit `page.h`, compile, upload. Everything between `R"PAGE(` and `)PAGE"` is the page exactly as the browser gets it.

The page costs no RAM: it's a `const` string, so it stays in flash (about 21 KB of 2 MB). The browser does the layout and drawing, so a bigger page doesn't slow the Pico.

## Offline mock (work on the page without the robot)

`tools/mock_bb1.py` is a pretend BB1 that runs on the Mac. It serves the real page straight out of `page.h` and answers every endpoint with made-up data (the robot rocks back and forth with some sensor noise). Record, Download, the live log, the memory panel and `logger.py` all work against it.

```bash
tools/venv/bin/python tools/mock_bb1.py
```

Open http://localhost:8080. Edit `page.h` in vim, save, reload the browser: no compile or upload. When it looks right, compile and upload to try it on the robot. Stop the mock with Ctrl-C.

- The top bar says **MOCK robot (made-up data)** in orange, so you can't mix it up with the real one.
- Mock log files say `# firmware,bb1 MOCK (tools/mock_bb1.py)`. Never put them in a training set.
- If you add an endpoint to `web.h`, add a matching fake answer in `mock_bb1.py` (`do_GET` / `do_POST`).
- Another port: `--port 9000`. Record from it on the Mac: `tools/venv/bin/python tools/logger.py --host localhost:8080 -t 5`.

### Adding an action

1. In `web.h`, write a handler and register it in `webBegin()` with `server.on(...)`.
2. If core 1 has to do something, add a request flag to `shared.h` and handle it at the top of `loop1()`.
3. Add a button in `page.h` that calls `post('/your-route', {...}, 'msgId')`.
4. Add the endpoint to the table above, and a fake answer to `tools/mock_bb1.py`.

### Building on a new machine

`secrets.h` isn't in git, so create it from the template first:

```bash
cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h
```

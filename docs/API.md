# BB1 HTTP API

Everything the web pages do goes through this API, so anything else (curl, a Python script, an AI agent) can do the same. The code is `firmware/bb1/web.h`; this file is the reference. `tools/mock_bb1.py` answers the same requests with made-up data, so you can try them without the robot.

- **Address:** `http://bb1.local` on the home network, or `http://192.168.42.1` on BB1's own network "BB1". (Some computers don't resolve `bb1.local`; use the IP from `wifi` on serial or the page's status bar.)
- **Style:** plain HTTP. `GET` reads, `POST` does something. Arguments go in the query string or as a form body (`-d name=value`), never JSON. No login: anyone on the same network can drive BB1.
- **One at a time:** the Pico serves one request at a time and closes each connection after answering. Keep requests short and don't open many in parallel. About 20 `/state` a second plus 10 `/drive` a second is what the page uses.
- **Answers:** `/api`, `/state`, `/sys`, `/wifi` (GET) and `/drive` answer JSON. Older actions answer one line of plain text meant for a person. Errors use the status code: `400` bad argument, `409` not allowed right now (the text says why), `404` unknown path.
- **Units:** degrees, deg/s, PWM (-255..255, forward +), wheel turns per second (520 encoder counts = 1 turn, assumed). Forward and turn right are positive.

## Every endpoint with curl

Copy-paste versions of every route in `web.h`. Set `B` once per terminal. Use the IP if `bb1.local` is slow (it can time out on macOS). Use `http://localhost:8080` for the mock. **Anything marked "moves" turns the wheels:** check BB1 is on the floor with room to move, or wheels in the air for `/motor`.

```bash
B=http://bb1.local                      # or http://192.168.1.240, http://192.168.42.1, http://localhost:8080

# Reading (safe any time)
curl -s $B/api | python3 -m json.tool   # every endpoint and its limits, JSON (start here)
curl -s $B/state                        # everything now, JSON (also the heartbeat)
curl -s $B/state | python3 -m json.tool # the same, readable
curl -s "$B/log?since=0"                # log records as CSV (up to 1000; ask since=<to> next)
curl -s $B/log/meta                     # the "# setting" lines for the top of a log file
curl -s $B/sys                          # RAM and flash use, JSON
curl -s $B/wifi                         # network mode and saved home network name, JSON
curl -s $B/ -o page.html                # the main page; /tune is the tuning page
curl -s $B/tune -o tune.html

# Balancing (moves)
curl -X POST $B/arm -d seconds=0 -d heartbeat=0   # balance, no time limit, no /state polls needed
curl -X POST $B/arm -d seconds=10                 # balance 10 s; disarms after 2 s without a /state poll
curl -X POST $B/disarm                            # stop balancing: motors off, hold BB1 first
curl -X POST $B/motor/stop                        # STOP: end a motor test and disarm
curl -X POST $B/step                              # step test while armed: setpoint +2 deg for 1 s

# Driving (moves, only while armed)
curl -X POST $B/drive -d speed=0.3 -d ms=2000     # forward 0.3 wheel turns/s for 2 s
curl -X POST $B/drive -d turn=0.5 -d ms=1000      # turn right on the spot for 1 s
curl -X POST $B/drive -d speed=-0.3 -d turn=-0.2  # back and left, 500 ms (the default)
curl -X POST $B/drive/stop                        # ramp to a stop, keep balancing

# Settings
curl -X POST $B/gains -d sdi=2 -d dff=25          # change any of kp ki kd trim cap deadband sp si th auto dff sdi smode wsp wsi irelax (RAM only)
curl -X POST $B/gains -d trim=here                # trim = the current pitch
curl -X POST $B/disarm && curl -X POST $B/gains/save   # store the gains in flash (disarmed only: hold BB1)
curl -X POST $B/gains/defaults                    # back to the config.h defaults (save to keep)
curl -X POST $B/alpha -d value=0.98               # angle filter weight, 0.90..0.999
curl -X POST $B/cal                               # recalibrate the gyro: 2 s, keep still, disarms

# Motor test (moves: wheels in the air, disarmed only, stops by itself)
curl -X POST $B/motor -d left=80 -d right=80 -d ms=1000   # PWM up to ±150, at most 3000 ms

# Network (BB1 restarts)
curl -X POST $B/wifi --data-urlencode ssid='MyHome' --data-urlencode password='...'   # join a home network
curl -X POST $B/wifi/forget                       # forget it, back to its own network "BB1"

# Firmware update over Wi-Fi (BB1 restarts; lying down and disarmed only). tools/flash.py --ota does this for you
curl -u bb1:<BB1_OTA_PASSWORD> -F firmware=@build/bb1/bb1.ino.bin $B/update
```

Answers: `/state`, `/sys`, `GET /wifi` and `/drive` send JSON. The other actions send one line of text. A `409` says why it wasn't allowed (for example `/gains/save` while armed). Add `-w ' %{http_code}\n'` to see the status code.

## Driving (milestone 2)

BB1 must be balancing (armed). It drives by moving the targets of its speed loop and heading hold (`firmware/bb1/drive.h`), never by sending PWM straight to the wheels, so the balance loop stays in charge.

### `POST /drive`

| Argument | Unit | Default | Limit | Meaning |
|---|---|---|---|---|
| `speed` | wheel turns/s | 0 | ±1.0 (`DRIVE_MAX_SPEED`) | forward +, back - |
| `turn` | turns/s of left-minus-right wheel difference | 0 | ±1.0 (`DRIVE_MAX_TURN`) | right +, left - |
| `ms` | ms | 500 (`DRIVE_HOLD_MS`) | 0..3000 (`DRIVE_HOLD_MAX_MS`) | how long this command lasts |

Out-of-range values are clamped, not refused; the answer says what will actually run:

```json
{"ok":true,"speed":0.50,"turn":0.00,"ms":500}
```

Not balancing: `409` and `{"ok":false,"error":"not balancing: arm first (POST /arm)"}`. Encoder fault (`/state` `enc_fault`): `409` with `"error":"encoder fault: ..."`, and a fault also ends the command already running. `speed` or `turn` not a number (`nan`, `inf`): `400` and `"error":"speed and turn must be numbers"`. Text that doesn't start with a number counts as 0, like Arduino's `toFloat()`.

**The failsafe:** every command runs out. To keep driving, send it again before it does (the page sends every 100 ms). When it runs out, BB1 ramps to a stop and keeps balancing where it is; it does not disarm. So a dropped Wi-Fi, a closed page or a crashed script can never leave BB1 driving for more than `ms`. A single command can ask for up to 3 s, for "drive forward for 2 s" without a loop.

Speed and turn ramp toward what you ask (1 turn/s per second, turn twice as fast), so a full jab can't tip it over. Disarming, a fall or a new arm clears the command.

Driving needs the speed loop (`sp`, `si`) and heading hold (`th`) on. They are by default (sp 3, si 0.5, th 60). With `th 0` turning still works on feedforward alone (`DRIVE_TURN_FF`, 15 PWM per turn/s), just less exactly. Turning on the spot keeps the position hold, and driving straight keeps the full heading hold (each axis only lets go of its own hold). Speed has an I term while driving, the setting `sdi` (degrees of lean per second for every turn/s it is slower than asked, default 4): without it BB1 stayed stuck going forward, because on wood the angle loop's I term is the real wheel-speed command and the speed loop only moves the setpoint. **`smode 1`** swaps that for a wheel speed loop: speed and position errors go straight to the PWM (`wsp` PWM per turn/s, `wsi` PWM per wheel turn), the classic balance-car layout, meant for use with a lower `ki` (tested 2026-10-26: it drove the wrong way at every ki from 100 to 25 on wood, so it stays off); `smode 0` (default) is the layout above. Speed also has a feedforward, the setting `dff` (PWM per turn/s, default 25): the PWM a speed needs goes straight to the wheels, so the angle loop's I term doesn't have to wind up to it and unwind it when you reverse. When a command has ramped down to a stop, the setting `irelax` (seconds, default 0.5, 0 = off) drains the angle I term toward 0 for 2 x irelax, so the I term can't keep the wheels rolling the old way (it rolled BB1 slowly backward after stops, 2026-10-26).

### `POST /drive/stop`

Ramp to a stop now and keep balancing. Always allowed. Answers `{"ok":true,"speed":0,"turn":0,"ms":0}`.

### Examples

```bash
B=http://bb1.local
curl -X POST $B/arm -d seconds=0 -d heartbeat=0            # balance (if it isn't already)
curl -X POST $B/drive -d speed=0.3 -d ms=2000               # forward slowly for 2 s
curl -X POST $B/drive -d turn=0.5 -d ms=1000                # turn right on the spot for 1 s
curl -X POST $B/drive/stop                                  # stop driving
curl -s $B/state | python3 -c "import json,sys; print(json.load(sys.stdin)['drive'])"
```

Serial has the same command: `drive 0.3 0 2000`, and `drive stop` (or `ds`).

## Reading state

### `GET /state`

The newest values, as one JSON object. The page asks about 20 times a second. Asking also counts as the **heartbeat**: a run armed with `heartbeat` on (the `/tune` page) disarms if nobody asks for 2 s.

| Field | Meaning |
|---|---|
| `have` | false until the first log record exists |
| `tick`, `t_us`, `uptime_s` | record number (200 a second), its time in µs, seconds since boot |
| `mode` | 0 boot, 1 calibrating, 2 running (motors off), 3 motor test, 4 armed (balancing), 5 fallen |
| `flag_bits` | 1 IMU error, 2 loop overran, 4 no gyro calibration yet, 8 PID at the PWM cap, 16 drive command ran out (stopping), 32 encoder fault, 64 I term draining after a drive (`irelax`) |
| `enc_fault` | 0 ok; 1 the left encoder, 2 the right one, is not counting while armed (one wheel reads near 0 while the other spins for 0.3 s). Heading hold is off and `/drive` answers 409 until the next arm. Check that encoder's wires |
| `pitch`, `accel_pitch`, `pitch_rate` | deg (forward +), deg from the accelerometer only, deg/s |
| `armed`, `reason`, `auto_state` | balancing or not; why it last stopped or refused (text, empty if none); what auto-arm is doing (text) |
| `arm_attempts`, `arm_ms`, `arm_left_ms`, `step` | arm requests handled; arm timer length and time left (-1 = no timer); step test running |
| `p`, `i`, `d` | the PID terms right now (PWM) |
| `enc_l`, `enc_r`, `speed_l`, `speed_r` | encoder counts since boot, wheel speed in counts/s (forward +) |
| `pwm_l`, `pwm_r` | PWM sent to each motor this tick |
| `drive` | `speed`, `turn`: the command in force (0 when none); `speed_now`, `turn_now`: the ramped targets BB1 aims for; `left_ms`: time left on the command; `commands`: accepted since boot; `max_speed`, `max_turn`: the limits |
| `gains` | `ver` (counts every change), `kp`, `ki`, `kd`, `trim`, `cap`, `deadband`, `sp`, `si`, `th`, `auto`, `dff` (drive feedforward), `sdi` (drive speed I), `smode` (speed loop mode), `wsp`, `wsi` (wheel speed loop), `irelax` (I-term relax after a drive, s) |
| `batt_v`, `batt_ok`, `batt_warn_v`, `batt_low_v`, `batt_cells` | whole-pack volts; false = no sense wire; orange and red limits; 2 cells |
| `test_running`, `tests_done`, `test_moved`, `test_ms` | motor test status and the last result |
| `alpha`, `bias`, `calibrations`, `cal_ok`, `ever_cal`, `imu` | angle filter weight; gyro bias x/y/z (deg/s); calibration status; IMU found |
| `wifi_mode`, `wifi_ssid`, `ip` | `home` or `own`, network name, address |

The mock adds `"mock": true`.

### `GET /log?since=N` and `GET /log/meta`

Every 5 ms record from tick `N` on, as CSV (up to 1000 records per request). The first line says what came back; ask `since=<to>` next time:

```text
# from,1200,to,1450,lost,0
tick,t_us,work_us,...,drive_speed,drive_turn
1200,6012345,451,...
```

`/log/meta` gives the `# setting,value` lines that go at the top of a saved log file. Columns are in `docs/CALIBRATION_AND_LOGS.md`. `tools/logger.py` is a ready-made client; `tools/drive_test.py` drives a scripted test with the same calls and records it.

### `GET /api`

The list of every endpoint and its limits, as JSON, so a program or an AI agent can find out what BB1 understands without reading this file. The code is `firmware/bb1/api.h`; the limits are read from `config.h`, so they always match what the firmware enforces.

| Field | Meaning |
|---|---|
| `api` | version of this list; goes up when an endpoint, argument or limit is added (1 = first, 2 = `POST /update`) |
| `firmware`, `log_format` | firmware name (the mock says `bb1 MOCK`), log column version |
| `units` | what `angle`, `speed`, `turn`, `pwm` mean, and `counts_per_rev` |
| `limits` | `drive_max_speed`, `drive_max_turn` (turns/s), `drive_hold_ms` (default command length), `drive_hold_max_ms`, `arm_seconds_max`, `arm_angle` (must be this close to trim to arm, deg), `fall_angle` (deg), `heartbeat_ms`, `motor_test_max_pwm`, `motor_test_max_ms`, `alpha` [min, max], `log_max_per_request` |
| `gains` | each `/gains` setting and its [min, max]; values outside are clamped. `auto` and `smode` are 0 or 1; `trim` also takes `here` |
| `endpoints` | one object per route: `method`, `path`, `args` (names), `answer` (`json`, `text`, `csv`, `html`), `moves` (true = can turn the wheels), `does` |

```json
{"api":2,"firmware":"bb1 milestone 2","log_format":11,
 "limits":{"drive_max_speed":1.00,"drive_hold_ms":500, ...},
 "gains":{"kp":[0.000,100.000], ...},
 "endpoints":[{"method":"POST","path":"/drive","args":["speed","turn","ms"],"answer":"json","moves":true,"does":"..."}, ...]}
```

Adding an endpoint: route it in `web.h`, add a row to `API_ENDPOINTS` in `api.h` (and bump `API_VERSION`), the same row in `tools/mock_bb1.py`, and a row here.

### `GET /sys`

RAM and flash use: `ram_total`, `ram_static`, `heap_total`, `heap_used`, `log_buffer`, `flash_total`, `flash_used`, `page`, `cpu_mhz` (bytes, MHz). `fs_total`: bytes of flash kept for an OTA update (1048576; missing = firmware without OTA). `build`: when this firmware was compiled (`"Oct 27 2026 12:41:07"`), so a tool can see that a new firmware is running.

## Balancing

| Request | Does |
|---|---|
| `POST /arm` | start balancing. Refused unless within 10 deg of the trim, battery not low, no motor test (`/state` `reason` says why). `seconds=N`: disarm after N s (0 = no limit, max 600). `heartbeat=0`: keep going without `/state` polls (the main page); otherwise it disarms after 2 s without one. Also turns auto-arm back on |
| `POST /disarm` | stop balancing; auto-arm stays off until the next ARM. `why=hidden` records "page hidden" as the reason |
| `POST /motor/stop` | **STOP**: ends a motor test and disarms |
| `POST /step` | step test while armed: setpoint +2 deg for 1 s |

## Settings

| Request | Does |
|---|---|
| `POST /gains` | any of `kp ki kd trim cap deadband sp si th auto dff sdi smode wsp wsi irelax`; `trim=here` uses the current pitch. Answers the full set as text |
| `POST /gains/save` | store them in flash (disarmed only) |
| `POST /gains/defaults` | back to the `config.h` defaults (save to keep) |
| `POST /alpha` | `value=0.90..0.999`: angle filter weight |
| `POST /cal` | recalibrate the gyro (2 s, keep still; disarms) |

## Tests and network

| Request | Does |
|---|---|
| `POST /motor` | `left`, `right` (PWM ±150), `ms` (max 3000): timed motor test, disarmed only. Wheels off the table |
| `GET /wifi` | network mode and the saved home network name (never the password) |
| `POST /wifi` | `ssid`, `password`: save a home network and restart |
| `POST /wifi/forget` | forget it and restart on the own network |
| `GET /`, `GET /tune` | the main page and the tuning page |
| `GET /api` | every endpoint and its limits, JSON (see above) |
| `POST /update` | new firmware over Wi-Fi (`ota.h`): multipart form field `firmware` = `build/bb1/bb1.ino.bin` (or gzipped), Basic auth user `bb1`, password `BB1_OTA_PASSWORD` from `secrets.h`. Refused (`409`, JSON `error`) while armed, during a motor test, or unless lying down (more than 45 deg from the trim); `401` for a wrong password. On success answers `{"ok":true,"bytes":N,...}` and restarts; the bootloader copies the new firmware in (about 10-20 s until Wi-Fi is back). While it runs, every arm is refused with reason "a firmware update is running" (code 14). A broken-off upload changes nothing |

## Ready for an AI agent? (next milestone)

The API is already usable by a script or an agent with plain HTTP: `/state` to see, `/drive` with `ms` to act, and the drive failsafe means a stuck agent can't leave BB1 driving. What the AI-agent milestone would still add (not built yet):

1. **A small client and CLI** (`tools/bb1.py`, **built 2026-10-26**, see `docs/TOOLS.md`): `state`, `arm`, `drive --speed 0.3 --for 2`, `stop`, `disarm`, one JSON line out, exit codes for scripts. An agent can call a CLI or the HTTP API; an MCP server wrapping the same calls is the next step after that.
2. **Real-world units:** measure the wheel diameter and track width so `speed` can be m/s and `turn` deg/s of heading, plus odometry in `/state` (distance and heading since arming).
3. **Machine-readable answers everywhere:** `GET /api` (**built 2026-10-27**) lists every endpoint and its limits, so an agent can discover them. Still to do: the older actions answer human text; add JSON answers (for example with `Accept: application/json`).
4. **Safety for unattended use:** an "agent mode" with a lower speed limit, a maximum distance from where it armed, and maybe a token so only the agent (and you) can drive. Today anyone on the network can.
5. **Events:** a way to know "it fell" or "it stopped" without polling (for example a counter in `/state` an agent can watch, or a WebSocket later).
6. **Sensors that let an agent see:** the HC-SR04 distance (and later the Pi Zero camera) in `/state`, so it can avoid hitting things.

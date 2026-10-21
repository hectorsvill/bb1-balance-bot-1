# Firmware Plan: balancing + Wi-Fi tuning page

Updated 2026-10-21 with Hector's choices (section 7). Built step by step; see the table in section 6.

Goal for this milestone: BB1 balances by itself, and a web page served by the Pico W shows live data, lets you change the PID gains while it runs, and runs safe test actions. Every run can be logged in a format that could later train a model to drive the bot.

## 1. Tools

### Already installed (checked 2026-10-21)

| Tool | Version | Used for |
|---|---|---|
| arduino-cli | 1.5.1 (Homebrew) | compile and upload |
| arduino-pico core | rp2040:rp2040 6.3.0 | board support; includes WiFi, WebServer, LittleFS, mDNS, AsyncUDP, EEPROM |
| Python | 3.9.6 (macOS system) | serial and log scripts |
| pyserial | 3.5 | reading the Pico over USB |
| screen | system | quick serial monitor |
| git | system | local commits |
| Node | 24.15 | not needed, but handy for testing the web page offline |

A test compile of `imu_test` worked (15% flash, 26% RAM).

### Missing, needed later

| Tool | Needed for | When |
|---|---|---|
| numpy, pandas, matplotlib (in the `tools/venv` virtual environment, see `tools/README.md`) | plotting test logs, step responses | logging step |
| OpenSCAD | only for rebuilding CAD | optional |

No third-party Arduino library is needed: the core already has Wi-Fi, a web server and flash storage. (WebSockets would need a third-party library whose Pico W support is shaky right now, so the plan avoids it.)

### Claude side

Nothing extra to install. The Obsidian skills already present cover the vault notes. No catalog plugin fits Arduino or embedded work today.

## 2. Software shape

One firmware, `firmware/bb1/`, grown one step at a time. The bring-up sketches stay as they are. Each concern lives in its own small file so you can read it on its own:

```text
firmware/bb1/
  bb1.ino          setup() and loop() for both cores, nothing clever
  config.h         pins, signs, loop rate, limits (one place to change numbers)
  imu.h            MPU-6050 registers, read, bias calibration
  angle.h          complementary filter -> pitch and pitch rate
  motors.h         TB6612 driver + encoders (from motor_test)
  pid.h            a small PID class (anti-windup, derivative on measurement)
  control.h        the balance controller, modes and safety checks
  shared.h         the data the two cores pass to each other
  telemetry.h      the log record and the ring buffer that holds the last few seconds
  wifi_setup.h     own network or home network, saved settings
  web.h            web server routes
  secrets.h        own-network password (not in git; copy secrets.example.h)
  page.h           the HTML/JS page as one string
tools/             Python helpers, own virtual environment in tools/venv
  requirements.txt
  logger.py        pulls log records from the Pico over Wi-Fi, writes CSV + metadata
  plot_run.py      plots one run
```

### Two cores, two jobs

The RP2040 has two cores. We use that to keep the balance loop's timing steady no matter what Wi-Fi is doing.

```text
core 1 (control, 200 Hz, never waits)        core 0 (everything slow)
  read IMU                                     Wi-Fi + web page
  filter -> pitch                              read commands from the page
  read encoders                                serve log records to the page
  PID -> motor PWM                             USB serial commands
  safety checks
          \__ shared state (gains, commands, latest sample) __/
```

Why: a web request can take tens of milliseconds. If that happened inside the balance loop the robot would fall. The Wi-Fi driver runs on core 0, so control goes on core 1.

Data between cores goes through a small struct guarded by a mutex (or a double buffer). Core 1 never blocks on it: if the lock is busy it keeps the old gains for one more tick.

One catch: writing flash (saving gains) pauses the other core. So gains are only saved while the motors are disarmed.

### Loop timing

- 200 Hz control loop (5 ms). Fast enough for a robot this size and leaves lots of spare time.
- MPU-6050 at 400 kHz I2C, gyro +/-500 deg/s (a falling robot can pass 250), accel +/-2 g, the chip's own low-pass filter at about 44 Hz.
- The loop measures its own time each tick; max and average loop time go in the telemetry so we can see if anything is too slow.

## 3. Control design

Built in layers, each tested before the next:

1. **Pitch angle.** Gyro bias measured over 2 s at startup (robot still). Complementary filter: `pitch = 0.98 * (pitch + gyroRate * dt) + 0.02 * accelPitch`. Signs per CLAUDE.md (negate so forward lean is positive; verify gyro X sign).
2. **Angle loop (PD, then PID).** `pwm = Kp * error + Ki * integral + Kd * pitchRate`, where `error = setpoint - pitch`. Derivative uses the gyro rate directly, which is cleaner than differentiating the angle.
3. **Speed loop (PI) on the encoders.** Wheel speed error tilts the angle setpoint a little, so the robot stops drifting and can drive. This is the standard cascade for balancing robots.
4. **Turning.** Add `+turn` to one wheel and `-turn` to the other.

Plus a deadband compensation (motors don't move below some PWM) and a balance-point trim (the angle where the robot is truly balanced is rarely exactly 0).

### Modes

`DISARMED` -> `ARMED` (motors live, PID running) -> `FALLEN` (auto stop). Test modes run from the page or serial: `MOTOR_TEST`, `STEP_TEST`.

### Safety (always on)

- Boots `DISARMED`. Arming needs a button press **and** the robot near upright (|pitch| < 10 deg).
- Auto-disarm if |pitch| > 45 deg (fallen), the IMU read fails, the loop overruns repeatedly, or the page stops sending its heartbeat for 0.5 s while driving.
- PWM capped (starts at 150/255, raise later).
- Every test action has a fixed time limit and stops itself.
- STBY pin LOW whenever disarmed.
- Wi-Fi password not in git (`secrets.h` in `.gitignore`).

## 4. Wi-Fi: own network or home network

Both are built in, chosen by a setting on the page:

| Mode | How you connect | Page address |
|---|---|---|
| **Own network** (default) | join the Wi-Fi "BB1" from your phone or Mac | `http://192.168.42.1` |
| **Home network** | robot joins your Wi-Fi; its own network turns off | `http://bb1.local` (or the IP it prints on serial) |

- The page has a **Network** panel: type your home Wi-Fi name and password, press Save. The robot restarts and joins it.
- **Settings persist.** They're saved in the Pico's flash through the `EEPROM` library (a small area at the end of flash; the default board setting has no LittleFS area), so they survive power-off and re-flashing. Erasing the whole flash would clear them.
- **Can't lock yourself out:** if the home network isn't found within 15 s at boot, the robot falls back to its own network so you can fix the settings. A serial command `wifi reset` also clears them.
- Settings are only saved while the motors are disarmed (flash writes pause the other core).
- The home Wi-Fi password is stored in plain text on the Pico's flash. It never goes in git.

## 5. The web page

Served by the Pico W itself, one page, no internet needed (no CDN libraries, the chart is drawn by hand on a `<canvas>`, so it works when your phone is on the robot's own network).

- **Live:** pitch, pitch rate, wheel speeds, motor PWM, P/I/D terms, loop time, mode. Scrolling chart of the last 10 s.
- **Tune:** sliders + number boxes for Kp, Ki, Kd, speed-loop gains, balance trim, PWM cap. Changes apply on the next tick. Save to flash (only when disarmed) / load defaults.
- **Actions:** Arm / Disarm (big red button, also the space bar), motor test (each wheel, timed), step test (angle setpoint +2 deg for 1 s, to see the response), calibrate gyro.
- **Log:** Record / Stop / Download (see section 6).
- **Network:** own network or home network (section 4).
- **Drive (milestone 2):** a joystick pad for forward/back and turn.

How it talks: the page polls `GET /state` (JSON) about 20 times a second and sends `POST /set` for changes. Simple, works with the core's built-in WebServer, easy to read.

## 6. Logging (and data for a model later)

The Pico keeps every control tick (200 per second) in a **ring buffer** in RAM: the newest few seconds, oldest overwritten. Anything can pull records out of it over Wi-Fi with `GET /log?since=<tick>`, which returns every record newer than that tick.

Two ways to save a run, using that same endpoint:

| Way | How | Good for |
|---|---|---|
| **In the browser** | press Record on the page; it pulls new records every 100 ms and keeps them in memory. Press Stop, then Download: you get `bb1_<date>_<time>.csv` and a matching `.json` with the settings. Works on the Mac or a phone. | everyday tuning runs |
| **On the Mac with `tools/logger.py`** | run it in a terminal; it pulls the same records and writes `logs/<date>_<time>/run.csv` + `meta.json` | long runs, automated test sessions, building a training set |

Both write the **same CSV columns**, so every analysis or training script reads either. The buffer holds about 10 s, so a short Wi-Fi hiccup loses nothing. Because the robot always keeps the last 10 s, the page also has **"Save last 10 s"**: press it right after a fall, even if you weren't recording.

Each record is one control tick:

| Group | Fields |
|---|---|
| Time | tick number, microseconds since boot, loop time |
| Raw sensors | accel x/y/z, gyro x/y/z (raw counts), encoder counts L/R |
| Estimates | pitch, pitch rate, accel-only pitch, wheel speeds L/R |
| Commands (the "actions") | operator throttle and turn from the page, angle setpoint |
| Controller | error, P, I, D terms, PWM L/R actually sent |
| Context | mode, gains version number, flags (saturated, fallen) |

The `.json` / `meta.json` holds the gains, firmware version and git commit, sign settings, and a note you type before recording.

Why this works for training later: each row has what the robot sensed (observation), what was asked of it (operator command), and what it did (PWM). That's the standard shape for imitation learning (learn the driving from your remote-control runs in milestone 2) or for fitting a model of the robot's physics. Raw sensor values are kept, not just filtered ones, so a future model isn't stuck with today's filter. Tick numbers make any missing rows show up as gaps instead of silent errors.

## 7. Steps (one at a time, tested on the bot between each)

| # | Step | You test | Motors? |
|---|---|---|---|
| 1 | `bb1` skeleton: config, IMU, bias calibration, complementary filter, print pitch over serial | tilt it, pitch follows and doesn't drift | no |
| 2 | Two cores: control loop on core 1 at 200 Hz, loop-time stats, log record + ring buffer | loop time steady | no |
| 3 | Wi-Fi (own network + home network setting) + page with live pitch chart | open the page on phone/Mac | no |
| 4 | Log Record / Download on the page, `logger.py`, `plot_run.py` | log a tilt run, plot it | no |
| 5 | Motors + encoders in `bb1`, timed motor test from the page | wheels in the air | yes, timed |
| 6 | Arm/disarm, safety checks, angle PD loop | hand-held, then on the floor | yes |
| 7 | Live gain tuning, step test, save gains | tune per PID_TUNING.md | yes |
| 8 | Speed loop on the encoders, stop drift | stands still in place | yes |
| 9 | (milestone 2) Drive pad: forward/back, turn | drives around | yes |

Each step is its own commit. Before any step that can move the motors I'll ask first, and you confirm the wheels are off the table.

## 8. Choices made (2026-10-21)

1. **Wi-Fi:** both. Own network by default; a saved setting joins a home network and turns the own network off; falls back to the own network if the home one isn't found.
2. **Build order:** web page and logging first (steps 3 and 4), then the PID.
3. **Logs:** saved through the browser (Download button) or on the Mac with `tools/logger.py`, same CSV format.
4. **Python tools** live in `tools/` with their own virtual environment `tools/venv`.

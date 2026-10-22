# Firmware

| Folder | What it is |
|---|---|
| `bb1/` | The main firmware, grown one step at a time (see `docs/FIRMWARE_PLAN.md`) |
| `motor_test/`, `imu_test/` | Bring-up tests, kept as they were |
| `uf2/` | Prebuilt files to drag onto the Pico |

## bb1, step 7: save settings + step test

- **Save to flash** (page Balance panel, or `save` on serial) stores Kp, Ki, Kd, trim, cap and deadband in the Pico's flash (`settings.h`, next to the Wi-Fi settings). They load at startup, so a restart keeps your tuning. Only while disarmed. **Defaults** (`defaults`) goes back to `config.h`, which now starts at trim −4.39 and cap 80.
- **Step test** (page button or `step`, armed only): the setpoint jumps +2 deg for 1 s, then back. Record it: the `setpoint` column shows the step and `pitch` shows how the loop follows. Used to compare gains (docs/PID_TUNING.md).

Checked on the robot: saved kd 0.6, restarted, it came back as 0.6; the flash write caused 0 missing ticks and 0 overruns.

Fix (2026-10-21): the web server now closes each connection after answering (`route()` in `web.h`). Before, one connection the browser left open froze the server for up to 5 s and armed runs disarmed with "lost the web page".

### Hand-held test, the current way: serial + logger.py (no web page)

Why not the page: arming from the page ties the motors to Wi-Fi (no `/state` for 1 s = disarm), and Wi-Fi hiccups kept cutting the test short. For getting BB1 to balance we leave the page closed: you arm from **serial** over the USB cable (no heartbeat check then) and **logger.py** records over Wi-Fi in the background. If Wi-Fi stalls, the robot keeps the last 10 s, so the logger catches up without losing records. All other cut-outs still work: the arm timer, 45 deg (FALLEN), IMU errors, late loops, Enter.

**Before you start**
1. Battery on (D153C switch) **first**, then plug in USB (power rule).
2. Close every BB1 page tab (phone too), so nothing else talks to BB1.
3. Fingers clear of the wheels. Hold the frame, wheels in the air.

**Terminal 1: the serial monitor** (find the port with `ls /dev/cu.usbmodem*`)

```bash
arduino-cli monitor -p /dev/cu.usbmodem21401 -c baudrate=115200
```

Type `gains` and check: kp 15, kd 0.5, trim about −3.98, cap 80. (`wifi` shows the IP if `bb1.local` doesn't work below.)

**Terminal 2: the logger** (from the repo folder). Start it just before you arm; it stops by itself after 30 s.

```bash
tools/venv/bin/python tools/logger.py -t 30 -n "handheld"
```

**The test** (type in terminal 1)
1. Hold BB1 at its balance point and type `arm 5`. You should see `ARMED`. It disarms by itself after 5 s.
2. Tilt a little forward ("hectorsvill" side down): both wheels roll **forward**, harder with more tilt.
3. Tilt a little back: both roll **backward**.
4. To stop early: press Enter. Wrong way round? Enter at once and tell me.
5. Once more with `arm 5`, then tilt past 45 deg: `DISARMED: fell over (leaned past 45 deg)`. If it says `arm time finished`, you were slower than 5 s: try again.

**After**
1. Wait for the logger to finish. It prints the file name: `logs/bb1_<date>_<time>_handheld.csv`.
2. Check it: `tools/venv/bin/python tools/check_dump.py logs/<that file> --plot`. Missing ticks should be 0.
3. Send me the file name and tell me what the wheels did. Monitor: Ctrl-C to quit.

## bb1, step 6: arm / disarm + the angle loop

The first balance controller (`pid.h`, `control.h`). Boots **disarmed**. **ARM** (page, or `arm` on serial) only works within 10 deg of the trim with the gyro calibrated; then the PID drives both wheels from the lean. It disarms by itself if it leans past 45 deg (FALLEN), the IMU fails 5 ticks running, the loop overruns 5 ticks running, or (armed from the page) the page stops asking for `/state` for 1 s. **DISARM**, STOP, the space bar, serial `disarm` or Enter stop it.

Gains, trim, PWM cap and deadband change live (page **Balance** panel or serial `kp 15`, `kd 0.5`, `trim here`, `cap 80`, `gains`). Defaults are in `config.h` (Kp 15, Ki 0, Kd 0.5, cap 150), a guess to start from. Not saved yet (step 7).

Every log row now also has `setpoint, p_term, i_term, d_term, gains_ver` (log format 3), and `mode` 4 = armed, 5 = fallen; flag 8 = output hit the cap.

What to check, **hand-held first** (hold the frame, wheels free, fingers clear of the wheels):
Why: this checks the wheels drive toward the lean (all the signs) and the safety cut-outs before it ever touches the floor. Full explanation: docs/PID_TUNING.md, "The hand-held test". The Balance panel's controls: docs/WEB_INTERFACE.md.

1. Hold it at the balance point, **Use current pitch as trim** (or `trim here`). Write the number down (measured −4.39 deg on 2026-10-21).
2. Set the cap low (`cap 80`), arm time 5 s, **ARM**. Tilt it a little forward: both wheels roll **forward** (toward the lean), harder the more you tilt. Tilt back: they roll backward. If it's the other way round, disarm and tell me (a sign is wrong).
3. Tilt it past 45 deg: it disarms by itself (FALLEN). Close the page while armed: disarms within 1 s.
4. **Record** a run of steps 2 and 3 and check it with `check_dump.py --plot`.
5. Only then on the floor, over something soft, following docs/PID_TUNING.md.

## bb1, step 5: motors + encoders (timed motor test)

Everything from step 4, plus the motors and encoders on core 1 (`motors.h`). Encoders count all the time; the motors only run during a **timed motor test**, which always stops by itself (max 3 s, max PWM 150 of 255; both in `config.h`). STBY stays low the rest of the time. Every log record now also has `enc_l, enc_r, speed_l, speed_r, pwm_l, pwm_r` (log format 2).

**Wheels off the table before any test.**

| Where | How |
|---|---|
| Serial | `m 100 100` (both forward 2 s), `m 0 -80 1000` (right backward 1 s), `stop` or Enter to stop early, `enc` prints both counts |
| Page | **Motor test** panel: left, right, time, **Run**. **STOP** button or the space bar stops it. |
| curl | `curl -X POST -d "left=100&right=100&ms=2000" http://bb1.local/motor` |

When a test ends, serial and the page report how far each wheel moved and the average counts/s.

What to check (wheels in the air):
1. `m 100 100`: both wheels roll **forward**, both counts positive, each about 1,750 counts/s (bring-up value).
2. `m -100 -100`: both backward, both negative.
3. `m 100 0` then `m 0 100`: only the left, then only the right wheel turns.
4. Start `m 100 100 3000` and press Enter (or space on the page) after a second: stops at once.
5. `enc`, turn one wheel exactly one turn forward by hand, `enc` again: the difference is the real counts per turn (520 assumed).
6. **Record** on the page during a test, then `check_dump.py --plot`: the bottom plot shows speed and PWM. Missing ticks 0.

## bb1, step 4: saving logs + live log view (no motors)

Everything from step 3, plus: **Record / Stop / Download** and **Save last 10 s** on the page, a **Live log** panel, a **Pico memory** panel, panels in one column you can reorder with ▲ ▼, and `tools/logger.py` to record from the Mac. New endpoints `/log?since=N`, `/log/meta`, `/sys` (docs/WEB_INTERFACE.md). If the robot is moving at startup, calibration now retries every 3 s until it's still (flag bit 4 in the log until then).

Working on the page without the robot: `tools/venv/bin/python tools/mock_bb1.py`, then http://localhost:8080.

What to check:
1. Page opens at http://bb1.local; Pico memory shows about 150 KB RAM and 420 KB flash used.
2. Live log: press Start, tilt the robot, the `pitch` column (13th) follows. Scroll sideways inside the box.
3. Type a note, **Record** about 20 s of tilting, **Stop**, **Download**. Then:

```bash
tools/venv/bin/python tools/check_dump.py ~/Downloads/<the file>.csv --plot
```

   Missing ticks 0, the plot shows your tilts.
4. **Save last 10 s** right after a tilt: the file has about 2000 records.
5. From the Mac (robot on the home network):

```bash
tools/venv/bin/python tools/logger.py -t 30 -n "logger test"
```

   0 lost, 0 missing ticks, spacing about 5000 us (the web traffic mustn't disturb core 1).

## bb1, step 3: Wi-Fi + web page (no motors)

Everything from step 2, plus the web page. How to connect and what's on it: [docs/WEB_INTERFACE.md](../docs/WEB_INTERFACE.md). New serial commands: `wifi` (network and address), `wifi reset` (forget the home network), `quiet` (pause the readout).

Building on a new machine needs `secrets.h`: `cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h`.

What to check:
1. Serial shows `Wi-Fi: own network "BB1" ... open http://192.168.42.1`.
2. Phone: join **BB1** (password `bb1robot`), open http://192.168.42.1. The page says **connected**; tilting the robot moves the number and the chart.
3. **Calibrate gyro** with the robot lying still: bias updates on the page.
4. Network: save your home Wi-Fi. BB1 restarts and joins it; on your home Wi-Fi, http://bb1.local opens the page. Then **Use own network** brings back "BB1".
5. While the page is open: `dump` still shows 0 missing ticks and 5000 us spacing (the web server mustn't disturb core 1).

## bb1, step 2: two cores + log buffer (no motors)

The control loop (IMU, filter, logging) runs on core 1 at exactly 200 Hz; serial and the readout run on core 0. Every tick is stored in a 10 s ring buffer. Same readout as step 1, plus a `tick` counter and two new commands:

| Command | Does |
|---|---|
| `dump` | print the buffer (about the last 9 s) as CSV |
| `dump N` | print the last N seconds |

What to check:
1. The readout looks like step 1; `tick` goes up by about 20 per line (200 per second).
2. `cal` still works (core 0 asks, core 1 does it, core 0 reports).
3. Tilt it around for a few seconds, then save a dump and check it:

```bash
tools/venv/bin/python tools/serial_log.py -t 5 -s dump -o logs/step2_dump.txt
```

```bash
tools/venv/bin/python tools/check_dump.py logs/step2_dump.txt --plot
```

Missing ticks 0, spacing exactly 5000 us, and the plot shows your tilts.

## bb1, step 1: pitch angle (no motors)

Motors stay off (STBY held low). Flash it, keep the robot still on the table for the first 2 s (gyro calibration), then watch the serial readout.

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/bb1 firmware/bb1
```

```bash
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow firmware/bb1
```

Or drag `uf2/bb1.ino.uf2` onto RPI-RP2. Full flashing steps, power order and troubleshooting: [docs/FLASHING.md](../docs/FLASHING.md). To capture 20 s of output into a file:

```bash
tools/venv/bin/python tools/serial_log.py -t 20 -o step1.txt
```

What calibration does and how to read the output: [docs/CALIBRATION_AND_LOGS.md](../docs/CALIBRATION_AND_LOGS.md).

Commands: `cal` (recalibrate), `alpha N` (filter weight), `csv` (spreadsheet output), `help`.

What to check:
1. At rest: the calibration prints a bias (Y about +2.5 deg/s); pitch sits still (within about 0.2 deg over a minute).
2. Tilt forward about 30 deg: pitch goes **positive**, and `pitch` and `accel only` agree within a degree or two once you stop.
3. While tilting, `rate` is positive going forward. If pitch and accel-only pitch move opposite ways, flip `GYRO_PITCH_SIGN` in `config.h`. If both move together but forward reads negative, flip both signs.
4. Tap the frame: `accel only` jumps, `pitch` barely moves. That's the filter working.
5. `loop avg` well under 5000 us; `overruns` and `imu errors` stay 0.

# Bring-up tests: motors and MPU-6050

Two small Arduino C++ sketches that check each part works before we write the balance code.

| **Sketch** | **Checks** | **Needs the battery?** |
| :-: | :-: | :-: |
| motor_test | Each motor spins both ways through the D153C; each encoder counts | Yes (D153C switch on) |
| imu_test | The MPU-6050 answers on I2C and gives sensible readings | Yes for now (see the power rule) |

Board: Raspberry Pi Pico W with the "Raspberry Pi Pico/RP2040" core by Earle F. Philhower (version 6.3.0 used to compile these).

## Power rule (no 1N5819 diode yet)

The D153C 5V is wired straight to the Pico's VSYS pin. The Pico already has its own diode on the USB side, so your Mac is protected, but USB alone would push power backwards into the D153C's 5V regulator. Until the diode goes in:

1. Switch the D153C on (battery connected) **before** plugging in USB.
2. Unplug USB **before** switching the D153C off.

## Fastest way: drag and drop the ready-made UF2

The `uf2` folder has both sketches already compiled.

1. Prop the robot so both wheels are off the table.
2. Hold the Pico's BOOTSEL button and switch the D153C on, then let go.
3. Plug USB into your Mac. A drive called RPI-RP2 appears.
4. Drag `motor_test.ino.uf2` onto it. The Pico restarts and runs the test.
5. Open a serial monitor: `screen /dev/cu.usbmodem* 115200` (quit with Ctrl-A, then K, then y).

## Learning way: build it yourself with arduino-cli and vim

```
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040

vim motor_test/motor_test.ino
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/motor_test motor_test
```

- First upload: put the Pico in BOOTSEL mode (steps 2 and 3 above) and copy the file: `cp build/motor_test/motor_test.ino.uf2 /Volumes/RPI-RP2/`
- After that, while one of these sketches is running: `arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow motor_test` (find the port with `ls /dev/cu.usbmodem*`)
- Serial monitor: `arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200`

Arduino IDE 2 works too: Settings, Additional boards manager URLs, paste the URL above; Boards Manager, install "Raspberry Pi Pico/RP2040"; pick board "Raspberry Pi Pico W"; Serial Monitor at 115200 with "Newline".

## Test 1: motors (motor_test)

Wheels off the table. Type a command and press Enter:

| **Command** | **What happens** |
| :-: | :-: |
| a+ / a- | Motor A forward / backward for 2 s, then the encoder 1 count |
| b+ / b- | Motor B forward / backward for 2 s, then the encoder 2 count |
| speed N | Set the test speed, 0 to 255 (starts at 100, about 40%) |
| e | Print both encoder counts for 10 s while you turn the wheels by hand |

Write down for each command: which wheel turned, which way it rolled (robot forward or backward), and the count (number and + or - sign). If a motor only hums, try `speed 150`.

## Test 2: MPU-6050 (imu_test)

Flash `imu_test.ino.uf2` the same way. Expect `device at 0x68` and `WHO_AM_I = 0x68`. Sitting still and level, one accel axis reads about 1.00 g and the gyro reads close to 0. Tilt the robot forward slowly and note which tilt angle changes and its sign: that axis becomes our pitch angle.

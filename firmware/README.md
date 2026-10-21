# Firmware

| Folder | What it is |
|---|---|
| `bb1/` | The main firmware, grown one step at a time (see `docs/FIRMWARE_PLAN.md`) |
| `motor_test/`, `imu_test/` | Bring-up tests, kept as they were |
| `uf2/` | Prebuilt files to drag onto the Pico |

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

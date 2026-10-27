# Flashing the Pico W

How to get a sketch onto BB1, and how to see it running.

## 0. Power order (every time, until the 1N5819 diode is fitted)

1. Battery connected, **D153C switch on**.
2. **Then** plug USB into the Mac.
3. When done: **unplug USB first**, then switch the D153C off.

USB alone pushes 5 V backwards into the D153C's regulator. The diode will fix this later.

### Battery care (2S 1300 mAh LiPo, no voltage sensing yet)

Leaving BB1 powered on the battery while you work is fine: with the motors off (STBY low) it draws roughly 50 to 100 mA (Pico W with Wi-Fi, IMU, the D153C's regulator), so the pack lasts many hours. But nothing on the robot watches the voltage yet, so:

- Keep each cell above about 3.5 V (7.0 V for the pack) and never let it go below 3.0 V per cell (6.0 V): that damages a LiPo for good.
- Check it with a LiPo checker or multimeter on the balance plug every couple of hours of use.
- Unplug the battery when you walk away or overnight (USB out first).
- For storage longer than a few days, charge or discharge to about 3.8 V per cell.

The battery voltage divider (docs/FUTURE_INTEGRATIONS.md) will let the page show and log the voltage.

## Flashing with tools/flash.py (the easy way)

One command does the compile, the safety check, the upload and the check afterwards:

1. **Lay BB1 down** on its front.
2. Switch the **D153C on**, then plug in **USB** (power order below).
3. Close anything holding the port: Arduino IDE Serial Monitor, `handheld.py`, `arduino-cli monitor`.
4. From the repo folder: `tools/venv/bin/python tools/flash.py`
5. **Expect**, in order:
   - `== Compiling firmware/bb1` and the `Sketch uses ...` lines
   - `== Safety check`: `pitch +89 deg ...` and `Lying down: safe to flash.`
   - `== Uploading to /dev/cu.usbmodem1401` and `Wrote ... bytes`
   - `== BB1 after the reboot`: `auto-arm: waiting: stand it up and hold still` and the `gains v...` line
   - `Done: flashed, lying down and waiting. Unplug USB to use it.`
6. Unplug USB.

**Why the lying-down check:** with auto-arm, BB1 starts balancing by itself after the reboot if it's upright, so on a desk the wheels would spin. The script reads the pitch over serial and refuses unless BB1 is more than 45 deg from upright. Nothing is uploaded if the compile fails or the check refuses.

| Message | What to do |
|---|---|
| `BB1 is upright` | lay it down, run again |
| `No Pico found` | USB not plugged in, or the D153C is off; check `ls /dev/cu.usbmodem*` |
| `Couldn't read the pitch` | something else has the port open: close it. Or lay it down and add `--force` |
| `Upload failed` | close any serial monitor and retry; else BOOTSEL (section 3) |
| `Compile failed` | the error is printed above it |

Options: `--skip-compile` (upload the last build), `--force` (skip the lying-down check), `-p PORT`, `--ota` and `--host` (next section).

## Flashing over Wi-Fi: `tools/flash.py --ota` (no USB cable)

BB1 can take a new firmware over Wi-Fi (`firmware/bb1/ota.h`). The new file lands in a spare 1 MB of flash first; after a restart, a small bootloader copies it over the old program. That's why every build uses Flash Size **"Sketch: 1MB, FS: 1MB"** (`FQBN rp2040:rp2040:rpipicow:flash=2097152_1048576`; `flash.py`, CI and section 1 use it, and `ota.h` refuses to compile without it). Saved gains and the home Wi-Fi stay where they are: they sit in the last 4 KB of flash with either layout.

**Once:** flash the first OTA firmware over USB (the usual way above), because it changes the flash layout. `secrets.h` needs a `BB1_OTA_PASSWORD` line (see `secrets.example.h`).

**Every time after that:**

1. **Lay BB1 down** on its front, battery on, on Wi-Fi (no USB needed).
2. From the repo folder: `tools/venv/bin/python tools/flash.py --ota` (add `--host 192.168.x.x` if `bb1.local` is slow).
3. **Expect**, in order:
   - `== Compiling firmware/bb1`, `version 3.2, build id <branch> <commit> <time>` and the `Sketch uses ...` lines
   - `== Safety check over Wi-Fi`: `pitch +89 deg ... armed False, now running version ...` and `Lying down: safe to flash.`
   - `== Sending the firmware over Wi-Fi (3xx KB gzipped)` and `{'ok': True, 'bytes': ...}`
   - `== BB1 after the restart`: the same build id, `auto-arm: waiting...` and the `gains v...` line
   - `Done: flashed over Wi-Fi.` and `Recorded in logs/flash_history.csv: ...`

**Safety:** BB1 checks it too: it refuses an update while armed, during a motor test, or unless lying down, and blocks arming until the update is done (flash writes pause the control core). Wrong password = refused.

**Which firmware is on BB1:** `curl -s $B/sys` shows `version` (hand-bumped in `firmware/bb1/version.h`, one step per firmware change) and `build_id` (branch, commit, `+` if the firmware folder had uncommitted changes, compile time; written by `flash.py`). The main page shows both in the Pico memory panel, and every log file has them. `logs/flash_history.csv` (bb1-private) has one line per flash, USB or Wi-Fi. Bump `FIRMWARE_VERSION` when a firmware change is ready to go on BB1, and name the version in its CHANGELOG entry.

**When it goes wrong:**

| What happened | What it means / what to do |
|---|---|
| `refused: ...` or `upload broke off` | nothing changed, the old firmware runs: fix the reason and run again |
| `isn't running build ... after 60 s` | power it off and on. If Wi-Fi still doesn't answer, the new firmware is broken: flash over USB |
| power lost while it restarted | harmless: the copy note is still there, and the bootloader copies again at the next power-on |
| nothing answers on Wi-Fi or USB | BOOTSEL (section 3) always works |

## 1. Compile

From the repo folder:

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 --output-dir build/bb1 firmware/bb1
```

On a new machine, create `secrets.h` first: `cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h`.

This checks the code and writes `build/bb1/bb1.ino.uf2` (the file the Pico runs). `build/` is not committed. A copy of each step's file is kept in `firmware/uf2/`.

## 2. Upload: the normal way (Pico already running one of our sketches)

Find the port:

```bash
ls /dev/cu.usbmodem*
```

Upload (replace the port with what `ls` printed):

```bash
arduino-cli upload -p /dev/cu.usbmodem21401 --fqbn rp2040:rp2040:rpipicow firmware/bb1
```

What happens: arduino-cli asks the running sketch to reboot into the bootloader, copies the UF2, and the Pico restarts with the new code. The port name can change after a reboot; run `ls` again if the next command can't find it.

## 3. Upload: the BOOTSEL way (always works)

Use this the first time, or if the upload above fails or no port shows up.

1. Unplug USB.
2. Hold the white **BOOTSEL** button on the Pico.
3. Plug USB in (D153C already on), then let go. A drive called **RPI-RP2** appears.
4. Copy the file:

```bash
cp build/bb1/bb1.ino.uf2 /Volumes/RPI-RP2/
```

(or drag `firmware/uf2/bb1.ino.uf2` onto the drive in Finder). The drive disappears and the Pico restarts with the new code. macOS may say the disk wasn't ejected properly; that's normal.

## 4. Arduino IDE 2

**One-time setup**
1. **Arduino IDE > Settings**, "Additional boards manager URLs": paste `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`, click **OK**.
2. **Tools > Board > Boards Manager**, search `pico`, install **Raspberry Pi Pico/RP2040/RP2350** by Earle F. Philhower (6.3.0 tested).

**Each upload**
1. D153C on **first**, then USB.
2. **File > Open** `firmware/bb1/bb1.ino` (the other `.h` files open as tabs).
3. **Tools > Board > Raspberry Pi Pico/RP2040 > Raspberry Pi Pico W**.
4. **Tools > Port**: pick the one that says **Raspberry Pi Pico W** (`/dev/cu.usbmodem21401` here). Not the Anker dock (`/dev/cu.usbmodem13401`).
5. **Close the Serial Monitor tab** if it's open (it holds the port and the upload fails with `No drive to deploy`).
6. Click **Upload** (the → arrow). **Expect:** `Wrote … bytes to /Volumes/RPI-RP2/NEW.UF2` at the bottom.
7. **Tools > Serial Monitor**, 115200 baud, "Newline", type `gains`. **Expect:** `gains vN: kp …`.

Close the Serial Monitor again before running `tools/handheld.py`: only one program can hold the port.

**Saved settings survive a reflash.** Gains, trim, cap (and the home Wi-Fi) live in a separate flash area, so they come back after flashing bb1 again, even after flashing another sketch like `motor_test` in between (checked 2026-10-23). `defaults` resets them to `config.h`.

## 5. Watch it run

Any one of these:

```bash
tools/venv/bin/python tools/serial_log.py -t 20 -o step1.txt
```

(reads for 20 s, prints and saves; stops by itself)

```bash
arduino-cli monitor -p /dev/cu.usbmodem21401 -c baudrate=115200
```

(interactive, type commands; quit with Ctrl-C)

```bash
screen /dev/cu.usbmodem21401 115200
```

(quit with Ctrl-A, then K, then y)

Only one program can hold the port at a time. Close the monitor before uploading again.

## Troubleshooting

| Problem | Fix |
|---|---|
| Two ports show up | The other one is a USB dock (Anker shows as `usbmodem13401`). The Pico is the one listed as "Raspberry Pi" by `tools/venv/bin/python -m serial.tools.list_ports -v`. |
| `ls /dev/cu.usbmodem*` finds nothing | Use the BOOTSEL way. Try another cable (some are charge-only). |
| Upload says the port is busy | Close the serial monitor / screen first. |
| Nothing prints after flashing | The sketch waits up to 5 s for a monitor, then runs anyway; open the monitor and type `help`, or press Enter. |
| Output is garbage | Baud rate isn't 115200. |
| Upload says `No drive to deploy` and the port stays | Something holds the serial port, usually the Arduino IDE Serial Monitor (shows as `serial-monitor` in `lsof /dev/cu.usbmodem21401`). Close it and upload again. Happened 2026-10-23. |
| Upload says `No drive to deploy` and the port disappears | The Pico rebooted into its bootloader but macOS didn't mount RPI-RP2. Look for an "Allow accessory to connect?" pop-up and allow it; otherwise unplug and replug USB (it boots the old firmware) and retry, or use the BOOTSEL way. |
| `Operation not permitted` on `/Volumes/RPI-RP2` (uploading from Claude Code) | macOS hasn't given that app access to removable drives. The Pico is already in bootloader mode at this point, so drag the `.uf2` onto RPI-RP2 in Finder yourself. Or allow it in System Settings > Privacy & Security > Files and Folders > Removable Volumes. |

# Tools: what we use and how to run them

Everything here runs in the macOS Terminal (or the terminal inside VS Code / the Arduino IDE). Commands are shown one per box.

## The basics first

### Where am I? (the current folder)

Every terminal is "in" a folder, and relative paths like `logs/run.txt` are looked up from there. Your prompt shows the folder name:

```text
(venv) ➜  tools git:(main)        <- you're in the tools/ folder, venv active, git branch main
```

| Command | Does |
|---|---|
| `pwd` | print the folder you're in |
| `ls` | list what's in it |
| `cd ~/Desktop/"bb1 - balance bot 1"` | go to the repo folder (quotes needed: the name has spaces) |
| `cd tools` / `cd ..` | into tools/ / back up one level |

`..` means "the folder above". So from `tools/`, the logs are at `../logs/`.

**Rule of thumb for this repo: run commands from the repo folder.** All the examples below assume that. (The Python tools also find `logs/...` if you're in `tools/`, but arduino-cli doesn't.)

### The Python virtual environment (venv)

A venv is a private copy of Python with its own libraries (pyserial, pandas, matplotlib), kept in `tools/venv/`. It keeps this project's libraries separate from the rest of the Mac.

Turn it on for this terminal window:

```bash
source tools/venv/bin/activate
```

The prompt starts with `(venv)`, and `python` now means the venv's Python. Turn it off with `deactivate`.

Or skip activating and call its Python by path, which always works:

```bash
tools/venv/bin/python tools/summarize_serial.py logs/2026-10-21_step1_tilt2.txt
```

Rebuild it from scratch (if it breaks, or on a new Mac):

```bash
python3 -m venv tools/venv
```

```bash
tools/venv/bin/python -m pip install -r tools/requirements.txt
```

`tools/venv/` is not in git; `tools/requirements.txt` is the list it's rebuilt from. Add a library: install it with pip, then add its name to `requirements.txt`.

## Tool list

| Tool | What it's for | Installed |
|---|---|---|
| **arduino-cli** | compile and upload firmware from the terminal | Homebrew, 1.5.1 |
| **arduino-pico core** | teaches arduino-cli / the IDE about the Pico W | 6.3.0 |
| **Arduino IDE 2** | same thing with buttons, plus Serial Monitor | app |
| **Python 3 + venv** | our helper scripts in `tools/` | macOS Python 3.9.6 |
| **pyserial** | Python talks to the Pico over USB | in the venv |
| **pandas, numpy, matplotlib** | load logs as tables, do math, draw charts | in the venv |
| **screen** | bare-bones serial monitor | built into macOS |
| **git** | saves each step (commits), local only | built in |
| **vim** | editing files in the terminal | built in |

## Firmware: compile, upload, watch

Full detail, power order and troubleshooting: [FLASHING.md](FLASHING.md).

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/bb1 firmware/bb1
```

```bash
ls /dev/cu.usbmodem*
```

```bash
arduino-cli upload -p /dev/cu.usbmodem21401 --fqbn rp2040:rp2040:rpipicow firmware/bb1
```

`--fqbn` is the board name ("fully qualified board name"). Only one program can use the serial port at a time: close the Serial Monitor before uploading.

## Our Python scripts (`tools/`)

### serial_log.py: capture what the Pico prints

```bash
tools/venv/bin/python tools/serial_log.py -t 60 -s csv -o logs/my_run.txt
```

| Option | Meaning |
|---|---|
| `-t 60` | read for 60 seconds, then stop |
| `-s csv` | send this command first (here: switch to CSV output) |
| `-o logs/my_run.txt` | also save to this file; relative paths land inside the repo folder |
| `-p /dev/cu.usbmodem21401` | which port (default: the first Pico found) |

Any time you forget: `tools/venv/bin/python tools/serial_log.py --help`.

### summarize_serial.py: read a captured run

```bash
tools/venv/bin/python tools/summarize_serial.py logs/2026-10-21_step1_tilt2.txt
```

One line per second, then totals. Add `--plot` for a chart window (close the window to get the prompt back). What the numbers mean: [CALIBRATION_AND_LOGS.md](CALIBRATION_AND_LOGS.md).

### check_dump.py: check and plot a log dump

```bash
tools/venv/bin/python tools/check_dump.py logs/my_dump.txt --plot
```

Reads what the firmware's `dump` command printed (captured with `serial_log.py -s dump`). Reports lost ticks, timing, errors; `--plot` shows pitch, rate and loop time.

From step 5 on it also prints how far the wheels moved and their speed while the motors were on, and `--plot` adds a fourth plot with wheel speed and PWM.

It also reads the CSV files from the web page (Download, Save last 10 s) and from `logger.py`: same layout. So `check_dump.py --plot` is also how you plot a run (the planned `plot_run.py` wasn't needed).

### handheld.py: an arm test in one window

**Testing and development only.** BB1 balances by itself at power-on (auto-arm, [POWER_ON.md](POWER_ON.md)); you never need this script to make it balance. It's for recording a log of a try and arming with a timer while tuning.

```bash
tools/venv/bin/python tools/handheld.py
```

For the hand-held and floor tests. **`--wifi`** runs it with no USB cable (the cable tugs on BB1): arm, disarm and gains go over Wi-Fi (`/arm`, `/disarm`, `/gains`), and its `/state` polling (10 per second) is the heartbeat, so BB1 disarms by itself if Wi-Fi goes quiet for 1 s. Measured 2026-10-23 while logging: `/state` answered in 0.07 s median, 0.2 s worst, out of 150. Without `--wifi`: Needs USB plugged in and BB1 on the home network. Keys, no Enter needed: **a** arms for 5 s (`arm 5` on serial, it stops by itself), **Enter** or **space** disarms, **g** prints the gains, **:** lets you type any serial command and Enter (`kp 20`, `kd 0.6`, `trim here`, `save`), **q** (or Ctrl-C) disarms, saves the log and quits. BB1's 10 Hz readout is folded into one status line (pitch and how far from the trim, arming needs within 10 deg, plus how much has been logged); answers like `ARMED` and `DISARMED: fell over` print in full. The log is recorded over Wi-Fi like `logger.py` (it reuses its code) and saved as `logs/bb1_<date>_<time>_handheld.csv`. Gains changed on the `/tune` page show up in the status line and in the file (`# gains_vN,...` lines).

| Option | Meaning |
|---|---|
| `-s 3` | arm time per press of a (default 5, 1 to 10 s) |
| `--cap 150 --kp 18 --kd 0.6 --trim -4.6` | set these before the test (any of them; RAM only, `save` with **:** to keep). The status line always shows the kp, kd and cap BB1 is really using |
| `-n "tilt"` | note in the file and its name (default handheld) |
| `--host 192.168.1.240` | robot address, if `bb1.local` is slow (type `wifi` on serial) |
| `-p /dev/cu.usbmodemXXXX` | serial port, if more than one Pico is plugged in |

Only one program can hold the USB port: close `arduino-cli monitor` and the Arduino IDE Serial Monitor first. How it works inside (threads, keys, status line): `docs/HANDHELD_TOOL.md`.

### Labels in every log

`handheld.py` and `logger.py` both take `--surface wood` (or carpet, air...) and `-n "note"`. The file gets `# surface,...`, `# note,...` and one `# gains_vN,...` line per gains setting used, so a log still makes sense months later (and for training a model: FIRMWARE_PLAN.md, section 6).

`pid_report.py` also shows **V/cell**, the lowest battery voltage per cell in each try (logs from 2026-10-24 on), and warns under 3.5 V/cell.

### deadband_test.py: the smallest PWM that turns the wheels

Over Wi-Fi, **wheels in the air**. Runs the normal timed motor test (1 s each, the firmware stops it) at PWM 5, 10 ... 60 forward then back, reads how far each wheel moved, and prints the first PWM where both turn. Saves `logs/deadband_<date>_<time>.csv`. Ctrl-C stops the motors. Use the result for the `deadband` setting on /tune (docs/TUNING_LOG.md, round 14).

```bash
tools/venv/bin/python tools/deadband_test.py                # asks "wheels off the floor?" first
tools/venv/bin/python tools/deadband_test.py --step 2 --max 40
```

### pid_report.py: how did each try go?

Reads a balance log (handheld.py, logger.py or the page's Download) and prints one row per try: seconds up, how it ended (timer / FELL fwd / FELL back / you), wobble, lean, the average size of the P, I and D terms, time at the cap, PWM sign flips per second, wheel turns. Then plain hints (which term looks too big or small, never a number). Column meanings: `docs/PID_LESSON.md` section 7.

```bash
tools/venv/bin/python tools/pid_report.py                         # newest file in logs/
tools/venv/bin/python tools/pid_report.py logs/bb1_....csv --plot # a file, and draw each try
```

### make_wiring_svg.py: the wiring picture

Draws `docs/wiring.svg` from the pin lists inside it (same as the tables in `docs/WIRING.md`). Run `tools/venv/bin/python tools/make_wiring_svg.py`. For the PNG copy (`docs/wiring.png`), with Chrome:

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --hide-scrollbars --screenshot="$PWD/docs/wiring.png" --window-size=1000,836 "file://$PWD/docs/wiring.svg"
```

### logger.py: record a run over Wi-Fi

```bash
tools/venv/bin/python tools/logger.py -t 60 -n "tilt test"
```

Pulls every 5 ms record from BB1 over Wi-Fi (the same `/log?since=` the page uses) and writes `logs/bb1_<date>_<time>_<note>.csv`. Good for long runs: no browser tab to keep open.

| Option | Meaning |
|---|---|
| `-t 60` | stop after 60 s (leave it out to stop with Ctrl-C) |
| `-n "tilt test"` | a note saved in the file and its name |
| `--host 192.168.42.1` | robot address (default `bb1.local`); use the IP on BB1's own network, or if `.local` is slow |
| `--host localhost:8080` | the offline mock instead of the robot |
| `-o logs/x.csv` | choose the file name |

Then check it with `check_dump.py`, which it prints for you at the end.

### drive_test.py: drive tests over Wi-Fi, recorded and reported

**Moves BB1.** BB1 must already be balancing on the floor, with about 1 m clear all round.

```bash
tools/venv/bin/python tools/drive_test.py --list                          # the tests and their steps
tools/venv/bin/python tools/drive_test.py fwd-back-30 -n "dff 90 sp 8"    # run one (default --surface wood)
tools/venv/bin/python tools/drive_test.py --report logs/bb1_...csv        # re-read a saved run
```

Tests: `stand` (30 s still), `fwd-back-30` and `fwd-back-50` (3 s forward, 3 s pause, 3 s back at 0.3 or 0.5 turns/s), `turn` (left then right on the spot, 0.5), `arc` (forward 0.3 while turning 0.3), `all`. It records exactly like `logger.py` and adds one `# step,name,speed,turn,from_tick,to_tick` line per step plus `# drive_test,<test>,<how it ended>`.

Safety: refuses to start unless `/state` says armed and `enc_fault` 0. Each `/drive` lasts 1 s and is re-sent every 100 ms, never longer, so BB1 stops by itself within 1 s if the script dies. `/drive/stop` between steps and on any error, fall or Ctrl-C. It ends the test if BB1 disarms, gets an encoder fault, gets more than 3 wheel turns (about 0.6 m) from where it started (added after BB1 crept into a wall, 2026-10-26), or gives no answer for 2 s. Shorter Wi-Fi stalls are ridden out.

The report has one line per step: asked speed and turn; what the wheels did (average of the step's last half, turns/s); **stop** = furthest it rolled (or turned) on after the command ended (goal under 0.5 turn); **head** = heading drift; max lean forward/back; peak PWM; **sat%** (flag 8); **drop** = seconds the command ran out mid-step (Wi-Fi stall: don't compare gains on that step); flags (ENCODER FAULT printed in capitals). Then run `pid_report.py` on the same file for the balance side.

### dataset.py: every real log as one training table

```bash
tools/venv/bin/python tools/dataset.py                # writes logs/dataset/bb1_dataset.csv.gz
tools/venv/bin/python tools/dataset.py --all-rows     # keep disarmed rows too
```

Reads every `logs/bb1_*.csv`, skips mock logs and logs older than format 9 (no operator action), drops rows with an IMU error (flag 1), an encoder fault (flag 32), or where the firmware's encoder fault rule trips on the log itself (logs from before the detector). Splits each log into armed segments and labels each row with its segment's outcome: `timer`, `stopped`, `fell` or `log_ended` (still balancing when recording stopped). Adds `file`, `segment`, `outcome`, `surface`, `note`, `gains` (the `# gains_vN` text for the row's version, `dff` filled in from the header for old lines), `step` (drive_test.py logs) and `tick_gap`. Prints rows per outcome, surface and gains version, and how many rows have a drive command. Gzipped CSV, so no new library is needed: `pd.read_csv("logs/dataset/bb1_dataset.csv.gz")`.

### mock_bb1.py: a pretend robot for working on the page

```bash
tools/venv/bin/python tools/mock_bb1.py
```

Open http://localhost:8080. Serves the real page from `firmware/bb1/page.h` with made-up data; edit `page.h`, reload the browser. Details: [WEB_INTERFACE.md](WEB_INTERFACE.md#offline-mock-work-on-the-page-without-the-robot). Uses only the Python standard library.

### flash.py: compile and flash BB1 safely

```bash
tools/venv/bin/python tools/flash.py
```

Compiles `firmware/bb1`, finds BB1's port, checks over serial that BB1 is **lying down** (auto-arm would start it balancing on the desk otherwise), uploads, then prints what BB1 says after the reboot (auto-arm state, gains). Stops before uploading if the compile fails or BB1 is upright. Step by step and errors: [FLASHING.md](FLASHING.md#flashing-with-toolsflashpy-the-easy-way). Uses pyserial and `arduino-cli`.

### wiki_sync.sh: copy the docs to the GitHub wiki

```bash
tools/wiki_sync.sh
```

Copies every `docs/*.md` to the [GitHub wiki](https://github.com/hectorsvill/bb1-balance-bot-1/wiki) as a page, fixes the links, writes the Home index and sidebar, then commits and pushes the wiki. The repo's `docs/` stay the source of truth: edit there, then run this. Never edit wiki pages by hand (the next sync overwrites them). The wiki has to exist first: on GitHub, **Wiki** tab, **Create the first page**, **Save** (once). Needs only git.

## Typical test session

```bash
cd ~/Desktop/"bb1 - balance bot 1"
```

```bash
source tools/venv/bin/activate
```

```bash
python tools/serial_log.py -t 60 -s csv -o logs/2026-10-22_test.txt
```

```bash
python tools/summarize_serial.py logs/2026-10-22_test.txt --plot
```

(With the venv active, `python` is enough. Battery on before USB.)

### `BB1: ERR: unknown cmd` in handheld.py

The tool is talking to another USB device, not the Pico (an Anker hub/dock showed up as `/dev/cu.usbmodem13401`). Since 2026-10-23 `find_port()` (`serial_log.py`) picks the port whose USB vendor is Raspberry Pi (0x2E8A). If it still happens:

1. `ls /dev/cu.usbmodem*` to list the ports.
2. Run with the Pico's port: `tools/venv/bin/python tools/handheld.py -p /dev/cu.usbmodem21401 ...`
3. **Expect:** the status line shows `pitch ...`, not `waiting for serial`.

## Reading an error

Python errors ("Traceback") are read from the **bottom up**: the last line says what went wrong.

```text
FileNotFoundError: [Errno 2] No such file or directory: 'logs/2026-10-21_step1_tilt2.txt'
```

means "there's no such file **from the folder I'm in**". Check `pwd` and `ls`. (This one happened because the terminal was in `tools/` while the file is in `logs/` one level up; the scripts now look in the repo folder too.)

| Error | Usual cause |
|---|---|
| `No such file or directory` | wrong folder, or a typo in the name |
| `ModuleNotFoundError: No module named 'serial'` | venv not active: `source tools/venv/bin/activate` |
| `Resource busy` on `/dev/cu.usbmodem...` | another program (Serial Monitor, screen) has the port |
| `No Pico found` | USB not plugged in, or the Pico is in BOOTSEL mode |
| `Can't find bb1.local` (logger.py) | Mac not on the same network as BB1, or `.local` lookups failing: use `--host <ip>` (type `wifi` on serial) |
| `Address already in use` (mock_bb1.py) | a mock is already running (another terminal), or pick `--port 9000` |

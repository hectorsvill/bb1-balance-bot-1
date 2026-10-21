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
| `cd "/Users/hectorstevenvillasano/Desktop/bb1 - balance bot 1"` | go to the repo folder (quotes needed: the name has spaces) |
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

### Coming later

`logger.py` (save runs over Wi-Fi) and `plot_run.py` (plot a full log), in step 4.

## Typical test session

```bash
cd "/Users/hectorstevenvillasano/Desktop/bb1 - balance bot 1"
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

# tools/

Python helpers that run on the Mac, with their own virtual environment in `tools/venv/` (not in git, rebuilt from `requirements.txt`).

**How to set up and run everything (venv, folders, examples, common errors): [docs/TOOLS.md](../docs/TOOLS.md).**

| Script | What it does |
|---|---|
| `flash.py` | Compile `firmware/bb1`, check BB1 is lying down, upload, show the auto-arm state after the reboot |
| `serial_log.py` | Reads the Pico's USB serial for N seconds, prints it, optionally saves it (`-o logs/run.txt`) |
| `summarize_serial.py` | Summarizes a captured `csv`-mode run, one line per second; `--plot` draws it |
| `check_dump.py` | Loads any log (serial `dump`, page Download, `logger.py`), checks nothing is missing, `--plot` draws it; `load_dump()` for your own scripts |
| `handheld.py` | (testing only) Arm test in one window: keys a = arm 5 s, Enter = disarm, q = save the log and quit; records over Wi-Fi like `logger.py` |
| `logger.py` | Records every log record from BB1 over Wi-Fi into `logs/bb1_<date>_<time>_<note>.csv` (same layout as the page's Download) |
| `mock_bb1.py` | A pretend BB1 on the Mac: serves the real page with made-up data at http://localhost:8080 |

Quick start, from the repo folder:

```bash
source tools/venv/bin/activate
```

```bash
python tools/summarize_serial.py logs/2026-10-21_step1_tilt2.txt --plot
```

Installed versions (2026-10-21, Python 3.9.6): pyserial 3.5, numpy 2.0.2, pandas 2.3.3, matplotlib 3.9.4.

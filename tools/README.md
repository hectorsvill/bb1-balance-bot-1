# tools/

Python helpers that run on the Mac, with their own virtual environment in `tools/venv/` (not in git, rebuilt from `requirements.txt`).

**How to set up and run everything (venv, folders, examples, common errors): [docs/TOOLS.md](../docs/TOOLS.md).**

| Script | What it does |
|---|---|
| `serial_log.py` | Reads the Pico's USB serial for N seconds, prints it, optionally saves it (`-o logs/run.txt`) |
| `summarize_serial.py` | Summarizes a captured `csv`-mode run, one line per second; `--plot` draws it |
| `logger.py` | (step 4) Pulls log records from the Pico over Wi-Fi, writes `logs/<date>_<time>/run.csv` + `meta.json` |
| `plot_run.py` | (step 4) Plots one saved run |

Quick start, from the repo folder:

```bash
source tools/venv/bin/activate
```

```bash
python tools/summarize_serial.py logs/2026-10-21_step1_tilt2.txt --plot
```

Installed versions (2026-10-21, Python 3.9.6): pyserial 3.5, numpy 2.0.2, pandas 2.3.3, matplotlib 3.9.4.

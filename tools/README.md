# tools/

Python helpers that run on the Mac. They have their own virtual environment so their libraries don't mix with anything else on the system.

## Set up once

```bash
cd tools
python3 -m venv venv
venv/bin/python -m pip install -r requirements.txt
```

`venv/` is in `.gitignore`: it's rebuilt from `requirements.txt`, never committed.

## Use

Either activate the environment for the terminal session:

```bash
source tools/venv/bin/activate
```

(`deactivate` leaves it), or call its Python directly: `tools/venv/bin/python tools/serial_log.py`.

## Scripts

| Script | What it does |
|---|---|
| `serial_log.py` | Reads the Pico's USB serial for N seconds, prints it and saves it to a text file (for test records) |
| `logger.py` | (step 4) Pulls log records from the Pico over Wi-Fi, writes `logs/<date>_<time>/run.csv` + `meta.json` |
| `plot_run.py` | (step 4) Plots one saved run |

Installed versions (2026-10-21, Python 3.9.6): pyserial 3.5, numpy 2.0.2, pandas 2.3.3, matplotlib 3.9.4.

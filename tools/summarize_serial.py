"""summarize_serial.py - turn a captured bb1 CSV run into one line per second.

Usage:
    tools/venv/bin/python tools/summarize_serial.py logs/2026-10-21_step1_tilt2.txt
    tools/venv/bin/python tools/summarize_serial.py logs/run.txt --plot     # also draw a chart

The input is what serial_log.py saved while bb1 was in "csv" mode:
    ms,pitch,accel_pitch,pitch_rate,loop_avg_us,loop_max_us
Any other lines in the file (the "> csv" echo, calibration messages) are skipped.
"""

import argparse
import io
from pathlib import Path

import pandas as pd

REPO = Path(__file__).resolve().parent.parent   # the repo folder (tools/ is one level down)

COLUMNS = ["ms", "pitch", "accel_pitch", "pitch_rate", "loop_avg_us", "loop_max_us"]


def find_file(name):
    """A relative path is looked up from the folder you're in (your terminal's
    current directory). If it isn't there, try from the repo folder, so
    `logs/x.txt` works whether you're in the repo folder or in tools/."""
    p = Path(name)
    if p.exists():
        return p
    if not p.is_absolute() and (REPO / p).exists():
        return REPO / p
    raise SystemExit(f"Can't find {name} (looked in {Path.cwd()} and {REPO}). Try: ls {REPO / 'logs'}")


def load(path):
    # Keep only lines that look like data: 6 comma-separated values starting with a number.
    rows = [l for l in open(path) if l.count(",") == 5 and l[0].isdigit()]
    df = pd.read_csv(io.StringIO("".join(rows)), names=COLUMNS)
    df["t"] = (df.ms - df.ms.iloc[0]) / 1000.0   # seconds since the start of the capture
    return df


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--plot", action="store_true", help="show pitch, accel pitch and rate over time")
    args = ap.parse_args()

    df = load(find_file(args.file))
    per_sec = df.groupby(df.t.astype(int)).agg(
        pitch=("pitch", "mean"),
        accel_pitch=("accel_pitch", "mean"),
        rate=("pitch_rate", "mean"),
        loop_max_us=("loop_max_us", "max"),
    )
    per_sec.index.name = "second"
    print(per_sec.round(1).to_string())

    # How well the filter agrees with the accelerometer while the robot is still
    # (rate under 2 deg/s). While moving they should differ: that's the filter's job.
    still = df[df.pitch_rate.abs() < 2]
    gap = (still.pitch - still.accel_pitch).abs().max() if len(still) else float("nan")
    print(f"\nrows {len(df)}, {df.t.iloc[-1]:.0f} s")
    print(f"loop work: avg {df.loop_avg_us.mean():.0f} us, max {df.loop_max_us.max()} us (budget 5000)")
    print(f"largest pitch vs accel-only gap while rate < 2 deg/s (still or moving slowly): {gap:.2f} deg")

    if args.plot:
        import matplotlib.pyplot as plt
        fig, (a1, a2) = plt.subplots(2, 1, sharex=True, figsize=(10, 6))
        a1.plot(df.t, df.accel_pitch, label="accel only", alpha=0.6)
        a1.plot(df.t, df.pitch, label="pitch (filtered)")
        a1.set_ylabel("deg (forward +)")
        a1.legend()
        a2.plot(df.t, df.pitch_rate, color="tab:green")
        a2.set_ylabel("rate deg/s")
        a2.set_xlabel("seconds")
        fig.suptitle(args.file)
        plt.show()


if __name__ == "__main__":
    main()

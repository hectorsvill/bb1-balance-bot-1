"""check_dump.py - load a bb1 log dump and check it's complete.

Usage:
    tools/venv/bin/python tools/serial_log.py -t 5 -s dump -o logs/my_dump.txt
    tools/venv/bin/python tools/check_dump.py logs/my_dump.txt
    tools/venv/bin/python tools/check_dump.py logs/my_dump.txt --plot

Also reads the CSV files from the web page's Download / Save last 10 s and
from tools/logger.py: all of them are a few "# setting" lines, a CSV header
starting with "tick", then one line per 5 ms control tick.
Column meanings: docs/CALIBRATION_AND_LOGS.md.
"""

import argparse
import io
from pathlib import Path

import pandas as pd

REPO = Path(__file__).resolve().parent.parent


def find_file(name):
    p = Path(name)
    if p.exists():
        return p
    if not p.is_absolute() and (REPO / p).exists():
        return REPO / p
    raise SystemExit(f"Can't find {name} (looked in {Path.cwd()} and {REPO}). Try: ls {REPO / 'logs'}")


def load_dump(path):
    """Returns (table, settings). settings holds the '# name,value,...' lines."""
    rows, settings = [], {}
    for line in open(path):
        if line.startswith("# ") and "," in line:
            parts = line[2:].strip().split(",")
            if len(parts) == 4 and parts[0] == "gyro_bias_dps":          # # gyro_bias_dps,x,y,z
                settings["gyro_bias_dps"] = [float(v) for v in parts[1:]]
            else:                                                         # # name,value,name,value...
                for k, v in zip(parts[0::2], parts[1::2]):
                    try:
                        settings[k] = float(v)
                    except ValueError:
                        settings[k] = v.strip()   # text, like the firmware name or a note
        elif line.startswith("tick") or line[:1].isdigit():
            rows.append(line)
    df = pd.read_csv(io.StringIO("".join(rows)))
    df["t"] = (df.t_us - df.t_us.iloc[0]) / 1e6        # seconds from the first record
    return df, settings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--plot", action="store_true", help="plot pitch, accel pitch, rate and loop time")
    args = ap.parse_args()

    df, settings = load_dump(find_file(args.file))
    gaps = int((df.tick.diff().dropna() != 1).sum())        # tick should go up by exactly 1
    spacing = df.t_us.diff().dropna()                        # should be 5000 us every time

    print(f"settings: {settings}")
    print(f"{len(df)} records, {df.t.iloc[-1]:.2f} s, ticks {df.tick.iloc[0]} to {df.tick.iloc[-1]}")
    print(f"missing ticks: {gaps}  (0 = nothing lost)")
    print(f"tick spacing: {spacing.min():.0f} to {spacing.max():.0f} us  (5000 = exactly 200 Hz)")
    print(f"work per tick: avg {df.work_us.mean():.0f} us, max {df.work_us.max()} us  (budget 5000)")
    print(f"IMU errors: {int(((df.flag_bits & 1) > 0).sum())}, overruns: {int(((df.flag_bits & 2) > 0).sum())}")
    print(f"pitch: {df.pitch.min():.1f} to {df.pitch.max():.1f} deg")
    wheels = "enc_l" in df                                   # step 5 (log_format 2) and later
    if wheels:
        moving = df[(df.pwm_l != 0) | (df.pwm_r != 0)]
        print(f"encoders moved: left {df.enc_l.iloc[-1] - df.enc_l.iloc[0]}, right {df.enc_r.iloc[-1] - df.enc_r.iloc[0]} counts")
        if len(moving):
            print(f"motors on for {len(moving) / 200:.2f} s; while on, avg speed left {moving.speed_l.mean():.0f}, "
                  f"right {moving.speed_r.mean():.0f} counts/s")

    if args.plot:
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(4 if wheels else 3, 1, sharex=True, figsize=(10, 9 if wheels else 7))
        a1, a2, a3 = axes[:3]
        a1.plot(df.t, df.accel_pitch, label="accel only", alpha=0.5)
        a1.plot(df.t, df.pitch, label="pitch (filtered)")
        a1.set_ylabel("deg (forward +)")
        a1.legend()
        a2.plot(df.t, df.pitch_rate, color="tab:green")
        a2.set_ylabel("rate deg/s")
        a3.plot(df.t, df.work_us, color="tab:gray")
        a3.set_ylabel("work us")
        a3.set_xlabel("seconds")
        if wheels:
            a4 = axes[3]
            a4.plot(df.t, df.speed_l, label="speed left (counts/s)")
            a4.plot(df.t, df.speed_r, label="speed right")
            a4b = a4.twinx()                                  # PWM on its own scale, right-hand axis
            a4b.plot(df.t, df.pwm_l, "--", color="tab:red", alpha=0.6, label="pwm left")
            a4b.plot(df.t, df.pwm_r, ":", color="tab:purple", alpha=0.6, label="pwm right")
            a4b.set_ylabel("PWM")
            a4.set_ylabel("counts/s")
            a4.legend(loc="upper left")
            a4.set_xlabel("seconds")
        fig.suptitle(args.file)
        plt.show()


if __name__ == "__main__":
    main()

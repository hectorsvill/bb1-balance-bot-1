"""dataset.py - turn every real BB1 log into one clean training table.

Usage:
    tools/venv/bin/python tools/dataset.py                 # all of logs/, writes logs/dataset/
    tools/venv/bin/python tools/dataset.py --all-rows      # keep disarmed rows too

What it does (rules: docs/FIRMWARE_PLAN.md section 6):
  1. reads logs/bb1_*.csv, skips mock logs (# firmware,bb1 MOCK) and logs
     older than format 9 (no drive_speed / drive_turn = no operator action)
  2. logs before format 10: multiplies speed_l/speed_r by 10/3 (they read
     3.3x low, CHANGELOG 2026-10-26), so every row has the same speed scale
  3. drops rows with an IMU read error (flag 1) or an encoder fault (flag 32),
     and rows where the firmware's encoder fault rule trips on the log itself
     (older logs had a dead encoder before the detector existed): their sensor
     values are wrong, a model shouldn't learn from them
  4. splits each log into armed segments (one per arm) and labels each with
     how it ended: timer, stopped (you, STOP, page hidden, Wi-Fi, battery,
     picked up), fell, or log_ended (recording stopped while it balanced)
  5. adds the context columns from the file's header to every row: file,
     segment, outcome, surface, note, gains (the "# gains_vN" text for the
     row's gains_ver), and the drive_test step if there is one
  6. writes logs/dataset/bb1_dataset.csv.gz and prints a summary

Kept as gzipped CSV (pandas reads it with read_csv) so no new library is needed.
"""

import argparse
from pathlib import Path

import pandas as pd

from check_dump import load_dump

REPO = Path(__file__).resolve().parent.parent
MIN_FORMAT = 9                       # log format 9 added drive_speed, drive_turn
MODE_ARMED, MODE_FALLEN = 4, 5       # telemetry.h
FLAG_IMU_ERROR, FLAG_ENC_FAULT = 1, 32
# The firmware's encoder fault rule (config.h ENC_FAULT_*), run again here because
# logs from before the detector (2026-10-25) have a dead left encoder with no flag 32
ENC_FAULT_MIN_SPEED, ENC_FAULT_RATIO, ENC_FAULT_TICKS = 500, 0.2, 60   # counts/s on the fixed speed scale
OLD_SPEED_SCALE = 10 / 3            # logs before format 10: speed_l/speed_r read 3.3x low (constrain() bug)
# Disarm reasons (control.h DisarmReason) grouped into the three outcomes a model cares about
REASON_OUTCOME = {1: "stopped", 2: "fell", 3: "stopped", 4: "stopped", 5: "stopped", 9: "stopped",
                  10: "stopped", 11: "timer", 12: "stopped", 13: "stopped", 14: "stopped"}


def header_lines(path):
    return [l.rstrip("\n") for l in open(path) if l.startswith("# ")]


def encoder_suspect(df):
    """True from the first tick the encoder fault rule trips to the end of that
    armed segment (sticky, like the firmware): going straight, one wheel near 0
    while the other spins, for 0.3 s."""
    fast = df[["speed_l", "speed_r"]].abs().max(axis=1)
    slow = df[["speed_l", "speed_r"]].abs().min(axis=1)
    odd = (df.drive_turn.abs() < 0.1) & (fast > ENC_FAULT_MIN_SPEED) & (slow < ENC_FAULT_RATIO * fast)
    run = odd.groupby((~odd).cumsum()).cumsum()                 # ticks in a row that look odd
    tripped = run >= ENC_FAULT_TICKS
    return tripped.groupby(df.segment).cummax() & (df.segment >= 0)


def segments(df):
    """Yield (rows of one armed stretch, outcome). Same rule as pid_report.py."""
    armed = df["mode"] == MODE_ARMED
    group = (armed != armed.shift()).cumsum()
    for _, g in df[armed].groupby(group[armed]):
        after = df.loc[g.index[-1] + 1:].head(5)          # the reason shows up a tick or two later
        reasons = set(after.reason.astype(int)) - {0}
        if after.empty:
            outcome = "log_ended"
        elif MODE_FALLEN in set(after["mode"]) or 2 in reasons:
            outcome = "fell"
        elif reasons:
            outcome = REASON_OUTCOME.get(min(reasons), "stopped")
        else:
            outcome = "log_ended"
        yield g, outcome


def load_one(path, all_rows):
    """One log -> labelled rows, or (None, why it was skipped)."""
    lines = header_lines(path)
    meta = dict(l[2:].split(",", 1) for l in lines if "," in l)
    if "MOCK" in meta.get("firmware", ""):
        return None, "mock"
    fmt = int(meta.get("log_format", "0").split(",")[0] or 0)
    if fmt < MIN_FORMAT:
        return None, f"format {fmt}"
    df, _ = load_dump(path)
    if fmt < 10:                                         # put old speeds on the real scale (counts/s)
        df[["speed_l", "speed_r"]] = (df[["speed_l", "speed_r"]] * OLD_SPEED_SCALE).round()
    gains = {int(k[7:]): v for k, v in meta.items() if k[:7] == "gains_v" and k[7:].isdigit()}   # "# gains_v7,kp 18 ..." (not "# gains_ver")
    # Older "# gains_vN" lines were written before dff existed; the "# gains_ver,N,...,drive_ff,60"
    # line (the gains when the log started) has it, so fill it in for that version.
    start = meta.get("gains_ver", "").split(",")
    if start[0].isdigit() and "drive_ff" in start:
        v, dff = int(start[0]), start[start.index("drive_ff") + 1]
        if v in gains and " dff " not in gains[v] + " ":
            gains[v] += f" dff {float(dff):g}"
    df["file"] = path.name
    df["surface"] = meta.get("surface", "unknown")
    df["note"] = meta.get("note", "")
    df["gains"] = df.gains_ver.map(gains).fillna("")    # "" = this version has no # gains_vN line
    df["step"] = ""
    for l in lines:                                      # drive_test.py: "# step,name,speed,turn,from,to"
        if l.startswith("# step,"):
            _, name, _, _, a, b = l[2:].split(",")
            df.loc[(df.tick >= int(a)) & (df.tick <= int(b)), "step"] = name
    df["tick_gap"] = df.tick.diff().fillna(1).astype(int) - 1   # records lost just before this row
    df["segment"] = -1
    df["outcome"] = "disarmed"
    for n, (g, outcome) in enumerate(segments(df)):
        df.loc[g.index, "segment"] = n
        df.loc[g.index, "outcome"] = outcome
    flagged = (df.flag_bits & (FLAG_IMU_ERROR | FLAG_ENC_FAULT)) > 0
    suspect = encoder_suspect(df) & ~flagged
    bad = flagged | suspect
    keep = ~bad & (all_rows | (df.segment >= 0))
    return df[keep].drop(columns="t"), f"{int(flagged.sum())} flagged + {int(suspect.sum())} encoder-suspect rows dropped"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", default=str(REPO / "logs"), help="folder of logs (default logs/)")
    ap.add_argument("-o", "--out", default=str(REPO / "logs" / "dataset" / "bb1_dataset.csv.gz"))
    ap.add_argument("--all-rows", action="store_true", help="keep disarmed rows too (default: armed only)")
    args = ap.parse_args()

    parts, skipped = [], {}
    for path in sorted(Path(args.logs).glob("bb1_*.csv")):
        df, why = load_one(path, args.all_rows)
        if df is None:
            skipped[why] = skipped.get(why, 0) + 1
            continue
        parts.append(df)
        print(f"  {path.name}: {len(df)} rows, {why}")
    if not parts:
        raise SystemExit("No usable logs (need log format 9 or newer, not mock).")
    data = pd.concat(parts, ignore_index=True)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    data.to_csv(out, index=False)

    seg = data[data.segment >= 0].groupby(["file", "segment"]).outcome.first()
    driving = (data.drive_speed != 0) | (data.drive_turn != 0)
    print(f"\nWrote {len(data)} rows ({len(data) / 200 / 60:.1f} min) from {len(parts)} logs to {out}")
    print("Skipped logs:", ", ".join(f"{n} {why}" for why, n in skipped.items()) or "none")
    print("\nArmed segments by outcome:", seg.value_counts().to_dict())
    print("Rows by outcome: ", data.outcome.value_counts().to_dict())
    print("Rows by surface: ", data.surface.value_counts().to_dict())
    print("Rows by gains version:", data.groupby("gains_ver").size().to_dict())
    print(f"Rows with a drive command: {int(driving.sum())} ({100 * driving.mean():.1f}%, "
          f"{driving.sum() / 200:.0f} s); turning: {int((data.drive_turn != 0).sum())}")
    print(f"Rows whose gains version has no '# gains_vN' line: {int((data.gains == '').sum())}")
    print(f"Rows right after lost records (tick_gap > 0): {int((data.tick_gap > 0).sum())}")


if __name__ == "__main__":
    main()

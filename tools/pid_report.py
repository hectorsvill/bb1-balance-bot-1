"""pid_report.py - read a balance log and say how each try went.

Usage:
    tools/venv/bin/python tools/pid_report.py logs/bb1_2026-10-23_...csv
    tools/venv/bin/python tools/pid_report.py logs/bb1_...csv --plot      # also draw each try
    tools/venv/bin/python tools/pid_report.py                             # newest file in logs/

One row per try (each time it was armed), then hints. The hints say which
term looks too big or too small, never a number: you pick the gains.

What the columns mean (all from the armed part of the try only):
    secs     how long it stayed armed (10 = the arm timer ran out, it never fell)
    end      timer / FELL fwd / FELL back / you (disarmed by you)
    wobble   standard deviation of the error, deg. Smaller = calmer
    lean     average error, deg. Far from 0 = trim is off (+ = leans forward)
    |P| |I| |D|  average size of each term, in PWM. Who is doing the work?
    cap%     share of ticks where the output hit the cap (motors maxed out)
    flips/s  how often the PWM changes sign. Tens per second = jitter (D)
    turns    wheel turns from start to end (+ = rolled forward)
"""

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

from check_dump import find_file, load_dump

REPO = Path(__file__).resolve().parent.parent
MODE_ARMED, MODE_FALLEN = 4, 5            # telemetry.h
REASON_FELL, REASON_TIMER = 2, 11         # control.h (DisarmReason order)
COUNTS_PER_REV = 520
SAT_FLAG = 8                              # FLAG_SATURATED in telemetry.h


def tries(df):
    """Split the log into armed stretches. Returns (rows, how it ended)."""
    armed = df["mode"] == MODE_ARMED
    group = (armed != armed.shift()).cumsum()
    for _, g in df[armed].groupby(group[armed]):
        # The disarm reason shows up a tick or two after the last armed row.
        after = df.loc[g.index[-1] + 1:].head(5)
        reasons = set(after.reason.astype(int))
        if after.empty:
            end = "log ended"
        elif MODE_FALLEN in set(after["mode"]) or REASON_FELL in reasons:
            end = "FELL fwd" if g.pitch.iloc[-1] > g.setpoint.iloc[-1] else "FELL back"
        elif REASON_TIMER in reasons:
            end = "timer"
        else:
            end = "you"
        yield g, end


def summary(g, end):
    err = g.pitch - g.setpoint
    pwm = g.pwm_l[g.pwm_l != 0]
    flips = (np.sign(pwm).diff().fillna(0) != 0).sum()
    secs = len(g) / 200
    return {
        "gains": f"v{g.gains_ver.iloc[0]}",
        "secs": round(secs, 1),
        "end": end,
        "wobble": round(err.std(), 1),
        "lean": round(err.mean(), 1),
        "|P|": round(g.p_term.abs().mean()),
        "|I|": round(g.i_term.abs().mean()),
        "|D|": round(g.d_term.abs().mean()),
        "cap%": round(100 * ((g.flag_bits & SAT_FLAG) > 0).mean()),
        "flips/s": round(flips / secs, 1),
        "turns": round((g.enc_l.iloc[-1] - g.enc_l.iloc[0]) / COUNTS_PER_REV, 1),
    }


def hints(t):
    """Plain-language reading of the table (direction only, no numbers)."""
    out = []
    if (t["flips/s"] > 10).any() or (t["|D|"] > t["|P|"]).any():
        out.append("Jitter: D term as big as P, or PWM flipping >10x/s -> D too high (like hot motors on a quad).")
    if (t["cap%"] > 20).any():
        out.append("Motors at the cap a lot -> the cap is too low for these gains (or P/D too high). It can't push harder.")
    if (t.end.str.startswith("FELL") & (t["cap%"] < 5) & (t.secs < 3)).any():
        out.append("Falls fast without maxing the motors -> P may be too weak (not pushing back hard enough).")
    if (t.wobble > 6).any() and (t["flips/s"] < 5).all():
        out.append("Big slow swings without jitter -> not enough braking: a little more D (bounce-back on a quad).")
    falls = t.end[t.end.str.startswith("FELL")]
    if len(falls) >= 2 and falls.nunique() == 1:
        out.append(f"Every fall goes the same way ({falls.iloc[0]}) -> check the trim first.")
    if (t.lean.abs() > 2).mean() > 0.5:
        out.append("Average lean far from 0 in most tries -> trim is off (+ = leans forward: raise trim; - = back: lower).")
    if (t["|I|"] > t["|P|"]).any():
        out.append("I term bigger than P -> I is doing the steering; too much I gives slow, growing rocking.")
    if (t.turns.abs() > 2).any():
        out.append("Rolls more than 2 wheel turns -> drift. PD can't hold position; that's the speed loop's job (step 8).")
    if (t.end == "timer").all():
        out.append("Every try lasted until the timer: this setting balances. Compare wobble to pick the best.")
    return out or ["Nothing stands out. Compare wobble between settings: smaller is better."]


def plot(df, name):
    import matplotlib.pyplot as plt
    for n, (g, end) in enumerate(tries(df), 1):
        t = (g.t_us - g.t_us.iloc[0]) / 1e6
        fig, (a1, a2) = plt.subplots(2, 1, sharex=True, figsize=(10, 6))
        a1.plot(t, g.pitch - g.setpoint, label="error (deg)")
        a1.axhline(0, color="gray", lw=0.5)
        a1.set_ylabel("deg"); a1.legend(loc="upper right")
        for col, label in (("p_term", "P term"), ("i_term", "I term"), ("d_term", "D term")):
            a2.plot(t, g[col], label=label, lw=1)
        a2.plot(t, g.pwm_l, "k", label="PWM out", lw=1.5)
        cap = g.pwm_l.abs().max()
        a2.axhline(cap, color="red", ls=":", lw=0.8); a2.axhline(-cap, color="red", ls=":", lw=0.8)
        a2.set_ylabel("PWM"); a2.set_xlabel("seconds"); a2.legend(loc="upper right")
        fig.suptitle(f"{name}  try {n}: {end}, gains v{g.gains_ver.iloc[0]}")
    plt.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?", help="log file (default: newest in logs/)")
    ap.add_argument("--plot", action="store_true", help="draw error, P/I/D terms and PWM for each try")
    args = ap.parse_args()
    path = find_file(args.file) if args.file else max((REPO / "logs").glob("bb1_*.csv"), key=lambda p: p.stat().st_mtime)
    df, _ = load_dump(path)
    print(path.name)
    for line in path.read_text().splitlines():
        if line.startswith("# gains_v"):
            print(" ", line[2:])                 # what each gains version meant
    rows = [summary(g, end) for g, end in tries(df)]
    if not rows:
        print("No armed tries in this log.")
        return
    table = pd.DataFrame(rows)
    print(table.to_string(index=False))
    print()
    for h in hints(table):
        print("-", h)
    if args.plot:
        plot(df, path.name)


if __name__ == "__main__":
    main()

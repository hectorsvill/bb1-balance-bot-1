"""deadband_test.py - find the smallest PWM that actually turns BB1's wheels.

Below some PWM the motors just hum: the gearbox friction wins. That's the
"deadband". The balance loop's small corrections fall inside it and do nothing,
so BB1 drifts until the lean is big (docs/TUNING_LOG.md, round 14). Set the
firmware's `deadband` to what this finds and every correction gets that much
extra PWM, so small ones move the wheels too.

Usage (BB1 on Wi-Fi, no USB needed, WHEELS IN THE AIR):
    tools/venv/bin/python tools/deadband_test.py                 # PWM 5, 10 ... 60, forward and back
    tools/venv/bin/python tools/deadband_test.py --step 2 --max 40
    tools/venv/bin/python tools/deadband_test.py --yes           # skip the wheels-off question (scripts)
    tools/venv/bin/python tools/deadband_test.py --host localhost:8080   # the mock robot

How: for each PWM it runs the normal timed motor test (POST /motor, the same
as the page's Motor test panel; the firmware stops it by itself, max 3 s),
waits for it to finish, and reads how far each wheel moved from GET /state.
A wheel "turns" when it moved at least --min-counts encoder counts (520 = one
wheel turn). Results print as a table and are saved to logs/ as CSV.

Exit codes: 0 done, 1 you answered no or a test didn't report back,
2 usage (no terminal and no --yes, or bad arguments), 130 Ctrl-C (motors stopped).
--json prints one JSON document instead of the table.
"""

import argparse
import json
import sys
import time
from datetime import datetime

import cli_util
from logger import REPO, get, post

COUNTS_PER_REV = 520             # config.h (assumed, not measured cleanly yet)

_JSON = False


def say(*args, **kwargs):
    """Progress. With --json this goes to stderr so stdout stays one JSON document."""
    kwargs["file"] = sys.stderr if _JSON else sys.stdout
    print(*args, **kwargs)


def state(base):
    return json.loads(get(f"{base}/state", timeout=2))


def run_test(base, pwm, ms):
    """One timed motor test at the same PWM on both wheels. Returns (left, right) counts moved."""
    done_before = state(base)["tests_done"]
    post(f"{base}/motor", left=pwm, right=pwm, ms=ms)
    deadline = time.time() + ms / 1000 + 3            # the firmware stops it; this is only our own timeout
    while time.time() < deadline:
        time.sleep(0.2)
        s = state(base)
        if s["tests_done"] > done_before and not s["test_running"]:
            return s["test_moved"]
    post(f"{base}/motor/stop")                         # should never be needed, but never leave it running
    cli_util.fail("The motor test didn't report back. Stopped the motors.", cli_util.EXIT_FAIL, _JSON)


def confirm_wheels(yes, as_json=False):
    """Wheels must be in the air. --yes is the answer for a script.

    A pipe with no --yes used to wait forever on input(). Refuse instead.
    """
    if yes:
        return
    if not sys.stdin.isatty():
        cli_util.fail(
            "deadband_test.py moves the motors, and stdin is not a terminal, so it will not wait on a prompt. "
            "Pass --yes if the wheels are OFF the floor and free to spin.",
            cli_util.EXIT_USAGE, as_json)
    if input("Are BB1's wheels OFF the floor and free to spin? (y/n) ").strip().lower() != "y":
        cli_util.fail("Stopped. Lift the wheels first.", cli_util.EXIT_FAIL, as_json)


def first_pwm(rows, name, min_counts, highest):
    found = next((pwm for d, pwm, left, right in rows
                  if d == name and min(abs(left), abs(right)) >= min_counts), None)
    return found, highest


def main(argv=None):
    global _JSON
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb1.local", help="robot address (default bb1.local, or its IP)")
    ap.add_argument("--step", type=int, default=5, help="PWM step (default 5)")
    ap.add_argument("--max", type=int, default=60, help="highest PWM to try (default 60, firmware limit 150)")
    ap.add_argument("--ms", type=int, default=1000, help="time per test in ms (default 1000, firmware max 3000)")
    ap.add_argument("--min-counts", type=int, default=20, help="counts that mean 'it turned' (default 20, about 1/26 turn)")
    ap.add_argument("--yes", action="store_true",
                    help="skip the wheels-off question (required when stdin is not a terminal)")
    cli_util.standard_cli(ap)
    args = ap.parse_args(argv)
    _JSON = bool(args.json)
    base = f"http://{args.host}"

    say("Every test runs both motors, then stops by itself.")
    confirm_wheels(args.yes, _JSON)

    rows = []                                          # (direction, pwm, left, right)
    say(f"\n{'dir':>4} {'PWM':>4} {'left':>6} {'right':>6}  turning?")
    for sign, name in ((1, "fwd"), (-1, "back")):
        for pwm in range(args.step, args.max + 1, args.step):
            left, right = run_test(base, sign * pwm, args.ms)
            both = abs(left) >= args.min_counts and abs(right) >= args.min_counts
            say(f"{name:>4} {pwm:>4} {left:>6} {right:>6}  {'both' if both else ''}")
            rows.append((name, pwm, left, right))
            time.sleep(0.3)                            # let the wheels stop before the next one

    summary = {}
    say()
    for name in ("fwd", "back"):
        found, highest = first_pwm(rows, name, args.min_counts, args.max)
        summary[name] = found
        if not _JSON:
            say(f"{name}: both wheels turn from PWM {found}" if found else f"{name}: no PWM up to {highest} turned both wheels")
    if not _JSON:
        say("In the air needs less than on the floor (no weight on the wheels).")

    out = REPO / "logs" / f"deadband_{datetime.now():%Y-%m-%d_%H-%M-%S}.csv"
    saved = None
    try:
        with open(out, "w") as f:
            f.write(f"# deadband test,host,{args.host},ms,{args.ms},min_counts,{args.min_counts},counts_per_rev,{COUNTS_PER_REV}\n")
            # What the result depends on: the PWM frequency (from the firmware's log header) and the battery.
            try:
                f.writelines(l + "\n" for l in get(f"{base}/log/meta").splitlines() if "pwm_freq_hz" in l)
                f.write(f"# batt_v,{state(base).get('batt_v', 0):.2f}\n")
            except OSError:
                pass
            f.write("direction,pwm,left_counts,right_counts\n")
            f.writelines(f"{d},{p},{l},{r}\n" for d, p, l, r in rows)
        saved = str(out.relative_to(REPO))
        if not _JSON:
            say(f"Saved {saved}")
    except OSError as e:
        say(f"(could not save the CSV: {e})")

    if _JSON:
        print(json.dumps({
            "ok": True,
            "host": args.host,
            "min_counts": args.min_counts,
            "first_pwm": summary,
            "file": saved,
            "rows": [{"direction": d, "pwm": p, "left": left, "right": right} for d, p, left, right in rows],
        }))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:                          # Ctrl-C: stop the motors now, don't wait for the timer
        host = sys.argv[sys.argv.index("--host") + 1] if "--host" in sys.argv else "bb1.local"
        try:
            post(f"http://{host}/motor/stop")
        except OSError:
            pass
        cli_util.fail("Stopped.", cli_util.EXIT_INTERRUPT, "--json" in sys.argv)

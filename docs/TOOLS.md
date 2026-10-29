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

**Python version:** since 2026-10-28 our `tools/venv` is built from Homebrew's Python (3.14.8), and every tool and test runs on it, the MCP server too. macOS's `python3` (from Xcode) is 3.9. Every tool works on it except the MCP server (`bb1_mcp.py`): the `mcp` package needs Python 3.10 or newer, so on 3.9 pip skips it (the line in `requirements.txt` says `python_version >= "3.10"`). To use the MCP server, build the venv from a newer Python, e.g. Homebrew's (`brew install python`, then `rm -rf tools/venv` and `/opt/homebrew/bin/python3 -m venv tools/venv`, then the pip line above). CI tests on both 3.9 and 3.12.

## Tool list

| Tool | What it's for | Installed |
|---|---|---|
| **arduino-cli** | compile and upload firmware from the terminal | Homebrew, 1.5.1 |
| **arduino-pico core** | teaches arduino-cli / the IDE about the Pico W | 6.3.0 |
| **Arduino IDE 2** | same thing with buttons, plus Serial Monitor | app |
| **Python 3 + venv** | our helper scripts in `tools/` | Homebrew Python 3.14.8 (since 2026-10-28; was macOS 3.9.6) |
| **pyserial** | Python talks to the Pico over USB | in the venv |
| **pandas, numpy, matplotlib** | load logs as tables, do math, draw charts | in the venv |
| **screen** | bare-bones serial monitor | built into macOS |
| **git** | saves each step (commits), local only | built in |
| **vim** | editing files in the terminal | built in |

## Firmware: compile, upload, watch

Full detail, power order and troubleshooting: [FLASHING.md](FLASHING.md).

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 --output-dir build/bb1 firmware/bb1
```

```bash
ls /dev/cu.usbmodem*
```

```bash
arduino-cli upload -p /dev/cu.usbmodem21401 --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 firmware/bb1
```

`--fqbn` is the board name ("fully qualified board name"). Only one program can use the serial port at a time: close the Serial Monitor before uploading.

## Our Python scripts (`tools/`)

**Where runs are saved:** `logs/` in this repo is a link to the private repo `bb1-private` (`~/Documents/GitHub/bb1-private/logs`), so every tool's `logs/...` file lands there. Back it up with `git add -A && git commit -m "Runs <date>" && git push` inside that repo (its README has the rules). On a new Mac, clone both and run `ln -s ~/Documents/GitHub/bb1-private/logs logs` in this repo.

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

### bb1.py: control BB1 from the command line (JSON out, for scripts and AI agents)

**`drive` moves BB1.** One command per call, one line of JSON back. It finds BB1 as `bb1.local` by itself (it looks the name up once, IPv4 only, about 0.1 s; `curl http://bb1.local` takes about 5 s because it waits for an IPv6 answer BB1 never gives). Set `BB1_HOST` or pass `--host` only for another address, e.g. 192.168.42.1 on BB1's own network.

An AI agent gets **`docs/AGENT.md`** as its instructions for this tool (settings, safety rules, what every answer means).

```bash
tools/venv/bin/python tools/bb1.py api                         # firmware version, limits, every endpoint (an agent runs it first)
tools/venv/bin/python tools/bb1.py state                       # short summary (--full: all of /state)
tools/venv/bin/python tools/bb1.py arm                         # start balancing (stand it up first)
tools/venv/bin/python tools/bb1.py drive --speed 0.3 --for 2   # forward 0.3 wheel turns/s for 2 s, then stop
tools/venv/bin/python tools/bb1.py drive --turn -0.4 --for 1   # left on the spot
tools/venv/bin/python tools/bb1.py stop                        # stop driving, keep balancing
tools/venv/bin/python tools/bb1.py disarm                      # motors off: BB1 falls over
tools/venv/bin/python tools/bb1.py log status                  # is BB1's telemetry being recorded? last saved run
tools/venv/bin/python tools/bb1.py log start --surface wood -n "square"   # label the log (starts it if needed)
tools/venv/bin/python tools/bb1.py log stop                    # save the log now
```

Units are the firmware's: wheel turns/s (one turn is 0.178 m, measured), turn = left-minus-right turns/s, right +. Every answer has `"ok"`; on false, `"error"` says why. Exit code 0 done, 1 refused or stopped by a safety check, 2 BB1 didn't answer. `drive` answers what was asked (after the caps), then waits up to 2 s for the wheels to stop and gives `moved_turns` (forward +), `turned_turns` (left-minus-right wheel turns, right +), `settled` (stopped before answering) and the summary state. First runs on BB1 (2026-10-26, wood): 2 s at 0.3 moved +0.27 forward but −0.51 back; 1 s turns at 0.4 turned 0.28 right, 0.10 left at th 20 and 0.32-0.48 both ways at th 60 (about 0.4 asked). An agent should read these and correct, not assume. On carpet (2026-10-28) it's weaker still: 3 s at 0.3 moved −0.06 to +0.29 turns and often hadn't settled within 2 s; five correcting tries reached only +0.50 / −0.48 of a 1-turn target; 1 s turns at 0.4 gave 0.27 both ways; fwd-back-30 reached 0.18 forward and 0.10 back of 0.3. One turn of `turned_turns` = 0.178 / 0.127 rad = about 80 deg of heading.

Safety: `drive` refuses unless armed with no encoder fault; speed is capped at `--max` (default 0.5) and turn at `--max-turn` (default: the same as `--max`); `--for` is at most `--max-for`, which can lower the tool's own 10 s but never raise it (so an owner's `max_drive_s` from `docs/AGENT.md` can be passed straight in). Each `/drive` lasts 1 s and is re-sent every 100 ms, so BB1 stops by itself within 1 s if the tool dies (it never fetches `/log`, so the Pico's slow `/log` answers can't starve it). It checks `/state` twice a second and stops on a disarm, an encoder fault, or more than 3 wheel turns from the start. Ctrl-C sends `/drive/stop`. A drive with speed and turn both 0 is refused (exit 1); anything the tool cut (`--speed` past `--max`, `--turn` past `--max-turn`, `--for` past `--max-for` or 10 s) is listed in the answer's `cut`. `arm` uses `heartbeat=0` (keeps balancing with nobody polling) and also turns auto-arm back on, even when it is refused, so a fallen BB1 re-arms by itself once stood up. Test it on the mock: `--host localhost:8080`.

**Command log:** every call against the real robot appends one row to `logs/agent_commands.csv` (in bb1-private): `time` (local, with offset), `host`, `cmd` (`mcp:drive` and so on when it came through `bb1_mcp.py`), `args` (the parsed options as JSON, as asked, before any cut), `exit_code`, `answer` (the exact JSON line printed). The header is written when the file is new. Calls to the mock (`localhost`, `127.*`) aren't recorded, so mock data never mixes with real data. If the file can't be written the command still runs, with a warning on stderr. `tools/runs_db.py` imports it next to the logs.

**Telemetry is recorded automatically** (`tools/session_log.py`). Once BB1 is balancing after an `arm` or a `drive`, `bb1.py` starts a background recorder that does what `logger.py` does: it reads every 5 ms record from `GET /log` (starting 2 s back, so the arm is in it), and saves the same file layout to `logs/` with `logger.save_log`, which puts it in `logs/runs.sqlite`. There, the session's commands get its `run_id` (`commands` table). One recorder at a time; every `bb1.py` command tells it the agent is still busy. It saves and stops by itself when BB1 has been disarmed for 10 s with no command, after 5 min with no command, after 30 s with no answer, or after 30 min (the next command starts a new log). `log start` labels the log (`--surface`, `-n` note; default surface `$BB1_SURFACE` or `unknown`, note "agent session") and starts it even when BB1 isn't balancing; `log stop` saves now and answers with the file and `run_id`; `log status` shows seconds, records, lost, and `last_saved`. Working files: `logs/.recorder/` (pid, status, label, last saved, `out.txt` = what it printed; gitignored in bb1-private). The mock is never recorded.

### session_log.py: the recorder bb1.py starts

Not run by hand. See the paragraph above, and its top comment for the stop rules (`DISARMED_STOP_S`, `IDLE_STOP_S`, `NO_ANSWER_STOP_S`, `MAX_RECORD_S`). If something goes wrong, `logs/.recorder/out.txt` says what it did; a stuck one: `bb1.py log stop`, or `kill $(cat logs/.recorder/pid)` (it saves on the way out).

### bb1_mcp.py: BB1 as MCP tools (for Claude Code and other agent apps)

**Can move BB1** (through `bb1_drive`). An MCP server: an agent app (Claude Code, Claude Desktop, any MCP client) starts it and sees BB1 as a set of tools instead of shell commands. Needs Python 3.10+ (see the venv section above).

There is no robot logic in it: every robot tool calls `bb1.run()` with the same arguments the command line gets, and every log tool calls the functions behind `logs.py`. So all of `bb1.py`'s safety checks and the `logs/agent_commands.csv` trace (rows say `mcp:drive` and so on) stay in one place.

| Tool | Same as | Notes |
|---|---|---|
| `bb1_api` | `bb1.py api` | read only |
| `bb1_state(full)` | `bb1.py state [--full]` | read only |
| `bb1_arm(seconds)` | `bb1.py arm --seconds` | starts the motors: ask the owner first |
| `bb1_drive(speed, turn, secs)` | `bb1.py drive ... --max --max-turn --max-for` | the caps come from the server's options, not the agent |
| `bb1_stop` | `bb1.py stop` | |
| `bb1_disarm` | `bb1.py disarm` | BB1 falls over |
| `bb1_log(action, note, surface)` | `bb1.py log status / start / stop` | the automatic telemetry recording; moves nothing |
| `logs_schema`, `logs_runs(where, limit)`, `logs_show(run)`, `logs_ticks(run, cols, start_s, end_s, every, limit)`, `logs_sql(query, limit)` | `logs.py schema / runs / show / ticks / sql` | read only; `logs_ticks` returns at most 500 rows by default |

Each answer is the same JSON as the command line, plus `exit_code`. Add it to Claude Code (once, from the repo folder; the caps are the owner's, like the settings in `docs/AGENT.md`):

```bash
claude mcp add bb1 -- "$PWD/tools/venv/bin/python" "$PWD/tools/bb1_mcp.py" --host bb1.local --max-speed 0.3 --max-turn 0.4 --max-for 3
```

Check it with `claude mcp list`; remove it with `claude mcp remove bb1`. Options: `--host` (default `$BB1_HOST` or `bb1.local`), `--max-speed` (default 0.3 wheel turns/s), `--max-turn` (0.4), `--max-for` (3 s; `bb1.py` allows 10), `--db` (the catalog for the `logs_*` tools). Try it on the mock first: start `tools/mock_bb1.py`, then add it with `--host localhost:8080` (mock calls aren't recorded). An agent can ask for more than the caps; `bb1.py` cuts it and says so in `cut`.

### tests/test_tools.py: the tools' own tests (CI runs them)

```bash
tools/venv/bin/python -m unittest discover -s tools/tests -v
```

Takes about 10 s, needs no robot and no private logs. It compiles every `tools/*.py`; builds a catalog with `runs_db.py` from two made-up 60-tick logs (a fall and a carpet drive, plus a tiny `flash_history.csv` and `agent_commands.csv`) that `tools/tests/make_fixtures.py` writes to a temp folder and checks every table; checks `logs.py`'s JSON (`schema`, `runs`, `show`, `ticks`, `download`, `sql`) and that `sql` refuses writes; starts `mock_bb1.py` on a free port and runs `bb1.py` against it (state, api, arm, drive, stop, disarm, the refusals, the caps and `cut`, exit code 2 when nothing answers, no command-log rows for the mock); and, on Python 3.10+, the same through the MCP server in memory and over stdio. On Python 3.9 the 4 MCP tests are skipped. **No real logs in this public repo:** they live in bb1-private; `.gitignore` keeps out every `*.csv`, and a test fails if one is tracked anyway. `.github/workflows/tools.yml` runs it on every pull request, on Python 3.9 and 3.12.

### check_agent_doc.py: is AGENT.md current?

```bash
python3 tools/check_agent_doc.py
```

Checks that the "Written for `/api` version" line in `docs/AGENT.md` matches `API_VERSION` in `firmware/bb1/api.h`. CI runs it on every pull request, so bumping `API_VERSION` without updating the agent manual fails the build. No venv needed (standard library only).

### drive_test.py: drive tests over Wi-Fi, recorded and reported

**Moves BB1.** BB1 must already be balancing on the floor, with about 1 m clear all round.

```bash
tools/venv/bin/python tools/drive_test.py --list                          # the tests and their steps
tools/venv/bin/python tools/drive_test.py fwd-back-30 -n "dff 90 sp 8"    # run one (default --surface wood)
tools/venv/bin/python tools/drive_test.py --report logs/bb1_...csv        # re-read a saved run
```

Tests: `stand` (30 s still), `fwd-back-30` and `fwd-back-50` (3 s forward, 3 s pause, 3 s back at 0.3 or 0.5 turns/s), `turn` (left then right on the spot, 0.5), `arc` (forward 0.3 while turning 0.3), `all`. It records exactly like `logger.py` and adds one `# step,name,speed,turn,from_tick,to_tick` line per step plus `# drive_test,<test>,<how it ended>`.

Safety: refuses to start unless `/state` says armed and `enc_fault` 0. Each `/drive` lasts 3 s and is re-sent every 100 ms, so BB1 stops by itself within 3 s if the script dies; every step ends with `/drive/stop`. (Was 1 s: about every 10 s one `GET /log` answer takes 1-2.5 s and the Pico answers nothing else meanwhile, so a 1 s command ran out 41% of the moving time in the first training run, 2026-10-26.) `/drive/stop` between steps and on any error, fall or Ctrl-C. It ends the test if BB1 disarms, gets an encoder fault, gets more than 3 wheel turns (about 0.53 m) from where it started (added after BB1 crept into a wall, 2026-10-26), or gives no answer for 2 s. Shorter Wi-Fi stalls are ridden out.

The report has one line per step: asked speed and turn; what the wheels did (average of the step's last half, turns/s); **stop** = furthest it rolled (or turned) on after the command ended (goal under 0.5 turn); **head** = heading drift; max lean forward/back; peak PWM; **sat%** (flag 8); **drop** = seconds the command ran out mid-step (Wi-Fi stall: don't compare gains on that step); flags (ENCODER FAULT printed in capitals). Then run `pid_report.py` on the same file for the balance side.

### drive_explore.py: long random driving inside a box, for training logs

**Moves BB1, for a long time.** BB1 balancing in the middle of a clear square.

```bash
tools/venv/bin/python tools/drive_explore.py --box 1.5 --minutes 60 --surface wood -n "explore 1"
```

Random moves (forward, back, spin, arc, stop; 1-4 s each, up to `--max-speed` 0.4 turns/s), each a `# step` line. Keeps inside the box with odometry from the encoders (0.178 m per wheel turn and 0.127 m track, measured 2026-10-26; uses 80% of the box because odometry drifts): near the edge it turns to the middle and drives back. Saves a complete log every `--chunk` minutes (default 5) as `..._explore-pN.csv`. Stops for good on a fall, an encoder fault, battery under `--min-cell` (3.5 V/cell) or past the box edge; a Wi-Fi stall only pauses it (gives up after 30 s). Same 3 s commands as drive_test.py. For the mock, add `--host localhost:8080 --out-dir <scratch folder>` so mock logs never land in `logs/`.

### dataset.py: every real log as one training table

```bash
tools/venv/bin/python tools/dataset.py                # writes logs/dataset/bb1_dataset.csv.gz
tools/venv/bin/python tools/dataset.py --all-rows     # keep disarmed rows too
```

Reads every `logs/bb1_*.csv`, skips mock logs and logs older than format 9 (no operator action), multiplies `speed_l`/`speed_r` by 10/3 in logs before format 10 (they read 3.3x low), drops rows with an IMU error (flag 1), an encoder fault (flag 32), or where the firmware's encoder fault rule trips on the log itself (logs from before the detector). Splits each log into armed segments and labels each row with its segment's outcome: `timer`, `stopped`, `fell` or `log_ended` (still balancing when recording stopped). Adds `file`, `segment`, `outcome`, `surface`, `note`, `gains` (the `# gains_vN` text for the row's version, `dff` filled in from the header for old lines), `step` (drive_test.py logs) and `tick_gap`. Prints rows per outcome, surface and gains version, and how many rows have a drive command. Gzipped CSV, so no new library is needed: `pd.read_csv("logs/dataset/bb1_dataset.csv.gz")`.

### runs_db.py: all our data in one SQLite file

Everything we record, each in a table shaped for its use: one row per log (`runs`), every telemetry row (`ticks`), every disarm, drive step, gains version and header line, every flash, and every `bb1.py` command. Ask it with SQL. **This is the one place we store and find data:** every recorder (`logger.py`, `handheld.py`, `drive_test.py`, `drive_explore.py`, all through `logger.save_log`) adds its log here right after saving the CSV, so the catalog is always current (if the tables changed since, a new `SCHEMA_VERSION`, or there is no catalog yet, that first save rebuilds it from every log in `logs/`, about 13 s); `build` catches up anything else (logs copied in by hand, the page's Download). Logs saved outside `logs/` (mock runs, `--out-dir`) are left out. The CSVs stay the backup and the source the catalog is rebuilt from: `logs/runs.sqlite` is never committed (bb1-private ignores `*.sqlite`) and can be deleted and rebuilt any time. To read it, use `logs.py` (next).

```bash
tools/venv/bin/python tools/runs_db.py build            # after new runs: adds new or changed logs only (under 1 s if nothing changed)
tools/venv/bin/python tools/runs_db.py build --rebuild  # start over (about 13 s for 158 logs, 2.3 M ticks, 400 MB)
tools/venv/bin/python tools/runs_db.py recent           # named queries: recent, longest, falls, by-gains, steps, commands
tools/venv/bin/python tools/runs_db.py tables           # every column you can ask about
tools/venv/bin/python tools/runs_db.py query "SELECT ..."
```

"Every wood run at ki 100 longer than 5 min, and how did it end" (real output, 2026-10-28):

```text
$ tools/venv/bin/python tools/runs_db.py query "SELECT started, label, round(armed_s/60,1) AS armed_min, disarm_count, outcome
    FROM runs WHERE surface = 'wood' AND ki = 100 AND duration_s > 300 ORDER BY started"
            started                               label  armed_min  disarm_count   outcome
2026-10-24 17:24:12                            auto-arm        8.7             4 log_ended
2026-10-26 21:12:03              sp3-explore-explore-p1        5.0             0 log_ended
2026-10-26 21:17:03              sp3-explore-explore-p2        5.1             0 log_ended
2026-10-26 23:39:43 train-v26-irelax0-5-wood-explore-p1        5.0             0 log_ended
2026-10-26 23:44:43 train-v26-irelax0-5-wood-explore-p2        5.0             0 log_ended
```

Which gains we've run most, and how they did (`by-gains`, top rows; `avg_err` = RMS of pitch minus target while armed, deg):

```text
   kp    ki   kd   sp  sdi   th  runs  armed_min  fell  avg_err
18.00 100.0 0.50  4.0  NaN 20.0    12       35.3     1     0.94
18.00 100.0 0.50  3.0  4.0 60.0    17       20.2     0     0.29
18.00 100.0 0.50  3.0  2.0 20.0    12       13.1     0     0.27
 9.85   0.0 0.74  NaN  NaN  NaN    14        4.7     9     9.40
```

Every table but `flashes` and `commands` joins to `runs` with `USING (run_id)`. Columns (`tables` lists them all):

| Table | One row per | Use it for | Columns |
|---|---|---|---|
| `runs` | log | finding runs, comparing gains | `run_id`, `file`, `started` (from the file name), `label` (the file name's tail), `surface` (the `# surface` line, else wood / carpet / tile / air from the label), `note`, `firmware`, `firmware_version` and `build_id` (firmware 3.2 on), `log_format`, `gains_ver` and the gains when the log started under their short names (`kp ki kd trim cap deadband sp si th auto dff sdi smode wsp wsi irelax aff dacc lmax tl`; empty when that log predates the setting), `rows`, `duration_s`, `armed_s`, `drive_s` (a drive command was live), `disarm_count`, `pitch_err_rms` and `pitch_max_abs` (while armed), `batt_v_start` / `batt_v_end` (whole pack, empty without the sense wire), `outcome` (how the last armed stretch ended: fell, stopped, timer, log_ended, or never_armed; same rule as `dataset.py`), `file_size`, `file_mtime` |
| `ticks` | 5 ms telemetry record | plots, training, "what happened just before" | `run_id`, every log column as written (`tick` ... `speed_pwm`, see CALIBRATION_AND_LOGS.md; empty where that log format lacks it), plus `t_s` (seconds into the file), `speed_tps` and `turn_tps` (wheels in turns/s like `/drive`, turn = left minus right; the 3.3x bug before format 10 fixed). Raw `speed_l`/`speed_r` stay as logged |
| `disarms` | disarm | why runs end | `run_id`, `t_s`, `tick` (last armed tick), `reason` (the log's code), `reason_text` |
| `steps` | drive step (`drive_test.py`, `drive_explore.py`) | asked vs got | `run_id`, `n`, `name`, `speed`, `turn` (asked), `from_tick`, `to_tick`, `secs`, `got_speed`, `got_turn` (wheel average over the step's last half, like drive_test.py's report) |
| `gains` | gains version a log used | live gain changes | `run_id`, `ver`, the 16 gains; from the start line and every `# gains_vN` line |
| `meta` | `# ` header line | anything else in the header | `run_id`, `line`, `name` (first field), `value` (the rest, word for word) |
| `flashes` | firmware flash | which firmware was on BB1 when | `logs/flash_history.csv`: `time`, `version`, `build_id`, `method`, `result` |
| `commands` | `bb1.py` call | what an agent (or you) asked and what happened | `logs/agent_commands.csv`: `time` (local), `host`, `cmd`, `exit_code`, `ok`, `error`, `speed`, `turn`, `secs` (after the caps), `moved_turns`, `turned_turns`, `settled`, `run_id` (the log recording at that time, if any), `args` and `answer` (the raw JSON) |

Example: the last second before every fall on carpet:

```sql
SELECT r.file, t.t_s, t.pitch, t.speed_tps, t.pwm_l FROM disarms d JOIN runs r USING (run_id)
  JOIN ticks t ON t.run_id = d.run_id AND t.tick BETWEEN d.tick - 200 AND d.tick
  WHERE d.reason = 2 AND r.surface = 'carpet'
```

Times count from tick steps, so a restart in the middle of a recording doesn't make them negative. Mock logs are skipped (and `bb1.py` writes no command rows for `localhost`). Gains come from the file's `# gains_ver` line: written when `logger.py` or a tool starts recording, but when the page saves its last 10 s, at save time. A run where you changed gains live shows only that one set (the log's `gains_ver` column has every change).

### logs.py: view, slice and download logs (JSON, for AI agents)

Reads `logs/runs.sqlite` **read only** (nothing it does can change or delete data), so an agent can call it freely. Every answer is one JSON document; errors are `{"ok": false, "error": ...}` with exit code 1.

```bash
tools/venv/bin/python tools/logs.py schema                       # every table, what it's for, columns, row count
tools/venv/bin/python tools/logs.py runs --limit 10 --where "surface = 'carpet'"
tools/venv/bin/python tools/logs.py show sn3-agent               # one run: summary, disarms, steps, gains, commands, header
tools/venv/bin/python tools/logs.py ticks sn3-agent --cols pitch,speed_tps --from 5 --to 10
tools/venv/bin/python tools/logs.py download sn3-agent --out run.csv            # the original CSV, header and all
tools/venv/bin/python tools/logs.py download sn3-agent --cols pitch --from 3 --to 4   # a slice as CSV
tools/venv/bin/python tools/logs.py sql "SELECT surface, count(*) FROM runs GROUP BY surface"
tools/venv/bin/python tools/logs.py serve                        # web page: http://localhost:8090
```

- **A run** is its `run_id`, its file name, or any part of the file name that matches one log (`sn3-agent`). If more match, the error lists them. `run_id`s can change after `runs_db.py build --rebuild`: note runs down by file name.
- **`ticks`** returns at most `--limit` rows (default 2000) and thins evenly to fit: the answer says `rows_total`, `every` (kept every Nth row) and `returned`. Pass `--every 1 --limit 100000` for every row. Default columns: `t_s mode pitch setpoint speed_tps turn_tps pwm_l pwm_r drive_speed drive_turn`; `show` lists all of them (`tick_columns`).
- **`sql`** takes one `SELECT` (or `WITH ... SELECT`); at most `--limit` rows, with `truncated` true if there were more.
- **`--db <file>`** (before the command) reads another catalog, with its logs in the same folder; the tests use it on the fixture catalog.
- **`serve`** (this computer only, `127.0.0.1`): a page to list and filter runs, open one (summary, disarms, drive steps, gains, bb1.py commands, header), plot any columns over any time range, download the original CSV or the plotted slice, and run SQL. The page only uses this JSON API, so an agent can use the same URLs: `/api/schema`, `/api/runs?where=&limit=`, `/api/runs/<run>`, `/api/runs/<run>/ticks?cols=&from=&to=&every=&limit=` (`&format=csv` to download), `/api/runs/<run>/csv` (the original file), `/api/sql?q=`. The page is `tools/logs_page.html`.

### todo.py: the todo list from the vault (JSON, for AI agents)

The todos live in the Obsidian vault, not here: `BB1 - Roadmap.md` (milestone checklists, `- [ ]` / `- [x]`) and `BB1 - Dashboard.md` (Now / Next / Later, shown read only). `todo.py` reads them fresh every time; ticking a box rewrites only that one line of the Roadmap. Obsidian Git commits the vault by itself, so `todo.py` never runs git there. Every answer is one JSON document; exit code 0 ok, 1 refused (`{"ok": false, "error": ...}`), 2 bad usage.

```bash
tools/venv/bin/python tools/todo.py list --open                  # open todos by section (--section hardware to narrow)
tools/venv/bin/python tools/todo.py done 961a2f8d                # tick one box (id from list)
tools/venv/bin/python tools/todo.py undo 961a2f8d                # untick it
tools/venv/bin/python tools/todo.py add --section "Hardware to fit" "Mount the OLED"
tools/venv/bin/python tools/todo.py status                       # firmware, runs, branches, PRs, CI
tools/venv/bin/python tools/todo.py serve                        # web page: http://localhost:8091
```

- **An id** is a short hash of the section title and the todo's text: ticking doesn't change it, editing the text does (run `list` again).
- **Safe edits:** the file is re-read before every change, the page sends the exact line it showed and is refused if the file says something else now ("reload"), and the new file is written to a temp file and renamed in, so a crash can't leave half a roadmap. Line endings and file mode stay as they were.
- **`add`** puts `- [ ] text` after the last box of the section (title or a unique part of it).
- **`status`** never contacts the robot: last line of `logs/flash_history.csv`, run count and armed minutes from `logs/runs.sqlite` (read only), git branches, open PRs (`gh pr list`) and the last 3 CI runs (`gh run list`). Each fact fails on its own (`"ok": false` with the reason) without breaking the others.
- **Options:** `--vault <folder>` (or `$BB1_VAULT`) for another copy of the vault, e.g. a test copy; `--logs <folder>` (default the repo's `logs/`; a worktree has none, so point it at the main repo's).
- **`serve`** (this computer only, `127.0.0.1`): status strip, overall progress, the Dashboard's focus, then one card per section with a progress bar, open todos first (done ones folded), and an Add box. JSON for agents: `GET /api/todos`, `GET /api/status`, `POST /api/done` / `/api/undo` (`{"id", "line"}`, `line` optional), `POST /api/add` (`{"section", "text"}`). POSTs must be `Content-Type: application/json`, so another web page can't tick boxes behind our back. The page is `tools/todo_page.html`.

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

Before compiling it writes `firmware/bb1/build_id.h` (branch, commit, `+` for uncommitted firmware changes, time; gitignored) so every build is fresh and BB1 can say which one it runs (`firmware/bb1/version.h`). After every flash it adds a line to `logs/flash_history.csv`: time, version, build id, `usb` or `wifi`, result.

`--ota` does the same over Wi-Fi, no USB cable: checks `/state` (lying down, disarmed), sends the build gzipped to `POST /update` with the password from `secrets.h`, then waits until `/sys` shows this build's `build_id`. `--host <ip>` or `BB1_HOST` when `bb1.local` is slow. [FLASHING.md](FLASHING.md#flashing-over-wi-fi-toolsflashpy---ota-no-usb-cable).

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

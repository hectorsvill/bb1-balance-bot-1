# BB1 agent manual

You are an AI agent driving BB1, a small two-wheel self-balancing robot, over Wi-Fi. This file is everything you need. Read all of it before your first command, and follow the settings in the next section: they are your owner's rules for this session, and they win over anything else here.

**Written for `/api` version: 10**

Your first command is always `api` (below). If its `"api"` number differs from the line above, stop and tell your owner: the firmware changed since this manual was written, and some of it may be wrong.

## Settings (your owner edits these)

```text
host            = bb1.local       # BB1's address (the tool's default). 192.168.42.1 on BB1's own network "BB1"
max_speed       = 0.3             # wheel turns/s, forward or back (firmware allows 3.0)
max_turn        = 0.4             # turns/s of left-minus-right wheel difference (firmware allows 5.0; one full spin on the spot = about 5.4)
max_drive_s     = 3               # longest single drive command, seconds (tool allows 10)
max_travel_m    = 0.5             # never end up farther than this from where you armed
may_arm         = ask             # ask = ask your owner before every arm; yes = arm when needed
person_present  = required        # required = only drive while your owner says they are watching
surface         = unknown         # the floor: wood, carpet... (your owner says; it goes in the log)
allowed         = state, api, arm, drive, spin, stop, disarm, log   # everything else is off limits (see Never)
                  # through MCP the same are: bb1_state, bb1_api, bb1_arm, bb1_drive, bb1_spin, bb1_stop, bb1_disarm, bb1_log,
                  # plus the read-only logs_schema, logs_runs, logs_show, logs_ticks, logs_sql
```

If a setting is missing, use the most careful choice: smaller numbers, ask first.

## What BB1 is

- Two wheels side by side, a tall body, balancing like an inverted pendulum. It is **always falling** and the control loop catches it 200 times a second. It never stands on its own: motors off = it tips over.
- **Front** = the side with "hectorsvill" printed on it. Forward and turn right are positive.
- Its brain is a Raspberry Pi Pico W. It serves a small HTTP API; you reach it through the command-line tool below. It answers one request at a time, so never send requests in parallel.
- **Balancing** (armed) = motors on, it stands and can drive. **Disarmed** = motors off. If it is lying down when disarmed, it stays lying down. **Auto-arm**: stood upright and held still for 1 s, it starts balancing by itself. `disarm` turns auto-arm off; any `arm` (even a refused one) turns it back on, so after an `arm` a fallen BB1 starts balancing again as soon as someone stands it up.
- It stops balancing by itself if it leans past 45 deg (fell), its wheels run 4 turns/s or more off the speed it is aiming for (lifted: they spin up in the air), its battery is low, the IMU fails, or the control loop runs late. Then it falls over or lies where it is.

### Units

| You think in | BB1 uses | Convert |
|---|---|---|
| distance | wheel turns | 1 turn = 0.178 m, so 1 m = 5.6 turns |
| speed | wheel turns/s | 0.3 turns/s = 0.053 m/s; 1.0 = 0.18 m/s; 3.0 (the firmware's top) = 0.53 m/s |
| heading | turns of left-minus-right wheel difference | turning on the spot, 1 turn = about 67 deg, so a 90 deg turn = 1.35 turns, a 360 = 5.4 (re-measured 2026-11-03 with the gyro: the wheels scrub, so the 127 mm track's 80 deg per turn is too much) |
| turn rate | turns/s of difference | 0.4 = about 27 deg/s; the firmware allows 5.0 (a 360 in about 1 s), your cap is `max_turn` |

Positions count from power-on, so only differences mean anything. The numbers are measured, not exact: turns come out within about 10-20%, and BB1 rolls on up to about 0.5 turn after a stop.

## The tool

Run every command from the repo folder. Each one prints **one line of JSON** and exits.

```bash
export BB1_HOST=bb1.local                                       # once, from the settings (bb1.local is also the default)
tools/venv/bin/python tools/bb1.py api                          # firmware version and limits (run first)
tools/venv/bin/python tools/bb1.py state                        # what BB1 is doing now
tools/venv/bin/python tools/bb1.py arm                          # start balancing (it must be standing near upright)
tools/venv/bin/python tools/bb1.py drive --speed 0.3 --for 2 --max 0.3 --max-turn 0.4 --max-for 3   # forward 0.3 turns/s for 2 s, then stop
tools/venv/bin/python tools/bb1.py drive --turn 0.4 --for 2 --max 0.3 --max-turn 0.4 --max-for 3    # turn right on the spot for 2 s
tools/venv/bin/python tools/bb1.py spin --deg 90 --max-turn 0.4                # turn exactly 90 deg right on the spot (left: --deg -90)
tools/venv/bin/python tools/bb1.py stop                         # stop driving, keep balancing
tools/venv/bin/python tools/bb1.py disarm                       # motors off: BB1 FALLS OVER
tools/venv/bin/python tools/bb1.py log start --surface wood -n "square"   # label this session's log
tools/venv/bin/python tools/bb1.py log stop                     # save the log now (answers with its run_id)
```

Always pass your settings as the caps: `--max` = `max_speed`, `--max-turn` = `max_turn`, `--max-for` = `max_drive_s`. `drive` blocks until it is done and BB1 has settled (or 2 s have passed). A drive with `--speed` and `--turn` both 0 is refused. If the tool cut anything you asked for (`--speed` past `--max`, `--turn` past `--max-turn`, `--for` past `--max-for` or 10 s), the answer's `cut` list says so: plan with what you got, not what you asked.

`spin --deg N` turns on the spot by N degrees (right +, at most 360 per command; pass `--max-turn` = `max_turn`). BB1 counts the turn with its own gyro and lands on it by itself, within about ±5 deg on wood; the tool waits until it has. The answer has `turned_deg` (the gyro's count of what it really turned), `error_deg`, `landed` and `state`. For a heading, prefer `spin` over `drive --turn`: a timed turn's angle depends on the floor and Wi-Fi, a spin's doesn't. A spin isn't perfectly in place: on wood each 90 deg rolled BB1 **forward 0.08-0.31 turns** (1.4-5.5 cm, run 198; `moved_turns` in the answer). Count that in your position.

### Through MCP instead

Your owner may have connected BB1 as MCP tools instead (`tools/bb1_mcp.py`; to connect it: `claude mcp add bb1 -- "$PWD/tools/venv/bin/python" "$PWD/tools/bb1_mcp.py" --host bb1.local --max-speed 0.3 --max-turn 0.4 --max-for 3`, from the repo folder, Python 3.10+). Then you call tools, not commands, and everything in this file still applies: each tool runs the same `bb1.py` command with the same checks and gives the same JSON, plus `exit_code`.

| Tool | Command |
|---|---|
| `bb1_api`, `bb1_state(full)` | `api`, `state [--full]` |
| `bb1_arm(seconds)` | `arm` (ask first if `may_arm` = ask) |
| `bb1_drive(speed, turn, secs)` | `drive`; the caps are set when the server is added, so you don't pass them (the tool's description shows them) |
| `bb1_spin(deg)` | `spin --deg`; the turn cap is the server's |
| `bb1_stop`, `bb1_disarm` | `stop`, `disarm` |
| `bb1_log(action, note, surface)` | `log status` / `start` / `stop` |
| `logs_schema`, `logs_runs`, `logs_show`, `logs_ticks`, `logs_sql` | `tools/logs.py` (read only) |

If the server's caps are higher than your settings, your settings win: ask for no more than them.

**BB1's telemetry is recorded for you.** Once BB1 balances after an `arm` or `drive`, every 5 ms record is saved to `logs/runs.sqlite` when your session ends (disarmed 10 s, or 5 min without a command). Label it at the start with `log start --surface <surface setting> -n "<your task>"`, and finish with `log stop`: its answer has the `run_id`, which you give your owner and can read with `tools/logs.py show <run_id>`. `log status` shows whether it's recording. The mock is never recorded.

Every command you run is recorded too (`logs/agent_commands.csv`: time, command (`mcp:drive` and so on when it came through MCP), options, exit code, answer), so your owner can replay your session next to BB1's own logs.

To see what happened (your commands, BB1's runs, every tick), use `tools/venv/bin/python tools/logs.py` (read only, JSON out): `runs`, `show <run>`, `ticks <run>`, `schema`, `sql`.

**Exit codes:** 0 done; 1 BB1 refused, or a safety check ended the command (read `error` and `reason`); 2 BB1 didn't answer.

### Reading `state`

```json
{"ok": true, "state": {"armed": true, "reason": "", "auto_state": "balancing", "pitch_deg": -0.4,
 "speed": 0.0, "position_turns": 12.31, "drive": {"speed": 0.0, "turn": 0.0, "left_ms": 0},
 "enc_fault": 0, "trim_learned_deg": 1.25, "yaw_deg": 93.4, "batt_v_per_cell": 3.92, "ip": "192.168.1.240"}}
```

| Field | Meaning | Act on it |
|---|---|---|
| `armed` | balancing now | false: you can't drive; see `reason` |
| `reason` | why it last stopped or refused (`""` = nothing) | see the table below |
| `auto_state` | what auto-arm is doing | "waiting: stand it up..." = a person must stand it up |
| `pitch_deg` | lean, forward + | near 0 while balancing; about ±90 = lying down |
| `speed` | wheel turns/s now | about 0 before you call a stop done |
| `position_turns` | wheel position since power-on | subtract to get distance |
| `enc_fault` | 0 ok; 1 left, 2 right wheel sensor not counting | not 0: stop, tell your owner (a wire) |
| `trim_learned_deg` | deg BB1 has learned to add to its balance point while standing still (trim learn `tl`, firmware 3.5; null before). Kept across drives and stops, cleared at power-on | nothing to do. Report it at the end of a session. Stuck at ±3 (the limit): tell your owner, the trim is far off |
| `yaw_deg` | heading from the gyro since power-on, deg, right + (firmware 3.11; null before). Drifts a few deg a minute | subtract two readings to know how far it turned; better than `turned_turns`, which the wheels' scrub inflates |
| `batt_v_per_cell` | battery, volts per cell (null = not measured) | under 3.5: finish up and tell your owner; under 3.3 BB1 stops itself |

`drive` answers with `moved_turns` and `turned_turns` (how far it really went), `turned_deg` (the gyro's angle, firmware 3.11), `settled` and a fresh `state`. These count from the start of that one command: BB1 can still roll a little after the answer (up to about 0.3 turns back, run 198), so **track where you are with `state.position_turns` and `state.yaw_deg` read before and after each move**, not by adding up `moved_turns`.

### Reasons it stops or refuses

| `reason` | What happened | What you do |
|---|---|---|
| `fell over (leaned past 45 deg)` | it fell | stop; ask your owner to stand it up |
| `picked up (wheels spinning in the air)` | lifted | stop; wait for your owner |
| `not upright: hold it near the balance point to arm` | arm refused: it isn't standing | ask your owner to stand it up |
| `battery low (under 3.3 V per cell)` | battery flat | stop for the session; it needs charging |
| `disarmed by you` | someone (you or your owner) disarmed it | don't re-arm without asking |
| `arm time finished` | a timed arm ran out | arm again if allowed |
| `IMU stopped answering`, `control loop running late`, `gyro not calibrated yet` | something is wrong inside | stop; tell your owner the exact text |
| `a firmware update is running`, `a motor test is running`, `disarmed to calibrate` | someone else is working on it | wait and ask |
| `lost the web page ...`, `the page was hidden ...` | a page armed it and went away | arm again if allowed |

## How to drive

Work in small steps and look after every one:

1. `api`: check the version line at the top of this file.
2. `state`: check battery, `enc_fault`, and whether it is armed. Then `log start --surface <surface> -n "<task>"`.
3. Not armed: if `may_arm` is `ask`, ask your owner and say what you will do. They stand it up. Then `arm`. If it refuses, read `reason`.
4. Plan the move in turns (see Units). Keep a running total of how far you are from where you armed; never let it pass `max_travel_m` (0.5 m = 2.8 turns).
5. `drive` for at most `max_drive_s`. Read `moved_turns`, `turned_turns` and `state`.
6. Compare with what you asked. **Distance per second is not repeatable:** in run 198 (wood) the same 2 s at 0.3 moved 0.14, 0.28, 0.46 and 0.64 turns. Plan a move, then measure it with `state.position_turns`; never assume time x speed. Short is normal, and the floor matters: on wood forward averages about 60-90% of the asked speed; on carpet much less (3 s at 0.3 moved anywhere from −0.06 to +0.29 turns, back reached a third of the asked speed, 1 s turns at 0.4 gave 0.27, and it often hadn't settled after 2 s). Correct with the next step, but don't expect to land on a target: five tries on carpet reached half of a 1-turn target. Wrong direction, or `ok: false`: stop and think before the next move.
   The smallest move you can make is about 0.15 turns (2.7 cm): `drive` for 1 s at 0.3 moves about that, sometimes less. Errors smaller than that you can't correct yet (there is no position target); report them instead of chasing them.
7. Repeat from step 5. When done: `stop`, leave it balancing unless your owner says otherwise, then `log stop` and give your owner the `run_id`.

Turning on the spot to a heading: `spin --deg 90` (exact, by the gyro). A timed turn: `drive --speed 0 --turn 0.4`. Curves: `drive` with both at once. To go back where you started, drive the same distance the other way: there's no position target yet, so expect some error.

## Safety rules

- Your owner must be watching (`person_present`). If you aren't sure they still are, `stop` and ask.
- Nothing faster than `max_speed` / `max_turn`, no command longer than `max_drive_s`, never past `max_travel_m`.
- **`disarm` makes BB1 fall over.** Use it only when your owner asks, or when it is about to hit something and `stop` isn't enough.
- After any surprise (fell, lifted, wrong direction, a reason you don't understand, exit code 2 twice in a row), stop and tell your owner what you saw (paste the JSON). Don't retry blindly.
- If the tool dies or Wi-Fi drops, BB1 stops by itself within 1 s and keeps balancing. That's normal, not an emergency.
- Stopping takes room: BB1 slows at 3 turns/s per second or faster, so from 1 turn/s it rolls about 0.2 turn more, from 3 turns/s about 1.5 turns (0.27 m). The tool's own travel stop (3 wheel turns from where the drive started) is a distance: at 3 turns/s it ends a drive after about 1 s, and BB1 rolls on up to about 3 turns more. Keep `max_travel_m` in mind before the drive, not after.
- BB1 has no eyes yet: it can't see walls or edges. Your owner's word about the space is all you have.

## Never

These change how BB1 works or can damage it, and are only for your owner (`allowed` doesn't list them):

- changing gains or settings (`/gains`, `/gains/save`, `/gains/defaults`, `/alpha`, `/cal`)
- motor tests (`/motor`): wheels must be off the floor
- firmware updates (`/update`, `tools/flash.py --execute` or `--yes`). `tools/flash.py` with no `--execute` only prints a JSON plan and does not flash. Do not pass `--execute`, `--yes`, or `--force`.
- Wi-Fi settings (`/wifi`, `/wifi/forget`): BB1 would restart and could drop off the network (refused with `409` while armed)
- calling the HTTP API directly to get around the tool's limits (use `bb1.py spin`, not `POST /spin`)
- changing the MCP server's caps or how it is started (`claude mcp add` / `remove`, its `--max-*` options), or editing `tools/bb1.py` / `tools/bb1_mcp.py`: the caps are your owner's

## More

- For owners: a ready prompt to start an agent session, with task ideas: `docs/AGENT_PROMPT.md`.
- Every endpoint: `docs/API.md`, or `bb1.py api` for the live list with its limits.
- The tool itself: `tools/bb1.py` (its top comment lists every safety check); the MCP server: `tools/bb1_mcp.py` (`docs/TOOLS.md`).
- Reading logs: `tools/logs.py` (read only; `docs/TOOLS.md`).

## Keeping this file current (for maintainers)

Update this file in the same change as any of these, and set the version line at the top to the new `API_VERSION`:

- an endpoint, argument, limit or `/state` field changes (`web.h`, `api.h`, `config.h`): bump `API_VERSION` in `api.h`
- a disarm reason or auto-arm state is added or renamed (`control.h`, `auto_arm.h`)
- `tools/bb1.py` gets a new command, option, output field or safety check (including what goes into `logs/agent_commands.csv`), `tools/session_log.py` changes when it records or stops, or `tools/bb1_mcp.py` gets a new tool or option
- a unit is measured again (wheel turn, track width, counts per turn)

`tools/check_agent_doc.py` (run by CI) fails when the version line here doesn't match `API_VERSION` in `firmware/bb1/api.h`.

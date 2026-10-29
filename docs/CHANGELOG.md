# Change Log

Newest first.

## Open items

- Measure counts per wheel turn cleanly (520 assumed)
- Mount and wire the HC-SR04 on the front end
- Battery cradle (record the pack's size and capacity)



## 2026-10-28 - Fast drive step 1: `aff`, `dacc`, `lmax` (firmware 3.4, flashed 2026-10-29)

- **Why:** "those motors are fast but we're not utilizing them." Driving at 0.5 turns/s on wood BB1 used about 24 of 200 PWM and leaned under 1 deg on average (max 2.8). The limits were the firmware's: speed ramp 1 turn/s² (`DRIVE_ACCEL`), setpoint shift ±6 deg (`SPEED_SHIFT_MAX`), and the lean only came once a speed error had built up (sp, sdi), so BB1 lagged and fell short. Physics: to accelerate at a, an inverted pendulum must lean atan(a/g); 1 turn/s² = 0.178 m/s² = about 1.04 deg
- **Three new live settings, defaults = the old behavior** (on /tune with tooltips, serial, `POST /gains`, `/state` gains, `gainsLine`, saved in flash as settings layout v9; older saves load with the defaults):
  - `aff` (deg per turn/s², 0-5, default 0 = off): acceleration lean feedforward. `drive.h` takes the ramped speed target's acceleration each tick (+dacc, -dacc or 0), smooths `aff x accel` over 0.05 s, and `control.h` adds it to the speed loop's setpoint shift in smode 0: lean forward while speeding up forward, back while slowing. Like Betaflight feedforward
  - `dacc` (turns/s², 0.1-6, default 1): the speed ramp, replaces the constant `DRIVE_ACCEL`. Turn ramp unchanged
  - `lmax` (deg, 6-15, default 6): the clamp on the setpoint shift and on the `sdi` lean while a speed target is set or ramping down; 6 (`SPEED_SHIFT_MAX`) standing. Raising is instant, lowering eases at 10 deg/s (`LEAN_LIMIT_EASE_DPS`) so the setpoint never jumps when a drive ends
- `DRIVE_MAX_SPEED` stays 1.0; step 2 (raise it: widen the int8 drive log fields, rework the lift detector) is planned in `docs/FIRMWARE_PLAN.md` section 7
- Log: no new columns (the setpoint column already includes the shift), `LOG_FORMAT` stays 11. `# gains_ver` line gets `accel_ff`, `drive_accel`, `lean_max`; the `# drive_max_speed` line's `drive_accel` is now the live `dacc`, plus `speed_shift_max` and `lean_limit_ease_dps`
- `GET /api` version 4 (gains `aff`, `dacc`, `lmax` with their ranges); `docs/AGENT.md` version line 4. `/state` buffer 1400 -> 1536 bytes (widest answer about 1440 with the new fields)
- Tools: `mock_bb1.py` (gains, limits, answer text, api 4), `logger.py` `GAIN_KEYS`, `runs_db.py` header names (`SCHEMA_VERSION` 3: the runs and gains tables get `aff`, `dacc`, `lmax` columns, rebuilt by itself)
- Firmware 3.4: 550,076 B flash (+2,784 vs 3.3), 217,992 B static RAM (+32)
- Docs: `firmware/README.md`, `docs/API.md`, `docs/AGENT.md`, `docs/PID_TUNING.md`, `docs/FIRMWARE_PLAN.md`, `docs/TOOLS.md`
- Flashed over Wi-Fi 2026-10-29 07:55 (`fast-drive b1db774`): `/api` says 4, `/sys` 3.4; saved gains unchanged, `aff 0`, `dacc 1`, `lmax 6` (today's behavior); a `/log` read had no gaps. Floor test next

## 2026-10-28 - `tools/todo.py`: a todo dashboard from the vault Roadmap

- **Why:** one page (and JSON for agents) that shows what's left per milestone and what state things are in, without a second todo list to keep in step
- **The vault stays the source:** todos are the checkboxes in `BB1 - Roadmap.md`, the focus is the Dashboard's Now / Next / Later. `list` groups them by section with done/total; `done` / `undo` rewrite only that one checkbox line (re-read first, refused if the line changed, temp file + rename); `add` appends `- [ ] text` to a section. Never runs git in the vault (Obsidian Git commits it)
- **`status`:** last flash from `logs/flash_history.csv`, runs and armed minutes from `logs/runs.sqlite` (read only), git branches, open PRs and the last 3 CI runs through `gh`. Each fact fails soft; nothing talks to the robot
- **`todo.py serve`:** a page at http://localhost:8091 (this computer only, `tools/todo_page.html`, same look as the logs page): status strip, progress bars, open todos first, tick a box, add a todo. Same JSON API for agents (`/api/todos`, `/api/status`, `POST /api/done|undo|add`)
- Docs: `docs/TOOLS.md`, `tools/README.md`, `CLAUDE.md`

## 2026-10-28 - CI tests the Python tools; MCP server `tools/bb1_mcp.py`

- **Why:** CI compiled the firmware but nothing checked the Python tools; and milestone 3 wants BB1 as MCP tools an agent app can call
- **New `tools/tests/test_tools.py`** (stdlib `unittest`, about 10 s, no robot, no private logs): compiles every `tools/*.py`; builds `runs.sqlite` from `tools/tests/fixtures/` (two 60-tick logs cut from real ones, a fall on wood and a carpet drive with its steps shortened to fit, plus tiny `flash_history.csv` / `agent_commands.csv`; checked free of Wi-Fi names and passwords) and checks every table; checks `logs.py`'s JSON and that `sql` refuses writes; runs `bb1.py` against `mock_bb1.py` (refusals, caps, `cut`, exit code 2, no command-log rows for the mock); and the MCP server in memory and over stdio. Run it with `tools/venv/bin/python -m unittest discover -s tools/tests -v`
- **New CI workflow `.github/workflows/tools.yml`** ("Test tools", PRs and pushes to main): installs `tools/requirements.txt` and runs the tests on Python 3.9 (macOS's Xcode Python, what `tools/venv` uses today; the MCP tests skip) and 3.12
- **New `tools/bb1_mcp.py`** (official `mcp` SDK 2.x, `MCPServer`, which is FastMCP renamed): tools `bb1_api`, `bb1_state`, `bb1_arm`, `bb1_drive`, `bb1_stop`, `bb1_disarm`, `logs_schema`, `logs_runs`, `logs_show`, `logs_ticks`, `logs_sql`. No robot logic of its own: it calls `bb1.run()` and the `logs.py` functions, so every safety check and the command log stay in `bb1.py`. The owner sets the caps when adding it (`--max-speed 0.3 --max-turn 0.4 --max-for 3`, AGENT.md's defaults); one robot command at a time. Tested on the mock only
- **`tools/bb1.py`:** `run(argv)` returns the answer instead of only printing it (the MCP server uses it); new `drive --max-turn` (turn cap, default `--max`, so old calls work the same) and `--max-for` (can only lower the 10 s); command-log rows from MCP say `mcp:<command>`
- **`tools/logs.py`:** `--db <file>` reads another catalog (the tests' fixture one)
- **`tools/requirements.txt`:** `mcp>=2,<3` only on Python 3.10+ (pip skips it on 3.9, so the current venv still installs)
- Docs: `docs/TOOLS.md`, `tools/README.md`, `docs/AGENT.md` (MCP section, caps, allowed tools, Never list; `/api` version unchanged), `docs/API.md`, `CLAUDE.md`

## 2026-10-28 - Firmware 3.3: 8 KB more RAM (the log buffer stores no tick number)

- **Why:** firmware 3.2 used 226.0 KB of static RAM (86%, the compiler warned), and milestone 3 adds more (JSON answers, odometry, events)
- **Where RAM goes** (`arm-none-eabi-nm` on the build): the log ring buffer 152.0 KB (67%), lwIP's TCP/IP buffers 58.4 KB (packet pool 36.8 KB, lwIP heap 16 KB; prebuilt in the core, already its smallest "IPv4 Only" setting), CYW43 Wi-Fi 3.2 KB, USB and core 11.5 KB, everything else of ours 0.6 KB. Table in `firmware/README.md`, "RAM"
- **Change:** `telemetry.h` splits the record into `LogData` (what the buffer stores) and `LogRecord` (`LogData` + `tick`). Record n in the buffer is always tick n (core 1 pushes exactly one record per tick, and a calibration pause skips the tick counter too), so `logGet()` writes n back into `tick` instead of storing it. 76 -> 72 bytes a record: static RAM 225,960 -> 217,960 bytes (heap at link time 36.2 -> 44.2 KB). Same 10 s of history, same CSV, same endpoints, control code untouched. `logMetaLines()` reserves its String up front (one heap block instead of many)
- **Not changed, our choice:** a shorter history (`LOG_SECONDS`): 8 s saves 28.8 KB more (and ends the compiler warning, under 75%), 6 s saves 57.6 KB. Recorders poll `/log` every 0.25 s, so they'd still have 3x the worst stall seen (2.5 s), but a recorder paused longer than that loses records. Packing `pitch`/`pitch_rate`/`accel_pitch` into 16 bits would save 12 KB but changes the logged numbers: not done
- **`/log` and the 1-2.5 s quiet spells:** `handleLog` already streams in 2 KB pieces from a stack buffer, no big Strings or heap, so the RAM work doesn't touch it. A normal answer (0.25 s of records, about 8 KB) fits lwIP's 11.7 KB send buffer, so the Pico isn't blocked waiting for the Mac either; the 1-2.5 s steps look like TCP retransmission timeouts instead: lwIP resends a lost packet only on its 0.5 s timer (about 1 s at the least), a lost connection-opening packet (SYN) is resent after about 1 s, then 2 s, and every request is a new connection (`route()` closes it), so each one has a handshake that can only be resent on a timer. Not proven. Next test, no firmware change: capture a stall with `tcpdump` on the Mac and look for a resent packet about 1 s after the gap starts
- `version.h` 3.3; mock `/sys` `log_buffer` 144000. Flashed over Wi-Fi 2026-10-28 21:18 (`ram 72047e4`): `/sys` shows 3.3, `log_buffer` 144000, `ram_static` 217960, heap 13.9 of 44.2 KB used; gains unchanged (kp 18 / ki 100 / kd 0.5, sp 3, sdi 4, th 60); a `/log` read gave 418 ticks with no gaps, tick column = `/state` tick

## 2026-10-28 - One store for all logs: recorders write to `runs.sqlite`, `tools/logs.py` reads it

- **Why:** `logs/runs.sqlite` is now the unified way to store and find BB1 data, and an AI agent (or Hector) should be able to see and use any log without opening CSVs
- **Recorders catalog as they save:** `logger.save_log` (used by `logger.py`, `handheld.py`, `drive_test.py`, `drive_explore.py`) calls `runs_db.catalog()` right after writing the CSV. Only logs in `logs/` go in; a catalog error prints a line and never loses the recording (`build` catches up)
- **New `tools/logs.py`** (read-only, JSON out): `schema` (tables, what each is for, columns, rows), `runs` (`--where` SQL filter), `show` (one run with its disarms, steps, gains, commands, header), `ticks` (columns, time range, thinned to `--limit` rows and says so), `download` (the original CSV, or a slice), `sql` (one SELECT). A run is its id, file name or a unique part of it. The database is opened read only, so writes are refused
- **`logs.py serve`:** a page at http://localhost:8090 (this computer only, `tools/logs_page.html`): filter runs, open one, plot any columns over any time range, download the original or the slice, run SQL. It uses the same JSON API an agent can call (`/api/...`)
- Docs: `docs/TOOLS.md`, `tools/README.md`, `docs/CALIBRATION_AND_LOGS.md`, `CLAUDE.md`

## 2026-10-28 - `runs_db.py` holds all the data (schema 2)

- **Why:** "with SQLite we should always track all data": the catalog had one row per log and per disarm; the telemetry rows, drive steps, live gain changes, flashes and agent commands were still only in files
- **New tables:** `ticks` (every telemetry row, 2.3 M, with `t_s` and wheel `speed_tps` / `turn_tps` in turns/s, old 3.3x bug fixed), `steps` (each drive step, asked vs got), `gains` (every gains version a log used), `meta` (every header line word for word), `flashes` (`logs/flash_history.csv`), `commands` (`logs/agent_commands.csv`, written by `bb1.py` from branch `agent-md`, linked to the log recording at the time). `runs` gets a `run_id` that the per-log tables use instead of `file`
- Named queries `steps` and `commands` added. The catalog now goes in the `--logs` folder (before, a build of another folder would have emptied the main catalog). About 13 s and 400 MB for 158 logs; still never committed
- `logs/bb1_2026-10-28_09-52-44_sn3-agent-doc-test-weak-drive-check.csv` relabelled `# surface,carpet` (it was recorded on carpet; drive_test.py wrote its default, wood)
- Docs: `docs/TOOLS.md` (table of tables), `docs/CALIBRATION_AND_LOGS.md`, `CLAUDE.md`

## 2026-10-28 - `tools/runs_db.py`: a SQLite catalog of every run

- **Why:** 157 logs (187 min, 347 MB in bb1-private) are too many to open one by one. Now "every wood run at ki 100 longer than 5 min, and how did it end" is one query
- **New `tools/runs_db.py`:** `build` reads `logs/bb1_*.csv` into `logs/runs.sqlite`: table `runs` (one row per log: label, surface, firmware and version, log format, the starting gains as columns, duration, time armed and driving, disarm count, pitch error RMS, battery start and end, outcome) and table `disarms` (one row per disarm: time, reason code and text). Incremental: unchanged files (same size and time) are skipped; `--rebuild` starts over. `query "SQL"`, `tables`, and named queries `recent`, `longest`, `falls`, `by-gains`. Parsing reuses `dataset.py` and `check_dump.py`; mock logs skipped. Only the standard library's sqlite3 plus pandas
- The CSVs stay the source of truth; the catalog is never committed (bb1-private's `.gitignore` now has `*.sqlite`)
- Docs: `docs/TOOLS.md` (with real example output), `docs/CALIBRATION_AND_LOGS.md` ("Finding runs"), `CLAUDE.md`

## 2026-10-28 - `docs/AGENT.md`: the manual an AI agent drives BB1 from

- **Why:** an agent needs one place that says how to drive BB1 safely: the API reference is written for people and doesn't say what to do when something goes wrong
- **New `docs/AGENT.md`:** settings the owner edits at the top (address, max speed and turn, longest command, max travel, may it arm, person present, allowed commands), what BB1 is, units in metres and degrees, the `tools/bb1.py` commands, what every `state` field and disarm reason means and what to do, a step-by-step way to drive, safety rules, and what it must never touch (gains, motor tests, firmware updates, Wi-Fi)
- **Kept current:** AGENT.md names the `/api` version it was written for (3). `bb1.py api` (new) prints `/api`, and an agent runs it first and stops on a mismatch. New `tools/check_agent_doc.py` runs in CI and fails when `API_VERSION` in `api.h` and AGENT.md's version line differ. `CLAUDE.md` lists what else needs an AGENT.md update
- Docs: `docs/TOOLS.md`, `docs/API.md`, `CLAUDE.md` (milestones 3 and 4 named; checklists in the vault Roadmap)
- **`bb1.py` records every command** (asked by Hector: "with sqlite we should always track all data"): one row per call in `logs/agent_commands.csv` (time, host, cmd, args as JSON, exit code, the JSON answer), header written on a new file; calls to the mock (localhost, 127.*) aren't recorded; a failed write never breaks the command. `tools/runs_db.py` imports it (branch `logs-db`)
- **`bb1.py drive` fixes** from a live test on carpet: speed and turn both 0 is refused (it used to run an empty drive), and the answer's `cut` lists anything the tool cut (`--for 11` -> 10, speeds past `--max`). AGENT.md and TOOLS.md have the carpet results (much weaker than wood). `docs/API.md` no longer lists the wheel and track measurements as to do
## 2026-10-28 - 1N5819 diode fitted, power rule gone

- 1N5819 Schottky diode soldered into the D153C 5V to VSYS wire, stripe toward the Pico. USB no longer back-feeds the D153C's 5V regulator, so USB and the battery go on and off in any order
- Docs: `CLAUDE.md` (Hardware, pins, Power), `docs/WIRING.md`, `docs/FLASHING.md`, `firmware/README.md`, `README.md`, `docs/FUTURE_INTEGRATIONS.md`, the wiring diagram, sketch comments. Battery care in `FLASHING.md` now says BB1 reads the battery (GP26, since 2026-10-24); the pin table lists GP26 as wired, not reserved. The diagram now shows the battery sense wire
## 2026-10-27 - `GET /api` (milestone 3 step 2), not flashed yet

- **Why:** so a program or an AI agent can discover every endpoint, its arguments, whether it moves the wheels, and the limits (drive speed, command length, gain ranges) without reading `docs/API.md`
- **New file `firmware/bb1/api.h`:** one table `API_ENDPOINTS` (method, path, args, answer type, moves, one-line description) and `apiJson()`. The limits are read from `config.h`, so they can't drift from what the firmware enforces. `API_VERSION` 1 goes up whenever the list changes. Routed in `web.h`; the table lives in flash (RAM use unchanged: 225,696 bytes static)
- `tools/mock_bb1.py` answers `/api` with the same list (`firmware: bb1 MOCK`); its gain limits are now one `GAIN_LIMITS` table shared with `/gains`. Docs: `docs/API.md` (new `GET /api` section), `docs/FIRMWARE_PLAN.md`, `docs/WEB_INTERFACE.md`
- No log columns changed (log format stays 11)

## 2026-10-27 - firmware version and build id (version 3.2), branch `ota`

- **Result:** flashed over Wi-Fi twice (Hector 14:02, Claude 14:04): `/sys` shows `version 3.2`, `build_id ota 8fb066a ...`, `flash_history.csv` has both lines. From now on Claude may run `flash.py --ota` itself (that's the point of OTA)
- **Why:** the first `flash.py --ota` worked (BB1 restarted into the copy), but flash.py said it didn't: arduino-cli had reused its cached compile, so `__TIME__` (the `/sys` `build`) never changed. And we had no way to say which firmware is on BB1
- **New `version.h`:** `FIRMWARE_VERSION` "3.2" (milestone.change, bumped by hand: 3.1 = `GET /api`, 3.2 = OTA) and `BB1_BUILD_ID` from `build_id.h`, which `flash.py` writes before every compile (branch, commit, `+` for uncommitted firmware changes, time; gitignored). The file changes every time, so the sketch always recompiles. Built without flash.py: `"no build id"`
- **Where it shows:** `/sys` and `/api` (`version`, `build_id`; `API_VERSION` 3), the main page's Pico memory panel (Firmware tile), and a new `# firmware_version,3.2,build_id,...` line in every log file (same log format)
- **`flash.py`:** prints the version and build id, waits for that exact `build_id` after an OTA restart, and adds a line to `logs/flash_history.csv` (bb1-private) after every flash: time, version, build id, usb/wifi, result. Mock and API.md updated

## 2026-10-27 - firmware updates over Wi-Fi (OTA), branch `ota`, flashed over USB, then over Wi-Fi

- **Result:** flashed over USB once (`/api` lists 23 endpoints; saved home Wi-Fi and gains survived the new split). Tested on BB1, lying down: a wrong password gets `401`; an upload cut off halfway leaves the old firmware running and arming is not blocked afterwards. The first `flash.py --ota` sent 338 KB in 10 s and BB1 restarted into it (the build time check failed only because of the cached compile, fixed above)

- **Why:** every firmware change needed the USB cable and the power order. arduino-pico (6.3.0) already has what it takes: its Updater writes the new firmware into LittleFS, and the small OTA bootloader the core always links copies it in at the next start
- **New `ota.h`, `POST /update`:** multipart field `firmware` (plain or gzipped), Basic auth `bb1` / `BB1_OTA_PASSWORD` (new line in `secrets.h`, template in `secrets.example.h`). Refused while armed, during a motor test, or unless lying down (same rule as `flash.py`). While it runs, `otaActive` (shared.h) makes core 1 refuse every arm (new reason 14 "a firmware update is running") and motor test and skip auto-arm, and core 1 confirms the motors are off before the first flash write (writing flash pauses core 1). A broken-off upload changes nothing; power lost during the copy repeats the copy at the next start; BOOTSEL stays the last resort
- **Flash layout:** every build now uses Flash Size "Sketch: 1MB, FS: 1MB" (`:flash=2097152_1048576` in `flash.py`, CI and FLASHING.md). The saved settings stay at the same address (the last 4 KB) in both layouts. `ota.h` fails the compile with any other layout. Firmware 547 KB of 1020 KB (OTA adds 28 KB)
- **`/sys`** gains `fs_total` and `build` (compile time), so a tool can see the new firmware is running
- **`tools/flash.py --ota [--host]`:** compile, check `/state` (lying down, disarmed), send the build gzipped (336 KB), wait for the new `build` in `/sys`, show auto-arm and gains. The first OTA firmware goes on over USB once
- Mock answers `/update` (refused: nothing to replace) and the new `/sys` fields; `dataset.py` maps reason 14. Docs: API.md, FLASHING.md (new section), TOOLS.md, CALIBRATION_AND_LOGS.md

## 2026-10-26 - I-term relax (`irelax`) and the Wi-Fi drops, flashed 23:30

- **Result:** drive commands live 93-98% of the moving time (was 59%); the training explore ran 11.2 min (was 4.8). `irelax` 0.5 works as built (flag 64) but **does not stop the creep**: 3-5 of 10-12 stops still roll back up to 0.32 turns/s. During the drain the I term rebuilds faster than it drains: after a forward drive the setpoint sits about +1 deg forward (speed loop + sdi), the body lags, the error is negative and ki 100 winds I negative again. `drive_explore.py`: the position poll now rides out a slow `/state` (one ended a run after 18 s)

- **Why:** the first training run (gains v26) stopped after 4.8 min at the box edge: told "home fwd 0.25", BB1 rolled backward 0.1-0.4 turns/s, angle I at −4 to −9 PWM. And 41% of the moving time the drive command had run out
- **New setting `irelax`** (s, default 0.5, 0 = off): when a drive command has ramped down to a stop, the angle I term decays toward 0 with that time constant for 2 x irelax; driving again cancels it. Log flag 64 (`FLAG_I_RELAX`, same log format). Live on /tune, serial, `/state` gains, `# gains_ver` line `i_relax`, saved in flash (settings layout v8; older saves load with the default). Mock and `logger.py` `GAIN_KEYS` updated
- **Wi-Fi drops found:** measured from the Mac with BB1 standing, `/drive` and `/state` always answer within 0.15 s, but about every 10 s one `GET /log` answer takes 1-2.5 s, and the Pico serves nothing else meanwhile. `drive_test.py` and `drive_explore.py` send everything from one loop, so the 1 s drive command ran out. Fix (tools only): each command now lasts 3 s (`CMD_MS`, still re-sent every 100 ms, stopped after every move); if the script dies BB1 stops within 3 s. Why `/log` is slow on the Pico is still open (the server already sets no-delay)

## 2026-10-26 - logs backed up in the private repo `bb1-private`

- New private GitHub repo `hectorsvill/bb1-private` (`~/Documents/GitHub/bb1-private`): `logs/` (all 159 real runs, 290 MB), plus `models/`, `media/` and `business/` for what we don't publish. Its README has the rules (real runs only, no secrets, photos without GPS, Git LFS before any file reaches 100 MB)
- This repo's `logs/` is now a link to `bb1-private/logs`; every tool writes to the same path as before. `.gitignore` says `logs` (no slash) so git ignores the link. Docs: `docs/TOOLS.md`, `docs/FIRMWARE_PLAN.md` section 6
- Docs now say "we" instead of naming who did what

## 2026-10-26 - wheel turn measured: 177.8 mm

- We rolled BB1 one full wheel turn on the wood floor: **177.8 mm per turn** (= 56.6 mm wheel, tyre squash included). The tools assumed 204 mm (65 mm wheel), 13% too far. `drive_explore.py` odometry (`WHEEL_TURN_M`, homing spin time), `bb1.py` and `drive_test.py` notes and `docs/TOOLS.md` updated. Track width measured too: **127 mm** between the tyre centres (was 170 assumed): `TRACK_M`, so 1 turn of left-minus-right = 80 deg of heading. Still depends on 520 counts per turn (not measured)

## 2026-10-26 - heading hold `th` 20 → 60 (turn tests, live; `config.h` default)

- `drive_test.py turn` (on the spot, 0.5 turns/s), 3+ runs per setting, steps with Wi-Fi drops left out: th 20 turned 0.1-0.4 (left weakest), th 40 0.3-0.6, **th 60 0.46-0.56 both ways**; lean ±1.3, PWM about 60
- `bb1.py drive --turn 0.4 --for 1` now turns 0.32-0.48 both ways (was 0.28 right, 0.10 left), about the 0.4 asked. (First reported as half that: `bb1.py` halved `turned_turns`, fixed the same day)
- Wi-Fi drops hit the first moving step of each test (6 of 10) and almost never the second: a clue for the server stalls

## 2026-10-26 - milestone 3 starts: `tools/bb1.py` (branch `agent-api`)

- New `tools/bb1.py`: command-line control for scripts and AI agents. `state` (summary or `--full`), `arm`, `drive --speed --turn --for`, `stop`, `disarm`; one line of JSON per call; exit code 0 / 1 refused or safety stop / 2 no answer. Uses only the existing HTTP API: no firmware change
- Safety: drive refuses unless armed with no encoder fault, caps speed and turn at `--max` (0.5), at most 10 s, 1 s commands re-sent every 100 ms (BB1's failsafe stops it within 1 s if the tool dies), stops on a disarm, an encoder fault or 3 wheel turns of travel, Ctrl-C sends `/drive/stop`
- After a drive it waits (up to 2 s) for the wheels to stop, then answers `moved_turns`, `turned_turns` and `settled`. First run on BB1: all commands, the 0.5 cap and the stops worked; forward 2 s at 0.3 moved 0.27, back 0.51; 1 s turns at 0.4 turned 0.28 right, 0.10 left (th 20)
- Tested on the mock and on BB1; docs in `docs/TOOLS.md`, `docs/API.md` (agent list item 1)

## 2026-10-26 - drive tuning: smode 1 dropped, sp 3, sdi 4 (saved in flash and `config.h` defaults)

- `smode` flashed and confirmed on `/state`. smode 1 drove the wrong way at every ki from 100 to 25 (the body never leans into the drive on wood; the wheel speed loop's PWM goes straight to the wheels), and standing rocked under ki 35. We chose smode 0 + sdi; smode 1 stays in the firmware, default 0
- `DEFAULT_SPEED_P` 1.2 → **3.0**: after a reverse stop at 0.5 BB1 rolled on 0.8 turns and rocked (2 s period); sp 3 stops in under 0.25 (3 of 3 runs)
- `DEFAULT_DRIVE_SPEED_I` 2 → **4**: forward at 0.3 now reaches 0.27-0.32 (was 0.16-0.21); 6 overshoots
- 10 min unattended `drive_explore.py` at sp 3: no disarms, stayed in the box (was 41 s). Still open: a slow backward roll after some stops (angle I keeps −4 to −9 for a few seconds)
- Results: vault `Logs/2026-10-26 bb1 drive tuning.md` (20:31 onward)

## 2026-10-26 - wheel speed loop (`smode 1`) and log format 11, flashed

- **Why:** the unattended explore run (19:20, 41 s, no fall) rolled backward at 0.45 turns/s with no command, leaning forward, angle I stuck at −12. On wood the angle loop's I term holds the wheel speed; the speed loop only moves the setpoint, the angle loop tracks it (error about 0), so I never changes. `sdi` pushes harder on that same weak lever
- **New:** setting `smode` (0 = as before, default; 1 = wheel speed loop). In smode 1 the setpoint shift (sp, si, sdi) is off and `wsp` x (speed − target) + `wsi` x (position − target position) is added to the PWM, capped at ±100 (`WHEEL_SPEED_PWM_MAX`). While driving the target position runs at the asked speed, kept within 0.3 wheel turns (`DRIVE_POS_MAX_LAG`), so `wsi` is a speed I with no catch-up debt; it is not snapped on stop, so BB1 stops where it was asked to. Starting values `wsp` 20, `wsi` 10 (= kp 18 x sp 1.2 and x si 0.5). Meant to be used with a lower `ki` (we tune it by hand). Live on /tune, serial, `/state`, saved in flash (settings layout v7; older saves load with the defaults), `# gains_ver` line
- **Log format 11:** `drive_speed_now`, `drive_turn_now` (the ramped targets) and `speed_pwm` (what the wheel speed loop added), in the record's last 3 padding bytes (still 76 bytes, no extra RAM). Mock and `logger.py`/`handheld.py` `GAIN_KEYS` updated

## 2026-10-26 - drive speed I (`sdi`), flashed 19:05

- After the speed fix (flashed): standing as good or better (wobble 0.3 deg), but **forward drive stuck** (0.01 of 0.5 asked) and reverse overshot (−0.75 for −0.5). On wood the angle loop's I term is the real wheel-speed command and the speed loop only moves the setpoint; with P alone the lean it asks for (sp x error, 0.6 deg) never broke BB1 free forward. sp 2.4 didn't help
- New setting **`sdi`** (drive speed I, default 2): while driving, adds that many degrees of lean per second for every turn/s it is slower than asked (less when faster). Capped by `SPEED_SHIFT_MAX`, fades out over 0.5 s after a stop (`DRIVE_LEAN_FADE_S`), reset on every arm. Live on /tune (**Drive I (sdi)**), serial `sdi N`, `/state` gains `sdi`, saved in flash (settings layout v6; v5 saves load with the default), `# gains_ver` line `drive_speed_i`. Mock, `logger.py`/`handheld.py` `GAIN_KEYS` updated

## 2026-10-26 - fix: wheel speed read 3.3x low (flashed 18:40)

- **Cause:** `bb1.ino` did `constrain(speedL.update(r.enc_l), ...)`. Arduino's `constrain()` is a macro that repeats its first argument up to 3 times, so `update()` ran 3 times a tick and the 10-slot window covered 3 ticks: speed = 20 x counts over 3 ticks, 3.3x low. Since step 5 (2026-10-21). Found by a wheels-in-the-air motor test (PWM 80: encoders 1410 / 1474 counts/s, speed 423 / 442) and the disassembly (3 calls per wheel). Fix: call `update()` once into a variable, then constrain
- **Scale-dependent settings changed so behavior stays the same:** `DEFAULT_SPEED_P` 4 → 1.2 (4 x 0.3; same standing behavior, and drive targets now mean real turns/s); `ENC_FAULT_MIN_SPEED` 150 → 500 counts/s (same real speed); `DEFAULT_DRIVE_FF` 60 → 25 (real floor slope about 26 PWM per real turn/s)
- **Lift detector:** `LIFT_TURNS_PER_SEC` stays 4, which is now 4 real turns/s. Before, it needed about 13 real turns/s and could never trip (in the air: about 3.4 at PWM 100)
- **Log format 10.** Older logs' `speed_l`/`speed_r` are 3.3x low; `dataset.py` multiplies them by 10/3. `drive_test.py` already uses encoder counts
- **After flashing:** the flash still holds sp 8 / dff 90 (saved earlier today on the wrong scale). Set the defaults and save before standing BB1 up

## 2026-10-26 - drive tests, drive tuning, training set

- **Correction (later the same day): the logged wheel speed reads 3.3x too low.** In every log `speed_l`/`speed_r` = 20 x (counts over the last **3** ticks), not 10 as `WheelSpeed`/`SPEED_WINDOW` say (the compiled object has the 10-deep window; cause not found yet). Encoder counts are right. So BB1 really drives 2-3x faster than asked, and the speed findings below are wrong: sp 8 / dff 90 made the overspeed worse (put back to sp 4 / dff 60 live, not saved), and turning was fine (0.42-0.51 real for 0.5), so `DRIVE_TURN_FF` 45 now overshoots (0.80) and is back to 15 (`TURN_HOLD_MAX_PWM` back to 40). `drive_test.py`'s report now takes speed from the encoder counts. The speed loop, the lift detector and the encoder fault rule all use this speed

- **`tools/drive_test.py`**: named drive tests over Wi-Fi (stand, fwd-back-30/50, turn, arc), recorded like `logger.py` with one `# step` line per step, and a per-step report (asked vs got, stop distance, heading drift, lean, saturation, Wi-Fi drops). Refuses unless armed with `enc_fault` 0; 1 s commands re-sent every 100 ms; `/drive/stop` between steps and on any error; ends the test at 3 wheel turns from the start (after BB1 crept into a wall), on a disarm, an encoder fault or 2 s without an answer
- **Floor runs (wood, 15 logs, all kept, one fall after hitting the wall):** stand: wobble 0.5 deg, no drift. Drive was slow because the speed loop is P-only while driving: it runs short by (lean needed) / sp. Live changes: `dff` 60 → 90 (mixed alone), `sp` 4 → 8 (**clearly better**: 0.3 asked → 0.19 forward / 0.28 back, stops within 0.5 turn at 0.3, calmer standing), `sp` 12 and `si` 1.5 and `th` 40 not better, put back. At 0.5 turns/s stops still overshoot (0.6 forward, 0.8-0.95 back). Turning reaches about 30% of the asked rate, and it rolls back 1-2 turns per turn test. Run table: vault `Logs/2026-10-26 bb1 drive tuning.md`
- **Firmware (not flashed yet):** `DRIVE_TURN_FF` 15 → 45 (each wheel needs about 87 PWM per turn/s on wood), `TURN_HOLD_MAX_PWM` 40 → 60. Each axis only lets go of its own hold: turning on the spot keeps the position hold, and driving straight keeps the full heading hold (review: the lag clamp capped straight-line heading hold at 5 PWM). Review fixes: an encoder fault now ends the command already running; `driveCommand()` in `drive.h` is the one clamp for HTTP and serial, refusing `nan` (HTTP 400); `static_assert` that the drive limits fit the int8 log fields (`DRIVE_MAX_SPEED`/`TURN` now `constexpr`); arrow keys scroll the main page again unless BB1 is balancing
- **Tools:** `logger.py` `GAIN_KEYS` shared with `handheld.py`, so `dff` reaches handheld logs and its `--wifi` `dff N` works. Mock: a drive command never outlives its arm, and `/drive` parses like the firmware (`ms=500.5` ok, `nan` → 400)
- **`tools/dataset.py`**: every real format-9 log as one table (`logs/dataset/bb1_dataset.csv.gz`) with outcome labels (timer / stopped / fell / log_ended), context columns, IMU-error and encoder-fault rows dropped (also re-detected on old logs). What training still lacks: FIRMWARE_PLAN.md section 6
- **API docs:** `docs/API.md` starts with a curl command for every endpoint (all 21 routes in `web.h`), including disarm + `/gains/save`
- **Open:** the Pico's web server goes quiet for 1-2.5 s now and then (`web_age_ms` up to 2550; Wi-Fi power save is already off); and one stuck browser tab blocked every other client until it was closed

## 2026-10-25 - milestone 2: drive from the web page

- **Driving** (`firmware/bb1/drive.h`): `POST /drive` with `speed` and `turn` (wheel turns/s, ±1.0) moves the speed loop's and heading hold's targets; BB1 leans into it by itself. Targets ramp (1 turn/s per s; turn 2). Turn feedforward `DRIVE_TURN_FF` 15 PWM per turn/s. `POST /drive/stop`, serial `drive S T [ms]` / `drive stop`
- **Failsafe:** each command lasts 500 ms (or `ms=`, max 3000). Not re-sent = ramp to a stop and keep balancing (no disarm). Disarm, fall and arm clear it. While driving the position target rides with the wheels and the heading target stays within 0.25 wheel turns; on stopping both snap to where BB1 is
- **Page:** new **Drive** panel on http://bb1.local: joystick pad, arrow keys / WASD, Max speed slider, asked / target / wheels tiles, hover hints. Sends every 100 ms, one request at a time; hiding the tab or leaving the window stops driving. The mock answers `/drive` and `/drive/stop` too
- **API docs:** new `docs/API.md`, the full HTTP reference (every endpoint, `/state` fields, units, errors) plus what the AI-agent milestone would still need. `/state` has a new `drive` object
- **Logs:** format 9 adds `drive_speed`, `drive_turn` (the operator command) and flag 16 (drive stopping); `# drive_...` settings line in the header. Record 76 bytes, +8 KB RAM (heap left about 36 KB)
- First floor test (2026-10-25): drives in all directions, a little slow, small left-right jitter; **fell backward when reversing** at 100% (log `drive-test-20`: the angle I term held about +100 PWM going forward and had to unwind before the wheels could reverse). Fix: speed feedforward setting `dff` (default 60, live on /tune, saved in flash, settings v5; `/state` gains `dff`; `# gains_ver` line `drive_ff`)
- Floor test 2: no visible change, still fell at 45%, and **swayed 35-90 deg when left alone after driving**. Log: after letting go BB1 rolled 2-4 more wheel turns and turned 0.5-3 wheel turns of difference, catching up targets that were allowed 1 turn of lag. Fix: position target rides with the wheels while driving, heading lag 0.25 turns, both snap to where BB1 is when it stops

## 2026-10-25 - encoder fault detector

- Floor tests 2 and 3 (circles on startup, falls when reversing): the drive log shows the **left encoder reading exactly 0 whenever the wheels roll backward** (right: −300 to −800 counts/s), while yesterday's logs have it counting both ways. A hardware fault (loose wire), not the branch; heading hold then thinks the right wheel is turns behind and spins BB1
- `control.h`: going straight, one wheel under 20% of the other's speed while that one does over 150 counts/s, for 0.3 s = `encoderFault` (1 left, 2 right; `ENC_FAULT_*` in config.h). Heading hold off, `/drive` and serial `drive` refused, log flag 32, `/state` `enc_fault`, orange warning in the Balance panel, pad greyed. Cleared by the next arm

## 2026-10-25 - low battery disarms while armed

- **Safety:** a pack under `BATTERY_LOW_V` (6.6 V, 3.3 V per cell) only blocked auto-arm. Once balancing, the motors kept driving as the voltage sagged. `balanceTick()` now disarms with `REASON_BATTERY` (13): motors braked, STBY low. `balanceArm()` refuses with the same reason. Both use `batteryTooLow()` in `battery.h` (moved out of `auto_arm.h`)
- No sense wire (under `BATTERY_MIN_SENSE_V`): still no battery check
- The disarm is not a fall, so auto-arm stays off until ARM (the voltage recovers as soon as the motors stop). `/state` `reason` is "battery low (under 3.3 V per cell)". `pid_report.py` names reason 13 "battery" (and 12 "picked up")
- Docs: POWER_ON.md case 15. Not flashed yet

## 2026-10-25 - narrative docs moved; compile CI

- Session, tuning, build-record, and PID-lesson notes now live in the second brain (`Projects/bb1 - balance bot 1/`). These repo files are short stubs so old links keep working: `SESSION_LOG.md` → `BB1 - Dev Log.md`; `TUNING_LOG.md` → `Logs/` or `BB1 - Tuning Log.md`; `BUILD_RECORD.md` → `BB1 - Build Record.md` or `Logs/`; `PID_LESSON.md` → Learning Log; `PID_LEARNING_PROMPT.md` → learning asset; `FUTURE_INTEGRATIONS.md` → Ideas and Roadmap (pin table kept here)
- Deleted the orphan serial dump `tools/step1.txt` (README and TOOLS.md did not link to it)
- GitHub Actions (`.github/workflows/compile.yml`) compiles `firmware/bb1` for Raspberry Pi Pico W (`rp2040:rp2040:rpipicow`, Earle Philhower core 6.3.0) on push and pull request to main

## 2026-10-25 - power-on work finished

- Flashed from branch `power-on` with `tools/flash.py`: the low-battery cut-out plus everything from 2026-10-24. we checked it: power-on behavior working as intended

## 2026-10-24 - tools/flash.py

- `tools/flash.py`: compile, check BB1 is lying down (pitch over serial), upload, print the auto-arm state and gains after the reboot. Docs: FLASHING.md, TOOLS.md
- First run (Claude, testing the script; BB1 was on USB lying down at +89.6 deg) flashed the bias fix and the site clean-up: `Gyro calibrated`, `auto-arm: waiting`, gains v2 loaded

## 2026-10-24 - fix: no balance after switching on standing up

- **Bug:** switched on standing up (held by hand), the startup gyro calibration failed because the hand moves, and arming needed a successful calibration, so auto-arm waited forever (`waiting: keep still to calibrate`). Lying down it worked
- **Fix:** BB1 starts with `GYRO_BIAS_TYPICAL` (config.h: the average bias from every log, X within 0.15 deg/s), so it can arm without a calibration. Retries only while lying down and disarmed (a calibration stops the loop for 2 s). Arming no longer requires a calibration (`REASON_NOT_CALIBRATED` unused). `FLAG_NO_BIAS` now means "using the typical bias"
- `auto_state` 3 now means "no IMU" (LOG_FORMAT 8)

## 2026-10-24 - main page clean-up

- Main page (`page.h`) is now for using BB1, not tuning it: the Balance panel shows Balancing / Not balancing / Fallen and what auto-arm is waiting for, with **ARM** and **STOP** and a link to /tune. Gain boxes, the arm timer, the countdown bar and the step test are gone from it (all on /tune)
- ARM from the main page sends `heartbeat=0` (new `/arm` argument): no time limit, keeps balancing if the page closes. Hiding the main page no longer disarms. /tune keeps its timed runs with the heartbeat
- Developer panels (PID, Pitch, Log, Live values, Motor test, Sensors, Pico memory, Network) start folded (new localStorage key `bb1.folded2`, so old fold settings reset once)
- Docs: `handheld.py` marked as testing only (TOOLS.md, POWER_ON.md)

## 2026-10-24 - power-on behavior (auto-arm rework)

- Auto-arm moved out of `control.h` into **`auto_arm.h`**, a named-state machine (off / waiting / holding / balancing). Edge cases written up in **`docs/POWER_ON.md`**
- **Lift detector**: wheels over 4 turns/s for 0.5 s while armed = picked up, motors off (`REASON_LIFTED` = 12). Logs show under 2 turns/s on the floor
- **Repeated falls**: 3 falls within 30 s turn auto-arm off until ARM
- Auto-arm state on serial (`auto-arm: ...`), on both pages next to the mode, in `/state` (`auto_state`) and in the log (`auto_state`, LOG_FORMAT 7, LogRecord still 72 bytes). Mock updated
- Flashed 2026-10-24 (BB1 lying down: `auto-arm: waiting`, saved gains loaded)
- `tools/wiki_sync.sh`: copies `docs/` to the GitHub wiki (TOOLS.md)

## 2026-10-24 - auto-arm, repo clean-up

- MIT license; GitHub repo `hectorsvill/bb1-balance-bot-1` (private to start)

- **Auto-arm**: starts balancing by itself when stood upright and still for 1 s (after power-up and after a fall); STOP and other disarms turn it off until the next ARM. Setting `auto` (flash layout v4), /tune box, serial help. Firmware banner "step 8". Flashed 2026-10-24 (BB1 lying down: waits, not armed)
- GitHub-ready pass: README rewritten (status, how it balances, build, safety); `secrets.example.h` now has a placeholder AP password (`change-me-please`) and the docs no longer print one; absolute home paths replaced with `~`; `firmware/uf2/bb1.ino.uf2` rebuilt from the current firmware with the template password. Checked: no Wi-Fi password, email or keys in the files or git history (commits use the GitHub noreply address; `secrets.h` was never committed)

## 2026-10-24 - step 8: speed loop

- **Milestone: BB1 balances in place on wood** (round 27): P 18, I 100, D 0.5, sp 4, si 0.5, th 20, deadband 10, cap 200, trim 0. Wobble 0.2 deg, stays between the sticky notes, no turning. Saved to flash and made the `config.h` defaults

- Round 25: P 18 / I 100 / **D 0.5** / **sp 4**: wobble **0.2 deg**, no jitter, 3 of 3 to the timer, drift 2 to 4 turns. Saved to flash
- Heading hold `th` (`balanceSteer()`): equal wheel distance so it stops curving left; /tune box, flash layout v3, `/state`, log header `turn_hold`. Default off. Flashed 2026-10-24 (saved v2 settings loaded fine)

- **First real balance on wood: P 18, I 100, D 0.7** (round 23): 3 of 3 tries to the 10 s timer, wobble about 1 deg. The key: on the floor the motors act like speed knobs, so the angle loop needs a big I term (TUNING_LOG round 22). Saved to flash. Still drives forward and veers left: speed loop next

- `control.h`: outer speed loop moves the angle target: `target = trim - (sp x turns/s + si x turns since arming)`, limited to +-6 deg, wheel speed smoothed. New settings `sp` / `si` (serial, /gains, /tune boxes, flash layout v2, `/state`, log header `speed_p` / `speed_i`). Default 0 = off. Also a Deadband box on /tune. Firmware name "bb1 step 8". Flashed 2026-10-24; saved settings P 18, D 0.7, trim 0, cap 200, deadband 10, sp 1.5, si 0
- Rounds 15 to 19 on wood (TUNING_LOG): trim must be typed (hand-held "current pitch" came out +3.5), deadband 10 (15 chattered), D limit about 0.7 to 0.8 at 10 kHz, P 13 / 18 both ran away forward: the angle loop's limit

## 2026-10-24 - motor kit folder reviewed

- PWM frequency 20 kHz -> **10 kHz** (`PWM_FREQ`): wheels start at lower PWM, mostly forward (BUILD_RECORD). Flashed

- Battery calibrated against a meter (7.9 V vs 7.2 V): `BATTERY_DIVIDER` 11 -> 12.07. Pages, serial readout, handheld.py and pid_report show **volts per cell** (log column `batt_v` stays the whole pack; `# battery_cells` in the log header). Flashed 2026-10-24; reads 8.06 V (4.03 V/cell) after flashing

- Battery voltage: `battery.h` (core 0, GP26, D153C `ADC` pin = battery / 11), battery chip on `/` and `/tune` (green / orange < 7.0 V / red < 6.6 V), serial readout, `/state` fields, log column `batt_v` (LOG_FORMAT 6, LogRecord 72 bytes). Mock, handheld.py status line and pid_report (`batt` column) updated. Flashed 2026-10-24 with the clean-up and the 2 s heartbeat; RAM free 44.6 KB (heap used 13 KB). Reads 7.29 to 7.36 V running; the first reading after power-up came out wrong (5.2 V), fixed by discarding a few ADC samples at start (boot rows then read 7.7 V: to check against a meter)

- New docs/KIT_REFERENCE.md: the vendor folder has no balance code. The D153C schematic shows a built-in battery divider (VIN / 11 on the `ADC` header pin), so battery sensing is one wire to GP26, not a divider to build (FUTURE_INTEGRATIONS, WIRING updated; fixes the 100k/33k vs 100k/47k mismatch). Also: 65 C over-temperature cut-off on the 5 V (would reset the Pico), STBY has no pull resistor, TB6612 limits
- Knowledge graph of the repo with graphify (`graphify-out/`, added to .gitignore). It found 3 doc mismatches, all fixed: FIRMWARE_PLAN error sign (now pitch - setpoint, as built), FIRMWARE_PLAN heartbeat (now 2 s), battery divider values (now the D153C's own pin)

## 2026-10-23 (evening) - clean-up and plans

- Firmware: the balance settings' names and limits live in one place, `setBalanceSetting()` / `gainsLine()` in `settings.h`, used by serial, `/gains` and flash loading; limits as constants in `config.h`. The `/gains` answer is now the same line as serial `gains`. **Compiled, not flashed yet**
- Tools: one shared `post()` in `logger.py`; `logger.py` saves `# gains_vN` lines like handheld.py; `--surface` on both (`# surface,...` in the file)
- Wi-Fi heartbeat 1 s -> **2 s** (`HEARTBEAT_MS`): ~1 s Wi-Fi gaps cut good tries (round 10). Compiled with the clean-up, not flashed yet
- New docs/SESSION_LOG.md; TUNING_LOG "Plan"; FIRMWARE_PLAN section 6 "Data for training: status and rules"
- Rounds 12 to 14 on wood: balance point moved to about +0.55; deadband measured (10 to 15 in the air, about 30 on wood)

## 2026-10-23 - handheld.py picks the Pico's port

- New `tools/deadband_test.py`: sweeps motor-test PWM over Wi-Fi and finds where both wheels turn (round 14 showed the wheels don't move below about PWM 30 on wood)
- docs/TUNING_LOG.md: every tuning round, and what we do with the log data. Round 10 (no cable): never fell, 2 tries cut by Wi-Fi, wobble about 6 vs 3 with the cable
- pid_report.py names every disarm reason (Wi-Fi lost, tab hidden...) instead of "you"
- /tune: **Run a try** panel (seconds, ARM, STOP, countdown; space = STOP; hidden tab disarms), so BB1 can run with no USB cable
- handheld.py `--wifi`: no USB cable, arm / disarm / gains over Wi-Fi, heartbeat from its `/state` polling (0.2 s worst latency measured while logging)
- New `tools/pid_report.py`: one row per try (secs, end, wobble, lean, |P| |I| |D|, cap%, flips/s, turns) plus hints
- /tune: "Logs: record and read" section (needs a reflash to show)
- docs/PID_LESSON.md (PID for a balancing robot, FPV comparisons) and docs/PID_LEARNING_PROMPT.md (prompt for a separate learning chat)
- Run 2026-10-22 22:20: P 9 to 9.85 / D 0.74 / I 0 / cap 150 reached the timer 7 of 7 tries (wobble 0.4 to 4.2). Cap 45 and I 2 made most tries fall
- FLASHING.md: Arduino IDE 2 steps, the `No drive to deploy` fix (close the IDE Serial Monitor), two ports, settings survive a reflash. bb1 reflashed after `motor_test`; gains came back from flash
- Wiring diagram: `docs/wiring.svg` / `wiring.png`, drawn by `tools/make_wiring_svg.py`, shown at the top of WIRING.md
- Both motors dead: STBY wire (GP9) came unsoldered, re-soldered (BUILD_RECORD, Repair)
- `find_port()` in `serial_log.py` (used by handheld.py) picks the port with the Raspberry Pi USB vendor ID instead of the first `/dev/cu.usbmodem*`. An Anker dock had taken the first slot, so `a` went to the dock (`ERR: unknown cmd`)

## 2026-10-22 - our tuning, floor run 5

- Run 8, same gains: 3 of 8 tries stood 10 s; rolls backward then falls. Likely the battery; drift needs step 8 (BUILD_RECORD, floor attempts 8)
- P 10 / D 0.75 to 0.85: **all 6 tries stood the full 10 s**. Left: slow ±10 deg swing while drifting (needs the step 8 speed loop) (BUILD_RECORD, floor attempts 7)
- P 10 / D 0.5 / trim −0.01: jitter gone, about 9.4 s up, slow growing overshoot (D term about 1/4 of P; deadband likely about PWM 30) (BUILD_RECORD, floor attempts 6)
- P 10 / D 5 / trim −0.58 / cap 150: about 3 s, overshoot and jitter. D term about 3x P and pinning the PWM at ±cap (BUILD_RECORD, floor attempts 5)

## 2026-10-21 - PID tuning page (/tune)

- New small page at http://bb1.local/tune (`tune_page.h`): Kp, Ki, Kd, trim, cap with Apply / Save to flash; live pitch, error, P / I / D, PWM out, 5 s chart; tuning notes. Never arms (arm and log with `handheld.py`). Also in `mock_bb1.py`
- Changing gains from a page now prints the `gains` line on serial (`printGains()` moved to `settings.h`), so `handheld.py` shows it and writes `# gains_vN,...` lines into the log
- docs/PID_TUNING.md: "Two ways to change the gains" (page, or `config.h` + `defaults` because flash overrides `config.h`)
- Gains renamed in the code for P / I / D: `gainKp` -> `gainP` (and I, D), `DEFAULT_KP` -> `DEFAULT_P_GAIN`, `Pid.kp` -> `Pid.gainP`; comments compare each to Betaflight. Serial also takes `p N` / `i N` / `d N`. Wire names (`kp` in the gains line, JSON, logs) unchanged. Flash layout unchanged (gains loaded fine after the update)
- /tune and PID_TUNING.md: which way to move the trim (rolls backward = raise it, forward = lower it)
- /tune and PID_TUNING.md: steps split into Start / Change a gain / Run a try / Finish (click the Terminal before pressing a)
- /tune: "How to use this page" rewritten, plus an "If you fly FPV" table
- /tune gain boxes labelled P (Kp), I (Ki), D (Kd), with a hover / tap helper saying what each does
- Checked on the robot: /tune loads, live data, Apply works and the gains line appears on serial

## 2026-10-21 - first floor attempts

- Run 4: cap 150 applied, longest try 7.2 s; rolls up to 2.6 wheel turns, so floor only (not a desk). we tune the PID by hand from here

- Run 3: cap still 80 (the `:` command didn't reach BB1 twice). All 4 tries fell backward at the cap. handheld.py now takes `--kp --kd --cap --trim` at start and shows kp / kd / cap in the status line
- handheld.py: status line cut to the window width and redrawn only on change (a wrapped line was printing a new line every refresh)

- Run 2 (cap150 file, cap actually still 80): **first full 10 s standing**, then 6 short tries falling mostly backward at the cap. Average pitch suggests trim about −4.6. Next: cap 150 for real

- 3 tries at kp 15 / kd 0.5 / cap 80: about 2 s upright each, then fell backward once the PWM hit the cap (80). Details in BUILD_RECORD.md. Next: cap 150
- docs/HANDHELD_TOOL.md: how `tools/handheld.py` works inside

## 2026-10-21 - hand-held arm test: directions correct

- First full hand-held run (`tools/handheld.py`): armed 5 s with no Wi-Fi cut-out; leaning forward drove both wheels forward, leaning back drove them backward. Fall cut-out checked both ways (22:21 run). Numbers in BUILD_RECORD.md. Hand-held test passed: next is the floor

## 2026-10-21 - web server: close each connection (fix "lost the web page")

- Hand-held ARM kept disarming after 1 to 2 s with "lost the web page". The 21:04 log showed /state reaching BB1 only every 0.5 to 2.5 s although the page asks 20 times a second
- Cause: the WebServer library serves one connection at a time and, after answering, waits up to 5 s for the browser to close it. One connection left open froze every other request (measured: 4.8 s)
- Fix: web.h `route()` closes the connection after every answer. Re-measured: no more 5 s freeze. A long `/log` answer (Record) can still delay /state by a few hundred ms
- tools/handheld.py: the arm test in one window (a = arm 5 s, Enter = disarm, : = any serial command such as `kp 20`, q = save the log), records over Wi-Fi like logger.py. logger.py's file naming and saving moved into `log_path()` / `save_log()` so both share them
- Decision: for balance tests, arm from serial (`arm 5`, no page heartbeat) and record with `logger.py`, page closed. Steps in `firmware/README.md`. Trim re-measured −3.98 after the battery was refitted, saved in flash

## 2026-10-21 - bb1 step 7: save settings + step test

- settings.h: balance settings saved in flash at EEPROM offset 128 (Wi-Fi uses 0..127), loaded at startup; `save`, `defaults`, page Save to flash / Defaults, `POST /gains/save`, `/gains/defaults`. Disarmed only
- Step test: `step`, page button, `POST /step` (armed only): setpoint +2 deg for 1 s, logged in `setpoint`
- config.h defaults now trim −4.39, cap 80
- Page: Live values List / Tiles switch (List = the original look, default)
- Checked on the robot: kd 0.6 saved, survived a restart; save caused 0 missing ticks, 0 overruns. Current saved: kp 15, ki 0, kd 0.5, trim −4.39, cap 80, deadband 0

## 2026-10-21 - bb1 step 6: arm / disarm + angle loop (not armed on the robot yet)

- Balance trim measured: −4.39 deg (page, Use current pitch as trim); to become the default in step 7
- First hand-held arm (23:19): armed at +8.8 deg with trim 0.16, wheels drove **forward** at the cap (80), about 420 counts/s: direction correct. It disarmed after 1.24 s at +12.5 deg with no fall, IMU or overrun flag, so the cause (button, space bar or the page heartbeat) wasn't recorded; later ARM presses were refused (not upright)
- Added `reason` to every log row (log format 4, in the old padding byte) and a page-hidden disarm (browsers slow hidden tabs to about 1 update a second, which trips the heartbeat)
- Second hand-held run (23:31): 3 arms, each stopped after about 1.1 s with reason 4 (lost the page / heartbeat), never a fall. Cause not known yet: the heartbeat compare is now race-proof (signed), `/state` polls time out after 1 s instead of hanging, and every row logs `web_age_ms` (log format 5) to show whether Wi-Fi stalls while the motors run
- Found it: with the motors off and /state polled about 20 times a second, `web_age_ms` reached 1190 ms (0.19 s of 8 s over 1 s): the Wi-Fi chip's power-save naps. `WiFi.noLowPowerMode()` on the home network: max 600 ms, never over 1 s in 10 s, 0 missing ticks
- Arm timer: ARM for N s on the page (default 5), `arm N` on serial, `seconds=` on /arm; reason 11
- Page: Record and Live log combined into one Log panel (recording shows the records as they arrive); gain boxes turn orange when they differ from BB1 and Apply says Apply changes; Enter applies; hover / tap hints on every non-obvious control
- Page redesign: status bar always on top with mode, arm countdown and STOP; Balance panel with a big countdown and progress bar, gain fields with units; new view-only PID panel (tiles + 10 s chart of P, I, D, output, ±cap); Log as a table with a sticky header, tinted armed rows, short / all columns; tiles instead of tables; panels fold; hints on hover. `/state` gained `arm_left_ms`. Page is now about 47 KB of flash
- docs: Balance panel control by control (WEB_INTERFACE.md), what the hand-held test is for (PID_TUNING.md)

- pid.h (derivative on the gyro rate, clamped integral, no windup while saturated), control.h (disarmed / armed / fallen, arm only within 10 deg of trim and calibrated; disarm on > 45 deg, 5 IMU errors or overruns in a row, or 1 s without the page while armed from it)
- Live gains: serial `arm`, `disarm`, `kp/ki/kd N`, `trim N` / `trim here`, `cap N`, `deadband N`, `gains`; page Balance panel; `POST /arm`, `/disarm`, `/gains`. STOP / space bar / Enter also disarm. Defaults in config.h (Kp 15, Ki 0, Kd 0.5, cap 150), not saved yet
- Sign convention: error = pitch - trim, all gains positive (docs/PID_TUNING.md updated)
- Log format 3: setpoint, p_term, i_term, d_term (stored x10 as int16), gains_ver; mode 4 armed, 5 fallen; flag 8 saturated; `# gains_ver,...` meta line. Record 68 bytes, buffer 136 KB; heap 40 KB free on the robot
- Mock arms, refuses when not upright, and shows the PD numbers
- Uploaded. Self-check disarmed, logger 15 s with /state polled at 20 Hz: 2947 records, 0 lost, 0 missing ticks, spacing 4997 to 5003 us, work 516 avg / 743 max

## 2026-10-21 - bb1 step 5: motors + encoders

- motors.h: TB6612 driver (forward +, STBY low unless a test runs), encoder interrupts on core 1, wheel speed over 10 ticks
- Timed motor test from serial (`m L R ms`, `stop`, Enter), the page (Motor test panel, STOP / space bar) and `POST /motor`, `/motor/stop`; capped at PWM 150 and 3 s; mode 3 while running
- LogRecord 40 -> 56 bytes (enc_l, enc_r, speed_l, speed_r, pwm_l, pwm_r); log format 2; `# counts_per_rev...` meta line; buffer 112 KB
- check_dump.py: wheel summary and a speed / PWM plot; mock_bb1.py fakes the motors (17.5 counts/s per PWM)
- Compiles: 20% flash, 70% RAM static, heap 64 KB free on the robot
- Motor test passed on the robot (2026-10-21). Uploaded (the Pico came back after we replugged it). Self-check with no motors, logger.py 21 s over Wi-Fi while /state was polled at 20 Hz: 4254 records, 0 lost, 0 missing ticks, spacing 4997 to 5002 us, work 500 us avg / 716 max, 0 errors. That also covers step 4's logging path.

## 2026-10-21 - bb1 step 4: saving logs, live log view

- web.h: `/log?since=N` (CSV, up to 1000 records per request, sent in chunks, reports lost records), `/log/meta`, `/sys` (RAM and flash use)
- log_meta.h: one block of `# setting` lines (firmware, log_format, bias, alpha, scales, signs) shared by the serial dump, the page and logger.py
- page.h: Record / Stop / Download, Save last 10 s, Live log panel (every record, 10th or 40th; scrolls sideways), Pico memory panel; panels in one column, reorderable with arrows (saved per browser)
- Startup calibration retries every 3 s if the robot was moving; new flag bit 4 (no bias yet); `ever_cal` in /state
- tools/logger.py (record over Wi-Fi), tools/mock_bb1.py (offline mock serving the real page); check_dump.py reads all three file sources and keeps text settings
- docs: WEB_INTERFACE.md (saving logs, endpoints, offline mock), TOOLS.md, CALIBRATION_AND_LOGS.md, FLASHING.md (battery care, upload troubleshooting), new FUTURE_INTEGRATIONS.md (battery divider, OLED, Pi Zero W + camera, HC-SR04)
- Compiles: 20% flash (421 KB), 58% RAM static, 109 KB free. Tested against the mock: logger 782 records 0 missing; page Record 415 records 0 gaps; layout fits a 375 px phone with the log scrolling inside its panel.

## 2026-10-21 - bb1 step 3: Wi-Fi + web page

- wifi_setup.h: own network "BB1" (192.168.42.1) by default; home network saved to flash (EEPROM library) and joined at startup, with mDNS bb1.local; 15 s fallback to the own network; `wifi`, `wifi reset`
- web.h + page.h: page with live pitch / accel pitch chart (10 s), live values, Calibrate, Set alpha, Network panel; endpoints /state, /cal, /alpha, /wifi, /wifi/forget
- secrets.h (gitignored) + secrets.example.h; `quiet` serial command; docs/WEB_INTERFACE.md
- Compiles: 19% flash, 109 KB RAM free
- Tested: we joined the page on the own network, then saved our home network; page works at bb1.local. With /state polled at 20 Hz: 0 missing ticks, spacing 4997 to 5003 us (was exactly 5000: core 0's Wi-Fi code now shares the flash cache), work 494 us avg / 659 max, request latency 41 ms median. Step 3 passed.

## 2026-10-21 - bb1 step 2: two cores + log buffer

- Control loop moved to core 1 (setup1/loop1, own 8 KB stack); serial on core 0; requests via shared.h
- telemetry.h: 40-byte LogRecord per tick (tick, time, work, mode, flag_bits, raw IMU, pitch, rate, accel pitch), 10 s ring buffer (80 KB) guarded by a mutex; `dump` / `dump N` print it as CSV with settings lines
- tools/check_dump.py; log columns documented in docs/CALIBRATION_AND_LOGS.md
- Self-check on the robot: 1800 records, 0 missing ticks, spacing exactly 5000 us, work 451 us avg / 484 max, 0 errors
- Our test (tilting, pitch -15.9 to +88.3): 1800 records, 0 missing ticks, exactly 5000 us, 451 / 474 us, 0 errors. Step 2 passed.

## 2026-10-21 - bb1 step 1: pitch angle

- Tested on the robot: filter tracks the accel angle within ~0.3 deg, loop 446 us avg / 527 us max of 5000, 0 overruns. Forward lean read negative, so ACCEL_PITCH_SIGN and GYRO_PITCH_SIGN changed to +1 (the bring-up note "forward reads negative" was wrong). Upright reads about +6 deg.
- Rerun after the sign fix and `cal`: lying on its front +88.7 deg, rock steady for 20 s with rate 0.0 (bias x -0.50, y +2.07, z -2.20 deg/s); forward tilt positive with positive rate; upright about -2.5 deg; filter within 0.3 deg of the accel angle at rest. Step 1 passed.

- firmware/bb1: config.h, imu.h (gyro +/-500 deg/s, 44 Hz DLPF, 200 Hz, bias calibration), angle.h (complementary filter, alpha 0.98), 200 Hz fixed loop with loop-time stats; motors held off
- Plan updated with our choices: own network by default plus a saved home-network setting (falls back to own network); web page before PID; logs saved from the browser or tools/logger.py, same CSV
- tools/: Python venv (tools/venv, not committed), requirements.txt, serial_log.py

## 2026-10-21 - Firmware plan drafted

- docs/FIRMWARE_PLAN.md: one `bb1` firmware, control on core 1 at 200 Hz, Wi-Fi page on core 0, UDP logging for analysis and later model training; three choices still open
- docs/PID_TUNING.md: how the PID works, tuning steps for BB1, reading and videos
- Toolchain checked: arduino-cli 1.5.1, rp2040 core 6.3.0, pyserial 3.5; no extra Arduino libraries needed

## 2026-10-20 - Moved to Claude Code

- Project packaged as a repo (balance-bot) with CLAUDE.md holding the pins, signs, power rule and toolchain

## 2026-10-20 - Bring-up results

- Motors: A = left (a- forward), B = right (b+ forward); both encoders count both ways; f / r both "signs correct"
- Direction settings: LEFT_MOTOR_REVERSED = true, RIGHT_MOTOR_REVERSED = false, LEFT_ENC_REVERSED = false, RIGHT_ENC_REVERSED = true
- MPU-6050: live on I2C0, flat face up, gyro y bias about +2.5 deg/s, pitch = tilt X with forward reading negative
- MicroPython replaced by the Arduino sketches

## 2026-10-20 - Bring-up tests written

- motor_test.ino and imu_test.ino (Arduino C++, arduino-pico core 6.3.0)
- Power note corrected: without the diode, switch the D153C on before plugging in USB

## 2026-10-20 - Milestone 1 built

- Short frame v1.1 with the sideways M2 Pico carrier at the 66.6 pin hole, USB toward the back
- D153C on the other face, Dupont sockets on its headers, wires through the windows
- IMU in the base pocket; battery strapped to the printed bottom plate; no diode yet

## 2026-10-20 - Design changes

- Sideways Pico carrier switched from pegs to 4x M2 screw bosses
- v1.2 stack layout designed (everything on one face, 106 tall), not printed; kept as layout = "stack"
- v1.1: column sockets 5.1 to 5.4, bosses 11.5; IMU mount flipped 180; "hectorsvill" engraved; short layout; HC-SR04 holder on the bottom plate

## 2026-10-18 to 2026-10-19

- D153C carrier with 4x M2.5 screws; replacement bottom plate; cyberpunk armor concept parked
- Fit tests passed: board_clear 0.2, peg_clear 0.2, dovetail_clear 0.25, column spacing 88 x 53

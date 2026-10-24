# How tools/handheld.py works

`tools/handheld.py` runs an arm test (hand-held or on the floor) from one terminal window: keys arm and disarm BB1, and every 5 ms log record is saved by itself. This note explains what's inside, so you can change it. How to *use* it: `firmware/README.md` ("Hand-held test, the current way") and `docs/TOOLS.md`.

## Why it exists

- Arming from the web page ties the motors to Wi-Fi: no `/state` for 2 s (1 s until 2026-10-23) and BB1 disarms ("lost the web page"). Arming from **serial** has no such check, so Wi-Fi hiccups can't cut a test short.
- Serial alone is hard to read: BB1 prints a status line 10 times a second.
- Two terminals (serial monitor + `logger.py`) work but are fiddly while you're holding the robot.

So the script does both jobs in one window and folds the 10 Hz readout into a single status line.

## The big picture

```text
            your keys                                  BB1 (Pico W)
   ┌──────────────────────┐   USB serial    ┌──────────────────────────────┐
   │ main loop            │ ──"arm 5"────▶  │ core 0: serial commands      │
   │  (reads keys, draws  │ ◀─"ARMED" ───── │         10 Hz readout        │
   │   the status line)   │                 │                              │
   │                      │                 │ core 1: 200 Hz balance loop  │
   │ SerialLink thread    │                 │         fills LogRecords     │
   │  (reads serial)      │                 │         (10 s ring buffer)   │
   │                      │   Wi-Fi HTTP    │                              │
   │ WifiRecorder thread  │ ──/log?since=─▶ │ core 0: web server           │
   │  (collects the log)  │ ◀──CSV rows──── │                              │
   └──────────────────────┘                 └──────────────────────────────┘
              │ on q
              ▼
   logs/bb1_<date>_<time>_<note>.csv
```

Three things run at the same time inside the script:

| Part | What it does | Why it's separate |
|---|---|---|
| main loop (`main()`) | waits for a key (0.2 s at most), sends the matching command, redraws the status line | must react to keys at once |
| `SerialLink` (a thread) | reads every line BB1 prints. Readout lines (`pitch ...`) only update the status line; anything else (`ARMED`, `DISARMED: fell over`) is printed in full | serial lines arrive whenever BB1 sends them |
| `WifiRecorder` (a thread) | asks `/log?since=<next tick>` about 4 times a second and keeps every row, like `logger.py` | a slow Wi-Fi answer must never delay a key press |

A *thread* is a second line of work in the same program. Python lets them share variables (`link.readout`, `rec.rows`), which is how the main loop shows what the other two collected.

## Step by step

1. **Find BB1.** `socket.gethostbyname("bb1.local")` once, then always the IP (looking up `.local` names can be slow every time).
2. **Open both links.** Serial: the first `/dev/cu.usbmodem*` at 115200 (from `serial_log.py`). Wi-Fi: `/log/meta` for the `# setting` lines, `/state` for the current tick, so the recording starts "now".
3. **Send any `--kp/--kd/--cap/--trim` you gave, then ask for `gains`**, so you see the settings. The script reads the trim (to show "from trim") and kp, kd, cap (shown in the status line) from that answer, so the screen always shows what BB1 is really using.
4. **Keys without Enter.** `tty.setcbreak()` puts the terminal in *cbreak* mode: each key arrives immediately and isn't echoed. `select()` waits up to 0.2 s for a key, so the status line refreshes 5 times a second even when you press nothing.
5. **Keys:** `a` sends `arm 5` (BB1 disarms itself after 5 s), Enter or space sends `disarm`, `g` sends `gains`, `:` switches back to normal typing for one line and sends what you type (`kp 20`, `save`...).
6. **The status line** is redrawn in place: `\r` goes back to the start of the line and `ESC[K` clears it. It's cut to the window width and only redrawn when it changes: a line wider than the window wraps onto two rows, `\r` can only go back to the start of the last row, and every refresh would leave a new line behind (it looks like spam). Keep the window at least about 60 characters wide to see all of it. Full lines from BB1 are printed above it the same way (`say()`).
7. **On q or Ctrl-C** (the `finally:` block, which runs even after an error): give the terminal back its normal mode, send `disarm` (never leave it armed), wait 0.5 s for the last records, then save.

## Wi-Fi mode (`--wifi`)

No USB cable, so nothing tugs on BB1. `WifiLink` replaces `SerialLink` and has the same job:

- **Status line:** it asks `GET /state` ten times a second and shows pitch, trim distance and gains from the answer. It prints `ARMED` / `DISARMED: why` when the armed state changes.
- **Commands:** `a` sends `POST /arm seconds=N`, Enter sends `POST /disarm`, and `:` takes `kp/ki/kd/p/i/d/trim/cap N`, `trim here`, `save`, `defaults` (sent to `/gains`). Motor tests and other serial-only commands say so.
- **Safety:** armed from Wi-Fi, the firmware's heartbeat applies. Those `/state` requests are the heartbeat, so if Wi-Fi stalls for 2 s BB1 disarms ("lost the web page"). With no cable that's the right failsafe. The arm timer, the 45 deg cut-out and the rest still work.
- **Gains in the log:** taken from `/state` (`gains.ver` and values), same `# gains_vN` lines as over USB.

## The saved file

Same layout as the page's Download and `logger.py`: the `# setting` lines from `log_meta.h`, `# note`, `# recorded_at`, `# records,N,lost,N`, one `# gains_vN,kp 15 ki 0 kd 0.5 trim -3.98 cap 80 deadband 0` line per gains version seen during the run (from `g`, `:` commands or the `/tune` page), the CSV header, then one row per tick. Saving is done by `save_log()` in `logger.py`, shared by both tools, so the two can never drift apart. Check any file with `tools/check_dump.py <file> --plot`.

## Safety

The script only sends the same serial commands you could type. All the cut-outs live in the firmware (`control.h`) and keep working whatever the script does: the arm timer (`-s`, 1 to 10 s here), 45 deg (fell over), IMU errors, late loops. If the script crashes or the USB cable comes out, the arm timer still stops the motors.

## Changing it

- **A new key:** add an `elif key == "x":` in the main loop next to the others, and add it to the docstring at the top and the `Keys:` print.
- **More in the status line:** the readout line looks like `pitch  -3.9 | accel only ... | rate +1.0 deg/s | ...` (`printStatus()` in `bb1.ino`); split it on `|` in `SerialLink.run()` and keep what you want.
- **If the firmware's readout changes**, check that it still starts with `pitch `, or readout lines will print one per line again.

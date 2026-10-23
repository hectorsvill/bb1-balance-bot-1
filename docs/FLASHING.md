# Flashing the Pico W

How to get a sketch onto BB1, and how to see it running.

## 0. Power order (every time, until the 1N5819 diode is fitted)

1. Battery connected, **D153C switch on**.
2. **Then** plug USB into the Mac.
3. When done: **unplug USB first**, then switch the D153C off.

USB alone pushes 5 V backwards into the D153C's regulator. The diode will fix this later.

### Battery care (2S 1300 mAh LiPo, no voltage sensing yet)

Leaving BB1 powered on the battery while you work is fine: with the motors off (STBY low) it draws roughly 50 to 100 mA (Pico W with Wi-Fi, IMU, the D153C's regulator), so the pack lasts many hours. But nothing on the robot watches the voltage yet, so:

- Keep each cell above about 3.5 V (7.0 V for the pack) and never let it go below 3.0 V per cell (6.0 V): that damages a LiPo for good.
- Check it with a LiPo checker or multimeter on the balance plug every couple of hours of use.
- Unplug the battery when you walk away or overnight (USB out first).
- For storage longer than a few days, charge or discharge to about 3.8 V per cell.

The battery voltage divider (docs/FUTURE_INTEGRATIONS.md) will let the page show and log the voltage.

## 1. Compile

From the repo folder:

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/bb1 firmware/bb1
```

On a new machine, create `secrets.h` first: `cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h`.

This checks the code and writes `build/bb1/bb1.ino.uf2` (the file the Pico runs). `build/` is not committed. A copy of each step's file is kept in `firmware/uf2/`.

## 2. Upload: the normal way (Pico already running one of our sketches)

Find the port:

```bash
ls /dev/cu.usbmodem*
```

Upload (replace the port with what `ls` printed):

```bash
arduino-cli upload -p /dev/cu.usbmodem21401 --fqbn rp2040:rp2040:rpipicow firmware/bb1
```

What happens: arduino-cli asks the running sketch to reboot into the bootloader, copies the UF2, and the Pico restarts with the new code. The port name can change after a reboot; run `ls` again if the next command can't find it.

## 3. Upload: the BOOTSEL way (always works)

Use this the first time, or if the upload above fails or no port shows up.

1. Unplug USB.
2. Hold the white **BOOTSEL** button on the Pico.
3. Plug USB in (D153C already on), then let go. A drive called **RPI-RP2** appears.
4. Copy the file:

```bash
cp build/bb1/bb1.ino.uf2 /Volumes/RPI-RP2/
```

(or drag `firmware/uf2/bb1.ino.uf2` onto the drive in Finder). The drive disappears and the Pico restarts with the new code. macOS may say the disk wasn't ejected properly; that's normal.

## 4. Arduino IDE 2

**One-time setup**
1. **Arduino IDE > Settings**, "Additional boards manager URLs": paste `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`, click **OK**.
2. **Tools > Board > Boards Manager**, search `pico`, install **Raspberry Pi Pico/RP2040/RP2350** by Earle F. Philhower (6.3.0 tested).

**Each upload**
1. D153C on **first**, then USB.
2. **File > Open** `firmware/bb1/bb1.ino` (the other `.h` files open as tabs).
3. **Tools > Board > Raspberry Pi Pico/RP2040 > Raspberry Pi Pico W**.
4. **Tools > Port**: pick the one that says **Raspberry Pi Pico W** (`/dev/cu.usbmodem21401` here). Not the Anker dock (`/dev/cu.usbmodem13401`).
5. **Close the Serial Monitor tab** if it's open (it holds the port and the upload fails with `No drive to deploy`).
6. Click **Upload** (the → arrow). **Expect:** `Wrote … bytes to /Volumes/RPI-RP2/NEW.UF2` at the bottom.
7. **Tools > Serial Monitor**, 115200 baud, "Newline", type `gains`. **Expect:** `gains vN: kp …`.

Close the Serial Monitor again before running `tools/handheld.py`: only one program can hold the port.

**Saved settings survive a reflash.** Gains, trim, cap (and the home Wi-Fi) live in a separate flash area, so they come back after flashing bb1 again, even after flashing another sketch like `motor_test` in between (checked 2026-10-23). `defaults` resets them to `config.h`.

## 5. Watch it run

Any one of these:

```bash
tools/venv/bin/python tools/serial_log.py -t 20 -o step1.txt
```

(reads for 20 s, prints and saves; stops by itself)

```bash
arduino-cli monitor -p /dev/cu.usbmodem21401 -c baudrate=115200
```

(interactive, type commands; quit with Ctrl-C)

```bash
screen /dev/cu.usbmodem21401 115200
```

(quit with Ctrl-A, then K, then y)

Only one program can hold the port at a time. Close the monitor before uploading again.

## Troubleshooting

| Problem | Fix |
|---|---|
| Two ports show up | The other one is a USB dock (Anker shows as `usbmodem13401`). The Pico is the one listed as "Raspberry Pi" by `tools/venv/bin/python -m serial.tools.list_ports -v`. |
| `ls /dev/cu.usbmodem*` finds nothing | Use the BOOTSEL way. Try another cable (some are charge-only). |
| Upload says the port is busy | Close the serial monitor / screen first. |
| Nothing prints after flashing | The sketch waits up to 5 s for a monitor, then runs anyway; open the monitor and type `help`, or press Enter. |
| Output is garbage | Baud rate isn't 115200. |
| Upload says `No drive to deploy` and the port stays | Something holds the serial port, usually the Arduino IDE Serial Monitor (shows as `serial-monitor` in `lsof /dev/cu.usbmodem21401`). Close it and upload again. Happened 2026-10-23. |
| Upload says `No drive to deploy` and the port disappears | The Pico rebooted into its bootloader but macOS didn't mount RPI-RP2. Look for an "Allow accessory to connect?" pop-up and allow it; otherwise unplug and replug USB (it boots the old firmware) and retry, or use the BOOTSEL way. |
| `Operation not permitted` on `/Volumes/RPI-RP2` (uploading from Claude Code) | macOS hasn't given that app access to removable drives. The Pico is already in bootloader mode at this point, so drag the `.uf2` onto RPI-RP2 in Finder yourself. Or allow it in System Settings > Privacy & Security > Files and Folders > Removable Volumes. |

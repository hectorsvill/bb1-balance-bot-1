# Flashing the Pico W

How to get a sketch onto BB1, and how to see it running.

## 0. Power order (every time, until the 1N5819 diode is fitted)

1. Battery connected, **D153C switch on**.
2. **Then** plug USB into the Mac.
3. When done: **unplug USB first**, then switch the D153C off.

USB alone pushes 5 V backwards into the D153C's regulator. The diode will fix this later.

## 1. Compile

From the repo folder:

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/bb1 firmware/bb1
```

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

Board "Raspberry Pi Pico W", port `/dev/cu.usbmodem...`, open `firmware/bb1/bb1.ino`, press Upload. Serial Monitor at 115200, "Newline".

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
| `ls /dev/cu.usbmodem*` finds nothing | Use the BOOTSEL way. Try another cable (some are charge-only). |
| Upload says the port is busy | Close the serial monitor / screen first. |
| Nothing prints after flashing | The sketch waits up to 5 s for a monitor, then runs anyway; open the monitor and type `help`, or press Enter. |
| Output is garbage | Baud rate isn't 115200. |
| `Operation not permitted` on `/Volumes/RPI-RP2` (uploading from Claude Code) | macOS hasn't given that app access to removable drives. The Pico is already in bootloader mode at this point, so drag the `.uf2` onto RPI-RP2 in Finder yourself. Or allow it in System Settings > Privacy & Security > Files and Folders > Removable Volumes. |

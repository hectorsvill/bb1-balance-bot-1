# Future Integrations

Parts planned for BB1 but not fitted yet. Pins are already reserved in CLAUDE.md so nothing has to be rewired later. Each one adds columns to the log record (CALIBRATION_AND_LOGS.md) when it goes in.

| Part | Pins | Status |
|---|---|---|
| Battery voltage divider | GP26 (ADC0), pin 31 | skipped for now |
| SSD1306 OLED | I2C1: GP6 SDA / GP7 SCL, pins 9 / 10 | not fitted |
| Raspberry Pi Zero W + camera | UART0: GP0 TX / GP1 RX, pins 1 / 2 | next big milestone |
| HC-SR04 distance sensor | GP20 Trig / GP21 Echo, pins 26 / 27 | holder printed, not wired |
| 1N5819 diode (D153C 5V to VSYS) | power wire | needed before the power rule can go |

## Battery voltage divider (skipped for now)

**Why:** a 2S LiPo is 8.4 V full and is damaged below about 6.0 V (3.0 V per cell). Today nothing watches it, so you check it by hand (FLASHING.md, Battery care). With a divider, BB1 can show the voltage on the page and OLED, log it every tick, and refuse to arm (or warn) when it's low. Battery voltage also changes how hard the motors push for the same PWM, so a model trained on the logs needs it.

**Why a divider:** the Pico's ADC reads 0 to 3.3 V. Two resistors scale the pack down:

```text
LiPo + (8.4 V max) ──[ 100 k ]──┬──[ 47 k ]── GND
                                │
                                └── GP26 (ADC0)   + 100 nF from GP26 to GND (smooths noise)

V_adc = V_batt x 47 / (100 + 47)     8.4 V -> 2.69 V (safe under 3.3 V)
V_batt = adc_counts / 4095 x 3.3 x 147 / 47
```

- 147 k total draws about 57 uA, small enough to leave connected, but it does drain a stored pack over weeks: unplug the battery for storage anyway.
- Measure both resistors with a multimeter and put the real values in `config.h`; 1% resistors are worth it.
- Don't use the Pico's built-in VSYS/3 reading: on the Pico W GP29 is shared with the Wi-Fi chip, and our VSYS is the D153C's regulated 5 V, not the battery.
- Thresholds to start with: warn at 7.0 V, refuse to arm at 6.6 V (under load the voltage sags, so read it with the motors off).

## SSD1306 OLED (0.96", 128 x 64, I2C)

**What it would show:** IP address / `bb1.local` (no more serial `wifi` to find it), mode (DISARMED / ARMED / FALLEN), pitch, battery voltage, and a big warning when the battery is low.

- Wire to I2C1 (GP6 / GP7), not the IMU's I2C0, so a slow screen update can never delay an IMU read. 3.3 V and GND from the same rail as the IMU.
- Most modules are at address 0x3C and already have pull-up resistors.
- Library: Adafruit SSD1306 + Adafruit GFX (or U8g2), both work with the arduino-pico core.
- Drawing runs on **core 0** a few times a second. A full screen update takes about 25 ms on I2C at 400 kHz; fine on core 0, unacceptable on core 1.
- RAM: about 1 KB for the screen buffer. We have about 100 KB free (Pico memory panel on the page).

## Raspberry Pi Zero W + camera

**Why:** the Pico balances; the Pi Zero would be the "eyes and brain": camera, recording video next to the logs, and later running the model that drives. The Pico keeps the 200 Hz balance loop no matter what the Pi is doing.

**Link:** UART (GP0 / GP1 on the Pico, GPIO14 TX / GPIO15 RX on the Pi, cross TX to RX, common GND). Both are 3.3 V, so no level shifter. The Pi sends drive commands (the same throttle / turn the web page will send in milestone 2) and reads the log records; the Pico logs which source each command came from.

**Power:** the Pi Zero W plus a camera draws 200 to 400 mA at 5 V, peaks higher. Don't hang that on the D153C's small 5 V regulator. Use a separate 5 V buck converter (3 A, for example a Pololu or MP1584 module) from the battery, and add the 1N5819 diode first.

**Which Pi:** the original Zero W (single core, ARMv6) can stream 640 x 480 video but struggles with anything clever. A **Pi Zero 2 W** is the same size and pins with four cores (about 5x faster) and runs current OS and camera software; worth the swap if you buy one.

**Camera options** (all use the Zero's small 22-pin camera connector; buy the matching "Zero" ribbon cable, the standard one doesn't fit):

| Camera | Why pick it | Notes |
|---|---|---|
| **Raspberry Pi Camera Module 3 Wide** (12 MP, 120 deg, autofocus) | **Recommended.** Wide view sees the floor in front and both sides, good for driving and for training data | about $35; needs current Raspberry Pi OS (libcamera) |
| Camera Module 3 (standard, 75 deg) | sharper at a distance, autofocus | narrower view |
| Camera Module 2 (8 MP, 62 deg) | cheap, very well supported, light | fixed focus, narrow |
| Arducam OV5647 wide / NoIR (5 MP) | cheapest, wide-angle and night-vision versions | older sensor, lower quality |

Tips: mount it rigidly on the top frame so the image doesn't wobble relative to the IMU, record its angle in the build record, and capture at a low resolution (320 x 240 to 640 x 480) for training: models want many small frames, not a few big ones. Timestamp each frame with the Pico's tick (sent over UART) so video and logs line up.

## HC-SR04

Holder goes on the front end. Its Echo pin outputs 5 V: put a divider (for example 1 k / 2 k) between Echo and GP21, never wire it straight to the Pico. Measure on core 0 or with a PIO / interrupt so a 30 ms echo wait never touches the control loop.

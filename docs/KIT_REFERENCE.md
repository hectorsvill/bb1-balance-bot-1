# Motor kit reference (what came with the chassis and D153C)

The vendor folder is on our Mac at `~/Desktop/B570 balance trolley` (not in this repo: 319 MB, mostly videos and 3D models). Read 2026-10-24. This page keeps what matters for BB1.

## What's in the folder

| Item | Useful for BB1? |
|---|---|
| D153C schematic (`5. Sch/2. ... (D153C).pdf`) | **Yes**: battery voltage pin, power path, over-temperature cut-off (below) |
| TB6612 user manual (27 pages) | Yes: chip limits, D153C features |
| "2WD Balance Car code" | **No balance code**: only a motor on/off demo (Arduino UNO) and an STM32 motor demo |
| D153B STM32 demo (Hall encoder) | A little: a wheel speed PI loop (Kp 1, Ki 3, target 500 counts per period, 4x hardware encoder counting). Same shape as our step 8 speed loop, different units |
| Encoder demo (Arduino UNO) | Confirms our method: interrupt on both edges of channel A, direction from channel B (2x counting), speed = counts per fixed period |
| Chassis assembly sheet, videos, STEP models | Assembly only |

## Findings that change BB1

### 1. The D153C already measures the battery (no divider to build)

The D153C has a **10 k / 1 k divider from VIN to a pin labelled `ADC`** (R13 / R15 on the schematic): `ADC = VIN / 11`. It's on the **Output_IO header (H2), pin 5**, between E1A and GND, the same header our encoder, 5V and GND wires already use.

- 2S LiPo: 8.4 V full reads 0.76 V, 6.0 V empty reads 0.55 V. Safe for the Pico's ADC (0 to 3.3 V).
- **One wire:** D153C `ADC` (H2 pin 5) to Pico **GP26 / ADC0, pin 31** (already reserved).
- `V_batt = counts / 4095 x 3.3 x 11`. Resolution is about 9 mV per count at the battery: plenty.
- It measures VIN after the D153C power switch, so it reads 0 when the switch is off.
- This replaces the external 100 k / 47 k divider planned in FUTURE_INTEGRATIONS.md.
- Why it matters now: the same PWM pushes less as the battery drops, so falling gains between sessions may just be a lower battery. The logs need this column (FIRMWARE_PLAN.md, section 6).

### 2. Over-temperature cut-off at 65 °C kills the 5 V

The D153C's 5 V regulator (RT8289, rated 5 A) has its enable pin on an NTC thermistor divider: at about **65 °C the 5 V switches off**. Our Pico runs from that 5 V (VSYS), so a hot driver would **restart the Pico**. Hard back-and-forth PWM (like the D 5 run) heats the TB6612. Worth remembering if BB1 ever reboots mid-run.

### 3. STBY has no pull-down or pull-up on the board

The schematic shows STBY going straight from the header to the TB6612. If the wire comes loose (2026-10-23), the pin floats and the motors stop or behave randomly. Our firmware drives GP9 high only when it should; the solder joint is the weak point.

### 4. Chip limits (TB6612FNG)

- VM (motor supply) 4.5 to 13.5 V (our 2S: 6 to 8.4 V, fine). VCC (logic) 2.7 to 5.5 V (the D153C feeds 5 V).
- 1.2 A average, 3.2 A peak per channel. Built-in over-heat and under-voltage protection.
- PWM up to 100 kHz; the vendor examples use **10 kHz**. We use 20 kHz. Lower PWM frequency usually gives more torque at small duty cycles (less time lost switching), which could shrink the deadband. A possible experiment, not a change yet.

### 5. Not answered by the kit

- **Encoder counts per wheel turn:** no number given (the STM32 demo targets "500" in its own units). Our 520 is still assumed; measuring it (turn a wheel exactly once, `enc` on serial) is still open.
- **Gear ratio:** the chassis photos show motors marked "30", but ours are recorded as MG310P20 (20:1). Check the label on BB1's motors; the counts-per-turn test above settles it either way.
- No balance PID values to copy, so our tuning plan (TUNING_LOG.md) stands.

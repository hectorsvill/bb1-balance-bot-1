# Future integrations

Write-ups for parts not fitted yet (why, power, what to buy) live in the second brain: Ideas and Roadmap under `Projects/bb1 - balance bot 1/`.

This file keeps the pin and status table the firmware still uses. Wiring detail: [WIRING.md](WIRING.md).

| Part | Pins | Status |
|---|---|---|
| Battery voltage (D153C `ADC` pin, no divider needed) | GP26 (ADC0), pin 31 | wired and flashed 2026-10-24 (`battery.h`); calibrated to a meter (12.07). Cut-out flashed 2026-10-25: under 6.6 V with a reading present disarms and will not arm (`REASON_BATTERY`); no sense wire skips the check |
| VL53L0X time-of-flight distance sensor (Hiraethge 5-pack) | I2C1: GP6 SDA / GP7 SCL, pins 9 / 10; VIN to 3V3(OUT) pin 36; address 0x29. XSHUT / GPIO1 not wired (a second sensor needs XSHUT to get its own address) | chosen 2026-11-03 instead of the HC-SR04 (smaller); bring-up sketch `firmware/tof_test` |
| SSD1306 OLED | I2C1: GP6 SDA / GP7 SCL, pins 9 / 10 (shared with the VL53L0X; 0x3C) | not fitted |
| Raspberry Pi Zero W + camera | UART0: GP0 TX / GP1 RX, pins 1 / 2 | next big milestone |
| HC-SR04 distance sensor | GP20 Trig / GP21 Echo, pins 26 / 27 | holder printed, not wired; replaced by the VL53L0X (2026-11-03), pins stay reserved |
| 1N5819 diode (D153C 5V to VSYS) | power wire | fitted 2026-10-28, stripe toward the Pico: the power rule is gone |

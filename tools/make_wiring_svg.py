"""Draws docs/wiring.svg: every wire between the Pico W, the D153C and the GY-521.

The pin lists below are the same as the tables in docs/WIRING.md. If a wire
changes, change it in both places, then run:

    tools/venv/bin/python tools/make_wiring_svg.py
"""

from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "wiring.svg"

# (Pico pin, Pico name, D153C pin, wire colour in the drawing)
D153C = [
    (12, "GP9", "STBY", "#e5534b"),
    (13, "GND", "GND (Input_IO)", "#555555"),
    (14, "GP10", "PWMA", "#e69500"),
    (15, "GP11", "PWMB", "#e69500"),
    (16, "GP12", "AIN1", "#2f81f7"),
    (17, "GP13", "AIN2", "#2f81f7"),
    (19, "GP14", "BIN1", "#8957e5"),
    (20, "GP15", "BIN2", "#8957e5"),
    (21, "GP16", "E1A (left encoder)", "#2da44e"),
    (22, "GP17", "E1B (left encoder)", "#2da44e"),
    (24, "GP18", "E2A (right encoder)", "#1b7f83"),
    (25, "GP19", "E2B (right encoder)", "#1b7f83"),
    (31, "GP26", "ADC (battery, VIN / 11)", "#bf8700"),
    (38, "GND", "GND (power)", "#555555"),
    (39, "VSYS", "5V", "#d1242f"),
]
# Notes drawn next to some D153C pins
D153C_NOTES = {"STBY": "HIGH = motors on", "5V": "via 1N5819, stripe to Pico"}

GY521 = [
    (6, "GP4", "SDA", "#2f81f7"),
    (7, "GP5", "SCL", "#e69500"),
    (8, "GND", "GND", "#555555"),
    (36, "3V3(OUT)", "VCC", "#d1242f"),
]

NOTES = [
    "Power: 2S LiPo + / - to the D153C Vin+ / Vin- (blue terminal). The D153C switch turns the robot on.",
    "Motors: left = socket A, right = socket B (XH cables). The encoder wires come back through the D153C header.",
    "Diode: a 1N5819 in the 5V to VSYS wire (stripe toward the Pico) means USB and the battery go on in any order.",
]

W, DY = 1000, 30
PICO_X, PICO_W = 40, 230          # Pico box
DEV_X, DEV_W = 620, 340           # D153C and GY-521 boxes


def wires(rows, y0, dashed=False):
    """One line per wire, Pico pin name on the left, device pin on the right."""
    out = []
    for n, (pin, name, dev, colour) in enumerate(rows):
        y = y0 + n * DY
        width = 4 if dev == "STBY" else 2.5
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        out.append(f'<line x1="{PICO_X + PICO_W}" y1="{y}" x2="{DEV_X}" y2="{y}" stroke="{colour}" stroke-width="{width}"{dash}/>')
        out.append(f'<circle cx="{PICO_X + PICO_W}" cy="{y}" r="4" fill="{colour}"/>')
        out.append(f'<circle cx="{DEV_X}" cy="{y}" r="4" fill="{colour}"/>')
        out.append(f'<text x="{PICO_X + PICO_W - 10}" y="{y + 5}" text-anchor="end">{name} ({pin})</text>')
        style = ' font-weight="bold" fill="#e5534b"' if dev == "STBY" else ""
        out.append(f'<text x="{DEV_X + 10}" y="{y + 5}"{style}>{dev}</text>')
        if dev in D153C_NOTES and not dashed:
            out.append(f'<text x="{DEV_X + 175}" y="{y + 5}" fill="#666666" font-size="12">{D153C_NOTES[dev]}</text>')
    return out


def box(x, y, w, h, fill, stroke, title):
    return [f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="2"/>',
            f'<text x="{x + w / 2}" y="{y - 8}" text-anchor="middle" font-weight="bold">{title}</text>']


def main():
    top = 110                                  # first D153C wire
    d_h = len(D153C) * DY + 10
    imu_top = top + d_h + 50                   # first GY-521 wire
    i_h = len(GY521) * DY + 10
    notes_y = imu_top + i_h + 30
    height = notes_y + len(NOTES) * 22 + 20

    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{height}" viewBox="0 0 {W} {height}" '
         'font-family="Helvetica, Arial, sans-serif" font-size="14" fill="#1f2328">',
         f'<rect width="{W}" height="{height}" fill="#ffffff"/>',
         '<text x="20" y="34" font-size="20" font-weight="bold">BB1 wiring: Pico W, D153C, GY-521</text>',
         '<text x="20" y="58" fill="#555555">Pico physical pin in brackets. Same as docs/WIRING.md. '
         'STBY must be HIGH, or both motors stay off.</text>']
    s += box(PICO_X, top - 20, PICO_W, imu_top + i_h - top, "#eaf5ea", "#2da44e", "Raspberry Pi Pico W")
    s += box(DEV_X, top - 20, DEV_W, d_h, "#fdf1e3", "#e69500", "D153C (TB6612) motor driver")
    s += box(DEV_X, imu_top - 20, DEV_W, i_h, "#e8f0fe", "#2f81f7", "GY-521 (MPU-6050), address 0x68")
    s += wires(D153C, top)
    s += wires(GY521, imu_top, dashed=True)
    for k, note in enumerate(NOTES):
        s.append(f'<text x="20" y="{notes_y + k * 22}">{note}</text>')
    s.append("</svg>")
    OUT.write_text("\n".join(s) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()

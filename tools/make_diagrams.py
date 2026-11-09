#!/usr/bin/env python3
"""
Generate the wiring diagrams in docs/media as SVG.

SVG rather than exported bitmaps: it is text, so it diffs and versions
sensibly, it renders natively on GitHub, and it stays sharp. Colours are
chosen to read on both light and dark GitHub themes, so nothing relies on
the page background.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "media")

INK, DIM, HOT, WIRE = "#2b2b3a", "#7a7a8c", "#e8720c", "#4a4a5e"
RED, BLK, GRN, BLU = "#d33", "#333", "#2a8", "#36c"


def svg(w, h, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" '
            f'width="{w}" height="{h}" font-family="ui-sans-serif,system-ui,sans-serif">'
            f'<title>{title}</title>'
            f'<rect width="{w}" height="{h}" fill="none"/>{body}</svg>')


def box(x, y, w, h, label, sub="", fill="#ffffff", stroke=INK):
    s = (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" '
         f'stroke="{stroke}" stroke-width="1.5"/>'
         f'<text x="{x+w/2}" y="{y+(h/2 if not sub else h/2-6)}" text-anchor="middle" '
         f'font-size="13" font-weight="600" fill="{INK}" dominant-baseline="middle">{label}</text>')
    if sub:
        s += (f'<text x="{x+w/2}" y="{y+h/2+11}" text-anchor="middle" font-size="10.5" '
              f'fill="{DIM}" dominant-baseline="middle">{sub}</text>')
    return s


def wire(pts, color=WIRE, width=2, dash=""):
    d = " ".join(f"{'M' if i == 0 else 'L'}{x},{y}" for i, (x, y) in enumerate(pts))
    da = f' stroke-dasharray="{dash}"' if dash else ""
    return f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"{da}/>'


def label(x, y, t, size=11, color=DIM, anchor="start", weight="400"):
    return (f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" '
            f'text-anchor="{anchor}" font-weight="{weight}">{t}</text>')


def dot(x, y, color=WIRE, r=3.5):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>'


# ---------------------------------------------------------------- chain ----
def diagram_chain():
    b = [label(20, 24, "LED matrix chain — only DATA chains; CLK, CS and power are a shared bus",
               13, INK, weight="600")]
    b.append(box(20, 50, 96, 74, "ESP32-C3", "AirM2M CORE", "#fff7ef", HOT))
    mods = [("Mod 0", "left eye"), ("Mod 1", "right eye"),
            ("Mod 2", "mouth L"), ("Mod 3", "mouth R")]
    xs = [160, 290, 420, 550]
    for (nm, sub), x in zip(mods, xs):
        b.append(box(x, 50, 104, 74, nm, sub))

    # data path
    b.append(wire([(116, 68), (160, 68)], HOT, 2.5))
    b.append(label(138, 58, "DIN", 9.5, HOT, "middle", "600"))
    for x in xs[:-1]:
        b.append(wire([(x + 104, 68), (x + 130, 68)], HOT, 2.5))
        b.append(label(x + 117, 58, "DOUT→DIN", 8.5, HOT, "middle"))

    # shared bus
    rows = [(150, "CLK  gpio 2", BLU), (170, "CS   gpio 7", GRN),
            (190, "5 V", RED), (208, "GND", BLK)]
    for y, txt, col in rows:
        b.append(wire([(68, 124), (68, y), (654, y)], col, 2))
        for x in xs:
            b.append(wire([(x + 52, 124), (x + 52, y)], col, 1.2))
            b.append(dot(x + 52, y, col, 3))
        b.append(label(660, y + 4, txt, 10.5, col, "start", "600"))
    b.append(label(20, 238, "Device 0 is the module whose DIN comes from the board. "
                            "Order must be: left eye, right eye, mouth left, mouth right.",
                   10.5, DIM))
    return svg(820, 256, "".join(b), "LED matrix chain wiring")


# ---------------------------------------------------------------- audio ----
def diagram_audio():
    b = [label(20, 24, "Audio — the ESP32-C3 has no DAC, so PDM plus a passive filter",
               13, INK, weight="600")]
    b.append(box(20, 50, 96, 60, "gpio 4", "PDM out", "#fff7ef", HOT))
    y = 80
    b.append(wire([(116, y), (150, y)], HOT, 2.5))
    # two RC stages
    for i, x in enumerate((150, 265)):
        b.append(f'<rect x="{x}" y="{y-9}" width="58" height="18" fill="#fff" stroke="{INK}" stroke-width="1.5"/>')
        b.append(label(x + 29, y + 4, "1k", 11, INK, "middle", "600"))
        b.append(wire([(x + 58, y), (x + 115 if i == 0 else x + 95, y)], HOT, 2.5))
        cx = x + 86 if i == 0 else x + 76
        b.append(wire([(cx, y), (cx, y + 34)], HOT, 2))
        b.append(f'<path d="M{cx-13},{y+34} L{cx+13},{y+34} M{cx-13},{y+42} L{cx+13},{y+42}" stroke="{INK}" stroke-width="2"/>')
        b.append(wire([(cx, y + 42), (cx, y + 56)], BLK, 2))
        b.append(f'<path d="M{cx-12},{y+56} L{cx+12},{y+56} M{cx-7},{y+61} L{cx+7},{y+61} M{cx-3},{y+66} L{cx+3},{y+66}" stroke="{BLK}" stroke-width="1.8"/>')
        b.append(label(cx + 18, y + 42, "10nF", 10.5, DIM))
    # DC blocking cap
    b.append(f'<path d="M398,{y-11} L398,{y+11} M408,{y-11} L408,{y+11}" stroke="{INK}" stroke-width="2.4"/>')
    b.append(label(403, y - 18, "1µF", 10.5, DIM, "middle"))
    b.append(wire([(408, y), (452, y)], HOT, 2.5))
    b.append(box(452, 50, 110, 60, "Amp", "PAM8403", "#fff"))
    b.append(wire([(562, y), (600, y)], WIRE, 2.5))
    b.append(box(600, 50, 86, 60, "Speaker", "4–8 Ω"))
    b.append(label(20, 178, "Corner near 16 kHz: the PDM carrier is ~1 MHz and speech is under 8 kHz. "
                            "10nF, not 100nF — 100nF muffles it.", 10.5, DIM))
    b.append(label(20, 196, "gpio 5 is the PDM clock. The driver demands a pin; leave it unconnected.",
                   10.5, DIM))
    return svg(820, 214, "".join(b), "Audio output wiring")


# ---------------------------------------------------------------- pinout ---
def diagram_pinout():
    rows = [("gpio 2", "Matrix CLK", BLU), ("gpio 3", "Matrix DIN", HOT),
            ("gpio 7", "Matrix CS", GRN), ("gpio 4", "Audio PDM out", HOT),
            ("gpio 5", "PDM clock — leave unconnected", DIM),
            ("gpio 6", "PIR motion in", GRN),
            ("gpio 10", "Toggle → GND", BLU), ("gpio 0", "Button → GND", BLU),
            ("gpio 1", "free", DIM), ("gpio 20", "free", DIM)]
    b = [label(20, 24, "ESP32-C3 pin assignment", 13, INK, weight="600")]
    b.append(box(20, 44, 120, len(rows) * 26 + 10, "ESP32-C3", "", "#fff7ef", HOT))
    for i, (pin, use, col) in enumerate(rows):
        y = 70 + i * 26
        b.append(wire([(140, y), (184, y)], col, 2))
        b.append(dot(140, y, col, 3))
        b.append(label(190, y + 4, pin, 11.5, INK, "start", "600"))
        b.append(label(258, y + 4, use, 11, col))
    y2 = 70 + len(rows) * 26
    b.append(label(20, y2 + 18, "Do not use gpio 8, 9 (strapping), 11–17 (SPI flash) or 18, 19 (USB).",
                   10.5, DIM))
    b.append(label(20, y2 + 36, "Matrices and amp run from an external 5 V supply, not the board. "
                                "Tie all grounds.", 10.5, DIM))
    return svg(640, y2 + 52, "".join(b), "ESP32-C3 pin assignment")


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("wiring-chain", diagram_chain),
                     ("wiring-audio", diagram_audio),
                     ("pinout", diagram_pinout)):
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w") as f:
            f.write(fn())
        print(f"  {os.path.relpath(path, os.path.dirname(OUT))}  ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()

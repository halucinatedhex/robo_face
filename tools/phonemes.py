import math

# Preston Blair / xLights phoneme set. Shapes share one geometry routine so
# the 16 and 24 column versions cannot drift apart.
#
#   style  'open'  elliptical opening   'line' lips together
#          'smile' relaxed curve        'fv'   top teeth on lower lip
#   a      horizontal semi-axis as a fraction of half the canvas
#   b      vertical semi-axis in rows
#   teeth  't' top, 'b' bottom, 'tb' both
PHONEMES = [
    # Order matters: it is the channel order documented in the README and
    # matches how the xLights Faces dialog lists them.
    # name,  style,   a,    b,   teeth, tongue
    ("AI",   "open",  0.84, 3.4,  "t",  False),
    ("E",    "open",  0.88, 1.9,  "tb", False),
    ("etc",  "open",  0.60, 2.1,  "t",  False),
    ("FV",   "fv",    0.74, 0.0,  "",   False),
    ("L",    "open",  0.58, 2.7,  "t",  True),
    ("MBP",  "line",  0.80, 0.0,  "",   False),
    ("O",    "open",  0.46, 3.1,  "",   False),
    ("rest", "smile", 0.74, 0.0,  "",   False),
    ("U",    "open",  0.34, 2.7,  "",   False),
    ("WQ",   "open",  0.26, 2.0,  "",   False),
]

CY = 3.5


def render(width, style, a_frac, b, teeth, tongue):
    cx = (width - 1) / 2.0
    a = a_frac * (width / 2.0)
    g = [['.'] * width for _ in range(8)]

    def bar(row, half, ch='#'):
        for x in range(width):
            if abs(x - cx) <= half:
                g[row][x] = ch

    if style == "line":
        bar(3, a); bar(4, a)
        return [''.join(r) for r in g]

    if style == "smile":
        # Relaxed closed mouth: a flat centre with the corners lifted a row.
        for x in range(width):
            d = abs(x - cx)
            if d > a:
                continue
            g[3 if d > a * 0.7 else 4][x] = '#'
        return [''.join(r) for r in g]

    if style == "fv":
        # Upper teeth resting on the lower lip.
        bar(5, a)
        for x in range(width):
            if abs(x - cx) <= a - 1 and int(x - (cx - a)) % 2 == 1:
                g[4][x] = '#'
        bar(3, a * 0.95)
        return [''.join(r) for r in g]

    # --- elliptical opening ---
    rows = {}
    for y in range(8):
        dy = (y - CY) / b
        if abs(dy) > 1.0:
            continue
        half = a * math.sqrt(max(0.0, 1.0 - dy * dy))
        lo, hi = int(round(cx - half)), int(round(cx + half))
        if hi >= lo:
            rows[y] = (lo, hi)
    if not rows:
        bar(3, a); bar(4, a)
        return [''.join(r) for r in g]

    top, bot = min(rows), max(rows)
    for y, (lo, hi) in rows.items():
        if y == top or y == bot:
            for x in range(lo, hi + 1):      # cap rows: solid lip
                g[y][x] = '#'
        else:
            g[y][lo] = '#'                    # side walls only
            g[y][hi] = '#'

    def dashes(y):
        lo, hi = rows[y]
        mid = (lo + hi) // 2
        # Leave a clear column beside each side wall so the teeth read as
        # teeth rather than just thickening the outline.
        for x in range(lo + 2, hi - 1):
            if (x - mid) % 2 == 0:
                g[y][x] = '#'

    if 't' in teeth and top + 1 < bot:
        dashes(top + 1)
    if 'b' in teeth and bot - 1 > top:
        dashes(bot - 1)

    if tongue and bot - top >= 3:
        y = bot - 1
        lo, hi = rows[y]
        mid = (lo + hi) // 2
        w = max(0, (hi - lo) // 4)
        for x in range(mid - w, mid + w + 1):
            if lo + 1 < x < hi - 1:
                g[y][x] = '#'
    return [''.join(r) for r in g]


def build(width):
    out = []
    for name, style, a, b, teeth, tongue in PHONEMES:
        f = render(width, style, a, b, teeth, tongue)
        assert len(f) == 8, name
        for r in f:
            assert len(r) == width, f"{name}: {len(r)} wide, want {width}"
        out.append((name, f))
    return out


if __name__ == "__main__":
    for w in (16, 24):
        print(f"============ {w} columns ============")
        for name, f in build(w):
            print(f"--- {name} ---")
            for r in f:
                print("  " + r)

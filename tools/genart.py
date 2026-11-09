import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from phonemes import build as build_phonemes, PHONEMES
W = 24

def row(*parts):
    s = ''.join(parts)
    assert len(s) == W, f"row is {len(s)} wide, need {W}: {s!r}"
    return s

def d(n):   return '.' * n
def h(n):   return '#' * n
def blank(): return d(W)

def overlay(base, spans):
    """spans: list of (start, end_inclusive) set to '#'"""
    c = list(base)
    for a, b in spans:
        for i in range(a, b + 1):
            c[i] = '#'
    return row(''.join(c))

# Rounded-rect mouth outline, 24 wide
O0 = row(d(3), h(18), d(3))
O1 = row(d(2), '#', d(18), '#', d(2))
O2 = row(d(1), '#', d(20), '#', d(1))
O3 = row('#', d(22), '#')
OUTLINE = [O0, O1, O2, O3, O3, O2, O1, O0]

def outlined(**rows):
    f = list(OUTLINE)
    for k, spans in rows.items():
        i = int(k[1:])
        f[i] = overlay(OUTLINE[i], spans)
    return f

# 24-column artwork, widened from the original for a 3 module mouth.
F = {}

# 0: plain open mouth
F[0] = list(OUTLINE)

# 1: teeth top+bottom
teeth1 = row(d(2), '#', '.##' * 6, '#', d(2))
F[1] = [O0, teeth1, O2, O3, O3, O2, teeth1, O0]

# 2: teeth two rows deep
teeth2 = row(d(1), '#', '.', '##.' * 6, '.', '#', d(1))
F[2] = [O0, teeth1, teeth2, O3, O3, teeth2, teeth1, O0]

# 3: big O
F[3] = [row(d(8), h(8), d(8)),
        row(d(6), h(2), d(8), h(2), d(6)),
        row(d(5), h(2), d(10), h(2), d(5)),
        row(d(5), '#', d(12), '#', d(5)),
        row(d(5), '#', d(12), '#', d(5)),
        row(d(5), h(2), d(10), h(2), d(5)),
        row(d(6), h(2), d(8), h(2), d(6)),
        row(d(8), h(8), d(8))]

# 4: medium o
F[4] = [blank(),
        row(d(8), h(8), d(8)),
        row(d(7), '#', d(8), '#', d(7)),
        row(d(6), '#', d(10), '#', d(6)),
        row(d(6), '#', d(10), '#', d(6)),
        row(d(7), '#', d(8), '#', d(7)),
        row(d(8), h(8), d(8)),
        blank()]

# 5: tiny o
F[5] = [blank(), blank(),
        row(d(11), h(2), d(11)),
        row(d(10), '#', d(2), '#', d(10)),
        row(d(10), '#', d(2), '#', d(10)),
        row(d(11), h(2), d(11)),
        blank(), blank()]

# 6: wide toothy grin
F[6] = [blank(), blank(),
        row(h(3), d(18), h(3)),
        row(h(24)),
        row(h(2), '.', h(18), '.', h(2)),
        row('.', h(2), d(18), h(2), '.'),
        row(d(2), h(20), d(2)),
        row(d(3), h(18), d(3))]

# 7: smirk
F[7] = [blank(), blank(),
        row(d(20), '#', d(3)),
        row(d(18), h(2), d(4)),
        row(d(3), h(15), d(6)),
        blank(), blank(), blank()]

# 8: small frown
F[8] = [blank(), blank(), blank(), blank(),
        row(d(6), h(12), d(6)),
        row(d(4), '#', d(14), '#', d(4)),
        row(d(2), '#', d(18), '#', d(2)),
        blank()]

# 9: flat line
F[9] = [blank(), blank(), blank(), blank(),
        row(d(3), h(18), d(3)),
        blank(), blank(), blank()]

# 10: funnel
F[10] = [blank(), blank(),
         row('.', h(22), '.'),
         row(d(6), '#', d(10), '#', d(6)),
         row(d(8), '#', d(6), '#', d(8)),
         row(d(10), h(4), d(10)),
         blank(), blank()]

# 11-14: shrinking solid block
F[11] = [blank()] + [row(d(8), h(8), d(8))] * 6 + [blank()]
F[12] = [blank(), blank()] + [row(d(8), h(8), d(8))] * 4 + [blank(), blank()]
F[13] = [blank()] * 3 + [row(d(8), h(8), d(8))] * 2 + [blank()] * 3
F[14] = [blank()] * 3 + [row(d(11), h(2), d(11))] * 2 + [blank()] * 3

# 15: bowtie / gritted
F[15] = [blank(), blank(),
         row('.', '#', d(20), '#', '.'),
         row(d(2), '#', d(18), '#', d(2)),
         row(d(3), h(18), d(3)),
         row(d(2), '#', d(18), '#', d(2)),
         row('.', '#', d(20), '#', '.'),
         blank()]

# 16: pursed
F[16] = [blank(), blank(), blank(),
         row(d(6), h(12), d(6)),
         row(d(3), h(3), d(12), h(3), d(3)),
         row(d(6), h(12), d(6)),
         blank(), blank()]

# 17-22: scream, tongue/blob travelling left to right
F[17] = outlined(r5=[(6, 11)],  r6=[(9, 13)])
F[18] = outlined(r4=[(6, 11)],  r5=[(6, 12)],  r6=[(7, 14)])
F[19] = outlined(r3=[(9, 11)],  r4=[(7, 13)],  r5=[(7, 14)],  r6=[(7, 14)])
F[20] = outlined(r3=[(12, 14)], r4=[(10, 16)], r5=[(9, 16)],  r6=[(9, 16)])
F[21] = outlined(r4=[(12, 17)], r5=[(11, 17)], r6=[(9, 16)])
F[22] = outlined(r5=[(12, 17)], r6=[(11, 15)])

# 23: upper teeth
F[23] = [O0,
         row(d(2), h(2), '.#' * 9, d(2)),
         row(d(1), '#', '.', h(20), '.'),
         row(h(2), d(21), '#'),
         O3, O2, O1, O0]

# 24: open smile
F[24] = [row('#', d(22), '#'),
         row(h(3), d(18), h(3)),
         row('#', d(2), h(2), d(14), h(2), d(2), '#'),
         row('.', '#', d(4), h(12), d(4), '#', '.'),
         row('.', '#', d(20), '#', '.'),
         row(d(2), '#', d(18), '#', d(2)),
         row(d(3), h(3), d(12), h(3), d(3)),
         row(d(8), h(8), d(8))]

# 25: toothy smile
F[25] = [row('#', d(22), '#'),
         row(h(3), d(18), h(3)),
         row('#', d(2), h(2), d(14), h(2), d(2), '#'),
         row('.', '#', d(2), h(16), d(2), '#', '.'),
         row('.', '#', d(2), h(2), d(12), h(2), d(2), '#', '.'),
         row(d(2), '#', d(7), h(4), d(7), '#', d(2)),
         row(d(3), '##..' * 4, '##', d(3)),
         row(d(8), h(8), d(8))]

# 26: slot
F[26] = [blank(), blank(),
         row('.', h(22), '.'),
         row('#', d(22), '#'),
         row('#', d(22), '#'),
         row('.', h(22), '.'),
         blank(), blank()]

# 27-30: chomp
C_TOP    = row(d(2), h(20), d(2))
C_BROW   = row('.', '#', '.', h(20), '.')
C_SOLID2 = row('#', d(2), h(21))
C_TEETHU = row('#', d(3), '#..' * 6, '.', '#')
C_OPEN   = row('#', d(22), '#')
C_TEETHL = row('#', '..#' * 7, d(2))
C_JAW    = row('#', '.', h(20), '.', '#')
C_LIP    = row('.', h(22), '.')
C_BOT    = row(d(2), h(20), d(2))

F[27] = [C_TOP, C_BROW, C_TEETHU, C_OPEN,  C_TEETHL, C_JAW,    C_LIP, C_BOT]
F[28] = [C_TOP, C_BROW, C_TEETHU, C_OPEN,  C_OPEN,   C_TEETHL, C_LIP, C_BOT]
F[29] = [C_TOP, C_BROW, C_SOLID2, C_TEETHU, C_OPEN,  C_TEETHL, C_LIP, C_BOT]
F[30] = [C_TOP, C_BROW, C_SOLID2, C_TEETHU, C_TEETHL, C_JAW,   C_LIP, C_BOT]

# 31-36: scrolling zigzag. The pattern repeats every 6 columns, so six
# frames at consecutive offsets are needed for the loop to close seamlessly.
# Four would jump half a period and visibly stutter.
def zig_w(off, width):
    """One frame of the scrolling zigzag, at any width.

    Shared by both artwork widths so the wave cannot differ between a
    two-module and a three-module mouth.
    """
    top = ''.join('#' if (c - 1 - off) % 6 in (0, 1) else '.' for c in range(width))
    mid = ''.join('#' if (c - off) % 3 == 0 else '.' for c in range(width))
    bot = ''.join('#' if (c - 4 - off) % 6 in (0, 1) else '.' for c in range(width))
    b = '.' * width
    return [b, b, top, mid, mid, bot, b, b]


def zig(off):
    return [row(r) for r in zig_w(off, W)]

for i, off in enumerate((-2, -1, 0, 1, 2, 3)):
    F[31 + i] = zig(off)

# ---- the original 16-column artwork, for a 2 module mouth ----

# Original 16-column mouth art, extracted verbatim from the AVR sketch.
# Do not hand-edit; this is the reference artwork.
ART16 = [
    [  # 0
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#..............#',
        '#..............#',
        '.#............#.',
        '..#..........#..',
        '...##########...',
    ],
    [  # 1
        '...##########...',
        '..#.##.##.##.#..',
        '.#............#.',
        '#..............#',
        '#..............#',
        '.#............#.',
        '..#.##.##.##.#..',
        '...##########...',
    ],
    [  # 2
        '...##########...',
        '..#.##.##.##.#..',
        '.#..##.##.##..#.',
        '#..............#',
        '#..............#',
        '.#..##.##.##..#.',
        '..#.##.##.##.#..',
        '...##########...',
    ],
    [  # 3
        '......####......',
        '....##....##....',
        '...##......##...',
        '...#........#...',
        '...#........#...',
        '...##......##...',
        '....##....##....',
        '......####......',
    ],
    [  # 4
        '................',
        '......####......',
        '.....#....#.....',
        '....#......#....',
        '....#......#....',
        '.....#....#.....',
        '......####......',
        '................',
    ],
    [  # 5
        '................',
        '................',
        '.......##.......',
        '......#..#......',
        '......#..#......',
        '.......##.......',
        '................',
        '................',
    ],
    [  # 6
        '................',
        '................',
        '###..........###',
        '################',
        '##.##########.##',
        '.##..........##.',
        '..############..',
        '...##########...',
    ],
    [  # 7
        '................',
        '................',
        '.............#..',
        '...........##...',
        '..#########.....',
        '................',
        '................',
        '................',
    ],
    [  # 8
        '................',
        '................',
        '................',
        '................',
        '....########....',
        '...#........#...',
        '..#..........#..',
        '................',
    ],
    [  # 9
        '................',
        '................',
        '................',
        '................',
        '..############..',
        '................',
        '................',
        '................',
    ],
    [  # 10
        '................',
        '................',
        '.##############.',
        '....#......#....',
        '.....#....#.....',
        '......####......',
        '................',
        '................',
    ],
    [  # 11
        '................',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '................',
    ],
    [  # 12
        '................',
        '................',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '.....######.....',
        '................',
        '................',
    ],
    [  # 13
        '................',
        '................',
        '................',
        '.....######.....',
        '.....######.....',
        '................',
        '................',
        '................',
    ],
    [  # 14
        '................',
        '................',
        '................',
        '.......##.......',
        '.......##.......',
        '................',
        '................',
        '................',
    ],
    [  # 15
        '................',
        '................',
        '.#............#.',
        '..#..........#..',
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '................',
    ],
    [  # 16
        '................',
        '................',
        '................',
        '....########....',
        '..###......###..',
        '....########....',
        '................',
        '................',
    ],
    [  # 17
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#..............#',
        '#..............#',
        '.#..####......#.',
        '..#...###....#..',
        '...##########...',
    ],
    [  # 18
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#..............#',
        '#...####.......#',
        '.#..#####.....#.',
        '..#..#####...#..',
        '...##########...',
    ],
    [  # 19
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#.....##.......#',
        '#....####......#',
        '.#...#####....#.',
        '..#..#####...#..',
        '...##########...',
    ],
    [  # 20
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#.......##.....#',
        '#......####....#',
        '.#....#####...#.',
        '..#...#####..#..',
        '...##########...',
    ],
    [  # 21
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#..............#',
        '#.......####...#',
        '.#.....#####..#.',
        '..#...#####..#..',
        '...##########...',
    ],
    [  # 22
        '...##########...',
        '..#..........#..',
        '.#............#.',
        '#..............#',
        '#..............#',
        '.#......####..#.',
        '..#....###...#..',
        '...##########...',
    ],
    [  # 23
        '...##########...',
        '..##.#.#.#.#.#..',
        '.#.############.',
        '##.............#',
        '#..............#',
        '.#............#.',
        '..#..........#..',
        '...##########...',
    ],
    [  # 24
        '#..............#',
        '###..........###',
        '#..##......##..#',
        '.#...######...#.',
        '.#............#.',
        '..#..........#..',
        '...##......##...',
        '.....######.....',
    ],
    [  # 25
        '#..............#',
        '###..........###',
        '#..##......##..#',
        '.#..########..#.',
        '.#..##....##..#.',
        '..#....##....#..',
        '...##..##..##...',
        '.....######.....',
    ],
    [  # 26
        '................',
        '................',
        '.##############.',
        '#..............#',
        '#..............#',
        '.##############.',
        '................',
        '................',
    ],
    [  # 27
        '..############..',
        '.#.#####.######.',
        '#...#..#..#..#.#',
        '#..............#',
        '#..#..#..#..#..#',
        '#.############.#',
        '.##############.',
        '..############..',
    ],
    [  # 28
        '..############..',
        '.#.#####.######.',
        '#...#..#..#..#.#',
        '#..............#',
        '#..............#',
        '#..#..#..#..#..#',
        '.##############.',
        '..############..',
    ],
    [  # 29
        '..############..',
        '.#.############.',
        '#..#############',
        '#...#..#..#..#.#',
        '#..............#',
        '#..#..#..#..#..#',
        '.##############.',
        '..############..',
    ],
    [  # 30
        '..############..',
        '.#.############.',
        '#..#############',
        '#...#..#..#..#.#',
        '#..#..#..#..#..#',
        '#.############.#',
        '.##############.',
        '..############..',
    ],
    [  # 31
        '................',
        '................',
        '#....##....##...',
        '.#..#..#..#..#..',
        '.#..#..#..#..#..',
        '..##....##....##',
        '................',
        '................',
    ],
    [  # 32
        '................',
        '................',
        '##....##....##..',
        '..#..#..#..#..#.',
        '..#..#..#..#..#.',
        '...##....##....#',
        '................',
        '................',
    ],
    [  # 33
        '................',
        '................',
        '.##....##....##.',
        '#..#..#..#..#..#',
        '#..#..#..#..#..#',
        '....##....##....',
        '................',
        '................',
    ],
    [  # 34
        '................',
        '................',
        '..##....##....##',
        '.#..#..#..#..#..',
        '.#..#..#..#..#..',
        '#....##....##...',
        '................',
        '................',
    ],
]

# The original sketch only drew four of the six wave frames. Compute the
# missing two with the same formula, and check the formula reproduces the
# four that were drawn by hand before trusting it for the new ones.
WAVE_BASE = 31
for i, off in enumerate((-2, -1, 0, 1)):
    assert ART16[WAVE_BASE + i] == zig_w(off, 16), \
        f"wave formula disagrees with the original frame {WAVE_BASE + i}"
for off in (2, 3):
    ART16.append(zig_w(off, 16))

for i, f in enumerate(ART16):
    assert len(f) == 8, f'ART16 frame {i} has {len(f)} rows'
    for r in f:
        assert len(r) == 16, f'ART16 frame {i} row is {len(r)} wide'

# ---- emit ----

N = max(F) + 1
assert sorted(F) == list(range(N)), 'frame indices must be contiguous'
assert len(ART16) == N, f'widths disagree: {len(ART16)} vs {N} frames'


def emit_array(out, name, frames, modules):
    out.append(f'const uint8_t {name}[][8][{modules}] PROGMEM = {{')
    for i, f in enumerate(frames):
        out.append(f'    {{ // {i}')
        for r in f:
            assert len(r) == modules * 8
            cells = ', '.join(
                '0b' + r[c * 8:(c + 1) * 8].replace('.', '0').replace('#', '1')
                for c in range(modules))
            out.append(f'        {{{cells}}},  // {r}')
        out.append('    },')
    out.append('};')
    out.append('')


out = []
out.append('// Auto-generated by tools/genart.py -- do not edit by hand.')
out.append('//')
out.append('// Two sets of mouth artwork, because the mouth is either two or three')
out.append('// modules wide depending on how many are wired up:')
out.append('//')
out.append('//   MOUTH_FRAMES_16  the original AVR artwork, verbatim, 2 modules')
out.append('//   MOUTH_FRAMES_24  the same frames widened by hand, 3 modules')
out.append('//')
out.append('// Config.h selects one via MOUTH_MODULES.')
out.append('')
out.append('#include "MouthFrames.h"')
out.append('')
out.append(f'const uint8_t MOUTH_FRAME_COUNT = {N};')
out.append('')
emit_array(out, 'MOUTH_FRAMES_16', ART16, 2)
emit_array(out, 'MOUTH_FRAMES_24', [F[i] for i in range(N)], 3)

# --- phoneme shapes, for lip sync driven over E1.31 ---
ph16 = build_phonemes(16)
ph24 = build_phonemes(24)
assert [n for n, _ in ph16] == [n for n, _ in ph24]
out.append(f'const uint8_t PHONEME_COUNT = {len(ph16)};')
out.append('')
out.append('const char *const PHONEME_NAMES[] = {')
for n, _ in ph16:
    out.append(f'    "{n}",')
out.append('};')
out.append('')
emit_array(out, 'PHONEMES_16', [f for _, f in ph16], 2)
emit_array(out, 'PHONEMES_24', [f for _, f in ph24], 3)

# Resolve relative to this script so the project can be moved or renamed.
dest = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'MouthFrames.cpp')
dest = os.path.normpath(dest)
open(dest, 'w').write('\n'.join(out) + '\n')
print(f'wrote {dest}: {N} face frames + {len(ph16)} phonemes, both widths')

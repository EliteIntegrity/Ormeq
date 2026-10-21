"""
The application icon, drawn rather than found.

    python tools/make_icon.py

Writes src/ormeq.ico (the icon the executable carries) and shots/icon.png
(the same thing at 128px, so it can be looked at).

**Why a generator and not a file.** The icon we inherited was
`src/angband.ico`: thirty-two pixels, sixteen colours, somebody else's, and
named after somebody else's game. Replacing it with a hand-drawn binary would
have swapped one opaque blob for another. This way the mark is a block of
text in a file anybody can edit, the sizes are all made the same way, and the
result is reproducible.

**Why a Q.** The home screen's wordmark is ORMEQ with the last letter never
quite arriving -- "say it again" -- so the Q is the letter the game is already
about. Drawn at sixteen pixels and scaled by whole numbers, which is the same
thing the wordmark does with the typeface: the blockiness is the look, not a
limitation.
"""
import io
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The mark, at its native size. '#' is ink, anything else is ground.
MARK = [
    "................",
    "................",
    "....########....",
    "..###......###..",
    "..##........##..",
    ".##..........##.",
    ".##..........##.",
    ".##..........##.",
    ".##..........##.",
    ".##.....##...##.",
    ".##......##..##.",
    "..##......##.##.",
    "..###......####.",
    "....########.##.",
    "..............##",
    "................",
]

GROUND = (0x0F, 0x17, 0x10)     # a shade up from the canvas, so the square
                                # reads as a tile on a dark taskbar
EDGE = (0x24, 0x33, 0x27)       # one pixel of it, to give the tile an edge
INK = (0xDC, 0xC2, 0x69)        # the amber the wordmark is drawn in

SIZES = [16, 32, 48, 64, 128, 256]


def master():
    """The 16x16 mark as a grid of (r, g, b)."""
    n = len(MARK)
    assert n == 16, n
    for row in MARK:
        assert len(row) == 16, (len(row), row)

    px = []
    for y in range(n):
        line = []
        for x in range(n):
            if MARK[y][x] == '#':
                line.append(INK)
            elif x == 0 or y == 0 or x == n - 1 or y == n - 1:
                line.append(EDGE)
            else:
                line.append(GROUND)
        px.append(line)
    return px


def scaled(px, size):
    """Nearest neighbour, and always by a whole number: 16 goes into every
    size in SIZES, so no pixel is ever a different size from its neighbour."""
    n = len(px)
    assert size % n == 0, (size, n)
    k = size // n
    out = []
    for y in range(size):
        row = px[y // k]
        out.append([row[x // k] for x in range(size)])
    return out


def bmp(px):
    """One icon image as a DIB: header, BGRA bottom-up, then the AND mask."""
    h = len(px)
    w = len(px[0])

    hdr = struct.pack('<IiiHHIIiiII',
                      40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)

    body = bytearray()
    for y in range(h - 1, -1, -1):
        for r, g, b in px[y]:
            body += bytes((b, g, r, 255))

    # Fully opaque, but the mask still has to be there and still has to have
    # its rows padded to four bytes.
    stride = ((w + 31) // 32) * 4
    mask = bytes(stride * h)

    return bytes(hdr) + bytes(body) + mask


def png(px):
    """The 256 is a PNG inside the icon, which is what Windows expects of it
    and what keeps the file from being a third of a megabyte."""
    h = len(px)
    w = len(px[0])

    raw = bytearray()
    for row in px:
        raw += b'\x00'                       # filter: none
        for r, g, b in row:
            raw += bytes((r, g, b, 255))

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
            + chunk(b'IEND', b''))


def main():
    px = master()
    images = []

    for size in SIZES:
        s = scaled(px, size)
        images.append((size, png(s) if size >= 256 else bmp(s)))

    out = bytearray(struct.pack('<HHH', 0, 1, len(images)))
    offset = 6 + 16 * len(images)

    for size, data in images:
        out += struct.pack('<BBBBHHII',
                           size & 0xFF, size & 0xFF, 0, 0, 1, 32,
                           len(data), offset)
        offset += len(data)

    for _size, data in images:
        out += data

    ico = os.path.join(ROOT, 'src', 'ormeq.ico')
    with io.open(ico, 'wb') as fh:
        fh.write(bytes(out))

    shots = os.path.join(ROOT, 'shots')
    if not os.path.isdir(shots):
        os.makedirs(shots)
    with io.open(os.path.join(shots, 'icon.png'), 'wb') as fh:
        fh.write(png(scaled(px, 128)))

    print('src/ormeq.ico  %d bytes, %s' % (len(out),
          ' '.join('%d' % s for s, _ in images)))
    print('shots/icon.png  a look at it')
    return 0


if __name__ == '__main__':
    sys.exit(main())

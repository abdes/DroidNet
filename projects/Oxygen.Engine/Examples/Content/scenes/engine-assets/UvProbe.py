"""Writes the UV probe textures that show a mesh's texture layout and frames.

`UvProbe_albedo.png` holds a black "F" over four tinted quadrants and an 8 x 8
grid: the "F" reads upright and unmirrored only where a surface's UVs follow
`space::texture` (U to the image's right, V down) the right way round, and the
quadrants show which corner of the image lies where.

`UvProbe_normal.png` holds an 8 x 8 grid of domes, encoded green-down: a dome
is lit on the side facing the light only where the tangent frame follows +U
and +V. Run it from this directory:

    python UvProbe.py
"""

import math
import pathlib
import struct
import zlib
from collections.abc import Callable

SIZE = 512
CELLS = 8
CELL = SIZE // CELLS

# Quadrant tints, light enough to keep the normal map's shading visible.
TOP_LEFT = (235, 150, 150)
TOP_RIGHT = (150, 220, 150)
BOTTOM_LEFT = (150, 170, 235)
BOTTOM_RIGHT = (235, 225, 140)
GRID = (90, 90, 90)
GLYPH = (20, 20, 20)

# The "F", in image pixels (x right, y down): a stem and two arms to its right.
GLYPH_RECTS = (
    (176, 112, 224, 400),  # stem
    (224, 112, 352, 160),  # top arm
    (224, 232, 320, 280),  # middle arm
)

Pixel = tuple[int, int, int]


def albedo(x: int, y: int) -> Pixel:
    """The albedo texel at image pixel (x, y)."""
    for x0, y0, x1, y1 in GLYPH_RECTS:
        if x0 <= x < x1 and y0 <= y < y1:
            return GLYPH
    if x % CELL < 2 or y % CELL < 2:
        return GRID
    left = x < SIZE // 2
    top = y < SIZE // 2
    if top:
        return TOP_LEFT if left else TOP_RIGHT
    return BOTTOM_LEFT if left else BOTTOM_RIGHT


def normal(x: int, y: int) -> Pixel:
    """The green-down tangent-space normal texel at image pixel (x, y).

    Each cell holds a dome of height h = A * (1 - r^2)^2 over its inscribed
    circle, whose slope falls smoothly to zero at the rim. The normal is
    normalize(-dh/dx, -dh/dy, 1) with y growing down the image, so green
    encodes the image-down component.
    """
    amplitude = 0.8
    radius = CELL * 0.45
    dx = (x % CELL + 0.5 - CELL / 2) / radius
    dy = (y % CELL + 0.5 - CELL / 2) / radius
    r2 = dx * dx + dy * dy
    nx, ny = 0.0, 0.0
    if r2 < 1.0:
        falloff = 4.0 * amplitude * (1.0 - r2)
        nx, ny = dx * falloff, dy * falloff
    length = math.sqrt(nx * nx + ny * ny + 1.0)
    return (
        round((nx / length * 0.5 + 0.5) * 255),
        round((ny / length * 0.5 + 0.5) * 255),
        round((1.0 / length * 0.5 + 0.5) * 255),
    )


def png_chunk(kind: bytes, data: bytes) -> bytes:
    """One PNG chunk with its length and CRC."""
    crc = zlib.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)


def write_png(path: pathlib.Path, texel: Callable[[int, int], "Pixel"]) -> None:
    """Writes an 8-bit RGB PNG of SIZE x SIZE texels, rows top first."""
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)  # no filter
        for x in range(SIZE):
            rows.extend(texel(x, y))
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + png_chunk(b"IEND", b"")
    )


def main() -> None:
    here = pathlib.Path(__file__).parent
    write_png(here / "UvProbe_albedo.png", albedo)
    write_png(here / "UvProbe_normal.png", normal)


if __name__ == "__main__":
    main()

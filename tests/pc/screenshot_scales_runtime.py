#!/usr/bin/env python3
"""F12 and Shift+F12 at every internal scale (notes/pc-build.md, "Screenshots").

F12 saves the source picture, Shift+F12 the composed window. At an internal
scale of 2 or more the OpenGL pass draws the picture in GPU textures and the
backend's own picture buffer is never filled, so F12 saved whatever the heap
held there (black). This drives the built game in a window through its
control channel, presses F12 and "shot" (Shift+F12's window picture) from
MEMORIES_SDL_SCRIPT at one frame, and checks each saved picture against the
frame the control channel's own `shot` dumps for the same frame
(MEMORIES_DUMP_PICTURE: the picture at its internal scale):

- F12's picture is the dump's size, is not black, and its thumbnail (40x30
  box averages) differs from the dump's by a few levels at most;
- the window picture holds the same picture somewhere inside it (below the
  menu bar), by the same thumbnail measure.

Cases: the OpenGL pass at 1x, 2x and 4x, the software GPU
(MEMORIES_GL_PICTURE=0) at the same scales, and widescreen at 2x (F12's
size is the widened picture's). Needs the player's disc and a built native
game with a window (Windows or a desktop); uses folders of its own.

    python3 tests/pc/screenshot_scales_runtime.py [--executable PATH] [--out DIR] [--scales 1 2 4]
"""
import argparse
from pathlib import Path
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pc"))
from yfm_control import Game

OUT = ROOT / "tmp/pc/screenshot-scales"
AT = 800          # the frame F12 is pressed at (the title picture)
WINDOW_AT = AT + 4  # the frame the window picture is taken at
GRID = (40, 30)   # the thumbnails' cells
SAMPLES = 4       # points per cell and side
TOLERANCE = 6.0   # mean levels of 255 a thumbnail may differ by (the dump may be a frame off)
NONBLACK = 0.25   # least share of a picture's samples that are not black


def read_ppm(path):
    data = Path(path).read_bytes()
    fields, at = [], 2
    assert data[:2] == b"P6", f"{path}: not a PPM"
    while len(fields) < 3:
        while data[at:at + 1].isspace():
            at += 1
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        fields.append(int(data[start:at]))
    width, height, _ = fields
    body = data[at + 1:]
    return width, height, [body[y * width * 3:(y + 1) * width * 3] for y in range(height)]


def read_bmp(path):
    """(width, height, rows of RGB bytes, top row first) of an uncompressed
    or bit-fielded 24 or 32 bit BMP, as SDL_SaveBMP writes them."""
    data = Path(path).read_bytes()
    assert data[:2] == b"BM", f"{path}: not a BMP"
    offset, = struct.unpack_from("<I", data, 10)
    size, width, height, _, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    assert bits in (24, 32) and compression in (0, 3), f"{path}: {bits} bits, compression {compression}"
    masks = struct.unpack_from("<III", data, 14 + 40) if compression == 3 else (0xFF0000, 0xFF00, 0xFF)
    shifts = [(mask & -mask).bit_length() - 1 for mask in masks]
    top_down = height < 0
    height = abs(height)
    step = bits // 8
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(height):
        start = offset + (y if top_down else height - 1 - y) * stride
        row = data[start:start + width * step]
        if bits == 24:
            rows.append(b"".join(row[i + 2:i + 3] + row[i + 1:i + 2] + row[i:i + 1] for i in range(0, len(row), 3)))
        else:
            pixels = struct.unpack(f"<{width}I", row)
            rows.append(b"".join(bytes(((p >> shifts[0]) & 255, (p >> shifts[1]) & 255, (p >> shifts[2]) & 255))
                                 for p in pixels))
    return width, height, rows


def thumbnail(image, x, y, w, h):
    """GRID cells of the w x h part of the image at x,y: each the mean of
    SAMPLES x SAMPLES points (a box filter, so the picture at two scales
    gives the same thumbnail), as flat RGB levels."""
    _, _, rows = image
    cells = []
    for cy in range(GRID[1]):
        for cx in range(GRID[0]):
            total = [0, 0, 0]
            for sy in range(SAMPLES):
                row = rows[y + int(((cy + (sy + 0.5) / SAMPLES) / GRID[1]) * h)]
                for sx in range(SAMPLES):
                    at = (x + int(((cx + (sx + 0.5) / SAMPLES) / GRID[0]) * w)) * 3
                    for c in range(3):
                        total[c] += row[at + c]
            cells.extend(t / (SAMPLES * SAMPLES) for t in total)
    return cells


def difference(a, b):
    return sum(abs(p - q) for p, q in zip(a, b)) / len(a)


def not_black(cells):
    """The share of cells with any light in them."""
    return sum(1 for i in range(0, len(cells), 3) if max(cells[i:i + 3]) > 24) / (len(cells) // 3)


def run_case(executable, label, scale, software=False, wide=False):
    """One game, one scale; AssertionError says what is wrong with which picture."""
    folder = OUT / label
    shutil.rmtree(folder, ignore_errors=True)
    shots = folder / "shots"
    shots.mkdir(parents=True)
    env = {"MEMORIES_SDL_SCRIPT": f"{AT}:key:f12,{WINDOW_AT}:shot", "MEMORIES_SCREENSHOT_DIR": str(shots),
           "MEMORIES_DUMP_PICTURE": "1", "MEMORIES_NO_AUDIO": "1"}
    if software:
        env["MEMORIES_GL_PICTURE"] = "0"
    settings = {"internal_scale": scale, "scale": 2, "aspect": 2 if wide else 0}
    dumps = {}
    with Game(executable=executable, out=folder / "run", headless=False, settings=settings, env=env) as game:
        for frame in (AT, AT + 1, WINDOW_AT, WINDOW_AT + 1):
            while game.frame < frame:  # frames are not VBlanks: one a time
                game.step(1)
            assert game.frame == frame, f"{label}: stopped at frame {game.frame}, not {frame}"
            dumps[frame] = game.shot(f"dump-{frame}.ppm")
        game.step(4)  # the window picture is written as its frame is shown
    by_frame = {int(path.stem.rsplit("-", 1)[1]): path for path in shots.glob("*.bmp")}
    # The window picture is taken at WINDOW_AT or a present or two after it.
    assert len(by_frame) == 2 and AT in by_frame, f"{label}: expected F12's and the window's picture, got {sorted(by_frame)}"
    window_path = by_frame[max(by_frame)]

    # F12: the source picture.
    picture = read_bmp(by_frame[AT])
    pictures = {frame: read_ppm(dumps[frame]) for frame in (AT, AT + 1)}
    width, height, _ = pictures[AT]
    assert (picture[0], picture[1]) == (width, height), \
        f"{label}: F12 saved {picture[0]}x{picture[1]}, the frame is {width}x{height}"
    cells = thumbnail(picture, 0, 0, width, height)
    assert not_black(cells) >= NONBLACK, f"{label}: F12's picture is black ({not_black(cells):.2f} lit)"
    best = min(difference(cells, thumbnail(pictures[frame], 0, 0, width, height)) for frame in pictures)
    assert best <= TOLERANCE, f"{label}: F12's picture differs from the frame by {best:.1f} levels"

    # Shift+F12: the window, with the picture in it below the menu bar.
    window = read_bmp(window_path)
    reference = [read_ppm(dumps[frame]) for frame in (WINDOW_AT, WINDOW_AT + 1)] + list(pictures.values())
    ww, wh, _ = window
    pw = ww
    ph = ww * reference[0][1] // reference[0][0]  # the picture's own proportions
    assert wh >= ph, f"{label}: the window picture is {ww}x{wh}"
    window_best = None
    for top in range(0, wh - ph + 1, 2):
        cells = thumbnail(window, 0, top, pw, ph)
        for dump in reference:
            miss = difference(cells, thumbnail(dump, 0, 0, dump[0], dump[1]))
            window_best = miss if window_best is None else min(window_best, miss)
    assert not_black(thumbnail(window, 0, wh - ph, pw, ph)) >= NONBLACK, f"{label}: the window picture is black"
    assert window_best <= TOLERANCE * 1.5, f"{label}: the window picture holds no picture like the frame ({window_best:.1f})"
    print(f"{label}: F12 {width}x{height} within {best:.1f} levels, window {ww}x{wh} within {window_best:.1f}")


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--executable", type=Path, help="the game to test (default: the build in tmp/pc/game32)")
    parser.add_argument("--out", type=Path, help=f"where the runs are kept (default: {OUT})")
    parser.add_argument("--scales", type=int, nargs="+", default=[1, 2, 4], help="internal scales (default 1 2 4)")
    arguments = parser.parse_args()
    if arguments.out:
        OUT = arguments.out.resolve()
    executable = arguments.executable.resolve() if arguments.executable else None
    for scale in arguments.scales:
        run_case(executable, f"gl-{scale}x", scale)
        run_case(executable, f"software-{scale}x", scale, software=True)
    if 2 in arguments.scales:
        run_case(executable, "gl-wide-2x", 2, wide=True)
    print(f"screenshot scales: ok (kept in {OUT})")


if __name__ == "__main__":
    main()

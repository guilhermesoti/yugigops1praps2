"""Recompose the USA title in 15-bit PS1 color, from the user's SU.MRG.
Sheet and sprite locations from the upstream FM Editor's ui_assets.py.
"""
import struct
from array import array
import sys

LAYER_PARTS = (
    (241, ((0, 0, 128, 128, 0, 0), (128, 0, 128, 128, 128, 0),
           (128, 128, 128, 40, 0, 168), (256, 56, 56, 80, 128, 176),
           (256, 136, 64, 32, 192, 224), (256, 24, 64, 32, 192, 192),
           (0, 128, 128, 40, 0, 128))),
    (242, ((27, 200, 128, 16, 0, 208), (155, 200, 128, 16, 0, 224),
           (283, 200, 16, 16, 0, 240))),
    (243, ((46, 177, 56, 16, 128, 128), (102, 177, 72, 16, 184, 128),
           (174, 177, 104, 16, 128, 144))),
)

def title_pixels(data):
    vram = array('H', [0]) * (1024 * 512)
    for first, count, x, y in ((0, 64, 512, 256), (64, 32, 896, 0)):
        for i in range(count):
            column, tile = divmod(i, 16)
            for row in range(16):
                at = (y + tile * 16 + row) * 1024 + x + column * 64
                offset = (first + i) * 2048 + row * 128
                vram[at:at + 64] = array('H', struct.unpack_from('<64H', data, offset))
    for row in range(8):
        at = (240 + row) * 1024
        vram[at:at + 256] = array('H', struct.unpack_from('<256H', data, 0x30000 + row * 512))
    pixels = array('H', [0]) * (320 * 240)
    for y in range(240):
        for x in range(320):
            u = x % 256
            word = vram[y * 1024 + 960 + u // 4]
            pixels[y * 320 + x] = vram[244 * 1024 + ((word >> (4 * (u % 4))) & 15)]
    for pal, parts in LAYER_PARTS:
        for dx, dy, w, h, u, v in parts:
            for y in range(h):
                for x in range(w):
                    ux = u + x
                    word = vram[(256 + v + y) * 1024 + 640 + ux // 2]
                    color = vram[pal * 1024 + ((word >> (8 * (ux % 2))) & 255)]
                    if color:
                        pixels[(dy + y) * 320 + dx + x] = color
    if sys.byteorder == 'big':
        pixels.byteswap()
    return pixels.tobytes()

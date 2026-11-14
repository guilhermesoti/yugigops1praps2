#!/usr/bin/env python3
"""Capas do port para o OPL (pasta ART do pendrive), arte original da
Retrogames: sol alado dourado, moldura e o título em texto. Nada da arte da
caixa original (a capa da Konami tem dono).

    python3 gerar_capas.py [pasta_de_saida] [screenshot1.png screenshot2.png]

Gera, com o ID da ISO (RGPM_001.01):
    _COV  140x200  frente       _COV2 242x344  verso
    _ICO   64x64   ícone        _LGO  300x125  logo
    _BG   640x480  fundo        _SCR/_SCR2 250x168 telas (se der screenshots)
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

GAME_ID = "RGPM_001.01"
SERIF_B = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
SANS_B = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
GOLD, GOLD_DARK, GOLD_LIGHT = (242, 193, 78), (150, 104, 30), (255, 232, 160)
NAVY_TOP, NAVY_BOTTOM = (14, 16, 48), (52, 22, 70)


def font(path, size):
    return ImageFont.truetype(path, size)


def gradient(w, h, top, bottom):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return im


def glyph_pattern(im, seed=7, alpha=26):
    """Faixas de 'hieróglifos' abstratos (formas geométricas simples)."""
    w, h = im.size
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(seed)
    cell = max(12, w // 14)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            c = (*GOLD, alpha)
            k = rnd.randrange(6)
            m = cell // 5
            box = [x + m, y + m, x + cell - m, y + cell - m]
            if k == 0: d.ellipse(box, outline=c, width=max(1, cell // 16))
            elif k == 1: d.line([box[0], box[3], (box[0] + box[2]) // 2, box[1], box[2], box[3]], fill=c, width=max(1, cell // 16))
            elif k == 2: d.rectangle(box, outline=c, width=max(1, cell // 16))
            elif k == 3: d.line([box[0], (box[1] + box[3]) // 2, box[2], (box[1] + box[3]) // 2], fill=c, width=max(1, cell // 12))
            elif k == 4:
                d.line([(box[0] + box[2]) // 2, box[1], (box[0] + box[2]) // 2, box[3]], fill=c, width=max(1, cell // 12))
                d.ellipse([box[0] + m, box[1], box[2] - m, box[1] + 2 * m], outline=c, width=max(1, cell // 16))
    im.paste(layer, (0, 0), layer)


def winged_sun(size):
    """Sol alado (motivo egípcio clássico), dourado, em RGBA."""
    w, h = size * 2, size
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy, r = w // 2, h // 2, size // 4
    # asas: penas em leque
    for side in (-1, 1):
        for i in range(7):
            ang = math.radians(8 + i * 6)
            length = size * (0.95 - i * 0.08)
            y0 = cy - r * 0.35 + i * r * 0.18
            x0 = cx + side * r * 0.9
            x1 = cx + side * (r + length * math.cos(ang))
            y1 = y0 - length * math.sin(ang) * 0.35 + i * 2
            col = GOLD if i % 2 == 0 else GOLD_DARK
            d.polygon([(x0, y0 - r * 0.12), (x1, y1), (x1 - side * 6, y1 + r * 0.22), (x0, y0 + r * 0.16)], fill=col)
    # disco
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GOLD_DARK)
    d.ellipse([cx - r * 0.82, cy - r * 0.82, cx + r * 0.82, cy + r * 0.82], fill=GOLD)
    d.ellipse([cx - r * 0.45, cy - r * 0.6, cx + r * 0.1, cy - r * 0.1], fill=GOLD_LIGHT)
    return im


def glow(im, radius, color=GOLD, strength=150):
    a = im.split()[-1].filter(ImageFilter.GaussianBlur(radius))
    g = Image.new("RGBA", im.size, (*color, 0))
    g.putalpha(a.point(lambda v: min(255, v * strength // 100)))
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.alpha_composite(g)
    out.alpha_composite(im)
    return out


def text_center(d, cx, y, text, f, fill, stroke=0, stroke_fill=(0, 0, 0)):
    w = d.textlength(text, font=f)
    d.text((cx - w / 2, y), text, font=f, fill=fill, stroke_width=stroke, stroke_fill=stroke_fill)


def frame(d, w, h, inset, width, color):
    d.rectangle([inset, inset, w - inset - 1, h - inset - 1], outline=color, width=width)


def cover(W=140, H=200, S=4):
    w, h = W * S, H * S
    im = gradient(w, h, NAVY_TOP, NAVY_BOTTOM).convert("RGBA")
    glyph_pattern(im, 3, 22)
    sun = glow(winged_sun(int(w * 0.5)), 18)
    im.alpha_composite(sun, ((w - sun.width) // 2, int(h * 0.07)))
    d = ImageDraw.Draw(im)
    frame(d, w, h, 10, 10, GOLD)
    frame(d, w, h, 30, 3, GOLD_DARK)
    text_center(d, w / 2, h * 0.335, "Yu-Gi-Oh!", font(SERIF_B, int(w * 0.155)), GOLD, 6, (40, 20, 0))
    text_center(d, w / 2, h * 0.47, "FORBIDDEN", font(SERIF_B, int(w * 0.095)), (255, 255, 255), 4, (20, 10, 40))
    text_center(d, w / 2, h * 0.545, "MEMORIES", font(SERIF_B, int(w * 0.095)), (255, 255, 255), 4, (20, 10, 40))
    # faixa
    y0 = int(h * 0.69)
    d.rectangle([40, y0, w - 41, y0 + int(h * 0.09)], fill=(150, 20, 30))
    d.rectangle([40, y0, w - 41, y0 + int(h * 0.09)], outline=GOLD, width=4)
    text_center(d, w / 2, y0 + h * 0.012, "PS2  ·  PORTUGUÊS", font(SANS_B, int(w * 0.07)), (255, 255, 255))
    text_center(d, w / 2, h * 0.83, "PORT NATIVO", font(SANS_B, int(w * 0.06)), GOLD_LIGHT)
    text_center(d, w / 2, h * 0.895, "Retrogames Patos de Minas", font(SANS, int(w * 0.05)), (200, 205, 235))
    return im.convert("RGB").resize((W, H), Image.LANCZOS)


def back(W=242, H=344, S=3, shots=()):
    w, h = W * S, H * S
    im = gradient(w, h, NAVY_TOP, NAVY_BOTTOM).convert("RGBA")
    glyph_pattern(im, 9, 18)
    d = ImageDraw.Draw(im)
    frame(d, w, h, 8, 8, GOLD)
    text_center(d, w / 2, 40, "Yu-Gi-Oh! Forbidden Memories", font(SERIF_B, int(w * 0.055)), GOLD, 3, (40, 20, 0))
    text_center(d, w / 2, 90, "Port nativo para PlayStation 2", font(SANS_B, int(w * 0.045)), (255, 255, 255))
    y = 150
    for line in ["• Roda direto no Emotion Engine", "• Totalmente em português (BR)",
                 "• Nomes das cartas da dublagem", "• Saves no memory card",
                 "• Projeto de fã, gratuito"]:
        d.text((50, y), line, font=font(SANS, int(w * 0.042)), fill=(220, 225, 245))
        y += int(w * 0.068)
    y += 20
    shots = [s for s in shots if s and os.path.exists(s)]
    if shots:
        tw = (w - 120) // 2
        for i, s in enumerate(shots[:2]):
            t = Image.open(s).convert("RGB").resize((tw, tw * 3 // 4), Image.LANCZOS)
            x = 50 + i * (tw + 20)
            im.paste(t, (x, y))
            d.rectangle([x - 3, y - 3, x + tw + 2, y + tw * 3 // 4 + 2], outline=GOLD, width=3)
        y += tw * 3 // 4 + 30
    text_center(d, w / 2, h - 110, "github.com/guilhermesoti/yugiohps1praps2", font(SANS, int(w * 0.033)), (160, 200, 255))
    text_center(d, w / 2, h - 70, "Yu-Gi-Oh! é marca de Konami / Kazuki Takahashi", font(SANS, int(w * 0.03)), (150, 150, 180))
    return im.convert("RGB").resize((W, H), Image.LANCZOS)


def icon(W=64, S=6):
    w = W * S
    im = gradient(w, w, NAVY_TOP, NAVY_BOTTOM).convert("RGBA")
    sun = glow(winged_sun(int(w * 0.48)), 10)
    im.alpha_composite(sun, ((w - sun.width) // 2, int(w * 0.08)))
    d = ImageDraw.Draw(im)
    text_center(d, w / 2, w * 0.58, "YFM", font(SERIF_B, int(w * 0.3)), GOLD, 6, (40, 20, 0))
    frame(d, w, w, 6, 12, GOLD)
    return im.convert("RGB").resize((W, W), Image.LANCZOS)


def logo(W=300, H=125, S=3):
    w, h = W * S, H * S
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    text_center(d, w / 2, 12, "Yu-Gi-Oh!", font(SERIF_B, int(h * 0.38)), GOLD, 8, (40, 20, 0))
    text_center(d, w / 2, h * 0.6, "FORBIDDEN MEMORIES", font(SERIF_B, int(h * 0.15)), (255, 255, 255), 5, (20, 10, 40))
    return glow(im, 12, GOLD, 90).resize((W, H), Image.LANCZOS)


def background(W=640, H=480):
    im = gradient(W, H, NAVY_TOP, NAVY_BOTTOM).convert("RGBA")
    glyph_pattern(im, 21, 20)
    sun = winged_sun(300)
    sun.putalpha(sun.split()[-1].point(lambda v: v * 30 // 100))
    im.alpha_composite(sun, ((W - sun.width) // 2, (H - sun.height) // 2))
    return im.convert("RGB")


def shot(path, W=250, H=168):
    return Image.open(path).convert("RGB").resize((W, H), Image.LANCZOS)


def main(argv):
    out = argv[1] if len(argv) > 1 else "ART"
    shots = argv[2:]
    os.makedirs(out, exist_ok=True)
    cover().save(os.path.join(out, GAME_ID + "_COV.png"))
    back(shots=shots).save(os.path.join(out, GAME_ID + "_COV2.png"))
    icon().save(os.path.join(out, GAME_ID + "_ICO.png"))
    logo().save(os.path.join(out, GAME_ID + "_LGO.png"))
    background().save(os.path.join(out, GAME_ID + "_BG.png"))
    for i, s in enumerate(shots[:2]):
        shot(s).save(os.path.join(out, GAME_ID + ("_SCR.png" if i == 0 else "_SCR2.png")))
    print("capas em", out)


if __name__ == "__main__":
    main(sys.argv)

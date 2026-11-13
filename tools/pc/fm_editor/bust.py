"""A duelist's bust: its large character, the one the campaign's dialogue
stands on its box, with named expressions that talk and blink
(notes/more-duelists.md, "bust"; src/pc/free_duel/busts.c).

A duelists entry writes it as

    "bust": {"default": "calm", "expressions": [
        {"name": "calm", "image": "busts/<id>/calm.png",
         "talk": ["busts/<id>/calm-talk1.png"], "blink": "busts/<id>/calm-blink.png"},
        ...]}

or "bust": "<a PNG>" (one expression, "default"). The editor keeps each
picture's bytes and the path it was read from; a picture made here goes in
busts/<id>/. "replace_bust": true on a disc duelist's ("replace") entry makes
it the campaign's character too (RosterDuelist.replace_bust).

The game draws an expression's talking and blink frames over its picture
where they differ from it, numbers the expressions 0 (the default) then in
the list's order, and changes four of one record in place; preview() shows a
picture where the game stands it, at the console's resolution.
"""
from __future__ import annotations

import copy as _copy
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import pngio

EXPRESSIONS = 16                  # busts.h BUST_EXPRESSIONS
TALK_FRAMES = 4                   # BUST_TALK_FRAMES
NAME_MAX = 31                     # BUST_NAME - 1
SIDE = 256                        # BUST_SIDE: a picture's most, shrunk to fit
IN_PLACE = 4                      # BUST_IN_PLACE: expressions a record changes in place
SCREEN = (320, 240)
ANCHOR_Y = 0xB2                   # the side's anchor, just under the box's top (func_80039F44)
SIDE_X = {"left": 0x68, "right": 0xD8}
BOX = (0x10 - 4, 0xB0 - 4, 0x120 + 8, 0x30 + 8)      # the stone-framed box, roughly, for the preview
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _'-]*$")
# The campaign character each disc duelist is (tower_config.c
# duelist_characters, Tower_DuelistCharacter): what replace_bust takes over.
DUELIST_CHARACTERS = (
    0,
    0x01, 0x03, 0x02, 0, 0, 0, 0x04, 0x07, 0x10, 0x11,
    0x12, 0x13, 0x0F, 0x14, 0x15, 0x2F, 0x0E, 0x18, 0x02, 0x03,
    0, 0x1A, 0, 0x1C, 0, 0x1E, 0, 0x20, 0, 0x22,
    0x2E, 0x04, 0x23, 0x24, 0x07, 0x04, 0x25, 0x26,
)


def campaign_character(d: int) -> int:
    """The campaign's character disc duelist d is, 0 for none."""
    return DUELIST_CHARACTERS[d] if isinstance(d, int) and 0 < d < len(DUELIST_CHARACTERS) else 0


def sharing(d: int) -> list:
    """The disc duelists that are the same campaign character as d (d too)."""
    character = campaign_character(d)
    return [o for o in range(1, len(DUELIST_CHARACTERS)) if character and DUELIST_CHARACTERS[o] == character]


@dataclass(eq=False)
class Expression:
    name: str
    image: bytes = field(default=None, repr=False)
    image_path: str = None                   # as written; None: the editor's busts/<id>/<name>.png
    talk: list = field(default_factory=list, repr=False)       # bytes, each a frame
    talk_paths: list = field(default_factory=list)             # as written, None where made here
    blink: bytes = field(default=None, repr=False)
    blink_path: str = None


@dataclass(eq=False)
class Bust:
    default: str = ""
    expressions: list = field(default_factory=list)
    written: object = field(default=None, repr=False)      # the value as it was read, None when made here
    unread: bool = False                     # a picture it names could not be read: written back as it was


def short(png: bytes, name: str = "default") -> Bust:
    """A bust of one picture."""
    return Bust(default=name, expressions=[Expression(name=name, image=bytes(png))])


def find(bust: Bust, name: str):
    for e in bust.expressions:
        if e.name.lower() == str(name).lower():
            return e
    return None


def order(bust: Bust) -> list:
    """The expressions as the game numbers them: the default, then the
    list's order."""
    first = find(bust, bust.default) or (bust.expressions[0] if bust.expressions else None)
    return ([first] if first else []) + [e for e in bust.expressions if e is not first]


def number(bust: Bust, e: Expression) -> int:
    return order(bust).index(e)


# --- reading -----------------------------------------------------------------------------------

def _read_png(folder, rel, where, messages):
    from .art import contained
    if not isinstance(rel, str) or not rel.lower().endswith(".png"):
        messages.append(f"{where}: {rel!r} is not a PNG of the mod's; kept as written")
        return None
    if not contained(rel):
        messages.append(f"{where}: {rel} is not a path inside the mod; kept as written")
        return None
    if folder is None:
        return None
    path = Path(folder) / rel
    if not path.is_file():
        messages.append(f"{where}: no file {rel}; kept as written")
        return None
    data = path.read_bytes()
    try:
        pngio.decode(data)
    except pngio.PngError as problem:
        messages.append(f"{where}: {rel} is not a PNG it can read ({problem}); kept as written")
        return None
    return data


def read(value, folder, where: str, messages: list, owned: set):
    """A duelists entry's "bust" (busts.c Busts_Read), its pictures from
    `folder`; the paths read go in `owned` -- the editor's to write afresh,
    or to delete once nothing uses them -- only when every picture of it was
    read: a bust kept as written keeps its files as they are. None when it is
    none."""
    if value is None:
        return None
    bust = Bust(written=_copy.deepcopy(value))
    mine = set()
    bust = _read(value, folder, where, messages, mine, bust)
    if not bust.unread:
        owned.update(mine)
    return bust


def _read(value, folder, where, messages, owned, bust):
    if isinstance(value, str):
        data = _read_png(folder, value, f"{where} bust", messages)
        bust.default = "default"
        bust.expressions = [Expression(name="default", image=data, image_path=value)]
        bust.unread = data is None
        if data is not None:
            owned.add(value)
        return bust
    if not isinstance(value, dict) or not isinstance(value.get("expressions"), list):
        messages.append(f"{where}: \"bust\" is {{\"default\": ..., \"expressions\": [...]}} or a PNG; kept as written")
        bust.unread = True
        return bust
    for index, item in enumerate(value["expressions"]):
        at = f"{where} bust expressions[{index}]"
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or "image" not in item:
            messages.append(f"{at}: an expression is {{\"name\": ..., \"image\": ...}}; kept as written")
            bust.unread = True
            continue
        e = Expression(name=item["name"], image_path=item["image"])
        e.image = _read_png(folder, item["image"], at, messages)
        talk = item.get("talk")
        for rel in talk if isinstance(talk, list) else [talk] if talk is not None else []:
            data = _read_png(folder, rel, at, messages)
            if data is None:
                bust.unread = True
                continue
            e.talk.append(data)
            e.talk_paths.append(rel)
            owned.add(rel)
        if item.get("blink") is not None:
            e.blink = _read_png(folder, item["blink"], at, messages)
            e.blink_path = item["blink"]
            if e.blink is None:
                bust.unread = True
            else:
                owned.add(item["blink"])
        if e.image is None:
            bust.unread = True
        else:
            owned.add(item["image"])
        bust.expressions.append(e)
    default = value.get("default")
    bust.default = default if isinstance(default, str) else (bust.expressions[0].name if bust.expressions else "")
    return bust


# --- writing -----------------------------------------------------------------------------------

def _slug(text: str) -> str:
    out = re.sub(r"[^A-Za-z0-9_-]+", "-", text.strip()).strip("-").lower()
    return out or "expression"


def paths(bust: Bust, key: str) -> dict:
    """Each picture's path: as it was read, else the editor's own under
    busts/<key>/ ({(expression, "image" | ("talk", k) | "blink"): path})."""
    out, taken = {}, set()

    def own(rel):
        stem, n = rel[:-4], 2
        while rel in taken:
            rel = f"{stem}-{n}.png"
            n += 1
        taken.add(rel)
        return rel
    folder = f"busts/{key}"
    for e in bust.expressions:
        if e.image_path:
            taken.add(e.image_path)
        taken.update(p for p in e.talk_paths if p)
        if e.blink_path:
            taken.add(e.blink_path)
    for e in bust.expressions:
        name = _slug(e.name)
        out[(id(e), "image")] = e.image_path or own(f"{folder}/{name}.png")
        for k in range(len(e.talk)):
            given = e.talk_paths[k] if k < len(e.talk_paths) else None
            out[(id(e), ("talk", k))] = given or own(f"{folder}/{name}-talk{k + 1}.png")
        if e.blink is not None or e.blink_path:
            out[(id(e), "blink")] = e.blink_path or own(f"{folder}/{name}-blink.png")
    return out


def value(bust: Bust, key: str):
    """The entry's "bust" as the game reads it; as it was written when it
    says the same, and as written when a picture of it could not be read."""
    if bust.unread and bust.written is not None:
        return _copy.deepcopy(bust.written)
    where = paths(bust, key)
    if len(bust.expressions) == 1 and bust.expressions[0].name == "default" and bust.default == "default" and \
            not bust.expressions[0].talk and bust.expressions[0].blink is None and isinstance(bust.written, str):
        out = where[(id(bust.expressions[0]), "image")]
    else:
        items = []
        for e in bust.expressions:
            item = {"name": e.name, "image": where[(id(e), "image")]}
            if e.talk:
                talks = [where[(id(e), ("talk", k))] for k in range(len(e.talk))]
                item["talk"] = talks[0] if len(talks) == 1 and isinstance(_written_talk(bust, e), str) else talks
            if e.blink is not None:
                item["blink"] = where[(id(e), "blink")]
            items.append(item)
        out = {"default": bust.default, "expressions": items}
    if bust.written is not None and _same(bust.written, out):
        return _copy.deepcopy(bust.written)
    return out


def _written_talk(bust: Bust, e: Expression):
    if isinstance(bust.written, dict):
        for item in bust.written.get("expressions") or []:
            if isinstance(item, dict) and item.get("name") == e.name:
                return item.get("talk")
    return None


def _same(a, b) -> bool:
    import json
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


def files(bust: Bust, key: str) -> dict:
    """{path in the mod: bytes} of its pictures."""
    if bust.unread and bust.written is not None:
        return {}
    where = paths(bust, key)
    out = {}
    for e in bust.expressions:
        if e.image is not None:
            out[where[(id(e), "image")]] = e.image
        for k, data in enumerate(e.talk):
            out[where[(id(e), ("talk", k))]] = data
        if e.blink is not None:
            out[where[(id(e), "blink")]] = e.blink
    return out


# --- editing -----------------------------------------------------------------------------------

def check_name(bust: Bust, name: str, but=None) -> str:
    """Why a name would not do, "" when it does."""
    name = name.strip()
    if not name:
        return "an expression has a name"
    if len(name.encode("utf-8")) > NAME_MAX:
        return f"a name is at most {NAME_MAX} bytes"
    if not NAME_RE.match(name):
        return "letters, digits, spaces, - _ and ' only, a letter or digit first"
    other = find(bust, name)
    if other is not None and other is not but:
        return f"there is an expression \"{other.name}\" already"
    return ""


def add(bust: Bust, name: str, png: bytes) -> Expression:
    pngio.decode(png)
    problem = check_name(bust, name)
    if problem:
        raise ValueError(problem)
    e = Expression(name=name.strip(), image=bytes(png))
    bust.expressions.append(e)
    if not bust.default or find(bust, bust.default) is None:
        bust.default = e.name
    return e


def rename(bust: Bust, e: Expression, name: str):
    problem = check_name(bust, name, but=e)
    if problem:
        raise ValueError(problem)
    if bust.default.lower() == e.name.lower():
        bust.default = name.strip()
    e.name = name.strip()
    # The editor's own files are named for the expression: new names, and
    # the old files go when the mod is saved.
    if e.image_path and e.image_path.startswith("busts/"):
        e.image_path = None
    e.talk_paths = [None if p and p.startswith("busts/") else p for p in e.talk_paths]
    if e.blink_path and e.blink_path.startswith("busts/"):
        e.blink_path = None


def remove(bust: Bust, e: Expression):
    bust.expressions.remove(e)
    if find(bust, bust.default) is None:
        bust.default = bust.expressions[0].name if bust.expressions else ""


def move(bust: Bust, e: Expression, by: int):
    at = bust.expressions.index(e)
    to = max(0, min(len(bust.expressions) - 1, at + by))
    bust.expressions.insert(to, bust.expressions.pop(at))


# --- checks ------------------------------------------------------------------------------------

def size_of(png):
    image = picture(png)
    return image.size if image is not None else None


def picture(png):
    if png is None:
        return None
    try:
        return pngio.decode(png)
    except pngio.PngError:
        return None


CELL = 8                          # the parts' squares (busts.c: 8 x 8 texels at the least)
PAGE_CELLS = (SIDE // CELL) ** 2  # the page's: 1024


def fit_size(w: int, h: int) -> tuple:
    """A picture's size as busts.c makes it: shrunk to fit SIDE."""
    if w > SIDE or h > SIDE:
        if w >= h:
            w, h = SIDE, max(1, h * SIDE // w)
        else:
            w, h = max(1, w * SIDE // h), SIDE
    return w, h


def room(size) -> tuple:
    """(cw, ch, free): the picture of `size` on the record's page, rounded up
    to 8 as busts.c places it, and the page's 8 x 8 squares it leaves for its
    expression's frames' changes."""
    w, h = fit_size(*size)
    cw, ch = (w + 7) & ~7, (h + 7) & ~7
    return cw, ch, PAGE_CELLS - (cw // CELL) * (ch // CELL)


_texels_cache = {}


def _texels(png: bytes, w: int, h: int):
    """The picture at w x h as the game samples it, a texel clear (all 0)
    under half covered; padded to the 8s with clear. None unreadable."""
    key = (png, w, h)
    if key in _texels_cache:
        return _texels_cache[key]
    image = picture(png)
    out = None
    if image is not None:
        if image.size != (w, h):
            image = pngio.resample(image, w, h)
        cw, ch = (w + 7) & ~7, (h + 7) & ~7
        rows = bytearray(cw * ch * 4)
        for y in range(h):
            for x in range(w):
                at = (y * w + x) * 4
                if image.rgba[at + 3] * 2 >= 255:
                    o = (y * cw + x) * 4
                    rows[o:o + 3] = image.rgba[at:at + 3]
                    rows[o + 3] = 255
        out = bytes(rows)
    if len(_texels_cache) > 256:
        _texels_cache.clear()
    _texels_cache[key] = out
    return out


def changed_cells(base: bytes, frame: bytes, w: int, h: int) -> set:
    """The 8 x 8 squares of `frame` that busts.c cuts for it over `base`
    (one not clear and not the same as the picture's), by their texels:
    squares alike are cut once."""
    a, b = _texels(base, w, h), _texels(frame, w, h)
    if a is None or b is None:
        return set()
    cw, ch = (w + 7) & ~7, (h + 7) & ~7
    out = set()
    pitch = cw * 4
    for cy in range(0, ch, CELL):
        for cx in range(0, cw, CELL):
            rows = [b[(cy + y) * pitch + cx * 4:(cy + y) * pitch + (cx + CELL) * 4] for y in range(CELL)]
            if not any(any(row[3::4]) for row in rows):
                continue
            if all(row == a[(cy + y) * pitch + cx * 4:(cy + y) * pitch + (cx + CELL) * 4] for y, row in enumerate(rows)):
                continue
            out.add(b"".join(rows))
    return out


def dropped(bust: Bust, e: Expression):
    """What busts.c leaves out of `e` to fit it in a record alone, when its
    frames' changes need more of the page than its picture leaves: (blink
    dropped, talking frames kept, cells needed, free); None when they fit.
    About: the editor compares colors where the game compares the colors
    the palette makes of them."""
    first = find(bust, bust.default) or (bust.expressions[0] if bust.expressions else None)
    size = size_of(first.image) if first is not None else None
    if size is None or picture(e.image) is None:
        return None
    w, h = fit_size(*size)
    _, _, free = room(size)
    talks = [changed_cells(e.image, t, w, h) for t in e.talk[:TALK_FRAMES]]
    blink = changed_cells(e.image, e.blink, w, h) if e.blink is not None else None

    def need(k, with_blink):
        cells = set().union(*talks[:k]) if k else set()
        return len(cells | (blink if with_blink and blink else set()))
    full = need(len(talks), blink is not None)
    if full <= free:
        return None
    keep_blink, keep = blink is not None, len(talks)
    while keep_blink or keep:
        if keep_blink:
            keep_blink = False
        else:
            keep -= 1
        if need(keep, keep_blink) <= free:
            break
    return (e.blink is not None and not keep_blink, keep, full, free)


def dropped_text(e: Expression, result) -> str:
    """The warning for dropped(): what goes and what the expression then
    does, as the game's note says."""
    lost_blink, keep, full, free = result
    had = min(len(e.talk), TALK_FRAMES)
    lost, does = [], []
    if lost_blink:
        lost.append("its blink frame")
        does.append("never blinks")
    if keep < had:
        lost.append("its talking frame" if had == 1 else f"all {had} talking frames" if not keep else
                    f"talking frame {had}" if keep == had - 1 else f"talking frames {keep + 1}-{had}")
        does.append("never talks" if not keep else f"talks with {keep} frame{'s' if keep != 1 else ''}")
    return (f"expression \"{e.name}\": its frames change about {full} of the 8 x 8 squares, and its picture leaves "
            f"{free} of the game's 256 x 256 page free for them: the game makes it without {' and '.join(lost)}, so "
            f"it {' and '.join(does)} (a smaller picture leaves more room: see the page's hint)")


def check(bust: Bust, add_issue):
    """What busts.c would note or do otherwise than meant: add_issue(level,
    message). (What "replace_bust" does is roster.check's.)"""
    if bust.unread:
        add_issue("warning", "a picture of its bust could not be read; the bust is kept as written")
        return
    if not bust.expressions:
        add_issue("error", "its bust has no expression; the game leaves the bust out")
        return
    if len(bust.expressions) > EXPRESSIONS:
        add_issue("error", f"a bust has at most {EXPRESSIONS} expressions; the rest are left out")
    if find(bust, bust.default) is None:
        add_issue("warning", f"its bust's default is none of its expressions; the first, "
                             f"\"{bust.expressions[0].name}\", is taken")
    seen = set()
    first = find(bust, bust.default) or bust.expressions[0]
    size = size_of(first.image)
    for e in bust.expressions:
        problem = check_name(bust, e.name, but=e) if e.name.lower() not in seen else "a name used twice"
        if problem and (problem.startswith("an expression has") or "twice" in problem or "bytes" in problem):
            add_issue("error", f"expression \"{e.name}\": {problem}; the game leaves it out")
        seen.add(e.name.lower())
        frames = [("its picture", e.image)] + [(f"talking frame {k + 1}", t) for k, t in enumerate(e.talk)]
        if e.blink is not None:
            frames.append(("its blink frame", e.blink))
        for label, png in frames:
            if picture(png) is None:
                add_issue("error", f"expression \"{e.name}\": {label} is not a PNG the game can read")
            elif size and size_of(png) != size:
                w, h = size_of(png)
                add_issue("warning", f"expression \"{e.name}\": {label} is {w} x {h}, the default's {size[0]} x "
                                     f"{size[1]}; the game stretches it to that")
        if len(e.talk) > TALK_FRAMES:
            add_issue("warning", f"expression \"{e.name}\": at most {TALK_FRAMES} talking frames; the rest are "
                                 "left out")
        lost = dropped(bust, e)
        if lost is not None:
            add_issue("warning", dropped_text(e, lost))
    if len(bust.expressions) > IN_PLACE:
        add_issue("warning", f"{len(bust.expressions)} expressions: the game changes {IN_PLACE} of one record "
                             "in place; the fifth on come in as another character, sliding")


# --- the preview ------------------------------------------------------------------------------

def frame(e: Expression, which="image"):
    """An expression's picture, or a talking/blink frame drawn over it as the
    game draws it (where the frame is opaque)."""
    base = picture(e.image)
    if base is None or which == "image":
        return base
    over = picture(e.blink if which == "blink" else e.talk[which[1]] if which[1] < len(e.talk) else None)
    if over is None:
        return base
    if over.size != base.size:
        over = pngio.resample(over, base.width, base.height)
    out = bytearray(base.rgba)
    for at in range(0, len(out), 4):
        if over.rgba[at + 3] >= 128:
            out[at:at + 4] = over.rgba[at:at + 3] + b"\xff"
    return pngio.Image(base.width, base.height, bytes(out))


def fitted(image: pngio.Image) -> pngio.Image:
    """The picture as the game makes it: shrunk to fit SIDE, its texels
    clear under half covered, opaque otherwise."""
    w, h = image.size
    if w > SIDE or h > SIDE:
        if w >= h:
            w, h = SIDE, max(1, h * SIDE // w)
        else:
            w, h = max(1, w * SIDE // h), SIDE
        image = pngio.resample(image, w, h)
    out = bytearray(image.rgba)
    for at in range(0, len(out), 4):
        out[at + 3] = 255 if out[at + 3] * 2 >= 255 else 0
    return pngio.Image(image.width, image.height, bytes(out))


def preview(image, side: str = "left", background=(0x1B, 0x21, 0x30)) -> pngio.Image:
    """The 320 x 240 screen with `image` standing where the game stands a
    character on `side`: its middle over the side's place, its bottom on the
    box's top, or, taller than the room above it, its top at the screen's;
    the box marked over it."""
    width, height = SCREEN
    out = bytearray(bytes(background) + b"\xff") * (width * height)
    if image is not None:
        made = fitted(image)
        cw, ch = (made.width + 7) & ~7, (made.height + 7) & ~7
        left = SIDE_X[side] - cw // 2
        top = ANCHOR_Y - (ch if ch < ANCHOR_Y else ANCHOR_Y)
        for y in range(made.height):
            for x in range(made.width):
                sx, sy = left + x, top + y
                at = (y * made.width + x) * 4
                if 0 <= sx < width and 0 <= sy < height and made.rgba[at + 3]:
                    o = (sy * width + sx) * 4
                    out[o:o + 4] = made.rgba[at:at + 4]
    bx, by, bw, bh = BOX
    for y in range(by, min(height, by + bh)):
        for x in range(bx, bx + bw):
            o = (y * width + x) * 4
            edge = y < by + 4 or y >= by + bh - 4 or x < bx + 4 or x >= bx + bw - 4
            out[o:o + 3] = b"\x80\x80\x70" if edge else bytes(v // 3 for v in out[o:o + 3])
    return pngio.Image(width, height, bytes(out))

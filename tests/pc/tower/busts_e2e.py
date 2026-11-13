"""A duelist's bust end to end, in the running game (notes/more-duelists.md,
Busts; notes/tower-mode.md, Story).

With the tests' tower mod (tests/pc/tower/tower-test) and pictures this
script draws (shapes and colors, nothing of the disc's):

  1. a tower story whose left side is the mod's duelist, Ghost Simon, by
     {"duelist": ..., "expression": ...}: its bust of three expressions
     comes in, its mouth moves with the words (the mouth's variant and the
     talking frame's pixels), it blinks (the eyes' variant and the blink
     frame's pixels), and it changes to its two other expressions in place
     (the same character, a new face, nothing sliding);
  2. the old forms in the same story: a campaign character by number and
     by {"character": n}, and a loose PNG;
  3. the campaign's first scene: without a mod its frames hash as a
     master build's; with a mod whose "replace" entry gives Simon Muran a
     bust, but no "replace_bust", they hash as the same mod's without the
     bust (and the shot is the no-mod one, pixel for pixel); with
     "replace_bust", Simon is the mod's bust in the campaign, and only he
     changes (the box, its words, the scene the same).

Pictures of the game's frames go to --out. Exit status 0 when every check
holds.

    python tests/pc/tower/busts_e2e.py --exe tmp/pc/game32/memories-pc.exe \\
        --baseline <a build of master>/memories-pc.exe --out <folder>
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tower_e2e as t2  # noqa: E402
from tower_e2e import (MOD, RECORDS, Tower, check, checks, differences, enter_tower, near, page_wait,  # noqa: E402
                       pixels, stage_of, stage_files, to_second_menu, write_png)
from yfm_control import MODE, Game  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]

# --- the pictures: a figure of 128 x 192, its face's mark the expression's -------------------

W, H = 128, 192
SKIN, BODY, EYE, MOUTH = (240, 200, 150), (90, 60, 160), (255, 255, 255), (120, 40, 40)
OPEN, OPEN2, SHUT = (250, 250, 250), (250, 240, 160), (10, 10, 10)
MARKS = {"calm": (40, 200, 60), "angry": (220, 40, 40), "sad": (40, 90, 230)}
MARK = (52, 20, 76, 36)              # the face's mark: x0, y0, x1, y1 of the picture
EYES = ((46, 44, 54, 50), (74, 44, 82, 50))
MOUTH_AT = (56, 62, 72, 74)
# The left side's anchor (func_80039F44: x 0x68): the picture's middle there,
# its top at the screen's (taller than the 178 rows above the box).
LEFT_X = 0x68 - W // 2


def inside(box, x, y) -> bool:
    return box[0] <= x < box[2] and box[1] <= y < box[3]


def figure(mark, talk=0, blink=False):
    def pixel(x, y):
        if any(inside(box, x, y) for box in EYES):
            if blink:
                return SHUT + (255,) if y == 47 else SKIN + (255,)
            return EYE + (255,)
        if inside(MOUTH_AT, x, y) and talk:
            return (OPEN if talk == 1 else OPEN2) + (255,)
        if 56 <= x < 72 and 66 <= y < 68:
            return MOUTH + (255,)
        if inside(MARK, x, y):
            return mark + (255,)
        if (x - 64) ** 2 + (y - 50) ** 2 < 34 ** 2:
            return SKIN + (255,)
        if y >= 90 and abs(x - 64) < 24 + (y - 90) // 3:
            return BODY + (255,)
        return (0, 0, 0, 0)
    return pixel


def bust_files(mod: Path, who: str = "ghost"):
    for name, mark in MARKS.items():
        write_png(mod / f"busts/{who}/{name}.png", W, H, figure(mark))
        write_png(mod / f"busts/{who}/{name}-talk.png", W, H, figure(mark, talk=1))
        write_png(mod / f"busts/{who}/{name}-talk2.png", W, H, figure(mark, talk=2))
        write_png(mod / f"busts/{who}/{name}-blink.png", W, H, figure(mark, blink=True))


def bust(who: str = "ghost") -> dict:
    return {"default": "calm", "expressions": [
        {"name": name, "image": f"busts/{who}/{name}.png",
         "talk": [f"busts/{who}/{name}-talk.png", f"busts/{who}/{name}-talk2.png"],
         "blink": f"busts/{who}/{name}-blink.png"} for name in MARKS]}


def manifest(change) -> dict:
    """The tests' tower mod, changed; a bust asks for mod API 17."""
    data = json.loads((MOD / "mod.json").read_text(encoding="utf-8"))
    change(data)
    if any("bust" in entry for entry in data.get("duelists", [])):
        data["min_api"] = 17
    return data


def all_files(mod: Path):
    stage_files(mod)
    bust_files(mod)
    bust_files(mod, "simon")


# --- reading the stage ------------------------------------------------------------------------

def piece(game: Game, side: int, k: int) -> int:
    """The variant a side's piece k (0 body, 1 eyes, 2 mouth) shows, -1 for none."""
    pointer = game.u32(RECORDS + side * 0x4C + k * 4)
    return game.u8(pointer + 0x69) if pointer else -1


def mouth_open(frame) -> bool:
    return any(near(frame[y][LEFT_X + x], OPEN) or near(frame[y][LEFT_X + x], OPEN2)
               for y in range(63, 73) for x in range(58, 70))


def next_page(t: Tower, seen: set | None = None):
    """Cross on a page's end, on to the next page's end (the box leaves its
    waiting state first), noting the left side's character all the way."""
    g = t.game
    g.press("cross", hold=4, after=0)
    for _ in range(120):
        if g.u8(0x800EB0F8 + 0x51) != 0x84:
            break
        g.step(1)
    for _ in range(900):
        g.step(1)
        if seen is not None:
            seen.add(stage_of(g)["left"])
        if g.u8(0x800EB0F8 + 0x51) == 0x84 and stage_of(g)["steps"] == (0, 0):
            break
    g.step(20)


def screen_mark(frame) -> tuple:
    return frame[28][LEFT_X + 64]



# --- 1, 2: the tower's story -------------------------------------------------------------------

def phase_story(exe: Path, out: Path):
    def change(data):
        data["duelists"][0]["bust"] = bust()
        tower = data["tower"]
        ghost = {"duelist": "tower-test:ghost"}
        tower["intro"] = {
            "scene": "none", "characters": {"left": dict(ghost, expression="calm"), "right": "none"},
            "lines": [
                {"text": "Calm words, typed one by one, so that my mouth moves with them.", "speaker": "left"},
                {"text": "Now I am angry!", "speaker": "left",
                 "characters": {"left": dict(ghost, expression="angry")}},
                {"text": "And now I am sad.", "speaker": "left",
                 "characters": {"left": dict(ghost, expression="sad")}},
                {"text": "A campaign character by its number.", "speaker": "right", "characters": {"right": 4}},
                {"text": "And by an object.", "speaker": "right",
                 "characters": {"right": {"character": 4, "expression": 1}}},
                {"text": "A loose PNG.", "speaker": "right", "characters": {"right": "art/hero.png"}},
                "The end."]}
    t = Tower(exe, out, "busts-story", manifest(change), files=all_files)
    g = t.game
    to_second_menu(g)
    enter_tower(t)
    # Page 1, frame by frame while it is typed: the mouth's variant moves.
    shapes, talking_shot = set(), None
    for _ in range(600):
        g.step(1)
        stage = stage_of(g)
        if stage["left"] == 0xFF:
            continue
        shape = piece(g, 0, 2)
        shapes.add(shape)
        if talking_shot is None and shape in (1, 2, 3) and stage["steps"] == (0, 0):
            # The frame on the screen is drawn a frame or two after the
            # variant is set: a few of them, until the mouth shows open.
            for k in range(6):
                g.step(1)
                path = g.shot(f"busts-talking-{k}.png")
                if mouth_open(pixels(path)):
                    talking_shot = path
                    break
        if g.u8(0x800EB0F8 + 0x51) == 0x84 and stage["steps"] == (0, 0):
            break
    stage = stage_of(g)
    check(f"the mod's duelist stands on the left as its bust (record 0 holds {stage['left']:#x}, one of the "
          f"characters the disc keeps empty), speaking ({stage['quiet']})",
          stage["left"] in (0x27, 0x28, 0x29, 0x0B, 0x05) and stage["quiet"][0] == 0)
    calm_id = stage["left"]
    check(f"its mouth moves with the words: the mouth's variants seen {sorted(shapes)}",
          bool(shapes & {1, 2, 3}) and 0 in shapes)
    check("the talking frame drawn over the calm face: the open mouth's pixels on the screen",
          talking_shot is not None)
    if talking_shot:
        shutil.copy(talking_shot, out / "b1-talking.png")
    # At the page's end it blinks, when the game's random wait says so.
    blinked = None
    for _ in range(900):
        g.step(1)
        if piece(g, 0, 1) == 1:
            for k in range(6):
                g.step(1)
                path = g.shot(f"busts-blink-{k}.png")
                if near(pixels(path)[47][LEFT_X + 50], SHUT):
                    blinked = path
                    break
            break
    check("it blinks: the eyes' variant 1, the blink frame's shut eye on the screen", blinked is not None)
    if blinked:
        shutil.copy(blinked, out / "b2-blink.png")
    for _ in range(300):
        if piece(g, 0, 1) == 0:
            break
        g.step(1)
    g.step(4)
    t.shot(out, "b3-calm.png")
    calm = pixels(out / "b3-calm.png")
    check(f"calm: its face's mark is calm's green ({screen_mark(calm)}), its eyes open again",
          near(screen_mark(calm), MARKS["calm"]) and near(calm[47][LEFT_X + 50], EYE))
    for name, file in (("angry", "b4-angry.png"), ("sad", "b5-sad.png")):
        seen = set()
        next_page(t, seen)
        t.shot(out, file)
        frame = pixels(out / file)
        record = g.peek(RECORDS, 0x4C)
        check(f"{name}, in place: the same character all along ({sorted(seen)}), expression {record[0x31]}, "
              f"its mark {screen_mark(frame)}",
              seen == {calm_id} and near(screen_mark(frame), MARKS[name]) and record[0x31] in (1, 2))
    # The old forms on the right.
    next_page(t)
    t.shot(out, "b6-character-number.png")
    check(f"the old form, a character's number: Seto (4) on the right ({stage_of(g)['right']:#x})",
          stage_of(g)["right"] == 4)
    next_page(t)
    record = g.peek(RECORDS + 0x4C, 0x4C)
    check(f"the old form, {{\"character\": 4, \"expression\": 1}}: Seto, expression {record[0x31]}",
          record[0x30] == 4 and record[0x31] == 1)
    next_page(t)
    t.shot(out, "b7-loose-png.png")
    frame = pixels(out / "b7-loose-png.png")
    body = [frame[150][x] for x in range(220, 300)]
    check(f"the old form, a loose PNG: the hero on the right ({stage_of(g)['right']:#x})",
          stage_of(g)["right"] in (0x27, 0x28, 0x29, 0x0B, 0x05) and any(near(c, t2.HERO_BODY) for c in body))
    t.until_said("tower: status")
    check("the story ran to its end", "tower: status" in t.log())
    t.quit()


# --- 3: the campaign --------------------------------------------------------------------------

def campaign(exe: Path, out: Path, name: str, data: dict | None, frames: int = 90):
    """NEW GAME to Simon Muran's first page (as tower_e2e.retail_simon),
    with the tests' mod as `data` says (None: no mod): its shot, and the
    hashes of `frames` frames from there."""
    folder = out / name
    shutil.rmtree(folder, ignore_errors=True)
    (folder / "mods").mkdir(parents=True)
    settings = {"mod.3d-monsters": 0, "mod.hand-camera": 0}
    if data is not None:
        shutil.copytree(MOD, folder / "mods/tower-test")
        (folder / "mods/tower-test/mod.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
        all_files(folder / "mods/tower-test")
        settings["mod.tower-test"] = 1
    g = Game(executable=exe, out=folder / "run", mods_dir=folder / "mods", settings=settings,
             env={"MEMORIES_TRACE": "mods"})
    try:
        g.wait_until(lambda g: g.resident("main_menu"), 3000, what="the title")
        g.press_until(lambda g: g.resident("password"), ["start", "cross"], every=40, what="the name entry")
        g.press_until(lambda g: g.player_name(), "cross", every=20, what="a letter of the name")
        g.press("start")
        g.press_until(lambda g: g.mode() == MODE["campaign"], "cross", every=40, what="the story")
        g.press_until(lambda g: g.peek(RECORDS, 0x4C)[0x30] == 1 and g.u8(0x800EB0F8 + 0x51) == 0x84 and
                      g.peek(RECORDS, 0x4C)[0x33] == 0, "cross", every=40, timeout=20000, what="Simon's first page")
        g.step(20)
        shot = g.shot(f"{name}.png")
        hashes = []
        for _ in range(frames):
            g.step(1)
            hashes.append(g.hash())
        log = g.log_path.read_text(encoding="utf-8", errors="replace")
        return shot, hashes, log
    finally:
        g.quit()


def phase_campaign(exe: Path, out: Path, baseline: Path | None):
    def simon(replace: bool):
        def change(data):
            entry = {"replace": "Simon Muran", "bust": bust("simon")}
            if replace:
                entry["replace_bust"] = True
            data["duelists"].append(entry)
        return manifest(change)

    plain, plain_hashes, _ = campaign(exe, out, "campaign-no-mod", None)
    if baseline and baseline.is_file():
        master, master_hashes, _ = campaign(baseline, out, "campaign-master", None)
        check(f"without a mod, the campaign's first scene hashes as a master build's ({len(plain_hashes)} frames)",
              plain_hashes == master_hashes)
        count, box = differences(master, plain)
        check(f"and its shot is the master build's, pixel for pixel ({count} differ)", count == 0)
    else:
        check("a master build given (--baseline) for the campaign's frames", False)
    # VRAM holds the mod's own pictures too (its title button's), so the
    # frames' hashes are compared with the same mod's without the bust.
    _, mod_hashes, _ = campaign(exe, out, "campaign-mod-no-bust", manifest(lambda data: None))
    kept, kept_hashes, log = campaign(exe, out, "campaign-bust-kept", simon(False))
    check(f"a bust for Simon without \"replace_bust\": the campaign's frames (all of VRAM) hash as the same mod's "
          f"without the bust ({len(kept_hashes)} frames)", kept_hashes == mod_hashes)
    count, box = differences(plain, kept)
    check(f"and its shot is the one without any mod, pixel for pixel ({count} differ)", count == 0)
    replaced, replaced_hashes, log = campaign(exe, out, "campaign-bust-replaced", simon(True))
    shutil.copy(replaced, out / "b8-campaign-replaced.png")
    shutil.copy(plain, out / "b9-campaign-retail.png")
    frame = pixels(replaced)
    check("with \"replace_bust\" the log says the campaign's character 1 is Simon's bust",
          "busts: campaign character 1 is Simon Muran's bust" in log)
    check(f"and the campaign shows it: the calm mark on Simon's place ({screen_mark(frame)})",
          near(screen_mark(frame), MARKS["calm"]))
    count, box = differences(plain, replaced, range(176, 240))
    check(f"the box and its words are the campaign's own ({count} pixels differ below the box's top)",
          count == 0)
    above, box = differences(plain, replaced, range(0, 176))
    check(f"above it, only Simon's place changed ({above} pixels, around {box})",
          above > 0 and box is not None and box[2] <= 0x68 + 100)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=ROOT / "tmp/pc/game32" / ("memories-pc.exe" if sys.platform == "win32" else "memories-pc"))
    parser.add_argument("--baseline", type=Path, help="a master build, for the campaign's frames without a mod")
    parser.add_argument("--out", type=Path, default=ROOT / "tmp/pc/busts-e2e")
    parser.add_argument("--only", help="run one phase")
    options = parser.parse_args()
    options.out.mkdir(parents=True, exist_ok=True)
    phases = {"story": lambda: phase_story(options.exe, options.out),
              "campaign": lambda: phase_campaign(options.exe, options.out, options.baseline)}
    for name, phase in phases.items():
        if options.only and name != options.only:
            continue
        try:
            phase()
        except Exception as error:   # a phase that cannot go on fails, the others still run
            traceback.print_exc()
            check(f"phase {name} ran to its end ({error!r})", False)
    failed = [what for what, ok in checks if not ok]
    print(f"{len(checks) - len(failed)}/{len(checks)} checks hold")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

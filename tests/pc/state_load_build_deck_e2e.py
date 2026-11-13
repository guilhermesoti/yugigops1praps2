"""A save state loaded in Build Deck shows the pane it was saved in (issue #229).

The report: a state saved with F5 in Build Deck before a duel, the cursor
moved, F7: the picture stood still while the game went on underneath, until
moving between the Chest and the Deck brought it back. The cursor had gone
to the other pane. Build Deck shows its two panes side by side and slides
between them by gGraphics_sViewportX (build_deck_pane_input.c), a small-data
variable (section(".sbss")); the Windows build kept small data in a host
section of its own, in no state, so a load restored the pane and the cursors
but left the view on the pane the player had gone to: the pane that ran was
off the screen, and only the next slide moved the view. Fixed by bfeaf27c3
("Port: the Windows build keeps the game's small data in save states"): on
its parent 4 of these 10 checks fail (2 on each screen), the frames parting
at the first one. Only the Windows builds could fail it: the ELF build always
kept small data with the rest of the game's data.

In the running game (tools/pc/yfm_control.py), on the Build Deck screen of
the main menu and on the one before a duel: a state is saved in the Chest,
loaded once at once and played on (the reference), then loaded again after
going over to the Deck and moving its cursor, and played on the same way.
Both must show the Chest, frame for frame: the frame hashes (all of VRAM)
and the shown picture. gGraphics_sViewportX is not read by its guest
address: the Windows build keeps small data after the rest of its game
data, not there (a peek at 0x8009B146 read 0 with the Deck on screen). The
pictures go to --out, emptied first. It needs the disc, so it is a local
gate, run before a PR like the replays (notes/agent-control.md).

    python tests/pc/state_load_build_deck_e2e.py --exe tmp/pc/game32/memories-pc.exe --out <folder>
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pc"))
from yfm_control import Game  # noqa: E402

SETTINGS = {"mod.3d-monsters": 0, "mod.hand-camera": 0, "deck_slots": 0}
DECK_SCREEN = 0x80    # D_8009B26E on the deck screen before a duel (yfm_control.duel_ready)

checks: list[tuple[str, bool]] = []


def check(what: str, ok: bool) -> bool:
    checks.append((what, bool(ok)))
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    return ok


def picture(game: Game, name: str) -> str:
    return hashlib.sha1(game.shot(name).read_bytes()).hexdigest()


def play(game: Game, tag: str) -> tuple[list[int], str]:
    """Down twice in the pane on screen, a hash a frame; then its picture."""
    hashes = []
    for _ in range(2):
        game.pad("down")
        for _ in range(4):
            game.step(1)
            hashes.append(game.hash())
        game.pad(0)
        for _ in range(20):
            game.step(1)
            hashes.append(game.hash())
    return hashes, picture(game, f"{tag}.png")


def case(game: Game, name: str) -> None:
    chest = picture(game, f"{name}-chest.png")
    game.save(f"{name}.state")
    game.load(f"{name}.state")
    loaded_at_once = picture(game, f"{name}-reference-loaded.png")
    reference, reference_after = play(game, f"{name}-reference-after")
    check(f"{name}: the cursor moves in the Chest after a load", reference_after != loaded_at_once)

    game.load(f"{name}.state")
    game.press("right", hold=4, after=90)   # over to the Deck: the view slides
    game.press("down", hold=4, after=20)
    deck = picture(game, f"{name}-deck-before-load.png")
    check(f"{name}: over in the Deck, the view slid away from the Chest", deck not in (chest, loaded_at_once))

    game.load(f"{name}.state")
    check(f"{name}: the picture right after the load is the Chest's",
          picture(game, f"{name}-loaded.png") == loaded_at_once)
    hashes, after = play(game, f"{name}-after")
    differ = next((i for i, (a, b) in enumerate(zip(reference, hashes)) if a != b), None)
    check(f"{name}: frame for frame as after the first load"
          + ("" if differ is None else f" (they part at frame {differ})"), differ is None)
    check(f"{name}: the cursor moving in the Chest is what is shown", after == reference_after)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", help="the game (default: yfm_control's)")
    parser.add_argument("--out", default=str(ROOT / "tmp/pc/state-load-build-deck"))
    args = parser.parse_args()
    out = Path(args.out)
    shutil.rmtree(out, ignore_errors=True)   # no settings, saves or log of an earlier run
    with Game(args.exe, out=out, settings=SETTINGS) as game:
        game.step(900)   # the logos, the movie and the title
        game.poke("gDuel_awPlayerDeck", struct.pack("<40H", *range(1, 41)))
        game.poke("gLibrary_abCardChest", bytes([3]) * 722)
        game.goto("build_deck")
        game.step(120)
        case(game, "build-deck")
        game.goto("duel", opponent=3, deck="1-40")
        game.wait_until(lambda g: g.u8("D_8009B26E") == DECK_SCREEN, 6000, what="the deck screen before the duel")
        game.step(120)
        case(game, "before-duel")
    failed = [what for what, ok in checks if not ok]
    print(f"{len(checks) - len(failed)}/{len(checks)} checks hold; pictures in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

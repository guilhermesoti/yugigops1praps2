#ifndef MEMORIES_PC_FREE_DUEL_BUSTS_H
#define MEMORIES_PC_FREE_DUEL_BUSTS_H
/* A duelist's large character, its "bust" (notes/more-duelists.md, Busts):
 * the picture the campaign's dialogue stands on its stone-framed box, here
 * a mod's own, with named expressions that talk and blink as the disc's do.
 *
 * A mod gives a "duelists" entry a "bust":
 *
 *   "bust": {
 *       "default": "calm",
 *       "expressions": [
 *           {"name": "calm", "image": "busts/ghost/calm.png",
 *            "talk": ["busts/ghost/calm-talk.png"], "blink": "busts/ghost/calm-blink.png"},
 *           {"name": "angry", "image": "busts/ghost/angry.png"}
 *       ]
 *   }
 *
 * (or "bust": "busts/ghost.png", one expression named "default"). Each
 * expression is a PNG with clear parts, and the frames its mouth moves
 * through and its eyes close in, drawn over it where they differ from it;
 * one it lacks is its picture alone, so it neither speaks nor blinks.
 *
 * What the game draws is the disc's kind of character (func_8003A560's
 * cache slot, func_8003A560.h): a 384 x 256 sheet at 8 bits whose first 256
 * columns the parts are cut from, two rows of 256 colors and a parts bank
 * (DisplayEffect_BuildResourceObjects): per expression a body, eyes and a
 * mouth, as the disc's characters are. A record holds up to four
 * expressions, the most the game's codes change in place (F6 id 4e, two
 * bits); a bust with more, or whose pictures do not fit one sheet together,
 * is made into several records, and the stage brings another record's
 * expression in as another character (it slides).
 *
 * The expressions are told in this order: the default first, then the rest
 * as the list has them. Expression n of the campaign's codes (0-3) is the
 * nth of that order, so a bust that replaces a disc duelist's
 * ("replace_bust", duelists.c) shows its default where the campaign shows
 * the character as it first comes in, and its next three where the
 * campaign changes that character's face. */
#include <stddef.h>

struct JsonValue;

#define BUST_EXPRESSIONS 16          /* a bust's expressions at most */
#define BUST_TALK_FRAMES 4           /* the frames a mouth moves through */
#define BUST_NAME 32
#define BUST_PATH 512
#define BUST_IN_PLACE 4              /* expressions one record changes in place */
#define BUST_RECORDS 127             /* every bust's records together (tower_state.h's pool) */
#define BUST_SIDE 256                /* a picture's most: one 8-bit texture page */
#define BUST_SHEET_W 384             /* the cache slot's texels a row */
#define BUST_SHEET_H 256
#define BUST_BANK 0x800
#define BUST_SLOT (BUST_SHEET_W * BUST_SHEET_H + 2 * 256 * 2)

/* "bust" as a duelists entry writes it, the PNGs found in `directory`: the
 * bust's number, or -1 (noted at `where`). */
int Busts_Read(const char *mod, const char *directory, const struct JsonValue *value, const char *where);
/* Back to none: Duelists_Clear. */
void Busts_Clear(void);
/* The busts the duelists placed have, made into the game's records, and
 * the campaign characters a "replace_bust" takes over: once, at the end of
 * Duelists_Build. */
void Busts_Make(void);

int Busts_Count(int bust);
const char *Busts_Name(int bust, int expression);
/* The default expression: the one a duelist is shown with when a story
 * names none. */
int Busts_Default(int bust);
/* An expression by its name, -1 for none. */
int Busts_Named(int bust, const char *name);
/* The expression the game's codes number n (0 the default, then the list's
 * order); the default past the bust's count. */
int Busts_Ordered(int bust, int n);
/* Where `expression` is drawn: its record and its place in it (0-3), 1;
 * 0 when it was not made. */
int Busts_Place(int bust, int expression, int *record, int *slot);
/* The campaign character (1-0x40) the bust stands for (replace_bust), 0
 * none: then its first record is that character. */
int Busts_Character(int bust);
/* Record `record`'s cache slot (BUST_SHEET_W x BUST_SHEET_H texels, then
 * two rows of 256 colors) and parts bank, NULL when there is none. */
const unsigned char *Busts_Record(int record, const unsigned short **clut, const unsigned char **bank);
/* The record standing for campaign character `character` ("replace_bust"),
 * -1 none: func_8003A560 fills its cache slot from it (zz_tower_storage.c). */
int Busts_Replacing(int character);

/* --- the record maker, on pictures already made (busts.c; the tests) --- */

/* One expression's pictures, w x h texels each (w, h multiples of 8, at
 * most BUST_SIDE), 0 clear, all through one palette. */
typedef struct {
    const unsigned char *base;
    const unsigned char *talk[BUST_TALK_FRAMES];
    int talk_count;
    const unsigned char *blink;       /* NULL: none */
} BustFrames;
/* Up to BUST_IN_PLACE expressions made into one record: `sheet` the slot's
 * texels (BUST_SHEET_W a row), `bank` its BUST_BANK bytes. 1 when they fit
 * (the first's picture at the sheet's top left, the others' parts that
 * differ from it beside it), 0 when not. */
int Busts_Pack(int w, int h, const BustFrames *frames, int count, unsigned char *sheet, unsigned char *bank);

#endif

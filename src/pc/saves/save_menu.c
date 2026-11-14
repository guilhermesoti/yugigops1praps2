/* The save slot menu. See save_menu.h. */
#include "save_menu.h"
#include "flow_text.h"
#include "pc/guest/state.h"
#include "pc/platform/settings.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { VIEW_CLOSED, VIEW_LIST, VIEW_CONFIRM, VIEW_MESSAGE };
enum { CHOICE_OVERWRITE, CHOICE_KEEP };
#define BACK_TO_LIST -1
#ifdef MEMORIES_PS2
#define PT(en, pt) pt /* the PS2 build's menu speaks Portuguese */
#else
#define PT(en, pt) en
#endif
#define MESSAGE_FRAMES 75 /* how long a success message stays without a button */

/* Everything a save state needs to carry an open menu across. The buffers
 * are guest addresses, the same in every build. */
static struct {
    int view, step, started, side;
    int cursor, top, choice;
    int after_message;  /* the outcome once the message closes, or BACK_TO_LIST */
    int message_waits;  /* 1: only a button closes it */
    unsigned message_frames;
    char message[160];
    unsigned char *buffer, *second;
    int size;
    int current_slot;   /* the slot loaded or saved last, -1 for none */
    int pair_slot[2];   /* the slots the two sides of a pair load came from */
    unsigned changes;
    SaveSlotInfo slots[SAVE_SLOT_COUNT];
} menu = {VIEW_CLOSED, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", NULL, NULL, 0, -1, {-1, -1}, 0, {{0}}};

static SaveSlotCheck check;
static int shown_rows = SAVE_SLOT_COUNT; /* how many rows the last draw had room for */
/* Where the last draw put what the mouse can point at (SaveMenu_Pointer), in
 * the canvas's pixels. Good while the menu shows the view and the first row
 * it was drawn for. */
static struct {
    int valid, view, top;
    int x, y, w, h;           /* the rows: the highlight's width and the first row's top */
    int row_h, rows;
    int button[2][4];         /* the confirm's two buttons: x, y, w, h */
} hit;
/* Under a failed save's message: where and why (SaveSlots_LastError). Kept
 * out of `menu`, whose bytes save states carry, so their layout stays. */
static char detail[1200];

static void changed(void) { menu.changes++; }

static int state_code(const unsigned char *state)
{
    return (int)(state[0x334] | state[0x335] << 8 | state[0x336] << 16 | (unsigned)state[0x337] << 24);
}

static unsigned state_sequence(const unsigned char *state)
{
    return state[0x404] | state[0x405] << 8 | state[0x406] << 16 | (unsigned)state[0x407] << 24;
}

/* Whether a used slot holds the game being saved as it was last loaded or
 * saved: the save being written carries the next sequence number. Anything
 * else of the same duelist is another point in their game, older or newer. */
static int same_game(const SaveSlotInfo *info)
{
    return info->status == SAVE_SLOT_USED && info->duelist_code == state_code(menu.buffer) &&
           info->sequence + 1 == state_sequence(menu.buffer);
}

static int selectable(int slot)
{
    const SaveSlotInfo *info = &menu.slots[slot];
    if (menu.step == SAVE_MENU_SAVE) return 1;
    if (info->status != SAVE_SLOT_USED) return 0;
    return !(menu.step == SAVE_MENU_LOAD_PAIR && menu.side == 1 && slot == menu.pair_slot[0]);
}

static void show_message(int after, int waits, const char *format, int slot)
{
    snprintf(menu.message, sizeof(menu.message), format, slot + 1);
    detail[0] = '\0';
    menu.view = VIEW_MESSAGE;
    menu.after_message = after;
    menu.message_waits = waits;
    menu.message_frames = 0;
    changed();
}

static int close_with(int outcome)
{
    menu.view = VIEW_CLOSED;
    changed();
    return outcome;
}

static void keep_cursor_shown(void)
{
    int rows = shown_rows < 1 ? 1 : shown_rows;
    if (menu.cursor < menu.top) menu.top = menu.cursor;
    if (menu.cursor >= menu.top + rows) menu.top = menu.cursor - rows + 1;
}

/* Where the cursor starts: the slot in use, else the newest save for a
 * load and the first empty slot for a save. */
static int first_cursor(void)
{
    int slot, best = -1;
    if (menu.current_slot >= 0 && selectable(menu.current_slot)) return menu.current_slot;
    for (slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        const SaveSlotInfo *info = &menu.slots[slot];
        if (!selectable(slot)) continue;
        if (menu.step == SAVE_MENU_SAVE) {
            if (info->status == SAVE_SLOT_EMPTY) return slot;
            continue;
        }
        if (best < 0 || info->saved_at > menu.slots[best].saved_at) best = slot;
    }
    return best < 0 ? 0 : best;
}

static int write_pair(void)
{
    static unsigned char states[2][SAVE_SLOT_STATE_SIZE], first[SAVE_SLOT_STATE_SIZE];
    int side;
    if (menu.size != 0x400 || menu.pair_slot[0] == menu.pair_slot[1]) return 2;
    /* Validate both destinations before changing either save. Build from
     * the sound copy so a recovered slot keeps its untouched progress. */
    for (side = 0; side < 2; side++) {
        int slot = menu.pair_slot[side];
        const unsigned char *record = side ? menu.second : menu.buffer;
        unsigned char *state = states[side];
        /* As the card dialog did: only write back over the same duelist. */
        if (slot < 0 || !record || SaveSlots_ReadState(slot, state, check) || state_code(state) != state_code(record)) {
            fprintf(stderr, "memories-pc: could not write the trade back to save slot %d\n", slot + 1);
            return 2;
        }
        if (side == 0) memcpy(first, state, sizeof(first));
        memcpy(state, record, (size_t)menu.size);
    }
    if (SaveSlots_WriteState(menu.pair_slot[0], states[0])) return 2;
    if (SaveSlots_WriteState(menu.pair_slot[1], states[1])) {
        /* Neither or both: the game drops the trade on a failure, so player
         * 1's save goes back to what it was. */
        if (SaveSlots_WriteState(menu.pair_slot[0], first))
            fprintf(stderr, "memories-pc: could not undo the trade in save slot %d\n", menu.pair_slot[0] + 1);
        return 2;
    }
    return 1;
}

static void start(int channel)
{
    char imported[sizeof(menu.message)];
    int slot, any = 0;
    menu.started = 1;
    menu.side = channel >> 4 ? 1 : 0;
    if (menu.step == SAVE_MENU_LOAD_PAIR && menu.side == 0) menu.pair_slot[0] = menu.pair_slot[1] = -1;
    /* Memory card files put in the saves folder (an emulator's, say)
     * become slots first, so the list shows them. */
    SaveSlots_ImportFolder(check, imported, sizeof(imported));
    SaveSlots_Scan(menu.slots, check);
    for (slot = 0; slot < SAVE_SLOT_COUNT; slot++) any |= selectable(slot);
    menu.cursor = first_cursor();
    menu.top = 0;
    keep_cursor_shown();
    menu.view = VIEW_LIST;
    changed();
    if (imported[0]) {
        show_message(any ? BACK_TO_LIST : 2, 1, "", 0);
        snprintf(menu.message, sizeof(menu.message), "%s", imported);
    } else if (!any && *SaveSlots_ReadError()) {
        /* Not "no saved games" when they could not be read: they may all be
         * there, behind a folder the system refuses. */
        show_message(2, 1, PT("Could not read your saved games.", "Nao deu para ler os saves."), 0);
        snprintf(detail, sizeof(detail), "%s", SaveSlots_ReadError());
    } else if (!any) {
        show_message(2, 1, PT("There are no saved games to load.", "Nenhum jogo salvo."), 0);
    }
}

/* Games saved and loaded through the menu (SaveMenu_SaveCount). */
static unsigned saves_done, loads_done;
unsigned SaveMenu_SaveCount(void) { return saves_done; }
unsigned SaveMenu_LoadCount(void) { return loads_done; }

static int load(int slot)
{
    if (menu.size != SAVE_SLOT_STATE_SIZE || SaveSlots_ReadState(slot, menu.buffer, check)) {
        show_message(BACK_TO_LIST, 1, PT("Slot %d could not be read.", "Slot %02d ilegivel."), slot);
        return 0;
    }
    if (menu.step == SAVE_MENU_LOAD_PAIR) {
        menu.pair_slot[menu.side] = slot;
    } else {
        menu.current_slot = slot;
        loads_done++;
    }
    return close_with(1);
}

static void save(int slot)
{
    /* The dialog's buffer is the state; the header the game built is the
     * 0x200 bytes before it (SaveData_RequestWrite). */
    if (SaveSlots_WriteFile(slot, menu.buffer - SAVE_SLOT_HEADER_SIZE, SAVE_SLOT_HEADER_SIZE + (size_t)menu.size)) {
        /* Where and why under it: another slot does not help when the
         * folder refuses every file. */
        const char *why = SaveSlots_LastError();
        show_message(BACK_TO_LIST, 1, *why ? PT("Could not save to slot %d.", "Erro ao salvar no slot %02d.") : PT("Could not save to slot %d. Try another slot.", "Erro ao salvar no slot %02d."),
                     slot);
        snprintf(detail, sizeof(detail), "%s", why);
        return;
    }
    menu.current_slot = slot;
    saves_done++;
    SaveSlots_Scan(menu.slots, check);
    show_message(1, 0, PT("Saved to slot %d.", "Salvo no slot %02d."), slot);
}

int SaveMenu_Begin(int step, unsigned char *buffer, unsigned char *second, int size, const char *name,
                   SaveSlotCheck validity)
{
    if (step != SAVE_MENU_LOAD && step != SAVE_MENU_LOAD_PAIR && step != SAVE_MENU_SAVE &&
        step != SAVE_MENU_WRITE_PAIR) {
        return 0;
    }
    SaveSlots_ImportMemoryCards(name);
    check = validity;
    menu.step = step;
    menu.buffer = buffer;
    menu.second = second;
    menu.size = size;
    menu.started = 0;
    menu.choice = CHOICE_KEEP;
    menu.view = VIEW_LIST; /* open; the first poll reads the slots */
    hit.valid = 0;
    changed();
    return 1;
}

int SaveMenu_Active(void) { return menu.view != VIEW_CLOSED; }
int SaveMenu_CurrentSlot(void) { return menu.current_slot; }
int SaveMenu_PairSlot(int side) { return side == 0 || side == 1 ? menu.pair_slot[side] : -1; }

int SaveMenu_Poll(unsigned pressed, int channel, int *sound, SaveSlotCheck validity)
{
    int slot;
    check = validity;
    *sound = SAVE_MENU_SOUND_NONE;
    if (menu.view == VIEW_CLOSED) return 2;
    if (!menu.started) {
        if (menu.step == SAVE_MENU_WRITE_PAIR) {
            menu.started = 1;
            return close_with(write_pair());
        }
        start(channel);
        return 0;
    }
    switch (menu.view) {
    case VIEW_MESSAGE:
        menu.message_frames++;
        if (!(pressed & (SAVE_MENU_PAD_CONFIRM | SAVE_MENU_PAD_CANCEL | SAVE_MENU_PAD_START)) &&
            (menu.message_waits || menu.message_frames < MESSAGE_FRAMES)) {
            return 0;
        }
        if (pressed & (SAVE_MENU_PAD_CONFIRM | SAVE_MENU_PAD_CANCEL | SAVE_MENU_PAD_START)) {
            *sound = SAVE_MENU_SOUND_CONFIRM;
        }
        if (menu.after_message != BACK_TO_LIST) return close_with(menu.after_message);
        menu.view = VIEW_LIST;
        changed();
        return 0;
    case VIEW_CONFIRM:
        if (pressed & (SAVE_MENU_PAD_LEFT | SAVE_MENU_PAD_RIGHT)) {
            menu.choice ^= 1;
            *sound = SAVE_MENU_SOUND_MOVE;
            changed();
        } else if (pressed & SAVE_MENU_PAD_CANCEL) {
            *sound = SAVE_MENU_SOUND_CANCEL;
            menu.view = VIEW_LIST;
            changed();
        } else if (pressed & (SAVE_MENU_PAD_CONFIRM | SAVE_MENU_PAD_START)) {
            if (menu.choice == CHOICE_OVERWRITE) {
                *sound = SAVE_MENU_SOUND_CONFIRM;
                save(menu.cursor);
            } else {
                *sound = SAVE_MENU_SOUND_CANCEL;
                menu.view = VIEW_LIST;
                changed();
            }
        }
        return 0;
    default:
        break;
    }
    if (pressed & (SAVE_MENU_PAD_UP | SAVE_MENU_PAD_DOWN)) {
        menu.cursor = (menu.cursor + (pressed & SAVE_MENU_PAD_UP ? SAVE_SLOT_COUNT - 1 : 1)) % SAVE_SLOT_COUNT;
        keep_cursor_shown();
        *sound = SAVE_MENU_SOUND_MOVE;
        changed();
        return 0;
    }
    if (pressed & SAVE_MENU_PAD_CANCEL) {
        *sound = SAVE_MENU_SOUND_CANCEL;
        return close_with(3);
    }
    if (!(pressed & (SAVE_MENU_PAD_CONFIRM | SAVE_MENU_PAD_START))) return 0;
    slot = menu.cursor;
    /* The files, not the list as it was drawn: a save state may have brought
     * the list back from before a slot was written. */
    SaveSlots_Scan(menu.slots, check);
    changed();
    if (!selectable(slot)) {
        *sound = SAVE_MENU_SOUND_BUZZER;
        return 0;
    }
    *sound = SAVE_MENU_SOUND_CONFIRM;
    if (menu.step != SAVE_MENU_SAVE) return load(slot);
    if (menu.slots[slot].status == SAVE_SLOT_EMPTY) {
        save(slot);
        return 0;
    }
    /* Overwriting the game you are playing is the usual case; another
     * duelist's save, an older or newer point of this one, or a damaged
     * save is kept unless the player moves to Overwrite. */
    menu.choice = same_game(&menu.slots[slot]) ? CHOICE_OVERWRITE : CHOICE_KEEP;
    menu.view = VIEW_CONFIRM;
    changed();
    return 0;
}

/* The mouse over the menu: the row it is on is the cursor once it moves
 * there, and a click is Cross on that row (or on the Overwrite or Cancel it
 * is over, or on a message). Returns the pad bits that stand for it, to add
 * to what SaveMenu_Poll is given; *sound is the move sound for a hover that
 * changed the cursor or the choice. Right click and the wheel are the pad's
 * own Circle, Up and Down. */
unsigned SaveMenu_Pointer(int x, int y, int moved, int clicked, int *sound)
{
    int i;
    *sound = SAVE_MENU_SOUND_NONE;
    if (menu.view == VIEW_CLOSED || !menu.started) return 0;
    if (menu.view == VIEW_MESSAGE) return clicked ? SAVE_MENU_PAD_CONFIRM : 0;
    if (!hit.valid || hit.view != menu.view || hit.top != menu.top) return 0;
    if (menu.view == VIEW_CONFIRM) {
        for (i = 0; i < 2; i++) {
            const int *b = hit.button[i];
            if (x < b[0] || x >= b[0] + b[2] || y < b[1] || y >= b[1] + b[3]) continue;
            if ((moved || clicked) && menu.choice != i) {
                menu.choice = i;
                *sound = SAVE_MENU_SOUND_MOVE;
                changed();
            }
            return clicked ? SAVE_MENU_PAD_CONFIRM : 0;
        }
        return 0;
    }
    if (x < hit.x || x >= hit.x + hit.w || y < hit.y || y >= hit.y + hit.rows * hit.row_h) return 0;
    i = menu.top + (y - hit.y) / hit.row_h;
    if (i >= SAVE_SLOT_COUNT) return 0;
    if ((moved || clicked) && i != menu.cursor) {
        menu.cursor = i;
        *sound = SAVE_MENU_SOUND_MOVE;
        changed();
    }
    return clicked ? SAVE_MENU_PAD_CONFIRM : 0;
}

unsigned SaveMenu_Signature(void)
{
    return menu.view == VIEW_CLOSED ? 0 : menu.changes * 8u + (unsigned)menu.view * 2u + 1u;
}

/* Drawing. */

#define COLOR_TEXT 0xf2f2f4u
#define COLOR_DIM 0x8a8a92u
#define COLOR_WARN 0xf0a070u
#define COLOR_TITLE 0xffd870u

static uint32_t blend(uint32_t under, uint32_t over, unsigned alpha)
{
    unsigned inverse = 255 - alpha;
    unsigned r = ((over >> 16 & 255) * alpha + (under >> 16 & 255) * inverse) / 255;
    unsigned g = ((over >> 8 & 255) * alpha + (under >> 8 & 255) * inverse) / 255;
    unsigned b = ((over & 255) * alpha + (under & 255) * inverse) / 255;
    unsigned a = alpha + (under >> 24) * inverse / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

static void fill(MenuCanvas *canvas, int x, int y, int w, int h, uint32_t color, unsigned alpha)
{
    int row, column;
    for (row = y < 0 ? 0 : y; row < y + h && row < canvas->height; row++) {
        for (column = x < 0 ? 0 : x; column < x + w && column < canvas->width; column++) {
            uint32_t *pixel = canvas->pixels + (size_t)row * (size_t)canvas->stride + (size_t)column;
            *pixel = blend(*pixel, color, alpha);
        }
    }
}

static void frame(MenuCanvas *canvas, int x, int y, int w, int h, int s)
{
#ifdef MEMORIES_PS2
    /* The game's own windows: deep blue, a gold rim with a light line inside. */
    fill(canvas, x, y, w, h, 0x101c58u, 255);
    fill(canvas, x, y, w, 2 * s, 0xd8a838u, 255);
    fill(canvas, x, y + h - 2 * s, w, 2 * s, 0xd8a838u, 255);
    fill(canvas, x, y, 2 * s, h, 0xd8a838u, 255);
    fill(canvas, x + w - 2 * s, y, 2 * s, h, 0xd8a838u, 255);
    fill(canvas, x + 3 * s, y + 3 * s, w - 6 * s, s, 0xe8e8f0u, 255);
    fill(canvas, x + 3 * s, y + h - 4 * s, w - 6 * s, s, 0xe8e8f0u, 255);
    fill(canvas, x + 3 * s, y + 3 * s, s, h - 6 * s, 0xe8e8f0u, 255);
    fill(canvas, x + w - 4 * s, y + 3 * s, s, h - 6 * s, 0xe8e8f0u, 255);
    return;
#endif
    fill(canvas, x, y, w, h, 0x0c0e18u, 232);
    fill(canvas, x, y, w, s, 0x6078c0u, 255);
    fill(canvas, x, y + h - s, w, s, 0x6078c0u, 255);
    fill(canvas, x, y, s, h, 0x6078c0u, 255);
    fill(canvas, x + w - s, y, s, h, 0x6078c0u, 255);
}

/* The menu's own scale: at least the menu bar's, and big enough to read over
 * the game picture in a large window. */
static int ui_scale;

static void draw_text(MenuCanvas *canvas, int x, int y, const char *text, uint32_t color)
{
    Menu_DrawTextScaled(canvas, x, y, text, color, ui_scale);
}

static int text_width(const char *text) { return Menu_TextWidthScaled(text, ui_scale); }

static void centred(MenuCanvas *canvas, int x, int w, int y, const char *text, uint32_t color)
{
    draw_text(canvas, x + (w - text_width(text)) / 2, y, text, color);
}

/* `text` word-wrapped to `w`, each line centred, `line_h` apart from `y`;
 * a word wider than a line (a long folder name) breaks inside. Without a
 * canvas it only counts. Returns the number of lines. */
static int wrapped(MenuCanvas *canvas, int x, int w, int y, int line_h, const char *text, uint32_t color)
{
    char line[256];
    int lines = 0;
    while (*text) {
        size_t n = 0, fit = 0;
        while (text[n]) {
            size_t next = n + 1;
            while (((unsigned char)text[next] & 0xC0) == 0x80) next++; /* whole UTF-8 characters */
            if (next >= sizeof(line)) break;
            memcpy(line, text, next);
            line[next] = '\0';
            if (text_width(line) > w) break;
            n = next;
            if (text[n] == ' ' || !text[n]) fit = n;
        }
        if (!fit) fit = n;
        if (!fit) /* not even one character fits: one a line, whole */
            for (fit = 1; ((unsigned char)text[fit] & 0xC0) == 0x80; fit++) {}
        memcpy(line, text, fit);
        line[fit] = '\0';
        if (canvas) centred(canvas, x, w, y + lines * line_h, line, color);
        lines++;
        text += fit;
        while (*text == ' ') text++;
    }
    return lines;
}

static void describe(const SaveSlotInfo *info, char *out, size_t size)
{
    char when[32] = "";
    time_t seconds = (time_t)info->saved_at;
    struct tm *local = info->saved_at ? localtime(&seconds) : NULL;
    if (local) strftime(when, sizeof(when), "%Y-%m-%d %H:%M", local);
    snprintf(out, size, "%u starchips   %d cards   W%d L%d   %s", info->starchips, info->cards, info->wins,
             info->losses, when);
}

static const char *title(void)
{
    if (menu.step == SAVE_MENU_SAVE) return "Save game";
    if (menu.step == SAVE_MENU_LOAD_PAIR) return menu.side ? "Player 2: choose a save" : "Player 1: choose a save";
    return "Load game";
}

/* The details and the hints on as few lines of `w` as hold them (flow_text.h). */
static int flowed(MenuCanvas *canvas, int x, int w, int y, int line_h, const char *text, uint32_t color,
                  int *widest)
{
    return FlowText(canvas, x, w, y, line_h, text, color, widest, text_width, draw_text);
}

/* A slot's row: its number and name into `line`, and what goes beside it
 * (the details of a save, or the slot's state). */
static const char *slot_text(int slot, char *line, size_t size, char *details, size_t details_size)
{
    const SaveSlotInfo *info = &menu.slots[slot];
    if (info->status == SAVE_SLOT_USED) {
        snprintf(line, size, "%2d   %s%s%s", slot + 1, info->name[0] ? info->name : "(no name)",
                 slot == menu.current_slot ? "  (current)" : "",
                 menu.step == SAVE_MENU_LOAD_PAIR && menu.side == 1 && slot == menu.pair_slot[0] ? "  (player 1)"
                                                                                                 : "");
        describe(info, details, details_size);
        return details;
    }
    snprintf(line, size, "%2d", slot + 1);
    return info->status == SAVE_SLOT_EMPTY ? "Empty" : info->status == SAVE_SLOT_UNREADABLE ? "Cannot read" : "Damaged save";
}

/* The menu reads the game's pad state, so View > Japanese buttons
 * (button_layout.h) exchanges its buttons too. */
static const char *hints(void)
{
    if (Settings_Get(SET_JP_BUTTONS))
        return menu.step == SAVE_MENU_SAVE ? "Circle: save here    Cross: back" : "Circle: load    Cross: back";
    return menu.step == SAVE_MENU_SAVE ? "Cross: save here    Circle: back" : "Cross: load    Circle: back";
}

/* How the list is set at scale `s` in a box `pw` wide: 0 each slot on one
 * row (the layout a desktop window has room for), 1 the details under the
 * name and the hints wrapped (a narrow window, or the middle of a phone
 * between the touch controls), -1 not even that. */
static int list_layout(int pw, int s)
{
    char line[160], details[128];
    int slot, one_row = 1, piece;
    ui_scale = s;
    if (text_width(title()) + 44 * s > pw || text_width(hints()) + 28 * s > pw) one_row = 0;
    for (slot = 0; slot < SAVE_SLOT_COUNT && one_row; slot++) {
        const char *right = slot_text(slot, line, sizeof(line), details, sizeof(details));
        if (32 * s + text_width(line) + 24 * s + text_width(right) > pw) one_row = 0;
    }
    if (one_row) return 0;
    if (text_width(title()) + 44 * s > pw) return -1;
    flowed(NULL, 0, pw, 0, 0, hints(), 0, &piece);
    if (piece + 28 * s > pw) return -1;
    for (slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        const char *right = slot_text(slot, line, sizeof(line), details, sizeof(details));
        flowed(NULL, 0, pw, 0, 0, right, 0, &piece);
        if (32 * s + text_width(line) + (menu.slots[slot].status == SAVE_SLOT_USED ? 0 : 24 * s + piece) > pw ||
            48 * s + piece > pw)
            return -1;
    }
    return 1;
}

static void draw_confirm(MenuCanvas *canvas, int px, int pw, int cy, int s)
{
    const SaveSlotInfo *info = &menu.slots[menu.cursor];
    char line[160], details[128], first[64];
    int w = pw - 48 * s, h = 132 * s, x = px + 24 * s, y = cy - h / 2, bx, bw = 120 * s;
    const char *labels[2] = {"Overwrite", "Cancel"}, *warning = NULL;
    int i;
    snprintf(first, sizeof(first), "Slot %d already has a save.", menu.cursor + 1);
    if (info->status == SAVE_SLOT_USED) {
        describe(info, details, sizeof(details));
        snprintf(line, sizeof(line), "%s   %s", info->name[0] ? info->name : "(no name)", details);
        if (info->duelist_code != state_code(menu.buffer)) {
            warning = "It belongs to a different duelist.";
        } else if (!same_game(info)) {
            warning = info->sequence + 1 > state_sequence(menu.buffer) ? "It is further along than the game you are saving."
                                                                       : "It is an earlier save of this game.";
        }
    } else {
        line[0] = '\0';
        warning = "The save in it is damaged.";
    }
    if (2 * bw + 16 * s > w || text_width(first) > w || text_width(line) > w || (warning && text_width(warning) > w)) {
        /* Narrow: the box takes the menu's width, the text wraps and the
         * buttons share what is left. */
        int line_h = 20 * s, inner, n, top;
        x = px + 8 * s;
        w = pw - 16 * s;
        inner = w - 24 * s;
        n = wrapped(NULL, x + 12 * s, inner, 0, line_h, first, COLOR_TEXT) +
            (line[0] ? wrapped(NULL, x + 12 * s, inner, 0, line_h, line, COLOR_DIM) : 0) +
            (warning ? wrapped(NULL, x + 12 * s, inner, 0, line_h, warning, COLOR_WARN) : 0) +
            wrapped(NULL, x + 12 * s, inner, 0, line_h, "Overwrite it?", COLOR_TEXT);
        h = 18 * s + n * line_h + 8 * s + 24 * s + 12 * s;
        y = cy - h / 2;
        frame(canvas, x, y, w, h, s);
        top = y + 18 * s;
        top += wrapped(canvas, x + 12 * s, inner, top, line_h, first, COLOR_TEXT) * line_h;
        if (line[0]) top += wrapped(canvas, x + 12 * s, inner, top, line_h, line, COLOR_DIM) * line_h;
        if (warning) top += wrapped(canvas, x + 12 * s, inner, top, line_h, warning, COLOR_WARN) * line_h;
        top += wrapped(canvas, x + 12 * s, inner, top, line_h, "Overwrite it?", COLOR_TEXT) * line_h;
        if (2 * bw + 16 * s > inner) bw = (inner - 16 * s) / 2;
        bx = x + (w - 2 * bw - 16 * s) / 2;
        for (i = 0; i < 2; i++) {
            int chosen = i == menu.choice, left = bx + i * (bw + 16 * s);
            fill(canvas, left, top - line_h / 2 + 8 * s, bw, 24 * s, chosen ? 0x3a5aa8u : 0x22263au, 255);
            hit.button[i][0] = left, hit.button[i][1] = top - line_h / 2 + 8 * s, hit.button[i][2] = bw, hit.button[i][3] = 24 * s;
            centred(canvas, left, bw, top - line_h / 2 + 20 * s, labels[i], chosen ? COLOR_TEXT : COLOR_DIM);
        }
        return;
    }
    frame(canvas, x, y, w, h, s);
    centred(canvas, x, w, y + 18 * s, first, COLOR_TEXT);
    if (line[0]) centred(canvas, x, w, y + 38 * s, line, COLOR_DIM);
    if (warning) centred(canvas, x, w, y + (line[0] ? 58 : 38) * s, warning, COLOR_WARN);
    centred(canvas, x, w, y + 80 * s, "Overwrite it?", COLOR_TEXT);
    bx = x + (w - 2 * bw - 16 * s) / 2;
    for (i = 0; i < 2; i++) {
        int chosen = i == menu.choice;
        int left = bx + i * (bw + 16 * s);
        fill(canvas, left, y + 96 * s, bw, 24 * s, chosen ? 0x3a5aa8u : 0x22263au, 255);
        hit.button[i][0] = left, hit.button[i][1] = y + 96 * s, hit.button[i][2] = bw, hit.button[i][3] = 24 * s;
        centred(canvas, left, bw, y + 108 * s, labels[i], chosen ? COLOR_TEXT : COLOR_DIM);
    }
}

void SaveMenu_Draw(MenuCanvas *canvas, int *x, int *y, int *w, int *h)
{
    int s, row_h, rows, pw, ph, px, py, top, i, list_y, left, right, bar, layout, line_h = 0, hint_lines = 1;
    char line[160], details[128];
    *x = *y = *w = *h = 0;
    if (menu.view == VIEW_CLOSED || !menu.started || !canvas || !canvas->pixels) return;
    /* The free width: the window's, or between the touch controls while
     * they show (they play this menu). Each slot on one row when that is
     * wide enough; else the details under the name; else smaller text. */
    Menu_OverlayArea(canvas, &left, &right, &bar);
    s = canvas->height / 420 > Menu_Scale() ? canvas->height / 420 : Menu_Scale();
    for (;; s--) {
        pw = right - left - 16 * s < 600 * s ? right - left - 16 * s : 600 * s;
        layout = list_layout(pw, s);
        if (layout >= 0 || s == 1) break;
    }
    ui_scale = s;
    row_h = 22 * s;
    if (layout > 0) {
        /* Every row as tall as the longest details need. */
        int most = 0;
        line_h = 18 * s;
        for (i = 0; i < SAVE_SLOT_COUNT; i++) {
            if (menu.slots[i].status != SAVE_SLOT_USED) continue;
            slot_text(i, line, sizeof(line), details, sizeof(details));
            top = flowed(NULL, 0, pw - 48 * s, 0, 0, details, 0, NULL);
            most = top > most ? top : most;
        }
        row_h = (1 + most) * line_h + 6 * s;
        hint_lines = flowed(NULL, 0, pw - 28 * s, 0, 0, hints(), 0, NULL);
    }
    rows = (canvas->height - bar - 96 * s - (hint_lines - 1) * line_h) / row_h;
    rows = rows < (layout > 0 ? 1 : 3) ? (layout > 0 ? 1 : 3) : rows > SAVE_SLOT_COUNT ? SAVE_SLOT_COUNT : rows;
    if (rows != shown_rows) {
        shown_rows = rows;
        keep_cursor_shown();
    }
    ph = 44 * s + rows * row_h + 34 * s + (hint_lines - 1) * line_h;
    px = left + (right - left - pw) / 2;
    py = bar + (canvas->height - bar - ph) / 2;
    frame(canvas, px, py, pw, ph, s);
    draw_text(canvas, px + 14 * s, py + 20 * s, title(), COLOR_TITLE);
    list_y = py + 38 * s;
    top = menu.top;
    hit.valid = 1;
    hit.view = menu.view;
    hit.top = menu.top;
    hit.x = px + 6 * s, hit.y = list_y, hit.w = pw - 12 * s, hit.row_h = row_h, hit.rows = rows;
    for (i = 0; i < rows && top + i < SAVE_SLOT_COUNT; i++) {
        int slot = top + i, ry = list_y + i * row_h, cy = ry + row_h / 2;
        const SaveSlotInfo *info = &menu.slots[slot];
        uint32_t color = selectable(slot) ? COLOR_TEXT : COLOR_DIM;
        uint32_t right_color =
            info->status == SAVE_SLOT_DAMAGED || info->status == SAVE_SLOT_UNREADABLE ? COLOR_WARN : color;
        const char *beside = slot_text(slot, line, sizeof(line), details, sizeof(details));
#ifdef MEMORIES_PS2
        if (slot == menu.cursor) fill(canvas, px + 6 * s, ry, pw - 12 * s, row_h - 2 * s, 0x3858c8u, 255);
#else
        if (slot == menu.cursor) fill(canvas, px + 6 * s, ry, pw - 12 * s, row_h - 2 * s, 0x3a5aa8u, 200);
#endif
        if (layout > 0) {
            int first = ry + 3 * s + line_h / 2;
            draw_text(canvas, px + 16 * s, first, line, color);
            if (info->status == SAVE_SLOT_USED)
                flowed(canvas, px + 32 * s, pw - 48 * s, first + line_h, line_h, beside, COLOR_DIM, NULL);
            else
                draw_text(canvas, px + pw - 16 * s - text_width(beside), first, beside, right_color);
            continue;
        }
        draw_text(canvas, px + 16 * s, cy, line, color);
        draw_text(canvas, px + pw - 16 * s - text_width(beside), cy, beside, right_color);
    }
    if (menu.top > 0) draw_text(canvas, px + pw - 30 * s, py + 20 * s, "^", COLOR_DIM);
    if (menu.top + rows < SAVE_SLOT_COUNT) draw_text(canvas, px + pw - 18 * s, py + 20 * s, "v", COLOR_DIM);
    if (layout > 0)
        flowed(canvas, px + 14 * s, pw - 28 * s, py + ph - 16 * s - (hint_lines - 1) * line_h, line_h, hints(),
               COLOR_DIM, NULL);
    else
        draw_text(canvas, px + 14 * s, py + ph - 16 * s, hints(), COLOR_DIM);
    if (menu.view == VIEW_CONFIRM) {
        draw_confirm(canvas, px, pw, py + ph / 2, s);
    } else if (menu.view == VIEW_MESSAGE && detail[0]) {
        /* A failed save: the message, then where and why, wrapped. */
        int mw = pw - 32 * s, mx = px + 16 * s, line_h = 18 * s;
        int head = text_width(menu.message) > mw - 16 * s
                       ? wrapped(NULL, mx + 12 * s, mw - 24 * s, 0, line_h, menu.message, COLOR_TEXT) - 1 : 0;
        int lines = wrapped(NULL, mx + 12 * s, mw - 24 * s, 0, line_h, detail, COLOR_DIM);
        int mh = 64 * s + (head + lines) * line_h, my = py + (ph - mh) / 2;
        frame(canvas, mx, my, mw, mh, s);
        if (head) wrapped(canvas, mx + 12 * s, mw - 24 * s, my + 24 * s, line_h, menu.message, COLOR_TEXT);
        else centred(canvas, mx, mw, my + 24 * s, menu.message, COLOR_TEXT);
        wrapped(canvas, mx + 12 * s, mw - 24 * s, my + 44 * s + head * line_h, line_h, detail, COLOR_WARN);
        if (menu.message_waits)
            centred(canvas, mx, mw, my + mh - 18 * s, Settings_Get(SET_JP_BUTTONS) ? "Press Circle" : "Press Cross",
                    COLOR_DIM);
    } else if (menu.view == VIEW_MESSAGE) {
        int mw = pw - 96 * s, mx = px + 48 * s, line_h = 18 * s, more = 0, mh, my;
        if (text_width(menu.message) > mw - 16 * s) { /* narrow: the menu's width, wrapped */
            mw = pw - 32 * s;
            mx = px + 16 * s;
            more = wrapped(NULL, mx + 12 * s, mw - 24 * s, 0, line_h, menu.message, COLOR_TEXT) - 1;
        }
        mh = 64 * s + more * line_h;
        my = py + (ph - mh) / 2;
        frame(canvas, mx, my, mw, mh, s);
        if (more) wrapped(canvas, mx + 12 * s, mw - 24 * s, my + 24 * s, line_h, menu.message, COLOR_TEXT);
        else centred(canvas, mx, mw, my + 24 * s, menu.message, COLOR_TEXT);
        if (menu.message_waits)
            centred(canvas, mx, mw, my + 46 * s + more * line_h,
                    Settings_Get(SET_JP_BUTTONS) ? "Press Circle" : "Press Cross", COLOR_DIM);
    }
    *x = px;
    *y = py;
    *w = pw;
    *h = ph;
}

void SaveMenu_State(MemoriesState *state)
{
    MemoriesStateField fields[] = {{&menu, sizeof(menu)}};
    int loaded = Memories_StateChunk(state, "save-menu", fields, 1);
    if (loaded) detail[0] = '\0'; /* it went with the message shown before */
    if (loaded && menu.view == VIEW_CONFIRM) {
        /* The slot may have changed since this prompt was saved. Return
         * to the list so the next pick reads disk and asks afresh, using
         * the validity callback supplied by the current caller. */
        menu.view = VIEW_LIST;
        menu.choice = CHOICE_KEEP;
        changed();
    }
}

#ifdef MEMORIES_PS2
/* The PS2's save and load window (docs/relatorio-menu-save-load.md): drawn
 * on a 640 x 480 canvas that platform_ps2.c halves to the 320 x 240 screen.
 * In screen pixels: 184 x 110 at (68, 65), a gold rim with stepped corners
 * round an indigo panel, an eye before the title, three slot rows with a
 * triangle on the chosen one, scroll marks, and the buttons' legend. */
#define PS_GOLD 0xd8a838u
#define PS_GOLD_DARK 0x6a4a10u
#define PS_PANEL 0x141a4au
#define PS_ROW 0x2a2c5au
#define PS_PICK 0x1830c8u
#define PS_IVORY 0xf4ecd8u
#define PS_DIM 0xb8b0a0u
#define PS_WARN 0xf0a070u

static void ps_triangle(MenuCanvas *c, int x, int y, int size, int down, uint32_t color)
{
    int i;
    for (i = 0; i < size; i++) {
        if (down > 0) fill(c, x - (size - i), y + i, 2 * (size - i), 1, color, 255);      /* pointing down */
        else if (down < 0) fill(c, x - i, y + i, 2 * i + 1, 1, color, 255);               /* pointing up */
        else fill(c, x + i, y - (size - i), 1, 2 * (size - i), color, 255);               /* pointing right */
    }
}

static void ps_disc(MenuCanvas *c, int cx, int cy, int r, int hole, uint32_t color)
{
    int i;
    for (i = -r; i <= r; i++) {
        int w = 0, wi = -1, out = r * r - i * i, in = hole * hole - i * i;
        while ((w + 1) * (w + 1) <= out) w++;
        if (hole && in >= 0) { wi = 0; while ((wi + 1) * (wi + 1) <= in) wi++; }
        if (wi < 0) fill(c, cx - w, cy + i, 2 * w + 1, 1, color, 255);
        else {
            fill(c, cx - w, cy + i, w - wi, 1, color, 255);
            fill(c, cx + wi + 1, cy + i, w - wi, 1, color, 255);
        }
    }
}

/* The pad's button: a dark disc with a blue cross or a red circle. */
static void ps_button(MenuCanvas *c, int cx, int cy, int k, int cross)
{
    int i, r = 6 * k;
    ps_disc(c, cx, cy, r, 0, 0x2a2a36u);
    if (cross) {
        for (i = -r / 2; i <= r / 2; i++) {
            fill(c, cx + i - k / 2, cy + i, k + 1, 1, 0x9ab4ffu, 255);
            fill(c, cx - i - k / 2, cy + i, k + 1, 1, 0x9ab4ffu, 255);
        }
    } else {
        ps_disc(c, cx, cy, r * 2 / 3, r * 2 / 3 - k - 1, 0xf04858u);
    }
}

/* An eye of Horus, 10 x 9 screen pixels: brow, almond, pupil, tail. */
static void ps_eye(MenuCanvas *c, int x, int y, int k)
{
    fill(c, x, y, 10 * k, k, PS_GOLD, 255);
    fill(c, x + k, y + 2 * k, 8 * k, k, PS_GOLD, 255);
    fill(c, x, y + 3 * k, k, k, PS_GOLD, 255);
    fill(c, x + 9 * k, y + 3 * k, k, k, PS_GOLD, 255);
    fill(c, x + k, y + 4 * k, 8 * k, k, PS_GOLD, 255);
    fill(c, x + 4 * k, y + 2 * k, 2 * k, 3 * k, PS_GOLD, 255);
    fill(c, x + 3 * k, y + 5 * k, k, 3 * k, PS_GOLD, 255);
    fill(c, x + 4 * k, y + 8 * k, 3 * k, k, PS_GOLD, 255);
}

/* Gold rim, a dark line inside it, a thin gold line, stepped corners. */
static void ps_frame(MenuCanvas *c, int x, int y, int w, int h, int k)
{
    int n;
    fill(c, x, y, w, h, PS_PANEL, 255);
    fill(c, x, y, w, k, PS_GOLD, 255);
    fill(c, x, y + h - k, w, k, PS_GOLD, 255);
    fill(c, x, y, k, h, PS_GOLD, 255);
    fill(c, x + w - k, y, k, h, PS_GOLD, 255);
    fill(c, x + k, y + k, w - 2 * k, k, PS_GOLD_DARK, 255);
    fill(c, x + k, y + k, k, h - 2 * k, PS_GOLD_DARK, 255);
    fill(c, x + 2 * k, y + 2 * k, w - 4 * k, 1, PS_GOLD, 255);
    fill(c, x + 2 * k, y + h - 2 * k - 1, w - 4 * k, 1, PS_GOLD, 255);
    fill(c, x + 2 * k, y + 2 * k, 1, h - 4 * k, PS_GOLD, 255);
    fill(c, x + w - 2 * k - 1, y + 2 * k, 1, h - 4 * k, PS_GOLD, 255);
    for (n = 0; n < 4; n++) {
        int right = n & 1, bottom = n & 2;
        int cx = right ? x + w - 7 * k : x + 3 * k, cy = bottom ? y + h - 7 * k : y + 3 * k;
        fill(c, cx, bottom ? cy + 3 * k : cy, 4 * k, k, PS_GOLD, 255);
        fill(c, right ? cx + 3 * k : cx, cy, k, 4 * k, PS_GOLD, 255);
        fill(c, right ? cx + k : cx + 2 * k, bottom ? cy + k : cy + 2 * k, k, k, PS_GOLD, 255);
    }
}

/* A name cut to `w` pixels at the current scale, ending ".." when cut. */
static void ps_fit(char *text, int w)
{
    size_t n = strlen(text);
    char work[80];
    if (text_width(text) <= w) return;
    while (n > 1) {
        n--;
        snprintf(work, sizeof(work), "%.*s..", (int)n, text);
        if (text_width(work) <= w) {
            strcpy(text, work);
            return;
        }
    }
}

void SaveMenu_DrawPs2(MenuCanvas *canvas, int *x, int *y, int *w, int *h)
{
    /* In screen pixels (k canvas pixels each): the window 192 x 110 at
     * (64, 65), whole GS qwords across; title 22, three 16-pixel rows, the
     * scroll mark 10, the legend 22. */
    const int k = canvas && canvas->width >= 320 ? canvas->width / 320 : 1;
    const int px = 64 * k, py = 65 * k, pw = 192 * k, ph = 110 * k, rows = 3, row_h = 16 * k;
    const int list_y = py + 26 * k;
    int i, swap = Settings_Get(SET_JP_BUTTONS);
    const char *heading = menu.step == SAVE_MENU_SAVE ? "SALVAR JOGO"
                          : menu.step == SAVE_MENU_LOAD_PAIR ? (menu.side ? "JOGADOR 2" : "JOGADOR 1")
                                                             : "CARREGAR JOGO";
    *x = *y = *w = *h = 0;
    if (menu.view == VIEW_CLOSED || !menu.started || !canvas || !canvas->pixels) return;
    if (shown_rows != rows) {
        shown_rows = rows;
        keep_cursor_shown();
    }
    ps_frame(canvas, px, py, pw, ph, k);
    ui_scale = k >= 3 ? 2 : 1; /* the text about 11 screen pixels */
    {
        int tw = text_width(heading), tx = px + (pw - tw + 14 * k) / 2;
        ps_eye(canvas, tx - 14 * k, py + 8 * k, k);
        draw_text(canvas, tx, py + 13 * k, heading, PS_IVORY);
    }
    if (menu.view == VIEW_LIST) {
        int nx = px + 22 * k, mx = nx + text_width("00") + 8 * k;
        for (i = 0; i < rows && menu.top + i < SAVE_SLOT_COUNT; i++) {
            int slot = menu.top + i, ry = list_y + i * row_h, cy = ry + row_h / 2, picked = slot == menu.cursor;
            const SaveSlotInfo *info = &menu.slots[slot];
            char number[8], name[64];
            uint32_t color = picked ? PS_IVORY : selectable(slot) ? PS_DIM : 0x7a7480u;
            fill(canvas, px + 10 * k, ry + k, pw - 20 * k, row_h - 2 * k, picked ? PS_PICK : PS_ROW, 255);
            if (picked) ps_triangle(canvas, px + 12 * k, cy, 4 * k, 0, PS_GOLD);
            snprintf(number, sizeof(number), "%02d", slot + 1);
            if (info->status == SAVE_SLOT_USED)
                snprintf(name, sizeof(name), "%s", info->name[0] ? info->name : "?");
            else
                snprintf(name, sizeof(name), "%s", info->status == SAVE_SLOT_EMPTY ? "Vazio"
                                                   : info->status == SAVE_SLOT_UNREADABLE ? "Ilegivel" : "Danificado");
            ps_fit(name, px + pw - 12 * k - mx);
            draw_text(canvas, nx, cy, number, color);
            draw_text(canvas, mx, cy, name,
                      info->status == SAVE_SLOT_DAMAGED || info->status == SAVE_SLOT_UNREADABLE ? PS_WARN : color);
        }
        if (menu.top > 0) ps_triangle(canvas, px + pw / 2, list_y - 5 * k, 4 * k, -1, PS_GOLD);
        if (menu.top + rows < SAVE_SLOT_COUNT) ps_triangle(canvas, px + pw / 2, list_y + rows * row_h + 2 * k, 4 * k, 1, PS_GOLD);
    } else if (menu.view == VIEW_CONFIRM) {
        const SaveSlotInfo *info = &menu.slots[menu.cursor];
        char line[80];
        int n;
        snprintf(line, sizeof(line), "%02d %s", menu.cursor + 1,
                 info->status == SAVE_SLOT_USED ? (info->name[0] ? info->name : "?") : "Danificado");
        ps_fit(line, pw - 24 * k);
        centred(canvas, px, pw, list_y + 7 * k, line, PS_DIM);
        centred(canvas, px, pw, list_y + 21 * k, "Sobrescrever?", PS_IVORY);
        for (n = 0; n < 2; n++) {
            int bw = 64 * k, bx = px + pw / 2 + (n ? 6 * k : -6 * k - bw), chosen = (n == 0) == (menu.choice == CHOICE_OVERWRITE);
            fill(canvas, bx, list_y + 30 * k, bw, 14 * k, chosen ? PS_PICK : PS_ROW, 255);
            if (chosen) ps_triangle(canvas, bx + 3 * k, list_y + 37 * k, 3 * k, 0, PS_GOLD);
            centred(canvas, bx, bw, list_y + 37 * k, n == 0 ? "Sim" : "Nao", chosen ? PS_IVORY : PS_DIM);
        }
    } else { /* VIEW_MESSAGE */
        wrapped(canvas, px + 10 * k, pw - 20 * k, list_y + 14 * k, 14 * k, menu.message[0] ? menu.message : "...",
                PS_IVORY);
    }
    {
        /* The legend, by the pad's mapping (View > Japanese buttons swaps them). */
        int ly = py + ph - 12 * k, half = pw / 2;
        const char *act = menu.view == VIEW_CONFIRM ? "OK" : menu.view == VIEW_MESSAGE ? "OK"
                          : menu.step == SAVE_MENU_SAVE ? "Salvar" : "Carregar";
        int a = 16 * k + text_width(act), b = 16 * k + text_width("Voltar");
        int ax = menu.view == VIEW_MESSAGE ? px + (pw - a) / 2 : px + (half - a) / 2 + 4 * k;
        int bx = px + half + (half - b) / 2 - 4 * k;
        ps_button(canvas, ax + 6 * k, ly, k, !swap);
        draw_text(canvas, ax + 16 * k, ly, act, PS_IVORY);
        if (menu.view != VIEW_MESSAGE) {
            ps_button(canvas, bx + 6 * k, ly, k, swap);
            draw_text(canvas, bx + 16 * k, ly, "Voltar", PS_IVORY);
        }
    }
    *x = px, *y = py, *w = pw, *h = ph;
}
#endif

/* A duelist's bust (src/pc/free_duel/busts.c): "bust" as a duelists entry
 * writes it, through the real JSON reader, and the records it is made
 * into, read back the way the game reads a character's parts bank
 * (DisplayEffect_BuildResourceObjects, DisplayObject_UpdateCommandStream,
 * DisplayObject_RenderSpriteSheet): every expression's body drawn from the
 * sheet is its picture, texel for texel; its blink frame and its talking
 * frames drawn over it are those pictures; an expression without them has
 * no eyes or no mouth, so it shows its picture alone; the variants the game
 * may ask for are all there; a bust too big for one record goes on in the
 * next; a disc duelist's "replace_bust" takes over its campaign character.
 * The pictures are the test's own, made by their file names. */
#include "../../src/pc/free_duel/busts.c"
#include <stdarg.h>
#include <assert.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

/* --- what busts.c leans on --------------------------------------------- */

static int notes;
static char said[8192];
void Mods_Note(const char *id, const char *format, ...)
{
    va_list arguments;
    size_t at = strlen(said);
    notes++;
    at += (size_t)snprintf(said + at, sizeof(said) - at, "[%s] ", id);
    va_start(arguments, format);
    if (at < sizeof(said)) at += (size_t)vsnprintf(said + at, sizeof(said) - at, format, arguments);
    va_end(arguments);
    if (at + 1 < sizeof(said)) strcat(said, "\n");
}
static int heard(const char *words) { return strstr(said, words) != NULL; }
static void forget(void) { notes = 0; said[0] = 0; }
int Log_Wanted(LogChannel channel) { (void)channel; return 0; }
void Log_Printf(LogChannel channel, const char *format, ...) { (void)channel; (void)format; }
int Paths_Contained(const char *relative) { return relative && relative[0] != '/' && !strstr(relative, ".."); }

/* The duelists: 41 an added one with bust 0; Teana (2) and Jono 2nd (19)
 * disc ones whose entries say "replace_bust" for busts 1 and 2 -- both
 * the campaign's character 3 and 2... see `owner`. */
static int owner[64];               /* the bust each duelist has, -1 none */
static int replaces[64];
int Duelists_Count(void) { return 42; }
int Duelists_Bust(int duelist) { return duelist >= 0 && duelist < 64 ? owner[duelist] : -1; }
int Duelists_ReplacesBust(int duelist) { return duelist >= 0 && duelist < 64 && replaces[duelist]; }
const char *Duelists_Name(int duelist)
{
    return duelist == 2 ? "Teana" : duelist == 20 ? "Teana 2nd" : duelist == 7 ? "Seto" : duelist == 36 ? "Seto 3rd" :
           "a duelist";
}
/* Teana and Teana 2nd are both the campaign's character 3, Seto and Seto
 * 3rd both 4; Simon Muran (1) is 1; a villager (4) none. */
int Tower_DuelistCharacter(int duelist)
{
    return duelist == 2 || duelist == 20 ? 3 : duelist == 7 || duelist == 36 ? 4 : duelist == 1 ? 1 : 0;
}

/* The pictures, by their names: "<w>x<h>" sizes them (96 x 128 without),
 * and the name says what is drawn -- a figure, its face's color by the
 * digit after "e", the mouth open with "talk", the eyes shut with
 * "blink", "noise" every texel its own. */
static void size_of(const char *path, int *w, int *h)
{
    const char *at = strrchr(path, '/');
    *w = 96;
    *h = 128;
    at = at ? at + 1 : path;
    if (strstr(at, "huge")) { *w = 600; *h = 400; }
    if (strstr(at, "wide")) { *w = 100; *h = 120; }
    if (strstr(at, "full")) { *w = 256; *h = 256; }
    if (strstr(at, "tall")) { *w = 240; *h = 256; }
}
int CardArt_ImageSize(const char *path, int *width, int *height)
{
    if (strstr(path, "missing")) return 0;
    size_of(path, width, height);
    return 1;
}
static void draw(const char *path, int w, int h, unsigned char *out)
{
    const char *e = strstr(path, "/e");
    int face = e ? e[2] - '0' + 1 : 1, x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int dx = x - w / 2, dy = y - h / 3, value = 0;
            if (strstr(path, "noise") || strstr(path, "full") || strstr(path, "tall"))
                value = 1 + (x * 7 + y * 13 + face * 31) % 250;
            if (strstr(path, "spread") && y >= h / 2 && y < h / 2 + 64) value = 1 + (x * 11 + y * 5 + 97) % 250;   /* much changed */
            else if (dx * dx + dy * dy < (w / 5) * (w / 5)) value = 10 + face;               /* the head */
            else if (y > h / 2 && dx > -w / 3 && dx < w / 3) value = 40;                        /* the body */
            if (strstr(path, "talk") && dx > -6 && dx < 6 && dy > 6 && dy < 12) value = strstr(path, "talk2") ? 61 : 60;
            if (strstr(path, "blink") && dy > -6 && dy < -1 && dx > -10 && dx < 10) value = 70;
            out[y * w + x] = (unsigned char)value;
        }
    }
}
static int quantized;
int CardArt_IndexedImages(const char *const *paths, int count, int w, int h, unsigned char *const *indices,
                          unsigned short *clut, char *why, size_t why_size)
{
    int k;
    (void)why; (void)why_size;
    quantized++;
    for (k = 0; k < 256; k++) clut[k] = (unsigned short)(k ? k | 0x400 : 0);
    for (k = 0; k < count; k++) draw(paths[k], w, h, indices[k]);
    return 1;
}

/* --- reading the bank as the game does -------------------------------- */

static int word(const unsigned char *bank, int at) { return bank[at] | bank[at + 1] << 8; }

/* DisplayObject_UpdateCommandStream's walk: expression, piece, variant ->
 * the stream, 0 when an offset on the way is 0. */
static int stream_of(const unsigned char *bank, int expression, int piece, int variant)
{
    int at = word(bank, expression * 2);
    if (!at) return 0;
    at = word(bank, at + piece * 2);
    if (!at) return 0;
    return word(bank, at + variant * 2);
}

/* A frame's parts drawn into `out` (w x h, as the picture), from `sheet`,
 * placed as DisplayObject_RenderSpriteSheet places them on the anchor. */
static void render(const unsigned char *sheet, const unsigned char *bank, int frame, int w, int h, unsigned char *out)
{
    int count = bank[frame], k, x, y;
    CHECK(bank[frame + 1] == 0x10);
    for (k = 0; k < count; k++) {
        const unsigned char *p = bank + frame + 4 + 6 * k;
        int cell = word(p, 2), size = word(p, 4);
        int dx = p[0] | ((cell & 0xC000) >> 6), dy = p[1] | ((size & 0xC000) >> 6);
        int u = (cell & 31) * 8, v = ((cell >> 5) & 31) * 8, pw = ((size >> 5) & 15) * 8 + 8, ph = ((size >> 9) & 15) * 8 + 8;
        if (dx & 0x200) dx -= 0x400;
        if (dy & 0x200) dy -= 0x400;
        CHECK((size & 0x1F) == 0 && ((cell >> 10) & 7) == 0);   /* one page, no palette step, as the disc's */
        CHECK(u + pw <= 256 && v + ph <= 256);
        dx += w / 2;
        dy += h < 0xB2 ? h : 0xB2;
        for (y = 0; y < ph; y++) {
            for (x = 0; x < pw; x++) {
                unsigned char value = sheet[(v + y) * BUST_SHEET_W + u + x];
                CHECK(dx + x >= 0 && dx + x < w && dy + y >= 0 && dy + y < h);
                if (value) out[(dy + y) * w + dx + x] = value;
            }
        }
    }
}

/* The first frame a stream shows, and how long. */
static int first_frame(const unsigned char *bank, int stream, int *frames)
{
    CHECK(stream > 0 && bank[stream] < 0xF0);
    *frames = bank[stream];
    return word(bank, stream + 1);
}

/* The stream stops: a held frame (0) or an 0xFF after its steps. */
static int stops(const unsigned char *bank, int stream)
{
    int k;
    if (bank[stream] == 0) return 1;
    for (k = 0; k < 16; k++, stream += 3) {
        if (bank[stream] == 0xFF) return 1;
        if (bank[stream] >= 0xF0) return 0;
    }
    return 0;
}

static unsigned char shown[3][BUST_SIDE * BUST_SIDE], drawn[BUST_SIDE * BUST_SIDE];
static unsigned char sheet[BUST_SHEET_W * BUST_SHEET_H], bank[BUST_BANK];

static void body_is(int expression, const unsigned char *wanted, int w, int h)
{
    int frames;
    memset(drawn, 0, sizeof(drawn));
    render(sheet, bank, first_frame(bank, stream_of(bank, expression, 0, 0), &frames), w, h, drawn);
    CHECK(frames == 0);
    CHECK(!memcmp(drawn, wanted, (size_t)w * h));
}

/* --- the cases ------------------------------------------------------------- */

static JsonDocument *documents[32];
static int document_count;

static int read_bust_of(const char *mod, const char *text)
{
    char error[256];
    JsonDocument *document = Json_Parse(text, error, sizeof(error));
    if (!document) fprintf(stderr, "%s\n", error);
    CHECK(document && document_count < 32);
    documents[document_count++] = document;
    return Busts_Read(mod, "folder", Json_Root(document), "duelists[0]");
}
static int read_bust(const char *text) { return read_bust_of("m", text); }

static void reading(void)
{
    int bust;
    Busts_Clear();
    forget();
    bust = read_bust("{\"default\": \"angry\", \"expressions\": ["
                     " {\"name\": \"calm\", \"image\": \"busts/e0.png\", \"talk\": [\"busts/e0-talk.png\", \"busts/e0-talk2.png\"],"
                     "  \"blink\": \"busts/e0-blink.png\"},"
                     " {\"name\": \"angry\", \"image\": \"busts/e1.png\", \"talk\": \"busts/e1-talk.png\"},"
                     " {\"name\": \"Sad\", \"image\": \"busts/e2.png\"}]}");
    CHECK(bust == 0 && notes == 0);
    CHECK(Busts_Count(0) == 3 && Busts_Default(0) == 1 && !strcmp(Busts_Name(0, 2), "Sad"));
    CHECK(Busts_Named(0, "sad") == 2 && Busts_Named(0, "ANGRY") == 1 && Busts_Named(0, "glad") == -1);
    /* The codes' numbers: the default, then the list's order. */
    CHECK(Busts_Ordered(0, 0) == 1 && Busts_Ordered(0, 1) == 0 && Busts_Ordered(0, 2) == 2 && Busts_Ordered(0, 3) == 1);
    CHECK(busts[0].expressions[0].talk_count == 2 && busts[0].expressions[0].blink[0]);
    CHECK(busts[0].expressions[1].talk_count == 1 && !busts[0].expressions[1].blink[0]);
    CHECK(!strcmp(busts[0].expressions[2].image, "folder/busts/e2.png"));

    /* One picture: one expression, "default". */
    bust = read_bust("\"busts/e0.png\"");
    CHECK(bust == 1 && Busts_Count(1) == 1 && !strcmp(Busts_Name(1, 0), "default") && notes == 0);

    /* The mistakes, each said, and what is left of the bust. */
    forget();
    bust = read_bust("{\"expressions\": [{\"name\": \"a\", \"image\": \"busts/e0.png\", \"mood\": 1,"
                     "  \"talk\": [\"t1.png\", \"t2.png\", \"t3.png\", \"t4.png\", \"t5.png\"], \"blink\": \"../b.png\"},"
                     " {\"name\": \"A\", \"image\": \"busts/e1.png\"}, {\"image\": \"busts/e1.png\"},"
                     " {\"name\": \"c\", \"image\": \"busts/missing.png\"}, {\"name\": \"d\", \"image\": \"busts/e1.jpg\"},"
                     " {\"name\": \"e\"}, \"busts/e3.png\"], \"style\": 2}");
    CHECK(bust == 2 && Busts_Count(2) == 1 && Busts_Default(2) == 0);
    CHECK(heard("unknown key \"style\"") && heard("unknown key \"mood\""));
    CHECK(heard("\"talk\" has at most 4 frames; the rest are left out"));
    CHECK(heard("../b.png is not a path inside the mod"));
    CHECK(heard("there is an expression \"a\" already; this one is left out"));
    CHECK(heard("an expression has a \"name\""));
    CHECK(heard("busts/missing.png is not a PNG it could read"));
    CHECK(heard("a picture is a PNG of the mod's, a path inside it"));
    CHECK(heard("an expression has an \"image\""));
    CHECK(heard("an expression is {\"name\": ..., \"image\": ...}"));
    CHECK(heard("a bust names its \"default\" expression; \"a\", the first, is taken"));
    CHECK(notes == 11);
    CHECK(busts[2].expressions[0].talk_count == 4 && !busts[2].expressions[0].blink[0]);
    forget();
    CHECK(read_bust("{\"default\": \"x\", \"expressions\": [{\"name\": \"a\", \"image\": \"busts/e0.png\"}]}") == 3);
    CHECK(heard("the bust has no expression \"x\"; \"a\", the first, is its default") && notes == 1);
    forget();
    CHECK(read_bust("{\"expressions\": []}") == -1 && read_bust("3") == -1 && read_bust("{\"default\": \"a\"}") == -1);
    CHECK(heard("the bust has no expression with a picture; the duelist keeps the one it had"));
    CHECK(heard("\"bust\" is {\"default\": ..., \"expressions\": [...]}, or a PNG"));
    CHECK(heard("\"bust\" has a list of \"expressions\""));
    /* No folder to find the pictures in. */
    forget();
    CHECK(Busts_Read("m", NULL, Json_Root(documents[1]), "duelists[0]") == -1 && heard("the mod has no folder"));
    Busts_Clear();
}

/* Pictures of the record maker's own, made by draw(). */
static void made(const char *name, int index, int w, int h)
{
    draw(name, w, h, shown[index]);
}

static void packing(void)
{
    BustFrames frames[BUST_IN_PLACE];
    static unsigned char talk[BUST_SIDE * BUST_SIDE], talk2[BUST_SIDE * BUST_SIDE], blink[BUST_SIDE * BUST_SIDE];
    int w = 96, h = 128, frame, frames_held, e, k;
    made("/e0", 0, w, h);
    made("/e1", 1, w, h);
    made("/e2", 2, w, h);
    draw("/e0-talk", w, h, talk);
    draw("/e0-talk2", w, h, talk2);
    draw("/e0-blink", w, h, blink);
    memset(frames, 0, sizeof(frames));
    frames[0].base = shown[0];
    frames[0].talk[0] = talk;
    frames[0].talk[1] = talk2;
    frames[0].talk_count = 2;
    frames[0].blink = blink;
    frames[1].base = shown[1];
    frames[2].base = shown[2];
    CHECK(Busts_Pack(w, h, frames, 3, sheet, bank));
    /* Four expressions the codes may name (two bits): the fourth is the
     * first's, never an offset of 0 (func_8003AD6C would build nothing). */
    for (e = 0; e < 4; e++) CHECK(word(bank, e * 2) != 0);
    CHECK(word(bank, 6) == word(bank, 0));
    /* Each body is its picture, texel for texel. */
    for (e = 0; e < 3; e++) body_is(e, shown[e], w, h);
    /* The first's eyes: held open (nothing over the body), then shut for
     * a while and stopped, as DisplayEffect_UpdateMenuRecord needs. */
    frame = first_frame(bank, stream_of(bank, 0, 1, 0), &frames_held);
    CHECK(frames_held == 0 && bank[frame] == 0);
    frame = first_frame(bank, stream_of(bank, 0, 1, 1), &frames_held);
    CHECK(frames_held > 0 && stops(bank, stream_of(bank, 0, 1, 1)));
    memcpy(drawn, shown[0], (size_t)w * h);
    render(sheet, bank, frame, w, h, drawn);
    CHECK(!memcmp(drawn, blink, (size_t)w * h));
    /* Its mouth: closed for 0 and past 3, open for 1 and 2, played through
     * and closed again for 3. */
    for (k = 0; k < MOUTH_VARIANTS; k++) {
        int stream = stream_of(bank, 0, 2, k);
        CHECK(stream && stops(bank, stream));
        frame = first_frame(bank, stream, &frames_held);
        memcpy(drawn, shown[0], (size_t)w * h);
        render(sheet, bank, frame, w, h, drawn);
        if (k == 1) CHECK(!memcmp(drawn, talk, (size_t)w * h));
        else if (k == 2) CHECK(!memcmp(drawn, talk2, (size_t)w * h));
        else if (k == 3) CHECK(frames_held > 0 && !memcmp(drawn, talk, (size_t)w * h));
        else CHECK(bank[frame] == 0 && frames_held == 0);
    }
    /* The others have neither: no eyes, no mouth -- their picture alone. */
    CHECK(stream_of(bank, 1, 1, 0) == 0 && stream_of(bank, 1, 2, 0) == 0);
    CHECK(stream_of(bank, 2, 1, 0) == 0 && stream_of(bank, 2, 2, 0) == 0);
    /* The second and third are mostly the first's: only their faces were
     * cut again. */
    {
        int used = 0, x, y;
        for (y = 0; y < CELLS; y++) {
            for (x = 0; x < CELLS; x++) used += packer.used[y][x];
        }
        CHECK(used * 64 < w * h + w * h / 2);
    }

    /* A talking frame the same as its picture is no frame: the mouth is
     * then no piece, and the expression only does not speak. */
    frames[0].talk[0] = shown[0];
    frames[0].talk_count = 1;
    frames[0].blink = shown[0];
    CHECK(Busts_Pack(w, h, frames, 1, sheet, bank));
    CHECK(stream_of(bank, 0, 1, 0) == 0 && stream_of(bank, 0, 2, 0) == 0);
    body_is(0, shown[0], w, h);

    /* A picture of 256 x 256 every texel its own fills the page: one
     * expression fits, a second does not. */
    w = h = 256;
    made("/e0-full", 0, w, h);
    made("/e1-full", 1, w, h);
    memset(frames, 0, sizeof(frames));
    frames[0].base = shown[0];
    frames[1].base = shown[1];
    CHECK(Busts_Pack(w, h, frames, 1, sheet, bank));
    body_is(0, shown[0], w, h);
    CHECK(!Busts_Pack(w, h, frames, 2, sheet, bank));
    /* Sizes the maker does not take. */
    CHECK(!Busts_Pack(100, 128, frames, 1, sheet, bank) && !Busts_Pack(264, 128, frames, 1, sheet, bank));
    CHECK(!Busts_Pack(96, 128, frames, 5, sheet, bank) && !Busts_Pack(96, 128, frames, 0, sheet, bank));
}

static void making(void)
{
    const unsigned short *clut;
    const unsigned char *texels, *made_bank;
    int record, slot, k, first;
    Busts_Clear();
    forget();
    memset(owner, -1, sizeof(owner));
    memset(replaces, 0, sizeof(replaces));
    /* Bust 0, the added duelist's: five expressions, so four in its first
     * record (the default first) and one in a second. */
    CHECK(read_bust("{\"default\": \"e4\", \"expressions\": ["
                    " {\"name\": \"e0\", \"image\": \"b/e0.png\", \"talk\": \"b/e0-talk.png\", \"blink\": \"b/e0-blink.png\"},"
                    " {\"name\": \"e1\", \"image\": \"b/e1.png\"}, {\"name\": \"e2\", \"image\": \"b/e2.png\"},"
                    " {\"name\": \"e3\", \"image\": \"b/e3.png\"}, {\"name\": \"e4\", \"image\": \"b/e4.png\"}]}") == 0);
    /* Busts 1 and 2: Teana's and Teana 2nd's, both replacing character 3;
     * the later has it, and the earlier is told. */
    CHECK(read_bust("{\"default\": \"a\", \"expressions\": [{\"name\": \"a\", \"image\": \"t/e5-wide.png\"},"
                    " {\"name\": \"b\", \"image\": \"t/e6.png\"}]}") == 1);
    CHECK(read_bust("\"t/e7.png\"") == 2);
    /* Bust 3, a villager's that replaces nothing the campaign shows. */
    CHECK(read_bust("\"v/e8-huge.png\"") == 3);
    CHECK(notes == 0);
    owner[41] = 0;
    owner[2] = 1;
    replaces[2] = 1;
    owner[20] = 2;
    replaces[20] = 1;
    owner[4] = 3;
    replaces[4] = 1;
    Busts_Make();
    CHECK(record_count == 5);
    /* Bust 0: the default and the next three in the first, e3 in the next. */
    first = busts[0].first;
    CHECK(Busts_Place(0, 4, &record, &slot) && record == first && slot == 0);
    CHECK(Busts_Place(0, 0, &record, &slot) && record == first && slot == 1);
    CHECK(Busts_Place(0, 2, &record, &slot) && record == first && slot == 3);
    CHECK(Busts_Place(0, 3, &record, &slot) && record == first + 1 && slot == 0);
    CHECK(quantized == 4);
    /* The record is the cache slot: its sheet, its two rows of colors,
     * the second semi-transparent, and its bank. */
    texels = Busts_Record(first, &clut, &made_bank);
    CHECK(texels && clut == (const unsigned short *)(texels + BUST_SHEET_W * BUST_SHEET_H) && made_bank);
    for (k = 1; k < 256; k++) CHECK(clut[k] == (unsigned short)(k | 0x400) && clut[256 + k] == (unsigned short)(k | 0x8400));
    CHECK(clut[0] == 0 && clut[256] == 0);
    CHECK(!Busts_Record(5, &clut, &made_bank) && !Busts_Record(-1, &clut, &made_bank));
    /* Teana's picture (100 x 120) is the default's size; the other is
     * stretched to it, and said so. */
    CHECK(heard("t/e6.png is 96 x 128, the default expression's picture 100 x 120; it is stretched to it"));
    /* Character 3 is Teana 2nd's bust, the later; both are told. */
    CHECK(Busts_Replacing(3) == busts[2].first && Busts_Character(2) == 3 && Busts_Character(1) == 0);
    CHECK(heard("replace_bust: campaign character 3 is Teana 2nd's bust from mod m instead (later in load order); "
                "Teana's bust here is not shown in the campaign"));
    CHECK(heard("replace_bust: campaign character 3 was Teana's bust from mod m (earlier in load order); Teana 2nd's "
                "bust here has it instead"));
    CHECK(Busts_Replacing(1) == -1 && Busts_Replacing(0) == -1 && Busts_Replacing(0x41) == -1);
    /* The villager: shrunk to fit the page (600 x 400 to 256 x 170), but
     * no character to replace. */
    CHECK(heard("replace_bust: the campaign shows a duelist as no character, so nothing changes there"));
    CHECK(Busts_Place(3, 0, &record, &slot) && Busts_Character(3) == 0);
    CHECK(made_bank[0] && notes == 4);
    Busts_Clear();
    CHECK(Busts_Replacing(3) == -1 && Busts_Count(0) == 0 && record_count == 0);
}

/* A picture of 240 x 256 leaves the page a strip of 16 columns, 64 of its
 * 8 x 8 squares: a talking frame that changes little fits there, a blink
 * and a second talking frame that change much do not. The note says
 * exactly what was left out and what the expression then does (the bug:
 * "no blink" whatever went), and the record has just that. */
static void dropping(void)
{
    const unsigned short *clut;
    const unsigned char *made_bank;
    int record, slot;
    Busts_Clear();
    forget();
    memset(owner, -1, sizeof(owner));
    memset(replaces, 0, sizeof(replaces));
    CHECK(read_bust("{\"default\": \"a\", \"expressions\": [{\"name\": \"a\", \"image\": \"d/e0-tall.png\","
                    " \"talk\": [\"d/e0-tall-talk.png\", \"d/e0-tall-talk2-spread.png\"],"
                    " \"blink\": \"d/e0-tall-blink-spread.png\"}]}") == 0);
    owner[41] = 0;
    Busts_Make();
    if (notes != 1) fputs(said, stderr);
    CHECK(notes == 1);
    CHECK(heard("expression \"a\" has too much to change to fit the game's record: it is made without its blink "
                "frame and talking frame 2, so it never blinks and talks with 1 frame (its 240 x 256 picture leaves "
                "64 of the page's 1024 8 x 8 squares for its frames' changes)"));
    CHECK(Busts_Place(0, 0, &record, &slot) && slot == 0);
    CHECK(Busts_Record(record, &clut, &made_bank) != NULL);
    CHECK(stream_of(made_bank, 0, 1, 0) == 0 && stream_of(made_bank, 0, 2, 1) != 0);
    /* Talking frames only, and none fits: all of them, and it never talks. */
    Busts_Clear();
    forget();
    CHECK(read_bust("\"d/e0-full.png\"") == 0);
    Busts_Clear();
    CHECK(read_bust("{\"default\": \"b\", \"expressions\": [{\"name\": \"b\", \"image\": \"d/e1-full.png\","
                    " \"talk\": [\"d/e1-full-talk.png\", \"d/e1-full-talk2.png\"]}]}") == 0);
    Busts_Make();
    CHECK(heard("expression \"b\" has too much to change to fit the game's record: it is made without all 2 talking "
                "frames, so it never talks (its 256 x 256 picture leaves 0 of the page's 1024 8 x 8 squares for its "
                "frames' changes)"));
    Busts_Clear();
}

/* Two mods' disc duelists replacing one campaign character -- Seto (7)
 * and Seto 3rd (36) are both character 4 -- in both load orders: the later
 * mod's bust has it whichever duelist's id is the higher (the bug: the
 * higher id won, Seto 3rd's, whatever the order), and the loser's mod is
 * told who has it. A bust's number is the order it was read in, which is
 * the mods' load order (duelists.c reads them so). */
static void load_order(void)
{
    int order;
    for (order = 0; order < 2; order++) {
        int seto, seto3;
        Busts_Clear();
        forget();
        memset(owner, -1, sizeof(owner));
        memset(replaces, 0, sizeof(replaces));
        if (!order) {
            seto = read_bust_of("a", "\"s/e1.png\"");
            seto3 = read_bust_of("b", "\"s/e2.png\"");
        } else {
            seto3 = read_bust_of("b", "\"s/e2.png\"");
            seto = read_bust_of("a", "\"s/e1.png\"");
        }
        owner[7] = seto;
        owner[36] = seto3;
        replaces[7] = replaces[36] = 1;
        Busts_Make();
        if (!order) {
            /* a, then b: Seto 3rd's, the later. */
            CHECK(Busts_Replacing(4) == busts[seto3].first && Busts_Character(seto3) == 4 && !Busts_Character(seto));
            CHECK(heard("[a] duelists[0]: replace_bust: campaign character 4 is Seto 3rd's bust from mod b instead "
                        "(later in load order); Seto's bust here is not shown in the campaign"));
            CHECK(heard("[b] duelists[0]: replace_bust: campaign character 4 was Seto's bust from mod a (earlier in "
                        "load order); Seto 3rd's bust here has it instead"));
        } else {
            /* b, then a: Seto's, though its id is the lower. */
            CHECK(Busts_Replacing(4) == busts[seto].first && Busts_Character(seto) == 4 && !Busts_Character(seto3));
            CHECK(heard("[b] duelists[0]: replace_bust: campaign character 4 is Seto's bust from mod a instead "
                        "(later in load order); Seto 3rd's bust here is not shown in the campaign"));
        }
        CHECK(notes == 2);
    }
    Busts_Clear();
}

int main(void)
{
    int i;
    reading();
    packing();
    making();
    dropping();
    load_order();
    for (i = 0; i < document_count; i++) Json_Free(documents[i]);
    printf("busts: ok\n");
    return 0;
}

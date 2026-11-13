/* What a duelist's bust costs the game's start (busts.c Busts_Make, with
 * the real art.c reading and quantizing its PNGs): the most a bust can be
 * -- sixteen expressions of six frames each (the picture, four talking
 * frames and a blink) at 256 x 256, many colors -- and a typical one, three
 * expressions of four frames at 160 x 224. The pictures are drawn here and
 * written to a folder of the test's own (nothing of the disc's). It prints
 * the milliseconds each takes (and those of its pictures' colors alone,
 * with a hash of what they came to, the same as before art.c's quantizer
 * was made quicker: dfa319e0 and c375fda9 on Windows), and checks the
 * records hold every expression and the time stays well within bounds.
 *
 *   memories_busts_make_test [folder]   (default: busts-make-test, made here) */
#include "../../src/pc/free_duel/busts.c"
#include <png.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define make_folder(path) _mkdir(path)
#else
#include <sys/stat.h>
#define make_folder(path) mkdir(path, 0755)
#endif

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

/* --- what busts.c and art.c lean on ------------------------------------ */

void Mods_Note(const char *id, const char *format, ...)
{
    va_list arguments;
    fprintf(stderr, "[%s] ", id);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}
int Log_Wanted(LogChannel channel) { (void)channel; return 0; }
void Log_Printf(LogChannel channel, const char *format, ...) { (void)channel; (void)format; }
int Paths_Contained(const char *relative) { return relative && relative[0] != '/' && !strstr(relative, ".."); }
uint32_t Glyphs_NextCharacter(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t c = *s++;
    *text = (const char *)s;
    return c;
}
#ifdef _WIN32
const char *Win32_SerifFontPath(void) { return NULL; }
#endif

/* One duelist, 41, an added one with bust 0. */
int Duelists_Count(void) { return 42; }
int Duelists_Bust(int duelist) { return duelist == 41 ? 0 : -1; }
int Duelists_ReplacesBust(int duelist) { (void)duelist; return 0; }
const char *Duelists_Name(int duelist) { (void)duelist; return "a duelist"; }
int Tower_DuelistCharacter(int duelist) { (void)duelist; return 0; }

/* --- the pictures ------------------------------------------------------------ */

/* A figure of many colors, as painted art has (a shaded gradient and a fine
 * pattern, its own for each expression), clear around it; frame 1-4 its
 * mouth open that much, 5 its eyes shut. */
static void draw(unsigned char *rgba, int w, int h, int e, int frame)
{
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned char *p = rgba + ((size_t)y * w + x) * 4;
            long dx = x - w / 2, dy = y - h / 2, r = w * 2 / 5;
            if (dx * dx * 3 + dy * dy >= r * r * 3) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            p[0] = (unsigned char)((x * 255 / w + e * 13) & 255);
            p[1] = (unsigned char)((y * 255 / h + e * 29) & 255);
            p[2] = (unsigned char)(((x * y) / 7 + e * 41) & 255);
            p[3] = 255;
            if (frame >= 1 && frame <= 4 && y >= h * 6 / 10 && y < h * 6 / 10 + 10 && labs(dx) < 8 + frame * 3) {
                p[0] = 250;
                p[1] = (unsigned char)(240 - frame * 20);
                p[2] = 200;
            }
            if (frame == 5 && y >= h * 4 / 10 && y < h * 4 / 10 + 4 && labs(dx) < 30) p[0] = p[1] = p[2] = 10;
        }
    }
}

static void write_png(const char *path, int w, int h, const unsigned char *rgba)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = (png_uint_32)w;
    image.height = (png_uint_32)h;
    image.format = PNG_FORMAT_RGBA;
    CHECK(png_image_write_to_file(&image, path, 0, rgba, 0, NULL));
}

static JsonDocument *documents[4];
static int document_count;

/* A bust of `count` expressions, each its picture, `talks` talking frames
 * and a blink, at w x h: written into `folder`, read, made. The ms it took. */
static long made_in(const char *folder, const char *name, int count, int talks, int w, int h)
{
    static char text[16384];
    unsigned char *rgba = malloc((size_t)w * h * 4);
    char path[600], error[256];
    size_t at;
    int e, k, bust;
    clock_t start;
    long ms;
    CHECK(rgba);
    at = (size_t)snprintf(text, sizeof(text), "{\"default\": \"e0\", \"expressions\": [");
    for (e = 0; e < count; e++) {
        for (k = 0; k <= talks + 1; k++) {
            int frame = k == 0 ? 0 : k <= talks ? k : 5;
            snprintf(path, sizeof(path), "%s/%s-e%d-%d.png", folder, name, e, frame);
            draw(rgba, w, h, e, frame);
            write_png(path, w, h, rgba);
        }
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%s{\"name\": \"e%d\", \"image\": \"%s-e%d-0.png\", \"talk\": [",
                               e ? ", " : "", e, name, e);
        for (k = 1; k <= talks; k++)
            at += (size_t)snprintf(text + at, sizeof(text) - at, "%s\"%s-e%d-%d.png\"", k > 1 ? ", " : "", name, e, k);
        at += (size_t)snprintf(text + at, sizeof(text) - at, "], \"blink\": \"%s-e%d-5.png\"}", name, e);
        CHECK(at < sizeof(text));
    }
    snprintf(text + at, sizeof(text) - at, "]}");
    free(rgba);
    Busts_Clear();
    CHECK(document_count < 4);
    documents[document_count] = Json_Parse(text, error, sizeof(error));
    CHECK(documents[document_count]);
    bust = Busts_Read("m", folder, Json_Root(documents[document_count++]), "duelists[0]");
    CHECK(bust == 0 && Busts_Count(0) == count);
    start = clock();
    Busts_Make();
    ms = (long)((clock() - start) * 1000 / CLOCKS_PER_SEC);
    for (e = 0; e < count; e++) {
        int record, slot;
        CHECK(Busts_Place(0, e, &record, &slot));
    }
    printf("busts make: %s: %d expressions x %d frames at %d x %d: %d records in %ld ms\n", name, count, talks + 2,
           w, h, record_count, ms);
    {   /* Of which the colors: every picture through one palette. */
        static char names[BUST_EXPRESSIONS * (BUST_TALK_FRAMES + 2)][600];
        const char *paths[BUST_EXPRESSIONS * (BUST_TALK_FRAMES + 2)];
        unsigned char *indices[BUST_EXPRESSIONS * (BUST_TALK_FRAMES + 2)], *all;
        unsigned short clut[256];
        int n = 0;
        for (e = 0; e < count; e++) {
            for (k = 0; k <= talks + 1; k++, n++) {
                snprintf(names[n], sizeof(names[n]), "%s/%s-e%d-%d.png", folder, name, e, k == 0 ? 0 : k <= talks ? k : 5);
                paths[n] = names[n];
            }
        }
        all = malloc((size_t)n * w * h);
        CHECK(all);
        for (k = 0; k < n; k++) indices[k] = all + (size_t)k * w * h;
        start = clock();
        CHECK(CardArt_IndexedImages(paths, n, w, h, indices, clut, error, sizeof(error)));
        {   /* and what they came to, hashed (FNV-1a): the same before and after
             * art.c's quantizer was made quicker. */
            uint32_t hash = 2166136261u;
            long spent = (long)((clock() - start) * 1000 / CLOCKS_PER_SEC);
            size_t x;
            for (x = 0; x < (size_t)n * w * h; x++) hash = (hash ^ all[x]) * 16777619u;
            for (x = 0; x < 256; x++) hash = (hash ^ clut[x]) * 16777619u;
            printf("busts make: %s: its %d pictures' colors in %ld ms (texels and colors hash %08lx)\n", name, n,
                   spent, (unsigned long)hash);
        }
        free(all);
    }
    return ms;
}

int main(int argc, char **argv)
{
    const char *folder = argc > 1 ? argv[1] : "busts-make-test";
    long typical, most;
    int i;
    make_folder(folder);
    /* About 45 and 480 ms on a desktop's 32-bit build; 500 and 9000 before
     * art.c's quantizer worked on colors rather than pixels. In an
     * optimized build the bounds, loose for a slow machine, catch that
     * coming back; a debug or sanitized one only prints the times. */
    typical = made_in(folder, "typical", 3, 2, 160, 224);
    most = made_in(folder, "most", BUST_EXPRESSIONS, BUST_TALK_FRAMES, BUST_SIDE, BUST_SIDE);
#ifdef __OPTIMIZE__
    CHECK(typical < 1000 && most < 4000);
#else
    (void)typical;
    (void)most;
#endif
    Busts_Clear();
    for (i = 0; i < document_count; i++) Json_Free(documents[i]);
    printf("busts make: ok\n");
    return 0;
}

/* The PS1 GPU on the PS2's GS.
 *
 * The console's 1 MiB of VRAM (1024x512 words of 15-bit color and the mask
 * bit) is a PSMCT16 buffer at the start of GS memory, the same bit layout:
 * the game's primitives are drawn into it by the GS, its 15-bit texture
 * pages are sampled from it as they are, and the TV shows its display area
 * straight from it (the PCRTC reads it). The software GPU's VRAM
 * (src/pc/render/soft_gpu.c) stays the copy the CPU side reads and writes:
 * uploads (LoadImage) go to both, and 4- and 8-bit texture pages are
 * decoded from it through their palettes into a texture cache in the rest
 * of GS memory, 64x64 texels at a time, as primitives use them.
 *
 * Semi-transparency: a textured primitive blends only the texels with the
 * mask bit set. Through TEXA a texel is alpha 0x40 without it, 0x80 with
 * it, 0 when it is the transparent 0x0000; a semi-transparent primitive is
 * drawn twice, alpha-tested, the opaque texels plain and the others blended
 * by the four PS1 equations, which the GS's (A-B)*C+D gives with a fixed C.
 * The mask bit is the frame's alpha bit: FBA sets it, DATE tests it.
 *
 * Where the GS has drawn what the CPU side has not seen (`gs_newer`, by
 * 64x32 block), a CPU read of it (StoreImage) or a palette page decoded from
 * it reads the software copy; the game draws into the display buffers, and
 * reads back none of that in the paths seen so far. */
#include <kernel.h>
#include <dma.h>
#include <graph.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pc/render/soft_gpu.h"
#include "gs_gpu.h"

typedef uint64_t u64q;

/* GS registers (A+D addresses). */
enum {
    R_PRIM = 0x00, R_RGBAQ = 0x01, R_UV = 0x03, R_XYZ2 = 0x05, R_TEX0 = 0x06, R_CLAMP = 0x08, R_TEX1 = 0x14,
    R_XYOFFSET = 0x18, R_PRMODECONT = 0x1A, R_TEXCLUT = 0x1C, R_TEXA = 0x3B, R_TEXFLUSH = 0x3F, R_SCISSOR = 0x40,
    R_ALPHA = 0x42, R_DIMX = 0x44, R_DTHE = 0x45, R_COLCLAMP = 0x46, R_TEST = 0x47, R_PABE = 0x49, R_FBA = 0x4A,
    R_FRAME = 0x4C, R_ZBUF = 0x4E, R_BITBLTBUF = 0x50, R_TRXPOS = 0x51, R_TRXREG = 0x52, R_TRXDIR = 0x53,
    R_FINISH = 0x61
};
#define PSM_CT32 0x00
#define PSM_CT24 0x01
#define PSM_CT16 0x02
#define PSM_T8 0x13
#define PSM_T4 0x14
#define VRAM_FBW 16      /* 1024 / 64 */
#define FBP_24 128       /* the 24-bit picture (movies): 320x256 CT32 */
#define CACHE_PAGE 168   /* the texture cache: CT16 256x256 slots of 16 pages */
#define CACHE_SLOTS 19
#define COMPOSE_PAGE 472 /* 2 x 320x240 CT16 (FBW 5, 20 pages each): the shown buffer with the port's menus over it */
static int compose_page = COMPOSE_PAGE;
#define OFFSET 1024      /* XYOFFSET: primitive coordinates are window + 1024 */

#define GS_DISPFB1 ((volatile uint64_t *)0x12000070)
#define GS_DISPFB2 ((volatile uint64_t *)0x12000090)
#define GS_CSR ((volatile uint64_t *)0x12001000)

static int active;
int GsGpu_Trace;
int GsGpu_FrameLog; /* frames to summarize: primitives, fills, offsets, clips */
static unsigned log_prims, log_cmds[4];
static char log_text[512]; static size_t log_len;
#define FRAME_NOTE(...) do { if (GsGpu_FrameLog && log_len < sizeof(log_text) - 64) log_len += snprintf(log_text + log_len, sizeof(log_text) - log_len, __VA_ARGS__); } while (0)


/* ---- the GIF packet: A+D register writes, sent by DMA in order ---- */
#define PACKET_QW 8192
static u64q packets[2][PACKET_QW * 2] __attribute__((aligned(64)));
static int current;
static unsigned used = 1; /* qwords; the first is the GIFtag */

static void flush(void)
{
    u64q *packet = packets[current];
    if (used <= 1) return;
    packet[0] = (uint64_t)(used - 1) | (1ull << 15) | (1ull << 60); /* NLOOP, EOP, NREG 1 */
    packet[1] = 0xE;                                           /* A+D */
    FlushCache(0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet, (int)used, 0, 0);
    current ^= 1;
    used = 1;
}

static inline void reg(int address, uint64_t value)
{
    u64q *packet = packets[current];
    if (used >= PACKET_QW) flush(), packet = packets[current];
    packet[used * 2] = value;
    packet[used * 2 + 1] = (uint64_t)address;
    used++;
}

static void wait_idle(void)
{
    flush();
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

/* Host to GS image transfer of w x h pixels of `psm` at (x, y) of the
 * buffer at block `bp`, width `bw` (in 64s). `data` is 16-byte aligned and
 * w * h * bytes a multiple of 16. */
static u64q image_tag[16] __attribute__((aligned(64)));
static void upload(int bp, int bw, int psm, int x, int y, int w, int h, const void *data, unsigned bytes)
{
    unsigned qwc = (bytes + 15) / 16;
    const uint8_t *at = data;
    reg(R_BITBLTBUF, ((uint64_t)bp << 32) | ((uint64_t)bw << 48) | ((uint64_t)psm << 56));
    reg(R_TRXPOS, ((uint64_t)x << 32) | ((uint64_t)y << 48));
    reg(R_TRXREG, (uint64_t)w | ((uint64_t)h << 32));
    reg(R_TRXDIR, 0);
    flush();
    while (qwc) {
        unsigned part = qwc > 32767 ? 32767 : qwc;
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        image_tag[0] = (uint64_t)part | (1ull << 15) | (2ull << 58); /* NLOOP, EOP, IMAGE */
        image_tag[1] = 0;
        FlushCache(0);
        dma_channel_send_normal(DMA_CHANNEL_GIF, image_tag, 1, 0, 0);
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        dma_channel_send_normal(DMA_CHANNEL_GIF, (void *)at, (int)part, 0, 0);
        at += part * 16;
        qwc -= part;
    }
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    reg(R_TEXFLUSH, 0);
}

/* GS to host: w x h pixels of CT16 at (x, y) of PS1 VRAM into `out`
 * (16-byte aligned, w * h * 2 a multiple of 16), through VIF1 turned around. */
#define VIF1_STAT ((volatile uint32_t *)0x10003C00)
#define VIF1_FIFO ((volatile uint64_t *)0x10005000)
#define D1_CHCR ((volatile uint32_t *)0x10009000)
#define D1_MADR ((volatile uint32_t *)0x10009010)
#define D1_QWC ((volatile uint32_t *)0x10009020)
#define GS_BUSDIR ((volatile uint64_t *)0x12001040)
static void download(int x, int y, int w, int h, uint16_t *out)
{
    unsigned qwc = (unsigned)(w * h * 2 + 15) / 16;
    reg(R_BITBLTBUF, 0 | ((uint64_t)VRAM_FBW << 16) | ((uint64_t)PSM_CT16 << 24));
    reg(R_TRXPOS, (uint64_t)x | ((uint64_t)y << 16));
    reg(R_TRXREG, (uint64_t)w | ((uint64_t)h << 32));
    reg(R_FINISH, 0);
    reg(R_TRXDIR, 1);
    wait_idle();
    FlushCache(0);
    *VIF1_STAT = 1u << 23; /* FDR: VIF1 reads from the GS */
    *GS_BUSDIR = 1;
    *D1_QWC = qwc;
    *D1_MADR = (uint32_t)(uintptr_t)out & 0x0FFFFFFFu;
    *D1_CHCR = 0x100; /* start, to memory */
    {
        static int reads;
        unsigned spin = 0;
        while ((*D1_CHCR & 0x100) && ++spin < 50000000u) {}
        if (spin >= 50000000u || reads++ < 2) {
            extern void Ps2_Stage(const char *format, ...);
            Ps2_Stage("GS leitura %d,%d %dx%d: %s", x, y, w, h, spin >= 50000000u ? "TRAVOU" : "ok");
        }
        spin = 0;
        while (((*VIF1_STAT >> 24) & 0x1F) && ++spin < 50000000u) {}
    }
    *VIF1_STAT = 0;
    *GS_BUSDIR = 0;
    FlushCache(0);
    reg(R_TRXDIR, 3);
}

/* ---- the PS1 GPU's state ---- */
static struct {
    int page_x, page_y, semi, depth, dither, draw_display, texture_off;
    int window_mask_x, window_mask_y, window_x, window_y;
    int clip_x1, clip_y1, clip_x2, clip_y2, offset_x, offset_y;
    int mask_set, mask_check;
    int bank;
} gpu;
static unsigned char gs_newer[16 * 16]; /* 64x32 blocks the GS drew since the CPU side last had them */
static unsigned gen[16 * 16];           /* changes on the CPU side, by block */
static unsigned cpu_epoch = 1;          /* any change on the CPU side */
static unsigned primitives, decoded_tiles;

static void mark_drawn(int x0, int y0, int x1, int y1)
{
    int bx, by;
    if (x0 < gpu.clip_x1) x0 = gpu.clip_x1;
    if (y0 < gpu.clip_y1) y0 = gpu.clip_y1;
    if (x1 > gpu.clip_x2) x1 = gpu.clip_x2;
    if (y1 > gpu.clip_y2) y1 = gpu.clip_y2;
    if (x0 > x1 || y0 > y1) return;
    for (by = (y0 & 511) >> 5; by <= (y1 & 511) >> 5; by++)
        for (bx = (x0 & 1023) >> 6; bx <= (x1 & 1023) >> 6; bx++) gs_newer[by * 16 + bx] = 1;
}

static void touch_cpu(int x, int y, int w, int h)
{
    int bx, by;
    cpu_epoch++;
    for (by = y >> 5; by <= (y + h - 1) >> 5 && by < 16; by++)
        for (bx = x >> 6; bx <= (x + w - 1) >> 6 && bx < 16; bx++) gen[(by & 15) * 16 + (bx & 15)]++;
}

/* ---- the texture cache (4- and 8-bit pages, and the port's banks) ---- */
typedef struct {
    int used, bank, page_x, page_y, depth, clut_x, clut_y;
    unsigned signature, last, epoch; /* epoch: cpu_epoch when the signature was last checked */
    uint16_t tiles; /* 4x4 tiles of 64x64 texels, decoded */
} Slot;
static Slot slots[CACHE_SLOTS];
static unsigned frame_number;
static uint16_t tile_pixels[64 * 64] __attribute__((aligned(64)));

/* What the slot was decoded from: the page's blocks as the CPU side last
 * changed them, and the palette's own words (palettes share blocks with
 * others the game rewrites every frame). */
static unsigned signature_of(int bank, int page_x, int page_y, int depth, int clut_x, int clut_y)
{
    int width = depth == 0 ? 64 : depth == 1 ? 128 : 256, bx, by, i, entries = depth == 0 ? 16 : depth == 1 ? 256 : 0;
    if (!bank) entries = 0; /* indexed pages keep their indices: the GS looks the palette up in VRAM */
    const uint16_t *source = bank ? SoftGpu_BankPixels(bank) : SoftGpu_Vram();
    const uint16_t *clut = (source ? source : SoftGpu_Vram()) + (clut_y & 511) * 1024;
    unsigned sum = 2166136261u;
    for (i = 0; i < entries; i++) sum = (sum ^ clut[(clut_x + i) & 1023]) * 16777619u;
    if (bank) sum ^= 0x9E3779B9u * (unsigned)bank;
    for (by = (page_y & 511) >> 5; by < (((page_y & 511) + 256) >> 5) && by < 16; by++)
        for (bx = (page_x & 1023) >> 6; bx < (((page_x & 1023) + width) >> 6); bx++)
            sum = sum * 31u + gen[by * 16 + (bx & 15)];
    return sum;
}

static int slot_bp(int index) { return (CACHE_PAGE + index * 16) * 32; }

static void decode_tile(Slot *slot, int index, int tx, int ty)
{
    const uint16_t *source = slot->bank ? SoftGpu_BankPixels(slot->bank) : SoftGpu_Vram();
    const uint16_t *clut = source + (slot->clut_y & 511) * 1024;
    int u, v;
    if (!source) source = SoftGpu_Vram(), clut = source + (slot->clut_y & 511) * 1024;
    if (!slot->bank && slot->depth < 2) {
        /* The page's own bytes are the GS's PSMT8 / PSMT4 texels, in the
         * same order (the low nibble first): no palette applied here. */
        int halfwords = slot->depth == 0 ? 16 : 32; /* a 64-texel row of the tile */
        uint8_t *out = (uint8_t *)tile_pixels;
        int bytes = halfwords * 2;
        for (v = 0; v < 64; v++) {
            const uint16_t *row = source + ((slot->page_y + ty * 64 + v) & 511) * 1024;
            int x = (slot->page_x + tx * halfwords) & 1023;
            if (x + halfwords <= 1024) memcpy(out + v * bytes, row + x, (size_t)bytes);
            else for (u = 0; u < halfwords; u++) ((uint16_t *)(out + v * bytes))[u] = row[(x + u) & 1023];
        }
        upload(slot_bp(index), 4, slot->depth == 0 ? PSM_T4 : PSM_T8, tx * 64, ty * 64, 64, 64, tile_pixels,
               (unsigned)(64 * bytes));
        slot->tiles |= (uint16_t)(1u << (ty * 4 + tx));
        decoded_tiles++;
        return;
    }
    for (v = 0; v < 64; v++) {
        const uint16_t *row = source + ((slot->page_y + ty * 64 + v) & 511) * 1024;
        uint16_t *out = tile_pixels + v * 64;
        int base_u = tx * 64;
        if (slot->depth == 0) {
            for (u = 0; u < 64; u++) {
                int uu = base_u + u;
                int index4 = (row[(slot->page_x + (uu >> 2)) & 1023] >> ((uu & 3) * 4)) & 15;
                out[u] = clut[(slot->clut_x + index4) & 1023];
            }
        } else if (slot->depth == 1) {
            for (u = 0; u < 64; u++) {
                int uu = base_u + u;
                int index8 = (row[(slot->page_x + (uu >> 1)) & 1023] >> ((uu & 1) * 8)) & 255;
                out[u] = clut[(slot->clut_x + index8) & 1023];
            }
        } else {
            for (u = 0; u < 64; u++) out[u] = row[(slot->page_x + base_u + u) & 1023];
        }
    }
    upload(slot_bp(index), 4, PSM_CT16, tx * 64, ty * 64, 64, 64, tile_pixels, sizeof(tile_pixels));
    slot->tiles |= (uint16_t)(1u << (ty * 4 + tx));
    decoded_tiles++;
}

/* The slot for this page and palette, with the tiles that cover u0..u1,
 * v0..v1 decoded; its index. */
static int cached(int bank, int page_x, int page_y, int depth, int clut_x, int clut_y, int u0, int v0, int u1,
                  int v1)
{
    unsigned signature;
    int i, found = -1, oldest = 0, tx, ty;
    static int last_found = -1;
    /* Most primitives in a row sample the slot the last one did, and
     * nothing on the CPU side changed in between: no signature then. */
    if (last_found >= 0) {
        Slot *s = &slots[last_found];
        if (s->used && s->epoch == cpu_epoch && !bank && s->bank == 0 && s->page_x == page_x && s->page_y == page_y &&
            s->depth == depth) {
            found = last_found;
            goto have;
        }
    }
    for (i = 0; i < CACHE_SLOTS; i++) {
        Slot *s = &slots[i];
        if (s->used && s->bank == bank && s->page_x == page_x && s->page_y == page_y && s->depth == depth &&
            (depth == 2 || !bank || (s->clut_x == clut_x && s->clut_y == clut_y))) {
            found = i;
            break;
        }
        if (!s->used || s->last < slots[oldest].last) oldest = i;
    }
    if (found >= 0 && !bank && slots[found].epoch == cpu_epoch) goto have;
    signature = signature_of(bank, page_x, page_y, depth, clut_x, clut_y);
    if (found < 0) {
        Slot *s = &slots[oldest];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->bank = bank;
        s->page_x = page_x;
        s->page_y = page_y;
        s->depth = depth;
        s->clut_x = clut_x;
        s->clut_y = clut_y;
        s->signature = signature;
        found = oldest;
    } else if (slots[found].signature != signature) {
        slots[found].signature = signature;
        slots[found].tiles = 0;
    }
    slots[found].epoch = cpu_epoch;
have:
    last_found = found;
    slots[found].last = frame_number;
    if (u0 < 0) u0 = 0;
    if (v0 < 0) v0 = 0;
    if (u1 > 255) u1 = 255;
    if (v1 > 255) v1 = 255;
    for (ty = v0 >> 6; ty <= v1 >> 6; ty++)
        for (tx = u0 >> 6; tx <= u1 >> 6; tx++)
            if (!(slots[found].tiles & (1u << (ty * 4 + tx)))) decode_tile(&slots[found], found, tx, ty);
    return found;
}

/* ---- drawing ---- */
typedef struct { int x, y, r, g, b, u, v; } Vertex;
static struct {
    int textured, raw, semi, gouraud, clut_x, clut_y, page_word;
} prim;

static uint64_t tex0_for(int u0, int v0, int u1, int v1)
{
    int bank = 0, page_x = gpu.page_x, page_y = gpu.page_y, depth = gpu.depth;
    if (prim.page_word >= 0) {
        bank = (prim.page_word >> 11) & 15;
        bank |= ((prim.page_word >> 10) & 1) << 4;
        if (bank && !SoftGpu_BankPixels(bank)) bank = 0;
    }
    if (depth == 2 && !bank) {
        /* Straight from VRAM: the page's origin is a page of the buffer. */
        int bp = (((page_y & 511) / 64) * 16 + (page_x & 1023) / 64) * 32;
        return (uint64_t)bp | ((uint64_t)VRAM_FBW << 14) | ((uint64_t)PSM_CT16 << 20) | (8ull << 26) | (8ull << 30) | (1ull << 34) |
               ((uint64_t)(prim.raw ? 1 : 0) << 35);
    }
    {
        int slot = cached(bank, page_x & 1023, page_y & 511, depth > 2 ? 2 : depth, prim.clut_x, prim.clut_y, u0, v0,
                          u1, v1);
        if (!bank && depth < 2) {
            /* CSM2: the palette straight from VRAM (CT16, 1024 wide), reloaded
             * with each TEX0. */
            reg(R_TEXCLUT, (uint64_t)VRAM_FBW | ((uint64_t)((prim.clut_x & 1023) >> 4) << 6) |
                               ((uint64_t)(prim.clut_y & 511) << 12));
            return (uint64_t)slot_bp(slot) | (4ull << 14) | ((uint64_t)(depth == 0 ? PSM_T4 : PSM_T8) << 20) | (8ull << 26) |
                   (8ull << 30) | (1ull << 34) | ((uint64_t)(prim.raw ? 1 : 0) << 35) | ((uint64_t)PSM_CT16 << 51) |
                   (1ull << 55) | (1ull << 61);
        }
        return (uint64_t)slot_bp(slot) | (4ull << 14) | ((uint64_t)PSM_CT16 << 20) | (8ull << 26) | (8ull << 30) | (1ull << 34) |
               ((uint64_t)(prim.raw ? 1 : 0) << 35);
    }
}

static uint64_t clamp_for(void)
{
    if (!gpu.window_mask_x && !gpu.window_mask_y) return 0; /* repeat: u and v wrap at 256 */
    {
        uint64_t minu = (~(gpu.window_mask_x * 8)) & 0xFF, maxu = (gpu.window_x & gpu.window_mask_x) * 8;
        uint64_t minv = (~(gpu.window_mask_y * 8)) & 0xFF, maxv = (gpu.window_y & gpu.window_mask_y) * 8;
        return 3 | (3 << 2) | (minu << 4) | (maxu << 14) | (minv << 24) | (maxv << 34);
    }
}

static uint64_t alpha_for(int semi)
{
    switch (semi) {
    case 0: return 0 | (1 << 2) | (2 << 4) | (1 << 6) | (0x40ull << 32); /* (Cs - Cd) * 1/2 + Cd */
    case 1: return 0 | (2 << 2) | (2 << 4) | (1 << 6) | (0x80ull << 32); /* Cs + Cd */
    case 2: return 1 | (0 << 2) | (2 << 4) | (2 << 6) | (0x80ull << 32); /* Cd - Cs */
    default: return 0 | (2 << 2) | (2 << 4) | (1 << 6) | (0x20ull << 32); /* Cs / 4 + Cd */
    }
}

static inline uint64_t rgbaq(int r, int g, int b, int a)
{
    return (uint64_t)(r & 255) | ((uint64_t)(g & 255) << 8) | ((uint64_t)(b & 255) << 16) | ((uint64_t)(a & 255) << 24) |
           (0x3F800000ull << 32);
}
/* Lines are drawn through the pixels' centres (half a pixel in): the GS
 * samples at integer positions, and a line lying exactly on them can lose
 * whole rows on the console (PCSX2 draws it either way). */
static int line_half;
static inline uint64_t xyz(int x, int y)
{
    return (uint64_t)((((x + OFFSET) << 4) + line_half) & 0xFFFF) |
           ((uint64_t)((((y + OFFSET) << 4) + line_half) & 0xFFFF) << 16);
}
static inline uint64_t uv(int u, int v) { return (uint64_t)((u << 4) & 0x3FFF) | ((uint64_t)((v << 4) & 0x3FFF) << 16); }

/* One pass of a primitive: `type` the GS's (3 triangle, 1 line, 6 sprite). */
static void emit(int type, const Vertex *v, int count, int pass_semi, int test)
{
    uint64_t prim_word = (uint64_t)type | (prim.gouraud && type != 6 ? 8 : 0) | (prim.textured ? 16 : 0) | (pass_semi ? 64 : 0) |
                    (prim.textured ? 256 : 0);
    int i;
    reg(R_TEST, (uint64_t)test | (1ull << 16) | (1ull << 17) | (gpu.mask_check ? (1ull << 14) : 0));
    reg(R_PRIM, prim_word);
    for (i = 0; i < count; i++) {
        int a = prim.textured ? 0x80 : 0x40; /* untextured: the mask bit stays 0 */
        reg(R_RGBAQ, rgbaq(v[i].r, v[i].g, v[i].b, a));
        if (prim.textured) reg(R_UV, uv(v[i].u, v[i].v));
        reg(R_XYZ2, xyz(v[i].x, v[i].y));
    }
}

/* ATST EQUAL/NOTEQUAL with AREF; ATE on. */
#define TEST_ALPHA(atst, aref) (1u | ((atst) << 1) | ((aref) << 4))
#define TEST_NONE 0u

static void draw(int type, Vertex *v, int count)
{
    int i, u0 = 255, v0 = 255, u1 = 0, v1 = 0, x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (i = 0; i < count; i++) {
        v[i].x += gpu.offset_x;
        v[i].y += gpu.offset_y;
        if (v[i].x < x0) x0 = v[i].x;
        if (v[i].y < y0) y0 = v[i].y;
        if (v[i].x > x1) x1 = v[i].x;
        if (v[i].y > y1) y1 = v[i].y;
        if (v[i].u < u0) u0 = v[i].u;
        if (v[i].v < v0) v0 = v[i].v;
        if (v[i].u > u1) u1 = v[i].u;
        if (v[i].v > v1) v1 = v[i].v;
    }
    if (x1 - x0 > 1023 || y1 - y0 > 511) return; /* the console skips these */
    primitives++;
    if (prim.textured) {
        reg(R_TEX0, tex0_for(u0, v0, u1, v1));
        reg(R_CLAMP, clamp_for());
    }
    reg(R_DTHE, gpu.dither && prim.gouraud && !(prim.textured && prim.raw) ? 1 : 0);
    if (prim.semi) reg(R_ALPHA, alpha_for(gpu.semi));
    if (prim.textured) {
        if (prim.semi) {
            emit(type, v, count, 0, TEST_ALPHA(4u, 0x40u)); /* the opaque texels */
            emit(type, v, count, 1, TEST_ALPHA(4u, 0x80u)); /* the ones with the mask bit, blended */
        } else {
            emit(type, v, count, 0, TEST_ALPHA(7u, 0u)); /* all but the transparent ones */
        }
    } else {
        emit(type, v, count, prim.semi, TEST_NONE);
    }
    mark_drawn(x0, y0, type == 6 ? x1 - 1 : x1, type == 6 ? y1 - 1 : y1);
    if (GsGpu_Trace) {
        static uint16_t probe[8] __attribute__((aligned(64)));
        download(160, 120, 8, 1, probe);
        printf("ps2:   after type %d at %d,%d..%d,%d tex %d semi %d/%d: center %04x %04x\n", type, x0, y0, x1, y1,
               prim.textured, prim.semi, gpu.semi, probe[0], probe[1]);
    }
}

static void set_draw_mode(uint32_t word)
{
    gpu.page_x = (word & 15) * 64;
    gpu.page_y = ((word >> 4) & 1) * 256;
    gpu.semi = (word >> 5) & 3;
    gpu.depth = (word >> 7) & 3;
    gpu.dither = (word >> 9) & 1;
    gpu.draw_display = (word >> 10) & 1;
    gpu.texture_off = (word >> 11) & 1;
}

static void set_scissor(void)
{
    reg(R_SCISSOR, (uint64_t)(gpu.clip_x1 & 2047) | ((uint64_t)(gpu.clip_x2 & 2047) << 16) | ((uint64_t)(gpu.clip_y1 & 2047) << 32) |
                       ((uint64_t)(gpu.clip_y2 & 2047) << 48));
}

static void set_mask(void)
{
    reg(R_FBA, gpu.mask_set ? 1 : 0);
}

static inline int sx11(uint32_t value) { return (int32_t)(value << 21) >> 21; }

static void vertex_position(Vertex *v, uint32_t word)
{
    v->x = sx11(word);
    v->y = sx11(word >> 16);
}

static void vertex_color(Vertex *v, uint32_t word)
{
    v->r = word & 255;
    v->g = (word >> 8) & 255;
    v->b = (word >> 16) & 255;
}

static size_t polygon(const uint32_t *words, size_t count)
{
    uint32_t command = words[0] >> 24;
    int gouraud = command & 0x10, quad = command & 8, textured = command & 4, n = quad ? 4 : 3, i;
    size_t per = 1 + (textured ? 1 : 0) + (gouraud ? 1 : 0), need = 1 + n * per - (gouraud ? 1 : 0), at = 0;
    Vertex v[4];
    if (count < need) return 0;
    prim.textured = textured != 0;
    prim.raw = command & 1;
    prim.semi = (command & 2) != 0;
    prim.gouraud = gouraud != 0;
    prim.page_word = -1;
    for (i = 0; i < n; i++) {
        if (i == 0 || gouraud) vertex_color(&v[i], words[at++]);
        else v[i].r = v[0].r, v[i].g = v[0].g, v[i].b = v[0].b;
        vertex_position(&v[i], words[at++]);
        if (textured) {
            uint32_t t = words[at++];
            v[i].u = t & 255;
            v[i].v = (t >> 8) & 255;
            if (i == 0) {
                prim.clut_x = ((t >> 16) & 0x3F) * 16;
                prim.clut_y = (t >> 22) & 0x1FF;
            } else if (i == 1) {
                /* The polygon's own page and depth for itself and after it. */
                uint32_t page = t >> 16;
                set_draw_mode((page & 0x9FF) | ((uint32_t)gpu.dither << 9) | ((uint32_t)gpu.draw_display << 10));
                prim.page_word = (int)page;
            }
        } else {
            v[i].u = v[i].v = 0;
        }
    }
    if (prim.textured && prim.raw) {
        for (i = 0; i < n; i++) v[i].r = v[i].g = v[i].b = 0x80;
    }
    draw(3, v, 3);
    if (quad) {
        Vertex second[3] = {v[1], v[2], v[3]};
        /* draw() moved the first three by the offset: the copy is undone. */
        second[0].x -= gpu.offset_x, second[0].y -= gpu.offset_y;
        second[1].x -= gpu.offset_x, second[1].y -= gpu.offset_y;
        draw(3, second, 3);
    }
    return need;
}

static size_t rectangle(const uint32_t *words, size_t count)
{
    static const int sizes[4] = {0, 1, 8, 16};
    uint32_t command = words[0] >> 24;
    int textured = command & 4, kind = (command >> 3) & 3, w, h;
    size_t need = 2 + (textured ? 1u : 0u) + (kind == 0 ? 1u : 0u), at = 2;
    Vertex v[2];
    if (count < need) return 0;
    prim.textured = textured != 0;
    prim.raw = command & 1;
    prim.semi = (command & 2) != 0;
    prim.gouraud = 0;
    prim.page_word = -1;
    vertex_color(&v[0], words[0]);
    vertex_position(&v[0], words[1]);
    v[0].u = v[0].v = 0;
    if (textured) {
        v[0].u = words[at] & 255;
        v[0].v = (words[at] >> 8) & 255;
        prim.clut_x = ((words[at] >> 16) & 0x3F) * 16;
        prim.clut_y = (words[at] >> 22) & 0x1FF;
        at++;
    }
    w = h = sizes[kind];
    if (kind == 0) {
        w = words[at] & 0x3FF;
        h = (words[at] >> 16) & 0x1FF;
    }
    if (!w || !h) return need;
    if (prim.textured && prim.raw) v[0].r = v[0].g = v[0].b = 0x80;
    v[1] = v[0];
    v[1].x = v[0].x + w;
    v[1].y = v[0].y + h;
    v[1].u = v[0].u + w;
    v[1].v = v[0].v + h;
    draw(6, v, 2);
    return need;
}

static size_t lines(const uint32_t *words, size_t count)
{
    uint32_t command = words[0] >> 24;
    int shaded = command & 0x10, poly = command & 8;
    size_t at = 0;
    Vertex a, b, pair[2];
    if (count < (shaded ? 4u : 3u)) return 0;
    prim.textured = 0;
    prim.raw = 0;
    prim.semi = (command & 2) != 0;
    prim.gouraud = shaded != 0;
    prim.page_word = -1;
    vertex_color(&a, words[at++]);
    vertex_position(&a, words[at++]);
    a.u = a.v = 0;
    for (;;) {
        if (poly && at < count && (words[at] & 0xF000F000u) == 0x50005000u) return at + 1;
        b = a;
        if (shaded) {
            if (at >= count) return 0;
            vertex_color(&b, words[at++]);
        }
        if (at >= count) return 0;
        vertex_position(&b, words[at++]);
        pair[0] = a;
        pair[1] = b;
        if (!shaded && (a.y == b.y || a.x == b.x)) {
            /* Level or upright: a sprite over the pixels, both ends included
             * as the PS1 draws them. */
            pair[0].x = a.x < b.x ? a.x : b.x;
            pair[0].y = a.y < b.y ? a.y : b.y;
            pair[1].x = (a.x < b.x ? b.x : a.x) + 1;
            pair[1].y = (a.y < b.y ? b.y : a.y) + 1;
            draw(6, pair, 2);
        } else {
            line_half = 8;
            draw(1, pair, 2);
            line_half = 0;
        }
        a = b;
        if (!poly) return at;
        if (at >= count) return 0;
    }
}

/* GP0(02): a fill, ignoring the drawing area, offset and mask. */
static void fill(int x, int y, int w, int h, uint32_t rgb)
{
    if (w <= 0 || h <= 0) return;
    reg(R_SCISSOR, 0 | (1023ull << 16) | (0ull << 32) | (511ull << 48));
    reg(R_FBA, 0);
    reg(R_TEST, (1ull << 16) | (1ull << 17));
    reg(R_DTHE, 0);
    reg(R_PRIM, 6);
    reg(R_RGBAQ, rgbaq(rgb & 255, (rgb >> 8) & 255, (rgb >> 16) & 255, 0));
    reg(R_XYZ2, xyz(x, y));
    reg(R_XYZ2, xyz(x + w, y + h));
    set_scissor();
    set_mask();
}

static void flush_pending(void);
size_t GsGpu_Gp0(const uint32_t *words, size_t count)
{
    flush_pending();
    size_t at = 0;
    while (at < count) {
        uint32_t word = words[at], command = word >> 24;
        size_t used = 1;
        if (GsGpu_Trace && command >= 0x02) {
            GsGpu_Trace--;
            printf("ps2: gp0 %08x %08x %08x %08x %08x semi %d page %d,%d depth %d clip %d,%d-%d,%d off %d,%d\n", word,
                   count - at > 1 ? words[at + 1] : 0, count - at > 2 ? words[at + 2] : 0,
                   count - at > 3 ? words[at + 3] : 0, count - at > 4 ? words[at + 4] : 0, gpu.semi, gpu.page_x,
                   gpu.page_y, gpu.depth, gpu.clip_x1, gpu.clip_y1, gpu.clip_x2, gpu.clip_y2, gpu.offset_x,
                   gpu.offset_y);
        }
        if (GsGpu_FrameLog && command >= 0x20 && command < 0x80) log_prims++, log_cmds[command >> 5]++;
        if (command >= 0x20 && command < 0x40) used = polygon(words + at, count - at);
        else if (command >= 0x40 && command < 0x60) used = lines(words + at, count - at);
        else if (command >= 0x60 && command < 0x80) used = rectangle(words + at, count - at);
        else if (command == 0x02) {
            if (count - at < 3) return at;
            {
                int x = words[at + 1] & 0x3F0, y = (words[at + 1] >> 16) & 0x1FF;
                int w = ((words[at + 2] & 0x3FF) + 15) & ~15, h = (words[at + 2] >> 16) & 0x1FF;
                fill(x, y, w, h, word & 0xFFFFFF);
                {
                    int saved_x1 = gpu.clip_x1, saved_y1 = gpu.clip_y1, saved_x2 = gpu.clip_x2, saved_y2 = gpu.clip_y2;
                    gpu.clip_x1 = 0, gpu.clip_y1 = 0, gpu.clip_x2 = 1023, gpu.clip_y2 = 511;
                    mark_drawn(x, y, x + w - 1, y + h - 1);
                    gpu.clip_x1 = saved_x1, gpu.clip_y1 = saved_y1, gpu.clip_x2 = saved_x2, gpu.clip_y2 = saved_y2;
                }
            }
            used = 3;
        } else if (command == 0xE1) {
            set_draw_mode(word);
        } else if (command == 0xE2) {
            gpu.window_mask_x = word & 31;
            gpu.window_mask_y = (word >> 5) & 31;
            gpu.window_x = (word >> 10) & 31;
            gpu.window_y = (word >> 15) & 31;
        } else if (command == 0xE3) {
            gpu.clip_x1 = word & 1023;
            gpu.clip_y1 = (word >> 10) & 511;
            set_scissor();
        } else if (command == 0xE4) {
            gpu.clip_x2 = word & 1023;
            gpu.clip_y2 = (word >> 10) & 511;
            FRAME_NOTE(" clip%d,%d-%d,%d", gpu.clip_x1, gpu.clip_y1, gpu.clip_x2, gpu.clip_y2);
            set_scissor();
        } else if (command == 0xE5) {
            gpu.offset_x = sx11(word);
            gpu.offset_y = sx11(word >> 11);
            FRAME_NOTE(" off%d,%d", gpu.offset_x, gpu.offset_y);
        } else if (command == 0xE6) {
            gpu.mask_set = word & 1;
            gpu.mask_check = (word >> 1) & 1;
            set_mask();
        } else if (command == 0x80 || command == 0xA0 || command == 0xC0) {
            /* VRAM transfers come through SoftGpu_Load/Store/Move, not packets. */
            used = command == 0x80 ? 4 : 3;
        }
        if (!used) return at;
        at += used;
    }
    return at;
}

/* ---- the CPU side (soft_gpu.c calls these after its own copy changed) ---- */
static uint16_t staging[1024 * 512] __attribute__((aligned(64)));

static void load_now(int x, int y, int w, int h)
{
    const uint16_t *vram = SoftGpu_Vram();
    int row, bx, by;
    if (GsGpu_Trace) printf("ps2: load %d,%d %dx%d\n", x, y, w, h);
    x &= 1023;
    y &= 511;
    if (x + w > 1024 || y + h > 512 || (w * h) & 7) {
        /* Wrapping or odd-sized: the whole rows involved, from the copy. */
        int y0 = 0, rows = 512;
        if (y + h <= 512) y0 = y, rows = h;
        for (row = 0; row < rows; row++) memcpy(staging + row * 1024, vram + (y0 + row) * 1024, 2048);
        upload(0, VRAM_FBW, PSM_CT16, 0, y0, 1024, rows, staging, (unsigned)rows * 2048u);
    } else {
        for (row = 0; row < h; row++) memcpy(staging + row * w, vram + (y + row) * 1024 + x, (size_t)w * 2);
        upload(0, VRAM_FBW, PSM_CT16, x, y, w, h, staging, (unsigned)(w * h * 2));
    }
    touch_cpu(x, y, w, h);
    for (by = y >> 5; by <= (y + h - 1) >> 5 && by < 16; by++)
        for (bx = x >> 6; bx <= (x + w - 1) >> 6 && bx < 16; bx++)
            if ((by << 5) >= y && ((by + 1) << 5) <= y + h && (bx << 6) >= x && ((bx + 1) << 6) <= x + w)
                gs_newer[by * 16 + bx] = 0;
}

/* While a 24-bit picture (a movie) is shown, the GS's copy of VRAM is not:
 * the movie's strips (LoadImage, 20 a frame) only change the CPU side, and
 * the rows they cover go to the GS in one transfer when anything next needs
 * the GS's VRAM (a primitive, a fill, a move, a 16-bit picture). */
static int shown_24_now, pending_y0 = 512, pending_y1 = -1;
static void flush_pending(void)
{
    int y0 = pending_y0, y1 = pending_y1;
    if (y1 < y0) return;
    pending_y0 = 512, pending_y1 = -1;
    load_now(0, y0, 1024, y1 - y0 + 1);
}

void GsGpu_Load(int x, int y, int w, int h)
{
    if (!active || w <= 0 || h <= 0) return;
    if (shown_24_now) {
        int y0 = y & 511, y1 = y0 + h - 1;
        touch_cpu(x & 1023, y0, w, h);
        if (y1 > 511) y0 = 0, y1 = 511;
        if (y0 < pending_y0) pending_y0 = y0;
        if (y1 > pending_y1) pending_y1 = y1;
        return;
    }
    flush_pending();
    load_now(x, y, w, h);
}

void GsGpu_Fill(int x, int y, int w, int h, uint32_t rgb)
{
    flush_pending();
    if (!active) return;
    if (GsGpu_Trace) printf("ps2: fill %d,%d %dx%d %06x\n", x, y, w, h, (unsigned)rgb);
    FRAME_NOTE(" fill%d,%d,%dx%d:%06x", x, y, w, h, (unsigned)rgb);
    fill(x, y, w, h, rgb);
    {
        int saved_x1 = gpu.clip_x1, saved_y1 = gpu.clip_y1, saved_x2 = gpu.clip_x2, saved_y2 = gpu.clip_y2;
        gpu.clip_x1 = 0, gpu.clip_y1 = 0, gpu.clip_x2 = 1023, gpu.clip_y2 = 511;
        mark_drawn(x, y, x + w - 1, y + h - 1);
        gpu.clip_x1 = saved_x1, gpu.clip_y1 = saved_y1, gpu.clip_x2 = saved_x2, gpu.clip_y2 = saved_y2;
    }
}

void GsGpu_Move(int sx, int sy, int dx, int dy, int w, int h)
{
    flush_pending();
    if (!active || w <= 0 || h <= 0) return;
    if (GsGpu_Trace) printf("ps2: move %d,%d -> %d,%d %dx%d\n", sx, sy, dx, dy, w, h);
    /* The CPU side already moved its copy: send the destination from it,
     * unless the source holds what only the GS drew. */
    {
        int bx, by, newer = 0;
        for (by = (sy & 511) >> 5; by <= ((sy + h - 1) & 511) >> 5; by++)
            for (bx = (sx & 1023) >> 6; bx <= ((sx + w - 1) & 1023) >> 6; bx++) newer |= gs_newer[by * 16 + bx];
        if (!newer) {
            GsGpu_Load(dx, dy, w, h);
            return;
        }
    }
    reg(R_BITBLTBUF, 0 | ((uint64_t)VRAM_FBW << 16) | ((uint64_t)PSM_CT16 << 24) | (0ull << 32) | ((uint64_t)VRAM_FBW << 48) |
                         ((uint64_t)PSM_CT16 << 56));
    reg(R_TRXPOS, (uint64_t)(sx & 2047) | ((uint64_t)(sy & 2047) << 16) | ((uint64_t)(dx & 2047) << 32) | ((uint64_t)(dy & 2047) << 48));
    reg(R_TRXREG, (uint64_t)w | ((uint64_t)h << 32));
    reg(R_TRXDIR, 2);
    reg(R_TEXFLUSH, 0);
    {
        int saved_x1 = gpu.clip_x1, saved_y1 = gpu.clip_y1, saved_x2 = gpu.clip_x2, saved_y2 = gpu.clip_y2;
        gpu.clip_x1 = 0, gpu.clip_y1 = 0, gpu.clip_x2 = 1023, gpu.clip_y2 = 511;
        mark_drawn(dx, dy, dx + w - 1, dy + h - 1);
        gpu.clip_x1 = saved_x1, gpu.clip_y1 = saved_y1, gpu.clip_x2 = saved_x2, gpu.clip_y2 = saved_y2;
    }
}

/* Whether the CPU side's copy of this rectangle is stale (the GS drew it). */
int GsGpu_Stale(int x, int y, int w, int h)
{
    int bx, by;
    if (!active) return 0;
    for (by = (y & 511) >> 5; by <= ((y + h - 1) & 511) >> 5; by++)
        for (bx = (x & 1023) >> 6; bx <= ((x + w - 1) & 1023) >> 6; bx++)
            if (gs_newer[by * 16 + bx]) return 1;
    return 0;
}

/* ---- the picture ---- */
static int shown_x = -1, shown_y, shown_w, shown_h, shown_24;
static uint32_t picture24[320 * 256] __attribute__((aligned(64)));

static void environment(void);
static void display(int fbp, int fbw, int psm, int x, int y, int w, int h, int interlace)
{
    static int mode_w = -1, mode_h, mode_interlace;
    uint64_t dispfb = (uint64_t)fbp | ((uint64_t)fbw << 9) | ((uint64_t)psm << 15) | ((uint64_t)x << 32) | ((uint64_t)y << 43);
    if (w != mode_w || h != mode_h || interlace != mode_interlace) {
        graph_set_mode(interlace ? GRAPH_MODE_INTERLACED : GRAPH_MODE_NONINTERLACED, GRAPH_MODE_NTSC,
                       GRAPH_MODE_FRAME, GRAPH_DISABLE);
        /* O libgraph poe o NTSC sem entrelacamento na linha 26 com 224 linhas.
         * O PS1 mostra 240 linhas de 16 a 256: com 240 a partir da 26 as
         * ultimas ~8 linhas caem fora da area visivel da TV (cortava a caixa
         * do FIELD embaixo no tabuleiro). Sobe a imagem o que passa de 224. */
        graph_set_screen(0, (!interlace && h > 224) ? -((h - 224) / 2) : 0, w, h);
        graph_set_bgcolor(0, 0, 0);
        graph_set_output(1, 0, 1, 0, 1, 0xFF);
        mode_w = w, mode_h = h, mode_interlace = interlace;
        environment(); /* a mode change resets the GS's drawing registers */
    }
    *GS_DISPFB1 = dispfb;
    *GS_DISPFB2 = dispfb;
}

/* The port's own menus (the save slots, the deck slots, a question):
 * platform_ps2.c renders them into a 320x240 picture with alpha (GS order,
 * alpha 0x80 opaque) and they are blended over the buffer about to be shown. */
extern const uint16_t *Ps2_OverlayBox(int *changed, int *bx, int *by, int *bw, int *bh);
static int overlay_lost = 1; /* kept for the movie path, which still marks it */
/* The buffer the game shows is copied aside and the menu's box written over
 * the copy, which is shown instead: the game's own buffers stay as it drew
 * them. Two copies alternate, so the one on screen is never written. */
static int draw_overlay(int x, int y)
{
    int changed = 0, bx, by, bw, bh;
    const uint16_t *box = Ps2_OverlayBox(&changed, &bx, &by, &bw, &bh);
    if (!box) return 0;
    reg(R_BITBLTBUF, 0 | ((uint64_t)VRAM_FBW << 16) | ((uint64_t)PSM_CT16 << 24) | ((uint64_t)(compose_page * 32) << 32) |
                         (5ull << 48) | ((uint64_t)PSM_CT16 << 56));
    reg(R_TRXPOS, (uint64_t)(x & 2047) | ((uint64_t)(y & 2047) << 16));
    reg(R_TRXREG, 320ull | (240ull << 32));
    reg(R_TRXDIR, 2);
    upload(compose_page * 32, 5, PSM_CT16, bx, by, bw, bh, box, (unsigned)(bw * bh * 2));
    (void)changed;
    return 1;
}

void GsGpu_Show(int x, int y, int w, int h, int rgb24)
{
    if (!active) return;
    shown_24_now = rgb24;
    if (rgb24) return;
    flush_pending();
    if (draw_overlay(x & 1023, y & 511)) {
        wait_idle();
        display(compose_page, 5, PSM_CT16, 0, 0, w, h, h > 256);
        /* The next composition goes to the other page: never into the one
         * being scanned out (that tore the menu). */
        compose_page = compose_page == COMPOSE_PAGE ? COMPOSE_PAGE + 20 : COMPOSE_PAGE;
        return;
    }
    wait_idle();
    display(0, VRAM_FBW, PSM_CT16, x & 1023, y & 511, w, h, h > 256);
}

void GsGpu_Present(const uint16_t *vram, int x, int y, int w, int h, int rgb24)
{
    if (!active) return;
    {
        extern void Ps2_MovieFrame(int rgb24);
        Ps2_MovieFrame(rgb24);
    }
    if (GsGpu_Trace) printf("ps2: present %d,%d %dx%d %d\n", x, y, w, h, rgb24);
    if (GsGpu_FrameLog) {
        GsGpu_FrameLog--;
        printf("ps2: frame show %d,%d prims %u (poly %u line %u rect %u)%s\n", x, y, log_prims, log_cmds[1],
               log_cmds[2], log_cmds[3], log_text);
        log_prims = 0, log_len = 0, log_text[0] = 0;
        memset(log_cmds, 0, sizeof(log_cmds));
    }
    flush();
    frame_number++;
    if (rgb24) {
        /* The PS1's 24-bit picture is the GS's PSMCT24 byte for byte: rows
         * copied as they are, no conversion on the EE. */
        int row, cw = w > 320 ? 320 : w, ch = h > 256 ? 256 : h;
        uint8_t *out = (uint8_t *)picture24;
        for (row = 0; row < ch; row++)
            memcpy(out + row * 960, (const uint8_t *)(vram + ((y + row) & 511) * 1024 + x), (size_t)cw * 3);
        upload(FBP_24 * 32, 5, PSM_CT24, 0, 0, 320, ch, picture24, (unsigned)(960 * ch));
        overlay_lost = 1;
        flush();
        display(FBP_24, 5, PSM_CT24, 0, 0, cw, ch, h > 256);
    } else {
        GsGpu_Show(x, y, w, h, 0); /* with the port's menus, redrawn at each VBlank */
    }
    static unsigned display_notes;
    if ((w != shown_w || h != shown_h || rgb24 != shown_24) && display_notes++ < 16)
        printf("ps2: display %d,%d %dx%d%s\n", x, y, w, h, rgb24 ? " 24-bit" : "");
    shown_x = x, shown_y = y, shown_w = w, shown_h = h, shown_24 = rgb24;
    shown_24_now = rgb24;
}

unsigned GsGpu_Stats(unsigned *tiles)
{
    unsigned count = primitives;
    if (tiles) *tiles = decoded_tiles;
    primitives = decoded_tiles = 0;
    return count;
}

int GsGpu_Active(void) { return active; }

/* The GS's VRAM into `out` (1024 x 512 words), for debugging. */
void GsGpu_ReadVram(uint16_t *out)
{
    flush_pending();
    int y;
    for (y = 0; y < 512; y += 64) download(0, y, 1024, 64, out + y * 1024);
}

static void environment(void)
{
    static const int dither[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
    uint64_t dimx = 0;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) dimx |= (uint64_t)(dither[i][j] & 7) << (i * 16 + j * 4);
    reg(R_PRMODECONT, 1);
    reg(R_FRAME, 0 | ((uint64_t)VRAM_FBW << 16) | ((uint64_t)PSM_CT16 << 24));
    reg(R_ZBUF, (uint64_t)400 | (1ull << 32)); /* no Z: masked */
    reg(R_XYOFFSET, ((uint64_t)OFFSET << 4) | ((uint64_t)(OFFSET << 4) << 32));
    reg(R_TEXA, 0x40 | (1ull << 15) | (0x80ull << 32));
    reg(R_TEX1, 0);
    reg(R_COLCLAMP, 1);
    reg(R_DIMX, dimx);
    reg(R_DTHE, 0);
    reg(R_PABE, 0);
    set_scissor();
    set_mask();
    flush();
}

/* A abertura (intro_ps2.c), antes do jogo: quadros 640 x 448 CT32 enviados
 * direto para um de dois framebuffers (paginas 0 e 140) e mostrados no
 * proximo vsync, em modo entrelacado. NULL: fim (GsGpu_Init refaz tudo). */
void GsGpu_IntroFrame(const uint32_t *abgr)
{
    static int started, back;
    if (!abgr) {
        if (started) {
            wait_idle();
            *GS_DISPFB1 = *GS_DISPFB2 = 0;
        }
        started = 0;
        return;
    }
    if (!started) {
        dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
        dma_channel_fast_waits(DMA_CHANNEL_GIF);
        graph_initialize(0, 640, 448, PSM_CT32, 0, 0);
        started = 1, back = 140;
    }
    FlushCache(0);
    upload(back * 32, 10, PSM_CT32, 0, 0, 640, 448, abgr, 640 * 448 * 4);
    wait_idle();
    graph_wait_vsync();
    display(back, 10, PSM_CT32, 0, 0, 640, 448, 1);
    if (started == 1) {
        /* 448 linhas entrelacadas: cada campo le uma linha sim, outra nao
         * (FFMD campo), com o filtro anti-tremido para o texto. */
        graph_set_mode(GRAPH_MODE_INTERLACED, GRAPH_MODE_NTSC, GRAPH_MODE_FIELD, GRAPH_ENABLE);
        graph_set_screen(0, 0, 640, 448);
        graph_set_output(1, 0, 1, 0, 1, 0xFF);
        started = 2;
    }
    back = back ? 0 : 140;
}

void GsGpu_Init(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    graph_initialize(0, 320, 240, PSM_CT16, 0, 0);
    gpu.clip_x2 = 1023;
    gpu.clip_y2 = 511;
    environment();
    /* VRAM as the CPU side has it (black). */
    memset(staging, 0, sizeof(staging));
    upload(0, VRAM_FBW, PSM_CT16, 0, 0, 1024, 512, staging, sizeof(staging));
    wait_idle();
    active = 1;
    printf("ps2: GS renderer on\n");
    if (getenv("PS2_GS_SELFTEST")) {
        static const uint32_t test[] = {
            0x020000FF, 0x00000000, 0x00400040,                   /* fill red 64x64 at 0,0 */
            0x6000FF00, 0x00000040, 0x00400040,                   /* green sprite at 64,0 */
            0xE1000000,                                           /* page 0,0 15-bit? depth from bits 7-8: 0 */
            0xE1000100,                                           /* page 0,0 depth 2 (15-bit) */
            0x65808080, 0x00000080, 0x00000000, 0x00400040,       /* raw textured sprite at 128,0 from page */
            0x2A0000FF, 0x00800000, 0x00800040, 0x00C00000, 0x00C00040, /* blue quad semi (mode 0) at 0,128 */
        };
        static uint16_t out[1024 * 256] __attribute__((aligned(64)));
        static uint16_t indices[4 * 16], clut[16];
        static const uint32_t test4[] = {
            0xE100000F,                                           /* page 960,0, 4-bit */
            0x64808080, 0x00800000, 0x3D000000, 0x00100010,       /* 16x16 sprite at 0,128 clut (0,244) */
            0x2C808080, 0x00800040, 0x3D000000, 0x00800050, 0x000F000F, 0x00900040, 0x00000F00, 0x00900050, 0x00000F0F,
        };
        FILE *file;
        int k;
        for (k = 0; k < 4 * 16; k++) indices[k] = (uint16_t)(((k % 4) * 4) | (((k % 4) * 4 + 1) << 4) | (((k % 4) * 4 + 2) << 8) | (((k % 4) * 4 + 3) << 12));
        for (k = 0; k < 16; k++) clut[k] = (uint16_t)(k == 0 ? 0 : (k << 1) | ((31 - k * 2) << 10));
        SoftGpu_Load(960, 0, 4, 16, indices);
        SoftGpu_Load(0, 244, 16, 1, clut);
        GsGpu_Gp0(test, sizeof(test) / 4);
        GsGpu_Gp0(test4, sizeof(test4) / 4);
        wait_idle();
        download(0, 0, 1024, 256, out);
        printf("ps2: selftest pixels: %04x %04x %04x %04x 4bit: %04x %04x %04x | quad %04x %04x\n", out[10 * 1024 + 10],
               out[10 * 1024 + 74], out[10 * 1024 + 138], out[140 * 1024 + 10], out[130 * 1024 + 0],
               out[130 * 1024 + 5], out[130 * 1024 + 15], out[130 * 1024 + 70], out[140 * 1024 + 75]);
        if ((file = fopen("host:selftest.bin", "wb"))) {
            fwrite(out, 2, 1024 * 256, file);
            fclose(file);
        }
    }
}

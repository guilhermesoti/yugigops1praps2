/* Abertura do port no PS2: uma vinheta propria da Retrogames (particulas que
 * formam um cubo dourado girando, com um acorde sintetizado aqui mesmo), o
 * titulo do port e a tela "Apoie o projeto" com o QR code do Pix.
 *
 * Roda antes do jogo (platform_ps2.c, main), quando ainda nao ha renderizador
 * do jogo: cada quadro e desenhado pela CPU num quadro 640 x 448 ARGB e
 * mandado ao GS por gs_gpu.c (GsGpu_IntroFrame). START pula a vinheta; na tela de apoio,
 * X ou START (ou 20 s) seguem para o jogo. */
#include <kernel.h>
#include <libpad.h>
#include <audsrv.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "pc/platform/menu.h"
#include "intro_qr.h"

#define W 640
#define H 448
#define RATE 22050

#include "gs_gpu.h"

/* Do heap e devolvidos no fim (o ELF ja ocupa quase toda a RAM). O quadro
 * vira CT32 no proprio lugar antes de ir para o GS e e refeito a cada quadro. */
static uint32_t *canvas_px;

/* ---- som: um "swell" grave que sobe e um acorde em arpejo ---- */
#define SOUND_FRAMES (RATE * 9 / 2)
static int16_t *sound;
static unsigned sound_at;

static void synth(void)
{
    /* Osciladores por rotacao (sem sinf por amostra). */
    static const float notes[] = {329.63f, 493.88f, 659.25f, 830.61f, 987.77f, 1318.5f};
    const int chime_at = RATE * 3 / 2;
    float mix[2];
    int i, n;
    float sw_c = 1, sw_s = 0;
    for (i = 0; i < SOUND_FRAMES; i++) sound[i * 2] = sound[i * 2 + 1] = 0;
    /* swell: 55 -> 110 Hz em 1,5 s, entrando e sumindo */
    for (i = 0; i < chime_at + RATE / 2; i++) {
        float t = (float)i / RATE, f = 55.0f * (1.0f + t / 1.5f), a = 2 * 3.14159265f * f / RATE;
        float c = sw_c * cosf(a) - sw_s * sinf(a), s = sw_s * cosf(a) + sw_c * sinf(a);
        float env = t < 1.2f ? t / 1.2f : (t < 1.5f ? 1.0f : 1.0f - (t - 1.5f) * 2.0f);
        sw_c = c, sw_s = s;
        if (env < 0) env = 0;
        mix[0] = s * env * 0.22f;
        sound[i * 2] = (int16_t)(mix[0] * 32767), sound[i * 2 + 1] = (int16_t)(mix[0] * 32767);
    }
    for (n = 0; n < 6; n++) {
        int start = chime_at + n * RATE * 7 / 100;
        float a = 2 * 3.14159265f * notes[n] / RATE, ca = cosf(a), sa = sinf(a);
        float a2 = 2 * a, ca2 = cosf(a2), sa2 = sinf(a2);
        float c = 1, s = 0, c2 = 1, s2 = 0, env = 0.16f, decay = expf(-2.4f / RATE);
        float pan = 0.25f + 0.1f * n; /* da esquerda para a direita */
        for (i = start; i < SOUND_FRAMES; i++) {
            float nc = c * ca - s * sa, ns = s * ca + c * sa, nc2 = c2 * ca2 - s2 * sa2, ns2 = s2 * ca2 + c2 * sa2;
            float v, attack = (float)(i - start) / (RATE / 200);
            c = nc, s = ns, c2 = nc2, s2 = ns2;
            v = (s + 0.3f * s2) * env * (attack < 1 ? attack : 1);
            env *= decay;
            mix[0] = sound[i * 2] / 32767.0f + v * (1 - pan);
            mix[1] = sound[i * 2 + 1] / 32767.0f + v * pan;
            if (mix[0] > 1) mix[0] = 1;
            if (mix[0] < -1) mix[0] = -1;
            if (mix[1] > 1) mix[1] = 1;
            if (mix[1] < -1) mix[1] = -1;
            sound[i * 2] = (int16_t)(mix[0] * 32767), sound[i * 2 + 1] = (int16_t)(mix[1] * 32767);
        }
    }
    /* sem estalo no fim */
    for (i = SOUND_FRAMES - RATE / 4; i < SOUND_FRAMES; i++) {
        float f = (float)(SOUND_FRAMES - i) / (RATE / 4);
        sound[i * 2] = (int16_t)(sound[i * 2] * f), sound[i * 2 + 1] = (int16_t)(sound[i * 2 + 1] * f);
    }
}

static void feed_sound(int audio_ok)
{
    int room;
    unsigned frames;
    if (!audio_ok || sound_at >= SOUND_FRAMES) return;
    room = audsrv_available() / 4;
    if (room <= 0) return;
    frames = SOUND_FRAMES - sound_at;
    if (frames > (unsigned)room) frames = (unsigned)room;
    if (frames > 2048) frames = 2048;
    audsrv_play_audio((const char *)(sound + sound_at * 2), (int)(frames * 4));
    sound_at += frames;
}

/* ---- desenho ---- */
static inline void add_px(int x, int y, unsigned r, unsigned g, unsigned b)
{
    uint32_t p, nr, ng, nb;
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    p = canvas_px[y * W + x];
    nr = ((p >> 16) & 255) + r, ng = ((p >> 8) & 255) + g, nb = (p & 255) + b;
    canvas_px[y * W + x] = (nr > 255 ? 255 : nr) << 16 | (ng > 255 ? 255 : ng) << 8 | (nb > 255 ? 255 : nb);
}

static void fill(int x, int y, int w, int h, uint32_t rgb)
{
    int i, j;
    for (j = y; j < y + h; j++)
        if ((unsigned)j < H)
            for (i = x; i < x + w; i++)
                if ((unsigned)i < W) canvas_px[j * W + i] = rgb;
}

/* Linha com brilho: o nucleo e um halo, somados (aditivo). */
static void glow_line(float x0, float y0, float x1, float y1, unsigned r, unsigned g, unsigned b)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int steps = (int)len + 1, i;
    for (i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int x = (int)(x0 + dx * t), y = (int)(y0 + dy * t);
        add_px(x, y, r, g, b);
        add_px(x + 1, y, r / 2, g / 2, b / 2), add_px(x - 1, y, r / 2, g / 2, b / 2);
        add_px(x, y + 1, r / 2, g / 2, b / 2), add_px(x, y - 1, r / 2, g / 2, b / 2);
        add_px(x + 2, y, r / 6, g / 6, b / 6), add_px(x - 2, y, r / 6, g / 6, b / 6);
        add_px(x, y + 2, r / 6, g / 6, b / 6), add_px(x, y - 2, r / 6, g / 6, b / 6);
    }
}

static void glow_dot(int x, int y, unsigned r, unsigned g, unsigned b)
{
    add_px(x, y, r, g, b);
    add_px(x + 1, y, r / 3, g / 3, b / 3), add_px(x - 1, y, r / 3, g / 3, b / 3);
    add_px(x, y + 1, r / 3, g / 3, b / 3), add_px(x, y - 1, r / 3, g / 3, b / 3);
}

static uint32_t scale_rgb(uint32_t rgb, int level) /* level 0..256 */
{
    if (level <= 0) return 0;
    if (level >= 256) return rgb;
    return ((((rgb >> 16) & 255) * level >> 8) << 16) | ((((rgb >> 8) & 255) * level >> 8) << 8) |
           ((rgb & 255) * level >> 8);
}

static MenuCanvas canvas = {NULL, W, W, H, 0};

static int text_w(const char *text, int scale) { return Menu_TextWidthScaled(text, scale); }
static void text(int x, int y, const char *s, uint32_t rgb, int scale)
{
    Menu_DrawTextScaled(&canvas, x, y, s, 0xFF000000u | rgb, scale);
}
static void text_c(int y, const char *s, uint32_t rgb, int scale)
{
    text((W - text_w(s, scale)) / 2, y, s, rgb, scale);
}
/* Texto com sombra, para ler bem na TV. */
static void text_cs(int y, const char *s, uint32_t rgb, int scale)
{
    int x = (W - text_w(s, scale)) / 2;
    text(x + 2, y + 2, s, 0x000000, scale);
    text(x, y, s, rgb, scale);
}

/* Um brilho que corre pela faixa [y0, y1): clareia o que nao e fundo preto. */
static void sweep(int y0, int y1, int at)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = at - 30; x < at + 30; x++) {
            uint32_t p;
            int k;
            if ((unsigned)x >= W || (unsigned)y >= H) continue;
            p = canvas_px[y * W + x];
            if (!(p & 0xFFFFFF)) continue;
            k = 30 - (x > at ? x - at : at - x);
            add_px(x, y, k * 5, k * 5, k * 4);
        }
}

/* Para o GS (0xAABBGGRR), com o brilho 0..128 do fade. */
static void present(int bright)
{
    int i;
    unsigned k = (unsigned)bright * 2;
    for (i = 0; i < W * H; i++) {
        uint32_t p = canvas_px[i], r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        if (k < 256) r = r * k >> 8, g = g * k >> 8, b = b * k >> 8;
        canvas_px[i] = 0x80000000u | (b << 16) | (g << 8) | r;
    }
    GsGpu_IntroFrame(canvas_px);
}

/* ---- cena 1: a vinheta ---- */
#define PARTICLES 240
static float px_[PARTICLES], py_[PARTICLES], pa_[PARTICLES], pr_[PARTICLES];
static unsigned seed = 0x1234567u;
static float rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 8) & 0xFFFF) / 65535.0f;
}

static void cube(float cx, float cy, float size, float ay, float ax, int level)
{
    static const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                                     {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    float sx[8], sy[8];
    int i;
    for (i = 0; i < 8; i++) {
        float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f;
        float x1 = x * cosf(ay) + z * sinf(ay), z1 = -x * sinf(ay) + z * cosf(ay);
        float y1 = y * cosf(ax) - z1 * sinf(ax), z2 = y * sinf(ax) + z1 * cosf(ax);
        float k = 4.0f / (z2 + 6.0f);
        sx[i] = cx + x1 * size * k, sy[i] = cy + y1 * size * k * 0.9f;
    }
    for (i = 0; i < 12; i++)
        glow_line(sx[edges[i][0]], sy[edges[i][0]], sx[edges[i][1]], sy[edges[i][1]], 255 * level >> 8,
                  190 * level >> 8, 60 * level >> 8);
}

/* ---- cena 3: apoie o projeto ---- */
static void support_screen(int frame, int can_go)
{
    int x, y, i;
    for (y = 0; y < H; y++) {
        uint32_t c = (uint32_t)(10 + y * 14 / H) << 16 | (uint32_t)(12 + y * 6 / H) << 8 | (uint32_t)(40 + y * 30 / H);
        for (x = 0; x < W; x++) canvas_px[y * W + x] = c;
    }
    /* estrelas lentas */
    seed = 777;
    for (i = 0; i < 90; i++) {
        int sx = (int)(rnd() * W), sy = (int)(rnd() * H), tw = (int)(rnd() * 60);
        int k = 40 + (int)(40 * (1 + sinf((frame + tw * 7) * 0.05f)));
        add_px((sx + frame / 8) % W, sy, k, k, k + 20);
    }
    /* moldura dourada dupla */
    fill(24, 22, W - 48, 3, 0xC8962E), fill(24, H - 25, W - 48, 3, 0xC8962E);
    fill(24, 22, 3, H - 44, 0xC8962E), fill(W - 27, 22, 3, H - 44, 0xC8962E);
    fill(32, 30, W - 64, 1, 0x7A5A1C), fill(32, H - 31, W - 64, 1, 0x7A5A1C);
    fill(32, 30, 1, H - 60, 0x7A5A1C), fill(W - 33, 30, 1, H - 60, 0x7A5A1C);

    text_cs(64, "APOIE O PROJETO", 0xF2C14E, 3);
    fill(W / 2 - 170, 92, 340, 2, 0xC8962E);
    {
        static const struct { const char *t; uint32_t c; } lines[] = {
            {"Este port e gratuito", 0xFFFFFF}, {"e sempre vai ser.", 0xFFFFFF}, {"", 0},
            {"Se ele te divertiu,", 0xC8D0F0}, {"ajude a fazer os", 0xC8D0F0}, {"proximos ports, como", 0xC8D0F0},
            {"o Tekken 3 para PS2.", 0xC8D0F0}, {"", 0},
            {"Pix: aponte a camera", 0xF2C14E}, {"do celular para o QR.", 0xF2C14E}};
        for (i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
            if (lines[i].t[0]) text(56, 126 + i * 21, lines[i].t, lines[i].c, 1);
    }
    /* QR: 4 px por modulo, borda branca de 4 modulos */
    {
        const int m = 4, n = INTRO_QR_N, q = 4, size = (n + 2 * q) * m;
        int ox = W - 56 - size, oy = 108;
        fill(ox - 4, oy - 4, size + 8, size + 8, 0xC8962E);
        fill(ox, oy, size, size, 0xFFFFFF);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++)
                if (intro_qr[y][x] == '1') fill(ox + (q + x) * m, oy + (q + y) * m, m, m, 0x000000);
    }
    text_c(350, "Do exterior: ko-fi.com/guilhermesoti", 0x9FE0FF, 1);
    text_c(372, "github.com/guilhermesoti/yugiohps1praps2", 0x9FE0FF, 1);
    if (can_go && (frame / 30) % 2 == 0) text_cs(H - 48, "Pressione X ou START para jogar", 0xFFFFFF, 1);
    else if (!can_go) text_cs(H - 48, "Retrogames Patos de Minas", 0x8890B0, 1);
}

static unsigned read_pad(int pad_ok)
{
    struct padButtonStatus status;
    int state;
    if (!pad_ok) return 0;
    state = padGetState(0, 0);
    if ((state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) && padRead(0, 0, &status) > 0)
        return 0xFFFFu ^ status.btns;
    return 0;
}

void Ps2_Intro(int pad_ok, int audio_ok, int fast)
{
    enum { A_START = 20, LOCK = 110, A_FADE = 300, B_START = 330, B_FADE = 450, C_START = 480 };
    unsigned held = 0xFFFF, now;
    int f, i, leaving = -1, bright, skip = fast ? C_START : 0, leave_at = 0;
    uint64_t t0;
    audsrv_fmt_t format = {RATE, 16, 2};
    canvas_px = memalign(128, W * H * 4);
    if (!canvas_px) return;
    canvas.pixels = canvas_px;
    printf("ps2: intro: fonte\n");
    Menu_PrepareFont();
    printf("ps2: intro: som\n");
    sound = audio_ok ? malloc(SOUND_FRAMES * 4) : NULL;
    if (sound && !audsrv_set_format(&format)) {
        audsrv_set_volume(MAX_VOLUME);
        synth();
    } else {
        audio_ok = 0;
    }
    printf("ps2: intro: quadros\n");
    for (i = 0; i < PARTICLES; i++) {
        pa_[i] = rnd() * 6.2831853f, pr_[i] = 260 + rnd() * 260;
        px_[i] = py_[i] = 0;
    }
    /* O quadro da animacao vem do relogio (60 por segundo), para a duracao
     * ser a mesma mesmo quando um desenho perde um vsync. */
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        t0 = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
    }
    for (;;) {
        {
            struct timespec ts;
            uint64_t us;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            us = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
            f = (int)((us - t0) * 60u / 1000000u) + skip;
        }
        now = read_pad(pad_ok);
        /* so vale botao apertado depois de solto (o START que abriu o ELF nao conta) */
        {
            unsigned pressed = now & ~held;
            held = now;
            if (f < C_START && (pressed & PAD_START)) skip += C_START - f, f = C_START;
            if (f >= C_START + 60 && leaving < 0 && (pressed & (PAD_START | PAD_CROSS))) leaving = 0;
        }
        if (f >= C_START + 60 * 20 && leaving < 0) leaving = 0;
        if (f >= A_START) feed_sound(audio_ok && f < C_START);
        memset(canvas_px, 0, W * H * 4);
        bright = 128;
        if (f < A_START) {
            /* preto */
        } else if (f < B_START) {
            float t = (float)(f - A_START);
            int level = f < LOCK ? 0 : 256;
            /* particulas em espiral ate o centro */
            for (i = 0; i < PARTICLES; i++) {
                float k = f < LOCK ? 1.0f - (float)(f - A_START) / (LOCK - A_START) : 0.0f;
                float r = pr_[i] * k * k, a = pa_[i] + t * 0.04f + (1 - k) * 3.0f;
                int x = W / 2 + (int)(r * cosf(a)), y = 170 + (int)(r * sinf(a) * 0.75f);
                if (f < LOCK) glow_dot(x, y, 160, 120, 50);
                else if (f < LOCK + 40) {
                    /* estouro: as particulas saem de novo, sumindo */
                    float e = (float)(f - LOCK) * 6.0f * (0.5f + pr_[i] / 520.0f);
                    int fade = 200 - (f - LOCK) * 5;
                    x = W / 2 + (int)(e * cosf(pa_[i])), y = 170 + (int)(e * sinf(pa_[i]) * 0.75f);
                    if (fade > 0) glow_dot(x, y, (unsigned)fade, (unsigned)fade * 3 / 4, (unsigned)fade / 3);
                }
            }
            if (f >= LOCK - 30) {
                int grow = f < LOCK ? (f - (LOCK - 30)) * 256 / 30 : 256;
                cube(W / 2, 170, 70.0f * (0.4f + 0.6f * grow / 256.0f), t * 0.035f, 0.5f + 0.25f * sinf(t * 0.02f),
                     f < LOCK ? grow / 2 : level);
            }
            if (f >= LOCK && f < LOCK + 12) {
                /* clarao */
                int k = (LOCK + 12 - f) * 20;
                for (i = 0; i < W * H; i++) {
                    uint32_t p = canvas_px[i];
                    unsigned r = ((p >> 16) & 255) + k, g = ((p >> 8) & 255) + k, b = (p & 255) + k;
                    canvas_px[i] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
                }
            }
            if (f >= LOCK + 20) {
                int a = (f - (LOCK + 20)) * 8;
                text_c(300, "RETROGAMES", scale_rgb(0xF2C14E, a), 4);
                if (f >= LOCK + 40 && f < LOCK + 130) sweep(270, 330, 80 + (f - (LOCK + 40)) * 6);
            }
            if (f >= LOCK + 60) text_c(345, "Patos de Minas - MG", scale_rgb(0xC8D0F0, (f - (LOCK + 60)) * 8), 1);
            if (f >= LOCK + 100) text_c(385, "apresenta", scale_rgb(0x9098B8, (f - (LOCK + 100)) * 6), 1);
            if (f >= A_FADE) bright = 128 - (f - A_FADE) * 128 / (B_START - A_FADE);
        } else if (f < C_START) {
            int a = (f - B_START) * 6;
            text_cs(150, "Yu-Gi-Oh!", scale_rgb(0xF2C14E, a), 4);
            text_cs(208, "Forbidden Memories", scale_rgb(0xFFFFFF, a), 2);
            fill(W / 2 - 160, 238, 320, 2, scale_rgb(0xC8962E, a));
            text_c(272, "Port nativo para PlayStation 2", scale_rgb(0xC8D0F0, a - 120), 1);
            text_c(320, "versao 0.1 beta  -  projeto de fa, gratuito", scale_rgb(0x8890B0, a - 200), 1);
            if (f < B_START + 20) bright = (f - B_START) * 128 / 20;
            if (f >= B_FADE) bright = 128 - (f - B_FADE) * 128 / (C_START - B_FADE);
        } else {
            support_screen(f - C_START, f >= C_START + 60);
            if (f < C_START + 20) bright = (f - C_START) * 128 / 20;
        }
        if (leaving == 0) leave_at = f, leaving = 1;
        if (leaving > 0) {
            bright = 128 - (f - leave_at) * 128 / 20;
            if (f - leave_at > 20) break;
        }
        if (bright < 0) bright = 0;
        present(bright);
    }
    memset(canvas_px, 0, W * H * 4);
    present(0);
    if (audio_ok) audsrv_stop_audio();
    GsGpu_IntroFrame(NULL);
    free(sound);
    free(canvas_px);
    sound = NULL, canvas_px = NULL;
}

/* The PS2 platform (src/pc/platform/platform.h): the GS shows the PS1 GPU's
 * picture, the DualShock is the PS1 pad, audsrv plays the SPU's mix, and the
 * game's files are on the USB stick (or host: in an emulator). It stands in
 * for src/pc/platform/sdl.c; the frame clock is platform_common.c's
 * cooperative one.
 *
 * Startup (main, below): reset the IOP, load the drivers, find the disc
 * image beside the ELF, then the port's own main (src/pc/guest/main.c,
 * built as Memories_PcMain). */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <audsrv.h>
extern int fileXioInit(void); /* newlib then goes through fileXio (iomanX devices: mass:) */
#include <gsKit.h>
#include <dmaKit.h>
#include <debug.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pc/platform/platform.h"
#include "pc/platform/controls_runtime.h"
#include "pc/platform/menu.h"
#include "pc/platform/loading.h"
#include "pc/cards/cards.h"
#include "ps2.h"
#include "pc/saves/save_menu.h"
#include "gs_gpu.h"

extern int Memories_PcMain(int argc, char **argv);
extern void Cheats_Frame(void);
extern void Duelists_Frame(void);
extern void Tower_Frame(void);

/* ---- the on-screen log (stdout too: an emulator's console) ---- */
#define LOG_LINES 8
static char log_lines[LOG_LINES][64];
static unsigned log_next;

void Ps2_Log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(log_lines[log_next % LOG_LINES], sizeof(log_lines[0]), format, args);
    va_end(args);
    printf("ps2: %s\n", log_lines[log_next % LOG_LINES]);
    log_next++;
}

static int video_open;
static void video_close(void);

/* ---- the boot log: on screen until the game's picture takes over, and in
 * YFM-LOG.txt beside the ELF (closed and reopened at each step, so what it
 * says survives a hang: the card's file size is only written at a close). */
static char log_path[300];
static int boot_screen;
void Ps2_LogSync(void)
{
    if (!log_path[0]) return;
    fflush(stdout);
    if (!freopen(log_path, "a", stdout)) log_path[0] = 0;
    else setvbuf(stdout, NULL, _IOLBF, 0);
}
/* A boot step that may hang on some console: named in YFM-SAVE/passo.txt
 * before it is tried, so the next boot skips it (Ps2_SkipStep). */
static char step_path[300], failed_step[32];
void Ps2_RiskyStep(const char *name)
{
    FILE *file;
    if (!step_path[0]) return;
    if ((file = fopen(step_path, "w"))) {
        fputs(name, file);
        fclose(file);
    }
}
int Ps2_SkipStep(const char *name) { return failed_step[0] && !strcmp(failed_step, name); }
void Ps2_Stage(const char *format, ...)
{
    char line[160];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("ps2: stage: %s\n", line);
    if (boot_screen) scr_printf("  %s\n", line);
    Ps2_LogSync();
}

void Ps2_Fatal(const char *message)
{
    unsigned i;
    printf("ps2: FATAL: %s\n", message);
    Ps2_LogSync();
    video_close();
    init_scr();
    scr_printf("\n\n  Yu-Gi-Oh! Forbidden Memories - PS2\n\n  %s\n\n", message);
    for (i = log_next > LOG_LINES ? log_next - LOG_LINES : 0; i < log_next; i++)
        scr_printf("  %s\n", log_lines[i % LOG_LINES]);
    for (;;) SleepThread();
}

/* ---- IOP modules ---- */
#define IRX(name) extern unsigned char irx_##name[], irx_##name##_end[]
IRX(iomanx); IRX(filexio); IRX(usbd); IRX(bdm); IRX(bdmfs); IRX(usbmass); IRX(libsd); IRX(audsrv); IRX(sio2man);
IRX(padman);

static int load_buffer(const char *name, unsigned char *start, unsigned char *end)
{
    int result = 0, id = SifExecModuleBuffer(start, (unsigned)(end - start), 0, NULL, &result);
    if (id < 0 || result == 1) {
        printf("ps2: module %s: %d (%d)\n", name, id, result);
        return -1;
    }
    return 0;
}

static int pad_ok, audio_ok, usb_ok;
static unsigned char pad_buffer[256] __attribute__((aligned(64)));

static void iop_start(void)
{
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
    if (!load_buffer("iomanX", irx_iomanx, irx_iomanx_end) && !load_buffer("fileXio", irx_filexio, irx_filexio_end))
        fileXioInit();
    usb_ok = !load_buffer("usbd", irx_usbd, irx_usbd_end) && !load_buffer("bdm", irx_bdm, irx_bdm_end) &&
             !load_buffer("bdmfs", irx_bdmfs, irx_bdmfs_end) && !load_buffer("usbmass", irx_usbmass, irx_usbmass_end);
    /* The pad driver the SDK's libpad speaks to: its own, as the SDK built
     * it; the console's newer ROM one if that cannot load; the old ROM one
     * last (the 0.1 prototype found consoles where one or the other worked). */
    if (!load_buffer("sio2man", irx_sio2man, irx_sio2man_end) && !load_buffer("padman", irx_padman, irx_padman_end))
        pad_ok = 1;
    else if (SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:XPADMAN", 0, NULL) >= 0)
        pad_ok = 2;
    else if (SifLoadModule("rom0:SIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:PADMAN", 0, NULL) >= 0)
        pad_ok = 3;
    if (pad_ok && (padInit(0) < 0 || padPortOpen(0, 0, pad_buffer) <= 0)) pad_ok = 0;
    audio_ok = !load_buffer("libsd", irx_libsd, irx_libsd_end) && !load_buffer("audsrv", irx_audsrv, irx_audsrv_end) &&
               audsrv_init() == 0;
    printf("ps2: IOP ready (usb %d, pad %d, audio %d)\n", usb_ok, pad_ok, audio_ok);
}

/* ---- video ---- */
static GSGLOBAL *gs;
static GSTEXTURE picture;
static uint16_t *picture16;
static uint32_t *picture32;
#define PICTURE_W 640
#define PICTURE_H 512

static void video_start(void)
{
    if (video_open) return;
    gs = gsKit_init_global();
    gs->Mode = GS_MODE_NTSC;
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;
    gs->Width = 640;
    gs->Height = 448;
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    picture16 = memalign(128, PICTURE_W * PICTURE_H * 2);
    picture32 = memalign(128, PICTURE_W * PICTURE_H * 4);
    picture.Width = PICTURE_W;
    picture.Height = PICTURE_H;
    picture.PSM = GS_PSM_CT32;
    picture.Filter = GS_FILTER_LINEAR;
    picture.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(PICTURE_W, PICTURE_H, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    video_open = 1;
}

static void video_close(void)
{
    if (!video_open) return;
    video_open = 0;
}

static void show(int w, int h, int psm, void *pixels)
{
    picture.Width = w;
    picture.Height = h;
    picture.PSM = psm;
    picture.Mem = pixels;
    gsKit_texture_upload(gs, &picture);
    gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    gsKit_prim_sprite_texture(gs, &picture, 0.0f, 0.0f, 0.0f, 0.0f, (float)gs->Width, (float)gs->Height,
                              (float)w, (float)h, 1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
}

void Platform_Present(const uint16_t *vram, int stride, int x, int y, int w, int h, int rgb24)
{
    int i, j;
    static unsigned presents;
    if (GsGpu_Active()) {
        if (++presents == 1) Ps2_Stage("primeira imagem %dx%d%s", w, h, rgb24 ? " (24-bit)" : "");
        if (presents == 120) {
            /* Booted: what was skipped stays skipped (passo.txt keeps its name). */
            Ps2_RiskyStep(failed_step[0] ? failed_step : "ok");
            Ps2_Stage("120 quadros ok");
        }
        GsGpu_Present(vram, x, y, w, h, rgb24);
        return;
    }
    if (!video_open || w <= 0 || h <= 0) return;
    if (w > PICTURE_W) w = PICTURE_W;
    if (h > PICTURE_H) h = PICTURE_H;
    if (rgb24) {
        for (j = 0; j < h; j++) {
            const uint8_t *bytes = (const uint8_t *)(vram + ((y + j) & 511) * stride + x);
            uint32_t *out = picture32 + j * w;
            for (i = 0; i < w; i++, bytes += 3) out[i] = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | 0x80000000u;
        }
        FlushCache(0);
        show(w, h, GS_PSM_CT32, picture32);
    } else {
        for (j = 0; j < h; j++) {
            const uint16_t *row = vram + ((y + j) & 511) * stride;
            uint16_t *out = picture16 + j * w;
            if (x + w <= 1024) memcpy(out, row + x, (size_t)w * 2);
            else for (i = 0; i < w; i++) out[i] = row[(x + i) & 1023];
            for (i = 0; i < w; i++) out[i] |= 0x8000u;
        }
        FlushCache(0);
        show(w, h, GS_PSM_CT16, picture16);
    }
    if (++presents == 1) printf("ps2: first picture %dx%d%s\n", w, h, rgb24 ? " (24-bit)" : "");
}

int Platform_PresentPicture(const uint32_t *pixels, int stride, int x, int y, int w, int h, int scale)
{
    (void)pixels; (void)stride; (void)x; (void)y; (void)w; (void)h; (void)scale;
    return 0;
}
int Platform_ReadPicture(uint32_t *out, int x, int y, int w, int h) { (void)out; (void)x; (void)y; (void)w; (void)h; return 0; }
int Platform_PresentWidePicture(int x, int y, int w, int h, int wide_w, int scale)
{
    (void)x; (void)y; (void)w; (void)h; (void)wide_w; (void)scale;
    return 0;
}
int Platform_ReadWidePicture(uint32_t *out, int x, int y, int w, int h, int wide_w, int scale)
{
    (void)out; (void)x; (void)y; (void)w; (void)h; (void)wide_w; (void)scale;
    return 0;
}
int Platform_Widescreen(void) { return 0; }
void Platform_SetScale(int scale) { (void)scale; }
void Platform_ApplyDisplaySettings(void) {}
int Platform_HasWindowModes(void) { return 0; }
void Platform_Screenshot(int window_image) { (void)window_image; }
int Platform_CopyText(const char *text) { (void)text; return 0; }

/* ---- the window's other duties ---- */
static const LoadingSink loading_sink = {Platform_ShowLoading, Platform_EndLoading, NULL};

int Platform_Open(const char *title)
{
    (void)title;
    ControlsRuntime_Init();
    Menu_LoadSettingsWithoutMods();
    Menu_LoadMods();
    Ps2_Stage("video: GS");
    boot_screen = 0;
    if (getenv("PS2_SOFT_PICTURE")) video_start();
    else GsGpu_Init();
    Ps2_Stage("video ligado");
    Loading_Begin(&loading_sink);
    return 0;
}

int Platform_ShowLoading(const char *line, unsigned done, unsigned total)
{
    static char last[64];
    if (line && strncmp(last, line, sizeof(last) - 1)) {
        snprintf(last, sizeof(last), "%s", line);
        printf("ps2: loading: %s\n", line);
    }
    (void)done; (void)total;
    return -1;
}
void Platform_EndLoading(void) {}

void Platform_ShowError(const char *title, const char *message)
{
    char text[512];
    snprintf(text, sizeof(text), "%s:\n  %s", title ? title : "Error", message ? message : "");
    Ps2_Fatal(text);
}

int Platform_SelectDisc(char *path, size_t size, char *why, size_t why_size)
{
    (void)path; (void)size;
    if (why && why_size && !why[0]) snprintf(why, why_size, "no disc image");
    return -1;
}

void Platform_OpenMods(void) {}
void Platform_OpenControls(void) {}
int Platform_OpenFolder(const char *path) { (void)path; return -1; }
int Platform_OpenUrl(const char *url) { (void)url; return -1; }
static volatile int quit_asked;
int Platform_ShouldQuit(void) { return quit_asked; }
void Platform_RequestQuit(void) { quit_asked = 1; }
static int state_slot = 1;
int Platform_StateSlot(void) { return state_slot; }
void Platform_SetStateSlot(int slot) { state_slot = slot; }
int Platform_WindowPointer(int *x, int *y, unsigned *buttons) { (void)x; (void)y; (void)buttons; return 0; }
void Platform_PumpEvents(void) {}

/* ---- the pad ---- */
static volatile uint16_t pad_bits;

static void poll_pad(void)
{
    struct padButtonStatus status;
    int state;
    if (!pad_ok) return;
    state = padGetState(0, 0);
    if ((state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) && padRead(0, 0, &status) > 0)
        pad_bits = (uint16_t)(0xFFFFu ^ status.btns);
}

static unsigned current_frame;
/* Emulator runs (host:): a command file the developer writes while the game
 * runs, "<id>\n<frames>:<bits hex>,..." -- each step holds the pad bits for
 * that many frames, from when a new id is seen. */
static int live_input;
static uint16_t live_bits;
static struct { unsigned frames; uint16_t bits; } live_steps[256];
static unsigned live_count, live_at, live_left;

static void poll_live_input(void)
{
    static char last_id[32];
    char text[4096], *at;
    FILE *file;
    size_t got;
    if (!live_input || current_frame % 20) return;
    if (!(file = fopen("host:cmd.txt", "rb"))) return;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    at = strchr(text, '\n');
    if (!at) return;
    *at++ = '\0';
    if (!strcmp(text, last_id)) return;
    snprintf(last_id, sizeof(last_id), "%s", text);
    live_count = live_at = live_left = 0;
    while (*at && live_count < 256) {
        char *end;
        unsigned long frames = strtoul(at, &end, 10);
        if (*end != ':') break;
        live_steps[live_count].frames = (unsigned)frames;
        live_steps[live_count].bits = (uint16_t)strtoul(end + 1, &end, 16);
        live_count++;
        at = *end == ',' ? end + 1 : end;
        while (*at == '\n' || *at == '\r' || *at == ' ') at++;
    }
    printf("ps2: live input %s: %u steps at frame %u\n", last_id, live_count, current_frame);
}

static void step_live_input(void)
{
    while (live_at < live_count && !live_left) {
        live_bits = live_steps[live_at].bits;
        live_left = live_steps[live_at].frames;
        live_at++;
    }
    if (live_left) live_left--;
    else live_bits = 0;
}

uint16_t Platform_Pad(int port)
{
    return port == 0 ? (uint16_t)(pad_bits | live_bits | ControlsRuntime_Keyboard() | Platform_ScriptedBits(current_frame))
                     : 0;
}
uint16_t Platform_PadFixedBits(int port) { (void)port; return 0; }
int Platform_PadConnected(int port) { return port == 0; }

/* ---- audio: the SPU's mix (22.05 kHz stereo) through audsrv, on the game's
 * thread at each frame: the mixer reads what the game's sound driver wrote,
 * which only changes on this thread too. ---- */
static void (*mixer)(int16_t *frames, size_t count);
static int16_t mix_buffer[2048 * 2] __attribute__((aligned(64)));
static unsigned underruns;
static uint64_t audio_clock_us;

static uint64_t now_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static void pump_audio(void)
{
    int room;
    uint64_t now;
    size_t frames;
    if (!mixer || !audio_ok) return;
    now = now_us();
    if (!audio_clock_us) audio_clock_us = now;
    frames = (size_t)((now - audio_clock_us) * 22050u / 1000000u);
    if (frames < 64) return;
    audio_clock_us += (uint64_t)frames * 1000000u / 22050u;
    room = audsrv_available() / 4;
    if ((int)frames > room) {
        frames = room > 0 ? (size_t)room : 0;
        underruns++;
    }
    while (frames) {
        size_t part = frames > 2048 ? 2048 : frames;
        mixer(mix_buffer, part);
        audsrv_play_audio((const char *)mix_buffer, (int)(part * 4));
        frames -= part;
    }
}

int Platform_StartAudio(void (*mix)(int16_t *frames, size_t count))
{
    audsrv_fmt_t format = {22050, 16, 2}; /* spu.c mixes at half the SPU's rate on the PS2 */
    if (!audio_ok || audsrv_set_format(&format)) {
        printf("ps2: no audio\n");
        return -1;
    }
    audsrv_set_volume(MAX_VOLUME);
    mixer = mix;
    return 0;
}

void Platform_AudioStats(int *queued_frames, unsigned *underrun_count)
{
    if (queued_frames) *queued_frames = 0;
    if (underrun_count) *underrun_count = underruns;
}

/* Once a presented frame (libgpu.c). */
void Platform_Frame(unsigned frame)
{
    current_frame = frame;
    poll_pad();
    poll_live_input();
    step_live_input();
    pump_audio();
    Cheats_Frame();
    Cards_Frame();
    Duelists_Frame();
    Tower_Frame();
    /* Anything that left supervisor mode (an SDK call that entered and left
     * kernel mode) is put back before the game touches its memory again. */
    Ps2_EnterGuestMode();
    if (frame == 1 || frame % 600 == 0) printf("ps2: frame %u\n", frame);
    if (frame <= 1800 && (frame <= 3 || frame % 60 == 0)) Ps2_LogSync();
    {
        extern int Ps2_ModelCalls;
        static int model_dumped, model_frames;
        if (live_input && Ps2_ModelCalls && !model_dumped && ++model_frames == 40) {
            model_dumped = 1;
            printf("ps2: the models ran (%d calls): dump\n", Ps2_ModelCalls);
            live_bits = 0xFFFF;
        }
    }
    if (live_bits == 0xFFFD) {
        static unsigned char test_buffer[0x4000];
        live_bits = 0;
        int sound;
        SaveMenu_Begin(SAVE_MENU_SAVE, test_buffer + 0x200, NULL, 0x2000, "BASLUS-01411YUGIOH", NULL);
        SaveMenu_Poll(0, 0, &sound, NULL);
    }
    if (live_bits == 0xFFFE) {
        extern int GsGpu_FrameLog;
        live_bits = 0;
        GsGpu_FrameLog = 240;
    }
    if ((getenv("PS2_DUMP_FRAME") && frame == (unsigned)atoi(getenv("PS2_DUMP_FRAME"))) || live_bits == 0xFFFF) {
        live_bits = 0;
        static uint16_t dump[1024 * 512] __attribute__((aligned(64)));
        FILE *file;
        GsGpu_ReadVram(dump);
        if ((file = fopen("host:gsvram.bin", "wb"))) {
            fwrite(dump, 2, 1024 * 512, file);
            fclose(file);
        }
        if ((file = fopen("host:cpuvram.bin", "wb"))) {
            extern const uint16_t *SoftGpu_Vram(void);
            fwrite(SoftGpu_Vram(), 2, 1024 * 512, file);
            fclose(file);
        }
        printf("ps2: dumped VRAM at frame %u\n", frame);
        {
            extern int GsGpu_Trace;
            GsGpu_Trace = 400;
        }
    }
    if (frame % 120 == 0) {
        unsigned tiles, count = GsGpu_Stats(&tiles);
        printf("ps2: gs: %u primitives, %u texture tiles decoded in 120 frames\n", count, tiles);
    }
}

/* ---- startup ---- */
static char program_dir[256];

static int exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static const char *find_disc(void)
{
    static const char *const names[] = {"YFM.bin", "YFM.BIN", "fm.bin", "Yu-Gi-Oh! Forbidden Memories (USA).bin",
                                        "game/YFM.bin", "game/Yu-Gi-Oh! Forbidden Memories (USA).bin", NULL};
    static char path[512];
    const char *const *name;
    for (name = names; *name; name++) {
        snprintf(path, sizeof(path), "%s/%s", program_dir, *name);
        if (exists(path)) return path;
    }
    for (name = names; *name; name++) {
        snprintf(path, sizeof(path), "host:%s", *name);
        if (exists(path)) return path;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    char *slash;
    const char *disc;
    char user[300];
    static char *pc_argv[2] = {"memories-ps2", NULL};
    printf("ps2: Yu-Gi-Oh! Forbidden Memories PS2 port starting (%s)\n", argc > 0 ? argv[0] : "?");
    iop_start();
    init_scr();
    boot_screen = 1;
    scr_printf("\n\n  Yu-Gi-Oh! Forbidden Memories - PS2 (debug)\n\n");
    Ps2_Stage("IOP: usb %d, controle %d, audio %d", usb_ok, pad_ok, audio_ok);
    {
        extern void Ps2_InstallFaultHandlers(void);
        Ps2_InstallFaultHandlers();
    }
    snprintf(program_dir, sizeof(program_dir), "%s", argc > 0 && argv[0] ? argv[0] : "host:");
    slash = strrchr(program_dir, '/');
    if (!slash) slash = strrchr(program_dir, '\\');
    if (slash) *slash = '\0';
    else if ((slash = strchr(program_dir, ':'))) slash[1] = '\0';
    if (!strncmp(program_dir, "mass:", 5) || !strncmp(program_dir, "host", 4)) {
        /* fine as it is */
    } else if (!strncmp(program_dir, "cdrom", 5) || !program_dir[0]) {
        snprintf(program_dir, sizeof(program_dir), "mass:/YFM");
    }
    Ps2_Stage("pasta: %s", program_dir);
    /* The USB stick needs a moment to appear after its driver loads. */
    disc = find_disc();
    if (!disc && usb_ok) {
        int tries;
        for (tries = 0; tries < 20 && !(disc = find_disc()); tries++) {
            struct timespec wait = {0, 250000000};
            nanosleep(&wait, NULL);
        }
    }
    if (!disc) {
        char text[700];
        snprintf(text, sizeof(text), "The disc image was not found.\n\n  Put YFM.bin (your Forbidden Memories USA .bin)\n"
                                     "  next to the ELF:\n  %s/YFM.bin", program_dir);
        Ps2_Fatal(text);
    }
    if (strncmp(program_dir, "host", 4)) {
        snprintf(log_path, sizeof(log_path), "%s/YFM-LOG.txt", program_dir);
        if (freopen(log_path, "w", stdout)) {
            setvbuf(stdout, NULL, _IOLBF, 0);
            stderr = stdout;
        } else {
            log_path[0] = 0;
        }
    }
    Ps2_Stage("disco: %s", disc);
    if (strncmp(program_dir, "host", 4)) {
        FILE *file;
        char dir[300];
        snprintf(dir, sizeof(dir), "%s/YFM-SAVE", program_dir);
        mkdir(dir, 0777);
        snprintf(step_path, sizeof(step_path), "%s/YFM-SAVE/passo.txt", program_dir);
        if ((file = fopen(step_path, "r"))) {
            size_t got = fread(failed_step, 1, sizeof(failed_step) - 1, file);
            fclose(file);
            failed_step[got] = 0;
            if (!strcmp(failed_step, "ok")) failed_step[0] = 0;
            else if (!strcmp(failed_step, "espelho")) {
                /* the low mirror hung once; then the supervisor mode is next */
            }
        }
        if (failed_step[0]) Ps2_Stage("a ultima vez travou em '%s': pulando", failed_step);
    }
    {
        struct timespec a, b;
        volatile unsigned spin;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (spin = 0; spin < 3000000; spin++) {}
        clock_gettime(CLOCK_MONOTONIC, &b);
        printf("ps2: clock %ld.%09ld -> %ld.%09ld\n", (long)a.tv_sec, a.tv_nsec, (long)b.tv_sec, b.tv_nsec);
    }
    setenv("MEMORIES_DISC", disc, 1);
    setenv("MEMORIES_PROGRAM_DIR", program_dir, 1);
    if (!strcmp(program_dir, "host:")) snprintf(program_dir, sizeof(program_dir), "host:.");
    snprintf(user, sizeof(user), "%s/YFM-SAVE", program_dir);
    setenv("MEMORIES_USER_DIR", user, 1);
    setenv("MEMORIES_NO_MONITOR", "1", 1);
    if (!strncmp(program_dir, "host:", 5)) {
        extern void Ps2_StartSampler(void);
        FILE *script = fopen("host:input.txt", "rb");
        live_input = 1;
        setenv("PS2_GUEST_DUMP", "host:dumps/g", 1);
        setenv("MEMORIES_TRACE", "frames,disc,stub,window,clock,mods,input,model", 1);
        if (script) {
            static char input[4096];
            size_t got = fread(input, 1, sizeof(input) - 1, script);
            fclose(script);
            while (got && (input[got - 1] == '\n' || input[got - 1] == '\r')) got--;
            input[got] = '\0';
            setenv("MEMORIES_INPUT", input, 1);
            printf("ps2: scripted input %s\n", input);
        }
        if ((script = fopen("host:selftest.txt", "rb"))) {
            fclose(script);
            setenv("PS2_GS_SELFTEST", "1", 1);
        }
        if ((script = fopen("host:dump.txt", "rb"))) {
            static char at[32];
            size_t got = fread(at, 1, sizeof(at) - 1, script);
            fclose(script);
            at[got] = 0;
            setenv("PS2_DUMP_FRAME", at, 1);
        }
        Ps2_StartSampler();
    }
    return Memories_PcMain(1, pc_argv);
}

/* ---- the port's menus over the picture (gs_gpu.c draws it) ---- */
#include "pc/platform/menu.h"
#include "pc/saves/save_menu.h"
#include "pc/saves/deck_menu.h"
static uint32_t overlay_argb[320 * 240];
static uint32_t overlay_gs[320 * 240] __attribute__((aligned(64)));
const uint32_t *Ps2_OverlayPicture(int *changed)
{
    static unsigned last_signature;
    static int was_shown;
    int shown = SaveMenu_Active() || DeckMenu_Active() || Menu_NoticeShown();
    unsigned signature;
    if (!shown) {
        was_shown = 0;
        return NULL;
    }
    signature = SaveMenu_Signature() * 2654435761u ^ DeckMenu_Signature() * 40503u ^ (unsigned)Menu_NoticeShown();
    *changed = !was_shown || signature != last_signature;
    if (*changed) {
        MenuCanvas canvas = {overlay_argb, 320, 320, 240, 1};
        int x, y, w, h, i;
        memset(overlay_argb, 0, sizeof(overlay_argb));
        Menu_SetOverlayArea(0, 0, 0);
        SaveMenu_Draw(&canvas, &x, &y, &w, &h);
        if (!w || !h) DeckMenu_Draw(&canvas, &x, &y, &w, &h);
        if (Menu_NoticeShown()) Menu_Draw(&canvas);
        for (i = 0; i < 320 * 240; i++) {
            uint32_t p = overlay_argb[i], a = p >> 24;
            overlay_gs[i] = ((p >> 16) & 0xFF) | (p & 0xFF00) | ((p & 0xFF) << 16) | (((a + 1) >> 1) << 24);
        }
        {
            unsigned nz = 0, op = 0;
            for (i = 0; i < 320 * 240; i++) nz += overlay_argb[i] != 0, op += (overlay_argb[i] >> 24) != 0;
            printf("ps2: overlay drawn %d,%d %dx%d: %u pixels, %u with alpha\n", x, y, w, h, nz, op);
        }
        last_signature = signature;
        was_shown = 1;
    }
    return overlay_gs;
}

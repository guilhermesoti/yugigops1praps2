/* The PS2's guest memory: the console's RAM, the scratchpad and the game's
 * stacks in a TLB window at 0xC0000000 (force.h), the retail executable's
 * image, the resolver native code reaches guest addresses through, calls
 * into guest code (Memories_Icall, thunk.S), and what the PC's fixed-address
 * engine (src/pc/guest/image.c, state.c) gives the rest of the port.
 *
 * Window layout (two TLB entries of two 1 MiB pages each):
 *   0xC0000000-0xC01FFFFF  guest RAM (retail 0x80000000-0x801FFFFF)
 *   0xC0200000-0xC02FFFFF  the game's stack (the PC's 0xB0000000)
 *   0xC0300000-0xC03003FF  the scratchpad (retail 0x1F800000)
 *   0xC0340000-0xC037FFFF  the MIPS interpreter's stack (mips.c)
 * Every address there is negative, as the console's KSEG0 ones are: the game
 * tells a pointer from a small number by its sign in places. The EE reaches
 * 0xC0000000 in supervisor or kernel mode only; the game runs in supervisor
 * mode, where a stray retail address (KSEG0) still faults. */
#include <kernel.h>
extern void *_gp;
#include <ee_debug.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#undef fread
#undef fwrite
#include "pc/guest/image.h"
#include "pc/guest/image_loader.h"
#include "pc/guest/function_map.h"
#include "pc/guest/mips.h"
#include "pc/guest/state.h"
#include "pc/memory.h"
#include "pc/compat/mman.h"
#include "ps2.h"

#define WINDOW 0xC0000000u
#define WINDOW_SIZE 0x400000u
#define GAME_STACK_TOP (WINDOW + 0x300000u - 64u)

/* The window's memory is the 3 MiB below the program, which ps2/linkfile
 * starts at 0x00400000: guest RAM at physical 0x00200000, the scratchpad
 * and the interpreter's stack at 0x00100000. */
#define GUEST_PHYSICAL 0x00200000u
#define STACKS_PHYSICAL 0x00100000u
int Memories_ScratchpadRetailView; /* 0: the interpreter hands canonical words over (mips.c, HANDED) */
static int mapped;

static uint32_t entry_lo(uint32_t physical)
{
    /* PFN, cacheable (3), dirty (writable), valid, global. */
    return ((physical >> 12) << 6) | (3u << 3) | (1u << 2) | (1u << 1) | 1u;
}

unsigned Ps2_GuestMode = 0x08;

void Ps2_EnterGuestMode(void)
{
    /* Supervisor: 0xC0000000-0xDFFFFFFF and the user segment, not KSEG0
     * (kernel mode if the fault handler found supervisor refused). */
    ee_set_opmode(Ps2_GuestMode);
}

int Memories_GuestMap(void)
{
    uint32_t physical = GUEST_PHYSICAL;
    int a, b;
    if (mapped) return 0;
    FlushCache(0);
    /* The kernel's own entries are 0-38 (scratchpad, kernel, default and
     * extended maps); 40 and 41 are free on every model. */
    a = _SetTLBEntry(40, 0x001FE000u, WINDOW, entry_lo(physical), entry_lo(physical + 0x100000u));
    b = _SetTLBEntry(41, 0x001FE000u, WINDOW + 0x200000u, entry_lo(STACKS_PHYSICAL), entry_lo(STACKS_PHYSICAL));
    printf("ps2: guest window 0x%08x -> physical 0x%08x (TLB %d, %d)\n", WINDOW, (unsigned)physical, a, b);
    Ps2_Stage("memoria: TLB %d %d", a, b);
    /* The console's RAM is also at its physical addresses, from 0: a null
     * pointer reads its first page, and a 24-bit packet link is a physical
     * address. The kernel maps nothing below 0x00080000 and the program's
     * own memory 1:1 above; the first 2 MiB become guest RAM (the program
     * starts at 0x00400000, ps2/linkfile), as on the PC (image.c's mirror). */
    /* Only entry 47 (VA 0-0x7FFFF, which the kernel leaves unmapped): on a
     * real console, remapping the kernel's entries 13-15 (0x80000-0x1FFFFF)
     * hangs it. Accesses above 0x80000 that the game makes through a null
     * pointer fall to sampler_ps2.c, which serves them from the window. */
    _SetTLBEntry(47, 0x0007E000u, 0, entry_lo(GUEST_PHYSICAL), entry_lo(GUEST_PHYSICAL + 0x40000u));
    Ps2_Stage("espelho baixo: entrada 47");
    if (a < 0 || b < 0) {
        Ps2_Fatal("Could not map the game's memory (TLB).");
        return -1;
    }
    Ps2_EnterGuestMode();
    Ps2_Stage("modo supervisor ok");
    mapped = 1;
    memset((void *)(uintptr_t)WINDOW, 0, 0x200000u);
    memset((void *)(uintptr_t)(WINDOW + 0x300000u), 0, 0x100000u);
    return 0;
}

const char *Memories_GuestMapError(void) { return ""; }
const char *Memories_GuestMapOccupants(void) { return ""; }
int Memories_KernelRefusesTakenRange(void) { return -1; }

/* The retail image's stored pointers to RAM, KSEG0 (or KSEG1) addresses,
 * become the window's. The resident code (`text`) stays as it is: it is
 * never run, and calls through pointers to it are mapped by address. */
static void relocate_image(uint32_t at, uint32_t size)
{
    extern const unsigned Memories_RetailTextStart, Memories_RetailTextEnd;
    uint32_t *words = (uint32_t *)(uintptr_t)MEMORIES_RETAIL(at);
    uint32_t i, count = size / 4, moved = 0;
    for (i = 0; i < count; i++) {
        uint32_t address = at + i * 4, value = words[i];
        if (address >= Memories_RetailTextStart && address < Memories_RetailTextEnd) continue;
        if ((value >= 0x80010000u && value < 0x80200000u) || (value >= 0xA0010000u && value < 0xA0200000u)) {
            words[i] = (value & 0x1FFFFFu) | WINDOW;
            moved++;
        }
    }
    printf("ps2: relocated %u pointers in the executable's image\n", (unsigned)moved);
}

int Memories_GuestLoadExeData(const unsigned char *data, size_t length, const char *name)
{
    MemoriesExeImage image;
    uint32_t address, size;
    if (length < 0x800 || memcmp(data, "PS-X EXE", 8)) {
        fprintf(stderr, "%s: invalid or truncated PS-X EXE\n", name);
        return -1;
    }
    address = Memories_LoadLE32(data + 0x18);
    size = Memories_LoadLE32(data + 0x1c);
    if (address < 0x80010000u || address + size > 0x80200000u || size > length - 0x800) {
        fprintf(stderr, "%s: the image does not fit guest RAM\n", name);
        return -1;
    }
    (void)image;
    memcpy((void *)(uintptr_t)MEMORIES_RETAIL(address), data + 0x800, size);
    relocate_image(address, size);
    Ps2_Stage("executavel do jogo carregado");
    return 0;
}

int Memories_GuestLoadExe(const char *path)
{
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long size;
    int result;
    if (!file) return -1;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size);
    if (!data || Ps2_Fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(data);
        return -1;
    }
    fclose(file);
    result = Memories_GuestLoadExeData(data, (size_t)size, path);
    free(data);
    return result;
}

/* Native code's view of a guest address (pc/memory.h). Any form of a RAM
 * address: the window's, KSEG0 or KSEG1, or physical (a 24-bit packet link);
 * the scratchpad at either of its retail addresses. */
void *Memories_Resolve(MemoriesMemory *memory, uint32_t address, size_t length, size_t alignment)
{
    uint32_t offset;
    (void)memory;
    if (!alignment || (alignment & (alignment - 1)) || (address & (alignment - 1))) return NULL;
    if ((address & 0x7FFFFC00u) == MEMORIES_GUEST_SCRATCHPAD_RETAIL ||
        address - MEMORIES_GUEST_SCRATCHPAD < MEMORIES_GUEST_SCRATCHPAD_SIZE) {
        offset = address & 0x3FFu;
        return length <= MEMORIES_GUEST_SCRATCHPAD_SIZE - offset
                   ? (void *)(uintptr_t)(MEMORIES_GUEST_SCRATCHPAD + offset) : NULL;
    }
    if (address - WINDOW < MEMORIES_GUEST_RAM_SIZE) offset = address - WINDOW;
    else if (address < MEMORIES_GUEST_RAM_SIZE) offset = address;
    else if ((address & 0xE0000000u) == 0x80000000u || (address & 0xE0000000u) == 0xA0000000u) {
        offset = address & 0x1FFFFFFFu;
        if (offset >= MEMORIES_GUEST_RAM_SIZE) return NULL;
    } else return NULL;
    return length <= MEMORIES_GUEST_RAM_SIZE - offset ? (void *)(uintptr_t)(WINDOW + offset) : NULL;
}

uint32_t Memories_ReadLE32(const uint8_t *bytes) { return Memories_LoadLE32(bytes); }
void Memories_WriteLE32(uint8_t *bytes, uint32_t value) { Memories_StoreLE32(bytes, value); }

/* Memories_Icall's slow path: where a call to guest address `address` goes. */
static void no_native(void)
{
    Ps2_Fatal("A call into guest code has no native function.");
}

void *Memories_GuestCallTarget(uint32_t address)
{
    const MemoriesGuestFunction *entry;
    uint32_t canonical = Memories_Ps2Canonical(address);
    entry = Memories_FindResidentFunction(canonical);
    if (entry) return (void *)(uintptr_t)entry->host;
    if (Memories_MipsInOverlay(canonical)) {
        Memories_MipsThunkTarget = canonical;
        return (void *)(uintptr_t)Memories_MipsThunk;
    }
    printf("ps2: call into guest code at 0x%08x, which has no native function\n", (unsigned)address);
    Ps2_Log("call 0x%08x: no native function", (unsigned)address);
    return (void *)(uintptr_t)no_native;
}

void Memories_Unimplemented(const char *name)
{
    static unsigned reported;
    if (reported < 64) {
        reported++;
        printf("ps2: unimplemented routine %s\n", name);
        Ps2_Log("missing: %s", name);
    }
}

/* The game runs on its stack in the window (negative, as on the PC). */
extern int Ps2_CallOnStack(int (*entry)(void), uint32_t top);
static int (*game_entry)(void);
static int run_entry(void) { return game_entry(); }

/* The game runs on the program's own stack (1 MiB, ps2/linkfile): the EE
 * kernel does not resume a thread whose stack pointer is negative, so it
 * cannot be in the window as the PC's is at 0xB0000000 (the PC ran on a
 * positive one up to its v0.2.0 too). */
int Memories_StateRunGame(int (*entry)(void))
{
    (void)run_entry;
    game_entry = entry;
    return entry();
}

void Memories_OnHostStack(void (*function)(void *), void *argument) { function(argument); }
void Memories_StatePoint(unsigned presented_frames) { (void)presented_frames; }
void Memories_StateRequest(int what, int slot) { (void)what; (void)slot; }
int Memories_StateSaveHere(const char *path) { (void)path; return -1; }
int Memories_StateLoadHere(const char *path, char *why, size_t why_size)
{
    (void)path;
    if (why && why_size) snprintf(why, why_size, "save states are not available on the PS2");
    return -1;
}
int Memories_StateStartupDone(void) { return 1; }
uint32_t Memories_StateBuildId(void) { return 0; }
int Memories_SymbolTablePath(char *out, size_t size) { (void)out; (void)size; return -1; }
int Memories_LastStateSlot(void) { return 0; }
void Memories_StateRemapRange(MemoriesState *state, uint32_t from, uint32_t to, uint32_t size)
{
    (void)state; (void)from; (void)to; (void)size;
}

/* Reads and writes through the IOP land by DMA at the buffer's physical
 * address, which a window address does not have: those go by way of a
 * bounce buffer in plain RAM. */
static uint8_t bounce[64 * 1024] __attribute__((aligned(64)));
static int in_window(const void *buffer) { return (uintptr_t)buffer >= WINDOW; }

size_t Ps2_Fread(void *buffer, size_t size, size_t count, FILE *file)
{
    size_t total = size * count, done = 0;
    if (!in_window(buffer) || !total) return fread(buffer, size, count, file);
    while (done < total) {
        size_t part = total - done < sizeof(bounce) ? total - done : sizeof(bounce);
        size_t got = fread(bounce, 1, part, file);
        memcpy((uint8_t *)buffer + done, bounce, got);
        done += got;
        if (got < part) break;
    }
    return size ? done / size : 0;
}

size_t Ps2_Fwrite(const void *buffer, size_t size, size_t count, FILE *file)
{
    size_t total = size * count, done = 0;
    if (!in_window(buffer) || !total) return fwrite(buffer, size, count, file);
    while (done < total) {
        size_t part = total - done < sizeof(bounce) ? total - done : sizeof(bounce);
        size_t put;
        memcpy(bounce, (const uint8_t *)buffer + done, part);
        put = fwrite(bounce, 1, part, file);
        done += put;
        if (put < part) break;
    }
    return size ? done / size : 0;
}

/* The disc image: libds.c reads it a sector (2352 bytes) at a time. On the
 * console each read is a trip to the IOP and the USB stick (USB 1.1), and a
 * movie spent most of its time waiting there. A reader thread keeps the
 * chunks after the one being read filled (DISC_RING chunks of 16 whole sectors,
 * about 300 KB ahead): while it waits for the IOP it sleeps, and the game
 * decodes the movie meanwhile. A read outside the ring (a seek) moves it. */
#define DISC_CHUNK (16 * 2352)
#define DISC_RING 8
enum { SLOT_EMPTY, SLOT_FILLING, SLOT_READY };
static uint8_t ring[DISC_RING][DISC_CHUNK] __attribute__((aligned(64)));
static struct { long long chunk; int length; volatile int state; } slots_disc[DISC_RING];
static int ring_fd = -1, ring_lock = -1, ring_wake = -1, ring_done = -1;
static volatile long long ring_head = -1; /* the chunk being read now: the ring holds it and the next DISC_RING-1 */
static long long file_position = -1;      /* where the reader left the file (no lseek when it follows on) */
static unsigned char reader_stack[16384] __attribute__((aligned(16)));

static int read_chunk(int fd, long long chunk, uint8_t *into)
{
    ssize_t got;
    off_t at = (off_t)(chunk * DISC_CHUNK);
    if (file_position != (long long)at && lseek(fd, at, SEEK_SET) < 0) {
        file_position = -1;
        return -1;
    }
    got = read(fd, into, DISC_CHUNK);
    file_position = got > 0 ? (long long)at + got : -1;
    return (int)got;
}

static void reader(void *argument)
{
    (void)argument;
    for (;;) {
        int i, slot = -1;
        long long want = -1, head;
        WaitSema(ring_wake);
        for (;;) {
            WaitSema(ring_lock);
            head = ring_head;
            slot = -1, want = -1;
            if (head >= 0) {
                long long c;
                for (c = head; c < head + DISC_RING && want < 0; c++) {
                    int present = 0;
                    for (i = 0; i < DISC_RING; i++)
                        if (slots_disc[i].state != SLOT_EMPTY && slots_disc[i].chunk == c) present = 1;
                    if (!present) want = c;
                }
                if (want >= 0)
                    for (i = 0; i < DISC_RING && slot < 0; i++)
                        if (slots_disc[i].state == SLOT_EMPTY ||
                            (slots_disc[i].state == SLOT_READY &&
                             (slots_disc[i].chunk < head || slots_disc[i].chunk >= head + DISC_RING)))
                            slot = i;
            }
            if (slot >= 0) {
                slots_disc[slot].state = SLOT_FILLING;
                slots_disc[slot].chunk = want;
            }
            SignalSema(ring_lock);
            if (slot < 0) break;
            {
                int got = read_chunk(ring_fd, want, ring[slot]);
                WaitSema(ring_lock);
                slots_disc[slot].length = got > 0 ? got : 0;
                slots_disc[slot].state = got > 0 ? SLOT_READY : SLOT_EMPTY;
                if (got <= 0) ring_head = -1; /* past the end, or an error: the game's own read reports it */
                SignalSema(ring_lock);
                SignalSema(ring_done);
                if (got <= 0) break;
            }
        }
    }
}

static int start_reader(int fd)
{
    ee_sema_t sema;
    ee_thread_t thread;
    ee_thread_status_t me;
    int id;
    sema.init_count = 1, sema.max_count = 1, sema.option = 0;
    ring_lock = CreateSema(&sema);
    sema.init_count = 0, sema.max_count = 1;
    ring_wake = CreateSema(&sema);
    sema.init_count = 0, sema.max_count = 1024;
    ring_done = CreateSema(&sema);
    if (ring_lock < 0 || ring_wake < 0 || ring_done < 0) return -1;
    ReferThreadStatus(GetThreadId(), &me);
    memset(&thread, 0, sizeof(thread));
    thread.func = (void *)reader;
    thread.stack = reader_stack;
    thread.stack_size = sizeof(reader_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = me.current_priority > 1 ? me.current_priority - 1 : 1; /* runs whenever it can */
    id = CreateThread(&thread);
    if (id < 0 || StartThread(id, NULL) < 0) return -1;
    ring_fd = fd;
    printf("ps2: disc reader thread on (priority %d)\n", thread.initial_priority);
    return 0;
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset)
{
    ssize_t got;
    static int reader_failed;
    if (count <= 4096 && !reader_failed && (ring_fd == fd || (ring_fd < 0 && !(reader_failed = start_reader(fd) < 0)))) {
        long long chunk = (long long)(offset / DISC_CHUNK);
        size_t at = (size_t)(offset - (off_t)(chunk * DISC_CHUNK));
        struct timespec t0, t1;
        int waited = 0;
        if (at + count > DISC_CHUNK) count = DISC_CHUNK - at; /* never: sectors do not straddle chunks */
        for (;;) {
            int i, found = -1, filling = 0;
            WaitSema(ring_lock);
            for (i = 0; i < DISC_RING; i++) {
                if (slots_disc[i].chunk != chunk) continue;
                if (slots_disc[i].state == SLOT_READY) found = i;
                else if (slots_disc[i].state == SLOT_FILLING) filling = 1;
            }
            if (found >= 0) {
                size_t have = slots_disc[found].length > (int)at ? (size_t)slots_disc[found].length - at : 0;
                if (count > have) count = have;
                memcpy(buffer, ring[found] + at, count);
            }
            if (ring_head != chunk || (found < 0 && !filling)) {
                /* The reader follows: from this chunk on. A chunk it is not
                 * reading and that is not there (a seek, or past an error)
                 * is asked for here. */
                if (found < 0 && !filling) {
                    for (i = 0; i < DISC_RING; i++)
                        if (slots_disc[i].state == SLOT_READY && (slots_disc[i].chunk < chunk || slots_disc[i].chunk >= chunk + DISC_RING))
                            slots_disc[i].state = SLOT_EMPTY;
                }
                ring_head = chunk;
                SignalSema(ring_lock);
                SignalSema(ring_wake);
            } else {
                SignalSema(ring_lock);
            }
            if (found >= 0) {
                if (waited) {
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    Ps2_ReadUs += (unsigned long long)((t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000);
                }
                return found >= 0 && count ? (ssize_t)count : 0;
            }
            if (!waited) clock_gettime(CLOCK_MONOTONIC, &t0);
            waited = 1;
            WaitSema(ring_done); /* a chunk was read: look again */
            {
                /* Past the end of the file the reader gives up (ring_head -1). */
                int gone;
                WaitSema(ring_lock);
                gone = ring_head < 0;
                SignalSema(ring_lock);
                if (gone) {
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    Ps2_ReadUs += (unsigned long long)((t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000);
                    return 0;
                }
            }
        }
    }
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    if (fd == ring_fd) file_position = -1;
    if (!in_window(buffer)) return read(fd, buffer, count);
    if (count > sizeof(bounce)) count = sizeof(bounce);
    got = read(fd, bounce, count);
    if (got > 0) memcpy(buffer, bounce, (size_t)got);
    return got;
}

/* sys/mman.h (compat): anonymous maps are heap blocks; a fixed address
 * cannot be had; a file map is the file read into a block. */
void *mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *block;
    (void)prot;
    if (address && (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        errno = EEXIST;
        return MAP_FAILED;
    }
    block = memalign(64, length ? length : 1);
    if (!block) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(block, 0, length);
    if (!(flags & MAP_ANONYMOUS) && fd >= 0) {
        size_t done = 0;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            free(block);
            return MAP_FAILED;
        }
        while (done < length) {
            ssize_t got = read(fd, (uint8_t *)block + done, length - done);
            if (got <= 0) break;
            done += (size_t)got;
        }
    }
    return block;
}

int munmap(void *address, size_t length)
{
    (void)length;
    if (address && address != MAP_FAILED && (uintptr_t)address < WINDOW) free(address);
    return 0;
}
int mprotect(void *address, size_t length, int prot) { (void)address; (void)length; (void)prot; return 0; }
int msync(void *address, size_t length, int flags) { (void)address; (void)length; (void)flags; return 0; }
int madvise(void *address, size_t length, int advice) { (void)address; (void)length; (void)advice; return 0; }

/* Host debugging (PS2_GUEST_DUMP=<prefix>): guest RAM, at a model load step. */
void Ps2_GuestDump(const char *why, int index)
{
    static int count;
    const char *prefix = getenv("PS2_GUEST_DUMP");
    char path[256];
    FILE *out;
    if (!prefix || count >= 24) return;
    snprintf(path, sizeof(path), "%s-%02d-%s-%d.bin", prefix, count++, why, index);
    if ((out = fopen(path, "wb"))) {
        enum { DUMP_CHUNK = 0x10000 };
        uint8_t *chunk = memalign(64, DUMP_CHUNK);
        uint32_t at;
        for (at = 0; chunk && at < 0x200000u; at += DUMP_CHUNK) {
            memcpy(chunk, (const void *)(uintptr_t)(0xC0000000u + at), DUMP_CHUNK);
            fwrite(chunk, 1, DUMP_CHUNK, out);
        }
        free(chunk);
        fclose(out);
    }
    printf("ps2: guest RAM dumped: %s\n", path);
    if (index == 99) Ps2_Fatal("GsSortUnit: a broken model");
}

#undef rename
int Ps2_Fsync(int fd) { (void)fd; return 0; }
int Ps2_Rename(const char *from, const char *to)
{
    static char buffer[0x4000];
    FILE *in, *out;
    size_t got;
    int failed = 0;
    if (rename(from, to) == 0) return 0;
    remove(to);
    if (rename(from, to) == 0) return 0;
    /* Copy, then drop the source. */
    if (!(in = fopen(from, "rb"))) return -1;
    if (!(out = fopen(to, "wb"))) {
        fclose(in);
        return -1;
    }
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0)
        if (fwrite(buffer, 1, got, out) != got) failed = 1;
    fclose(in);
    if (fclose(out) != 0) failed = 1;
    if (failed) return -1;
    remove(from);
    printf("ps2: rename %s -> %s by copying\n", from, to);
    return 0;
}

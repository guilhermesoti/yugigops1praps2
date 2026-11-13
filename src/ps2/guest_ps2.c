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
#include <ee_debug.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* The disc image is read a sector (2352 bytes) at a time, and each read is
 * two calls to the IOP and a trip to the USB stick: a 32 KiB read-ahead
 * turns a movie's 150 reads a second into ten; 32 KiB, a stop of about
 * 40 ms on the console's USB 1.1, so no read stalls a movie's frame long. */
static uint8_t ahead[32 * 1024] __attribute__((aligned(64)));
static int ahead_fd = -1;
static off_t ahead_start;
static size_t ahead_length;

ssize_t pread(int fd, void *buffer, size_t count, off_t offset)
{
    ssize_t got;
    if (count <= 4096) {
        if (fd != ahead_fd || offset < ahead_start || offset + (off_t)count > ahead_start + (off_t)ahead_length) {
            ssize_t filled;
            ahead_fd = -1;
            if (lseek(fd, offset, SEEK_SET) < 0) return -1;
            filled = read(fd, ahead, sizeof(ahead));
            if (filled <= 0) return filled;
            ahead_fd = fd;
            ahead_start = offset;
            ahead_length = (size_t)filled;
        }
        {
            size_t at = (size_t)(offset - ahead_start), have = ahead_length - at;
            if (count > have) count = have;
            memcpy(buffer, ahead + at, count);
            return (ssize_t)count;
        }
    }
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
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
        static uint8_t chunk[0x10000] __attribute__((aligned(64)));
        uint32_t at;
        for (at = 0; at < 0x200000u; at += sizeof(chunk)) {
            memcpy(chunk, (const void *)(uintptr_t)(0xC0000000u + at), sizeof(chunk));
            fwrite(chunk, 1, sizeof(chunk), out);
        }
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

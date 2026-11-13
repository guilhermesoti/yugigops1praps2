/* What the PC port's excluded units (ps2/build_ps2.py, EXCLUDED) give the
 * rest: the crash monitor, the control channel's sockets, code mods and
 * their hooks, the HD pack's download, mod import from zips, the update
 * check's network. On the PS2 there is none of that: each answers "not
 * here", the way the PC port's own callers already expect when a piece is
 * unavailable. */
#include <kernel.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/debug/control_net.h"
#include "pc/debug/crash.h"
#include "pc/debug/crash_test.h"
#include "pc/debug/monitor.h"
#include "pc/mods/hd_pack.h"
#include "pc/mods/hooks.h"
#include "pc/mods/import.h"
#include "pc/mods/object_loader.h"
#include "pc/platform/update_net.h"

const char Memories_Version[] = "ps2-1.0";
const char Memories_BuildDate[] = __DATE__;
char __executable_start[4], __start_game_text[4], __stop_game_text[4], etext[4];

/* crash.c */
char Crash_ReportDir[512];
void Crash_Init(void) {}
void Crash_ReportFatal(const char *kind, const char *detail)
{
    printf("ps2: fatal: %s: %s\n", kind ? kind : "", detail ? detail : "");
}
void CrashTest_Init(void) {}
void CrashTest_Frame(void) {}
void CrashTest_Present(void) {}

/* monitor.c */
static MonitorShared shared;
int Monitor_Main(int argc, char **argv, int *status) { (void)argc; (void)argv; (void)status; return 0; }
MonitorShared *Monitor_Shared(void) { return &shared; }
int Monitor_Active(void) { return 0; }
void Monitor_Fact(const char *key, const char *format, ...) { (void)key; (void)format; }
void Monitor_NoteSystem(void) {}
size_t Monitor_Facts(char *out, size_t size)
{
    if (size) out[0] = '\0';
    return 0;
}
void Monitor_Modal(int on) { (void)on; }

/* control_net.c */
int ControlNet_Listen(unsigned port, unsigned *bound) { (void)port; (void)bound; return -1; }
int ControlNet_Accept(int timeout_ms) { (void)timeout_ms; return -1; }
long ControlNet_Receive(char *buffer, size_t size, int timeout_ms) { (void)buffer; (void)size; (void)timeout_ms; return -1; }
int ControlNet_Send(const char *data, size_t size, int (*idle)(void)) { (void)data; (void)size; (void)idle; return -1; }
void ControlNet_RefuseOthers(const char *reply) { (void)reply; }
int ControlNet_Gone(void) { return 1; }
void ControlNet_Drop(void) {}

/* hooks.c: no code mods, so nothing is ever hooked. */
int Hooks_Add(int owner, void *function, void *replacement, void **original)
{
    (void)owner; (void)function; (void)replacement; (void)original;
    return -1;
}
void Hooks_Remove(int owner, int token) { (void)owner; (void)token; }
void Hooks_Clear(int owner) { (void)owner; }
void Hooks_Relink(void) {}
int Hooks_IsHooked(const void *function) { (void)function; return 0; }
int Hooks_At(int index, int *owner, const void **function) { (void)index; (void)owner; (void)function; return 0; }

/* object_loader.c: code mods are machine code for PCs. */
int ObjectLoader_Load(const void *data, size_t size, ObjectResolver resolve, void *context, LoadedObject *object,
                      char *error, size_t error_size)
{
    (void)data; (void)size; (void)resolve; (void)context; (void)object;
    if (error && error_size) snprintf(error, error_size, "code mods are not supported on the PS2");
    return -1;
}
void *ObjectLoader_Symbol(const LoadedObject *object, const char *name) { (void)object; (void)name; return NULL; }

/* hd_pack.c */
int HdPack_Available(void) { return 0; }
int HdPack_State(void) { return 0; }
int HdPack_Busy(void) { return 0; }
unsigned long HdPack_Done(void) { return 0; }
unsigned long HdPack_Total(void) { return 0; }
int HdPack_Waiting(void) { return 0; }
const HdRelease *HdPack_Release(void) { return NULL; }
ModsImport *HdPack_Import(void) { return NULL; }
const char *HdPack_Why(void) { return "not available on the PS2"; }
int HdPack_Cancelled(void) { return 0; }
int HdPack_Lookup(void) { return -1; }
int HdPack_Download(const char *folder) { (void)folder; return -1; }
int HdPack_Install(const char *mods) { (void)mods; return -1; }
void HdPack_Cancel(void) {}
void HdPack_Reset(void) {}
void HdPack_Cleanup(const char *folder) { (void)folder; }
unsigned long long HdPack_SpaceNeeded(const HdRelease *release) { (void)release; return 0; }
long long HdPack_FreeBytes(const char *path) { (void)path; return -1; }
int HdPack_InstalledTag(const char *directory, char *tag, size_t size)
{
    (void)directory;
    if (size) tag[0] = '\0';
    return 0;
}

/* import.c */
ModsImport *Mods_ImportOpen(const char *zip, char *why, size_t why_size)
{
    (void)zip;
    if (why && why_size) snprintf(why, why_size, "importing mods is not supported on the PS2");
    return NULL;
}
int Mods_ImportCount(const ModsImport *import) { (void)import; return 0; }
ModsImportMod *Mods_ImportMod(ModsImport *import, int index) { (void)import; (void)index; return NULL; }
int Mods_ImportInstall(ModsImport *import, const char *mods, char *why, size_t why_size)
{
    (void)import; (void)mods; (void)why; (void)why_size;
    return -1;
}
unsigned long long Mods_ImportBytes(const ModsImport *import) { (void)import; return 0; }
void Mods_ImportWatch(ModsImport *import, unsigned *done, const int *cancel) { (void)import; (void)done; (void)cancel; }
void Mods_ImportStepped(ModsImport *import, void (*stepped)(void)) { (void)import; (void)stepped; }
void Mods_ImportClose(ModsImport *import) { (void)import; }
int Mods_ImportTaken(const char *mods, const char *folder, char *path, size_t size)
{
    (void)mods; (void)folder; (void)path; (void)size;
    return 0;
}
int Mods_ImportFolderId(const char *directory, char *id, size_t size)
{
    (void)directory;
    if (size) id[0] = '\0';
    return -1;
}
void Mods_ImportCleanup(const char *mods) { (void)mods; }
int Mods_ImportRemoveTree(const char *path) { (void)path; return -1; }
int Mods_ImportRemoveInstalled(const char *mods, const char *directory, char *why, size_t why_size)
{
    (void)mods; (void)directory; (void)why; (void)why_size;
    return -1;
}

/* update_net.c */
int UpdateNet_Get(const char *url, int timeout_seconds, UpdateNetSink sink, void *context, char *why, size_t why_size)
{
    (void)url; (void)timeout_seconds; (void)sink; (void)context;
    if (why && why_size) snprintf(why, why_size, "no network on the PS2");
    return -1;
}

/* libc pieces the SDK's newlib does not have. */
char *realpath(const char *path, char *resolved)
{
    size_t length = strlen(path) + 1;
    if (!resolved) resolved = malloc(length);
    if (resolved) memcpy(resolved, path, length);
    return resolved;
}

typedef void *iconv_t;
iconv_t iconv_open(const char *to, const char *from) { (void)to; (void)from; return (iconv_t)-1; }
size_t iconv(iconv_t cd, char **in, size_t *in_left, char **out, size_t *out_left)
{
    (void)cd; (void)in; (void)in_left; (void)out; (void)out_left;
    return (size_t)-1;
}
int iconv_close(iconv_t cd) { (void)cd; return 0; }

/* GCC's out-of-line atomics, for the one core (interrupts off around each). */
#define ATOMIC_BEGIN int was = DIntr()
#define ATOMIC_END if (was) EIntr()
unsigned __atomic_exchange_4(volatile void *memory, unsigned value, int model)
{
    unsigned old;
    ATOMIC_BEGIN;
    (void)model;
    old = *(volatile unsigned *)memory;
    *(volatile unsigned *)memory = value;
    ATOMIC_END;
    return old;
}
_Bool __atomic_compare_exchange_4(volatile void *memory, void *expected, unsigned desired, _Bool weak, int success,
                                  int failure)
{
    _Bool same;
    ATOMIC_BEGIN;
    (void)weak; (void)success; (void)failure;
    same = *(volatile unsigned *)memory == *(unsigned *)expected;
    if (same) *(volatile unsigned *)memory = desired;
    else *(unsigned *)expected = *(volatile unsigned *)memory;
    ATOMIC_END;
    return same;
}
#define FETCH(name, op) \
    unsigned __atomic_fetch_##name##_4(volatile void *memory, unsigned value, int model) \
    { \
        unsigned old; \
        ATOMIC_BEGIN; \
        (void)model; \
        old = *(volatile unsigned *)memory; \
        *(volatile unsigned *)memory = old op value; \
        ATOMIC_END; \
        return old; \
    }
FETCH(add, +)
FETCH(sub, -)
FETCH(and, &)
FETCH(or, |)

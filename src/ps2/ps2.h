/* What the PS2 platform's units share (src/ps2). */
#ifndef MEMORIES_PS2_H
#define MEMORIES_PS2_H
#include <stdint.h>
/* Supervisor mode: the guest window reachable, KSEG0 not (guest_ps2.c). */
void Ps2_EnterGuestMode(void);
/* A message on the screen and stdout, and the game stops there. */
void Ps2_Fatal(const char *message) __attribute__((noreturn));
/* A line for the on-screen log the debug overlay shows (platform_ps2.c). */
void Ps2_Log(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* Memories_Icall's resolver (thunk.S). */
void *Memories_GuestCallTarget(uint32_t address);
/* A boot step: on the boot screen and in YFM-LOG.txt (platform_ps2.c). */
void Ps2_Stage(const char *format, ...) __attribute__((format(printf, 1, 2)));
void Ps2_LogSync(void);
void Ps2_LogQuiet(void);
#endif

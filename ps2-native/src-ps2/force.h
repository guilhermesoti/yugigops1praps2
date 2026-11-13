/* Included first in every unit of the PS2 build (ps2/build_ps2.py).
 *
 * The PS2 memory model: the console's 2 MiB of RAM is a TLB window at
 * 0xC0000000 in the EE's supervisor segment (guest_ps2.c maps it), so every
 * guest pointer stays negative, as the game's sign tests on pointers expect.
 * A retail address 0x80xxxxxx (KSEG0) or 0xA0xxxxxx (KSEG1) of RAM is the
 * same byte at 0xC0000000 | (address & 0x1FFFFF): the build rewrites the
 * retail literals in the sources to MEMORIES_RETAIL(...), and the loader
 * relocates the pointers stored in the retail image the same way.
 * No includes here: game units have their own types. */
#ifndef MEMORIES_PS2_FORCE_H
#define MEMORIES_PS2_FORCE_H
#define MEMORIES_PS2_WINDOW 0xC0000000u
#define MEMORIES_RETAIL(address) ((((address) & 0x1FFFFFu)) | MEMORIES_PS2_WINDOW)
/* Any form of a guest RAM address (KSEG0, KSEG1, physical below 2 MiB or
 * the window itself) as the window's; anything else as it is. */
#ifndef __ASSEMBLER__
static inline unsigned Memories_Ps2Canonical(unsigned address)
{
    unsigned segment = address & 0xE0000000u;
    if ((segment == 0x80000000u || segment == 0xA0000000u) && (address & 0x1FFFFFFFu) < 0x200000u)
        return (address & 0x1FFFFFu) | MEMORIES_PS2_WINDOW;
    return address;
}
#endif
#endif

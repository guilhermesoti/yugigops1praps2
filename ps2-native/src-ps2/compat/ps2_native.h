/* Included in the native (src/pc, src/ps2) units after force.h. */
#ifndef MEMORIES_PS2_NATIVE_H
#define MEMORIES_PS2_NATIVE_H
/* The IOP writes a read's bytes by DMA to the physical address of the
 * buffer: a buffer in the guest window (0xC0000000) has none it can use, so
 * reads into it go through a bounce buffer (guest_ps2.c defines these).
 * stdio.h declares them under these names. */
#define fread Ps2_Fread
#define fwrite Ps2_Fwrite
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif

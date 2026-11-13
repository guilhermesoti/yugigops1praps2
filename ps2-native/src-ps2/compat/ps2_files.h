/* The USB stick's FAT through fileXio: rename may refuse to replace a file
 * that exists, and fsync is not there (guest_ps2.c). Included by the units
 * that save files, after the system headers. */
#ifndef MEMORIES_PS2_FILES_H
#define MEMORIES_PS2_FILES_H
int Ps2_Rename(const char *from, const char *to);
int Ps2_Fsync(int fd);
#define rename Ps2_Rename
#define fsync Ps2_Fsync
#endif

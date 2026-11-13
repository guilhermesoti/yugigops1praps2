/* PS2: no virtual memory to speak of. Anonymous maps are heap blocks, a
 * file map is the file read into one, and an address cannot be asked for
 * (every caller in the port has a fallback for that). */
#ifndef MEMORIES_PS2_SYS_MMAN_H
#define MEMORIES_PS2_SYS_MMAN_H
#include <stddef.h>
#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define MAP_FAILED ((void *)-1)
#define MS_SYNC 4
#define MADV_DONTNEED 4
void *mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int prot);
int msync(void *address, size_t length, int flags);
int madvise(void *address, size_t length, int advice);
#endif

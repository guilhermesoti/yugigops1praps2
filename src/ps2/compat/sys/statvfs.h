#ifndef MEMORIES_PS2_STATVFS_H
#define MEMORIES_PS2_STATVFS_H
struct statvfs { unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail; };
static inline int statvfs(const char *path, struct statvfs *out) { (void)path; (void)out; return -1; }
#endif

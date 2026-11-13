#ifndef MEMORIES_PS2_SYSCALL_H
#define MEMORIES_PS2_SYSCALL_H
#define SYS_gettid 0
static inline long syscall(long number, ...) { (void)number; return 1; }
#endif

#ifndef MEMORIES_PS2_UCONTEXT_H
#define MEMORIES_PS2_UCONTEXT_H
typedef struct { unsigned long gregs[32]; } mcontext_t;
typedef struct ucontext { mcontext_t uc_mcontext; } ucontext_t;
#endif

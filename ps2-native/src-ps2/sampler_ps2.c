/* A debugging aid for emulator runs (host:): every two seconds, where the
 * game's thread was at the last VBlank interrupt (COP0 EPC), printed by a
 * thread of its own, so a hang can be found with addr2line on the ELF. */
#include <kernel.h>
#include <timer.h>
#include <timer_alarm.h>
#include <stdio.h>
#include <stdint.h>

static volatile uint32_t samples[8], sample_ra;
#define PROFILE_SIZE 8192
static uint32_t profile[PROFILE_SIZE];
static volatile unsigned profile_count;
static volatile unsigned vblanks, sample_at;
static int monitor_thread = -1, main_thread = -1;
static unsigned char monitor_stack[16 * 1024] __attribute__((aligned(16)));

static int on_vblank(int cause)
{
    uint32_t epc;
    (void)cause;
    __asm__ volatile("mfc0 %0, $14" : "=r"(epc));
    samples[sample_at++ & 7] = epc;

    if (++vblanks % 120 == 0 && monitor_thread >= 0) iWakeupThread(monitor_thread);
    ExitHandler();
    return 0;
}

static void monitor(void *argument)
{
    (void)argument;
    for (;;) {
        unsigned i;
        SleepThread();
        if (vblanks % 1200 == 0 && profile_count) {
            unsigned n = profile_count;
            printf("ps2: profile %u", n);
            for (i = 0; i < n; i++) printf(i % 16 ? " %x" : "\nps2p: %x", (unsigned)profile[i]);
            printf("\n");
            profile_count = 0;
        }

    }
}

/* The profile: where the CPU was at a timer alarm, about 1 kHz (not in step
 * with the VBlank, which always finds the game waiting for it). */
#define PERIOD (kBUSCLK / 199)
static u64 on_alarm(s32 id, u64 scheduled, u64 actual, void *arg, void *pc)
{
    (void)id; (void)actual; (void)arg;
    if (profile_count < PROFILE_SIZE) profile[profile_count++] = (uint32_t)(uintptr_t)pc;
    (void)scheduled;
    return PERIOD;
}

void Ps2_StartSampler(void)
{
    ee_thread_t thread = {0};
    main_thread = GetThreadId();
    ChangeThreadPriority(main_thread, 64);
    thread.func = (void *)monitor;
    thread.stack = monitor_stack;
    thread.stack_size = sizeof(monitor_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = 1;
    monitor_thread = CreateThread(&thread);
    if (monitor_thread >= 0) StartThread(monitor_thread, NULL);
    AddIntcHandler(INTC_VBLANK_S, on_vblank, 0);
    EnableIntc(INTC_VBLANK_S);
    EI();
    printf("ps2: sampler thread %d, alarm %d\n", monitor_thread, (int)SetTimerAlarm(PERIOD, on_alarm, NULL));
}

/* A fault in the game's thread (a TLB miss, an address or bus error) ends
 * in a report on the screen rather than a black hang: the handler records
 * it and resumes the thread in crash_report, which shows it (and on host:,
 * prints it for addr2line). */
#include <ee_debug.h>
#include "ps2.h"

/* A load or store through a retail RAM address (KSEG0/KSEG1) that nothing
 * relocated: done at the window's address instead, and the instruction
 * skipped. Supervisor mode makes these fault on the console; each place is
 * logged once, so it can be fixed at its source. */
static uint32_t retail_sites[16];
static unsigned retail_site_count;
static int emulate_retail_access(EE_RegFrame *frame)
{
    uint32_t *regs = (uint32_t *)frame, instruction = *(const uint32_t *)(uintptr_t)frame->epc;
    unsigned op = instruction >> 26, rt = (instruction >> 16) & 31, i;
    uint32_t address = (frame->badvaddr & 0x1FFFFFu) | 0xC0000000u;
    uint32_t *target = &regs[rt * 4];
    switch (op) {
    case 0x20: target[0] = (uint32_t)(int32_t)*(volatile int8_t *)address; break;
    case 0x24: target[0] = *(volatile uint8_t *)address; break;
    case 0x21: target[0] = (uint32_t)(int32_t)*(volatile int16_t *)address; break;
    case 0x25: target[0] = *(volatile uint16_t *)address; break;
    case 0x23: target[0] = *(volatile uint32_t *)address; break;
    case 0x27: target[0] = *(volatile uint32_t *)address; target[1] = 0; break;
    case 0x37: target[0] = ((volatile uint32_t *)address)[0]; target[1] = ((volatile uint32_t *)address)[1]; break;
    case 0x28: *(volatile uint8_t *)address = rt ? (uint8_t)target[0] : 0; goto stored;
    case 0x29: *(volatile uint16_t *)address = rt ? (uint16_t)target[0] : 0; goto stored;
    case 0x2B: *(volatile uint32_t *)address = rt ? target[0] : 0; goto stored;
    case 0x3F: ((volatile uint32_t *)address)[0] = rt ? target[0] : 0; ((volatile uint32_t *)address)[1] = rt ? target[1] : 0;
        goto stored;
    default: return 0;
    }
    if (op != 0x27 && op != 0x37 && rt) target[1] = (target[0] & 0x80000000u) ? 0xFFFFFFFFu : 0;
    if (!rt) target[0] = target[1] = 0;
stored:
    for (i = 0; i < retail_site_count && retail_sites[i] != frame->epc; i++) {}
    if (i == retail_site_count && retail_site_count < 16) retail_sites[retail_site_count++] = frame->epc;
    frame->epc += 4;
    return 1;
}

unsigned Ps2_RetailSites(const uint32_t **sites)
{
    *sites = retail_sites;
    return retail_site_count;
}

static volatile uint32_t crash_epc, crash_badvaddr, crash_cause, crash_ra, crash_sp, crash_status;

static void crash_report(void)
{
    char text[200];
    snprintf(text, sizeof(text), "Exception %u at 0x%08x (address 0x%08x, ra 0x%08x, sp 0x%08x, status 0x%08x)",
             (unsigned)((crash_cause >> 2) & 31), (unsigned)crash_epc, (unsigned)crash_badvaddr, (unsigned)crash_ra,
             (unsigned)crash_sp, (unsigned)crash_status);
    Ps2_Fatal(text);
}

/* The mode the game runs in (guest_ps2.c): supervisor, where a stray KSEG0
 * address faults; kernel if the console refuses the window to supervisor. */
extern unsigned Ps2_GuestMode;

static int on_fault(EE_RegFrame *frame)
{
    unsigned cause = (frame->cause >> 2) & 31;
    if ((cause >= 2 && cause <= 5) && frame->badvaddr >= 0xC0000000u && frame->badvaddr < 0xC0400000u &&
        (frame->status & 0x18u)) {
        /* The guest window from a mode that cannot reach it: from now on
         * kernel mode, and the access again. */
        Ps2_GuestMode = 0;
        frame->status &= ~0x18u;
        return 1;
    }
    if ((cause == 4 || cause == 5) && !(frame->cause & 0x80000000u) &&
        (frame->badvaddr & 0xE0000000u) >= 0x80000000u && (frame->badvaddr & 0xE0000000u) <= 0xA0000000u &&
        (frame->badvaddr & 0x1FFFFFFFu) < 0x200000u && emulate_retail_access(frame))
        return 1;
    /* A null pointer plus a little, where the low mirror is not mapped (a
     * console whose kernel refused it): the guest's first pages. */
    if ((cause == 2 || cause == 3) && !(frame->cause & 0x80000000u) && frame->badvaddr < 0x00080000u &&
        emulate_retail_access(frame))
        return 1;
    crash_epc = frame->epc;
    crash_badvaddr = frame->badvaddr;
    crash_cause = frame->cause;
    crash_ra = frame->ra[0];
    crash_sp = frame->sp[0];
    crash_status = frame->status;
    frame->epc = (uint32_t)(uintptr_t)crash_report;
    /* Back in kernel-reachable memory: a fresh stack below the faulting one
     * would do too, but the fault may be the stack itself. */
    frame->sp[0] = 0x01F00000u;
    frame->status = (frame->status & ~0x18u) | 0x10u; /* user mode for the report */
    return 1;
}

void Ps2_InstallFaultHandlers(void)
{
    int cause;
    ee_dbg_install(1);
    for (cause = 1; cause <= 7; cause++) ee_dbg_set_level1_handler(cause, on_fault);
    ee_dbg_set_level1_handler(10, on_fault); /* reserved instruction */
}

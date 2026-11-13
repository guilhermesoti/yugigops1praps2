/* The PS2 build's indirect calls and the game's setjmp.
 *
 * Memories_Icall: ps2/build_ps2.py turns every `jalr $25` of every unit into
 * `jal Memories_Icall` (and `jr $25` into `j`), so a call through a pointer
 * comes here with the target in $25 and the caller's return address in $31.
 * A native target (positive: the program is in the EE's user RAM) is jumped
 * to as it is. A negative one is a guest address (the 0xC0000000 window, or
 * a retail KSEG0/KSEG1 one read from game data): Memories_GuestCallTarget
 * (guest_ps2.c) gives the native function for it, or the bridge into the
 * MIPS interpreter for an overlay that has none. The arguments (a0-a7) and
 * the return address are kept; $1 and $24 are free at a call. */
    .set noreorder
    .set noat
    .text

    .globl Memories_Icall
    .type Memories_Icall, @function
    .ent Memories_Icall
Memories_Icall:
    bltz $25, 1f
    nop
    jr $25
    nop
1:  addiu $sp, $sp, -112
    sd $4, 0($sp)
    sd $5, 8($sp)
    sd $6, 16($sp)
    sd $7, 24($sp)
    sd $8, 32($sp)
    sd $9, 40($sp)
    sd $10, 48($sp)
    sd $11, 56($sp)
    sd $31, 64($sp)
    sd $2, 72($sp)
    sd $3, 80($sp)
    jal Memories_GuestCallTarget
    move $4, $25
    move $25, $2
    ld $4, 0($sp)
    ld $5, 8($sp)
    ld $6, 16($sp)
    ld $7, 24($sp)
    ld $8, 32($sp)
    ld $9, 40($sp)
    ld $10, 48($sp)
    ld $11, 56($sp)
    ld $31, 64($sp)
    ld $2, 72($sp)
    ld $3, 80($sp)
    jr $25
    addiu $sp, $sp, 112
    .end Memories_Icall
    .size Memories_Icall, . - Memories_Icall

/* The game's jmp_buf is the Psy-Q int[12] at a fixed guest address: the
 * twelve callee-saved registers (s0-s7, fp, sp, ra, gp) as 32-bit words.
 * The n32 ABI's registers are 64 bits wide and native code above the
 * setjmp may keep 64-bit values in them, so the full registers also go to
 * a shadow copy here, kept for the last jmp_buf set (the game has one:
 * Main_Init's), and longjmp restores from it when it is that jmp_buf. */
    .bss
    .align 4
Psx_jmp_key:
    .space 16
Psx_jmp_shadow:
    .space 96
    .text

    .globl Psx_setjmp
    .type Psx_setjmp, @function
    .ent Psx_setjmp
Psx_setjmp:
    sw $16, 0($4)
    sw $17, 4($4)
    sw $18, 8($4)
    sw $19, 12($4)
    sw $20, 16($4)
    sw $21, 20($4)
    sw $22, 24($4)
    sw $23, 28($4)
    sw $30, 32($4)
    sw $29, 36($4)
    sw $31, 40($4)
    sw $28, 44($4)
    lui $8, %hi(Psx_jmp_key)
    sw $4, %lo(Psx_jmp_key)($8)
    lui $8, %hi(Psx_jmp_shadow)
    addiu $8, $8, %lo(Psx_jmp_shadow)
    sd $16, 0($8)
    sd $17, 8($8)
    sd $18, 16($8)
    sd $19, 24($8)
    sd $20, 32($8)
    sd $21, 40($8)
    sd $22, 48($8)
    sd $23, 56($8)
    sd $30, 64($8)
    sd $29, 72($8)
    sd $31, 80($8)
    sd $28, 88($8)
    jr $31
    move $2, $0
    .end Psx_setjmp
    .size Psx_setjmp, . - Psx_setjmp

    .globl Psx_longjmp
    .type Psx_longjmp, @function
    .ent Psx_longjmp
Psx_longjmp:
    lui $8, %hi(Psx_jmp_key)
    lw $8, %lo(Psx_jmp_key)($8)
    bne $8, $4, 2f
    nop
    lui $8, %hi(Psx_jmp_shadow)
    addiu $8, $8, %lo(Psx_jmp_shadow)
    ld $16, 0($8)
    ld $17, 8($8)
    ld $18, 16($8)
    ld $19, 24($8)
    ld $20, 32($8)
    ld $21, 40($8)
    ld $22, 48($8)
    ld $23, 56($8)
    ld $30, 64($8)
    ld $29, 72($8)
    ld $31, 80($8)
    b 3f
    ld $28, 88($8)
2:  lw $16, 0($4)
    lw $17, 4($4)
    lw $18, 8($4)
    lw $19, 12($4)
    lw $20, 16($4)
    lw $21, 20($4)
    lw $22, 24($4)
    lw $23, 28($4)
    lw $30, 32($4)
    lw $29, 36($4)
    lw $31, 40($4)
    lw $28, 44($4)
3:  bnez $5, 1f
    move $2, $5
    li $2, 1
1:  jr $31
    nop
    .end Psx_longjmp
    .size Psx_longjmp, . - Psx_longjmp

/* Run `entry` (a0) on the stack whose top is a1; returns what it returns,
 * on the caller's stack again. */
    .globl Ps2_CallOnStack
    .type Ps2_CallOnStack, @function
    .ent Ps2_CallOnStack
Ps2_CallOnStack:
    addiu $5, $5, -32
    sd $31, 0($5)
    sd $29, 8($5)
    sd $16, 16($5)
    move $29, $5
    move $25, $4
    jalr $25
    nop
    ld $31, 0($29)
    ld $16, 16($29)
    ld $29, 8($29)
    jr $31
    nop
    .end Ps2_CallOnStack
    .size Ps2_CallOnStack, . - Ps2_CallOnStack

/* VSync: the PC's records the caller's registers for save states first
 * (state_i386.S); the PS2 has none. */
    .globl VSync
    .type VSync, @function
    .ent VSync
VSync:
    j Memories_VSync
    nop
    .end VSync

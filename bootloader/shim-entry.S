/* SPDX-License-Identifier: BSD-2-Clause
 * Minimal ARM64 BootShim / Bare-metal XNU Pre-loader for Sony Xperia XZ1 Compact
 */

.section .text.head, "ax"
.globl _start
.align 3

_start:
    b       _real_start

    .balign 8
_text_offset:
    .quad   0x00080000
_image_size:
    .quad   _end - _start
_flags:
    .quad   0x0000000a
_res2:
    .quad   0
_res3:
    .quad   0
_res4:
    .quad   0
_magic:
    .ascii  "ARM\x64"
_res5:
    .long   0

.align 3
_real_start:
    msr     daifset, #0xf

    /* Preserve DTB pointer in x19 */
    mov     x19, x0

    /* Record initial Exception Level in x20 */
    mrs     x20, CurrentEL
    lsr     x20, x20, #2
    cmp     x20, #2
    b.ne    _in_el1

_in_el2:
    /* Disable EL2 MMU & caches, configure EL1 access, and drop cleanly to EL1h */
    mrs     x0, sctlr_el2
    bic     x0, x0, #(1 << 0)   /* M = 0 */
    bic     x0, x0, #(1 << 2)   /* C = 0 */
    bic     x0, x0, #(1 << 12)  /* I = 0 */
    msr     sctlr_el2, x0
    isb

    mov     x0, #(1 << 31)      /* HCR_EL2.RW = 1 (EL1 is AArch64) */
    msr     hcr_el2, x0

    mov     x0, #3              /* CNTHCTL_EL2: allow EL1 physical timer/counter */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    mrs     x0, midr_el1
    msr     vpidr_el2, x0
    mrs     x0, mpidr_el1
    msr     vmpidr_el2, x0

    mov     x0, #0x33ff         /* CPTR_EL2: RES1 bits, TFP=0 (do not trap FP/SIMD) */
    msr     cptr_el2, x0
    msr     hstr_el2, xzr

    adr     x0, _in_el1
    msr     elr_el2, x0
    mov     x0, #0x3c5          /* SPSR_EL2: DAIF masked, EL1h */
    msr     spsr_el2, x0
    isb
    eret

_in_el1:
    /* Set stack pointer safely at 0x82000000 (32MB in RAM) */
    msr     spsel, #1
    ldr     x2, =0x82000000
    mov     sp, x2

    /* Disable MMU & Caches at EL1 */
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   /* M = 0 */
    bic     x0, x0, #(1 << 2)   /* C = 0 */
    bic     x0, x0, #(1 << 12)  /* I = 0 */
    bic     x0, x0, #(1 << 1)   /* A = 0 */
    msr     sctlr_el1, x0
    isb

    /* Enable FP/SIMD in CPACR_EL1 just in case */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* Install BootShim's own exception vector table */
    adrp    x0, shim_vectors
    add     x0, x0, :lo12:shim_vectors
    msr     vbar_el1, x0
    isb

    /* Invalidate entire instruction cache */
    ic      iallu
    dsb     sy
    isb

    /* Call C loader: x0 = payload_pa, x1 = dtb_pa, x2 = initial_el */
    adrp    x0, _payload_start
    add     x0, x0, :lo12:_payload_start
    mov     x1, x19
    mov     x2, x20
    bl      shim_main

_hang:
    wfi
    b       _hang

.globl shim_return_stub
.balign 64
shim_return_stub:
    msr     daifset, #0xf
    mrs     x9, sctlr_el1
    bic     x9, x9, #(1 << 0)   /* M = 0 */
    bic     x9, x9, #(1 << 2)   /* C = 0 */
    bic     x9, x9, #(1 << 12)  /* I = 0 */
    msr     sctlr_el1, x9
    isb
    tlbi    vmalle1
    dsb     sy
    ic      iallu
    isb

    /* Reinstall BootShim vectors and stack */
    adrp    x9, shim_vectors
    add     x9, x9, :lo12:shim_vectors
    msr     vbar_el1, x9
    msr     spsel, #1
    ldr     x9, =0x82000000
    mov     sp, x9
    isb

    /* Call C function: shim_on_kernel_return(log_pa, log_len, reason, esr, elr, far, lr, sp) */
    bl      shim_on_kernel_return

_return_hang:
    wfi
    b       _return_hang

.balign 2048
shim_vectors:
    .rept 16
    b       _shim_exception
    .balign 128
    .endr

_shim_exception:
    msr     daifset, #0xf
    mov     x6, lr
    mov     x7, sp
    mrs     x3, esr_el1
    mrs     x4, elr_el1
    mrs     x5, far_el1
    mov     x0, #0
    mov     x1, #0
    mov     x2, #0xdead
    msr     spsel, #1
    ldr     x9, =0x82000000
    mov     sp, x9
    bl      shim_on_kernel_return
1:  wfi
    b       1b

#ifdef LILAC_UFS_SNAPSHOT
/* The opt-in snapshot's only UFS MMIO access: one aligned 32-bit load.
 * A synchronous abort follows the existing terminal exception path.
 */
.globl shim_ufs_read32
.align 2
shim_ufs_read32:
    ldr     w0, [x0]
    ret
#endif

.align 3
_end:

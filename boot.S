/* boot.S — ARM64 bootloader for DIGITAL CAVIAR [OS] */

.global _start
.global kmain

.section .text
.align 4

_start:
    // QEMU passes a device tree pointer in x0 for -kernel boots.
    mov x19, x0

    // QEMU's virt machine may enter at EL2. Establish a predictable EL1
    // environment before touching C code.
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3
    cmp x0, #2
    b.ne at_el1

    // EL1 uses AArch64, its own stack, and an identity-mapped physical view
    // while the MMU remains disabled.
    mov x0, #(1 << 31)
    msr hcr_el2, x0
    mov x0, #0x3c5
    msr spsr_el2, x0
    adr x0, at_el1
    msr elr_el2, x0
    dsb sy
    isb
    eret

at_el1:
    // Set stack pointer
    ldr x0, =stack_top
    mov sp, x0

    // Clear BSS
    ldr x1, =__bss_start
    ldr x2, =__bss_end

zero_bss:
    cmp x1, x2
    b.eq bss_done
    str xzr, [x1], #8
    b zero_bss

bss_done:
    // Pass the preserved device tree pointer to the kernel.
    mov x0, x19
    bl kmain

hang:
    wfe
    b hang

.section .bss
.align 16
stack_bottom:
    /* 64KiB — desktop/event paths nest past the old 16KiB budget. */
    .skip 0x10000
stack_top:

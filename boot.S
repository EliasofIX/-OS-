/* boot.S — ARM64 bootloader for DIGITAL CAVIAR [OS] */

.global _start
.global kmain

.section .text
.align 4

_start:
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
    // Call main kernel function
    bl kmain

hang:
    wfe
    b hang

.section .bss
.align 16
stack_bottom:
    .skip 0x4000
stack_top:

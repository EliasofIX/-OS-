/* linker_arm64.ld — for ARM64 bare metal kernel */

ENTRY(_start)

SECTIONS {
    /* QEMU virt DRAM starts at 0x40000000; leave room for its boot data. */
    . = 0x40080000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}


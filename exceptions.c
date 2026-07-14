#include "os.h"

extern void vector_table(void);

void exceptions_init(void) {
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vector_table));
}

void sync_exception(void) {
    uart_puts("Synchronous exception\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

void irq_exception(void) {
    uart_puts("IRQ received\n");
}

void fiq_exception(void) {
    uart_puts("FIQ received\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

void serror_exception(void) {
    uart_puts("SError received\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

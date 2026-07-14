#include "os.h"

#define UART_BASE 0x09000000UL
#define UART_DR   (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_FR_TXFF (1U << 5)

static void uart_putc(char character) {
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = (uint32_t)character;
}

void uart_puts(const char *text) {
    while (*text) {
        if (*text == '\n') uart_putc('\r');
        uart_putc(*text++);
    }
}

void kmain(void) {
    uart_puts("\nDIGITAL CAVIAR [OS] 1.0\n");
    uart_puts("Establishing display...\n");
    if (!graphics_init()) {
        uart_puts("fatal: QEMU ramfb unavailable\n");
        while (1) __asm__ volatile("wfe");
    }

    uart_puts("Discovering devices...\n");
    virtio_init();

    struct desktop_state desktop;
    desktop_init(&desktop);
    desktop_render(&desktop);
    uart_puts("Desktop ready: pointer, keyboard, apps, storage\n");

    struct dc_event event;
    for (;;) {
        if (input_poll(&event)) desktop_handle_event(&desktop, &event);
        else continue;
        desktop_render(&desktop);
    }
}

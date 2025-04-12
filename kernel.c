// kernel.c - DIGITAL CAVIAR [OS] Kernel (ARM64, 64-bit)
// Boots, displays CLI via UART, handles basic commands
void execute_command(void);
#include <stdint.h>

// PL011 UART base address (QEMU virt machine)
#define UART_BASE 0x09000000
#define UART_DR   (UART_BASE + 0x00) // Data register
#define UART_FR   (UART_BASE + 0x18) // Flag register
#define UART_FR_TXFF (1 << 5)        // Transmit FIFO full

// Exception levels
#define EL1 1
#define EL1h 0x5 // EL1 with SP1

// System registers
#define SPSR_EL1 0xC2000000 // Mask interrupts, AArch64 mode
#define VBAR_EL1 0xC0000000 // Vector base address

// Filesystem stub
#define MAX_FILES 16
#define NAME_LEN 32

struct file {
    char name[NAME_LEN];
    uint64_t size;
};

struct dir {
    struct file files[MAX_FILES];
    uint64_t count;
};

// Global state
static struct dir root_dir;
static char input_buf[256];
static uint64_t input_pos = 0;

// UART functions
void uart_putc(char c) {
    // Wait until transmit FIFO is not full
    while (*(volatile uint32_t *)UART_FR & UART_FR_TXFF) {}
    *(volatile uint32_t *)UART_DR = c;
}

void uart_puts(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\n') uart_putc('\r');
        uart_putc(str[i]);
    }
}

// Memory barrier
void dsb() {
    __asm__ volatile ("dsb sy");
}

// Exception vector table
void vector_table(void);
__asm__(
    ".section .text\n"
    ".align 11\n" // 2KB alignment for VBAR
    "vector_table:\n"
    // Synchronous EL1h
    "b sync_handler\n"
    ".align 7\n"
    // IRQ EL1h
    "b irq_handler\n"
    ".align 7\n"
    // FIQ EL1h
    "b fiq_handler\n"
    ".align 7\n"
    // SError EL1h
    "b serror_handler\n"
);

// Exception handlers
void sync_handler(void) {
    uart_puts("Synchronous exception!\n");
    while (1);
}

void irq_handler(void) {
    // Stub for keyboard (UART input later)
    uart_puts("IRQ received\n");
}

void fiq_handler(void) {
    uart_puts("FIQ received\n");
    while (1);
}

void serror_handler(void) {
    uart_puts("SError received\n");
    while (1);
}

// Initialize MMU (stub)
void init_mmu(void) {
    // Placeholder: Flat mapping for now
    // Later: Set up page tables for EL1
}

// Initialize UART
void init_uart(void) {
    // QEMU virt PL011 needs no init for basic use
    uart_puts("\033[2J\033[H"); // Clear terminal (ANSI)
}

// Initialize filesystem stub
void init_filesystem(void) {
    root_dir.count = 2;
    for (int i = 0; i < NAME_LEN; i++) {
        root_dir.files[0].name[i] = 0;
        root_dir.files[1].name[i] = 0;
    }
    for (int i = 0; i < 8; i++) {
        root_dir.files[0].name[i] = "welcome.txt"[i];
        root_dir.files[1].name[i] = "test.txt"[i];
    }
    root_dir.files[0].size = 50;
    root_dir.files[1].size = 10;
}

// strcmp
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

// strcpy
void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = 0;
}

// Print uint64
void print_uint64(uint64_t n) {
    char buf[32];
    int i = 0;
    if (n == 0) {
        uart_putc('0');
        return;
    }
    while (n) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        uart_putc(buf[j]);
    }
}

// Read char from UART (polling for now)
char uart_getc(void) {
    // QEMU virt UART input via terminal
    // Later: Use interrupts
    while (*(volatile uint32_t *)UART_FR & (1 << 4)) {} // Wait for RXFE
    return *(volatile uint32_t *)UART_DR;
}

// Shell
void shell(void) {
    uart_puts("Welcome to DIGITAL CAVIAR OS\n");
    uart_puts("Type 'help' for commands\n");
    uart_puts("> ");
    
    while (1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            if (input_pos > 0) {
                uart_puts("\n");
                execute_command();
                input_pos = 0;
                input_buf[0] = 0;
                uart_puts("> ");
            }
        } else if (c == '\b' || c == 0x7F) {
            if (input_pos > 0) {
                input_pos--;
                input_buf[input_pos] = 0;
                uart_puts("\b \b");
            }
        } else if (input_pos < sizeof(input_buf) - 1) {
            input_buf[input_pos++] = c;
            input_buf[input_pos] = 0;
            uart_putc(c);
        }
    }
}

// Execute command
void execute_command(void) {
    if (strcmp(input_buf, "help") == 0) {
        uart_puts("Commands: help, clear, list, view, go\n");
    } else if (strcmp(input_buf, "clear") == 0) {
        uart_puts("\033[2J\033[H");
    } else if (strcmp(input_buf, "list") == 0) {
        for (uint64_t i = 0; i < root_dir.count; i++) {
            uart_puts(root_dir.files[i].name);
            uart_puts(" (");
            print_uint64(root_dir.files[i].size);
            uart_puts(" bytes)\n");
        }
    } else if (strcmp(input_buf, "view welcome") == 0) {
        uart_puts("Welcome to DIGITAL CAVIAR OS!\n");
    } else if (strcmp(input_buf, "go home") == 0) {
        uart_puts("Changed to home directory\n");
    } else {
        uart_puts("Unknown command\n");
    }
}

// Kernel entry point
void kmain(void) {
    // Ensure EL1
    uint64_t current_el;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    current_el = (current_el >> 2) & 3;
    if (current_el != EL1) {
        uart_puts("Not in EL1, halting\n");
        while (1);
    }

    // Initialize hardware
    init_mmu();
    init_uart();
    init_filesystem();

    // Set up exception vectors
    __asm__ volatile ("msr vbar_el1, %0" : : "r"(vector_table));

    // Enable interrupts (stub for now)
    __asm__ volatile ("msr daifclr, #2");

    // Start shell
    shell();
}

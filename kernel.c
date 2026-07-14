// DIGITAL CAVIAR [OS] — graphical foundation for QEMU's ARM64 virt machine.
//
// The desktop deliberately uses opaque value masses, generous negative space,
// asymmetry, and one broken grid element. Depth comes from ordered dithering,
// not transparency.

#include <stdint.h>
#include <stddef.h>

#define UART_BASE 0x09000000UL
#define UART_DR   (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_FR_TXFF (1U << 5)

#define FW_CFG_BASE     0x09020000UL
#define FW_CFG_DATA     (*(volatile uint8_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SELECTOR (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA      (*(volatile uint64_t *)(FW_CFG_BASE + 0x10))
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_DMA_ERROR  0x01U
#define FW_CFG_DMA_SELECT 0x08U
#define FW_CFG_DMA_WRITE  0x10U

#define SCREEN_WIDTH  640U
#define SCREEN_HEIGHT 480U
#define SCREEN_STRIDE (SCREEN_WIDTH * 4U)
#define FRAMEBUFFER_ADDRESS 0x41000000UL

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define VOID_COLOR    RGB(12, 12, 14)
#define DEEP_COLOR    RGB(25, 25, 28)
#define SHADOW_COLOR  RGB(40, 39, 39)
#define DIM_COLOR     RGB(176, 173, 166)
#define SURFACE_COLOR RGB(232, 228, 220)
#define INK_COLOR     RGB(21, 21, 22)
#define ACCENT_COLOR  RGB(126, 141, 153)
#define BREAK_COLOR   RGB(183, 156, 118)

static volatile uint32_t *const framebuffer =
    (volatile uint32_t *)FRAMEBUFFER_ADDRESS;

static const uint8_t bayer8[8][8] = {
    { 0, 48, 12, 60,  3, 51, 15, 63 },
    {32, 16, 44, 28, 35, 19, 47, 31 },
    { 8, 56,  4, 52, 11, 59,  7, 55 },
    {40, 24, 36, 20, 43, 27, 39, 23 },
    { 2, 50, 14, 62,  1, 49, 13, 61 },
    {34, 18, 46, 30, 33, 17, 45, 29 },
    {10, 58,  6, 54,  9, 57,  5, 53 },
    {42, 26, 38, 22, 41, 25, 37, 21 },
};

struct glyph {
    char character;
    uint8_t rows[7];
};

// Five-bit-wide uppercase display face. Lowercase strings are normalized.
static const struct glyph font[] = {
    {' ', {0,0,0,0,0,0,0}}, {'-', {0,0,0,31,0,0,0}},
    {'.', {0,0,0,0,0,12,12}}, {'/', {1,2,4,8,16,0,0}},
    {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,30,1,1,17,14}},
    {'6', {6,8,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,2,12}},
    {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
    {'C', {14,17,16,16,16,17,14}}, {'D', {30,17,17,17,17,17,30}},
    {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
    {'G', {14,17,16,23,17,17,14}}, {'H', {17,17,17,31,17,17,17}},
    {'I', {14,4,4,4,4,4,14}}, {'J', {7,2,2,2,2,18,12}},
    {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
    {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,16,14,1,1,30}}, {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
    {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
    {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
};

static void uart_putc(char character) {
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = (uint32_t)character;
}

static void uart_puts(const char *text) {
    while (*text) {
        if (*text == '\n') {
            uart_putc('\r');
        }
        uart_putc(*text++);
    }
}

static uint16_t byte_swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t byte_swap32(uint32_t value) {
    return __builtin_bswap32(value);
}

static uint64_t byte_swap64(uint64_t value) {
    return __builtin_bswap64(value);
}

static uint32_t read_be32(void) {
    uint32_t value = 0;
    for (unsigned int i = 0; i < 4; ++i) {
        value = (value << 8) | FW_CFG_DATA;
    }
    return value;
}

static uint16_t read_be16(void) {
    uint16_t value = (uint16_t)FW_CFG_DATA << 8;
    return (uint16_t)(value | FW_CFG_DATA);
}

static int names_equal(const char *left, const char *right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) {
            return 0;
        }
        if (left[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

static uint16_t fw_cfg_find_file(const char *wanted_name) {
    FW_CFG_SELECTOR = byte_swap16(FW_CFG_FILE_DIR);
    uint32_t count = read_be32();
    if (count > 256U) {
        return 0;
    }

    for (uint32_t file = 0; file < count; ++file) {
        (void)read_be32();
        uint16_t selector = read_be16();
        (void)read_be16();

        char name[56];
        for (unsigned int i = 0; i < sizeof(name); ++i) {
            name[i] = (char)FW_CFG_DATA;
        }
        if (names_equal(name, wanted_name, sizeof(name))) {
            return selector;
        }
    }
    return 0;
}

struct ramfb_config {
    uint64_t address;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed));

struct fw_cfg_dma_access {
    volatile uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed, aligned(8)));

static struct ramfb_config ramfb_config;
static struct fw_cfg_dma_access fw_cfg_dma_access;

static int fw_cfg_dma_write(uint16_t selector, const void *data, uint32_t length) {
    fw_cfg_dma_access.control =
        byte_swap32(((uint32_t)selector << 16) | FW_CFG_DMA_SELECT | FW_CFG_DMA_WRITE);
    fw_cfg_dma_access.length = byte_swap32(length);
    fw_cfg_dma_access.address = byte_swap64((uint64_t)(uintptr_t)data);

    __asm__ volatile("dsb sy" ::: "memory");
    FW_CFG_DMA = byte_swap64((uint64_t)(uintptr_t)&fw_cfg_dma_access);
    __asm__ volatile("dsb sy" ::: "memory");

    for (uint32_t spins = 0; spins < 1000000U; ++spins) {
        uint32_t control = byte_swap32(fw_cfg_dma_access.control);
        if (control == 0) {
            return 1;
        }
        if (control & FW_CFG_DMA_ERROR) {
            return 0;
        }
    }
    return 0;
}

static int configure_ramfb(void) {
    uart_puts("Locating QEMU ramfb...\n");
    uint16_t selector = fw_cfg_find_file("etc/ramfb");
    if (selector == 0) {
        return 0;
    }

    uart_puts("Configuring framebuffer...\n");
    ramfb_config.address = byte_swap64(FRAMEBUFFER_ADDRESS);
    ramfb_config.fourcc =
        byte_swap32(0x34325258U); // DRM_FORMAT_XRGB8888 ('X', 'R', '2', '4')
    ramfb_config.flags = 0;
    ramfb_config.width = byte_swap32(SCREEN_WIDTH);
    ramfb_config.height = byte_swap32(SCREEN_HEIGHT);
    ramfb_config.stride = byte_swap32(SCREEN_STRIDE);

    if (!fw_cfg_dma_write(selector, &ramfb_config, sizeof(ramfb_config))) {
        return 0;
    }
    uart_puts("Framebuffer configured.\n");
    return 1;
}

static void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= (int)SCREEN_WIDTH || y >= (int)SCREEN_HEIGHT) {
        return;
    }
    framebuffer[(uint32_t)y * SCREEN_WIDTH + (uint32_t)x] = color;
}

static void fill_rect(int x, int y, int width, int height, uint32_t color) {
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            put_pixel(column, row, color);
        }
    }
}

static void fill_dithered_rect(int x, int y, int width, int height,
                               uint32_t foreground, uint32_t background,
                               uint8_t coverage) {
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            uint8_t threshold = bayer8[row & 7][column & 7];
            put_pixel(column, row, threshold < coverage ? foreground : background);
        }
    }
}

static const uint8_t *glyph_rows(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
        if (font[i].character == character) {
            return font[i].rows;
        }
    }
    return font[0].rows;
}

static void draw_character(int x, int y, char character, uint32_t color, int scale) {
    const uint8_t *rows = glyph_rows(character);
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
            if (rows[row] & (1U << (4 - column))) {
                fill_rect(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint32_t color, int scale) {
    while (*text) {
        draw_character(x, y, *text++, color, scale);
        x += 6 * scale;
    }
}

static void draw_icon(int x, int y, uint32_t color, int document) {
    if (document) {
        fill_rect(x + 4, y + 1, 18, 26, color);
        fill_rect(x + 7, y + 6, 12, 2, VOID_COLOR);
        fill_rect(x + 7, y + 12, 10, 2, VOID_COLOR);
        fill_rect(x + 7, y + 18, 12, 2, VOID_COLOR);
    } else {
        fill_rect(x + 1, y + 8, 26, 18, color);
        fill_rect(x + 4, y + 4, 10, 6, color);
        fill_rect(x + 5, y + 12, 18, 2, VOID_COLOR);
    }
}

static void draw_grain(void) {
    // Deterministic and sparse: texture should be perceived, not announced.
    uint32_t state = 0xC4A71A2BU;
    for (uint32_t y = 24; y < SCREEN_HEIGHT; ++y) {
        for (uint32_t x = 0; x < SCREEN_WIDTH; ++x) {
            state = state * 1664525U + 1013904223U;
            if ((state >> 24) == 0 && bayer8[y & 7][x & 7] < 24) {
                framebuffer[y * SCREEN_WIDTH + x] = DEEP_COLOR;
            }
        }
    }
}

static void draw_desktop(void) {
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, VOID_COLOR);
    draw_grain();

    // Quiet top bar: opaque mass and a single value-based separator.
    fill_rect(0, 0, SCREEN_WIDTH, 24, DEEP_COLOR);
    fill_rect(0, 23, SCREEN_WIDTH, 1, SHADOW_COLOR);
    draw_text(22, 8, "DIGITAL CAVIAR", SURFACE_COLOR, 1);
    draw_text(158, 8, "FILE", DIM_COLOR, 1);
    draw_text(204, 8, "VIEW", DIM_COLOR, 1);

    // Bayer-stepped shadow under the primary light mass.
    fill_dithered_rect(82, 78, 424, 300, SHADOW_COLOR, VOID_COLOR, 46);
    fill_dithered_rect(78, 74, 424, 300, SHADOW_COLOR, VOID_COLOR, 24);

    fill_rect(68, 62, 424, 300, SURFACE_COLOR);
    fill_rect(68, 62, 424, 28, DIM_COLOR);
    fill_rect(68, 89, 424, 1, INK_COLOR);
    fill_rect(78, 71, 9, 9, INK_COLOR);
    draw_text(103, 72, "HARVESTER", INK_COLOR, 1);

    // A narrow content anchor leaves most of the window deliberately empty.
    draw_text(114, 116, "SYSTEM", INK_COLOR, 1);
    draw_text(114, 154, "SCRIPT", INK_COLOR, 1);
    draw_text(114, 192, "ADJUST", INK_COLOR, 1);
    draw_icon(78, 108, INK_COLOR, 0);
    draw_icon(78, 146, INK_COLOR, 1);
    draw_icon(78, 184, INK_COLOR, 0);

    // The sole grid break: one warm document set apart from the anchor.
    draw_icon(390, 278, BREAK_COLOR, 1);
    draw_text(377, 312, "NOTES", INK_COLOR, 1);

    // One focal state, represented as a restrained value block.
    fill_rect(106, 110, 92, 20, ACCENT_COLOR);
    draw_text(114, 116, "SYSTEM", SURFACE_COLOR, 1);

    draw_text(22, 448, "0.1 FOUNDATION", DIM_COLOR, 1);
}

void kmain(void) {
    uart_puts("\nDIGITAL CAVIAR [OS]\n");
    uart_puts("Establishing graphical foundation...\n");

    if (!configure_ramfb()) {
        uart_puts("ramfb unavailable; start QEMU with -device ramfb\n");
        while (1) {
            __asm__ volatile("wfe");
        }
    }

    draw_desktop();
    __asm__ volatile("dsb sy");
    uart_puts("Desktop ready: 640x480 XRGB8888\n");

    while (1) {
        __asm__ volatile("wfe");
    }
}

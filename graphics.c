#include "os.h"

#define FW_CFG_BASE     0x09020000UL
#define FW_CFG_DATA     (*(volatile uint8_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SELECTOR (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA      (*(volatile uint64_t *)(FW_CFG_BASE + 0x10))
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_DMA_ERROR  0x01U
#define FW_CFG_DMA_SELECT 0x08U
#define FW_CFG_DMA_WRITE  0x10U

#define FRONTBUFFER_ADDRESS 0x41000000UL
#define BACKBUFFER_ADDRESS  0x41200000UL
#define SCREEN_STRIDE ((uint32_t)SCREEN_WIDTH * 4U)

static volatile uint32_t *const frontbuffer =
    (volatile uint32_t *)FRONTBUFFER_ADDRESS;
static uint32_t *const backbuffer = (uint32_t *)BACKBUFFER_ADDRESS;
static uint32_t background[SCREEN_WIDTH * SCREEN_HEIGHT];
static int background_ready;

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

/* Chicago-like system face (original Digital Caviar glyphs). */
static const struct glyph font_chicago[] = {
    {' ', {0,0,0,0,0,0,0}}, {'!', {4,4,4,4,4,0,4}},
    {'"', {10,10,10,0,0,0,0}}, {'#', {10,31,10,10,31,10,0}},
    {'%', {17,2,4,8,17,0,0}}, {'&', {12,18,20,8,21,18,13}},
    {'\'',{4,4,2,0,0,0,0}}, {'(', {2,4,8,8,8,4,2}},
    {')', {8,4,2,2,2,4,8}}, {'*', {0,10,4,31,4,10,0}},
    {'+', {0,4,4,31,4,4,0}}, {',', {0,0,0,0,6,4,8}},
    {'-', {0,0,0,31,0,0,0}}, {'.', {0,0,0,0,0,12,12}},
    {'/', {1,2,4,8,16,0,0}}, {':', {0,12,12,0,12,12,0}},
    {';', {0,12,12,0,6,4,8}}, {'<', {2,4,8,16,8,4,2}},
    {'=', {0,0,31,0,31,0,0}}, {'>', {8,4,2,1,2,4,8}},
    {'?', {14,17,1,2,4,0,4}}, {'@', {14,17,23,21,23,16,14}},
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
    {'[', {14,8,8,8,8,8,14}}, {'\\',{16,8,4,2,1,0,0}},
    {']', {14,2,2,2,2,2,14}}, {'_', {0,0,0,0,0,0,31}},
};

/* Geneva-like: slightly condensed / rounded. */
static const struct glyph font_geneva[] = {
    {' ', {0,0,0,0,0,0,0}}, {'!', {4,4,4,4,0,0,4}},
    {'"', {10,10,0,0,0,0,0}}, {'#', {10,31,10,31,10,0,0}},
    {'%', {19,20,8,4,2,19,0}}, {'&', {4,10,4,10,17,17,14}},
    {'\'',{4,4,0,0,0,0,0}}, {'(', {2,4,4,4,4,4,2}},
    {')', {8,4,4,4,4,4,8}}, {'*', {0,4,21,14,21,4,0}},
    {'+', {0,4,4,31,4,4,0}}, {',', {0,0,0,0,4,4,8}},
    {'-', {0,0,0,14,0,0,0}}, {'.', {0,0,0,0,0,4,0}},
    {'/', {1,2,4,8,16,0,0}}, {':', {0,4,0,0,4,0,0}},
    {';', {0,4,0,0,4,4,8}}, {'<', {2,4,8,16,8,4,2}},
    {'=', {0,0,31,0,31,0,0}}, {'>', {8,4,2,1,2,4,8}},
    {'?', {14,17,1,2,4,0,4}}, {'@', {14,17,23,21,22,16,15}},
    {'0', {14,17,17,17,17,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,6,8,16,31}}, {'3', {14,17,1,6,1,17,14}},
    {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,30,1,1,17,14}},
    {'6', {14,16,16,30,17,17,14}}, {'7', {31,1,2,4,4,4,4}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,1,14}},
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
    {'[', {14,8,8,8,8,8,14}}, {'\\',{16,8,4,2,1,0,0}},
    {']', {14,2,2,2,2,2,14}}, {'_', {0,0,0,0,0,0,31}},
};

/* London-like: decorative / blackletter-ish capitals. */
static const struct glyph font_london[] = {
    {' ', {0,0,0,0,0,0,0}}, {'!', {4,4,4,4,0,4,4}},
    {'"', {10,10,10,0,0,0,0}}, {'#', {10,31,10,10,31,10,0}},
    {'%', {17,2,4,8,17,0,0}}, {'&', {12,18,20,8,21,18,13}},
    {'\'',{4,4,2,0,0,0,0}}, {'(', {2,4,8,8,8,4,2}},
    {')', {8,4,2,2,2,4,8}}, {'*', {4,21,14,4,14,21,4}},
    {'+', {0,4,4,31,4,4,0}}, {',', {0,0,0,0,6,4,8}},
    {'-', {0,0,0,31,0,0,0}}, {'.', {0,0,0,0,0,12,12}},
    {'/', {1,2,4,8,16,0,0}}, {':', {0,12,12,0,12,12,0}},
    {';', {0,12,12,0,6,4,8}}, {'<', {2,4,8,16,8,4,2}},
    {'=', {0,0,31,0,31,0,0}}, {'>', {8,4,2,1,2,4,8}},
    {'?', {14,17,1,2,4,0,4}}, {'@', {14,17,23,21,23,16,14}},
    {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,30,1,1,17,14}},
    {'6', {6,8,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,2,12}},
    {'A', {4,14,17,17,31,17,17}}, {'B', {28,18,18,28,18,18,28}},
    {'C', {14,17,16,16,16,17,14}}, {'D', {28,18,17,17,17,18,28}},
    {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
    {'G', {14,17,16,19,17,17,15}}, {'H', {17,17,17,31,17,17,17}},
    {'I', {14,4,4,4,4,4,14}}, {'J', {7,2,2,2,18,18,12}},
    {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}}, {'N', {17,17,25,21,19,17,17}},
    {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,14,1,1,17,30}}, {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,10,10,4}},
    {'W', {17,17,17,21,21,10,10}}, {'X', {17,10,4,4,4,10,17}},
    {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
    {'[', {14,8,8,8,8,8,14}}, {'\\',{16,8,4,2,1,0,0}},
    {']', {14,2,2,2,2,2,14}}, {'_', {0,0,0,0,0,0,31}},
};

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

static struct ramfb_config ramfb;
static struct fw_cfg_dma_access dma;

static uint16_t swap16(uint16_t value) { return __builtin_bswap16(value); }
static uint32_t swap32(uint32_t value) { return __builtin_bswap32(value); }
static uint64_t swap64(uint64_t value) { return __builtin_bswap64(value); }

static uint32_t read_be32(void) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | FW_CFG_DATA;
    return value;
}

static uint16_t read_be16(void) {
    uint16_t value = (uint16_t)FW_CFG_DATA << 8;
    return (uint16_t)(value | FW_CFG_DATA);
}

static int equal_name(const char *left, const char *right) {
    for (int i = 0; i < 56; ++i) {
        if (left[i] != right[i]) return 0;
        if (left[i] == '\0') return 1;
    }
    return 1;
}

static uint16_t find_fw_file(const char *wanted) {
    FW_CFG_SELECTOR = swap16(FW_CFG_FILE_DIR);
    uint32_t count = read_be32();
    if (count > 256) return 0;
    for (uint32_t file = 0; file < count; ++file) {
        (void)read_be32();
        uint16_t selector = read_be16();
        (void)read_be16();
        char name[56];
        for (int i = 0; i < 56; ++i) name[i] = (char)FW_CFG_DATA;
        if (equal_name(name, wanted)) return selector;
    }
    return 0;
}

static int dma_write(uint16_t selector, const void *data, uint32_t length) {
    dma.control = swap32(((uint32_t)selector << 16) |
                         FW_CFG_DMA_SELECT | FW_CFG_DMA_WRITE);
    dma.length = swap32(length);
    dma.address = swap64((uint64_t)(uintptr_t)data);
    __asm__ volatile("dsb sy" ::: "memory");
    FW_CFG_DMA = swap64((uint64_t)(uintptr_t)&dma);
    __asm__ volatile("dsb sy" ::: "memory");
    for (uint32_t spin = 0; spin < 1000000; ++spin) {
        uint32_t control = swap32(dma.control);
        if (control == 0) return 1;
        if (control & FW_CFG_DMA_ERROR) return 0;
    }
    return 0;
}

int graphics_init(void) {
    uint16_t selector = find_fw_file("etc/ramfb");
    if (!selector) return 0;
    ramfb.address = swap64(FRONTBUFFER_ADDRESS);
    ramfb.fourcc = swap32(0x34325258U);
    ramfb.flags = 0;
    ramfb.width = swap32(SCREEN_WIDTH);
    ramfb.height = swap32(SCREEN_HEIGHT);
    ramfb.stride = swap32(SCREEN_STRIDE);
    return dma_write(selector, &ramfb, sizeof(ramfb));
}

static void pixel(int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        backbuffer[y * SCREEN_WIDTH + x] = color;
}

void gfx_fill(struct rect area, uint32_t color) {
    int x0 = area.x < 0 ? 0 : area.x;
    int y0 = area.y < 0 ? 0 : area.y;
    int x1 = area.x + area.width > SCREEN_WIDTH ? SCREEN_WIDTH : area.x + area.width;
    int y1 = area.y + area.height > SCREEN_HEIGHT ? SCREEN_HEIGHT : area.y + area.height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) backbuffer[y * SCREEN_WIDTH + x] = color;
}

void gfx_dither(struct rect area, uint32_t foreground, uint32_t background_color,
                uint8_t coverage) {
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x)
            pixel(x, y, bayer8[y & 7][x & 7] < coverage ? foreground
                                                        : background_color);
}

void gfx_hline(int x, int y, int width, uint32_t color) {
    for (int column = 0; column < width; ++column) pixel(x + column, y, color);
}

void gfx_vline(int x, int y, int height, uint32_t color) {
    for (int row = 0; row < height; ++row) pixel(x, y + row, color);
}

void gfx_rect_outline(struct rect area, uint32_t color) {
    gfx_hline(area.x, area.y, area.width, color);
    gfx_hline(area.x, area.y + area.height - 1, area.width, color);
    gfx_vline(area.x, area.y, area.height, color);
    gfx_vline(area.x + area.width - 1, area.y, area.height, color);
}

static const struct glyph *font_table(enum dc_font font, size_t *count) {
    if (font == DC_FONT_GENEVA) {
        *count = sizeof(font_geneva) / sizeof(font_geneva[0]);
        return font_geneva;
    }
    if (font == DC_FONT_LONDON) {
        *count = sizeof(font_london) / sizeof(font_london[0]);
        return font_london;
    }
    *count = sizeof(font_chicago) / sizeof(font_chicago[0]);
    return font_chicago;
}

static const uint8_t *glyph_rows(char character, enum dc_font font) {
    size_t count = 0;
    const struct glyph *table = font_table(font, &count);
    if (character >= 'a' && character <= 'z') character -= ('a' - 'A');
    for (size_t i = 0; i < count; ++i)
        if (table[i].character == character) return table[i].rows;
    return table[0].rows;
}

int gfx_font_advance(enum dc_font font, int scale) {
    if (font == DC_FONT_GENEVA) return 5 * scale;
    if (font == DC_FONT_LONDON) return 7 * scale;
    return 6 * scale;
}

int gfx_font_height(int scale) { return 9 * scale; }

static void draw_glyph(int x, int y, char value, uint32_t color, int scale,
                       enum dc_font font, uint8_t style) {
    const uint8_t *rows = glyph_rows(value, font);
    int bold = (style & DC_STYLE_BOLD) != 0;
    int london = font == DC_FONT_LONDON;
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
            if (rows[row] & (1U << (4 - column))) {
                gfx_fill((struct rect){x + column * scale, y + row * scale,
                                       scale, scale},
                         color);
                if (bold || london) {
                    gfx_fill((struct rect){x + column * scale + scale,
                                           y + row * scale, scale, scale},
                             color);
                }
                if (london && row == 0) {
                    gfx_fill((struct rect){x + column * scale,
                                           y + row * scale - scale, scale,
                                           scale},
                             color);
                }
            }
        }
    }
    if (style & DC_STYLE_UNDERLINE) {
        int width = 5 * scale + ((bold || london) ? scale : 0);
        gfx_fill((struct rect){x, y + 7 * scale, width, scale > 1 ? 2 : 1},
                 color);
    }
}

void gfx_text_font(int x, int y, const char *text, uint32_t color, int scale,
                   enum dc_font font, uint8_t style) {
    int origin = x;
    while (*text) {
        if (*text == '\n') {
            y += gfx_font_height(scale);
            x = origin;
        } else {
            draw_glyph(x, y, *text, color, scale, font, style);
            x += gfx_font_advance(font, scale);
        }
        ++text;
    }
}

void gfx_text(int x, int y, const char *text, uint32_t color, int scale) {
    gfx_text_font(x, y, text, color, scale, DC_FONT_CHICAGO, 0);
}

void gfx_icon(int x, int y, uint32_t color, int document) {
    if (document) {
        gfx_fill((struct rect){x + 4, y + 1, 18, 26}, color);
        gfx_fill((struct rect){x + 7, y + 6, 12, 2}, DC_VOID);
        gfx_fill((struct rect){x + 7, y + 12, 10, 2}, DC_VOID);
        gfx_fill((struct rect){x + 7, y + 18, 12, 2}, DC_VOID);
    } else {
        gfx_fill((struct rect){x + 1, y + 8, 26, 18}, color);
        gfx_fill((struct rect){x + 4, y + 4, 10, 6}, color);
        gfx_fill((struct rect){x + 5, y + 12, 18, 2}, DC_VOID);
    }
}

void gfx_paint_icon(int x, int y, uint32_t color) {
    gfx_fill((struct rect){x + 2, y + 2, 22, 22}, DC_SURFACE);
    gfx_rect_outline((struct rect){x + 2, y + 2, 22, 22}, color);
    gfx_fill((struct rect){x + 6, y + 6, 6, 6}, DC_BREAK);
    gfx_fill((struct rect){x + 14, y + 8, 6, 6}, DC_ACCENT);
    gfx_fill((struct rect){x + 8, y + 14, 10, 6}, color);
}

void gfx_bitmap(int x, int y, const uint8_t *bits, int width, int height,
                uint32_t ink, uint32_t paper, int draw_paper) {
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            uint8_t on = bits[row * width + column];
            if (on) {
                pixel(x + column, y + row, ink);
            } else if (draw_paper) {
                pixel(x + column, y + row, paper);
            }
        }
    }
}

void gfx_cursor(int x, int y) {
    static const uint8_t tip[11] = {1, 2, 3, 4, 5, 6, 7, 8, 3, 3, 3};
    for (int row = 0; row < 11; ++row) {
        for (int column = 0; column < tip[row]; ++column) {
            pixel(x + column, y + row, DC_INK);
            if (column == tip[row] - 1 || row == 0)
                pixel(x + column, y + row, DC_SURFACE);
        }
        pixel(x + tip[row], y + row, DC_INK);
    }
    gfx_hline(x + 1, y + 11, 3, DC_INK);
    gfx_hline(x + 2, y + 12, 2, DC_INK);
}

void graphics_prepare_background(void) {
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            background[y * SCREEN_WIDTH + x] = DC_VOID;
        }
    }
    uint32_t state = 0xC4A71A2BU;
    for (int y = 24; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            state = state * 1664525U + 1013904223U;
            if ((state >> 24) == 0 && bayer8[y & 7][x & 7] < 24) {
                background[y * SCREEN_WIDTH + x] = DC_DEEP;
            }
        }
    }
    background_ready = 1;
}

void graphics_begin(void) {
    if (!background_ready) {
        graphics_prepare_background();
    }
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        backbuffer[i] = background[i];
    }
}

void graphics_present(void) {
    __asm__ volatile("dmb oshst" ::: "memory");
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i)
        frontbuffer[i] = backbuffer[i];
    __asm__ volatile("dsb sy" ::: "memory");
}

#ifndef DIGITAL_CAVIAR_OS_H
#define DIGITAL_CAVIAR_OS_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480
#define DOCUMENT_CAPACITY 2048
#define PAINT_WIDTH  256
#define PAINT_HEIGHT 160
#define CLIP_TEXT_CAPACITY DOCUMENT_CAPACITY
#define RICH_ATTR_BYTES DOCUMENT_CAPACITY
#define EMBED_META_BYTES 512
#define EMBED_IMAGE_BYTES (PAINT_WIDTH * PAINT_HEIGHT)
#define EMBED_IMAGE_SECTORS ((EMBED_IMAGE_BYTES + 511) / 512)
#define SCRIPT_TEXT_SECTORS 4
#define SCRIPT_ATTR_SECTORS 4
#define SCRIPT_SLOT_SECTORS (1 + SCRIPT_TEXT_SECTORS + SCRIPT_ATTR_SECTORS + 1 + EMBED_IMAGE_SECTORS)

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define DC_VOID    RGB(12, 12, 14)
#define DC_DEEP    RGB(25, 25, 28)
#define DC_SHADOW  RGB(40, 39, 39)
#define DC_DIM     RGB(176, 173, 166)
#define DC_SURFACE RGB(232, 228, 220)
#define DC_INK     RGB(21, 21, 22)
#define DC_ACCENT  RGB(126, 141, 153)
#define DC_BREAK   RGB(183, 156, 118)
#define DC_SELECT  RGB(90, 110, 140)

struct rect {
    int x;
    int y;
    int width;
    int height;
};

enum dc_event_kind {
    DC_EVENT_NONE,
    DC_EVENT_POINTER_MOVE,
    DC_EVENT_POINTER_BUTTON,
    DC_EVENT_KEY,
};

struct dc_event {
    enum dc_event_kind kind;
    int x;
    int y;
    uint16_t code;
    int32_t value;
};

enum dc_app {
    DC_APP_HARVESTER,
    DC_APP_SCRIPT,
    DC_APP_PAINT,
};

enum storage_load_result {
    STORAGE_LOAD_OK,
    STORAGE_LOAD_NO_MEDIA,
    STORAGE_LOAD_EMPTY,
    STORAGE_LOAD_CORRUPT,
};

enum dc_font {
    DC_FONT_CHICAGO = 0,
    DC_FONT_GENEVA = 1,
    DC_FONT_LONDON = 2,
    DC_FONT_COUNT = 3,
};

enum dc_style_bits {
    DC_STYLE_BOLD = 1,
    DC_STYLE_UNDERLINE = 2,
};

enum paint_tool {
    PAINT_PENCIL = 0,
    PAINT_ERASER = 1,
    PAINT_LINE = 2,
    PAINT_RECT = 3,
    PAINT_OVAL = 4,
    PAINT_SELECT = 5,
};

enum clip_kind {
    CLIP_NONE = 0,
    CLIP_TEXT = 1,
    CLIP_IMAGE = 2,
};

enum menu_id {
    MENU_NONE = 0,
    MENU_FILE = 1,
    MENU_EDIT = 2,
    MENU_FONT = 3,
    MENU_STYLE = 4,
    MENU_SIZE = 5,
    MENU_VIEW = 6,
};

struct window_state {
    struct rect frame;
    int visible;
};

struct clipboard_state {
    enum clip_kind kind;
    char text[CLIP_TEXT_CAPACITY + 1];
    size_t text_length;
    uint8_t image[PAINT_WIDTH * PAINT_HEIGHT];
    int image_width;
    int image_height;
};

struct desktop_state {
    struct window_state harvester;
    struct window_state script;
    struct window_state paint;
    enum dc_app front_app;
    int pointer_x;
    int pointer_y;
    int pointer_down;
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
    enum menu_id menu_open;
    int about_open;
    int confirm_new_open;
    int save_notice;
    int clip_truncated;
    int shift_down;
    enum storage_load_result storage_status;

    char document[DOCUMENT_CAPACITY + 1];
    uint8_t doc_font[DOCUMENT_CAPACITY];
    uint8_t doc_style[DOCUMENT_CAPACITY];
    uint8_t doc_size[DOCUMENT_CAPACITY];
    size_t document_length;
    size_t caret;
    size_t sel_anchor;
    int selecting;
    int has_selection;
    enum dc_font typing_font;
    uint8_t typing_style;
    uint8_t typing_size;
    int document_dirty;

    int embed_valid;
    uint8_t embed_image[PAINT_WIDTH * PAINT_HEIGHT];
    int embed_width;
    int embed_height;

    uint8_t canvas[PAINT_WIDTH * PAINT_HEIGHT];
    enum paint_tool tool;
    int paint_pattern;
    int stroke_active;
    int stroke_x0;
    int stroke_y0;
    int stroke_x1;
    int stroke_y1;
    int has_marquee;
    int marquee_x0;
    int marquee_y0;
    int marquee_x1;
    int marquee_y1;

    struct clipboard_state clipboard;
    int needs_redraw;
};

void uart_puts(const char *text);

void exceptions_init(void);

int graphics_init(void);
void graphics_prepare_background(void);
void graphics_begin(void);
void graphics_present(void);
void gfx_fill(struct rect area, uint32_t color);
void gfx_dither(struct rect area, uint32_t foreground, uint32_t background,
                uint8_t coverage);
void gfx_text(int x, int y, const char *text, uint32_t color, int scale);
void gfx_text_font(int x, int y, const char *text, uint32_t color, int scale,
                   enum dc_font font, uint8_t style);
int gfx_font_advance(enum dc_font font, int scale);
int gfx_font_height(int scale);
void gfx_icon(int x, int y, uint32_t color, int document);
void gfx_paint_icon(int x, int y, uint32_t color);
void gfx_cursor(int x, int y);
void gfx_hline(int x, int y, int width, uint32_t color);
void gfx_vline(int x, int y, int height, uint32_t color);
void gfx_rect_outline(struct rect area, uint32_t color);
void gfx_bitmap(int x, int y, const uint8_t *bits, int width, int height,
                uint32_t ink, uint32_t paper, int draw_paper);

void virtio_init(const void *dtb);
int input_poll(struct dc_event *event);
int storage_available(void);
enum storage_load_result storage_load_document(char *text, size_t capacity,
                                               size_t *length);
int storage_save_document(const char *text, size_t length);

struct script_store {
    char text[DOCUMENT_CAPACITY + 1];
    size_t length;
    uint8_t font[DOCUMENT_CAPACITY];
    uint8_t style[DOCUMENT_CAPACITY];
    uint8_t size[DOCUMENT_CAPACITY];
    int embed_valid;
    int embed_width;
    int embed_height;
    uint8_t embed_image[PAINT_WIDTH * PAINT_HEIGHT];
};

enum storage_load_result storage_load_script(struct script_store *doc);
int storage_save_script(const struct script_store *doc);

void desktop_init(struct desktop_state *desktop);
void desktop_handle_event(struct desktop_state *desktop,
                          const struct dc_event *event);
void desktop_render(const struct desktop_state *desktop);

#endif

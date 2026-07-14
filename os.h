#ifndef DIGITAL_CAVIAR_OS_H
#define DIGITAL_CAVIAR_OS_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480
#define DOCUMENT_CAPACITY 2048

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define DC_VOID    RGB(12, 12, 14)
#define DC_DEEP    RGB(25, 25, 28)
#define DC_SHADOW  RGB(40, 39, 39)
#define DC_DIM     RGB(176, 173, 166)
#define DC_SURFACE RGB(232, 228, 220)
#define DC_INK     RGB(21, 21, 22)
#define DC_ACCENT  RGB(126, 141, 153)
#define DC_BREAK   RGB(183, 156, 118)

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
};

enum storage_load_result {
    STORAGE_LOAD_OK,
    STORAGE_LOAD_NO_MEDIA,
    STORAGE_LOAD_EMPTY,
    STORAGE_LOAD_CORRUPT,
};

struct window_state {
    struct rect frame;
    int visible;
};

struct desktop_state {
    struct window_state harvester;
    struct window_state script;
    enum dc_app front_app;
    int pointer_x;
    int pointer_y;
    int pointer_down;
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
    int menu_open;
    int about_open;
    int save_notice;
    int shift_down;
    enum storage_load_result storage_status;
    char document[DOCUMENT_CAPACITY + 1];
    size_t document_length;
    int document_dirty;
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
void gfx_icon(int x, int y, uint32_t color, int document);
void gfx_cursor(int x, int y);

void virtio_init(const void *dtb);
int input_poll(struct dc_event *event);
int storage_available(void);
enum storage_load_result storage_load_document(char *text, size_t capacity,
                                               size_t *length);
int storage_save_document(const char *text, size_t length);

void desktop_init(struct desktop_state *desktop);
void desktop_handle_event(struct desktop_state *desktop,
                          const struct dc_event *event);
void desktop_render(const struct desktop_state *desktop);

#endif

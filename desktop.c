#include "os.h"

static int contains(struct rect area, int x, int y) {
    return x >= area.x && y >= area.y &&
           x < area.x + area.width && y < area.y + area.height;
}

static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }
static int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void zero_bytes(void *memory, size_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < size; ++i) out[i] = in[i];
}

static struct window_state *front_window(struct desktop_state *desktop) {
    if (desktop->front_app == DC_APP_SCRIPT) return &desktop->script;
    if (desktop->front_app == DC_APP_PAINT) return &desktop->paint;
    return &desktop->harvester;
}

static void close_window(struct desktop_state *desktop,
                         struct window_state *window);

static void ensure_shell_visible(struct desktop_state *desktop) {
    if (!desktop->harvester.visible && !desktop->script.visible &&
        !desktop->paint.visible) {
        desktop->harvester.visible = 1;
        desktop->front_app = DC_APP_HARVESTER;
    }
}

static void bring_app(struct desktop_state *desktop, enum dc_app app) {
    desktop->front_app = app;
    if (app == DC_APP_SCRIPT) desktop->script.visible = 1;
    if (app == DC_APP_PAINT) desktop->paint.visible = 1;
    if (app == DC_APP_HARVESTER) desktop->harvester.visible = 1;
}

static void open_script(struct desktop_state *desktop) {
    bring_app(desktop, DC_APP_SCRIPT);
}

static void open_paint(struct desktop_state *desktop) {
    bring_app(desktop, DC_APP_PAINT);
}

static void open_harvester(struct desktop_state *desktop) {
    bring_app(desktop, DC_APP_HARVESTER);
}

static void selection_bounds(const struct desktop_state *desktop, size_t *start,
                             size_t *end) {
    size_t a = desktop->sel_anchor;
    size_t b = desktop->caret;
    if (a > b) {
        size_t tmp = a;
        a = b;
        b = tmp;
    }
    *start = a;
    *end = b;
}

static int selection_active(const struct desktop_state *desktop) {
    size_t start = 0;
    size_t end = 0;
    selection_bounds(desktop, &start, &end);
    return desktop->has_selection && start != end;
}

static void clear_selection(struct desktop_state *desktop) {
    desktop->has_selection = 0;
    desktop->sel_anchor = desktop->caret;
    desktop->selecting = 0;
}

static void set_default_document(struct desktop_state *desktop) {
    const char *welcome =
        "Digital Caviar\n\n"
        "A quiet machine with a loud idea:\n"
        "write, paint, and paste between them.\n\n"
        "Try Font, Style, and Size.";
    desktop->document_length = 0;
    while (welcome[desktop->document_length] &&
           desktop->document_length < DOCUMENT_CAPACITY) {
        size_t i = desktop->document_length;
        desktop->document[i] = welcome[i];
        desktop->doc_font[i] = DC_FONT_CHICAGO;
        desktop->doc_style[i] = 0;
        desktop->doc_size[i] = 1;
        ++desktop->document_length;
    }
    desktop->document[desktop->document_length] = '\0';
    desktop->caret = desktop->document_length;
    clear_selection(desktop);
}

static void save_document(struct desktop_state *desktop) {
    /* script_store is ~50KiB — must not live on the 16KiB boot stack. */
    static struct script_store store;
    zero_bytes(&store, sizeof(store));
    copy_bytes(store.text, desktop->document, desktop->document_length);
    store.text[desktop->document_length] = '\0';
    store.length = desktop->document_length;
    copy_bytes(store.font, desktop->doc_font, desktop->document_length);
    copy_bytes(store.style, desktop->doc_style, desktop->document_length);
    copy_bytes(store.size, desktop->doc_size, desktop->document_length);
    store.embed_valid = desktop->embed_valid;
    store.embed_width = desktop->embed_width;
    store.embed_height = desktop->embed_height;
    if (desktop->embed_valid) {
        copy_bytes(store.embed_image, desktop->embed_image,
                   (size_t)desktop->embed_width * desktop->embed_height);
    }
    desktop->save_notice = storage_save_script(&store) ? 1 : -1;
    if (desktop->save_notice > 0) {
        desktop->document_dirty = 0;
        desktop->storage_status = STORAGE_LOAD_OK;
    }
}

static void canvas_clear(struct desktop_state *desktop) {
    zero_bytes(desktop->canvas, sizeof(desktop->canvas));
}

static void canvas_set(struct desktop_state *desktop, int x, int y, uint8_t on) {
    if (x < 0 || y < 0 || x >= PAINT_WIDTH || y >= PAINT_HEIGHT) return;
    desktop->canvas[y * PAINT_WIDTH + x] = on ? 1 : 0;
}

static uint8_t canvas_get(const struct desktop_state *desktop, int x, int y) {
    if (x < 0 || y < 0 || x >= PAINT_WIDTH || y >= PAINT_HEIGHT) return 0;
    return desktop->canvas[y * PAINT_WIDTH + x];
}

static void canvas_brush(struct desktop_state *desktop, int x, int y, int erase) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            canvas_set(desktop, x + dx, y + dy, erase ? 0 : 1);
}

static void canvas_line(struct desktop_state *desktop, int x0, int y0, int x1,
                        int y1, int erase) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = maxi(maxi(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy), 1);
    for (int i = 0; i <= steps; ++i) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        canvas_brush(desktop, x, y, erase);
    }
}

static int pattern_on(int pattern, int x, int y) {
    static const uint8_t pats[4][8] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},
        {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    pattern = clampi(pattern, 0, 3);
    return (pats[pattern][y & 7] >> (7 - (x & 7))) & 1;
}

static void canvas_rect(struct desktop_state *desktop, int x0, int y0, int x1,
                        int y1, int filled) {
    int left = mini(x0, x1);
    int right = maxi(x0, x1);
    int top = mini(y0, y1);
    int bottom = maxi(y0, y1);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            int edge = x == left || x == right || y == top || y == bottom;
            if (filled) {
                if (pattern_on(desktop->paint_pattern, x, y))
                    canvas_set(desktop, x, y, 1);
            } else if (edge) {
                canvas_set(desktop, x, y, 1);
            }
        }
    }
}

static void canvas_oval(struct desktop_state *desktop, int x0, int y0, int x1,
                        int y1, int filled) {
    int left = mini(x0, x1);
    int right = maxi(x0, x1);
    int top = mini(y0, y1);
    int bottom = maxi(y0, y1);
    int cx = (left + right) / 2;
    int cy = (top + bottom) / 2;
    int rx = maxi((right - left) / 2, 1);
    int ry = maxi((bottom - top) / 2, 1);
    long outer = (long)rx * rx * (long)ry * ry;
    long inner_rx = rx > 1 ? rx - 1 : 0;
    long inner_ry = ry > 1 ? ry - 1 : 0;
    long inner = inner_rx * inner_rx * inner_ry * inner_ry;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            long dx = x - cx;
            long dy = y - cy;
            long value = dx * dx * ry * ry + dy * dy * rx * rx;
            if (filled) {
                if (value <= outer && pattern_on(desktop->paint_pattern, x, y))
                    canvas_set(desktop, x, y, 1);
            } else if (value <= outer && value >= inner) {
                canvas_set(desktop, x, y, 1);
            }
        }
    }
}

static struct rect paint_canvas_rect(const struct window_state *window) {
    return (struct rect){window->frame.x + 78, window->frame.y + 40,
                         PAINT_WIDTH, PAINT_HEIGHT};
}

static int map_to_canvas(const struct window_state *window, int x, int y,
                         int *cx, int *cy) {
    struct rect canvas = paint_canvas_rect(window);
    if (!contains(canvas, x, y)) return 0;
    *cx = x - canvas.x;
    *cy = y - canvas.y;
    return 1;
}

void desktop_init(struct desktop_state *desktop) {
    zero_bytes(desktop, sizeof(*desktop));
    desktop->harvester = (struct window_state){{48, 56, 360, 280}, 1};
    desktop->script = (struct window_state){{170, 70, 420, 320}, 0};
    desktop->paint = (struct window_state){{70, 50, 460, 360}, 0};
    desktop->front_app = DC_APP_HARVESTER;
    desktop->pointer_x = SCREEN_WIDTH / 2;
    desktop->pointer_y = SCREEN_HEIGHT / 2;
    desktop->typing_font = DC_FONT_CHICAGO;
    desktop->typing_style = 0;
    desktop->typing_size = 1;
    desktop->tool = PAINT_PENCIL;
    desktop->paint_pattern = 1;
    desktop->needs_redraw = 1;
    canvas_clear(desktop);

    {
        static struct script_store loaded;
        desktop->storage_status = storage_load_script(&loaded);
        if (desktop->storage_status != STORAGE_LOAD_OK) {
            set_default_document(desktop);
        } else {
            copy_bytes(desktop->document, loaded.text, loaded.length);
            desktop->document[loaded.length] = '\0';
            desktop->document_length = loaded.length;
            copy_bytes(desktop->doc_font, loaded.font, loaded.length);
            copy_bytes(desktop->doc_style, loaded.style, loaded.length);
            copy_bytes(desktop->doc_size, loaded.size, loaded.length);
            desktop->embed_valid = loaded.embed_valid;
            desktop->embed_width = loaded.embed_width;
            desktop->embed_height = loaded.embed_height;
            if (loaded.embed_valid) {
                copy_bytes(desktop->embed_image, loaded.embed_image,
                           (size_t)loaded.embed_width * loaded.embed_height);
            }
            desktop->caret = desktop->document_length;
            clear_selection(desktop);
        }
    }
}

static char key_character(uint16_t code, int shift) {
    static const char normal[] = {
        [2] = '1',  [3] = '2',  [4] = '3',  [5] = '4',  [6] = '5',  [7] = '6',
        [8] = '7',  [9] = '8',  [10] = '9', [11] = '0', [12] = '-', [13] = '=',
        [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r', [20] = 't', [21] = 'y',
        [22] = 'u', [23] = 'i', [24] = 'o', [25] = 'p', [26] = '[', [27] = ']',
        [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f', [34] = 'g', [35] = 'h',
        [36] = 'j', [37] = 'k', [38] = 'l', [39] = ';', [40] = '\'', [41] = '`',
        [43] = '\\', [44] = 'z', [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b',
        [49] = 'n', [50] = 'm', [51] = ',', [52] = '.', [53] = '/', [57] = ' ',
    };
    static const char shifted[] = {
        [2] = '!',  [3] = '@',  [4] = '#',  [5] = '$',  [6] = '%',  [7] = '^',
        [8] = '&',  [9] = '*',  [10] = '(', [11] = ')', [12] = '_', [13] = '+',
        [16] = 'Q', [17] = 'W', [18] = 'E', [19] = 'R', [20] = 'T', [21] = 'Y',
        [22] = 'U', [23] = 'I', [24] = 'O', [25] = 'P', [26] = '{', [27] = '}',
        [30] = 'A', [31] = 'S', [32] = 'D', [33] = 'F', [34] = 'G', [35] = 'H',
        [36] = 'J', [37] = 'K', [38] = 'L', [39] = ':', [40] = '"', [41] = '~',
        [43] = '|', [44] = 'Z', [45] = 'X', [46] = 'C', [47] = 'V', [48] = 'B',
        [49] = 'N', [50] = 'M', [51] = '<', [52] = '>', [53] = '?', [57] = ' ',
    };
    if (code >= sizeof(normal)) return 0;
    return shift ? shifted[code] : normal[code];
}

static void delete_range(struct desktop_state *desktop, size_t start, size_t end) {
    if (end <= start || end > desktop->document_length) return;
    size_t count = end - start;
    for (size_t i = start; i + count < desktop->document_length; ++i) {
        desktop->document[i] = desktop->document[i + count];
        desktop->doc_font[i] = desktop->doc_font[i + count];
        desktop->doc_style[i] = desktop->doc_style[i + count];
        desktop->doc_size[i] = desktop->doc_size[i + count];
    }
    desktop->document_length -= count;
    desktop->document[desktop->document_length] = '\0';
    desktop->caret = start;
    clear_selection(desktop);
    desktop->document_dirty = 1;
}

static void insert_char(struct desktop_state *desktop, char character) {
    if (selection_active(desktop)) {
        size_t start = 0;
        size_t end = 0;
        selection_bounds(desktop, &start, &end);
        delete_range(desktop, start, end);
    }
    if (desktop->document_length >= DOCUMENT_CAPACITY) return;
    for (size_t i = desktop->document_length; i > desktop->caret; --i) {
        desktop->document[i] = desktop->document[i - 1];
        desktop->doc_font[i] = desktop->doc_font[i - 1];
        desktop->doc_style[i] = desktop->doc_style[i - 1];
        desktop->doc_size[i] = desktop->doc_size[i - 1];
    }
    desktop->document[desktop->caret] = character;
    desktop->doc_font[desktop->caret] = (uint8_t)desktop->typing_font;
    desktop->doc_style[desktop->caret] = desktop->typing_style;
    desktop->doc_size[desktop->caret] = desktop->typing_size;
    ++desktop->document_length;
    ++desktop->caret;
    desktop->document[desktop->document_length] = '\0';
    clear_selection(desktop);
    desktop->document_dirty = 1;
}

static void apply_format_to_selection(struct desktop_state *desktop,
                                      int set_font, enum dc_font font,
                                      int set_style, uint8_t style_bits,
                                      int toggle_style, int set_size,
                                      uint8_t size) {
    size_t start = 0;
    size_t end = 0;
    if (selection_active(desktop)) {
        selection_bounds(desktop, &start, &end);
    } else {
        desktop->typing_font = set_font ? font : desktop->typing_font;
        if (set_style) desktop->typing_style = style_bits;
        if (toggle_style) desktop->typing_style ^= style_bits;
        if (set_size) desktop->typing_size = size;
        return;
    }
    for (size_t i = start; i < end; ++i) {
        if (set_font) desktop->doc_font[i] = (uint8_t)font;
        if (set_style) desktop->doc_style[i] = style_bits;
        if (toggle_style) desktop->doc_style[i] ^= style_bits;
        if (set_size) desktop->doc_size[i] = size;
    }
    if (set_font) desktop->typing_font = font;
    if (set_style) desktop->typing_style = style_bits;
    if (toggle_style) desktop->typing_style ^= style_bits;
    if (set_size) desktop->typing_size = size;
    desktop->document_dirty = 1;
}

static void clipboard_copy_text(struct desktop_state *desktop) {
    size_t start = 0;
    size_t end = 0;
    if (!selection_active(desktop)) return;
    selection_bounds(desktop, &start, &end);
    size_t length = end - start;
    desktop->clip_truncated = 0;
    if (length > CLIP_TEXT_CAPACITY) {
        length = CLIP_TEXT_CAPACITY;
        desktop->clip_truncated = 1;
    }
    copy_bytes(desktop->clipboard.text, desktop->document + start, length);
    desktop->clipboard.text[length] = '\0';
    desktop->clipboard.text_length = length;
    desktop->clipboard.kind = CLIP_TEXT;
}

static void clipboard_cut_text(struct desktop_state *desktop) {
    size_t start = 0;
    size_t end = 0;
    if (!selection_active(desktop)) return;
    selection_bounds(desktop, &start, &end);
    clipboard_copy_text(desktop);
    delete_range(desktop, start, end);
}

static void clipboard_paste_text(struct desktop_state *desktop) {
    if (desktop->clipboard.kind != CLIP_TEXT) return;
    if (selection_active(desktop)) {
        size_t start = 0;
        size_t end = 0;
        selection_bounds(desktop, &start, &end);
        delete_range(desktop, start, end);
    }
    for (size_t i = 0; i < desktop->clipboard.text_length; ++i) {
        insert_char(desktop, desktop->clipboard.text[i]);
    }
}

static void clipboard_copy_paint(struct desktop_state *desktop) {
    int x0 = 0;
    int y0 = 0;
    int x1 = PAINT_WIDTH - 1;
    int y1 = PAINT_HEIGHT - 1;
    if (desktop->has_marquee) {
        x0 = mini(desktop->marquee_x0, desktop->marquee_x1);
        y0 = mini(desktop->marquee_y0, desktop->marquee_y1);
        x1 = maxi(desktop->marquee_x0, desktop->marquee_x1);
        y1 = maxi(desktop->marquee_y0, desktop->marquee_y1);
    }
    int width = x1 - x0 + 1;
    int height = y1 - y0 + 1;
    if (width < 1 || height < 1) return;
    if (width > PAINT_WIDTH) width = PAINT_WIDTH;
    if (height > PAINT_HEIGHT) height = PAINT_HEIGHT;
    zero_bytes(desktop->clipboard.image, sizeof(desktop->clipboard.image));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            desktop->clipboard.image[y * PAINT_WIDTH + x] =
                canvas_get(desktop, x0 + x, y0 + y);
    desktop->clipboard.image_width = width;
    desktop->clipboard.image_height = height;
    desktop->clipboard.kind = CLIP_IMAGE;
    desktop->clip_truncated = 0;
}

static void clipboard_paste_into_script(struct desktop_state *desktop) {
    if (desktop->clipboard.kind == CLIP_TEXT) {
        clipboard_paste_text(desktop);
        return;
    }
    if (desktop->clipboard.kind != CLIP_IMAGE) return;
    zero_bytes(desktop->embed_image, sizeof(desktop->embed_image));
    for (int y = 0; y < desktop->clipboard.image_height; ++y)
        for (int x = 0; x < desktop->clipboard.image_width; ++x)
            desktop->embed_image[y * PAINT_WIDTH + x] =
                desktop->clipboard.image[y * PAINT_WIDTH + x];
    desktop->embed_width = desktop->clipboard.image_width;
    desktop->embed_height = desktop->clipboard.image_height;
    desktop->embed_valid = 1;
    desktop->document_dirty = 1;
}

static void clipboard_paste_into_paint(struct desktop_state *desktop) {
    if (desktop->clipboard.kind != CLIP_IMAGE) return;
    int ox = 12;
    int oy = 12;
    for (int y = 0; y < desktop->clipboard.image_height; ++y)
        for (int x = 0; x < desktop->clipboard.image_width; ++x)
            if (desktop->clipboard.image[y * PAINT_WIDTH + x])
                canvas_set(desktop, ox + x, oy + y, 1);
}

static void handle_key(struct desktop_state *desktop, uint16_t code,
                       int32_t value) {
    if (code == 42 || code == 54) {
        desktop->shift_down = value != 0;
        return;
    }
    if (value == 0 || desktop->about_open || desktop->confirm_new_open) return;
    if (desktop->front_app != DC_APP_SCRIPT || !desktop->script.visible) return;

    desktop->save_notice = 0;
    if (code == 14) {
        if (selection_active(desktop)) {
            size_t start = 0;
            size_t end = 0;
            selection_bounds(desktop, &start, &end);
            delete_range(desktop, start, end);
        } else if (desktop->caret > 0) {
            delete_range(desktop, desktop->caret - 1, desktop->caret);
        }
        return;
    }
    if (code == 28) {
        insert_char(desktop, '\n');
        return;
    }
    if (code == 105) { /* left */
        if (desktop->caret == 0) return;
        if (desktop->shift_down) {
            if (!desktop->has_selection) desktop->sel_anchor = desktop->caret;
            --desktop->caret;
            desktop->has_selection = desktop->caret != desktop->sel_anchor;
        } else {
            --desktop->caret;
            clear_selection(desktop);
        }
        return;
    }
    if (code == 106) { /* right */
        if (desktop->caret >= desktop->document_length) return;
        if (desktop->shift_down) {
            if (!desktop->has_selection) desktop->sel_anchor = desktop->caret;
            ++desktop->caret;
            desktop->has_selection = desktop->caret != desktop->sel_anchor;
        } else {
            ++desktop->caret;
            clear_selection(desktop);
        }
        return;
    }
    if (code == 103 || code == 108) { /* up / down */
        size_t line_start = desktop->caret;
        while (line_start > 0 && desktop->document[line_start - 1] != '\n') {
            --line_start;
        }
        size_t column = desktop->caret - line_start;
        if (code == 103) {
            if (line_start == 0) return;
            size_t prev_end = line_start - 1;
            size_t prev_start = prev_end;
            while (prev_start > 0 && desktop->document[prev_start - 1] != '\n') {
                --prev_start;
            }
            size_t prev_len = prev_end - prev_start + 1;
            size_t target = prev_start + mini((int)column, (int)prev_len);
            if (desktop->shift_down) {
                if (!desktop->has_selection) desktop->sel_anchor = desktop->caret;
                desktop->caret = target;
                desktop->has_selection = desktop->caret != desktop->sel_anchor;
            } else {
                desktop->caret = target;
                clear_selection(desktop);
            }
        } else {
            size_t next = desktop->caret;
            while (next < desktop->document_length &&
                   desktop->document[next] != '\n') {
                ++next;
            }
            if (next >= desktop->document_length) return;
            size_t next_start = next + 1;
            size_t next_end = next_start;
            while (next_end < desktop->document_length &&
                   desktop->document[next_end] != '\n') {
                ++next_end;
            }
            size_t next_len = next_end - next_start;
            size_t target = next_start + mini((int)column, (int)next_len);
            if (desktop->shift_down) {
                if (!desktop->has_selection) desktop->sel_anchor = desktop->caret;
                desktop->caret = target;
                desktop->has_selection = desktop->caret != desktop->sel_anchor;
            } else {
                desktop->caret = target;
                clear_selection(desktop);
            }
        }
        return;
    }
    char character = key_character(code, desktop->shift_down);
    if (character) insert_char(desktop, character);
}

static int menu_bar_index(int x, int y) {
    if (y >= 24) return MENU_NONE;
    if (x >= 148 && x < 190) return MENU_FILE;
    if (x >= 196 && x < 240) return MENU_EDIT;
    if (x >= 246 && x < 292) return MENU_FONT;
    if (x >= 298 && x < 350) return MENU_STYLE;
    if (x >= 356 && x < 402) return MENU_SIZE;
    if (x >= 408 && x < 456) return MENU_VIEW;
    return MENU_NONE;
}

static struct rect menu_frame(enum menu_id menu) {
    switch (menu) {
    case MENU_FILE:
        return (struct rect){148, 24, 120, 84};
    case MENU_EDIT:
        return (struct rect){196, 24, 120, 84};
    case MENU_FONT:
        return (struct rect){246, 24, 120, 84};
    case MENU_STYLE:
        return (struct rect){298, 24, 130, 84};
    case MENU_SIZE:
        return (struct rect){356, 24, 100, 56};
    case MENU_VIEW:
        return (struct rect){408, 24, 150, 34};
    default:
        return (struct rect){0, 0, 0, 0};
    }
}

static void handle_menu_command(struct desktop_state *desktop, enum menu_id menu,
                                int item) {
    int max_item = 2;
    if (menu == MENU_SIZE) max_item = 1;
    if (menu == MENU_VIEW) max_item = 0;
    if (item < 0 || item > max_item) return;

    if (menu == MENU_FILE) {
        if (item == 0) { /* Save */
            if (desktop->front_app == DC_APP_SCRIPT) save_document(desktop);
        } else if (item == 1) { /* Close */
            struct window_state *window = front_window(desktop);
            close_window(desktop, window);
        } else if (item == 2) { /* New */
            if (desktop->front_app == DC_APP_PAINT) {
                canvas_clear(desktop);
                desktop->has_marquee = 0;
            } else if (desktop->front_app == DC_APP_SCRIPT) {
                desktop->confirm_new_open = 1;
            }
        }
    } else if (menu == MENU_EDIT) {
        if (desktop->front_app == DC_APP_SCRIPT) {
            if (item == 0) clipboard_cut_text(desktop);
            else if (item == 1) clipboard_copy_text(desktop);
            else if (item == 2) clipboard_paste_into_script(desktop);
        } else if (desktop->front_app == DC_APP_PAINT) {
            if (item == 1) clipboard_copy_paint(desktop);
            else if (item == 2) clipboard_paste_into_paint(desktop);
        }
    } else if (menu == MENU_FONT) {
        if (item >= DC_FONT_COUNT) return;
        apply_format_to_selection(desktop, 1, (enum dc_font)item, 0, 0, 0, 0, 0);
    } else if (menu == MENU_STYLE) {
        if (item == 0)
            apply_format_to_selection(desktop, 0, 0, 1, 0, 0, 0, 0);
        else if (item == 1)
            apply_format_to_selection(desktop, 0, 0, 0, DC_STYLE_BOLD, 1, 0, 0);
        else if (item == 2)
            apply_format_to_selection(desktop, 0, 0, 0, DC_STYLE_UNDERLINE, 1, 0,
                                     0);
    } else if (menu == MENU_SIZE) {
        apply_format_to_selection(desktop, 0, 0, 0, 0, 0, 1, item == 0 ? 1 : 2);
    } else if (menu == MENU_VIEW) {
        desktop->about_open = 1;
    }
}

static int hit_menu_item(enum menu_id menu, int x, int y) {
    struct rect frame = menu_frame(menu);
    if (!contains(frame, x, y)) return -1;
    return (y - frame.y) / 28;
}

static struct rect script_text_rect(const struct window_state *window) {
    return (struct rect){window->frame.x + 18, window->frame.y + 40,
                         window->frame.width - 36, window->frame.height - 100};
}

static int glyph_metrics(const struct desktop_state *desktop, size_t index,
                         int *advance, int *height) {
    int scale = desktop->doc_size[index] ? desktop->doc_size[index] : 1;
    *advance = gfx_font_advance((enum dc_font)desktop->doc_font[index], scale);
    *height = gfx_font_height(scale);
    return scale;
}

static int word_pixel_width(const struct desktop_state *desktop, size_t start,
                            size_t end) {
    int width = 0;
    for (size_t i = start; i < end; ++i) {
        int advance = 0;
        int height = 0;
        glyph_metrics(desktop, i, &advance, &height);
        (void)height;
        width += advance;
    }
    return width;
}

static size_t word_end_index(const struct desktop_state *desktop, size_t start) {
    size_t end = start;
    while (end < desktop->document_length && desktop->document[end] != ' ' &&
           desktop->document[end] != '\n') {
        ++end;
    }
    return end;
}

static size_t hit_test_document(const struct desktop_state *desktop, int x,
                                int y) {
    struct rect area = script_text_rect(&desktop->script);
    int cursor_x = area.x;
    int cursor_y = area.y;
    int right = area.x + area.width;
    int bottom = area.y + area.height;
    size_t i = 0;
    while (i < desktop->document_length) {
        char value = desktop->document[i];
        int advance = 0;
        int height = 0;
        glyph_metrics(desktop, i, &advance, &height);
        if (value == '\n') {
            if (y < cursor_y + height + 2) return i;
            cursor_x = area.x;
            cursor_y += height + 2;
            ++i;
            continue;
        }
        if (value != ' ') {
            size_t end = word_end_index(desktop, i);
            int word_w = word_pixel_width(desktop, i, end);
            if (cursor_x > area.x && cursor_x + word_w > right) {
                cursor_x = area.x;
                cursor_y += height + 2;
            }
        } else if (cursor_x + advance > right && cursor_x > area.x) {
            cursor_x = area.x;
            cursor_y += height + 2;
            ++i;
            continue;
        }
        if (cursor_y + height >= bottom) break;
        if (y >= cursor_y && y < cursor_y + height && x >= cursor_x &&
            x < cursor_x + advance) {
            return i + (x > cursor_x + advance / 2 ? 1 : 0);
        }
        cursor_x += advance;
        ++i;
    }
    return desktop->document_length;
}

static void close_window(struct desktop_state *desktop,
                         struct window_state *window) {
    window->visible = 0;
    if (desktop->script.visible) desktop->front_app = DC_APP_SCRIPT;
    else if (desktop->paint.visible) desktop->front_app = DC_APP_PAINT;
    else {
        desktop->harvester.visible = 1;
        desktop->front_app = DC_APP_HARVESTER;
    }
    ensure_shell_visible(desktop);
}

static int point_hits_window(const struct desktop_state *desktop, int x, int y) {
    if (desktop->harvester.visible && contains(desktop->harvester.frame, x, y))
        return 1;
    if (desktop->script.visible && contains(desktop->script.frame, x, y)) return 1;
    if (desktop->paint.visible && contains(desktop->paint.frame, x, y)) return 1;
    return 0;
}

/* Front first, then other visible apps in reverse stable draw order (P,S,H). */
static void apps_front_to_back(const struct desktop_state *desktop,
                               enum dc_app out[3], int *count) {
    *count = 0;
    struct window_state const *front =
        desktop->front_app == DC_APP_SCRIPT
            ? &desktop->script
            : (desktop->front_app == DC_APP_PAINT ? &desktop->paint
                                                  : &desktop->harvester);
    if (front->visible) {
        out[(*count)++] = desktop->front_app;
    }
    enum dc_app stable[3] = {DC_APP_PAINT, DC_APP_SCRIPT, DC_APP_HARVESTER};
    for (int i = 0; i < 3; ++i) {
        enum dc_app app = stable[i];
        if (app == desktop->front_app) continue;
        struct window_state const *window =
            app == DC_APP_SCRIPT
                ? &desktop->script
                : (app == DC_APP_PAINT ? &desktop->paint : &desktop->harvester);
        if (!window->visible) continue;
        out[(*count)++] = app;
    }
}

static void handle_press(struct desktop_state *desktop, int x, int y) {
    desktop->save_notice = 0;

    if (desktop->confirm_new_open) {
        if (contains((struct rect){250, 300, 84, 26}, x, y)) {
            desktop->document_length = 0;
            desktop->document[0] = '\0';
            desktop->caret = 0;
            desktop->embed_valid = 0;
            clear_selection(desktop);
            desktop->document_dirty = 1;
            desktop->confirm_new_open = 0;
        } else if (contains((struct rect){350, 300, 84, 26}, x, y) ||
                   !contains((struct rect){188, 170, 272, 170}, x, y)) {
            desktop->confirm_new_open = 0;
        }
        return;
    }

    if (desktop->about_open) {
        if (contains((struct rect){360, 305, 84, 26}, x, y)) {
            desktop->about_open = 0;
        }
        return;
    }

    if (desktop->menu_open != MENU_NONE) {
        int item = hit_menu_item(desktop->menu_open, x, y);
        if (item >= 0) {
            handle_menu_command(desktop, desktop->menu_open, item);
            desktop->menu_open = MENU_NONE;
            return;
        }
        enum menu_id again = menu_bar_index(x, y);
        if (again != MENU_NONE) {
            desktop->menu_open = again;
            return;
        }
        desktop->menu_open = MENU_NONE;
    }

    if (y < 24) {
        desktop->menu_open = menu_bar_index(x, y);
        return;
    }

    enum dc_app order[3];
    int order_count = 0;
    apps_front_to_back(desktop, order, &order_count);
    for (int index = 0; index < order_count; ++index) {
        enum dc_app app = order[index];
        struct window_state *window =
            app == DC_APP_SCRIPT
                ? &desktop->script
                : (app == DC_APP_PAINT ? &desktop->paint : &desktop->harvester);
        if (!window->visible || !contains(window->frame, x, y)) continue;

        desktop->front_app = app;
        if (contains((struct rect){window->frame.x + 10, window->frame.y + 9, 10,
                                   10},
                     x, y)) {
            close_window(desktop, window);
            return;
        }
        if (y < window->frame.y + 28) {
            desktop->dragging = 1;
            desktop->drag_offset_x = x - window->frame.x;
            desktop->drag_offset_y = y - window->frame.y;
            return;
        }

        if (app == DC_APP_HARVESTER) {
            if (contains((struct rect){window->frame.x + 22, window->frame.y + 56,
                                       150, 40},
                         x, y)) {
                desktop->about_open = 1; /* SYSTEM → Acknowledgment */
            } else if (contains((struct rect){window->frame.x + 22,
                                              window->frame.y + 110, 150, 40},
                                x, y)) {
                open_script(desktop);
            } else if (contains((struct rect){window->frame.x + 22,
                                              window->frame.y + 164, 150, 40},
                                x, y)) {
                open_paint(desktop);
            }
            return;
        }

        if (app == DC_APP_SCRIPT) {
            struct rect text = script_text_rect(window);
            if (contains(text, x, y)) {
                size_t index_hit = hit_test_document(desktop, x, y);
                desktop->caret = clampi((int)index_hit, 0,
                                        (int)desktop->document_length);
                desktop->sel_anchor = desktop->caret;
                desktop->selecting = 1;
                desktop->has_selection = 0;
            }
            return;
        }

        if (app == DC_APP_PAINT) {
            int tool_x = window->frame.x + 12;
            int tool_y = window->frame.y + 40;
            for (int tool = 0; tool < 6; ++tool) {
                if (contains((struct rect){tool_x, tool_y + tool * 28, 54, 24},
                             x, y)) {
                    desktop->tool = (enum paint_tool)tool;
                    return;
                }
            }
            for (int pat = 0; pat < 4; ++pat) {
                if (contains((struct rect){tool_x, tool_y + 180 + pat * 18, 54,
                                           16},
                             x, y)) {
                    desktop->paint_pattern = pat;
                    return;
                }
            }
            int cx = 0;
            int cy = 0;
            if (map_to_canvas(window, x, y, &cx, &cy)) {
                desktop->stroke_active = 1;
                desktop->stroke_x0 = cx;
                desktop->stroke_y0 = cy;
                desktop->stroke_x1 = cx;
                desktop->stroke_y1 = cy;
                if (desktop->tool == PAINT_PENCIL)
                    canvas_brush(desktop, cx, cy, 0);
                else if (desktop->tool == PAINT_ERASER)
                    canvas_brush(desktop, cx, cy, 1);
                else if (desktop->tool == PAINT_SELECT) {
                    desktop->has_marquee = 1;
                    desktop->marquee_x0 = cx;
                    desktop->marquee_y0 = cy;
                    desktop->marquee_x1 = cx;
                    desktop->marquee_y1 = cy;
                }
            }
            return;
        }
        return;
    }

    /* Desktop icons only when no window covers the point. */
    if (!point_hits_window(desktop, x, y)) {
        if (contains((struct rect){24, 48, 70, 70}, x, y)) {
            open_harvester(desktop);
            return;
        }
        if (contains((struct rect){24, 130, 70, 70}, x, y)) {
            open_script(desktop);
            return;
        }
        if (contains((struct rect){24, 212, 70, 70}, x, y)) {
            open_paint(desktop);
            return;
        }
        if (contains((struct rect){540, 390, 80, 70}, x, y)) {
            open_script(desktop);
            return;
        }
    }
}

static void handle_drag_paint(struct desktop_state *desktop, int x, int y) {
    int cx = 0;
    int cy = 0;
    if (!map_to_canvas(&desktop->paint, x, y, &cx, &cy)) return;
    desktop->stroke_x1 = cx;
    desktop->stroke_y1 = cy;
    if (desktop->tool == PAINT_PENCIL) canvas_brush(desktop, cx, cy, 0);
    else if (desktop->tool == PAINT_ERASER) canvas_brush(desktop, cx, cy, 1);
    else if (desktop->tool == PAINT_SELECT) {
        desktop->marquee_x1 = cx;
        desktop->marquee_y1 = cy;
    }
}

static void finish_paint_stroke(struct desktop_state *desktop) {
    if (!desktop->stroke_active) return;
    if (desktop->tool == PAINT_LINE)
        canvas_line(desktop, desktop->stroke_x0, desktop->stroke_y0,
                    desktop->stroke_x1, desktop->stroke_y1, 0);
    else if (desktop->tool == PAINT_RECT)
        canvas_rect(desktop, desktop->stroke_x0, desktop->stroke_y0,
                    desktop->stroke_x1, desktop->stroke_y1, 1);
    else if (desktop->tool == PAINT_OVAL)
        canvas_oval(desktop, desktop->stroke_x0, desktop->stroke_y0,
                    desktop->stroke_x1, desktop->stroke_y1, 1);
    desktop->stroke_active = 0;
}

void desktop_handle_event(struct desktop_state *desktop,
                          const struct dc_event *event) {
    if (event->kind == DC_EVENT_POINTER_MOVE) {
        desktop->pointer_x = event->x;
        desktop->pointer_y = event->y;
        if (desktop->dragging) {
            struct window_state *window = front_window(desktop);
            window->frame.x = event->x - desktop->drag_offset_x;
            window->frame.y = event->y - desktop->drag_offset_y;
            window->frame.x = clampi(window->frame.x, 8,
                                     SCREEN_WIDTH - 8 - window->frame.width);
            window->frame.y = clampi(window->frame.y, 25,
                                     SCREEN_HEIGHT - 8 - window->frame.height);
        } else if (desktop->selecting && desktop->front_app == DC_APP_SCRIPT) {
            desktop->caret = hit_test_document(desktop, event->x, event->y);
            desktop->has_selection = desktop->caret != desktop->sel_anchor;
        } else if (desktop->stroke_active && desktop->front_app == DC_APP_PAINT) {
            handle_drag_paint(desktop, event->x, event->y);
        }
    } else if (event->kind == DC_EVENT_POINTER_BUTTON) {
        desktop->pointer_x = event->x;
        desktop->pointer_y = event->y;
        desktop->pointer_down = event->value != 0;
        if (event->value == 1) {
            handle_press(desktop, event->x, event->y);
        } else {
            desktop->dragging = 0;
            desktop->selecting = 0;
            if (desktop->stroke_active && desktop->paint.visible) {
                int cx = 0;
                int cy = 0;
                if (map_to_canvas(&desktop->paint, event->x, event->y, &cx,
                                  &cy)) {
                    desktop->stroke_x1 = cx;
                    desktop->stroke_y1 = cy;
                    if (desktop->tool == PAINT_SELECT) {
                        desktop->marquee_x1 = cx;
                        desktop->marquee_y1 = cy;
                    }
                }
                /* Pencil/eraser already painted during drag — do not redraw. */
            }
            finish_paint_stroke(desktop);
        }
    } else if (event->kind == DC_EVENT_KEY) {
        handle_key(desktop, event->code, event->value);
    }
}

static void draw_window_frame(const struct window_state *window,
                              const char *title, int active) {
    struct rect frame = window->frame;
    if (active) {
        gfx_dither((struct rect){frame.x + 10, frame.y + 12, frame.width,
                                 frame.height},
                   DC_SHADOW, DC_VOID, 42);
    }
    gfx_fill(frame, active ? DC_SURFACE : DC_DIM);
    gfx_fill((struct rect){frame.x, frame.y, frame.width, 28},
             active ? DC_DIM : DC_SHADOW);
    if (active) {
        for (int stripe = 6; stripe < 22; stripe += 2) {
            gfx_hline(frame.x + 28, frame.y + stripe, frame.width - 40, DC_SHADOW);
        }
    }
    gfx_fill((struct rect){frame.x, frame.y + 27, frame.width, 1}, DC_INK);
    gfx_fill((struct rect){frame.x + 10, frame.y + 9, 9, 9}, DC_INK);
    gfx_fill((struct rect){frame.x + 12, frame.y + 11, 5, 5}, DC_SURFACE);
    /* Punch a clear plate behind the title so stripes do not erase glyphs. */
    {
        int title_w = 0;
        for (const char *p = title; *p; ++p) {
            title_w += gfx_font_advance(DC_FONT_CHICAGO, 1);
        }
        gfx_fill((struct rect){frame.x + 32, frame.y + 6, title_w + 8, 16},
                 active ? DC_DIM : DC_SHADOW);
    }
    gfx_text(frame.x + 35, frame.y + 10, title, active ? DC_INK : DC_SURFACE, 1);
}

static void draw_harvester(const struct desktop_state *desktop, int active) {
    const struct window_state *window = &desktop->harvester;
    draw_window_frame(window, "HARVESTER", active);
    int x = window->frame.x + 24;
    int y = window->frame.y + 52;
    gfx_icon(x, y, DC_INK, 0);
    gfx_text(x + 38, y + 9, "SYSTEM", DC_INK, 1);
    gfx_icon(x, y + 54, DC_BREAK, 1);
    gfx_text(x + 38, y + 63, "SCRIPT", DC_INK, 1);
    gfx_paint_icon(x, y + 108, DC_INK);
    gfx_text(x + 38, y + 117, "PAINT", DC_INK, 1);
    gfx_text(window->frame.x + 210, window->frame.y + 240,
             storage_available() ? "DISK PRESENT" : "MEMORY ONLY", DC_SHADOW, 1);
}

static void draw_document(const struct desktop_state *desktop) {
    struct rect area = script_text_rect(&desktop->script);
    int text_bottom = area.y + area.height;
    if (desktop->embed_valid) {
        text_bottom -= desktop->embed_height + 24;
        if (text_bottom < area.y + 24) text_bottom = area.y + 24;
    }
    int cursor_x = area.x;
    int cursor_y = area.y;
    int right = area.x + area.width;
    size_t sel_start = 0;
    size_t sel_end = 0;
    selection_bounds(desktop, &sel_start, &sel_end);
    char text[2] = {0, 0};
    int caret_x = area.x;
    int caret_y = area.y;
    int caret_set = 0;

    size_t i = 0;
    while (i < desktop->document_length && cursor_y + 8 < text_bottom) {
        char value = desktop->document[i];
        int advance = 0;
        int height = 0;
        int scale = glyph_metrics(desktop, i, &advance, &height);
        enum dc_font font = (enum dc_font)desktop->doc_font[i];

        if (value == '\n') {
            if (!caret_set && desktop->caret == i) {
                caret_x = cursor_x;
                caret_y = cursor_y;
                caret_set = 1;
            }
            cursor_x = area.x;
            cursor_y += height + 2;
            ++i;
            continue;
        }
        if (value != ' ') {
            size_t end = word_end_index(desktop, i);
            int word_w = word_pixel_width(desktop, i, end);
            if (cursor_x > area.x && cursor_x + word_w > right) {
                cursor_x = area.x;
                cursor_y += height + 2;
                if (cursor_y + 8 >= text_bottom) break;
            }
        } else if (cursor_x + advance > right && cursor_x > area.x) {
            cursor_x = area.x;
            cursor_y += height + 2;
            ++i;
            continue;
        }

        if (!caret_set && desktop->caret == i) {
            caret_x = cursor_x;
            caret_y = cursor_y;
            caret_set = 1;
        }

        int selected = desktop->has_selection && i >= sel_start && i < sel_end;
        if (selected) {
            gfx_fill((struct rect){cursor_x, cursor_y, advance, height}, DC_SELECT);
        }
        text[0] = value;
        gfx_text_font(cursor_x, cursor_y, text, selected ? DC_SURFACE : DC_INK,
                      scale, font, desktop->doc_style[i]);
        cursor_x += advance;
        ++i;
    }
    if (!caret_set) {
        caret_x = cursor_x;
        caret_y = cursor_y;
    }
    if (!desktop->has_selection && caret_y + 8 < text_bottom) {
        int scale = desktop->typing_size ? desktop->typing_size : 1;
        gfx_fill((struct rect){caret_x, caret_y, 1, gfx_font_height(scale)},
                 DC_INK);
    }

    if (desktop->embed_valid) {
        int image_y = area.y + area.height - desktop->embed_height - 4;
        if (image_y >= text_bottom - 4) {
            gfx_text(area.x, image_y - 12, "PASTED PICTURE", DC_SHADOW, 1);
            gfx_bitmap(area.x, image_y, desktop->embed_image, desktop->embed_width,
                       desktop->embed_height, DC_INK, DC_SURFACE, 1);
            gfx_rect_outline((struct rect){area.x, image_y, desktop->embed_width,
                                           desktop->embed_height},
                             DC_SHADOW);
        }
    }
}

static void draw_script(const struct desktop_state *desktop, int active) {
    const struct window_state *window = &desktop->script;
    draw_window_frame(window, desktop->document_dirty ? "SCRIPT *" : "SCRIPT",
                      active);
    gfx_fill(script_text_rect(window), DC_SURFACE);
    draw_document(desktop);
    gfx_text(window->frame.x + 18, window->frame.y + window->frame.height - 22,
             "FONT / STYLE / SIZE IN MENU", DC_SHADOW, 1);
}

static void draw_paint(const struct desktop_state *desktop, int active) {
    const struct window_state *window = &desktop->paint;
    draw_window_frame(window, "PAINT", active);
    int tool_x = window->frame.x + 12;
    int tool_y = window->frame.y + 40;
    const char *names[6] = {"PENCIL", "ERASER", "LINE", "RECT", "OVAL", "SELECT"};
    for (int tool = 0; tool < 6; ++tool) {
        int selected = desktop->tool == (enum paint_tool)tool;
        gfx_fill((struct rect){tool_x, tool_y + tool * 28, 54, 24},
                 selected ? DC_ACCENT : DC_DIM);
        gfx_text(tool_x + 4, tool_y + tool * 28 + 8, names[tool],
                 selected ? DC_SURFACE : DC_INK, 1);
    }
    for (int pat = 0; pat < 4; ++pat) {
        struct rect swatch = {tool_x, tool_y + 180 + pat * 18, 54, 16};
        gfx_fill(swatch, DC_SURFACE);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 54; ++x)
                if (pattern_on(pat, x, y))
                    gfx_fill((struct rect){swatch.x + x, swatch.y + y, 1, 1},
                             DC_INK);
        if (desktop->paint_pattern == pat) gfx_rect_outline(swatch, DC_BREAK);
    }

    struct rect canvas = paint_canvas_rect(window);
    gfx_fill(canvas, DC_SURFACE);
    gfx_bitmap(canvas.x, canvas.y, desktop->canvas, PAINT_WIDTH, PAINT_HEIGHT,
               DC_INK, DC_SURFACE, 1);
    gfx_rect_outline(canvas, DC_INK);

    if (desktop->stroke_active &&
        (desktop->tool == PAINT_LINE || desktop->tool == PAINT_RECT ||
         desktop->tool == PAINT_OVAL || desktop->tool == PAINT_SELECT)) {
        int x0 = canvas.x + mini(desktop->stroke_x0, desktop->stroke_x1);
        int y0 = canvas.y + mini(desktop->stroke_y0, desktop->stroke_y1);
        int x1 = canvas.x + maxi(desktop->stroke_x0, desktop->stroke_x1);
        int y1 = canvas.y + maxi(desktop->stroke_y0, desktop->stroke_y1);
        gfx_rect_outline((struct rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1},
                         DC_BREAK);
    } else if (desktop->has_marquee) {
        int x0 = canvas.x + mini(desktop->marquee_x0, desktop->marquee_x1);
        int y0 = canvas.y + mini(desktop->marquee_y0, desktop->marquee_y1);
        int x1 = canvas.x + maxi(desktop->marquee_x0, desktop->marquee_x1);
        int y1 = canvas.y + maxi(desktop->marquee_y0, desktop->marquee_y1);
        gfx_rect_outline((struct rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1},
                         DC_ACCENT);
    }
}

static void draw_menu(const struct desktop_state *desktop) {
    if (desktop->menu_open == MENU_NONE) return;
    struct rect frame = menu_frame(desktop->menu_open);
    gfx_fill(frame, DC_SURFACE);
    gfx_rect_outline(frame, DC_SHADOW);
    if (desktop->menu_open == MENU_FILE) {
        gfx_text(frame.x + 12, frame.y + 10, "SAVE", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 38, "CLOSE", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 66, "NEW", DC_INK, 1);
    } else if (desktop->menu_open == MENU_EDIT) {
        gfx_text(frame.x + 12, frame.y + 10, "CUT", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 38, "COPY", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 66, "PASTE", DC_INK, 1);
    } else if (desktop->menu_open == MENU_FONT) {
        gfx_text(frame.x + 12, frame.y + 10, "CHICAGO", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 38, "GENEVA", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 66, "LONDON", DC_INK, 1);
    } else if (desktop->menu_open == MENU_STYLE) {
        gfx_text(frame.x + 12, frame.y + 10, "PLAIN", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 38, "BOLD", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 66, "UNDERLINE", DC_INK, 1);
    } else if (desktop->menu_open == MENU_SIZE) {
        gfx_text(frame.x + 12, frame.y + 10, "12 POINT", DC_INK, 1);
        gfx_text(frame.x + 12, frame.y + 38, "18 POINT", DC_INK, 1);
    } else if (desktop->menu_open == MENU_VIEW) {
        gfx_text(frame.x + 12, frame.y + 10, "ACKNOWLEDGMENT", DC_INK, 1);
    }
}

static void draw_about(void) {
    struct rect dialog = {168, 140, 304, 200};
    gfx_dither((struct rect){dialog.x + 9, dialog.y + 10, dialog.width,
                             dialog.height},
               DC_SHADOW, DC_VOID, 40);
    gfx_fill(dialog, DC_SURFACE);
    gfx_text(190, 168, "DIGITAL CAVIAR", DC_INK, 2);
    gfx_text(190, 206, "VERSION 1.1  BCS DEMO", DC_SHADOW, 1);
    gfx_text(190, 230, "WRITE  PAINT  CLIPBOARD", DC_INK, 1);
    gfx_text(190, 252, "FONTS LIKE THE 1984 MAC", DC_INK, 1);
    gfx_fill((struct rect){360, 305, 84, 26}, DC_ACCENT);
    gfx_text(380, 315, "DISMISS", DC_SURFACE, 1);
}

static void draw_confirm_new(void) {
    struct rect dialog = {188, 170, 272, 170};
    gfx_dither((struct rect){dialog.x + 9, dialog.y + 10, dialog.width,
                             dialog.height},
               DC_SHADOW, DC_VOID, 40);
    gfx_fill(dialog, DC_SURFACE);
    gfx_text(210, 200, "ERASE DOCUMENT?", DC_INK, 1);
    gfx_text(210, 230, "THIS CANNOT BE UNDONE", DC_SHADOW, 1);
    gfx_fill((struct rect){250, 300, 84, 26}, DC_BREAK);
    gfx_text(268, 310, "ERASE", DC_SURFACE, 1);
    gfx_fill((struct rect){350, 300, 84, 26}, DC_ACCENT);
    gfx_text(362, 310, "CANCEL", DC_SURFACE, 1);
}

static void draw_storage_status(const struct desktop_state *desktop) {
    if (desktop->save_notice) {
        gfx_text(24, 452,
                 desktop->save_notice > 0 ? "SAVED" : "SAVE UNAVAILABLE",
                 desktop->save_notice > 0 ? DC_ACCENT : DC_BREAK, 1);
        return;
    }
    if (desktop->clipboard.kind == CLIP_IMAGE) {
        gfx_text(24, 452, "CLIPBOARD: PICTURE", DC_ACCENT, 1);
    } else if (desktop->clipboard.kind == CLIP_TEXT) {
        gfx_text(24, 452,
                 desktop->clip_truncated ? "CLIPBOARD: TEXT TRUNCATED"
                                         : "CLIPBOARD: TEXT",
                 desktop->clip_truncated ? DC_BREAK : DC_ACCENT, 1);
    } else if (desktop->storage_status == STORAGE_LOAD_CORRUPT) {
        gfx_text(24, 452, "STORAGE CORRUPT", DC_BREAK, 1);
    } else if (desktop->storage_status == STORAGE_LOAD_NO_MEDIA) {
        gfx_text(24, 452, "NO STORAGE", DC_DIM, 1);
    }
}

void desktop_render(const struct desktop_state *desktop) {
    graphics_begin();
    gfx_fill((struct rect){0, 0, SCREEN_WIDTH, 24}, DC_DEEP);
    gfx_fill((struct rect){0, 23, SCREEN_WIDTH, 1}, DC_SHADOW);

    const char *brand = "DIGITAL CAVIAR";
    if (desktop->front_app == DC_APP_SCRIPT) brand = "SCRIPT";
    else if (desktop->front_app == DC_APP_PAINT) brand = "PAINT";
    gfx_text(16, 8, brand, DC_SURFACE, 1);
    gfx_text(148, 8, "FILE", DC_DIM, 1);
    gfx_text(196, 8, "EDIT", DC_DIM, 1);
    gfx_text(246, 8, "FONT", DC_DIM, 1);
    gfx_text(298, 8, "STYLE", DC_DIM, 1);
    gfx_text(356, 8, "SIZE", DC_DIM, 1);
    gfx_text(408, 8, "VIEW", DC_DIM, 1);

    gfx_icon(34, 52, DC_INK, 0);
    gfx_text(28, 86, "SYSTEM", DC_DIM, 1);
    gfx_icon(34, 134, DC_BREAK, 1);
    gfx_text(28, 168, "SCRIPT", DC_DIM, 1);
    gfx_paint_icon(34, 216, DC_INK);
    gfx_text(32, 250, "PAINT", DC_DIM, 1);
    gfx_icon(552, 398, DC_BREAK, 1);
    gfx_text(540, 432, "NOTES", DC_DIM, 1);

    struct window_state const *windows[3];
    const char *titles[3];
    int count = 0;
    (void)windows;
    (void)titles;
    (void)count;

    enum dc_app order[3];
    int order_count = 0;
    apps_front_to_back(desktop, order, &order_count);
    /* Draw back-to-front = reverse of hit order. */
    for (int index = order_count - 1; index >= 0; --index) {
        enum dc_app app = order[index];
        int active = app == desktop->front_app;
        if (app == DC_APP_HARVESTER) draw_harvester(desktop, active);
        else if (app == DC_APP_SCRIPT) draw_script(desktop, active);
        else if (app == DC_APP_PAINT) draw_paint(desktop, active);
    }

    draw_storage_status(desktop);
    draw_menu(desktop);
    if (desktop->about_open) draw_about();
    if (desktop->confirm_new_open) draw_confirm_new();
    gfx_cursor(desktop->pointer_x, desktop->pointer_y);
    graphics_present();
}

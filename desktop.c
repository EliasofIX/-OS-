#include "os.h"

static int contains(struct rect area, int x, int y) {
    return x >= area.x && y >= area.y &&
           x < area.x + area.width && y < area.y + area.height;
}

static struct window_state *front_window(struct desktop_state *desktop) {
    return desktop->front_app == DC_APP_SCRIPT ?
           &desktop->script : &desktop->harvester;
}

static void open_script(struct desktop_state *desktop) {
    desktop->script.visible = 1;
    desktop->front_app = DC_APP_SCRIPT;
}

static void save_document(struct desktop_state *desktop) {
    desktop->save_notice =
        storage_save_document(desktop->document, desktop->document_length) ? 1 : -1;
    if (desktop->save_notice > 0) desktop->document_dirty = 0;
}

void desktop_init(struct desktop_state *desktop) {
    uint8_t *bytes = (uint8_t *)desktop;
    for (size_t i = 0; i < sizeof(*desktop); ++i) bytes[i] = 0;
    desktop->harvester = (struct window_state){{66, 62, 424, 300}, 1};
    desktop->script = (struct window_state){{190, 112, 374, 280}, 0};
    desktop->front_app = DC_APP_HARVESTER;
    desktop->pointer_x = SCREEN_WIDTH / 2;
    desktop->pointer_y = SCREEN_HEIGHT / 2;

    if (!storage_load_document(desktop->document, DOCUMENT_CAPACITY,
                               &desktop->document_length)) {
        const char *welcome =
            "Digital Caviar\n\n"
            "A quiet machine.\n"
            "Move through it deliberately.\n\n"
            "Open File and choose Save to keep this text.";
        while (welcome[desktop->document_length] &&
               desktop->document_length < DOCUMENT_CAPACITY) {
            desktop->document[desktop->document_length] =
                welcome[desktop->document_length];
            ++desktop->document_length;
        }
    }
}

static char key_character(uint16_t code, int shift) {
    static const char normal[] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',
        [10]='9',[11]='0',[12]='-',[13]='=',
        [16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',
        [23]='i',[24]='o',[25]='p',[26]='[',[27]=']',
        [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',
        [37]='k',[38]='l',[39]=';',[40]='\'',[41]='`',[43]='\\',
        [44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',
        [51]=',',[52]='.',[53]='/',[57]=' '
    };
    static const char shifted[] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',
        [10]='(',[11]=')',[12]='_',[13]='+',
        [16]='Q',[17]='W',[18]='E',[19]='R',[20]='T',[21]='Y',[22]='U',
        [23]='I',[24]='O',[25]='P',[26]='{',[27]='}',
        [30]='A',[31]='S',[32]='D',[33]='F',[34]='G',[35]='H',[36]='J',
        [37]='K',[38]='L',[39]=':',[40]='"',[41]='~',[43]='|',
        [44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',[49]='N',[50]='M',
        [51]='<',[52]='>',[53]='?',[57]=' '
    };
    if (code >= sizeof(normal)) return 0;
    return shift ? shifted[code] : normal[code];
}

static void handle_key(struct desktop_state *desktop,
                       uint16_t code, int32_t value) {
    if (code == 42 || code == 54) {
        desktop->shift_down = value != 0;
        return;
    }
    if (value == 0 || !desktop->script.visible ||
        desktop->front_app != DC_APP_SCRIPT || desktop->about_open) return;
    desktop->save_notice = 0;
    if (code == 14) {
        if (desktop->document_length) --desktop->document_length;
    } else if (code == 28) {
        if (desktop->document_length < DOCUMENT_CAPACITY)
            desktop->document[desktop->document_length++] = '\n';
    } else {
        char character = key_character(code, desktop->shift_down);
        if (character && desktop->document_length < DOCUMENT_CAPACITY)
            desktop->document[desktop->document_length++] = character;
        else return;
    }
    desktop->document_dirty = 1;
}

static void handle_press(struct desktop_state *desktop, int x, int y) {
    desktop->save_notice = 0;
    if (desktop->about_open) {
        if (contains((struct rect){360, 305, 84, 26}, x, y))
            desktop->about_open = 0;
        return;
    }

    if (y < 24) {
        if (x >= 146 && x < 196) desktop->menu_open = 1;
        else if (x >= 200 && x < 250) desktop->menu_open = 2;
        else desktop->menu_open = 0;
        return;
    }

    if (desktop->menu_open == 1) {
        if (contains((struct rect){142, 24, 120, 28}, x, y)) save_document(desktop);
        else if (contains((struct rect){142, 52, 120, 28}, x, y) &&
                 desktop->front_app == DC_APP_SCRIPT) {
            desktop->script.visible = 0;
            desktop->front_app = DC_APP_HARVESTER;
        }
        desktop->menu_open = 0;
        return;
    }
    if (desktop->menu_open == 2) {
        if (contains((struct rect){198, 24, 166, 34}, x, y))
            desktop->about_open = 1;
        desktop->menu_open = 0;
        return;
    }

    // Hit-test the front window first, then the rear window.
    enum dc_app order[2] = {
        desktop->front_app,
        desktop->front_app == DC_APP_SCRIPT ? DC_APP_HARVESTER : DC_APP_SCRIPT
    };
    for (int index = 0; index < 2; ++index) {
        enum dc_app app = order[index];
        struct window_state *window =
            app == DC_APP_SCRIPT ? &desktop->script : &desktop->harvester;
        if (!window->visible || !contains(window->frame, x, y)) continue;
        desktop->front_app = app;
        if (contains((struct rect){window->frame.x + 10, window->frame.y + 9,
                                   10, 10}, x, y)) {
            if (app == DC_APP_SCRIPT) {
                window->visible = 0;
                desktop->front_app = DC_APP_HARVESTER;
            }
            return;
        }
        if (y < window->frame.y + 28) {
            desktop->dragging = 1;
            desktop->drag_offset_x = x - window->frame.x;
            desktop->drag_offset_y = y - window->frame.y;
            return;
        }
        if (app == DC_APP_HARVESTER &&
            contains((struct rect){window->frame.x + 22, window->frame.y + 116,
                                   170, 42}, x, y)) open_script(desktop);
        return;
    }

    if (contains((struct rect){32, 128, 100, 46}, x, y)) open_script(desktop);
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
            if (window->frame.x < 8) window->frame.x = 8;
            if (window->frame.y < 25) window->frame.y = 25;
            if (window->frame.x + window->frame.width > SCREEN_WIDTH - 8)
                window->frame.x = SCREEN_WIDTH - 8 - window->frame.width;
            if (window->frame.y + window->frame.height > SCREEN_HEIGHT - 8)
                window->frame.y = SCREEN_HEIGHT - 8 - window->frame.height;
        }
    } else if (event->kind == DC_EVENT_POINTER_BUTTON) {
        desktop->pointer_x = event->x;
        desktop->pointer_y = event->y;
        desktop->pointer_down = event->value != 0;
        if (event->value == 1) handle_press(desktop, event->x, event->y);
        else if (event->value == 0) desktop->dragging = 0;
    } else if (event->kind == DC_EVENT_KEY) {
        handle_key(desktop, event->code, event->value);
    }
}

static void draw_window_frame(const struct window_state *window,
                              const char *title, int active) {
    struct rect frame = window->frame;
    if (active) {
        gfx_dither((struct rect){frame.x + 10, frame.y + 12,
                                 frame.width, frame.height},
                   DC_SHADOW, DC_VOID, 42);
    }
    gfx_fill(frame, active ? DC_SURFACE : DC_DIM);
    gfx_fill((struct rect){frame.x, frame.y, frame.width, 28},
             active ? DC_DIM : DC_SHADOW);
    gfx_fill((struct rect){frame.x, frame.y + 27, frame.width, 1}, DC_INK);
    gfx_fill((struct rect){frame.x + 10, frame.y + 9, 9, 9}, DC_INK);
    gfx_text(frame.x + 35, frame.y + 10, title,
             active ? DC_INK : DC_SURFACE, 1);
}

static void draw_harvester(const struct desktop_state *desktop, int active) {
    const struct window_state *window = &desktop->harvester;
    draw_window_frame(window, "HARVESTER", active);
    int x = window->frame.x + 24;
    int y = window->frame.y + 52;
    gfx_icon(x, y, DC_INK, 0);
    gfx_text(x + 38, y + 9, "SYSTEM", DC_INK, 1);
    gfx_icon(x, y + 58, DC_BREAK, 1);
    gfx_text(x + 38, y + 67, "NOTES", DC_INK, 1);
    gfx_text(window->frame.x + 250, window->frame.y + 250,
             storage_available() ? "DISK PRESENT" : "MEMORY ONLY", DC_SHADOW, 1);
}

static void draw_document(const struct desktop_state *desktop,
                          int x, int y, int width, int height) {
    int cursor_x = x;
    int cursor_y = y;
    int right = x + width;
    int bottom = y + height;
    char text[2] = {0, 0};
    for (size_t i = 0; i < desktop->document_length && cursor_y + 7 < bottom; ++i) {
        char value = desktop->document[i];
        if (value == '\n' || cursor_x + 6 > right) {
            cursor_x = x;
            cursor_y += 11;
            if (value == '\n') continue;
        }
        text[0] = value;
        gfx_text(cursor_x, cursor_y, text, DC_INK, 1);
        cursor_x += 6;
    }
    if (cursor_y + 8 < bottom) gfx_fill((struct rect){cursor_x, cursor_y, 1, 8}, DC_INK);
}

static void draw_script(const struct desktop_state *desktop, int active) {
    const struct window_state *window = &desktop->script;
    draw_window_frame(window, desktop->document_dirty ? "SCRIPT -" : "SCRIPT", active);
    int margin = 24;
    draw_document(desktop, window->frame.x + margin, window->frame.y + 52,
                  window->frame.width - margin * 2, window->frame.height - 72);
}

static void draw_menu(const struct desktop_state *desktop) {
    if (desktop->menu_open == 1) {
        gfx_fill((struct rect){142, 24, 120, 56}, DC_SURFACE);
        gfx_fill((struct rect){142, 24, 1, 56}, DC_SHADOW);
        gfx_text(158, 35, "SAVE", DC_INK, 1);
        gfx_text(158, 63, "CLOSE", DC_INK, 1);
    } else if (desktop->menu_open == 2) {
        gfx_fill((struct rect){198, 24, 166, 34}, DC_SURFACE);
        gfx_text(212, 37, "ACKNOWLEDGMENT", DC_INK, 1);
    }
}

static void draw_about(void) {
    struct rect dialog = {188, 152, 272, 182};
    gfx_dither((struct rect){dialog.x + 9, dialog.y + 10,
                             dialog.width, dialog.height},
               DC_SHADOW, DC_VOID, 40);
    gfx_fill(dialog, DC_SURFACE);
    gfx_text(220, 184, "DIGITAL CAVIAR", DC_INK, 2);
    gfx_text(220, 226, "VERSION 1.0", DC_SHADOW, 1);
    gfx_text(220, 250, "A QUIET MACHINE FOR ARM64", DC_INK, 1);
    gfx_fill((struct rect){360, 305, 84, 26}, DC_ACCENT);
    gfx_text(380, 315, "DISMISS", DC_SURFACE, 1);
}

void desktop_render(const struct desktop_state *desktop) {
    graphics_begin();
    gfx_fill((struct rect){0, 0, SCREEN_WIDTH, 24}, DC_DEEP);
    gfx_fill((struct rect){0, 23, SCREEN_WIDTH, 1}, DC_SHADOW);
    gfx_text(20, 8,
             desktop->front_app == DC_APP_SCRIPT ? "SCRIPT" : "DIGITAL CAVIAR",
             DC_SURFACE, 1);
    gfx_text(148, 8, "FILE", DC_DIM, 1);
    gfx_text(202, 8, "VIEW", DC_DIM, 1);

    gfx_icon(34, 62, DC_INK, 0);
    gfx_text(32, 96, "SYSTEM", DC_DIM, 1);
    gfx_icon(34, 128, DC_INK, 1);
    gfx_text(32, 162, "SCRIPT", DC_DIM, 1);
    gfx_icon(548, 394, DC_BREAK, 1);
    gfx_text(530, 428, "NOTES", DC_DIM, 1);

    if (desktop->harvester.visible && desktop->script.visible) {
        if (desktop->front_app == DC_APP_SCRIPT) {
            draw_harvester(desktop, 0);
            draw_script(desktop, 1);
        } else {
            draw_script(desktop, 0);
            draw_harvester(desktop, 1);
        }
    } else if (desktop->harvester.visible) draw_harvester(desktop, 1);
    else if (desktop->script.visible) draw_script(desktop, 1);

    if (desktop->save_notice)
        gfx_text(24, 452, desktop->save_notice > 0 ? "SAVED" : "SAVE UNAVAILABLE",
                 desktop->save_notice > 0 ? DC_ACCENT : DC_BREAK, 1);
    draw_menu(desktop);
    if (desktop->about_open) draw_about();
    gfx_cursor(desktop->pointer_x, desktop->pointer_y);
    graphics_present();
}

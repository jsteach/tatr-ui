#include "ui.h"

#include "renderer.h"
#include "vendor/fenster.h"
#include "vendor/lite/rencache.h"
#include "vendor/tinyfiledialogs/tinyfiledialogs.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_WINDOW_WIDTH 1200
#define INITIAL_WINDOW_HEIGHT 780
#define ROW_H 68

typedef enum {
    FILTER_ALL,
    FILTER_OPEN,
    FILTER_CLOSED
} TaskFilter;

typedef struct {
    size_t selected;
    size_t scroll;
    TaskFilter filter;
    bool sort_by_id;
    bool ascending;
    int mouse_was_down;
    int keys_were_down[256];
} UiState;

typedef struct {
    int width;
    int height;
    int margin;
    int top;
    int sidebar_x;
    int sidebar_y;
    int sidebar_w;
    int details_x;
    int details_y;
    int details_w;
    int sidebar_h;
    int panel_h;
    int details_h;
    int row_y;
    int row_count;
    int filter_x[3];
    int filter_w;
    int sort_x;
    int sort_w;
    bool stacked;
} UiLayout;

typedef struct {
    int active;
    int is_new;
    int focused;
    char task_id[64];
    char title[512];
    size_t title_cursor;
    char status[8];
    char priority[32];
    size_t priority_cursor;
    char tags[512];
    int tags_open;
    size_t tag_scroll;
    size_t tag_highlight;
    int creating_tag;
    char new_tag[64];
    size_t new_tag_cursor;
    char *body;
    size_t body_len;
    size_t body_cursor;
    size_t body_capacity;
    char message[160];
} EditForm;

static RenFont *ui_font;

static int clamp_int(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static UiLayout make_layout(int width, int height) {
    UiLayout layout = {0};
    layout.width = width;
    layout.height = height;
    layout.margin = clamp_int(width / 30, 8, 24);
    layout.top = clamp_int(height / 7, 72, 112);
    layout.sidebar_x = layout.margin;
    layout.sidebar_y = layout.top;
    layout.stacked = width < 720;
    if (layout.stacked) {
        layout.sidebar_w = width - 2 * layout.margin;
        if (layout.sidebar_w < 1) layout.sidebar_w = 1;
        layout.panel_h = height - layout.top - layout.margin;
        if (layout.panel_h < 1) layout.panel_h = 1;
        layout.details_x = layout.sidebar_x;
        layout.details_w = layout.sidebar_w;
        layout.sidebar_h = layout.panel_h * 60 / 100;
        if (layout.sidebar_h < 1) layout.sidebar_h = 1;
        layout.details_y = layout.sidebar_y + layout.sidebar_h + layout.margin / 2;
        layout.details_h = height - layout.margin - layout.details_y;
        if (layout.details_h < 1) layout.details_h = 1;
    } else {
        layout.sidebar_w = clamp_int((width - 3 * layout.margin) * 38 / 100, 240, 420);
        layout.details_x = layout.sidebar_x + layout.sidebar_w + layout.margin;
        layout.details_w = width - layout.margin - layout.details_x;
        if (layout.details_w < 1) layout.details_w = 1;
        layout.panel_h = height - layout.top - layout.margin;
        if (layout.panel_h < 1) layout.panel_h = 1;
        layout.sidebar_h = layout.panel_h;
        layout.details_y = layout.sidebar_y;
        layout.details_h = layout.panel_h;
    }
    layout.row_y = layout.sidebar_y + 104;
    layout.row_count = (layout.sidebar_h - 104) / ROW_H;
    if (layout.row_count < 0) layout.row_count = 0;

    int toolbar_width = layout.sidebar_w - 36;
    if (toolbar_width < 1) toolbar_width = 1;
    layout.filter_w = (toolbar_width - 24) / 4;
    if (layout.filter_w > 66) layout.filter_w = 66;
    if (layout.filter_w < 1) layout.filter_w = 1;
    for (int i = 0; i < 3; ++i) {
        layout.filter_x[i] =
            layout.sidebar_x + 18 + i * (layout.filter_w + 8);
    }
    layout.sort_x = layout.sidebar_x + 18 + 3 * (layout.filter_w + 8);
    layout.sort_w = toolbar_width - 3 * layout.filter_w - 24;
    if (layout.sort_w < 1) layout.sort_w = 1;
    return layout;
}

static int project_button_width(const UiLayout *layout) {
    return clamp_int(layout->width / 5, 90, 132);
}

static RenRect welcome_button_rect(const UiLayout *layout) {
    int width = clamp_int(layout->width / 3, 1, 210);
    if (width > layout->width - 2 * layout->margin)
        width = layout->width - 2 * layout->margin;
    if (width < 1) width = 1;
    int height = 40;
    return (RenRect){
        (layout->width - width) / 2,
        layout->height / 2 + 42,
        width,
        height
    };
}

static RenColor rgb(unsigned char r, unsigned char g, unsigned char b) {
    RenColor color = {b, g, r, 255};
    return color;
}

static RenColor priority_color(const TatrList *list, const TatrTask *task) {
    if (!task->has_priority) return rgb(119, 133, 151);

    size_t rank = 0;
    size_t count = 0;
    for (size_t i = 0; i < list->count; ++i) {
        const TatrTask *other = &list->items[i];
        if (!other->has_priority) continue;
        ++count;
        if (other->priority > task->priority) ++rank;
    }

    if (rank * 3 < count) return rgb(239, 112, 95);
    if (rank * 3 < count * 2) return rgb(239, 183, 82);
    return rgb(91, 190, 151);
}

static void rect(int x, int y, int width, int height, RenColor color) {
    rencache_draw_rect((RenRect){x, y, width, height}, color);
}

static void text(const char *value, int x, int y, RenColor color) {
    rencache_draw_text(ui_font, value, x, y, color);
}

static bool task_matches(const TatrTask *task, TaskFilter filter) {
    if (filter == FILTER_OPEN) return !tatr_status_is_closed(task);
    if (filter == FILTER_CLOSED) return tatr_status_is_closed(task);
    return true;
}

static size_t visible_count(const TatrList *list, TaskFilter filter) {
    size_t count = 0;
    for (size_t i = 0; i < list->count; ++i) {
        if (task_matches(&list->items[i], filter)) ++count;
    }
    return count;
}

static const TatrTask *visible_task(const TatrList *list, TaskFilter filter,
                                    size_t visible_index) {
    for (size_t i = 0; i < list->count; ++i) {
        if (task_matches(&list->items[i], filter)) {
            if (visible_index == 0) return &list->items[i];
            --visible_index;
        }
    }
    return NULL;
}

static int key_pressed(struct fenster *window, UiState *state, int key) {
    int down = key >= 0 && key < 256 && window->keys[key];
    int pressed = down && !state->keys_were_down[key];
    if (key >= 0 && key < 256) state->keys_were_down[key] = down;
    return pressed;
}

static int copy_form_field(char *dst, size_t dst_size, const char *src) {
    size_t len = src ? strlen(src) : 0;
    if (len >= dst_size) return 0;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
    return 1;
}

static void edit_form_close(EditForm *form) {
    free(form->body);
    memset(form, 0, sizeof(*form));
}

static int edit_form_open(EditForm *form, const TatrTask *task) {
    edit_form_close(form);
    form->active = 1;
    form->is_new = task == NULL;
    form->focused = 0;
    if (task) {
        if (!copy_form_field(form->task_id, sizeof(form->task_id), task->id) ||
            !copy_form_field(form->title, sizeof(form->title), task->title) ||
            !copy_form_field(form->status, sizeof(form->status),
                             task->status ? task->status : "OPEN")) {
            form->message[0] = 0;
            edit_form_close(form);
            return 0;
        }
        size_t tags_len = 0;
        for (int i = 0; i < task->ntags; ++i) {
            size_t tag_len = strlen(task->tags[i]);
            if (tag_len >= sizeof(form->tags) - tags_len) {
                edit_form_close(form);
                return 0;
            }
            tags_len += tag_len;
            if (i + 1 < task->ntags) {
                if (tags_len + 1 >= sizeof(form->tags)) {
                    edit_form_close(form);
                    return 0;
                }
                ++tags_len;
            }
        }
        if (task->has_priority)
            snprintf(form->priority, sizeof(form->priority), "%d", task->priority);
        tatr_format_tags(task, form->tags, sizeof(form->tags));
        form->title_cursor = strlen(form->title);
        form->priority_cursor = strlen(form->priority);
    } else {
        snprintf(form->status, sizeof(form->status), "OPEN");
        snprintf(form->priority, sizeof(form->priority), "%d",
                 TATR_DEFAULT_PRIORITY);
        form->priority_cursor = strlen(form->priority);
    }

    const char *body = task && task->body ? task->body : "";
    form->body_len = strlen(body);
    form->body_capacity = form->body_len + 256;
    if (form->body_capacity < 1024) form->body_capacity = 1024;
    form->body = (char *)malloc(form->body_capacity);
    if (!form->body) {
        edit_form_close(form);
        return 0;
    }
    memcpy(form->body, body, form->body_len + 1);
    form->body_cursor = form->body_len;
    return 1;
}

static size_t utf8_previous(const char *text, size_t cursor) {
    if (!cursor) return 0;
    --cursor;
    while (cursor && ((unsigned char)text[cursor] & 0xc0) == 0x80) --cursor;
    return cursor;
}

static size_t utf8_next(const char *text, size_t length, size_t cursor) {
    if (cursor >= length) return length;
    ++cursor;
    while (cursor < length && ((unsigned char)text[cursor] & 0xc0) == 0x80)
        ++cursor;
    return cursor;
}

static int word_class(unsigned char value) {
    if (isspace(value)) return 0;
    if (value >= 0x80 || isalnum(value) || value == '_') return 1;
    return 2;
}

static size_t word_left(const char *text, size_t cursor) {
    while (cursor) {
        size_t previous = utf8_previous(text, cursor);
        if (!isspace((unsigned char)text[previous])) break;
        cursor = previous;
    }
    if (!cursor) return cursor;
    int category = word_class((unsigned char)text[utf8_previous(text, cursor)]);
    while (cursor) {
        size_t previous = utf8_previous(text, cursor);
        if (word_class((unsigned char)text[previous]) != category) break;
        cursor = previous;
    }
    return cursor;
}

static size_t word_right(const char *text, size_t length, size_t cursor) {
    while (cursor < length && isspace((unsigned char)text[cursor]))
        cursor = utf8_next(text, length, cursor);
    if (cursor >= length) return cursor;
    int category = word_class((unsigned char)text[cursor]);
    while (cursor < length &&
           word_class((unsigned char)text[cursor]) == category)
        cursor = utf8_next(text, length, cursor);
    while (cursor < length && isspace((unsigned char)text[cursor]))
        cursor = utf8_next(text, length, cursor);
    return cursor;
}

static size_t delete_word_left(char *text, size_t *length, size_t cursor) {
    size_t start = word_left(text, cursor);
    memmove(text + start, text + cursor, *length - cursor + 1);
    *length -= cursor - start;
    return start;
}

static size_t delete_previous_character(char *text, size_t *length,
                                        size_t cursor) {
    if (!cursor) return cursor;
    size_t start = utf8_previous(text, cursor);
    memmove(text + start, text + cursor, *length - cursor + 1);
    *length -= cursor - start;
    return start;
}

static int insert_text(char *value, size_t capacity, size_t *length,
                       size_t *cursor, const char *input, size_t input_len) {
    if (*cursor > *length || input_len >= capacity - *length) return 0;
    memmove(value + *cursor + input_len, value + *cursor,
            *length - *cursor + 1);
    memcpy(value + *cursor, input, input_len);
    *length += input_len;
    *cursor += input_len;
    return 1;
}

static int form_append_body(EditForm *form, const char *input, size_t input_len) {
    if (input_len > SIZE_MAX - form->body_len - 1) return 0;
    size_t required = form->body_len + input_len + 1;
    if (required > form->body_capacity) {
        size_t capacity = form->body_capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        char *body = (char *)realloc(form->body, capacity);
        if (!body) return 0;
        form->body = body;
        form->body_capacity = capacity;
    }
    if (form->body_cursor > form->body_len) return 0;
    memmove(form->body + form->body_cursor + input_len,
            form->body + form->body_cursor,
            form->body_len - form->body_cursor + 1);
    memcpy(form->body + form->body_cursor, input, input_len);
    form->body_len += input_len;
    form->body_cursor += input_len;
    return 1;
}

static int tag_is_selected(const EditForm *form, const char *name) {
    const char *p = form->tags;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) ++p;
        const char *start = p;
        while (*p && *p != ',' && !isspace((unsigned char)*p)) ++p;
        if ((size_t)(p - start) == strlen(name) &&
            memcmp(start, name, (size_t)(p - start)) == 0)
            return 1;
    }
    return 0;
}

static int toggle_form_tag(EditForm *form, const char *name) {
    char updated[sizeof(form->tags)];
    size_t used = 0;
    size_t count = 0;
    int was_selected = 0;
    const char *p = form->tags;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) ++p;
        const char *start = p;
        while (*p && *p != ',' && !isspace((unsigned char)*p)) ++p;
        size_t length = (size_t)(p - start);
        if (!length) continue;
        if (length == strlen(name) && memcmp(start, name, length) == 0) {
            was_selected = 1;
            continue;
        }
        if (used && used + 1 >= sizeof(updated)) return 0;
        if (used) updated[used++] = ',';
        if (length >= sizeof(updated) - used) return 0;
        memcpy(updated + used, start, length);
        used += length;
        ++count;
    }
    if (!was_selected) {
        size_t name_len = strlen(name);
        if (count >= TATR_MAX_TAGS ||
            name_len >= sizeof(updated) - used - (used ? 1 : 0))
            return 0;
        if (used) updated[used++] = ',';
        memcpy(updated + used, name, name_len);
        used += name_len;
    }
    updated[used] = 0;
    memcpy(form->tags, updated, used + 1);
    return 1;
}

static size_t *focused_cursor(EditForm *form) {
    switch (form->focused) {
    case 0: return &form->title_cursor;
    case 2: return &form->priority_cursor;
    case 4: return &form->body_cursor;
    default: return NULL;
    }
}

static char *focused_text(EditForm *form, size_t *length, size_t *capacity) {
    switch (form->focused) {
    case 0:
        *length = strlen(form->title);
        *capacity = sizeof(form->title);
        return form->title;
    case 2:
        *length = strlen(form->priority);
        *capacity = sizeof(form->priority);
        return form->priority;
    case 4:
        *length = form->body_len;
        *capacity = form->body_capacity;
        return form->body;
    default:
        return NULL;
    }
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void fit_text(const char *value, int max_width, char *out, size_t out_size) {
    if (!out_size) return;
    size_t n = strlen(value);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, value, n);
    out[n] = 0;
    if (ren_get_font_width(ui_font, out) <= max_width) return;

    while (n > 0) {
        out[--n] = 0;
        if (ren_get_font_width(ui_font, out) + 18 <= max_width) {
            if (n + 4 < out_size) strcat(out, "...");
            return;
        }
    }
}

static void draw_button(const char *label, int x, int y, int width, int active) {
    RenColor fill = active ? rgb(46, 80, 105) : rgb(31, 39, 51);
    RenColor foreground = active ? rgb(220, 241, 255) : rgb(160, 174, 192);
    rect(x, y, width, 30, fill);
    text(label, x + 10, y + 7, foreground);
}

static void draw_wrapped_cursor(const char *value, int x, int y, int width,
                                int bottom, size_t cursor, int show_cursor) {
    if (!value) value = "";
    size_t text_len = strlen(value);
    int line_height = ren_get_font_height(ui_font) + 7;
    char line[1024];
    size_t offset = 0;
    int cursor_drawn = 0;

    while (offset < text_len && y + line_height <= bottom) {
        size_t start = offset;
        size_t scan = offset;
        size_t last_space = SIZE_MAX;
        size_t used = 0;
        int forced_line = 0;
        while (scan < text_len && value[scan] != '\n' &&
               used + 4 < sizeof(line)) {
            size_t next = utf8_next(value, text_len, scan);
            if (value[scan] == '\r') {
                scan = next;
                continue;
            }
            size_t char_len = next - scan;
            memcpy(line + used, value + scan, char_len);
            used += char_len;
            line[used] = 0;
            if (value[scan] == ' ') last_space = scan;
            scan = next;
            if (ren_get_font_width(ui_font, line) > width) {
                if (last_space != SIZE_MAX && last_space > start) {
                    used = last_space - start;
                    scan = last_space + 1;
                    while (scan < text_len && value[scan] == ' ') ++scan;
                }
                forced_line = 1;
                break;
            }
        }

        size_t next_offset = scan;
        if (next_offset < text_len && value[next_offset] == '\n') ++next_offset;
        while (used && line[used - 1] == ' ') --used;
        line[used] = 0;
        text(line, x, y, rgb(185, 197, 213));
        int cursor_at_final_end =
            cursor == text_len && next_offset == text_len &&
            (!text_len || value[text_len - 1] != '\n');
        if (show_cursor && !cursor_drawn && cursor >= start &&
            (cursor < next_offset || cursor_at_final_end)) {
            size_t prefix_len = cursor - start;
            if (prefix_len > used) prefix_len = used;
            char prefix[sizeof(line)];
            memcpy(prefix, line, prefix_len);
            prefix[prefix_len] = 0;
            int cursor_x = x + ren_get_font_width(ui_font, prefix);
            rect(cursor_x, y + 1, 1, line_height - 3, rgb(225, 235, 246));
            cursor_drawn = 1;
        }
        y += line_height;
        offset = next_offset;
        if (!forced_line && offset >= text_len) break;
    }

    if (show_cursor && !cursor_drawn && cursor >= text_len &&
        y + line_height <= bottom) {
        rect(x, y + 1, 1, line_height - 3, rgb(225, 235, 246));
    }
}

static void draw_wrapped(const char *value, int x, int y, int width, int bottom) {
    draw_wrapped_cursor(value, x, y, width, bottom, 0, 0);
}
typedef struct {
    int x;
    int y;
    int width;
    int height;
    int title_y;
    int status_x;
    int status_y;
    int status_width;
    int priority_x;
    int priority_y;
    int priority_width;
    int tags_y;
    int body_y;
    int body_height;
    int save_x;
    int cancel_x;
    int buttons_y;
    int button_width;
} FormLayout;

typedef struct {
    int x;
    int y;
    int width;
    int row_height;
    size_t first_tag;
    size_t visible_tags;
} TagMenuLayout;

static FormLayout make_form_layout(const UiLayout *layout) {
    FormLayout form = {0};
    form.width = layout->width - 2 * layout->margin;
    if (form.width > 760) form.width = 760;
    if (form.width < 1) form.width = 1;
    form.height = layout->height - 2 * layout->margin;
    if (form.height > 700) form.height = 700;
    if (form.height < 1) form.height = 1;
    form.x = (layout->width - form.width) / 2;
    form.y = (layout->height - form.height) / 2;
    form.title_y = form.y + 84;
    form.status_x = form.x + 24;
    form.status_y = form.y + 154;
    form.status_width = (form.width - 60) / 2;
    form.priority_x = form.status_x + form.status_width + 12;
    form.priority_y = form.status_y;
    form.priority_width = form.status_width;
    form.tags_y = form.y + 226;
    form.body_y = form.y + 294;
    form.buttons_y = form.y + form.height - 54;
    form.body_height = form.buttons_y - form.body_y - 54;
    if (form.body_height < 44) form.body_height = 44;
    form.button_width = 112;
    form.cancel_x = form.x + form.width - form.button_width - 24;
    form.save_x = form.cancel_x - form.button_width - 12;
    return form;
}

static TagMenuLayout make_tag_menu_layout(const FormLayout *form,
                                          const TatrTagDocs *tags,
                                          size_t scroll) {
    TagMenuLayout menu = {0};
    menu.x = form->x + 24;
    menu.y = form->tags_y + 34;
    menu.width = form->width - 48;
    menu.row_height = 26;
    menu.first_tag = scroll;
    int available = form->buttons_y - menu.y - 4;
    size_t max_rows = available > 0 ? (size_t)(available / menu.row_height) : 0;
    if (max_rows > 7) max_rows = 7;
    if (max_rows) --max_rows;
    size_t remaining = tags->count > scroll ? tags->count - scroll : 0;
    menu.visible_tags = remaining < max_rows ? remaining : max_rows;
    return menu;
}

static void draw_form_field(const char *label, const char *value,
                            size_t cursor, int x, int label_y, int width,
                            int focused) {
    text(label, x, label_y, rgb(160, 174, 192));
    rect(x, label_y + 20, width, 34,
         focused ? rgb(39, 57, 75) : rgb(29, 37, 49));
    rect(x, label_y + 20, width, 1,
         focused ? rgb(93, 164, 205) : rgb(51, 62, 78));
    char shown[1024];
    fit_text(value, width - 18, shown, sizeof(shown));
    text(shown, x + 9, label_y + 29, rgb(222, 231, 242));
    if (focused) {
        size_t visible_cursor = cursor < strlen(shown) ? cursor : strlen(shown);
        char prefix[sizeof(shown)];
        memcpy(prefix, shown, visible_cursor);
        prefix[visible_cursor] = 0;
        int cursor_x = x + 9 + ren_get_font_width(ui_font, prefix);
        int right = x + width - 6;
        if (cursor_x > right) cursor_x = right;
        rect(cursor_x, label_y + 25, 1, 21, rgb(225, 235, 246));
    }
}

static void draw_tag_menu(const EditForm *form, const TatrTagDocs *tags,
                          const FormLayout *pos) {
    TagMenuLayout menu = make_tag_menu_layout(pos, tags, form->tag_scroll);
    size_t rows = menu.visible_tags + 1;
    int height = (int)rows * menu.row_height;
    if (menu.y + height > pos->buttons_y - 4)
        height = pos->buttons_y - menu.y - 4;
    if (height < menu.row_height) return;
    rect(menu.x, menu.y, menu.width, height, rgb(18, 23, 31));
    for (size_t i = 0; i < menu.visible_tags; ++i) {
        size_t index = menu.first_tag + i;
        int row_y = menu.y + (int)i * menu.row_height;
        int highlighted = form->tag_highlight == index;
        if (highlighted)
            rect(menu.x + 1, row_y, menu.width - 2, menu.row_height,
                 rgb(39, 57, 75));
        char label[256];
        snprintf(label, sizeof(label), "%s %s",
                 tag_is_selected(form, tags->items[index].name) ? "[x]" : "[ ]",
                 tags->items[index].name);
        fit_text(label, menu.width - 24, label, sizeof(label));
        text(label, menu.x + 10, row_y + 5, rgb(220, 229, 239));
        if (tags->items[index].desc && *tags->items[index].desc) {
            char description[256];
            fit_text(tags->items[index].desc, menu.width / 2,
                     description, sizeof(description));
            int desc_x = menu.x + menu.width -
                         ren_get_font_width(ui_font, description) - 10;
            text(description, desc_x, row_y + 5, rgb(130, 148, 170));
        }
    }
    int create_y = menu.y + (int)menu.visible_tags * menu.row_height;
    if (create_y + menu.row_height <= pos->buttons_y - 4) {
        if (form->tag_highlight == tags->count)
            rect(menu.x + 1, create_y, menu.width - 2, menu.row_height,
                 rgb(39, 57, 75));
        text("+ Create tag...", menu.x + 10, create_y + 5,
             rgb(167, 201, 224));
    }
}

static void draw_tag_creator(const EditForm *form, const FormLayout *pos) {
    int width = pos->width < 440 ? pos->width - 24 : 416;
    if (width < 1) width = 1;
    int x = pos->x + (pos->width - width) / 2;
    int y = pos->y + pos->height / 2 - 84;
    rect(0, 0, pos->x * 2 + pos->width, pos->y * 2 + pos->height,
         rgb(6, 8, 12));
    rect(x, y, width, 168, rgb(27, 34, 45));
    text("Create project tag", x + 20, y + 18, rgb(235, 241, 249));
    text("Tag names cannot contain spaces or commas.", x + 20, y + 46,
         rgb(145, 163, 183));
    rect(x + 20, y + 72, width - 40, 36, rgb(39, 57, 75));
    text(form->new_tag, x + 30, y + 82, rgb(222, 231, 242));
    int caret_x = x + 30 + ren_get_font_width(ui_font, form->new_tag);
    if (caret_x > x + width - 26) caret_x = x + width - 26;
    rect(caret_x, y + 79, 1, 22, rgb(225, 235, 246));
    if (form->message[0])
        text(form->message, x + 20, y + 116, rgb(239, 145, 120));
    draw_button("Create", x + width - 228, y + 132, 96, 1);
    draw_button("Cancel", x + width - 120, y + 132, 96, 0);
}

static void draw_edit_form(const EditForm *form, const TatrTagDocs *tags,
                           const UiLayout *layout) {
    FormLayout pos = make_form_layout(layout);
    rect(0, 0, layout->width, layout->height, rgb(11, 14, 20));
    rect(pos.x, pos.y, pos.width, pos.height, rgb(22, 27, 36));
    text(form->is_new ? "Create task" : "Edit task", pos.x + 24,
         pos.y + 24, rgb(235, 241, 249));
    text("Changes are saved to TASK.md.", pos.x + 24,
         pos.y + 51, rgb(130, 148, 170));

    draw_form_field("Title", form->title, form->title_cursor,
                    pos.x + 24, pos.y + 64,
                    pos.width - 48, form->focused == 0);

    text("Status", pos.status_x, pos.status_y - 4, rgb(160, 174, 192));
    rect(pos.status_x, pos.status_y + 16, pos.status_width, 34,
         form->focused == 1 ? rgb(39, 57, 75) : rgb(29, 37, 49));
    text(form->status, pos.status_x + 10, pos.status_y + 25,
         strcmp(form->status, "CLOSED") == 0
             ? rgb(155, 168, 185) : rgb(91, 190, 151));

    draw_form_field("Priority (number, blank for unset)", form->priority,
                    form->priority_cursor,
                    pos.priority_x, pos.priority_y - 24,
                    pos.priority_width, form->focused == 2);
    text("Tags (select one or more)", pos.x + 24, pos.tags_y - 20,
         rgb(160, 174, 192));
    rect(pos.x + 24, pos.tags_y, pos.width - 48, 34,
         form->focused == 3 ? rgb(39, 57, 75) : rgb(29, 37, 49));
    char selected_tags[512];
    if (form->tags[0])
        fit_text(form->tags, pos.width - 90, selected_tags, sizeof(selected_tags));
    else
        snprintf(selected_tags, sizeof(selected_tags), "Select project tags...");
    text(selected_tags, pos.x + 34, pos.tags_y + 9, rgb(222, 231, 242));
    text(form->tags_open ? "^" : "v", pos.x + pos.width - 46,
         pos.tags_y + 9, rgb(167, 201, 224));

    text("Description", pos.x + 24, pos.body_y - 22,
         rgb(160, 174, 192));
    rect(pos.x + 24, pos.body_y, pos.width - 48, pos.body_height,
         form->focused == 4 ? rgb(39, 57, 75) : rgb(29, 37, 49));
    rencache_set_clip_rect((RenRect){pos.x + 32, pos.body_y + 4,
                                     pos.width - 64, pos.body_height - 8});
    draw_wrapped_cursor(form->body, pos.x + 34, pos.body_y + 8,
                        pos.width - 68, pos.body_y + pos.body_height - 4,
                        form->body_cursor,
                        form->focused == 4 &&
                            ((fenster_time() / 500) % 2 == 0));
    rencache_set_clip_rect((RenRect){0, 0, layout->width, layout->height});

    if (form->tags_open) draw_tag_menu(form, tags, &pos);
    if (form->message[0])
        text(form->message, pos.x + 24, pos.buttons_y - 27,
             rgb(239, 145, 120));
    draw_button("Save", pos.save_x, pos.buttons_y, pos.button_width, 1);
    draw_button("Cancel", pos.cancel_x, pos.buttons_y, pos.button_width, 0);
    if (form->creating_tag) draw_tag_creator(form, &pos);
}

static void draw_list(const TatrList *list, UiState *state, size_t count,
                      const UiLayout *layout) {
    rencache_set_clip_rect((RenRect){layout->sidebar_x, layout->sidebar_y,
                                    layout->sidebar_w, layout->sidebar_h});
    rect(layout->sidebar_x, layout->sidebar_y, layout->sidebar_w,
         layout->sidebar_h, rgb(22, 27, 36));
    text("TASKS", layout->sidebar_x + 18, layout->sidebar_y + 16,
         rgb(224, 232, 243));

    const char *filter_names[] = {"All", "Open", "Closed"};
    for (int i = 0; i < 3; ++i) {
        draw_button(filter_names[i], layout->filter_x[i],
                    layout->sidebar_y + 44, layout->filter_w,
                    state->filter == (TaskFilter)i);
    }
    char sort_label[48];
    snprintf(sort_label, sizeof(sort_label), "%s %s",
             state->sort_by_id ? "ID" : "Priority",
             state->ascending ? "^" : "v");
    draw_button(sort_label, layout->sort_x, layout->sidebar_y + 44,
                layout->sort_w, 0);
    rect(layout->sidebar_x + 18, layout->sidebar_y + 80, 8, 8,
         rgb(239, 112, 95));
    text("High", layout->sidebar_x + 30, layout->sidebar_y + 76,
         rgb(155, 168, 185));
    rect(layout->sidebar_x + 88, layout->sidebar_y + 80, 8, 8,
         rgb(239, 183, 82));
    text("Mid", layout->sidebar_x + 100, layout->sidebar_y + 76,
         rgb(155, 168, 185));
    rect(layout->sidebar_x + 150, layout->sidebar_y + 80, 8, 8,
         rgb(91, 190, 151));
    text("Low", layout->sidebar_x + 162, layout->sidebar_y + 76,
         rgb(155, 168, 185));
    rect(layout->sidebar_x + 210, layout->sidebar_y + 80, 8, 8,
         rgb(119, 133, 151));
    text("Unset", layout->sidebar_x + 222, layout->sidebar_y + 76,
         rgb(155, 168, 185));

    int rows_y = layout->sidebar_y + 104;
    int rows_bottom = layout->sidebar_y + layout->sidebar_h;
    size_t rows = rows_bottom > rows_y
        ? (size_t)((rows_bottom - rows_y) / ROW_H) : 0;
    if (state->selected >= count && count) state->selected = count - 1;
    if (count && state->selected < state->scroll) state->scroll = state->selected;
    if (count && rows && state->selected >= state->scroll + rows) {
        state->scroll = state->selected - rows + 1;
    }
    if (state->scroll > count) state->scroll = count;

    for (size_t row = 0; row < rows; ++row) {
        size_t index = state->scroll + row;
        const TatrTask *task = visible_task(list, state->filter, index);
        if (!task) break;
        int y = rows_y + (int)row * ROW_H;
        int selected = index == state->selected;
        RenColor priority_tint = priority_color(list, task);
        rect(layout->sidebar_x + 10, y, layout->sidebar_w - 20, ROW_H - 4,
             selected ? rgb(39, 53, 70) : rgb(22, 27, 36));
        rect(layout->sidebar_x + 10, y, 3, ROW_H - 4,
             tatr_status_is_closed(task) ? rgb(111, 130, 150) : rgb(76, 195, 153));
        rect(layout->sidebar_x + layout->sidebar_w - 15, y, 3, ROW_H - 4,
             priority_tint);

        char title[256];
        fit_text(task->title ? task->title : "(untitled)", layout->sidebar_w - 120,
                 title, sizeof(title));
        text(title, layout->sidebar_x + 24, y + 9,
             selected ? rgb(238, 244, 251) : rgb(204, 214, 226));

        char metadata[256];
        char formatted_tags[160];
        tatr_format_tags(task, formatted_tags, sizeof(formatted_tags));
        char priority[24];
        if (!task->has_priority) {
            snprintf(priority, sizeof(priority), "P --");
        } else {
            snprintf(priority, sizeof(priority), "P %d", task->priority);
        }
        snprintf(metadata, sizeof(metadata), "%s  |  %s",
                 task->id ? task->id : "no id",
                 formatted_tags[0] ? formatted_tags : "no tags");
        fit_text(metadata, layout->sidebar_w - 120, title, sizeof(title));
        text(title, layout->sidebar_x + 24, y + 37, rgb(129, 145, 164));
        int badge_x = layout->sidebar_x + layout->sidebar_w - 94;
        rect(badge_x, y + 8, 66, 24,
             rgb(priority_tint.r / 5, priority_tint.g / 5,
                 priority_tint.b / 5));
        fit_text(priority, 56, title, sizeof(title));
        text(title, badge_x + 7, y + 12, priority_tint);
    }

    if (!count) {
        text("No tasks match this filter.", layout->sidebar_x + 18,
             layout->sidebar_y + 112,
             rgb(140, 155, 175));
    }
    rencache_set_clip_rect((RenRect){0, 0, layout->width, layout->height});
}

static void draw_details(const TatrList *list, const TatrTask *task,
                         const TatrTagDocs *tags, const UiLayout *layout) {
    rencache_set_clip_rect((RenRect){layout->details_x, layout->details_y,
                                    layout->details_w, layout->details_h});
    rect(layout->details_x, layout->details_y, layout->details_w,
         layout->details_h, rgb(22, 27, 36));
    if (!task) {
        text("Select a task to view its details.", layout->details_x + 24,
             layout->details_y + 28, rgb(140, 155, 175));
        rencache_set_clip_rect((RenRect){0, 0, layout->width, layout->height});
        return;
    }

    char title[512];
    fit_text(task->title ? task->title : "(untitled)", layout->details_w - 48,
             title, sizeof(title));
    text(title, layout->details_x + 24, layout->details_y + 24,
         rgb(235, 241, 249));
    int edit_width = clamp_int(layout->details_w / 5, 76, 104);
    int edit_x = layout->details_x + layout->details_w - edit_width - 16;
    draw_button("Edit task", edit_x, layout->details_y + 16,
                edit_width, 0);

    char metadata[512];
    char priority[24];
    if (!task->has_priority) {
        snprintf(priority, sizeof(priority), "unset");
    } else {
        snprintf(priority, sizeof(priority), "%d", task->priority);
    }
    snprintf(metadata, sizeof(metadata), "%s   %s",
             task->id ? task->id : "no id",
             tatr_status_is_closed(task) ? "CLOSED" : "OPEN");
    fit_text(metadata, layout->details_w - 48, title, sizeof(title));
    text(title, layout->details_x + 24, layout->details_y + 66,
         rgb(145, 163, 183));

    RenColor tint = priority_color(list, task);
    char priority_label[64];
    snprintf(priority_label, sizeof(priority_label), "Priority: %s", priority);
    fit_text(priority_label, layout->details_w - 56, title, sizeof(title));
    int badge_width = ren_get_font_width(ui_font, title) + 20;
    if (badge_width > layout->details_w - 48)
        badge_width = layout->details_w - 48;
    if (badge_width < 1) badge_width = 1;
    rect(layout->details_x + 24, layout->details_y + 91, badge_width, 26,
         rgb(tint.r / 5, tint.g / 5, tint.b / 5));
    text(title, layout->details_x + 34, layout->details_y + 96, tint);

    int tag_y = layout->details_y + 130;
    int tag_x = layout->details_x + 24;
    for (int i = 0; i < task->ntags; ++i) {
        const char *description = tatr_tag_desc(tags, task->tags[i]);
        char label[256];
        snprintf(label, sizeof(label), "%s%s%s", task->tags[i],
                 description && *description ? " - " : "",
                 description && *description ? description : "");
        fit_text(label, layout->details_w - 56, title, sizeof(title));
        int tag_width = ren_get_font_width(ui_font, title) + 20;
        if (tag_width > layout->details_w - 40)
            tag_width = layout->details_w - 40;
        if (tag_width < 1) tag_width = 1;
        if (tag_x + tag_width > layout->details_x + layout->details_w - 20) {
            tag_x = layout->details_x + 24;
            tag_y += 30;
        }
        rect(tag_x, tag_y, tag_width, 24, rgb(35, 48, 62));
        text(title, tag_x + 10, tag_y + 4, rgb(167, 201, 224));
        tag_x += tag_width + 8;
    }

    int body_y = task->ntags ? tag_y + 42 : layout->details_y + 138;
    rect(layout->details_x + 24, body_y - 10, layout->details_w - 48, 1,
         rgb(45, 54, 67));
    draw_wrapped(task->body, layout->details_x + 24, body_y + 8,
                 layout->details_w - 48,
                 layout->details_y + layout->details_h - 20);
    rencache_set_clip_rect((RenRect){0, 0, layout->width, layout->height});
}

static void draw_ui(const TatrList *list, const TatrTagDocs *tags,
                    const char *tasks_dir, UiState *state,
                    EditForm *form, const UiLayout *layout) {
    if (form->active) {
        rencache_begin_frame();
        draw_edit_form(form, tags, layout);
        rencache_end_frame();
        return;
    }

    if (!tasks_dir[0]) {
        rencache_begin_frame();
        rect(0, 0, layout->width, layout->height, rgb(15, 19, 26));
        text("TATR", 26, 22, rgb(234, 241, 250));

        int card_width = layout->width - 2 * layout->margin;
        if (card_width > 560) card_width = 560;
        if (card_width < 1) card_width = 1;
        int card_height = 230;
        int card_x = (layout->width - card_width) / 2;
        int card_y = (layout->height - card_height) / 2;
        rect(card_x, card_y, card_width, card_height, rgb(22, 27, 36));
        text("Welcome to TATR", card_x + 28, card_y + 32,
             rgb(235, 241, 249));
        text("Open a project folder to browse its tasks.",
             card_x + 28, card_y + 76, rgb(160, 174, 192));
        text("Choose either the project root or its tasks folder.",
             card_x + 28, card_y + 106, rgb(130, 148, 170));

        RenRect button = welcome_button_rect(layout);
        rect(button.x, button.y, button.width, button.height,
             rgb(46, 80, 105));
        text("Choose project folder", button.x + 17, button.y + 11,
             rgb(220, 241, 255));
        rencache_end_frame();
        return;
    }

    size_t count = visible_count(list, state->filter);
    const TatrTask *selected = count
        ? visible_task(list, state->filter, state->selected) : NULL;

    rencache_begin_frame();
    rect(0, 0, layout->width, layout->height, rgb(15, 19, 26));
    text("TATR", 26, 22, rgb(234, 241, 250));
    int project_button_w = project_button_width(layout);
    int project_button_x = layout->width - layout->margin - project_button_w;
    int new_button_x = project_button_x - 112;
    draw_button("New task", new_button_x, 18, 100, 0);
    draw_button(project_button_w >= 120 ? "Open project" : "Project",
                project_button_x, 18, project_button_w, 0);
    char subtitle[320];
    snprintf(subtitle, sizeof(subtitle), "%zu task%s  |  %s",
             list->count, list->count == 1 ? "" : "s", tasks_dir);
    char fitted[320];
    fit_text(subtitle, new_button_x - 132, fitted, sizeof(fitted));
    text(fitted, 114, 27, rgb(130, 148, 170));

    draw_list(list, state, count, layout);
    draw_details(list, selected, tags, layout);
    rencache_end_frame();
}

static void show_project_error(const char *message) {
    tinyfd_messageBox("TATR", message, "ok", "error", 1);
}

static void choose_project(TatrList *list, TatrTagDocs *tags,
                           char *tasks_dir, size_t tasks_dir_size,
                           UiState *state) {
    char *selected = tinyfd_selectFolderDialog("Choose a TATR project folder",
                                                NULL);
    if (!selected) return;

    char new_tasks_dir[4096];
    const char *resolved = tatr_find_tasks_dir(selected, new_tasks_dir,
                                                sizeof(new_tasks_dir));
    if (!resolved) {
        show_project_error("The selected folder has no tasks directory.");
        return;
    }
    if (strlen(new_tasks_dir) >= tasks_dir_size) {
        show_project_error("The selected tasks directory path is too long.");
        return;
    }

    TatrList new_list = {0};
    TatrTagDocs new_tags = {0};
    if (!tatr_load_dir(&new_list, new_tasks_dir)) {
        show_project_error("Unable to read tasks from the selected project.");
        tatr_list_free(&new_list);
        return;
    }
    if (!tatr_load_tag_docs(&new_tags, new_tasks_dir)) {
        show_project_error("Unable to read tag documentation from the selected project.");
        tatr_list_free(&new_list);
        tatr_tags_free(&new_tags);
        return;
    }

    tatr_sort(&new_list, state->sort_by_id, state->ascending);
    tatr_list_free(list);
    tatr_tags_free(tags);
    *list = new_list;
    *tags = new_tags;
    memcpy(tasks_dir, new_tasks_dir, strlen(new_tasks_dir) + 1);
    state->selected = 0;
    state->scroll = 0;
}

static int reload_after_save(TatrList *list, TatrTagDocs *tags,
                             const char *tasks_dir, UiState *state,
                             const char *selected_id,
                             char *error, size_t error_size) {
    TatrList new_list = {0};
    TatrTagDocs new_tags = {0};
    if (!tatr_load_dir(&new_list, tasks_dir)) {
        snprintf(error, error_size, "Saved, but unable to reload project tasks.");
        return 0;
    }
    if (!tatr_load_tag_docs(&new_tags, tasks_dir)) {
        tatr_list_free(&new_list);
        snprintf(error, error_size, "Saved, but unable to reload task tag docs.");
        return 0;
    }

    tatr_sort(&new_list, state->sort_by_id, state->ascending);
    tatr_list_free(list);
    tatr_tags_free(tags);
    *list = new_list;
    *tags = new_tags;

    state->filter = FILTER_ALL;
    state->selected = 0;
    state->scroll = 0;
    for (size_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].id, selected_id) == 0) {
            state->selected = i;
            break;
        }
    }
    return 1;
}

static int save_edit_form(EditForm *form, TatrList *list, TatrTagDocs *tags,
                          const char *tasks_dir, UiState *state) {
    if (!form->title[0]) {
        snprintf(form->message, sizeof(form->message), "Enter a task title.");
        return 0;
    }
    if (strcmp(form->status, "OPEN") != 0 &&
        strcmp(form->status, "CLOSED") != 0) {
        snprintf(form->message, sizeof(form->message), "Choose OPEN or CLOSED status.");
        return 0;
    }

    bool has_priority = form->priority[0] != 0;
    int priority = 0;
    if (has_priority) {
        errno = 0;
        char *end = NULL;
        long parsed = strtol(form->priority, &end, 10);
        if (errno == ERANGE || end == form->priority || *end != 0 ||
            parsed < INT_MIN || parsed > INT_MAX) {
            snprintf(form->message, sizeof(form->message),
                     "Priority must be a whole number or blank.");
            return 0;
        }
        priority = (int)parsed;
    }

    char saved_id[sizeof(form->task_id)];
    if (form->is_new) {
        if (!tatr_create_task(tasks_dir, form->title, form->status,
                              has_priority, priority, form->tags, form->body,
                              saved_id, sizeof(saved_id),
                              form->message, sizeof(form->message)))
            return 0;
    } else {
        TatrTask *task = NULL;
        for (size_t i = 0; i < list->count; ++i) {
            if (strcmp(list->items[i].id, form->task_id) == 0) {
                task = &list->items[i];
                break;
            }
        }
        if (!task) {
            snprintf(form->message, sizeof(form->message),
                     "The task is no longer in the loaded project.");
            return 0;
        }
        if (!tatr_save_task(tasks_dir, task, form->title, form->status,
                            has_priority, priority, form->tags, form->body,
                            form->message, sizeof(form->message)))
            return 0;
        copy_form_field(saved_id, sizeof(saved_id), form->task_id);
    }

    char reload_error[160] = {0};
    if (!reload_after_save(list, tags, tasks_dir, state, saved_id,
                           reload_error, sizeof(reload_error))) {
        tinyfd_messageBox("TATR", reload_error, "ok", "error", 1);
    }
    edit_form_close(form);
    return 1;
}

static int field_text_append(EditForm *form, const char *input, size_t len) {
    if (form->focused == 4) {
        char filtered[64];
        size_t used = 0;
        if (len >= sizeof(filtered)) return 0;
        for (size_t i = 0; i < len; ++i) {
            if ((unsigned char)input[i] < 0x20 && input[i] != '\n') continue;
            filtered[used++] = input[i];
        }
        return form_append_body(form, filtered, used);
    }

    if (form->focused == 2) {
        size_t current_len = strlen(form->priority);
        for (size_t i = 0; i < len; ++i) {
            if (!(input[i] >= '0' && input[i] <= '9') &&
                !(input[i] == '-' && form->priority_cursor == 0 &&
                  current_len == 0))
                continue;
            if (!insert_text(form->priority, sizeof(form->priority),
                             &current_len, &form->priority_cursor,
                             input + i, 1))
                return 0;
        }
        return 1;
    }

    if (form->focused != 0) return 1;
    char filtered[64];
    size_t used = 0;
    if (len >= sizeof(filtered)) return 0;
    for (size_t i = 0; i < len; ++i) {
        if ((unsigned char)input[i] < 0x20) continue;
        filtered[used++] = input[i];
    }
    size_t current_len = strlen(form->title);
    return insert_text(form->title, sizeof(form->title), &current_len,
                       &form->title_cursor, filtered, used);
}

static void move_focused_cursor(EditForm *form, int direction, int by_word) {
    size_t length = 0;
    size_t capacity = 0;
    char *value = focused_text(form, &length, &capacity);
    size_t *cursor = focused_cursor(form);
    (void)capacity;
    if (!value || !cursor) return;
    if (direction < 0) {
        *cursor = by_word ? word_left(value, *cursor)
                          : utf8_previous(value, *cursor);
    } else {
        *cursor = by_word ? word_right(value, length, *cursor)
                          : utf8_next(value, length, *cursor);
    }
}

static void backspace_focused(EditForm *form, int by_word) {
    size_t length = 0;
    size_t capacity = 0;
    char *value = focused_text(form, &length, &capacity);
    size_t *cursor = focused_cursor(form);
    (void)capacity;
    if (!value || !cursor || !*cursor) return;
    if (by_word) {
        *cursor = delete_word_left(value, &length, *cursor);
    } else {
        *cursor = delete_previous_character(value, &length, *cursor);
    }
    if (form->focused == 4) {
        form->body_len = length;
    }
}

static int create_editor_tag(EditForm *form, TatrTagDocs *tags,
                             const char *tasks_dir) {
    if (!form->new_tag[0]) {
        snprintf(form->message, sizeof(form->message), "Enter a tag name.");
        return 0;
    }
    EditForm candidate = *form;
    if (!tag_is_selected(form, form->new_tag) &&
        !toggle_form_tag(&candidate, form->new_tag)) {
        snprintf(form->message, sizeof(form->message),
                 "This task cannot hold another tag.");
        return 0;
    }
    if (!tatr_create_tag(tasks_dir, form->new_tag,
                         form->message, sizeof(form->message)))
        return 0;

    TatrTagDocs reloaded = {0};
    if (!tatr_load_tag_docs(&reloaded, tasks_dir)) {
        tatr_tags_free(&reloaded);
        snprintf(form->message, sizeof(form->message),
                 "Tag was written, but project tags could not be reloaded.");
        return 0;
    }
    size_t found_index = 0;
    int found = 0;
    for (size_t i = 0; i < reloaded.count; ++i) {
        if (strcmp(reloaded.items[i].name, form->new_tag) == 0) {
            found = 1;
            found_index = i;
            break;
        }
    }
    if (!found) {
        tatr_tags_free(&reloaded);
        snprintf(form->message, sizeof(form->message),
                 "Tag was written, but is missing from the tag list.");
        return 0;
    }

    tatr_tags_free(tags);
    *tags = reloaded;
    if (!tag_is_selected(form, form->new_tag) &&
        !toggle_form_tag(form, form->new_tag)) {
        snprintf(form->message, sizeof(form->message),
                 "Tag created, but could not be selected for this task.");
        return 0;
    }
    form->tag_highlight = found_index;
    form->tag_scroll = found_index;
    form->creating_tag = 0;
    form->tags_open = 1;
    form->new_tag[0] = 0;
    form->new_tag_cursor = 0;
    form->message[0] = 0;
    return 1;
}

static int handle_tag_creator_input(struct fenster *window, EditForm *form,
                                    TatrTagDocs *tags, const char *tasks_dir,
                                    UiState *state, const FormLayout *pos) {
    if (key_pressed(window, state, 27)) {
        form->creating_tag = 0;
        form->new_tag[0] = 0;
        form->message[0] = 0;
    }

    int enter = key_pressed(window, state, 10);
    int backspace = key_pressed(window, state, 8);
    int left = key_pressed(window, state, 20);
    int right = key_pressed(window, state, 19);
    int control = (window->mod & 1) != 0;
    size_t length = strlen(form->new_tag);
    if (backspace)
        form->new_tag_cursor = control
            ? delete_word_left(form->new_tag, &length, form->new_tag_cursor)
            : delete_previous_character(form->new_tag, &length,
                                        form->new_tag_cursor);
    if (left)
        form->new_tag_cursor = control
            ? word_left(form->new_tag, form->new_tag_cursor)
            : utf8_previous(form->new_tag, form->new_tag_cursor);
    if (right)
        form->new_tag_cursor = control
            ? word_right(form->new_tag, length, form->new_tag_cursor)
            : utf8_next(form->new_tag, length, form->new_tag_cursor);

    if (window->text_input_len > 0) {
        char filtered[64];
        size_t used = 0;
        for (int i = 0; i < window->text_input_len; ++i) {
            unsigned char ch = (unsigned char)window->text_input[i];
            if (ch <= 0x20 || ch == ',' || ch == 0x7f) continue;
            filtered[used++] = window->text_input[i];
        }
        if (!insert_text(form->new_tag, sizeof(form->new_tag), &length,
                         &form->new_tag_cursor, filtered, used)) {
            snprintf(form->message, sizeof(form->message), "Tag name is too long.");
        } else {
            form->message[0] = 0;
        }
    }
    if (enter) create_editor_tag(form, tags, tasks_dir);

    int mouse_down = window->mouse != 0;
    if (mouse_down && !state->mouse_was_down) {
        int width = pos->width < 440 ? pos->width - 24 : 416;
        if (width < 1) width = 1;
        int x = pos->x + (pos->width - width) / 2;
        int y = pos->y + pos->height / 2 - 84;
        if (point_in_rect(window->x, window->y, x + width - 228,
                          y + 132, 96, 30)) {
            create_editor_tag(form, tags, tasks_dir);
        } else if (point_in_rect(window->x, window->y, x + width - 120,
                                 y + 132, 96, 30)) {
            form->creating_tag = 0;
            form->new_tag[0] = 0;
            form->message[0] = 0;
        }
    }
    state->mouse_was_down = mouse_down;
    return 0;
}

static void choose_tag_menu_item(EditForm *form, TatrTagDocs *tags,
                                 size_t index) {
    form->tag_highlight = index;
    if (index == tags->count) {
        form->creating_tag = 1;
        form->new_tag[0] = 0;
        form->new_tag_cursor = 0;
        form->message[0] = 0;
        return;
    }
    if (index < tags->count && !toggle_form_tag(form, tags->items[index].name))
        snprintf(form->message, sizeof(form->message),
                 "This task cannot hold another tag.");
    else
        form->message[0] = 0;
}

static int handle_form_input(struct fenster *window, EditForm *form,
                             TatrList *list, TatrTagDocs *tags,
                             char *tasks_dir, UiState *state,
                             const UiLayout *layout) {
    FormLayout pos = make_form_layout(layout);
    if (form->creating_tag)
        return handle_tag_creator_input(window, form, tags, tasks_dir, state,
                                        &pos);

    if (key_pressed(window, state, 27)) {
        edit_form_close(form);
        return 0;
    }
    int tab = key_pressed(window, state, 9);
    int enter = key_pressed(window, state, 10);
    int backspace = key_pressed(window, state, 8);
    int left = key_pressed(window, state, 20);
    int right = key_pressed(window, state, 19);
    int up = key_pressed(window, state, 17);
    int down = key_pressed(window, state, 18);
    int space = key_pressed(window, state, 32);
    int save_shortcut = enter && (window->mod & (1 | 8));
    int control = (window->mod & 1) != 0;
    if (tab) {
        form->tags_open = 0;
        form->focused = (form->focused + 1) % 5;
    } else if (save_shortcut) {
        save_edit_form(form, list, tags, tasks_dir, state);
        return 0;
    } else if (form->focused == 3 && form->tags_open) {
        size_t total = tags->count + 1;
        if (up && form->tag_highlight > 0) --form->tag_highlight;
        if (down && form->tag_highlight + 1 < total)
            ++form->tag_highlight;
        TagMenuLayout menu = make_tag_menu_layout(&pos, tags, form->tag_scroll);
        if (form->tag_highlight < form->tag_scroll)
            form->tag_scroll = form->tag_highlight;
        else if (menu.visible_tags &&
                 form->tag_highlight >= form->tag_scroll + menu.visible_tags)
            form->tag_scroll =
                form->tag_highlight - menu.visible_tags + 1;
        if (enter || space)
            choose_tag_menu_item(form, tags, form->tag_highlight);
    } else if (form->focused == 3 && (enter || space)) {
        form->tags_open = 1;
        form->tag_scroll = 0;
        form->tag_highlight = 0;
    } else if (enter && form->focused != 4) {
        form->focused = (form->focused + 1) % 5;
    } else if (enter && form->focused == 4) {
        if (!form_append_body(form, "\n", 1))
            snprintf(form->message, sizeof(form->message),
                     "Not enough memory to extend the description.");
    }

    if (form->focused == 1 && space) {
        snprintf(form->status, sizeof(form->status), "%s",
                 strcmp(form->status, "OPEN") == 0 ? "CLOSED" : "OPEN");
    }

    if (backspace && !form->tags_open && form->focused != 1)
        backspace_focused(form, control);
    if (!form->tags_open && form->focused != 1 && form->focused != 3) {
        if (left) move_focused_cursor(form, -1, control);
        if (right) move_focused_cursor(form, 1, control);
    }

    if (!form->tags_open && form->focused != 3 &&
        window->text_input_len > 0) {
        if (!field_text_append(form, window->text_input,
                               (size_t)window->text_input_len)) {
            snprintf(form->message, sizeof(form->message),
                     "Field is full or the input is not valid here.");
        } else {
            form->message[0] = 0;
        }
    }

    int mouse_down = window->mouse != 0;
    if (mouse_down && !state->mouse_was_down) {
        int x = window->x;
        int y = window->y;
        TagMenuLayout menu = make_tag_menu_layout(&pos, tags,
                                                  form->tag_scroll);
        int menu_click_handled = 0;
        if (form->tags_open &&
            point_in_rect(x, y, pos.x + 24, pos.tags_y,
                          pos.width - 48, 34)) {
            form->tags_open = 0;
            menu_click_handled = 1;
        } else if (form->tags_open &&
                   point_in_rect(x, y, menu.x, menu.y, menu.width,
                                 (int)(menu.visible_tags + 1) *
                                     menu.row_height)) {
            size_t row = (size_t)(y - menu.y) / (size_t)menu.row_height;
            size_t index = row < menu.visible_tags
                ? menu.first_tag + row : tags->count;
            choose_tag_menu_item(form, tags, index);
            menu_click_handled = 1;
        } else if (form->tags_open) {
            form->tags_open = 0;
        }

        if (menu_click_handled) {
            state->mouse_was_down = mouse_down;
            return 0;
        }
        if (form->creating_tag) {
            state->mouse_was_down = mouse_down;
            return 0;
        }

        if (point_in_rect(x, y, pos.x + 24, pos.y + 84,
                          pos.width - 48, 34)) {
            form->focused = 0;
            form->title_cursor = strlen(form->title);
        } else if (point_in_rect(x, y, pos.status_x, pos.status_y + 16,
                                 pos.status_width, 34)) {
            form->focused = 1;
            snprintf(form->status, sizeof(form->status), "%s",
                     strcmp(form->status, "OPEN") == 0 ? "CLOSED" : "OPEN");
        } else if (point_in_rect(x, y, pos.priority_x, pos.priority_y + 16,
                                 pos.priority_width, 34)) {
            form->focused = 2;
            form->priority_cursor = strlen(form->priority);
        } else if (point_in_rect(x, y, pos.x + 24, pos.tags_y,
                                 pos.width - 48, 34)) {
            form->focused = 3;
            form->tags_open = 1;
            form->tag_scroll = 0;
            form->tag_highlight = 0;
        } else if (point_in_rect(x, y, pos.x + 24, pos.body_y,
                                 pos.width - 48, pos.body_height)) {
            form->focused = 4;
        } else if (point_in_rect(x, y, pos.save_x, pos.buttons_y,
                                 pos.button_width, 30)) {
            state->mouse_was_down = mouse_down;
            save_edit_form(form, list, tags, tasks_dir, state);
            return 0;
        } else if (point_in_rect(x, y, pos.cancel_x, pos.buttons_y,
                                 pos.button_width, 30)) {
            edit_form_close(form);
        }
    }
    state->mouse_was_down = mouse_down;
    return 0;
}

static int handle_input(struct fenster *window, TatrList *list,
                         TatrTagDocs *tags, char *tasks_dir,
                         size_t tasks_dir_size, UiState *state,
                         EditForm *form,
                         const UiLayout *layout) {
    if (form->active)
        return handle_form_input(window, form, list, tags, tasks_dir,
                                 state, layout);
    size_t count = visible_count(list, state->filter);
    int down_pressed = key_pressed(window, state, 18);
    int up_pressed = key_pressed(window, state, 17);
    int page_down = key_pressed(window, state, 4);
    int page_up = key_pressed(window, state, 3);
    int escape = key_pressed(window, state, 27);
    int sort_priority = key_pressed(window, state, 'P');
    int sort_id = key_pressed(window, state, 'I');
    int filter = key_pressed(window, state, 'F');
    int reverse = key_pressed(window, state, 'O');

    if (down_pressed && state->selected + 1 < count) ++state->selected;
    if (up_pressed && state->selected > 0) --state->selected;
    if (page_down && count) {
        state->selected += layout->row_count > 0
            ? (size_t)layout->row_count : 1;
        if (state->selected >= count) state->selected = count - 1;
    }
    if (page_up) {
        size_t page = layout->row_count > 0 ? (size_t)layout->row_count : 1;
        state->selected = state->selected > page ? state->selected - page : 0;
    }
    if (sort_priority || sort_id) {
        state->sort_by_id = sort_id != 0;
        tatr_sort(list, state->sort_by_id, state->ascending);
        state->selected = state->scroll = 0;
    }
    if (reverse) {
        state->ascending = !state->ascending;
        tatr_sort(list, state->sort_by_id, state->ascending);
        state->selected = state->scroll = 0;
    }
    if (filter) {
        state->filter = (TaskFilter)((state->filter + 1) % 3);
        state->selected = state->scroll = 0;
    }
    int mouse_down = window->mouse != 0;
    if (mouse_down && !state->mouse_was_down) {
        int x = window->x;
        int y = window->y;
        RenRect welcome_button = welcome_button_rect(layout);
        int project_width = project_button_width(layout);
        int project_button_x = layout->width - layout->margin - project_width;
        int new_button_x = project_button_x - 112;
        if (!tasks_dir[0] &&
            point_in_rect(x, y, welcome_button.x, welcome_button.y,
                          welcome_button.width, welcome_button.height)) {
            choose_project(list, tags, tasks_dir, tasks_dir_size, state);
        } else if (tasks_dir[0] &&
                   point_in_rect(x, y, new_button_x, 18, 100, 30)) {
            if (!edit_form_open(form, NULL))
                tinyfd_messageBox("TATR", "Unable to open the task editor.",
                                  "ok", "error", 1);
        } else if (tasks_dir[0] &&
                   point_in_rect(x, y, project_button_x, 18,
                                 project_width, 30)) {
            choose_project(list, tags, tasks_dir, tasks_dir_size, state);
        } else if (tasks_dir[0] && count) {
            const TatrTask *selected_task =
                visible_task(list, state->filter, state->selected);
            int edit_width = clamp_int(layout->details_w / 5, 76, 104);
            int edit_x = layout->details_x + layout->details_w - edit_width - 16;
            if (selected_task &&
                point_in_rect(x, y, edit_x, layout->details_y + 16,
                              edit_width, 30)) {
                if (!edit_form_open(form, selected_task))
                    tinyfd_messageBox("TATR",
                                      "The task has fields too large for the editor.",
                                      "ok", "error", 1);
            } else if (point_in_rect(x, y, layout->filter_x[0],
                                     layout->sidebar_y + 44,
                                     layout->filter_w, 30)) {
                state->filter = FILTER_ALL;
                state->selected = state->scroll = 0;
            } else if (point_in_rect(x, y, layout->filter_x[1],
                                     layout->sidebar_y + 44,
                                     layout->filter_w, 30)) {
                state->filter = FILTER_OPEN;
                state->selected = state->scroll = 0;
            } else if (point_in_rect(x, y, layout->filter_x[2],
                                     layout->sidebar_y + 44,
                                     layout->filter_w, 30)) {
                state->filter = FILTER_CLOSED;
                state->selected = state->scroll = 0;
            } else if (point_in_rect(x, y, layout->sort_x,
                                     layout->sidebar_y + 44,
                                     layout->sort_w, 30)) {
                state->sort_by_id = !state->sort_by_id;
                tatr_sort(list, state->sort_by_id, state->ascending);
                state->selected = state->scroll = 0;
            } else if (point_in_rect(x, y, layout->sidebar_x + 10,
                                     layout->row_y, layout->sidebar_w - 20,
                                     layout->row_count * ROW_H)) {
                size_t row = (size_t)((y - layout->row_y) / ROW_H);
                size_t selected = state->scroll + row;
                if (selected < count) state->selected = selected;
            }
        } else if (point_in_rect(x, y, layout->filter_x[0], layout->sidebar_y + 44,
                          layout->filter_w, 30)) {
            state->filter = FILTER_ALL;
            state->selected = state->scroll = 0;
        } else if (point_in_rect(x, y, layout->filter_x[1],
                                 layout->sidebar_y + 44, layout->filter_w, 30)) {
            state->filter = FILTER_OPEN;
            state->selected = state->scroll = 0;
        } else if (point_in_rect(x, y, layout->filter_x[2],
                                 layout->sidebar_y + 44, layout->filter_w, 30)) {
            state->filter = FILTER_CLOSED;
            state->selected = state->scroll = 0;
        } else if (point_in_rect(x, y, layout->sort_x,
                                 layout->sidebar_y + 44, layout->sort_w, 30)) {
            state->sort_by_id = !state->sort_by_id;
            tatr_sort(list, state->sort_by_id, state->ascending);
            state->selected = state->scroll = 0;
        } else if (point_in_rect(x, y, layout->sidebar_x + 10,
                                 layout->row_y, layout->sidebar_w - 20,
                                 layout->row_count * ROW_H)) {
            size_t row = (size_t)((y - layout->row_y) / ROW_H);
            size_t selected = state->scroll + row;
            if (selected < count) state->selected = selected;
        }
    }
    state->mouse_was_down = mouse_down;
    return escape;
}

static RenFont *load_ui_font(void) {
    const char *configured = getenv("TATR_FONT");
    if (configured && *configured) {
        RenFont *font = ren_load_font(configured, 16.0f);
        if (font) return font;
        fprintf(stderr, "Unable to load TATR_FONT: %s\n", configured);
    }

    const char *candidates[4];
    char windows_font[512];
#ifdef _WIN32
    const char *windir = getenv("WINDIR");
    if (!windir || !*windir) windir = "C:\\Windows";
    snprintf(windows_font, sizeof(windows_font), "%s\\Fonts\\segoeui.ttf", windir);
    candidates[0] = windows_font;
    candidates[1] = "C:\\Windows\\Fonts\\arial.ttf";
    candidates[2] = "C:\\Windows\\Fonts\\tahoma.ttf";
    candidates[3] = NULL;
#elif defined(__APPLE__)
    candidates[0] = "/System/Library/Fonts/Supplemental/Arial.ttf";
    candidates[1] = "/System/Library/Fonts/SFNS.ttf";
    candidates[2] = "/Library/Fonts/Arial.ttf";
    candidates[3] = NULL;
#else
    candidates[0] = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    candidates[1] = "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf";
    candidates[2] = "/usr/share/fonts/TTF/DejaVuSans.ttf";
    candidates[3] = NULL;
#endif
    for (size_t i = 0; candidates[i]; ++i) {
        RenFont *font = ren_load_font(candidates[i], 16.0f);
        if (font) return font;
    }
    return NULL;
}

int tatr_desktop_ui(TatrList *list, TatrTagDocs *tags, char *tasks_dir,
                    size_t tasks_dir_size) {
    UiState state = {0};
    EditForm form = {0};
    state.filter = FILTER_OPEN;
    uint32_t *pixels = (uint32_t *)calloc(
        INITIAL_WINDOW_WIDTH * INITIAL_WINDOW_HEIGHT, sizeof(*pixels));
    if (!pixels) {
        fprintf(stderr, "Unable to allocate the window framebuffer.\n");
        return 1;
    }

    struct fenster window = {
        .title = "TATR Tasks",
        .width = INITIAL_WINDOW_WIDTH,
        .height = INITIAL_WINDOW_HEIGHT,
        .buf = pixels,
        .buf_ref = &pixels
    };
    if (fenster_open(&window) != 0) {
        fprintf(stderr, "Unable to open the Fenster desktop window.\n");
        fenster_close(&window);
        free(pixels);
        return 1;
    }

    ren_init(&window);
    ui_font = load_ui_font();
    if (!ui_font) {
        fprintf(stderr, "No supported system font found. Set TATR_FONT to a .ttf file.\n");
        fenster_close(&window);
        free(pixels);
        return 1;
    }

    int running = 1;
    while (running) {
        if (fenster_loop(&window) < 0) break;
        UiLayout layout = make_layout(window.width, window.height);
        if (handle_input(&window, list, tags, tasks_dir, tasks_dir_size,
                         &state, &form, &layout)) running = 0;
        if (!running) break;
        draw_ui(list, tags, tasks_dir, &state, &form, &layout);
        fenster_sleep(16);
    }

    ren_free_font(ui_font);
    ui_font = NULL;
    edit_form_close(&form);
    fenster_close(&window);
    free(pixels);
    return 0;
}

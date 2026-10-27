#include "ui.h"

#include "renderer.h"
#include "vendor/fenster.h"
#include "vendor/lite/rencache.h"
#include "vendor/tinyfiledialogs/tinyfiledialogs.h"

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

static void draw_wrapped(const char *value, int x, int y, int width, int bottom) {
    const char *p = value ? value : "";
    int line_height = ren_get_font_height(ui_font) + 7;
    char line[1024];

    while (*p && y + line_height <= bottom) {
        const char *start = p;
        const char *scan = p;
        const char *last_space = NULL;
        size_t used = 0;
        int forced_line = 0;
        while (*scan && *scan != '\n' && used + 1 < sizeof(line)) {
            char ch = *scan++;
            if (ch == '\r') continue;
            line[used++] = ch;
            line[used] = 0;
            if (ch == ' ') last_space = scan - 1;
            if (ren_get_font_width(ui_font, line) > width) {
                if (last_space && last_space > start) {
                    used = (size_t)(last_space - start);
                    scan = last_space + 1;
                    while (*scan == ' ') ++scan;
                }
                forced_line = 1;
                break;
            }
        }

        if (*scan == '\n') {
            p = scan + 1;
        } else {
            p = scan;
        }
        while (used && line[used - 1] == ' ') --used;
        line[used] = 0;
        text(line, x, y, rgb(185, 197, 213));
        y += line_height;
        if (!forced_line && !*p) break;
    }
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
                    const UiLayout *layout) {
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
    draw_button(project_button_w >= 120 ? "Open project" : "Project",
                project_button_x, 18, project_button_w, 0);
    char subtitle[320];
    snprintf(subtitle, sizeof(subtitle), "%zu task%s  |  %s",
             list->count, list->count == 1 ? "" : "s", tasks_dir);
    char fitted[320];
    fit_text(subtitle, project_button_x - 132, fitted, sizeof(fitted));
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

static int handle_input(struct fenster *window, TatrList *list,
                         TatrTagDocs *tags, char *tasks_dir,
                         size_t tasks_dir_size, UiState *state,
                         const UiLayout *layout) {
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
        if (!tasks_dir[0] &&
            point_in_rect(x, y, welcome_button.x, welcome_button.y,
                          welcome_button.width, welcome_button.height)) {
            choose_project(list, tags, tasks_dir, tasks_dir_size, state);
        } else if (tasks_dir[0] &&
                   point_in_rect(x, y, project_button_x, 18,
                                 project_width, 30)) {
            choose_project(list, tags, tasks_dir, tasks_dir_size, state);
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
                         &state, &layout)) running = 0;
        if (!running) break;
        draw_ui(list, tags, tasks_dir, &state, &layout);
        fenster_sleep(16);
    }

    ren_free_font(ui_font);
    ui_font = NULL;
    fenster_close(&window);
    free(pixels);
    return 0;
}

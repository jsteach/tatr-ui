#pragma once

#include <stdint.h>

struct fenster;
typedef struct RenFont RenFont;

typedef struct {
    uint8_t b, g, r, a;
} RenColor;

typedef struct {
    int x, y, width, height;
} RenRect;

void ren_init(struct fenster *window);
void ren_update_rects(RenRect *rects, int count);
void ren_set_clip_rect(RenRect rect);
void ren_get_size(int *width, int *height);

RenFont *ren_load_font(const char *filename, float size);
void ren_free_font(RenFont *font);
void ren_set_font_tab_width(RenFont *font, int width);
int ren_get_font_tab_width(RenFont *font);
int ren_get_font_width(RenFont *font, const char *text);
int ren_get_font_height(RenFont *font);

void ren_draw_rect(RenRect rect, RenColor color);
int ren_draw_text(RenFont *font, const char *text, int x, int y, RenColor color);

#include "renderer.h"
#include "vendor/fenster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb/stb_truetype.h"

#define FONT_ATLAS_MAX 2048

struct RenFont {
    unsigned char *data;
    unsigned char *atlas;
    stbtt_fontinfo info;
    stbtt_bakedchar glyphs[256];
    int atlas_width;
    int atlas_height;
    int height;
    int tab_width;
    float size;
};

static struct fenster *ren_window;
static RenRect ren_clip;

static RenRect intersect_rects(RenRect a, RenRect b) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    int y2 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    RenRect result = {x1, y1, x2 > x1 ? x2 - x1 : 0, y2 > y1 ? y2 - y1 : 0};
    return result;
}

void ren_init(struct fenster *window) {
    ren_window = window;
    ren_clip = (RenRect){0, 0, window->width, window->height};
}

void ren_update_rects(RenRect *rects, int count) {
    (void)rects;
    (void)count;
}

void ren_set_clip_rect(RenRect rect) {
    if (ren_window) {
        ren_clip = intersect_rects(rect, (RenRect){0, 0, ren_window->width, ren_window->height});
    }
}

void ren_get_size(int *width, int *height) {
    *width = ren_window ? ren_window->width : 0;
    *height = ren_window ? ren_window->height : 0;
}

RenFont *ren_load_font(const char *filename, float size) {
    FILE *file = fopen(filename, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    RenFont *font = (RenFont *)calloc(1, sizeof(*font));
    if (!font) {
        fclose(file);
        return NULL;
    }
    font->data = (unsigned char *)malloc((size_t)file_size);
    if (!font->data) {
        fclose(file);
        free(font);
        return NULL;
    }
    if (fread(font->data, 1, (size_t)file_size, file) != (size_t)file_size) {
        fclose(file);
        free(font->data);
        free(font);
        return NULL;
    }
    fclose(file);

    font->size = size;
    font->tab_width = 4;
    if (!stbtt_InitFont(&font->info, font->data, 0)) {
        free(font->data);
        free(font);
        return NULL;
    }

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    float scale = stbtt_ScaleForMappingEmToPixels(&font->info, size);
    font->height = (int)((ascent - descent + line_gap) * scale + 0.5f);
    if (font->height < 1) font->height = (int)size;

    for (font->atlas_width = 256; font->atlas_width <= FONT_ATLAS_MAX;
         font->atlas_width *= 2) {
        font->atlas_height = font->atlas_width;
        font->atlas = (unsigned char *)calloc(
            (size_t)font->atlas_width, (size_t)font->atlas_height);
        if (!font->atlas) break;
        int baked = stbtt_BakeFontBitmap(font->data, 0, size, font->atlas,
                                         font->atlas_width, font->atlas_height,
                                         0, 256, font->glyphs);
        if (baked >= 0) break;
        free(font->atlas);
        font->atlas = NULL;
    }
    if (!font->atlas) {
        free(font->data);
        free(font);
        return NULL;
    }

    int scaled_ascent = (int)(ascent * scale + 0.5f);
    for (int i = 0; i < 256; ++i) {
        font->glyphs[i].yoff += (float)scaled_ascent;
    }
    return font;
}

void ren_free_font(RenFont *font) {
    if (!font) return;
    free(font->atlas);
    free(font->data);
    free(font);
}

void ren_set_font_tab_width(RenFont *font, int width) {
    if (font && width > 0) font->tab_width = width;
}

int ren_get_font_tab_width(RenFont *font) {
    return font ? font->tab_width : 0;
}

static const char *decode_utf8(const char *p, unsigned *codepoint) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) {
        *codepoint = c;
        return p + 1;
    }
    unsigned count = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;
    if (!count) {
        *codepoint = '?';
        return p + 1;
    }
    unsigned value = c & (0x7f >> (count + 1));
    for (unsigned i = 1; i <= count; ++i) {
        unsigned char next = (unsigned char)p[i];
        if (!next || (next & 0xc0) != 0x80) {
            *codepoint = '?';
            return p + 1;
        }
        value = (value << 6) | (next & 0x3f);
    }
    *codepoint = value;
    return p + count + 1;
}

static unsigned char glyph_index(unsigned char ch) {
    return ch >= 32 ? ch : (unsigned char)'?';
}

int ren_get_font_width(RenFont *font, const char *text) {
    if (!font || !text) return 0;
    int width = 0;
    int widest = 0;
    while (*text) {
        unsigned codepoint;
        text = decode_utf8(text, &codepoint);
        if (codepoint == '\n') {
            if (width > widest) widest = width;
            width = 0;
        } else if (codepoint == '\t') {
            width += (int)font->glyphs[' '].xadvance * font->tab_width;
        } else if (codepoint != '\r') {
            unsigned char ch = codepoint < 256 ? glyph_index((unsigned char)codepoint) : '?';
            width += (int)font->glyphs[ch].xadvance;
        }
    }
    return width > widest ? width : widest;
}

int ren_get_font_height(RenFont *font) {
    return font ? font->height : 0;
}

static void blend_pixel(int x, int y, RenColor color, unsigned char alpha) {
    if (!ren_window || x < ren_clip.x || y < ren_clip.y ||
        x >= ren_clip.x + ren_clip.width || y >= ren_clip.y + ren_clip.height) {
        return;
    }
    unsigned int coverage = (unsigned int)alpha * color.a / 255;
    if (!coverage) return;
    uint32_t *pixel = &fenster_pixel(ren_window, x, y);
    uint32_t dst = *pixel;
    unsigned int inv = 255 - coverage;
    unsigned int r = (color.r * coverage + ((dst >> 16) & 0xff) * inv + 127) / 255;
    unsigned int g = (color.g * coverage + ((dst >> 8) & 0xff) * inv + 127) / 255;
    unsigned int b = (color.b * coverage + (dst & 0xff) * inv + 127) / 255;
    *pixel = (r << 16) | (g << 8) | b;
}

void ren_draw_rect(RenRect rect, RenColor color) {
    if (!ren_window) return;
    rect = intersect_rects(rect, ren_clip);
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        for (int x = rect.x; x < rect.x + rect.width; ++x) {
            blend_pixel(x, y, color, 255);
        }
    }
}

int ren_draw_text(RenFont *font, const char *text, int x, int y, RenColor color) {
    if (!font || !text) return x;
    int start_x = x;
    int line_y = y;
    int tab_span = (int)font->glyphs[' '].xadvance * font->tab_width;
    if (tab_span < 1) tab_span = 1;

    while (*text) {
        unsigned codepoint;
        const char *next = decode_utf8(text, &codepoint);
        text = next;
        if (codepoint == '\n') {
            x = start_x;
            line_y += font->height;
            continue;
        }
        if (codepoint == '\r') continue;
        if (codepoint == '\t') {
            x += tab_span;
            continue;
        }
        unsigned char ch = codepoint < 256 ? glyph_index((unsigned char)codepoint) : '?';
        stbtt_bakedchar *glyph = &font->glyphs[ch];
        int gx = x + (int)glyph->xoff;
        int gy = line_y + (int)glyph->yoff;
        for (int yy = glyph->y0; yy < glyph->y1; ++yy) {
            for (int xx = glyph->x0; xx < glyph->x1; ++xx) {
                unsigned char alpha =
                    font->atlas[yy * font->atlas_width + xx];
                blend_pixel(gx + xx - glyph->x0, gy + yy - glyph->y0, color, alpha);
            }
        }
        x += (int)glyph->xadvance;
    }
    return x;
}

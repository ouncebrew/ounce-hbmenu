/* SPDX-License-Identifier: ISC
 * Copyright 2026 nx-hbmenu Switch 2 port contributors
 *
 * GPU draw list for upstream hbmenu on Switch 2.
 *
 * Upstream hbmenu draws every frame on the CPU, one pixel at a time, into a
 * 1280x720 framebuffer. On Switch 2 the same calls build a list of textured
 * quads that the Vulkan backend (graphics_vulkan.c) draws at 1920x1080:
 *
 *   - DrawPixel, DrawPixelRaw and Draw4PixelsRaw (common/common.h) extend
 *     runs of equal colour, which are merged into rectangles;
 *   - drawImage and drawIcon (common/menu.c) draw a cached texture of the
 *     image buffer;
 *   - text (common/font.c) draws glyphs from a cached atlas;
 *   - waves (common/menu.c) draw one column of rectangles per logical x.
 *
 * Logical coordinates are upstream's 1280x720 layout; each logical pixel
 * covers the physical pixels HBMENU_SCALE_COORD gives it (render_scale.h).
 * Glyph coordinates are physical, as in upstream's scaled font path.
 */
#ifndef HBMENU_SWITCH2_RENDER_H
#define HBMENU_SWITCH2_RENDER_H

#include <stdbool.h>
#include <stdint.h>

enum RenderImageFormat {
    RenderImageFormat_Rgb24,          /* upstream IMAGE_MODE_RGB24 */
    RenderImageFormat_Rgba32,         /* upstream IMAGE_MODE_RGBA32 */
    RenderImageFormat_AlphaMask,      /* RGBA32 alpha tinted by a colour (drawIcon) */
    RenderImageFormat_Rgb24Rounded,   /* RGB24 with anti-aliased Switch 2 rounded corners */
    RenderImageFormat_Rgba32Rounded,  /* RGBA32 with anti-aliased Switch 2 rounded corners */
};

/* The open horizontal run of equal-colour pixels, in logical coordinates.
 * y is -1 when no run is open. Colours are color_t.abgr values. It is
 * visible here only so that renderPixels, called for every upstream pixel,
 * can extend a run inline; other code must not use it. */
struct RenderPixelRun {
    int32_t x0;
    int32_t x1;
    int32_t y;
    uint32_t color;
};

extern struct RenderPixelRun render_open_run;

void renderPixelsSlow(uint32_t x, uint32_t y, uint32_t width, uint32_t color);

/* Draws `width` logical pixels from (x, y), blending by the colour's alpha.
 * The caller has clipped the pixels to the logical screen. */
static inline void renderPixels(uint32_t x, uint32_t y, uint32_t width, uint32_t color) {
    if ((int32_t)y == render_open_run.y && (int32_t)x == render_open_run.x1 &&
        color == render_open_run.color) {
        render_open_run.x1 += (int32_t)width;
        return;
    }
    renderPixelsSlow(x, y, width, color);
}

/* Sets physical pixels per logical pixel, num/den, between frames. Clears
 * the glyph atlas: glyphs are rendered at the new size. */
void renderSetScale(uint32_t num, uint32_t den);

/* Fills the logical rectangle [x0, x1) x [y0, y1), blending by alpha. */
void renderRect(int x0, int y0, int x1, int y1, uint32_t color);

/* Fills the logical rectangle with rows y0..y1-1 of a 720-row colour table
 * (upstream's frontWaveGradient), ignoring the table's alpha. */
void renderGradientRect(int x0, int y0, int x1, int y1, const uint32_t gradient[720]);

/* Draws a width x height image buffer at logical (x, y). The buffer is
 * cached as a texture keyed by its address, size and format until
 * renderForgetImage. `tint` multiplies the image (alpha masks use its RGB). */
void renderImage(const void *pixels, int x, int y, int width, int height,
                 enum RenderImageFormat format, uint32_t tint);

/* Draws an image_width x image_height image buffer scaled to width x height
 * at logical (x, y), with the same cache as renderImage. */
void renderImageScaled(const void *pixels, int image_width, int image_height,
                       int x, int y, int width, int height,
                       enum RenderImageFormat format, uint32_t tint);

/* Floating-point logical coordinates variant for smooth animations. */
void renderImageScaledF(const void *pixels, int image_width, int image_height,
                        float x, float y, float width, float height,
                        enum RenderImageFormat format, uint32_t tint);

/* Switch 2 HOME UI GPU primitives (logical coordinates, scaled natively). */
void renderRectF(float x, float y, float w, float h, uint32_t color);
void renderCircleF(float cx, float cy, float radius, uint32_t color);
void renderStarF(float cx, float cy, float radius, uint32_t color);
void renderNetloaderIconF(float cx, float cy, float radius, uint32_t color);
void renderSettingsIconF(float cx, float cy, float radius, uint32_t color);
void renderRoundedRectF(float x, float y, float w, float h, float radius, uint32_t color);
void renderSoftShadowF(float x, float y, float w, float h, float radius, float spread, uint8_t alpha);
void renderSwitch2FocusRingF(float x, float y, float w, float h, float radius, float thickness,
                             float timer, uint32_t primary_color, uint32_t secondary_color);
void renderSwitch2CircleFocusRingF(float cx, float cy, float radius, float thickness,
                                   float timer, uint32_t primary_color, uint32_t secondary_color);

/* Drops textures made from an image buffer that is about to be freed, and
 * any vector it was drawn from. */
void renderForgetImage(const void *pixels);

/* Draws an image buffer from a vector (nanosvg) instead: renderImage
 * rasterizes the vector at the size the image covers on screen, rasterizing
 * it again after renderSetScale. Takes ownership of the vector. The buffer
 * remains the image's bitmap at its layout size for upstream code. */
struct NSVGimage;
void renderSetImageVector(const void *pixels, struct NSVGimage *vector);
/* Draws `alias`, an image made from `pixels` (such as a resized copy), from
 * the same vector. */
void renderAliasImageVector(const void *alias, const void *pixels);

/* Glyph atlas. Adds a coverage bitmap and returns its atlas position;
 * returns false when the atlas is full. The atlas is cleared at the start
 * of the next frame after it fills, which increments the generation. */
bool renderGlyphAtlasAdd(uint32_t width, uint32_t height, const uint8_t *bitmap, int pitch,
                         uint16_t *atlas_x, uint16_t *atlas_y);
uint32_t renderGlyphAtlasGeneration(void);

/* Draws an atlas glyph at physical (x, y), coloured by `color`. */
void renderGlyph(int x, int y, uint32_t width, uint32_t height,
                 uint16_t atlas_x, uint16_t atlas_y, uint32_t color);

#endif

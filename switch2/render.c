/* SPDX-License-Identifier: ISC
 * Copyright 2026 nx-hbmenu Switch 2 port contributors
 *
 * Draw list for upstream hbmenu on Switch 2 (see render.h).
 */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "render.h"
#include "render_backend.h"
#include "render_scale.h"
#include "third_party/nanosvg/nanosvg.h"
#include "third_party/nanosvg/nanosvgrast.h"

enum {
    ATLAS_SIZE = 2048,
    ATLAS_PADDING = 1,
    GRADIENT_ROWS = 720,
    MAX_IMAGES = 512,
    MAX_VECTORS = 64,
    /* Recently closed pixel runs that a new run may extend. */
    OPEN_RECTS = 16,
    /* A run merges into an earlier rectangle only if few quads were drawn
     * since; each of them must be checked for overlap. */
    MERGE_LOOKBACK = 32,
    INITIAL_QUADS = 16384,
};

uint32_t hbmenu_scale_num = 3;
uint32_t hbmenu_scale_den = 2;
/* Texture coordinates sample texel centres at pixel centres. Where 1.5x
 * scaling puts a pixel centre exactly on a texel edge, this bias picks the
 * texel that HBMENU_SCALE_COORD assigns that pixel. */
static const float kTexelBias = 0.1f;

struct RenderPixelRun render_open_run = { .y = -1 };

/* A rectangle of pixel runs in logical coordinates, and its quad. */
struct OpenRect {
    int32_t x0, x1, y0, y1;
    uint32_t color;
    uint32_t quad;
    bool valid;
};

struct CachedImage {
    const void *pixels;
    int width;
    int height;
    enum RenderImageFormat format;
    RenderTexture texture;
    /* Rasterized from a vector at the scale it was drawn at, in texels. */
    bool from_vector;
    uint32_t texture_width, texture_height;
};

/* An image buffer that is drawn from a vector instead (renderSetImageVector). */
struct ImageVector {
    const void *pixels;
    NSVGimage *vector;
};

static struct {
    float scale;  /* hbmenu_scale_num / hbmenu_scale_den */
    struct RenderQuad *quads;
    uint32_t count;
    uint32_t capacity;

    struct OpenRect open_rects[OPEN_RECTS];
    uint32_t next_open_rect;

    uint8_t *atlas;
    RenderTexture atlas_texture;
    uint32_t atlas_generation;
    uint32_t cursor_x, cursor_y, row_height;
    bool atlas_full;
    uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

    RenderTexture gradient_texture;
    uint32_t gradient[GRADIENT_ROWS];
    bool gradient_uploaded;

    RenderTexture circle_texture;
    RenderTexture ring_texture;
    RenderTexture circle_ring_texture;
    RenderTexture shadow_texture;
    RenderTexture star_texture;
    RenderTexture netloader_texture;
    RenderTexture gear_texture;
    bool shapes_uploaded;
    bool ring_luts_ready;
    uint8_t rect_angle_lut[256 * 256];
    uint8_t rect_ring_alpha[256 * 256];
    uint8_t circ_angle_lut[128 * 128];
    uint8_t circle_ring_alpha[128 * 128];
    uint32_t iridescent_lut[256]; /* 0x00BBGGRR */

    Mutex image_lock;
    struct CachedImage images[MAX_IMAGES];
    uint32_t image_count;

    /* Under image_lock. The vectors stay allocated until renderShutdown:
     * aliases of a forgotten buffer may still draw them. */
    struct ImageVector vectors[MAX_VECTORS];
    uint32_t vector_count;
    NSVGimage *owned_vectors[MAX_VECTORS];
    uint32_t owned_vector_count;
    NSVGrasterizer *rasterizer;
} render = {
    .scale = 1.5f,
    .atlas_texture = RenderTexture_None,
    .gradient_texture = RenderTexture_None,
    .circle_texture = RenderTexture_None,
    .ring_texture = RenderTexture_None,
    .circle_ring_texture = RenderTexture_None,
    .shadow_texture = RenderTexture_None,
    .star_texture = RenderTexture_None,
    .netloader_texture = RenderTexture_None,
    .gear_texture = RenderTexture_None,
};

/* --- Quads ---------------------------------------------------------------- */

static struct RenderQuad *pushQuad(void) {
    if (render.count == render.capacity) {
        uint32_t capacity = render.capacity ? render.capacity * 2 : INITIAL_QUADS;
        struct RenderQuad *quads = realloc(render.quads, capacity * sizeof(*quads));
        if (!quads)
            return NULL;
        render.quads = quads;
        render.capacity = capacity;
    }
    return &render.quads[render.count++];
}

/* Returns false when the quad list cannot grow; the quad is then dropped. */
static bool pushTexturedQuad(float x0, float y0, float x1, float y1,
                             float u0, float v0, float u1, float v1,
                             uint32_t color, RenderTexture texture) {
    struct RenderQuad *quad = pushQuad();
    if (!quad)
        return false;
    *quad = (struct RenderQuad){
        .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1,
        .u0 = u0, .v0 = v0, .u1 = u1, .v1 = v1,
        .color = color, .texture = texture,
    };
    return true;
}

/* Solid colour quads sample the white texels at the atlas origin. */
static bool pushSolidQuad(float x0, float y0, float x1, float y1, uint32_t color) {
    const float white = 0.5f / ATLAS_SIZE;
    return pushTexturedQuad(x0, y0, x1, y1, white, white, white, white, color,
                     render.atlas_texture);
}

static void setQuadRect(struct RenderQuad *quad, const struct OpenRect *rect) {
    quad->x0 = rect->x0 * render.scale;
    quad->x1 = rect->x1 * render.scale;
    quad->y0 = rect->y0 * render.scale;
    quad->y1 = rect->y1 * render.scale;
}

/* A merged-away quad keeps its slot but covers nothing. */
static void clearQuad(struct RenderQuad *quad) {
    quad->x1 = quad->x0;
    quad->y1 = quad->y0;
}

static bool quadOverlaps(const struct RenderQuad *quad, const struct OpenRect *rect) {
    return quad->x0 < rect->x1 * render.scale && rect->x0 * render.scale < quad->x1 &&
           quad->y0 < rect->y1 * render.scale && rect->y0 * render.scale < quad->y1;
}

/* Whether `rect` can be drawn as part of quad `index` instead of where it
 * stands: no later quad, except `skip`, may cover any of its pixels. */
static bool canDrawEarlier(uint32_t index, const struct OpenRect *rect, uint32_t skip) {
    if (render.count - index > MERGE_LOOKBACK)
        return false;
    for (uint32_t i = index + 1; i < render.count; i++) {
        if (i != skip && quadOverlaps(&render.quads[i], rect))
            return false;
    }
    return true;
}

/* --- Pixel runs ----------------------------------------------------------- */

static bool rectIsAbove(const struct OpenRect *upper, const struct OpenRect *lower) {
    return upper->x0 == lower->x0 && upper->x1 == lower->x1 && upper->y1 == lower->y0;
}

static bool rectIsBeside(const struct OpenRect *rect, const struct OpenRect *row) {
    return rect->y0 == row->y0 && rect->y1 == row->y1 &&
           (rect->x1 == row->x0 || rect->x0 == row->x1);
}

/* After a single-row rectangle grows sideways it may match the rectangle
 * above it; if so, the rows join and its own quad is cleared. */
static void foldIntoRectAbove(struct OpenRect *row) {
    for (uint32_t i = 0; i < OPEN_RECTS; i++) {
        struct OpenRect *upper = &render.open_rects[i];
        if (!upper->valid || upper == row || upper->color != row->color ||
            !rectIsAbove(upper, row) || upper->quad > row->quad ||
            !canDrawEarlier(upper->quad, row, row->quad))
            continue;
        upper->y1 = row->y1;
        setQuadRect(&render.quads[upper->quad], upper);
        clearQuad(&render.quads[row->quad]);
        row->valid = false;
        return;
    }
}

/* Merges a finished run into a recent rectangle when that draws the same
 * pixels in an equivalent order; otherwise draws it as a new rectangle. */
static void closeOpenRun(void) {
    const struct RenderPixelRun run = render_open_run;
    render_open_run.y = -1;
    if (run.y < 0 || (run.color >> 24) == 0)
        return;

    const struct OpenRect added = {
        .x0 = run.x0, .x1 = run.x1, .y0 = run.y, .y1 = run.y + 1,
        .color = run.color,
    };
    for (uint32_t i = 0; i < OPEN_RECTS; i++) {
        struct OpenRect *rect = &render.open_rects[i];
        if (!rect->valid || rect->color != run.color)
            continue;
        const bool below = rectIsAbove(rect, &added);
        const bool beside = rectIsBeside(&added, rect);
        if ((!below && !beside) || !canDrawEarlier(rect->quad, &added, UINT32_MAX))
            continue;
        if (below) {
            rect->y1 = added.y1;
        } else if (rect->x1 == added.x0) {
            rect->x1 = added.x1;
        } else {
            rect->x0 = added.x0;
        }
        setQuadRect(&render.quads[rect->quad], rect);
        if (beside)
            foldIntoRectAbove(rect);
        return;
    }

    if (!pushSolidQuad(added.x0 * render.scale, added.y0 * render.scale, added.x1 * render.scale,
                       added.y1 * render.scale, run.color))
        return;
    struct OpenRect *slot = &render.open_rects[render.next_open_rect];
    render.next_open_rect = (render.next_open_rect + 1) % OPEN_RECTS;
    *slot = added;
    slot->quad = render.count - 1;
    slot->valid = true;
}

void renderPixelsSlow(uint32_t x, uint32_t y, uint32_t width, uint32_t color) {
    closeOpenRun();
    render_open_run = (struct RenderPixelRun){
        .x0 = (int32_t)x, .x1 = (int32_t)(x + width), .y = (int32_t)y, .color = color,
    };
}

void renderRect(int x0, int y0, int x1, int y1, uint32_t color) {
    closeOpenRun();
    if (x0 >= x1 || y0 >= y1 || (color >> 24) == 0)
        return;
    pushSolidQuad(x0 * render.scale, y0 * render.scale, x1 * render.scale, y1 * render.scale, color);
}

/* --- Gradient ------------------------------------------------------------- */

static void updateGradient(const uint32_t gradient[GRADIENT_ROWS]) {
    bool changed = !render.gradient_uploaded;
    for (uint32_t i = 0; i < GRADIENT_ROWS; i++) {
        const uint32_t opaque = gradient[i] | 0xff000000u;
        if (render.gradient[i] != opaque) {
            render.gradient[i] = opaque;
            changed = true;
        }
    }
    if (!changed)
        return;
    uint32_t *texels = renderBackendUpload(render.gradient_texture, 0, 0, 1, GRADIENT_ROWS);
    if (!texels)
        return;
    memcpy(texels, render.gradient, sizeof(render.gradient));
    render.gradient_uploaded = true;
}

void renderGradientRect(int x0, int y0, int x1, int y1, const uint32_t gradient[720]) {
    closeOpenRun();
    if (render.gradient_texture == RenderTexture_None || x0 >= x1 || y0 >= y1)
        return;
    updateGradient(gradient);
    if (!render.gradient_uploaded)
        return;
    pushTexturedQuad(x0 * render.scale, y0 * render.scale, x1 * render.scale, y1 * render.scale,
                     0.5f, (y0 + kTexelBias) / GRADIENT_ROWS,
                     0.5f, (y1 + kTexelBias) / GRADIENT_ROWS,
                     0xffffffffu, render.gradient_texture);
}

/* --- Images --------------------------------------------------------------- */

static void applyRoundedMask(uint8_t *texels, int width, int height) {
    const float w = (float)width;
    const float h = (float)height;
    const float r = (w < h ? w : h) * 0.072f;
    const float hw = w * 0.5f - r;
    const float hh = h * 0.5f - r;
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    for (int y = 0; y < height; y++) {
        const float dy = fabsf((y + 0.5f) - cy) - hh;
        const float qy = dy > 0.0f ? dy : 0.0f;
        for (int x = 0; x < width; x++) {
            const float dx = fabsf((x + 0.5f) - cx) - hw;
            const float qx = dx > 0.0f ? dx : 0.0f;
            if (qx > 0.0f || qy > 0.0f) {
                const float dist = sqrtf(qx * qx + qy * qy) - r;
                if (dist > -1.0f) {
                    float cov = 0.5f - dist * 0.75f;
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                    uint8_t *p = &texels[((size_t)y * (size_t)width + (size_t)x) * 4];
                    p[3] = (uint8_t)((float)p[3] * cov + 0.5f);
                }
            }
        }
    }
}

static void convertImage(uint8_t *texels, const uint8_t *pixels, int width, int height,
                         enum RenderImageFormat format) {
    const size_t count = (size_t)width * (size_t)height;
    uint8_t *out = texels;
    for (size_t i = 0; i < count; i++) {
        switch (format) {
        case RenderImageFormat_Rgb24:
        case RenderImageFormat_Rgb24Rounded:
            out[0] = pixels[0];
            out[1] = pixels[1];
            out[2] = pixels[2];
            out[3] = 0xff;
            pixels += 3;
            break;
        case RenderImageFormat_Rgba32:
        case RenderImageFormat_Rgba32Rounded:
            memcpy(out, pixels, 4);
            pixels += 4;
            break;
        case RenderImageFormat_AlphaMask:
            out[0] = out[1] = out[2] = 0xff;
            out[3] = pixels[3];
            pixels += 4;
            break;
        }
        out += 4;
    }
    if (format == RenderImageFormat_Rgb24Rounded || format == RenderImageFormat_Rgba32Rounded)
        applyRoundedMask(texels, width, height);
}

static RenderTexture createImageTexture(const void *pixels, int width, int height,
                                        enum RenderImageFormat format) {
    const RenderTexture texture = renderBackendCreateTexture((uint32_t)width, (uint32_t)height,
                                                             RenderTextureFormat_Rgba8,
                                                             RenderTextureFilter_Linear);
    if (texture == RenderTexture_None)
        return RenderTexture_None;
    uint8_t *texels = renderBackendUpload(texture, 0, 0, (uint32_t)width, (uint32_t)height);
    if (!texels) {
        renderBackendReleaseTexture(texture);
        return RenderTexture_None;
    }
    convertImage(texels, pixels, width, height, format);
    return texture;
}

static NSVGimage *findVector(const void *pixels) {
    for (uint32_t i = 0; i < render.vector_count; i++) {
        if (render.vectors[i].pixels == pixels)
            return render.vectors[i].vector;
    }
    return NULL;
}

/* Rasterizes a vector at the size a width x height logical image covers on
 * screen now, so it is as sharp as the text at any resolution. */
static RenderTexture createVectorTexture(NSVGimage *vector, int width, int height,
                                         enum RenderImageFormat format,
                                         uint32_t *out_width, uint32_t *out_height) {
    const uint32_t texture_width = (uint32_t)ceilf(width * render.scale);
    const uint32_t texture_height = (uint32_t)ceilf(height * render.scale);
    if (!render.rasterizer)
        render.rasterizer = nsvgCreateRasterizer();
    uint8_t *raster = malloc((size_t)texture_width * texture_height * 4);
    if (!render.rasterizer || !raster || vector->width <= 0.0f) {
        free(raster);
        return RenderTexture_None;
    }
    nsvgRasterize(render.rasterizer, vector, 0, 0, texture_width / vector->width,
                  raster, (int)texture_width, (int)texture_height, (int)texture_width * 4);
    /* An alpha mask is tinted by drawIcon's colour: keep only coverage. */
    if (format == RenderImageFormat_AlphaMask) {
        for (size_t i = 0; i < (size_t)texture_width * texture_height; i++)
            raster[i * 4] = raster[i * 4 + 1] = raster[i * 4 + 2] = 0xff;
    } else if (format == RenderImageFormat_Rgb24Rounded || format == RenderImageFormat_Rgba32Rounded) {
        applyRoundedMask(raster, (int)texture_width, (int)texture_height);
    }

    RenderTexture texture = renderBackendCreateTexture(texture_width, texture_height,
                                                       RenderTextureFormat_Rgba8,
                                                       RenderTextureFilter_Nearest);
    uint8_t *texels = texture == RenderTexture_None ? NULL :
        renderBackendUpload(texture, 0, 0, texture_width, texture_height);
    if (texels) {
        memcpy(texels, raster, (size_t)texture_width * texture_height * 4);
    } else if (texture != RenderTexture_None) {
        renderBackendReleaseTexture(texture);
        texture = RenderTexture_None;
    }
    free(raster);
    *out_width = texture_width;
    *out_height = texture_height;
    return texture;
}

static bool imageTexture(const void *pixels, int width, int height,
                         enum RenderImageFormat format, struct CachedImage *out) {
    mutexLock(&render.image_lock);
    for (uint32_t i = 0; i < render.image_count; i++) {
        const struct CachedImage *image = &render.images[i];
        if (image->pixels == pixels && image->width == width &&
            image->height == height && image->format == format) {
            *out = *image;
            mutexUnlock(&render.image_lock);
            return true;
        }
    }
    struct CachedImage image = {
        .pixels = pixels, .width = width, .height = height, .format = format,
        .texture_width = (uint32_t)width, .texture_height = (uint32_t)height,
    };
    NSVGimage *vector = findVector(pixels);
    if (vector) {
        image.from_vector = true;
        image.texture = createVectorTexture(vector, width, height, format,
                                            &image.texture_width, &image.texture_height);
    } else {
        image.texture = createImageTexture(pixels, width, height, format);
    }
    if (image.texture != RenderTexture_None) {
        if (render.image_count == MAX_IMAGES) {
            renderBackendReleaseTexture(render.images[0].texture);
            render.images[0] = render.images[--render.image_count];
        }
        render.images[render.image_count++] = image;
    }
    mutexUnlock(&render.image_lock);
    *out = image;
    return image.texture != RenderTexture_None;
}

void renderImage(const void *pixels, int x, int y, int width, int height,
                 enum RenderImageFormat format, uint32_t tint) {
    closeOpenRun();
    if (!pixels || width <= 0 || height <= 0)
        return;
    struct CachedImage image;
    if (!imageTexture(pixels, width, height, format, &image))
        return;
    const float x0 = x * render.scale, y0 = y * render.scale;
    const float x1 = (x + width) * render.scale, y1 = (y + height) * render.scale;
    if (image.from_vector) {
        /* Drawn at its own resolution: one texel per physical pixel. */
        pushTexturedQuad(x0, y0, x1, y1, 0.0f, 0.0f,
                         (x1 - x0) / image.texture_width, (y1 - y0) / image.texture_height,
                         tint, image.texture);
        return;
    }
    /* A raster image is scaled to the screen with linear filtering across
     * the whole texture. */
    pushTexturedQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, tint, image.texture);
}

void renderImageScaled(const void *pixels, int image_width, int image_height,
                       int x, int y, int width, int height,
                       enum RenderImageFormat format, uint32_t tint) {
    if (findVector(pixels)) {
        /* A vector is rasterized at the size it is drawn at. */
        renderImage(pixels, x, y, width, height, format, tint);
        return;
    }
    closeOpenRun();
    if (!pixels || image_width <= 0 || image_height <= 0 || width <= 0 || height <= 0)
        return;
    struct CachedImage image;
    if (!imageTexture(pixels, image_width, image_height, format, &image))
        return;
    pushTexturedQuad(x * render.scale, y * render.scale,
                     (x + width) * render.scale, (y + height) * render.scale,
                     0.0f, 0.0f, 1.0f, 1.0f, tint, image.texture);
}

void renderImageScaledF(const void *pixels, int image_width, int image_height,
                        float x, float y, float width, float height,
                        enum RenderImageFormat format, uint32_t tint) {
    closeOpenRun();
    if (!pixels || image_width <= 0 || image_height <= 0 || width <= 0.0f || height <= 0.0f)
        return;
    struct CachedImage image;
    if (!imageTexture(pixels, image_width, image_height, format, &image))
        return;
    pushTexturedQuad(x * render.scale, y * render.scale,
                     (x + width) * render.scale, (y + height) * render.scale,
                     0.0f, 0.0f, 1.0f, 1.0f, tint, image.texture);
}

/* --- Switch 2 HOME UI Primitives ----------------------------------------- */

void renderRectF(float x, float y, float w, float h, uint32_t color) {
    closeOpenRun();
    if (w <= 0.0f || h <= 0.0f || (color >> 24) == 0)
        return;
    pushSolidQuad(x * render.scale, y * render.scale,
                  (x + w) * render.scale, (y + h) * render.scale, color);
}

void renderCircleF(float cx, float cy, float radius, uint32_t color) {
    closeOpenRun();
    if (render.circle_texture == RenderTexture_None || radius <= 0.0f || (color >> 24) == 0)
        return;
    const float s = render.scale;
    pushTexturedQuad((cx - radius) * s, (cy - radius) * s,
                     (cx + radius) * s, (cy + radius) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, color, render.circle_texture);
}

void renderStarF(float cx, float cy, float radius, uint32_t color) {
    closeOpenRun();
    if (render.star_texture == RenderTexture_None || radius <= 0.0f || (color >> 24) == 0)
        return;
    const float s = render.scale;
    pushTexturedQuad((cx - radius) * s, (cy - radius) * s,
                     (cx + radius) * s, (cy + radius) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, color, render.star_texture);
}

void renderNetloaderIconF(float cx, float cy, float radius, uint32_t color) {
    closeOpenRun();
    if (render.netloader_texture == RenderTexture_None || radius <= 0.0f || (color >> 24) == 0)
        return;
    const float s = render.scale;
    pushTexturedQuad((cx - radius) * s, (cy - radius) * s,
                     (cx + radius) * s, (cy + radius) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, color, render.netloader_texture);
}

void renderSettingsIconF(float cx, float cy, float radius, uint32_t color) {
    closeOpenRun();
    if (render.gear_texture == RenderTexture_None || radius <= 0.0f || (color >> 24) == 0)
        return;
    const float s = render.scale;
    pushTexturedQuad((cx - radius) * s, (cy - radius) * s,
                     (cx + radius) * s, (cy + radius) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, color, render.gear_texture);
}

static void pushNineSlice(float x, float y, float w, float h, float r,
                          uint32_t colors[9], RenderTexture tex, bool draw_center) {
    if (tex == RenderTexture_None || w <= 0.0f || h <= 0.0f)
        return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;

    const float s = render.scale;
    const float x0 = x * s, x1 = (x + r) * s, x2 = (x + w - r) * s, x3 = (x + w) * s;
    const float y0 = y * s, y1 = (y + r) * s, y2 = (y + h - r) * s, y3 = (y + h) * s;

    /* 0:TL, 1:T, 2:TR, 3:R, 4:BR, 5:B, 6:BL, 7:L, 8:Center */
    if (r > 0.0f) {
        pushTexturedQuad(x0, y0, x1, y1, 0.0f, 0.0f, 0.5f, 0.5f, colors[0], tex);
        pushTexturedQuad(x2, y0, x3, y1, 0.5f, 0.0f, 1.0f, 0.5f, colors[2], tex);
        pushTexturedQuad(x2, y2, x3, y3, 0.5f, 0.5f, 1.0f, 1.0f, colors[4], tex);
        pushTexturedQuad(x0, y2, x1, y3, 0.0f, 0.5f, 0.5f, 1.0f, colors[6], tex);
    }
    if (x2 > x1 && r > 0.0f) {
        pushTexturedQuad(x1, y0, x2, y1, 0.5f, 0.0f, 0.5f, 0.5f, colors[1], tex);
        pushTexturedQuad(x1, y2, x2, y3, 0.5f, 0.5f, 0.5f, 1.0f, colors[5], tex);
    }
    if (y2 > y1 && r > 0.0f) {
        pushTexturedQuad(x2, y1, x3, y2, 0.5f, 0.5f, 1.0f, 0.5f, colors[3], tex);
        pushTexturedQuad(x0, y1, x1, y2, 0.0f, 0.5f, 0.5f, 0.5f, colors[7], tex);
    }
    if (draw_center && x2 > x1 && y2 > y1) {
        pushTexturedQuad(x1, y1, x2, y2, 0.5f, 0.5f, 0.5f, 0.5f, colors[8], tex);
    }
}

void renderRoundedRectF(float x, float y, float w, float h, float radius, uint32_t color) {
    closeOpenRun();
    if ((color >> 24) == 0)
        return;
    uint32_t c[9] = { color, color, color, color, color, color, color, color, color };
    pushNineSlice(x, y, w, h, radius, c, render.circle_texture, true);
}

void renderSoftShadowF(float x, float y, float w, float h, float radius, float spread, uint8_t alpha) {
    closeOpenRun();
    if (render.shadow_texture == RenderTexture_None || alpha == 0)
        return;
    uint32_t col = ((uint32_t)alpha << 24); /* black with alpha */
    uint32_t c[9] = { col, col, col, col, col, col, col, col, col };
    pushNineSlice(x - spread, y - spread, w + spread * 2.0f, h + spread * 2.0f,
                  radius + spread, c, render.shadow_texture, true);
}

static void initRingLuts(void) {
    if (render.ring_luts_ready)
        return;

    /* 256-entry smooth cosine-interpolated Switch 2 iridescent 360° palette:
     * Bright Cyan -> Electric Blue -> Soft Lavender/Pink -> Ice White-Cyan */
    static const float kR[4] = {  72.0f,  46.0f, 198.0f, 196.0f };
    static const float kG[4] = { 236.0f, 144.0f, 148.0f, 244.0f };
    static const float kB[4] = { 255.0f, 255.0f, 255.0f, 255.0f };
    for (int i = 0; i < 256; i++) {
        float t = ((float)i / 256.0f) * 4.0f;
        int seg = (int)t & 3;
        int next = (seg + 1) & 3;
        float f = t - floorf(t);
        /* Smoothstep cosine easing for zero gradient kinks */
        f = 0.5f - 0.5f * cosf(f * 3.14159265f);
        uint32_t r = (uint32_t)(kR[seg] + (kR[next] - kR[seg]) * f + 0.5f);
        uint32_t g = (uint32_t)(kG[seg] + (kG[next] - kG[seg]) * f + 0.5f);
        uint32_t b = (uint32_t)(kB[seg] + (kB[next] - kB[seg]) * f + 0.5f);
        render.iridescent_lut[i] = (b << 16) | (g << 8) | r;
    }

    /* 1) High-res 256x256 razor-sharp rounded-square focus ring (zero blur at 1080p/4K) */
    const int rsz = 256;
    const float rhalf = 128.0f;
    const float rect_r = 22.0f;
    const float rect_hw = 122.0f - rect_r; /* 6px margin for subtle tight glow */
    for (int y = 0; y < rsz; y++) {
        float dy = (y + 0.5f) - rhalf;
        for (int x = 0; x < rsz; x++) {
            float dx = (x + 0.5f) - rhalf;
            int idx = y * rsz + x;

            float ang = atan2f(dy, dx);
            if (ang < 0.0f) ang += 6.2831853f;
            render.rect_angle_lut[idx] = (uint8_t)((ang / 6.2831853f) * 256.0f);

            float qx = fabsf(dx) - rect_hw;
            float qy = fabsf(dy) - rect_hw;
            float ox = qx > 0.0f ? qx : 0.0f;
            float oy = qy > 0.0f ? qy : 0.0f;
            float inside = qx > qy ? qx : qy;
            if (inside > 0.0f) inside = 0.0f;
            float sd_rect = sqrtf(ox * ox + oy * oy) + inside - rect_r;

            /* Crisp 4.4px stroke (razor-sharp 1px anti-aliased edge) */
            float d_ring = fabsf(sd_rect + 2.2f) - 2.2f;
            float core = 0.5f - d_ring * 1.35f;
            if (core < 0.0f) core = 0.0f;
            if (core > 1.0f) core = 1.0f;
            float glow = 0.0f;
            if (sd_rect > 0.0f && sd_rect < 4.5f) {
                float g = 1.0f - sd_rect / 4.5f;
                glow = g * g * 0.16f;
            }
            float a_rect = core > glow ? core : glow;
            render.rect_ring_alpha[idx] = (uint8_t)(a_rect * 255.0f + 0.5f);
        }
    }

    /* 2) Crisp 128x128 circular focus ring for bottom pill buttons */
    const int csz = 128;
    const float chalf = 64.0f;
    const float circ_r = 59.0f;
    for (int y = 0; y < csz; y++) {
        float dy = (y + 0.5f) - chalf;
        for (int x = 0; x < csz; x++) {
            float dx = (x + 0.5f) - chalf;
            int idx = y * csz + x;

            float ang = atan2f(dy, dx);
            if (ang < 0.0f) ang += 6.2831853f;
            render.circ_angle_lut[idx] = (uint8_t)((ang / 6.2831853f) * 256.0f);

            float sd_circ = sqrtf(dx * dx + dy * dy) - circ_r;
            float d_cring = fabsf(sd_circ + 3.0f) - 3.0f;
            float c_core = 0.5f - d_cring * 1.35f;
            if (c_core < 0.0f) c_core = 0.0f;
            if (c_core > 1.0f) c_core = 1.0f;
            float c_glow = 0.0f;
            if (sd_circ > 0.0f && sd_circ < 3.5f) {
                float g = 1.0f - sd_circ / 3.5f;
                c_glow = g * g * 0.16f;
            }
            float a_circ = c_core > c_glow ? c_core : c_glow;
            render.circle_ring_alpha[idx] = (uint8_t)(a_circ * 255.0f + 0.5f);
        }
    }
    render.ring_luts_ready = true;
}

static void uploadAnimatedRing(RenderTexture tex, int sz, const uint8_t *angle_lut,
                               const uint8_t *alpha_lut, float timer) {
    if (tex == RenderTexture_None)
        return;
    initRingLuts();
    uint32_t *texels = renderBackendUpload(tex, 0, 0, (uint32_t)sz, (uint32_t)sz);
    if (!texels)
        return;
    /* Calm, smooth Switch 2 rotation (~1 full gentle cycle every ~6 seconds) */
    uint8_t shift = (uint8_t)((int)(timer * 14.0f) & 255);
    const int total = sz * sz;
    for (int i = 0; i < total; i++) {
        uint8_t a = alpha_lut[i];
        if (!a) {
            texels[i] = 0;
            continue;
        }
        uint8_t cidx = (uint8_t)(angle_lut[i] + shift);
        texels[i] = ((uint32_t)a << 24) | render.iridescent_lut[cidx];
    }
}

void renderSwitch2FocusRingF(float x, float y, float w, float h, float radius, float thickness,
                             float timer, uint32_t primary_color, uint32_t secondary_color) {
    closeOpenRun();
    (void)radius; (void)thickness; (void)primary_color; (void)secondary_color;
    if (render.ring_texture == RenderTexture_None)
        return;
    uploadAnimatedRing(render.ring_texture, 256, render.rect_angle_lut, render.rect_ring_alpha, timer);
    /* Expand slightly by 6/122 to accommodate the subtle tight outer glow */
    const float glow_pad_x = w * (6.0f / 244.0f);
    const float glow_pad_y = h * (6.0f / 244.0f);
    const float s = render.scale;
    pushTexturedQuad((x - glow_pad_x) * s, (y - glow_pad_y) * s,
                     (x + w + glow_pad_x) * s, (y + h + glow_pad_y) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu, render.ring_texture);
}

void renderSwitch2CircleFocusRingF(float cx, float cy, float radius, float thickness,
                                   float timer, uint32_t primary_color, uint32_t secondary_color) {
    closeOpenRun();
    (void)thickness; (void)primary_color; (void)secondary_color;
    if (render.circle_ring_texture == RenderTexture_None)
        return;
    uploadAnimatedRing(render.circle_ring_texture, 128, render.circ_angle_lut, render.circle_ring_alpha, timer);
    /* Expand by 64/59 to accommodate the subtle tight outer glow */
    const float draw_r = radius * (64.0f / 59.0f);
    const float s = render.scale;
    pushTexturedQuad((cx - draw_r) * s, (cy - draw_r) * s,
                     (cx + draw_r) * s, (cy + draw_r) * s,
                     0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu, render.circle_ring_texture);
}

void renderForgetImage(const void *pixels) {
    if (!pixels)
        return;
    mutexLock(&render.image_lock);
    for (uint32_t i = 0; i < render.image_count;) {
        if (render.images[i].pixels == pixels) {
            renderBackendReleaseTexture(render.images[i].texture);
            render.images[i] = render.images[--render.image_count];
        } else {
            i++;
        }
    }
    for (uint32_t i = 0; i < render.vector_count;) {
        if (render.vectors[i].pixels == pixels)
            render.vectors[i] = render.vectors[--render.vector_count];
        else
            i++;
    }
    mutexUnlock(&render.image_lock);
}

static void addImageVector(const void *pixels, NSVGimage *vector) {
    if (render.vector_count < MAX_VECTORS)
        render.vectors[render.vector_count++] = (struct ImageVector){ pixels, vector };
}

void renderSetImageVector(const void *pixels, struct NSVGimage *vector) {
    if (!pixels || !vector)
        return;
    mutexLock(&render.image_lock);
    if (render.owned_vector_count < MAX_VECTORS) {
        render.owned_vectors[render.owned_vector_count++] = vector;
        addImageVector(pixels, vector);
    } else {
        nsvgDelete(vector);
    }
    mutexUnlock(&render.image_lock);
}

void renderAliasImageVector(const void *alias, const void *pixels) {
    if (!alias || !pixels)
        return;
    mutexLock(&render.image_lock);
    NSVGimage *vector = findVector(pixels);
    if (vector)
        addImageVector(alias, vector);
    mutexUnlock(&render.image_lock);
}

/* Vector textures are rasterized for one scale. */
static void releaseVectorTextures(void) {
    mutexLock(&render.image_lock);
    for (uint32_t i = 0; i < render.image_count;) {
        if (render.images[i].from_vector) {
            renderBackendReleaseTexture(render.images[i].texture);
            render.images[i] = render.images[--render.image_count];
        } else {
            i++;
        }
    }
    mutexUnlock(&render.image_lock);
}

/* --- Glyph atlas ---------------------------------------------------------- */

static void markAtlasDirty(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (render.dirty_x0 >= render.dirty_x1) {
        render.dirty_x0 = x;
        render.dirty_y0 = y;
        render.dirty_x1 = x + width;
        render.dirty_y1 = y + height;
        return;
    }
    if (x < render.dirty_x0) render.dirty_x0 = x;
    if (y < render.dirty_y0) render.dirty_y0 = y;
    if (x + width > render.dirty_x1) render.dirty_x1 = x + width;
    if (y + height > render.dirty_y1) render.dirty_y1 = y + height;
}

/* Clears the atlas except for the 2x2 white block used by solid quads. */
static void resetAtlas(void) {
    memset(render.atlas, 0, (size_t)ATLAS_SIZE * ATLAS_SIZE);
    render.atlas[0] = render.atlas[1] = 0xff;
    render.atlas[ATLAS_SIZE] = render.atlas[ATLAS_SIZE + 1] = 0xff;
    render.cursor_x = 2 + ATLAS_PADDING;
    render.cursor_y = 0;
    render.row_height = 2;
    render.atlas_full = false;
    render.atlas_generation++;
    render.dirty_x0 = render.dirty_x1 = 0;
    markAtlasDirty(0, 0, ATLAS_SIZE, ATLAS_SIZE);
}

static bool ensureAtlas(void) {
    if (render.atlas)
        return true;
    render.atlas = malloc((size_t)ATLAS_SIZE * ATLAS_SIZE);
    if (!render.atlas)
        return false;
    resetAtlas();
    return true;
}

bool renderGlyphAtlasAdd(uint32_t width, uint32_t height, const uint8_t *bitmap, int pitch,
                         uint16_t *atlas_x, uint16_t *atlas_y) {
    if (!ensureAtlas() || render.atlas_full || width > ATLAS_SIZE || height > ATLAS_SIZE)
        return false;
    if (render.cursor_x + width > ATLAS_SIZE) {
        render.cursor_x = 0;
        render.cursor_y += render.row_height + ATLAS_PADDING;
        render.row_height = 0;
    }
    if (render.cursor_y + height > ATLAS_SIZE) {
        render.atlas_full = true;
        return false;
    }
    const uint32_t x = render.cursor_x;
    const uint32_t y = render.cursor_y;
    for (uint32_t row = 0; row < height; row++)
        memcpy(&render.atlas[(size_t)(y + row) * ATLAS_SIZE + x],
               bitmap + (ptrdiff_t)row * pitch, width);
    render.cursor_x += width + ATLAS_PADDING;
    if (height > render.row_height)
        render.row_height = height;
    markAtlasDirty(x, y, width, height);
    *atlas_x = (uint16_t)x;
    *atlas_y = (uint16_t)y;
    return true;
}

uint32_t renderGlyphAtlasGeneration(void) {
    return render.atlas_generation;
}

void renderGlyph(int x, int y, uint32_t width, uint32_t height,
                 uint16_t atlas_x, uint16_t atlas_y, uint32_t color) {
    closeOpenRun();
    const float texel = 1.0f / ATLAS_SIZE;
    pushTexturedQuad((float)x, (float)y, (float)(x + (int)width), (float)(y + (int)height),
                     atlas_x * texel, atlas_y * texel,
                     (atlas_x + width) * texel, (atlas_y + height) * texel,
                     color, render.atlas_texture);
}

static void uploadAtlas(void) {
    if (render.atlas_texture == RenderTexture_None || render.dirty_x0 >= render.dirty_x1)
        return;
    const uint32_t width = render.dirty_x1 - render.dirty_x0;
    const uint32_t height = render.dirty_y1 - render.dirty_y0;
    uint8_t *texels = renderBackendUpload(render.atlas_texture, render.dirty_x0,
                                          render.dirty_y0, width, height);
    if (!texels)
        return;
    for (uint32_t row = 0; row < height; row++)
        memcpy(texels + (size_t)row * width,
               &render.atlas[(size_t)(render.dirty_y0 + row) * ATLAS_SIZE + render.dirty_x0],
               width);
    render.dirty_x0 = render.dirty_x1 = 0;
}

/* --- Frame ---------------------------------------------------------------- */

static void createShapeTextures(void) {
    const uint32_t sz = 128;
    render.circle_texture = renderBackendCreateTexture(sz, sz, RenderTextureFormat_Rgba8,
                                                       RenderTextureFilter_Linear);
    render.ring_texture = renderBackendCreateTexture(256, 256, RenderTextureFormat_Rgba8,
                                                     RenderTextureFilter_Linear);
    render.circle_ring_texture = renderBackendCreateTexture(128, 128, RenderTextureFormat_Rgba8,
                                                            RenderTextureFilter_Linear);
    render.shadow_texture = renderBackendCreateTexture(sz, sz, RenderTextureFormat_Rgba8,
                                                       RenderTextureFilter_Linear);
    render.star_texture = renderBackendCreateTexture(sz, sz, RenderTextureFormat_Rgba8,
                                                     RenderTextureFilter_Linear);
    render.netloader_texture = renderBackendCreateTexture(sz, sz, RenderTextureFormat_Rgba8,
                                                          RenderTextureFilter_Linear);
    render.gear_texture = renderBackendCreateTexture(sz, sz, RenderTextureFormat_Rgba8,
                                                     RenderTextureFilter_Linear);
    render.shapes_uploaded = false;
}

static float sdSegment(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float h = (pax * bax + pay * bay) / (bax * bax + bay * bay);
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    float dx = pax - bax * h;
    float dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy);
}

/* Exact 5-point star signed distance (negative inside, positive outside, point up) */
static float sdStar5(float px, float py, float r, float rf) {
    const float k1x =  0.809016994f, k1y = -0.587785252f;
    const float k2x = -0.809016994f, k2y = -0.587785252f;
    px = fabsf(px);
    float d1 = 2.0f * fmaxf(k1x * px + k1y * py, 0.0f);
    px -= d1 * k1x; py -= d1 * k1y;
    float d2 = 2.0f * fmaxf(k2x * px + k2y * py, 0.0f);
    px -= d2 * k2x; py -= d2 * k2y;
    px = fabsf(px);
    py -= r;
    const float bax = rf * (-k1y);
    const float bay = rf * ( k1x) - 1.0f;
    float h = (px * bax + py * bay) / (bax * bax + bay * bay);
    if (h < 0.0f) h = 0.0f;
    if (h > r) h = r;
    float dx = px - bax * h;
    float dy = py - bay * h;
    float dist = sqrtf(dx * dx + dy * dy);
    float sgn = (px * bay - py * bax > 0.0f) ? -1.0f : 1.0f;
    return dist * sgn;
}

static void uploadShapeTextures(void) {
    if (render.shapes_uploaded)
        return;
    const uint32_t sz = 128;
    const float half = sz * 0.5f;
    const float outer_r = half - 2.0f;
    bool all_ok = true;

    /* Filled SDF circle (powers circles and 9-slice rounded rectangles) */
    if (render.circle_texture != RenderTexture_None) {
        uint8_t *texels = renderBackendUpload(render.circle_texture, 0, 0, sz, sz);
        if (texels) {
            for (uint32_t y = 0; y < sz; y++) {
                float dy = (y + 0.5f) - half;
                for (uint32_t x = 0; x < sz; x++) {
                    float dx = (x + 0.5f) - half;
                    float dist = sqrtf(dx * dx + dy * dy) - outer_r;
                    float cov = 0.5f - dist * 0.8f;
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                    uint8_t *p = &texels[((size_t)y * sz + x) * 4];
                    p[0] = p[1] = p[2] = 0xff;
                    p[3] = (uint8_t)(cov * 255.0f + 0.5f);
                }
            }
        } else {
            all_ok = false;
        }
    }

    /* Animated iridescent focus rings are uploaded dynamically per frame when drawn,
     * initialize them once here so they are always in SHADER_READ_ONLY_OPTIMAL layout. */
    uploadAnimatedRing(render.ring_texture, 256, render.rect_angle_lut, render.rect_ring_alpha, 0.0f);
    uploadAnimatedRing(render.circle_ring_texture, 128, render.circ_angle_lut, render.circle_ring_alpha, 0.0f);

    /* Soft Gaussian-like drop shadow (powers 9-slice soft elevation shadows) */
    if (render.shadow_texture != RenderTexture_None) {
        uint8_t *texels = renderBackendUpload(render.shadow_texture, 0, 0, sz, sz);
        if (texels) {
            for (uint32_t y = 0; y < sz; y++) {
                float dy = (y + 0.5f) - half;
                for (uint32_t x = 0; x < sz; x++) {
                    float dx = (x + 0.5f) - half;
                    float t = sqrtf(dx * dx + dy * dy) / outer_r;
                    if (t > 1.0f) t = 1.0f;
                    float a = (1.0f - t);
                    a = a * a * (3.0f - 2.0f * a); /* smoothstep */
                    uint8_t *p = &texels[((size_t)y * sz + x) * 4];
                    p[0] = p[1] = p[2] = 0xff;
                    p[3] = (uint8_t)(a * 255.0f + 0.5f);
                }
            }
        } else {
            all_ok = false;
        }
    }

    /* Crisp anti-aliased 5-point Star (centered on origin, slightly rounded tips) */
    if (render.star_texture != RenderTexture_None) {
        uint8_t *texels = renderBackendUpload(render.star_texture, 0, 0, sz, sz);
        if (texels) {
            const float star_r = half - 6.0f;
            for (uint32_t y = 0; y < sz; y++) {
                /* Flip Y so the top star point points upward in screen space */
                float py = half - (y + 0.5f);
                for (uint32_t x = 0; x < sz; x++) {
                    float px = (x + 0.5f) - half;
                    float dist = sdStar5(px, py, star_r, 0.44f) - 2.0f;
                    float cov = 0.5f - dist * 0.75f;
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                    uint8_t *p = &texels[((size_t)y * sz + x) * 4];
                    p[0] = p[1] = p[2] = 0xff;
                    p[3] = (uint8_t)(cov * 255.0f + 0.5f);
                }
            }
        } else {
            all_ok = false;
        }
    }

    /* Crisp Switch 2 style Download / Netloader icon (down arrow + U tray) */
    if (render.netloader_texture != RenderTexture_None) {
        uint8_t *texels = renderBackendUpload(render.netloader_texture, 0, 0, sz, sz);
        if (texels) {
            const float stroke = 5.5f;
            for (uint32_t y = 0; y < sz; y++) {
                float py = (y + 0.5f) - half;
                for (uint32_t x = 0; x < sz; x++) {
                    float px = (x + 0.5f) - half;
                    float d = sdSegment(px, py, 0.0f, -34.0f, 0.0f, 12.0f);
                    float d2 = sdSegment(px, py, -20.0f, -8.0f, 0.0f, 12.0f);
                    if (d2 < d) d = d2;
                    d2 = sdSegment(px, py, 20.0f, -8.0f, 0.0f, 12.0f);
                    if (d2 < d) d = d2;
                    /* U-shaped tray underneath */
                    d2 = sdSegment(px, py, -34.0f, 14.0f, -34.0f, 32.0f);
                    if (d2 < d) d = d2;
                    d2 = sdSegment(px, py, 34.0f, 14.0f, 34.0f, 32.0f);
                    if (d2 < d) d = d2;
                    d2 = sdSegment(px, py, -34.0f, 32.0f, 34.0f, 32.0f);
                    if (d2 < d) d = d2;
                    float cov = 0.5f - (d - stroke) * 0.8f;
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                    uint8_t *p = &texels[((size_t)y * sz + x) * 4];
                    p[0] = p[1] = p[2] = 0xff;
                    p[3] = (uint8_t)(cov * 255.0f + 0.5f);
                }
            }
        } else {
            all_ok = false;
        }
    }

    /* Crisp Switch 2 style 8-tooth Settings Gear icon */
    if (render.gear_texture != RenderTexture_None) {
        uint8_t *texels = renderBackendUpload(render.gear_texture, 0, 0, sz, sz);
        if (texels) {
            for (uint32_t y = 0; y < sz; y++) {
                float py = (y + 0.5f) - half;
                for (uint32_t x = 0; x < sz; x++) {
                    float px = (x + 0.5f) - half;
                    float r = sqrtf(px * px + py * py);
                    float a = atan2f(py, px);
                    float tooth = cosf(a * 8.0f) * 2.4f;
                    if (tooth > 1.0f) tooth = 1.0f;
                    if (tooth < -1.0f) tooth = -1.0f;
                    float r_out = 36.5f + 7.5f * tooth;
                    float r_in = 16.5f;
                    float cov_out = 0.5f - (r - r_out) * 0.75f;
                    float cov_in = 0.5f + (r - r_in) * 0.75f;
                    float cov = cov_out < cov_in ? cov_out : cov_in;
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                    uint8_t *p = &texels[((size_t)y * sz + x) * 4];
                    p[0] = p[1] = p[2] = 0xff;
                    p[3] = (uint8_t)(cov * 255.0f + 0.5f);
                }
            }
        } else {
            all_ok = false;
        }
    }

    render.shapes_uploaded = all_ok;
}

void renderInit(void) {
    mutexInit(&render.image_lock);
    if (!ensureAtlas())
        return;
    render.atlas_texture = renderBackendCreateTexture(ATLAS_SIZE, ATLAS_SIZE,
                                                      RenderTextureFormat_Alpha8,
                                                      RenderTextureFilter_Nearest);
    render.gradient_texture = renderBackendCreateTexture(1, GRADIENT_ROWS,
                                                         RenderTextureFormat_Rgba8,
                                                         RenderTextureFilter_Nearest);
    render.gradient_uploaded = false;
    createShapeTextures();
    markAtlasDirty(0, 0, ATLAS_SIZE, ATLAS_SIZE);
}

void renderShutdown(void) {
    mutexLock(&render.image_lock);
    for (uint32_t i = 0; i < render.image_count; i++)
        renderBackendReleaseTexture(render.images[i].texture);
    render.image_count = 0;
    render.vector_count = 0;
    for (uint32_t i = 0; i < render.owned_vector_count; i++)
        nsvgDelete(render.owned_vectors[i]);
    render.owned_vector_count = 0;
    if (render.rasterizer)
        nsvgDeleteRasterizer(render.rasterizer);
    render.rasterizer = NULL;
    mutexUnlock(&render.image_lock);
    renderBackendReleaseTexture(render.atlas_texture);
    renderBackendReleaseTexture(render.gradient_texture);
    renderBackendReleaseTexture(render.circle_texture);
    renderBackendReleaseTexture(render.ring_texture);
    renderBackendReleaseTexture(render.circle_ring_texture);
    renderBackendReleaseTexture(render.shadow_texture);
    renderBackendReleaseTexture(render.star_texture);
    renderBackendReleaseTexture(render.netloader_texture);
    renderBackendReleaseTexture(render.gear_texture);
    render.atlas_texture = render.gradient_texture = RenderTexture_None;
    render.circle_texture = render.ring_texture = render.circle_ring_texture = render.shadow_texture = render.star_texture = render.netloader_texture = render.gear_texture = RenderTexture_None;
    free(render.atlas);
    render.atlas = NULL;
    render.atlas_generation++;
    free(render.quads);
    render.quads = NULL;
    render.count = render.capacity = 0;
    render_open_run.y = -1;
}

void renderSetScale(uint32_t num, uint32_t den) {
    if (!num || !den || (num == hbmenu_scale_num && den == hbmenu_scale_den))
        return;
    hbmenu_scale_num = num;
    hbmenu_scale_den = den;
    render.scale = (float)num / (float)den;
    if (render.atlas)
        resetAtlas();
    releaseVectorTextures();
}

void renderFrameBegin(void) {
    render.count = 0;
    render_open_run.y = -1;
    memset(render.open_rects, 0, sizeof(render.open_rects));
    if (render.atlas_full)
        resetAtlas();
    if (!render.shapes_uploaded)
        uploadShapeTextures();
}

const struct RenderQuad *renderFrameFinish(uint32_t *count) {
    closeOpenRun();
    uploadAtlas();
    *count = render.count;
    return render.quads;
}

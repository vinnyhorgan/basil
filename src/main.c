#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb/stb_image.h"

#define BASIL_VERSION "0.1.0"
#define MAX_PATH_LENGTH 256

#define VM_ABORT(vm, error)              \
    do                                   \
    {                                    \
        wrenSetSlotString(vm, 0, error); \
        wrenAbortFiber(vm, 0);           \
    } while (false);

#define ASSERT_SLOT_TYPE(vm, slot, type, fieldName)    \
    if (wrenGetSlotType(vm, slot) != WREN_TYPE_##type) \
    {                                                  \
        VM_ABORT(vm, #fieldName " was not " #type);    \
        return;                                        \
    }

#define COLOR_ARGB(alpha, red, green, blue) (uint32_t)(((uint8_t)(alpha) << 24) | ((uint8_t)(red) << 16) | ((uint8_t)(green) << 8) | (uint8_t)(blue))
#define COLOR_A(color) ((uint8_t)(color >> 24))
#define COLOR_R(color) ((uint8_t)(color >> 16))
#define COLOR_G(color) ((uint8_t)(color >> 8))
#define COLOR_B(color) ((uint8_t)(color))

#include "api.wren.inc"

typedef struct
{
    uint32_t *data;
    int width, height;
} Image;

typedef struct
{
    uint64_t start;
    uint64_t lastTick;
    double delta;
} Timer;

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen;
    int screenWidth, screenHeight;
    bool closed;
    SDL_Scancode keysHeld[SDL_NUM_SCANCODES];
    SDL_Scancode keysPressed[SDL_NUM_SCANCODES];
    bool mouseHeld[5];
    bool mousePressed[5];
    int mouseX, mouseY;
} Window;

static Window *window = NULL;

static int argCount;
static char **args;
static int exitCode = 0;
static char basePath[MAX_PATH_LENGTH];

static void imageAllocate(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotNewForeign(vm, 0, 0, sizeof(Image));
}

static void imageFinalize(void *data)
{
    Image *image = (Image *)data;

    if (image->data == NULL)
        return;

    free(image->data);
    image->data = NULL;
}

static void imageCreate(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "height");

    int width = (int)wrenGetSlotDouble(vm, 1);
    int height = (int)wrenGetSlotDouble(vm, 2);

    if (width <= 0 || height <= 0)
    {
        VM_ABORT(vm, "Image dimensions must be positive");
        return;
    }

    image->data = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (image->data == NULL)
    {
        VM_ABORT(vm, "Error allocating image");
        return;
    }

    image->width = width;
    image->height = height;
}

static void imageCreateImage(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, STRING, "path");

    char *path = (char *)wrenGetSlotString(vm, 1);

    char fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s", basePath, path);

    image->data = (uint32_t *)stbi_load(fullPath, &image->width, &image->height, NULL, 4);
    if (image->data == NULL)
    {
        VM_ABORT(vm, "Error loading image");
        return;
    }

    uint8_t *bytes = (uint8_t *)image->data;
    int32_t n = image->width * image->height * sizeof(uint32_t);
    for (int32_t i = 0; i < n; i += 4)
    {
        uint8_t b = bytes[i];
        bytes[i] = bytes[i + 2];
        bytes[i + 2] = b;
    }
}

static void imageSet(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 3);

    if (x < 0 || x >= image->width || y < 0 || y >= image->height)
    {
        VM_ABORT(vm, "Image index out of bounds");
        return;
    }

    image->data[x + y * image->width] = color;
}

static void imageGet(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);

    if (x < 0 || x >= image->width || y < 0 || y >= image->height)
    {
        VM_ABORT(vm, "Image index out of bounds");
        return;
    }

    wrenSetSlotDouble(vm, 0, image->data[x + y * image->width]);
}

static void imageClear(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "color");

    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 1);

    uint32_t *pixels = image->data;
    for (int32_t i = 0, n = image->width * image->height; i < n; i++)
        pixels[i] = color;
}

static void imageRect(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "height");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "color");

    int x1 = (int)wrenGetSlotDouble(vm, 1);
    int y1 = (int)wrenGetSlotDouble(vm, 2);
    int width = (int)wrenGetSlotDouble(vm, 3);
    int height = (int)wrenGetSlotDouble(vm, 4);
    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 5);

    if (width <= 0 || height <= 0)
    {
        VM_ABORT(vm, "Rect dimensions must be positive");
        return;
    }

    int x2 = x1 + width - 1;
    int y2 = y1 + height - 1;

    if (x1 >= image->width || x2 < 0 || y1 >= image->height || y2 < 0)
    {
        VM_ABORT(vm, "Rect out of bounds");
        return;
    }

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 >= image->width)
        x2 = image->width - 1;
    if (y2 >= image->height)
        y2 = image->height - 1;

    int clipped_width = x2 - x1 + 1;
    int next_row = image->width - clipped_width;
    uint32_t *pixel = image->data + y1 * image->width + x1;

    for (int y = y1; y <= y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
            *pixel++ = color;

        pixel += next_row;
    }
}

static void imageBlit(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);

    int dst_x1 = x;
    int dst_y1 = y;
    int dst_x2 = x + other->width - 1;
    int dst_y2 = y + other->height - 1;
    int src_x1 = 0;
    int src_y1 = 0;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (y = dst_y1; y <= dst_y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
            *dst_pixel++ = *src_pixel++;

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageBlitAlpha(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);

    int dst_x1 = x;
    int dst_y1 = y;
    int dst_x2 = x + other->width - 1;
    int dst_y2 = y + other->height - 1;
    int src_x1 = 0;
    int src_y1 = 0;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (y = dst_y1; y <= dst_y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
        {
            uint32_t src_color = *src_pixel;
            uint32_t dst_color = *dst_pixel;

            // Extract alpha channel
            uint8_t src_alpha = (src_color >> 24) & 0xFF;
            uint8_t dst_alpha = (dst_color >> 24) & 0xFF;

            // Alpha blending calculation
            uint8_t blended_alpha = src_alpha + (dst_alpha * (255 - src_alpha) / 255);
            uint8_t blended_red = ((src_color >> 16) & 0xFF) * src_alpha / 255 + ((dst_color >> 16) & 0xFF) * dst_alpha * (255 - src_alpha) / (255 * 255);
            uint8_t blended_green = ((src_color >> 8) & 0xFF) * src_alpha / 255 + ((dst_color >> 8) & 0xFF) * dst_alpha * (255 - src_alpha) / (255 * 255);
            uint8_t blended_blue = (src_color & 0xFF) * src_alpha / 255 + (dst_color & 0xFF) * dst_alpha * (255 - src_alpha) / (255 * 255);

            // Pack ARGB components into a single 32-bit pixel
            *dst_pixel = (blended_alpha << 24) | (blended_red << 16) | (blended_green << 8) | blended_blue;

            src_pixel++;
            dst_pixel++;
        }

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageBlitRect(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dstX");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dstY");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "srcX");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "srcY");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "srcWidth");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "srcHeight");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int dst_x = (int)wrenGetSlotDouble(vm, 2);
    int dst_y = (int)wrenGetSlotDouble(vm, 3);
    int src_x = (int)wrenGetSlotDouble(vm, 4);
    int src_y = (int)wrenGetSlotDouble(vm, 5);
    int src_width = (int)wrenGetSlotDouble(vm, 6);
    int src_height = (int)wrenGetSlotDouble(vm, 7);

    if ((src_x + src_width - 1 > other->width) || (src_y + src_height - 1 > other->height))
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    int dst_x1 = dst_x;
    int dst_y1 = dst_y;
    int dst_x2 = dst_x + src_width - 1;
    int dst_y2 = dst_y + src_height - 1;
    int src_x1 = src_x;
    int src_y1 = src_y;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (int y = dst_y1; y <= dst_y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
            *dst_pixel++ = *src_pixel++;

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageBlitKey(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "key");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);
    uint32_t key = (uint32_t)wrenGetSlotDouble(vm, 4);

    int dst_x1 = x;
    int dst_y1 = y;
    int dst_x2 = x + other->width - 1;
    int dst_y2 = y + other->height - 1;
    int src_x1 = 0;
    int src_y1 = 0;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (y = dst_y1; y <= dst_y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
        {
            uint32_t src_color = *src_pixel;
            uint32_t dst_color = *dst_pixel;
            *dst_pixel = src_color != key ? src_color : dst_color;
            src_pixel++;
            dst_pixel++;
        }

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageBlitRectKey(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dstX");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dstY");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "srcX");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "srcY");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "srcWidth");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "srcHeight");
    ASSERT_SLOT_TYPE(vm, 8, NUM, "key");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int dst_x = (int)wrenGetSlotDouble(vm, 2);
    int dst_y = (int)wrenGetSlotDouble(vm, 3);
    int src_x = (int)wrenGetSlotDouble(vm, 4);
    int src_y = (int)wrenGetSlotDouble(vm, 5);
    int src_width = (int)wrenGetSlotDouble(vm, 6);
    int src_height = (int)wrenGetSlotDouble(vm, 7);
    uint32_t key = (uint32_t)wrenGetSlotDouble(vm, 8);

    if ((src_x + src_width - 1 > other->width) || (src_y + src_height - 1 > other->height))
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    int dst_x1 = dst_x;
    int dst_y1 = dst_y;
    int dst_x2 = dst_x + src_width - 1;
    int dst_y2 = dst_y + src_height - 1;
    int src_x1 = src_x;
    int src_y1 = src_y;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (dst_y = dst_y1; dst_y <= dst_y2; dst_y++)
    {
        for (int i = 0; i < clipped_width; i++)
        {
            uint32_t src_color = *src_pixel;
            uint32_t dst_color = *dst_pixel;
            *dst_pixel = src_color != key ? src_color : dst_color;
            src_pixel++;
            dst_pixel++;
        }

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageBlitRectKeyTint(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dstX");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dstY");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "srcX");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "srcY");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "srcWidth");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "srcHeight");
    ASSERT_SLOT_TYPE(vm, 8, NUM, "key");
    ASSERT_SLOT_TYPE(vm, 9, NUM, "tint");

    Image *other = (Image *)wrenGetSlotForeign(vm, 1);
    int dst_x = (int)wrenGetSlotDouble(vm, 2);
    int dst_y = (int)wrenGetSlotDouble(vm, 3);
    int src_x = (int)wrenGetSlotDouble(vm, 4);
    int src_y = (int)wrenGetSlotDouble(vm, 5);
    int src_width = (int)wrenGetSlotDouble(vm, 6);
    int src_height = (int)wrenGetSlotDouble(vm, 7);
    uint32_t key = (uint32_t)wrenGetSlotDouble(vm, 8);
    uint32_t tint = (uint32_t)wrenGetSlotDouble(vm, 9);

    if ((src_x + src_width - 1 > other->width) || (src_y + src_height - 1 > other->height))
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    int dst_x1 = dst_x;
    int dst_y1 = dst_y;
    int dst_x2 = dst_x + src_width - 1;
    int dst_y2 = dst_y + src_height - 1;
    int src_x1 = src_x;
    int src_y1 = src_y;

    if (dst_x1 >= image->width || dst_x2 < 0 || dst_y1 >= image->height || dst_y2 < 0)
    {
        VM_ABORT(vm, "Blit out of bounds");
        return;
    }

    if (dst_x1 < 0)
    {
        src_x1 -= dst_x1;
        dst_x1 = 0;
    }
    if (dst_y1 < 0)
    {
        src_y1 -= dst_y1;
        dst_y1 = 0;
    }
    if (dst_x2 >= image->width)
        dst_x2 = image->width - 1;
    if (dst_y2 >= image->height)
        dst_y2 = image->height - 1;

    uint32_t tint_r = COLOR_R(tint);
    uint32_t tint_g = COLOR_G(tint);
    uint32_t tint_b = COLOR_B(tint);

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = image->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = image->data + dst_y1 * image->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (dst_y = dst_y1; dst_y <= dst_y2; dst_y++)
    {
        for (int i = 0; i < clipped_width; i++)
        {
            uint32_t src_color = *src_pixel;
            uint32_t dst_color = *dst_pixel;
            *dst_pixel = src_color != key ? COLOR_ARGB(
                                                COLOR_A(src_color),
                                                ((COLOR_R(src_color) * tint_r) >> 8) & 0xff,
                                                ((COLOR_G(src_color) * tint_g) >> 8) & 0xff,
                                                ((COLOR_B(src_color) * tint_b) >> 8) & 0xff)
                                          : dst_color;
            src_pixel++;
            dst_pixel++;
        }

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void imageWidth(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, image->width);
}

static void imageHeight(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, image->height);
}

static void osName(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, SDL_GetPlatform());
}

static void osBasilVersion(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, BASIL_VERSION);
}

static void osArgs(WrenVM *vm)
{
    wrenEnsureSlots(vm, 2);
    wrenSetSlotNewList(vm, 0);
    for (int i = 0; i < argCount; i++)
    {
        wrenSetSlotString(vm, 1, args[i]);
        wrenInsertInList(vm, 0, i, 1);
    }
}

static void osExit(WrenVM *vm)
{
    ASSERT_SLOT_TYPE(vm, 1, NUM, "code");
    exitCode = (int)wrenGetSlotDouble(vm, 1);
}

static void timerAllocate(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    Timer *timer = (Timer *)wrenSetSlotNewForeign(vm, 0, 0, sizeof(Timer));

    timer->start = SDL_GetPerformanceCounter();
    timer->lastTick = SDL_GetPerformanceCounter();
    timer->delta = 0;
}

static void timerTick(WrenVM *vm)
{
    Timer *timer = (Timer *)wrenGetSlotForeign(vm, 0);

    uint64_t now = SDL_GetPerformanceCounter();
    timer->delta = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();
    timer->lastTick = now;
}

static void timerTickFramerate(WrenVM *vm)
{
    Timer *timer = (Timer *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "framerate");

    int framerate = (int)wrenGetSlotDouble(vm, 1);
    double targetFrameTime = 1.0 / framerate;

    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();

    if (elapsed < targetFrameTime)
    {
        double delayTime = targetFrameTime - elapsed;
        uint32_t delayMS = (uint32_t)(delayTime * 1000.0);
        SDL_Delay(delayMS);

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();
    }

    timer->delta = elapsed;
    timer->lastTick = now;
}

static void timerTime(WrenVM *vm)
{
    Timer *timer = (Timer *)wrenGetSlotForeign(vm, 0);

    float passed = (SDL_GetPerformanceCounter() - timer->start) / (float)SDL_GetPerformanceFrequency();

    wrenSetSlotDouble(vm, 0, passed);
}

static void timerDelta(WrenVM *vm)
{
    Timer *timer = (Timer *)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, timer->delta);
}

static void windowInit(WrenVM *vm)
{
    if (window != NULL)
    {
        VM_ABORT(vm, "Window already initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "title");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "height");

    const char *title = wrenGetSlotString(vm, 1);
    int width = (int)wrenGetSlotDouble(vm, 2);
    int height = (int)wrenGetSlotDouble(vm, 3);

    if (width <= 0 || height <= 0)
    {
        VM_ABORT(vm, "Window dimensions must be positive");
        return;
    }

    window = (Window *)malloc(sizeof(Window));

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        VM_ABORT(vm, "Error initializing SDL");
        return;
    }

    window->window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_RESIZABLE);
    if (window->window == NULL)
    {
        VM_ABORT(vm, "Error creating window");
        return;
    }

    window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED);
    if (window->renderer == NULL)
    {
        wrenSetSlotString(vm, 0, "Error creating renderer");
        return;
    }

    window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (window->screen == NULL)
    {
        VM_ABORT(vm, "Error creating screen texture");
        return;
    }

    window->screenWidth = width;
    window->screenHeight = height;
    window->closed = false;

    SDL_SetWindowMinimumSize(window->window, width, height);
    SDL_RenderSetLogicalSize(window->renderer, width, height);
}

static void windowQuit(WrenVM *vm)
{
    if (window == NULL)
        return;

    if (window->screen != NULL)
    {
        SDL_DestroyTexture(window->screen);
        window->screen = NULL;
    }

    if (window->renderer != NULL)
    {
        SDL_DestroyRenderer(window->renderer);
        window->renderer = NULL;
    }

    if (window->window != NULL)
    {
        SDL_DestroyWindow(window->window);
        window->window = NULL;
    }

    free(window);

    SDL_Quit();
}

static void windowUpdate(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");

    Image *image = (Image *)wrenGetSlotForeign(vm, 1);

    if (image->width != window->screenWidth || image->height != window->screenHeight)
    {
        SDL_DestroyTexture(window->screen);

        window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, image->width, image->height);
        if (window->screen == NULL)
        {
            VM_ABORT(vm, "Error creating screen texture");
            return;
        }

        window->screenWidth = image->width;
        window->screenHeight = image->height;

        SDL_RenderSetLogicalSize(window->renderer, image->width, image->height);
    }

    SDL_UpdateTexture(window->screen, NULL, image->data, image->width * 4);

    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->screen, NULL, NULL);
    SDL_RenderPresent(window->renderer);

    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
        window->keysPressed[i] = false;

    for (int i = 0; i < 5; i++)
        window->mousePressed[i] = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
        {
            window->closed = true;
        }
        else if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
        {
            window->keysHeld[event.key.keysym.scancode] = true;
            window->keysPressed[event.key.keysym.scancode] = true;
        }
        else if (event.type == SDL_KEYUP)
        {
            window->keysHeld[event.key.keysym.scancode] = false;
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN)
        {
            window->mouseHeld[event.button.button] = true;
            window->mousePressed[event.button.button] = true;
        }
        else if (event.type == SDL_MOUSEBUTTONUP)
        {
            window->mouseHeld[event.button.button] = false;
        }
        else if (event.type == SDL_MOUSEMOTION)
        {
            window->mouseX = event.motion.x;
            window->mouseY = event.motion.y;
        }
    }
}

static void windowKeyHeld(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "key");

    const char *key = wrenGetSlotString(vm, 1);
    SDL_KeyCode sdlKey = SDL_GetScancodeFromName(key);

    wrenSetSlotBool(vm, 0, window->keysHeld[sdlKey]);
}

static void windowKeyPressed(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "key");

    const char *key = wrenGetSlotString(vm, 1);
    SDL_KeyCode sdlKey = SDL_GetScancodeFromName(key);

    wrenSetSlotBool(vm, 0, window->keysPressed[sdlKey]);
}

static void windowMouseHeld(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, NUM, "button");

    int button = (int)wrenGetSlotDouble(vm, 1);

    wrenSetSlotBool(vm, 0, window->mouseHeld[button]);
}

static void windowMousePressed(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, NUM, "button");

    int button = (int)wrenGetSlotDouble(vm, 1);

    wrenSetSlotBool(vm, 0, window->mousePressed[button]);
}

static void windowWidth(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);

    int width;
    SDL_GetWindowSize(window->window, &width, NULL);

    wrenSetSlotDouble(vm, 0, width);
}

static void windowHeight(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);

    int height;
    SDL_GetWindowSize(window->window, NULL, &height);

    wrenSetSlotDouble(vm, 0, height);
}

static void windowTitle(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, SDL_GetWindowTitle(window->window));
}

static void windowClosed(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotBool(vm, 0, window->closed);
}

static void windowMouseX(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotDouble(vm, 0, window->mouseX);
}

static void windowMouseY(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotDouble(vm, 0, window->mouseY);
}

static char *readFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        printf("Error opening file: %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(fileSize + 1);
    if (buffer == NULL)
    {
        printf("Error allocating memory for file: %s\n", path);
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, fileSize, file) != fileSize)
    {
        printf("Error reading file: %s\n", path);
        fclose(file);
        free(buffer);
        return NULL;
    }

    buffer[fileSize] = '\0';

    fclose(file);

    return buffer;
}

static void onComplete(WrenVM *vm, const char *name, WrenLoadModuleResult result)
{
    if (result.source)
        free((void *)result.source);
}

static WrenLoadModuleResult wrenLoadModule(WrenVM *vm, const char *name)
{
    WrenLoadModuleResult result = {0};

    if (strcmp(name, "meta") == 0 || strcmp(name, "random") == 0)
        return result;

    if (strcmp(name, "basil") == 0)
    {
        result.source = apiModuleSource;
        return result;
    }

    char fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s.wren", basePath, name);

    result.source = readFile(fullPath);
    result.onComplete = onComplete;

    return result;
}

static WrenForeignMethodFn wrenBindForeignMethod(WrenVM *vm, const char *module, const char *className, bool isStatic, const char *signature)
{
    if (strcmp(className, "Image") == 0)
    {
        if (strcmp(signature, "init create(_,_)") == 0)
            return imageCreate;
        if (strcmp(signature, "init create(_)") == 0)
            return imageCreateImage;
        if (strcmp(signature, "f_set(_,_,_)") == 0)
            return imageSet;
        if (strcmp(signature, "f_get(_,_)") == 0)
            return imageGet;
        if (strcmp(signature, "f_clear(_)") == 0)
            return imageClear;
        if (strcmp(signature, "f_rect(_,_,_,_,_)") == 0)
            return imageRect;
        if (strcmp(signature, "blit(_,_,_)") == 0)
            return imageBlit;
        if (strcmp(signature, "blitAlpha(_,_,_)") == 0)
            return imageBlitAlpha;
        if (strcmp(signature, "blit(_,_,_,_,_,_,_)") == 0)
            return imageBlitRect;
        if (strcmp(signature, "f_blit(_,_,_,_)") == 0)
            return imageBlitKey;
        if (strcmp(signature, "f_blit(_,_,_,_,_,_,_,_)") == 0)
            return imageBlitRectKey;
        if (strcmp(signature, "f_blit(_,_,_,_,_,_,_,_,_)") == 0)
            return imageBlitRectKeyTint;
        if (strcmp(signature, "width") == 0)
            return imageWidth;
        if (strcmp(signature, "height") == 0)
            return imageHeight;
    }
    else if (strcmp(className, "OS") == 0)
    {
        if (strcmp(signature, "name") == 0)
            return osName;
        if (strcmp(signature, "basilVersion") == 0)
            return osBasilVersion;
        if (strcmp(signature, "args") == 0)
            return osArgs;
        if (strcmp(signature, "f_exit(_)") == 0)
            return osExit;
    }
    else if (strcmp(className, "Timer") == 0)
    {
        if (strcmp(signature, "tick()") == 0)
            return timerTick;
        if (strcmp(signature, "tick(_)") == 0)
            return timerTickFramerate;
        if (strcmp(signature, "time") == 0)
            return timerTime;
        if (strcmp(signature, "delta") == 0)
            return timerDelta;
    }
    else if (strcmp(className, "Window") == 0)
    {
        if (strcmp(signature, "init(_,_,_)") == 0)
            return windowInit;
        if (strcmp(signature, "quit()") == 0)
            return windowQuit;
        if (strcmp(signature, "update(_)") == 0)
            return windowUpdate;
        if (strcmp(signature, "keyHeld(_)") == 0)
            return windowKeyHeld;
        if (strcmp(signature, "keyPressed(_)") == 0)
            return windowKeyPressed;
        if (strcmp(signature, "mouseHeld(_)") == 0)
            return windowMouseHeld;
        if (strcmp(signature, "mousePressed(_)") == 0)
            return windowMousePressed;
        if (strcmp(signature, "width") == 0)
            return windowWidth;
        if (strcmp(signature, "height") == 0)
            return windowHeight;
        if (strcmp(signature, "title") == 0)
            return windowTitle;
        if (strcmp(signature, "closed") == 0)
            return windowClosed;
        if (strcmp(signature, "mouseX") == 0)
            return windowMouseX;
        if (strcmp(signature, "mouseY") == 0)
            return windowMouseY;
    }

    return NULL;
}

static WrenForeignClassMethods wrenBindForeignClass(WrenVM *vm, const char *module, const char *className)
{
    WrenForeignClassMethods methods = {0};

    if (strcmp(className, "Image") == 0)
    {
        methods.allocate = imageAllocate;
        methods.finalize = imageFinalize;
    }
    else if (strcmp(className, "Timer") == 0)
    {
        methods.allocate = timerAllocate;
    }

    return methods;
}

static void wrenWrite(WrenVM *vm, const char *text)
{
    printf("%s", text);
}

static void wrenError(WrenVM *vm, WrenErrorType type, const char *module, int line, const char *message)
{
    switch (type)
    {
    case WREN_ERROR_COMPILE:
        printf("[%s line %d] %s\n", module, line, message);
        break;
    case WREN_ERROR_RUNTIME:
        printf("%s\n", message);
        break;
    case WREN_ERROR_STACK_TRACE:
        printf("[%s line %d] in %s\n", module, line, message);
        break;
    }
}

static const char *strprbrk(const char *s, const char *charset)
{
    const char *latestMatch = NULL;
    for (; s = strpbrk(s, charset), s != NULL; latestMatch = s++)
    {
    }
    return latestMatch;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage:\n");
        printf("\tbasil [file] [arguments...]\n");
        printf("\tbasil version\n");
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "version") == 0)
    {
        printf("basil %s\n", BASIL_VERSION);
        return 0;
    }

    const char *sourcePath = argv[1];

    char *source = readFile(sourcePath);
    if (source == NULL)
        return 1;

    // find base path from raylib

    if (sourcePath[1] != ':' && sourcePath[0] != '\\' && sourcePath[0] != '/')
    {
        basePath[0] = '.';
        basePath[1] = '/';
    }

    const char *lastSlash = strprbrk(sourcePath, "\\/");
    if (lastSlash)
    {
        if (lastSlash == sourcePath)
        {
            basePath[0] = sourcePath[0];
            basePath[1] = '\0';
        }
        else
        {
            char *basePathPtr = basePath;
            if ((sourcePath[1] != ':') && (sourcePath[0] != '\\') && (sourcePath[0] != '/'))
                basePathPtr += 2;
            memcpy(basePathPtr, sourcePath, strlen(sourcePath) - (strlen(lastSlash) - 1));
            basePath[strlen(sourcePath) - strlen(lastSlash) + (((sourcePath[1] != ':') && (sourcePath[0] != '\\') && (sourcePath[0] != '/')) ? 2 : 0)] = '\0';
        }
    }

    argCount = argc;
    args = argv;

    WrenConfiguration config;
    wrenInitConfiguration(&config);

    config.loadModuleFn = wrenLoadModule;
    config.bindForeignMethodFn = wrenBindForeignMethod;
    config.bindForeignClassFn = wrenBindForeignClass;
    config.writeFn = wrenWrite;
    config.errorFn = wrenError;

    WrenVM *vm = wrenNewVM(&config);

    wrenInterpret(vm, sourcePath, source);

    free(source);
    wrenFreeVM(vm);

    return exitCode;
}

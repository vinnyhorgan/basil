#include "api.h"

#include "lib/font/font8x8_basic.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb/stb_image.h"

#include "util.h"

#define VM_ABORT(vm, error)              \
    do {                                 \
        wrenSetSlotString(vm, 0, error); \
        wrenAbortFiber(vm, 0);           \
    } while (false);

#define ASSERT_SLOT_TYPE(vm, slot, type, fieldName)                       \
    if (wrenGetSlotType(vm, slot) != WREN_TYPE_##type) {                  \
        VM_ABORT(vm, "Expected " #fieldName " to be of type " #type "."); \
        return;                                                           \
    }

#define EXPAND(X) ((X) + ((X) > 0))

static int argCount = 0;
static char** args = NULL;
static const char* basePath = NULL;

static Window* window = NULL;
static Image defaultFont[128];
static int exitCode = 0;

static void blitTint(Image* image, Image* src, int dx, int dy, int sx, int sy, int width, int height, Color tint)
{
    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    CLIP();

    int xr = EXPAND(tint.r);
    int xg = EXPAND(tint.g);
    int xb = EXPAND(tint.b);
    int xa = EXPAND(tint.a);

    Color* ts = &src->data[sy * src->width + sx];
    Color* td = &image->data[dy * image->width + dx];
    int st = src->width;
    int dt = image->width;

    do {
        for (int x = 0; x < width; x++) {
            uint32_t r = (xr * ts[x].r) >> 8;
            uint32_t g = (xg * ts[x].g) >> 8;
            uint32_t b = (xb * ts[x].b) >> 8;
            uint32_t a = xa * EXPAND(ts[x].a);
            td[x].r += (uint8_t)((r - td[x].r) * a >> 16);
            td[x].g += (uint8_t)((g - td[x].g) * a >> 16);
            td[x].b += (uint8_t)((b - td[x].b) * a >> 16);
            td[x].a += (uint8_t)((ts[x].a - td[x].a) * a >> 16);
        }

        ts += st;
        td += dt;
    } while (--height);
}

void setArgs(int argc, char** argv)
{
    argCount = argc;
    args = argv;

    basePath = getDirectoryPath(argv[1]);
}

int getExitCode()
{
    return exitCode;
}

void colorAllocate(WrenVM* vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotNewForeign(vm, 0, 0, sizeof(Color));
}

void colorNew(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "r");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "g");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "b");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "a");

    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 2);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 3);
    color->a = (uint8_t)wrenGetSlotDouble(vm, 4);
}

void colorNew2(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "r");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "g");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "b");

    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 2);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 3);
    color->a = 255;
}

void colorNew3(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "num");

    uint32_t num = (uint32_t)wrenGetSlotDouble(vm, 1);

    color->r = (uint8_t)(num >> 16);
    color->g = (uint8_t)(num >> 8);
    color->b = (uint8_t)(num);
    color->a = (uint8_t)(num >> 24);
}

void colorGetR(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->r);
}

void colorGetG(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->g);
}

void colorGetB(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->b);
}

void colorGetA(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->a);
}

void colorSetR(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
}

void colorSetG(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 1);
}

void colorSetB(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 1);
}

void colorSetA(WrenVM* vm)
{
    Color* color = (Color*)wrenGetSlotForeign(vm, 0);
    color->a = (uint8_t)wrenGetSlotDouble(vm, 1);
}

void imageAllocate(WrenVM* vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotNewForeign(vm, 0, 0, sizeof(Image));
}

void imageFinalize(void* data)
{
    Image* image = (Image*)data;

    if (image->data == NULL)
        return;

    free(image->data);
    image->data = NULL;
}

void imageNew(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "height");

    int width = (int)wrenGetSlotDouble(vm, 1);
    int height = (int)wrenGetSlotDouble(vm, 2);

    if (width <= 0 || height <= 0) {
        VM_ABORT(vm, "Image dimensions must be positive.");
        return;
    }

    image->data = (Color*)calloc(width * height, sizeof(Color));
    if (image->data == NULL) {
        VM_ABORT(vm, "Failed to allocate image data.");
        return;
    }

    image->width = width;
    image->height = height;

    image->clipX = 0;
    image->clipY = 0;
    image->clipWidth = -1;
    image->clipHeight = -1;
}

void imageNew2(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    if (wrenGetSlotType(vm, 1) == WREN_TYPE_STRING) {
        const char* path = wrenGetSlotString(vm, 1);

        char fullPath[MAX_PATH_LENGTH];
        snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s", basePath, path);

        image->data = (Color*)stbi_load(fullPath, &image->width, &image->height, NULL, STBI_rgb_alpha);
        if (image->data == NULL) {
            VM_ABORT(vm, "Failed to load image data.");
            return;
        }

        uint8_t* bytes = (uint8_t*)image->data;
        int32_t n = image->width * image->height * sizeof(uint32_t);
        for (int32_t i = 0; i < n; i += 4) {
            uint8_t b = bytes[i];
            bytes[i] = bytes[i + 2];
            bytes[i + 2] = b;
        }

        image->clipX = 0;
        image->clipY = 0;
        image->clipWidth = -1;
        image->clipHeight = -1;
    } else if (wrenGetSlotType(vm, 1) == WREN_TYPE_FOREIGN) {
        Image* toCopy = (Image*)wrenGetSlotForeign(vm, 1);

        image->data = (Color*)calloc(toCopy->width * toCopy->height, sizeof(Color));
        if (image->data == NULL) {
            VM_ABORT(vm, "Failed to allocate image data.");
            return;
        }

        memcpy(image->data, toCopy->data, toCopy->width * toCopy->height * sizeof(Color));

        image->width = toCopy->width;
        image->height = toCopy->height;

        image->clipX = 0;
        image->clipY = 0;
        image->clipWidth = -1;
        image->clipHeight = -1;
    } else {
        VM_ABORT(vm, "Expected \"path\" to be of type STRING or FOREIGN.");
        return;
    }
}

void imageGetWidth(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, image->width);
}

void imageGetHeight(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, image->height);
}

void imageClip(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "height");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    int width = (int)wrenGetSlotDouble(vm, 3);
    int height = (int)wrenGetSlotDouble(vm, 4);

    image->clipX = x;
    image->clipY = y;
    image->clipWidth = width;
    image->clipHeight = height;
}

void imageUnload(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    free(image->data);
    image->data = NULL;
}

void imageGet(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);

    Color color = { 0, 0, 0, 0 };

    if (x >= 0 && y >= 0 && x < image->width && y < image->height)
        color = image->data[y * image->width + x];

    wrenSetSlotDouble(vm, 0, (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b);
}

static void setColor(Image* image, int x, int y, Color color)
{
    int xa, i, a;

    int cx = image->clipX;
    int cy = image->clipY;
    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    if (x >= cx && y >= cy && x < cx + cw && y < cy + ch) {
        xa = EXPAND(color.a);
        a = xa * xa;
        i = y * image->width + x;

        image->data[i].r += (uint8_t)((color.r - image->data[i].r) * a >> 16);
        image->data[i].g += (uint8_t)((color.g - image->data[i].g) * a >> 16);
        image->data[i].b += (uint8_t)((color.b - image->data[i].b) * a >> 16);
        image->data[i].a += (uint8_t)((color.a - image->data[i].a) * a >> 16);
    }
}

void imageSet(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, FOREIGN, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    Color* color = (Color*)wrenGetSlotForeign(vm, 3);

    setColor(image, x, y, *color);
}

void imageClear(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "color");

    Color* color = (Color*)wrenGetSlotForeign(vm, 1);

    int count = image->width * image->height;

    int n;
    for (n = 0; n < count; n++)
        image->data[n] = *color;
}

void imageFill(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "height");
    ASSERT_SLOT_TYPE(vm, 5, FOREIGN, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    int width = (int)wrenGetSlotDouble(vm, 3);
    int height = (int)wrenGetSlotDouble(vm, 4);
    Color* color = (Color*)wrenGetSlotForeign(vm, 5);

    Color* td;
    int dt, i;

    if (x < 0) {
        width += x;
        x = 0;
    }

    if (y < 0) {
        height += y;
        y = 0;
    }

    if (x + width > image->width) {
        width = image->width - x;
    }

    if (y + height > image->height) {
        height = image->height - y;
    }

    if (width <= 0 || height <= 0)
        return;

    td = &image->data[y * image->width + x];
    dt = image->width;

    do {
        for (i = 0; i < width; i++)
            td[i] = *color;

        td += dt;
    } while (--height);
}

static void line(Image* image, int x0, int y0, int x1, int y1, Color color)
{
    int sx, sy, dx, dy, err, e2;

    dx = abs(x1 - x0);
    dy = abs(y1 - y0);

    if (x0 < x1)
        sx = 1;
    else
        sx = -1;

    if (y0 < y1)
        sy = 1;
    else
        sy = -1;

    err = dx - dy;

    do {
        setColor(image, x0, y0, color);

        e2 = 2 * err;

        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    } while (x0 != x1 || y0 != y1);
}

void imageLine(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x0");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y0");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "x1");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "y1");
    ASSERT_SLOT_TYPE(vm, 5, FOREIGN, "color");

    int x0 = (int)wrenGetSlotDouble(vm, 1);
    int y0 = (int)wrenGetSlotDouble(vm, 2);
    int x1 = (int)wrenGetSlotDouble(vm, 3);
    int y1 = (int)wrenGetSlotDouble(vm, 4);
    Color* color = (Color*)wrenGetSlotForeign(vm, 5);

    line(image, x0, y0, x1, y1, *color);
}

void imageRect(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "height");
    ASSERT_SLOT_TYPE(vm, 5, FOREIGN, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    int width = (int)wrenGetSlotDouble(vm, 3);
    int height = (int)wrenGetSlotDouble(vm, 4);
    Color* color = (Color*)wrenGetSlotForeign(vm, 5);

    int x1, y1;

    if (width <= 0 || height <= 0) {
        return;
    }

    if (width == 1) {
        line(image, x, y, x, y + height, *color);
    } else if (height == 1) {
        line(image, x, y, x + width, y, *color);
    } else {
        x1 = x + width - 1;
        y1 = y + height - 1;

        line(image, x, y, x1, y, *color);
        line(image, x1, y, x1, y1, *color);
        line(image, x1, y1, x, y1, *color);
        line(image, x, y1, x, y, *color);
    }
}

void imageBlit(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dx");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dy");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "sx");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "sy");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "height");

    Image* src = (Image*)wrenGetSlotForeign(vm, 1);
    int dx = (int)wrenGetSlotDouble(vm, 2);
    int dy = (int)wrenGetSlotDouble(vm, 3);
    int sx = (int)wrenGetSlotDouble(vm, 4);
    int sy = (int)wrenGetSlotDouble(vm, 5);
    int width = (int)wrenGetSlotDouble(vm, 6);
    int height = (int)wrenGetSlotDouble(vm, 7);

    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    CLIP();

    Color* ts = &src->data[sy * src->width + sx];
    Color* td = &image->data[dy * image->width + dx];
    int st = src->width;
    int dt = image->width;

    do {
        memcpy(td, ts, width * sizeof(Color));
        ts += st;
        td += dt;
    } while (--height);
}

void imageBlitAlpha(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dx");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dy");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "sx");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "sy");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "height");
    ASSERT_SLOT_TYPE(vm, 8, FOREIGN, "tint");

    Image* src = (Image*)wrenGetSlotForeign(vm, 1);
    int dx = (int)wrenGetSlotDouble(vm, 2);
    int dy = (int)wrenGetSlotDouble(vm, 3);
    int sx = (int)wrenGetSlotDouble(vm, 4);
    int sy = (int)wrenGetSlotDouble(vm, 5);
    int width = (int)wrenGetSlotDouble(vm, 6);
    int height = (int)wrenGetSlotDouble(vm, 7);
    Color* tint = (Color*)wrenGetSlotForeign(vm, 8);

    blitTint(image, src, dx, dy, sx, sy, width, height, *tint);
}

void imageText(WrenVM* vm)
{
    Image* image = (Image*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, STRING, "text");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 4, FOREIGN, "color");

    const char* text = wrenGetSlotString(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);
    Color* color = (Color*)wrenGetSlotForeign(vm, 4);

    for (int i = 0; i < strlen(text); i++)
        blitTint(image, &defaultFont[text[i]], x + i * 8, y, 0, 0, 8, 8, *color);
}

void osName(WrenVM* vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, SDL_GetPlatform());
}

void osBasilVersion(WrenVM* vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, BASIL_VERSION);
}

void osArgs(WrenVM* vm)
{
    wrenEnsureSlots(vm, 2);
    wrenSetSlotNewList(vm, 0);
    for (int i = 0; i < argCount; i++) {
        wrenSetSlotString(vm, 1, args[i]);
        wrenInsertInList(vm, 0, i, 1);
    }
}

void osExit(WrenVM* vm)
{
    ASSERT_SLOT_TYPE(vm, 1, NUM, "code");
    exitCode = (int)wrenGetSlotDouble(vm, 1);
}

void timerAllocate(WrenVM* vm)
{
    wrenEnsureSlots(vm, 1);
    Timer* timer = (Timer*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(Timer));

    timer->start = SDL_GetPerformanceCounter();
    timer->lastTick = SDL_GetPerformanceCounter();
    timer->delta = 0;
}

void timerTick(WrenVM* vm)
{
    Timer* timer = (Timer*)wrenGetSlotForeign(vm, 0);

    uint64_t now = SDL_GetPerformanceCounter();
    timer->delta = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();
    timer->lastTick = now;
}

void timerTickFramerate(WrenVM* vm)
{
    Timer* timer = (Timer*)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "framerate");

    int framerate = (int)wrenGetSlotDouble(vm, 1);
    double targetFrameTime = 1.0 / framerate;

    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();

    if (elapsed < targetFrameTime) {
        double delayTime = targetFrameTime - elapsed;
        uint32_t delayMS = (uint32_t)(delayTime * 1000.0);
        SDL_Delay(delayMS);

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - timer->lastTick) / (double)SDL_GetPerformanceFrequency();
    }

    timer->delta = elapsed;
    timer->lastTick = now;
}

void timerTime(WrenVM* vm)
{
    Timer* timer = (Timer*)wrenGetSlotForeign(vm, 0);

    float passed = (SDL_GetPerformanceCounter() - timer->start) / (float)SDL_GetPerformanceFrequency();

    wrenSetSlotDouble(vm, 0, passed);
}

void timerDelta(WrenVM* vm)
{
    Timer* timer = (Timer*)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, timer->delta);
}

void windowInit(WrenVM* vm)
{
    if (window != NULL) {
        VM_ABORT(vm, "Window already initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "title");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "height");

    const char* title = wrenGetSlotString(vm, 1);
    int width = (int)wrenGetSlotDouble(vm, 2);
    int height = (int)wrenGetSlotDouble(vm, 3);

    if (width <= 0 || height <= 0) {
        VM_ABORT(vm, "Window dimensions must be positive");
        return;
    }

    window = (Window*)malloc(sizeof(Window));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        VM_ABORT(vm, "Error initializing SDL");
        return;
    }

    window->window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_RESIZABLE);
    if (window->window == NULL) {
        VM_ABORT(vm, "Error creating window");
        return;
    }

    window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED);
    if (window->renderer == NULL) {
        wrenSetSlotString(vm, 0, "Error creating renderer");
        return;
    }

    window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (window->screen == NULL) {
        VM_ABORT(vm, "Error creating screen texture");
        return;
    }

    window->screenWidth = width;
    window->screenHeight = height;
    window->closed = false;

    SDL_SetWindowMinimumSize(window->window, width, height);
    SDL_RenderSetLogicalSize(window->renderer, width, height);

#ifndef _WIN32
#include "icon.h"
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        (void*)icon_rgba, 64, 64,
        32, 64 * 4,
        0x000000ff,
        0x0000ff00,
        0x00ff0000,
        0xff000000);
    SDL_SetWindowIcon(window->window, surf);
    SDL_FreeSurface(surf);
#endif

    for (int c = 0; c < 128; c++) {
        char* bitmap = font8x8_basic[c];

        defaultFont[c].data = (Color*)malloc(8 * 8 * sizeof(Color));
        defaultFont[c].width = 8;
        defaultFont[c].height = 8;

        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                if (bitmap[i] & (1 << j))
                    defaultFont[c].data[i * 8 + j] = (Color) { 255, 255, 255, 255 };
                else
                    defaultFont[c].data[i * 8 + j] = (Color) { 0, 0, 0, 0 };
            }
        }
    }
}

void windowQuit(WrenVM* vm)
{
    if (window == NULL)
        return;

    if (window->screen != NULL) {
        SDL_DestroyTexture(window->screen);
        window->screen = NULL;
    }

    if (window->renderer != NULL) {
        SDL_DestroyRenderer(window->renderer);
        window->renderer = NULL;
    }

    if (window->window != NULL) {
        SDL_DestroyWindow(window->window);
        window->window = NULL;
    }

    free(window);

    SDL_Quit();
}

void windowUpdate(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");

    Image* image = (Image*)wrenGetSlotForeign(vm, 1);

    if (image->width != window->screenWidth || image->height != window->screenHeight) {
        SDL_DestroyTexture(window->screen);

        window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, image->width, image->height);
        if (window->screen == NULL) {
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
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            window->closed = true;
        } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            window->keysHeld[event.key.keysym.scancode] = true;
            window->keysPressed[event.key.keysym.scancode] = true;
        } else if (event.type == SDL_KEYUP) {
            window->keysHeld[event.key.keysym.scancode] = false;
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            window->mouseHeld[event.button.button] = true;
            window->mousePressed[event.button.button] = true;
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            window->mouseHeld[event.button.button] = false;
        } else if (event.type == SDL_MOUSEMOTION) {
            window->mouseX = event.motion.x;
            window->mouseY = event.motion.y;
        }
    }
}

void windowKeyHeld(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "key");

    const char* key = wrenGetSlotString(vm, 1);
    SDL_KeyCode sdlKey = SDL_GetScancodeFromName(key);

    wrenSetSlotBool(vm, 0, window->keysHeld[sdlKey]);
}

void windowKeyPressed(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, STRING, "key");

    const char* key = wrenGetSlotString(vm, 1);
    SDL_KeyCode sdlKey = SDL_GetScancodeFromName(key);

    wrenSetSlotBool(vm, 0, window->keysPressed[sdlKey]);
}

void windowMouseHeld(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, NUM, "button");

    int button = (int)wrenGetSlotDouble(vm, 1);

    wrenSetSlotBool(vm, 0, window->mouseHeld[button]);
}

void windowMousePressed(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, NUM, "button");

    int button = (int)wrenGetSlotDouble(vm, 1);

    wrenSetSlotBool(vm, 0, window->mousePressed[button]);
}

void windowWidth(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);

    int width;
    SDL_GetWindowSize(window->window, &width, NULL);

    wrenSetSlotDouble(vm, 0, width);
}

void windowHeight(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);

    int height;
    SDL_GetWindowSize(window->window, NULL, &height);

    wrenSetSlotDouble(vm, 0, height);
}

void windowTitle(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, SDL_GetWindowTitle(window->window));
}

void windowClosed(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotBool(vm, 0, window->closed);
}

void windowMouseX(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotDouble(vm, 0, window->mouseX);
}

void windowMouseY(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotDouble(vm, 0, window->mouseY);
}

void windowGetIntegerScaling(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 1);
    wrenSetSlotBool(vm, 0, SDL_RenderGetIntegerScale(window->renderer));
}

void windowSetIntegerScaling(WrenVM* vm)
{
    if (window == NULL) {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, BOOL, "integerScaling");

    bool integerScaling = wrenGetSlotBool(vm, 1);
    SDL_RenderSetIntegerScale(window->renderer, integerScaling);
}

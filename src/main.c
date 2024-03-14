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

#define ASSERT_SLOT_TYPE(vm, slot, type, fieldName)                       \
    if (wrenGetSlotType(vm, slot) != WREN_TYPE_##type)                    \
    {                                                                     \
        VM_ABORT(vm, "Expected " #fieldName " to be of type " #type "."); \
        return;                                                           \
    }

#define EXPAND(X) ((X) + ((X) > 0))

#define CLIP0(CX, X, X2, W) \
    if (X < CX)             \
    {                       \
        int D = CX - X;     \
        W -= D;             \
        X2 += D;            \
        X += D;             \
    }

#define CLIP1(X, DW, W) \
    if (X + W > DW)     \
        W = DW - X;

#define CLIP()                            \
    CLIP0(image->clipX, dx, sx, width);   \
    CLIP0(image->clipY, dy, sy, height);  \
    CLIP0(0, sx, dx, width);              \
    CLIP0(0, sy, dy, height);             \
    CLIP1(dx, image->clipX + cw, width);  \
    CLIP1(dy, image->clipY + ch, height); \
    CLIP1(sx, src->width, width);         \
    CLIP1(sy, src->height, height);       \
    if (width <= 0 || height <= 0)        \
    return

#define PACK_ARGB(alpha, red, green, blue) (uint32_t)(((uint8_t)(alpha) << 24) | ((uint8_t)(red) << 16) | ((uint8_t)(green) << 8) | (uint8_t)(blue))
#define ALPHA(color) ((uint8_t)(color >> 24))
#define RED(color) ((uint8_t)(color >> 16))
#define GREEN(color) ((uint8_t)(color >> 8))
#define BLUE(color) ((uint8_t)(color))

#include "api.wren.inc"

typedef struct
{
    uint8_t b, g, r, a;
} Color;

typedef struct
{
    int width, height;
    int clipX, clipY, clipWidth, clipHeight;
    Color *data;
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

static void colorAllocate(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotNewForeign(vm, 0, 0, sizeof(Color));
}

static void colorNew(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "red");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "green");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "blue");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "alpha");

    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 2);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 3);
    color->a = (uint8_t)wrenGetSlotDouble(vm, 4);
}

static void colorNew2(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "red");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "green");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "blue");

    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 2);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 3);
    color->a = 255;
}

static void colorNew3(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "hex");

    uint32_t hex = (uint32_t)wrenGetSlotDouble(vm, 1);

    color->r = (uint8_t)(hex >> 16);
    color->g = (uint8_t)(hex >> 8);
    color->b = (uint8_t)(hex);
    color->a = (uint8_t)(hex >> 24);
}

static void colorGetR(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->r);
}

static void colorGetG(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->g);
}

static void colorGetB(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->b);
}

static void colorGetA(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, color->a);
}

static void colorSetR(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    color->r = (uint8_t)wrenGetSlotDouble(vm, 1);
}

static void colorSetG(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    color->g = (uint8_t)wrenGetSlotDouble(vm, 1);
}

static void colorSetB(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    color->b = (uint8_t)wrenGetSlotDouble(vm, 1);
}

static void colorSetA(WrenVM *vm)
{
    Color *color = (Color *)wrenGetSlotForeign(vm, 0);
    color->a = (uint8_t)wrenGetSlotDouble(vm, 1);
}

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

static void imageNew(WrenVM *vm)
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

    image->data = (Color *)malloc(width * height * sizeof(Color));
    if (image->data == NULL)
    {
        VM_ABORT(vm, "Error allocating image");
        return;
    }

    image->width = width;
    image->height = height;

    image->clipX = 0;
    image->clipY = 0;
    image->clipWidth = width - 1;
    image->clipHeight = height - 1;
}

static void imageNew2(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, STRING, "path");

    char *path = (char *)wrenGetSlotString(vm, 1);

    char fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s", basePath, path);

    image->data = (Color *)stbi_load(fullPath, &image->width, &image->height, NULL, 4);
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

    image->clipX = 0;
    image->clipY = 0;
    image->clipWidth = image->width - 1;
    image->clipHeight = image->height - 1;
}

static void setColor(Image *image, int x, int y, Color color)
{
    int xa, i, a;

    int cx = image->clipX;
    int cy = image->clipY;
    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    if (x >= cx && y >= cy && x < cx + cw && y < cy + ch)
    {
        xa = EXPAND(color.a);
        a = xa * xa;
        i = y * image->width + x;

        image->data[i].r += (uint8_t)((color.r - image->data[i].r) * a >> 16);
        image->data[i].g += (uint8_t)((color.g - image->data[i].g) * a >> 16);
        image->data[i].b += (uint8_t)((color.b - image->data[i].b) * a >> 16);
        image->data[i].a += (uint8_t)((color.a - image->data[i].a) * a >> 16);
    }
}

static void imageSet(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, FOREIGN, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    Color *color = (Color *)wrenGetSlotForeign(vm, 3);

    setColor(image, x, y, *color);
}

static void imageGet(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);

    Color color = {0, 0, 0, 0};

    if (x >= 0 && y >= 0 && x < image->width && y < image->height)
        color = image->data[y * image->width + x];

    wrenSetSlotDouble(vm, 0, (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b);
}

static void imageClear(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "color");

    Color *color = (Color *)wrenGetSlotForeign(vm, 1);

    int count = image->width * image->height;

    int n;
    for (n = 0; n < count; n++)
        image->data[n] = *color;
}

static void imageBlit(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "image");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "dx");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "dy");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "sx");
    ASSERT_SLOT_TYPE(vm, 5, NUM, "sy");
    ASSERT_SLOT_TYPE(vm, 6, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 7, NUM, "height");

    Image *src = (Image *)wrenGetSlotForeign(vm, 1);
    int dx = (int)wrenGetSlotDouble(vm, 2);
    int dy = (int)wrenGetSlotDouble(vm, 3);
    int sx = (int)wrenGetSlotDouble(vm, 4);
    int sy = (int)wrenGetSlotDouble(vm, 5);
    int width = (int)wrenGetSlotDouble(vm, 6);
    int height = (int)wrenGetSlotDouble(vm, 7);

    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    CLIP();

    Color *ts = &src->data[sy * src->width + sx];
    Color *td = &image->data[dy * image->width + dx];
    int st = src->width;
    int dt = image->width;

    do
    {
        memcpy(td, ts, width * sizeof(Color));
        ts += st;
        td += dt;
    } while (--height);
}

static void imageBlitAlpha(WrenVM *vm)
{
    Image *image = (Image *)wrenGetSlotForeign(vm, 0);

    Image *src = (Image *)wrenGetSlotForeign(vm, 1);
    int dx = (int)wrenGetSlotDouble(vm, 2);
    int dy = (int)wrenGetSlotDouble(vm, 3);
    int sx = (int)wrenGetSlotDouble(vm, 4);
    int sy = (int)wrenGetSlotDouble(vm, 5);
    int width = (int)wrenGetSlotDouble(vm, 6);
    int height = (int)wrenGetSlotDouble(vm, 7);
    Color *tint = (Color *)wrenGetSlotForeign(vm, 8);

    int cw = image->clipWidth >= 0 ? image->clipWidth : image->width;
    int ch = image->clipHeight >= 0 ? image->clipHeight : image->height;

    CLIP();

    int xr = EXPAND(tint->r);
    int xg = EXPAND(tint->g);
    int xb = EXPAND(tint->b);
    int xa = EXPAND(tint->a);

    Color *ts = &src->data[sy * src->width + sx];
    Color *td = &image->data[dy * image->width + dx];
    int st = src->width;
    int dt = image->width;

    do
    {
        for (int x = 0; x < width; x++)
        {
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
        if (strcmp(signature, "init new(_,_)") == 0)
            return imageNew;
        if (strcmp(signature, "init new(_)") == 0)
            return imageNew2;
        if (strcmp(signature, "set(_,_,_)") == 0)
            return imageSet;
        if (strcmp(signature, "f_get(_,_)") == 0)
            return imageGet;
        if (strcmp(signature, "clear(_)") == 0)
            return imageClear;
        if (strcmp(signature, "blit(_,_,_,_,_,_,_)") == 0)
            return imageBlit;
        if (strcmp(signature, "blitAlpha(_,_,_,_,_,_,_,_)") == 0)
            return imageBlitAlpha;
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
    else if (strcmp(className, "Color") == 0)
    {
        if (strcmp(signature, "init new(_,_,_,_)") == 0)
            return colorNew;
        if (strcmp(signature, "init new(_,_,_)") == 0)
            return colorNew2;
        if (strcmp(signature, "init new(_)") == 0)
            return colorNew3;
        if (strcmp(signature, "r") == 0)
            return colorGetR;
        if (strcmp(signature, "g") == 0)
            return colorGetG;
        if (strcmp(signature, "b") == 0)
            return colorGetB;
        if (strcmp(signature, "a") == 0)
            return colorGetA;
        if (strcmp(signature, "r=(_)") == 0)
            return colorSetR;
        if (strcmp(signature, "g=(_)") == 0)
            return colorSetG;
        if (strcmp(signature, "b=(_)") == 0)
            return colorSetB;
        if (strcmp(signature, "a=(_)") == 0)
            return colorSetA;
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
    else if (strcmp(className, "Color") == 0)
    {
        methods.allocate = colorAllocate;
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

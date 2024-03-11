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

#include "api.wren.inc"

typedef struct
{
    uint32_t *data;
    int width, height;
} Bitmap;

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
} Window;

static Window *window = NULL;

static int argCount;
static char **args;
static int exitCode = 0;
static char basePath[MAX_PATH_LENGTH];

static void bitmapAllocate(WrenVM *vm)
{
    wrenEnsureSlots(vm, 1);
    wrenSetSlotNewForeign(vm, 0, 0, sizeof(Bitmap));
}

static void bitmapFinalize(void *data)
{
    Bitmap *bitmap = (Bitmap *)data;

    if (bitmap->data == NULL)
        return;

    free(bitmap->data);
    bitmap->data = NULL;
}

static void bitmapCreate(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "width");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "height");

    int width = (int)wrenGetSlotDouble(vm, 1);
    int height = (int)wrenGetSlotDouble(vm, 2);

    if (width <= 0 || height <= 0)
    {
        VM_ABORT(vm, "Bitmap dimensions must be positive");
        return;
    }

    bitmap->data = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (bitmap->data == NULL)
    {
        VM_ABORT(vm, "Error allocating bitmap");
        return;
    }

    bitmap->width = width;
    bitmap->height = height;
}

static void bitmapCreateImage(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, STRING, "path");

    char *path = (char *)wrenGetSlotString(vm, 1);

    char fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s", basePath, path);

    bitmap->data = (uint32_t *)stbi_load(fullPath, &bitmap->width, &bitmap->height, NULL, 4);
    if (bitmap->data == NULL)
    {
        VM_ABORT(vm, "Error loading image");
        return;
    }

    uint8_t *bytes = (uint8_t *)bitmap->data;
    int32_t n = bitmap->width * bitmap->height * sizeof(uint32_t);
    for (int32_t i = 0; i < n; i += 4)
    {
        uint8_t b = bytes[i];
        bytes[i] = bytes[i + 2];
        bytes[i + 2] = b;
    }
}

static void bitmapSet(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "color");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 3);

    if (x < 0 || x >= bitmap->width || y < 0 || y >= bitmap->height)
    {
        VM_ABORT(vm, "Bitmap index out of bounds");
        return;
    }

    bitmap->data[x + y * bitmap->width] = color;
}

static void bitmapGet(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "y");

    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);

    if (x < 0 || x >= bitmap->width || y < 0 || y >= bitmap->height)
    {
        VM_ABORT(vm, "Bitmap index out of bounds");
        return;
    }

    wrenSetSlotDouble(vm, 0, bitmap->data[x + y * bitmap->width]);
}

static void bitmapClear(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, NUM, "color");

    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 1);

    uint32_t *pixels = bitmap->data;
    for (int32_t i = 0, n = bitmap->width * bitmap->height; i < n; i++)
        pixels[i] = color;
}

static void bitmapRect(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

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

    if (x1 >= bitmap->width || x2 < 0 || y1 >= bitmap->height || y2 < 0)
    {
        VM_ABORT(vm, "Rect out of bounds");
        return;
    }

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 >= bitmap->width)
        x2 = bitmap->width - 1;
    if (y2 >= bitmap->height)
        y2 = bitmap->height - 1;

    int clipped_width = x2 - x1 + 1;
    int next_row = bitmap->width - clipped_width;
    uint32_t *pixel = bitmap->data + y1 * bitmap->width + x1;

    for (int y = y1; y <= y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
            *pixel++ = color;

        pixel += next_row;
    }
}

static void bitmapBlit(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "bitmap");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");

    Bitmap *other = (Bitmap *)wrenGetSlotForeign(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);

    int dst_x1 = x;
    int dst_y1 = y;
    int dst_x2 = x + other->width - 1;
    int dst_y2 = y + other->height - 1;
    int src_x1 = 0;
    int src_y1 = 0;

    if (dst_x1 >= bitmap->width || dst_x2 < 0 || dst_y1 >= bitmap->height || dst_y2 < 0)
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
    if (dst_x2 >= bitmap->width)
        dst_x2 = bitmap->width - 1;
    if (dst_y2 >= bitmap->height)
        dst_y2 = bitmap->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = bitmap->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = bitmap->data + dst_y1 * bitmap->width + dst_x1;
    uint32_t *src_pixel = other->data + src_y1 * other->width + src_x1;

    for (y = dst_y1; y <= dst_y2; y++)
    {
        for (int i = 0; i < clipped_width; i++)
            *dst_pixel++ = *src_pixel++;

        dst_pixel += dst_next_row;
        src_pixel += src_next_row;
    }
}

static void bitmapBlitKey(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "bitmap");
    ASSERT_SLOT_TYPE(vm, 2, NUM, "x");
    ASSERT_SLOT_TYPE(vm, 3, NUM, "y");
    ASSERT_SLOT_TYPE(vm, 4, NUM, "key");

    Bitmap *other = (Bitmap *)wrenGetSlotForeign(vm, 1);
    int x = (int)wrenGetSlotDouble(vm, 2);
    int y = (int)wrenGetSlotDouble(vm, 3);
    uint32_t key = (uint32_t)wrenGetSlotDouble(vm, 4);

    int dst_x1 = x;
    int dst_y1 = y;
    int dst_x2 = x + other->width - 1;
    int dst_y2 = y + other->height - 1;
    int src_x1 = 0;
    int src_y1 = 0;

    if (dst_x1 >= bitmap->width || dst_x2 < 0 || dst_y1 >= bitmap->height || dst_y2 < 0)
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
    if (dst_x2 >= bitmap->width)
        dst_x2 = bitmap->width - 1;
    if (dst_y2 >= bitmap->height)
        dst_y2 = bitmap->height - 1;

    int clipped_width = dst_x2 - dst_x1 + 1;
    int dst_next_row = bitmap->width - clipped_width;
    int src_next_row = other->width - clipped_width;
    uint32_t *dst_pixel = bitmap->data + dst_y1 * bitmap->width + dst_x1;
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

static void bitmapWidth(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, bitmap->width);
}

static void bitmapHeight(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);

    wrenSetSlotDouble(vm, 0, bitmap->height);
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

static void windowPoll(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    wrenEnsureSlots(vm, 2);
    wrenSetSlotNewList(vm, 0);

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0)
    {
        switch (event.type)
        {
        case SDL_QUIT:
            wrenSetSlotString(vm, 1, "quit");
            wrenInsertInList(vm, 0, -1, 1);
            break;
        case SDL_KEYDOWN:
            wrenSetSlotString(vm, 1, "key");
            wrenInsertInList(vm, 0, -1, 1);
            break;
        }
    }
}

static void windowUpdate(WrenVM *vm)
{
    if (window == NULL)
    {
        VM_ABORT(vm, "Window not initialized");
        return;
    }

    ASSERT_SLOT_TYPE(vm, 1, FOREIGN, "bitmap");

    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 1);

    if (bitmap->width != window->screenWidth || bitmap->height != window->screenHeight)
    {
        SDL_DestroyTexture(window->screen);

        window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, bitmap->width, bitmap->height);
        if (window->screen == NULL)
        {
            VM_ABORT(vm, "Error creating screen texture");
            return;
        }

        window->screenWidth = bitmap->width;
        window->screenHeight = bitmap->height;

        SDL_RenderSetLogicalSize(window->renderer, bitmap->width, bitmap->height);
    }

    SDL_UpdateTexture(window->screen, NULL, bitmap->data, bitmap->width * 4);

    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->screen, NULL, NULL);
    SDL_RenderPresent(window->renderer);
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
    if (strcmp(className, "Bitmap") == 0)
    {
        if (strcmp(signature, "init create(_,_)") == 0)
            return bitmapCreate;
        if (strcmp(signature, "init create(_)") == 0)
            return bitmapCreateImage;
        if (strcmp(signature, "f_set(_,_,_)") == 0)
            return bitmapSet;
        if (strcmp(signature, "f_get(_,_)") == 0)
            return bitmapGet;
        if (strcmp(signature, "f_clear(_)") == 0)
            return bitmapClear;
        if (strcmp(signature, "f_rect(_,_,_,_,_)") == 0)
            return bitmapRect;
        if (strcmp(signature, "blit(_,_,_)") == 0)
            return bitmapBlit;
        if (strcmp(signature, "f_blitKey(_,_,_,_)") == 0)
            return bitmapBlitKey;
        if (strcmp(signature, "width") == 0)
            return bitmapWidth;
        if (strcmp(signature, "height") == 0)
            return bitmapHeight;
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
        if (strcmp(signature, "poll()") == 0)
            return windowPoll;
        if (strcmp(signature, "update(_)") == 0)
            return windowUpdate;
        if (strcmp(signature, "width") == 0)
            return windowWidth;
        if (strcmp(signature, "height") == 0)
            return windowHeight;
        if (strcmp(signature, "title") == 0)
            return windowTitle;
    }

    return NULL;
}

static WrenForeignClassMethods wrenBindForeignClass(WrenVM *vm, const char *module, const char *className)
{
    WrenForeignClassMethods methods = {0};

    if (strcmp(className, "Bitmap") == 0)
    {
        methods.allocate = bitmapAllocate;
        methods.finalize = bitmapFinalize;
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

#define BASIL_VERSION "0.1.0"

#include "api.wren.inc"

typedef struct
{
    uint32_t *data;
    int width, height;
} Bitmap;

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen;
} Window;

static Window *window;

static int argCount;
static char **args;
static int exitCode = 0;

static void bitmapAllocate(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenSetSlotNewForeign(vm, 0, 0, sizeof(Bitmap));
    int width = (int)wrenGetSlotDouble(vm, 1);
    int height = (int)wrenGetSlotDouble(vm, 2);

    bitmap->data = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (bitmap->data == NULL)
    {
        wrenSetSlotString(vm, 0, "Error allocating bitmap");
        wrenAbortFiber(vm, 0);
        return;
    }

    bitmap->width = width;
    bitmap->height = height;
}

static void bitmapFinalize(void *data)
{
    Bitmap *bitmap = (Bitmap *)data;

    if (bitmap->data == NULL)
        return;

    free(bitmap->data);
    bitmap->data = NULL;
}

static void bitmapSet(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 0);
    int x = (int)wrenGetSlotDouble(vm, 1);
    int y = (int)wrenGetSlotDouble(vm, 2);
    uint32_t color = (uint32_t)wrenGetSlotDouble(vm, 3);

    bitmap->data[y * bitmap->width + x] = color;
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
    if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM)
    {
        wrenSetSlotString(vm, 0, "Argument must be a number");
        wrenAbortFiber(vm, 0);
        return;
    }

    exitCode = (int)wrenGetSlotDouble(vm, 1);
}

static void windowInit(WrenVM *vm)
{
    window = (Window *)malloc(sizeof(Window));

    const char *title = wrenGetSlotString(vm, 1);
    int width = (int)wrenGetSlotDouble(vm, 2);
    int height = (int)wrenGetSlotDouble(vm, 3);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        wrenSetSlotString(vm, 0, "Error initializing SDL");
        wrenAbortFiber(vm, 0);
        return;
    }

    window->window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_RESIZABLE);
    if (window->window == NULL)
    {
        wrenSetSlotString(vm, 0, "Error creating window");
        wrenAbortFiber(vm, 0);
        return;
    }

    window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED);
    if (window->renderer == NULL)
    {
        wrenSetSlotString(vm, 0, "Error creating renderer");
        wrenAbortFiber(vm, 0);
        return;
    }

    window->screen = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (window->screen == NULL)
    {
        wrenSetSlotString(vm, 0, "Error creating screen texture");
        wrenAbortFiber(vm, 0);
        return;
    }

    SDL_SetWindowMinimumSize(window->window, width, height);
    SDL_RenderSetLogicalSize(window->renderer, width, height);
}

static void windowQuit(WrenVM *vm)
{
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

    SDL_Quit();

    free(window);
}

static void windowUpdate(WrenVM *vm)
{
    Bitmap *bitmap = (Bitmap *)wrenGetSlotForeign(vm, 1);

    SDL_UpdateTexture(window->screen, NULL, bitmap->data, bitmap->width * 4);

    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->screen, NULL, NULL);
    SDL_RenderPresent(window->renderer);
}

static void windowPoll(WrenVM *vm)
{
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

static WrenLoadModuleResult wrenLoadModule(WrenVM *vm, const char *name)
{
    WrenLoadModuleResult result = {0};

    if (strcmp(name, "basil") == 0)
        result.source = apiModuleSource;

    return result;
}

static WrenForeignMethodFn wrenBindForeignMethod(WrenVM *vm, const char *module, const char *className, bool isStatic, const char *signature)
{
    if (strcmp(className, "Bitmap") == 0)
    {
        if (strcmp(signature, "f_set(_,_,_)") == 0)
            return bitmapSet;
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
    else if (strcmp(className, "Window") == 0)
    {
        if (strcmp(signature, "init(_,_,_)") == 0)
            return windowInit;
        if (strcmp(signature, "quit()") == 0)
            return windowQuit;
        if (strcmp(signature, "update(_)") == 0)
            return windowUpdate;
        if (strcmp(signature, "poll()") == 0)
            return windowPoll;
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

int main(int argc, char *argv[])
{
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

    char *source = readFile("test.wren");
    if (source == NULL)
        return 1;

    wrenInterpret(vm, "main", source);

    free(source);

    wrenFreeVM(vm);

    return exitCode;
}

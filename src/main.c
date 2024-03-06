#include <stdio.h>
#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

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
    WrenConfiguration config;
    wrenInitConfiguration(&config);

    config.writeFn = wrenWrite;
    config.errorFn = wrenError;

    WrenVM *vm = wrenNewVM(&config);

    wrenInterpret(vm, "main", "System.print(\"Hello, World!\")");

    wrenFreeVM(vm);

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("Error initializing SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Basil", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Error creating window: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Event event;
    int quit = 0;

    while (!quit)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                quit = 1;
        }
    }

    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}

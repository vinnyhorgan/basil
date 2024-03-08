#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

#define BASIL_VERSION "0.1.0"

#define WIDTH 320
#define HEIGHT 240

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

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("Error initializing SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Basil", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH * 2, HEIGHT * 2, SDL_WINDOW_RESIZABLE);
    if (window == NULL)
    {
        printf("Error creating window: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Error creating renderer: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetWindowMinimumSize(window, WIDTH, HEIGHT);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);

    SDL_Texture *screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    uint32_t *pixels = malloc(WIDTH * HEIGHT * 4);

    SDL_Event event;
    bool quit = false;

    while (!quit)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                quit = true;
        }

        for (int y = 0; y < HEIGHT; ++y)
        {
            for (int x = 0; x < WIDTH; ++x)
            {
                pixels[y * WIDTH + x] = 0xFFFFFFFF;
            }
        }

        SDL_UpdateTexture(screen, NULL, pixels, WIDTH * 4);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, screen, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    free(pixels);

    SDL_DestroyTexture(screen);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}

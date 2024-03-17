#ifndef API_H
#define API_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

void setArgs(int argc, char** argv);
int getExitCode();

typedef struct
{
    uint8_t b, g, r, a;
} Color;

void colorAllocate(WrenVM* vm);
void colorNew(WrenVM* vm);
void colorNew2(WrenVM* vm);
void colorNew3(WrenVM* vm);
void colorGetR(WrenVM* vm);
void colorGetG(WrenVM* vm);
void colorGetB(WrenVM* vm);
void colorGetA(WrenVM* vm);
void colorSetR(WrenVM* vm);
void colorSetG(WrenVM* vm);
void colorSetB(WrenVM* vm);
void colorSetA(WrenVM* vm);

typedef struct
{
    int width, height;
    int clipX, clipY, clipWidth, clipHeight;
    Color* data;
} Image;

void imageAllocate(WrenVM* vm);
void imageFinalize(void* data);
void imageNew(WrenVM* vm);
void imageNew2(WrenVM* vm);
void imageGetWidth(WrenVM* vm);
void imageGetHeight(WrenVM* vm);
void imageClip(WrenVM* vm);
void imageGet(WrenVM* vm);
void imageSet(WrenVM* vm);
void imageClear(WrenVM* vm);
void imageFill(WrenVM* vm);
void imageLine(WrenVM* vm);
void imageRect(WrenVM* vm);
void imageFillRect(WrenVM* vm);
void imageCircle(WrenVM* vm);
void imageFillCircle(WrenVM* vm);
void imagePrint(WrenVM* vm);
void imageBlit(WrenVM* vm);
void imageBlitAlpha(WrenVM* vm);
void imageBlitTint(WrenVM* vm);

void osName(WrenVM* vm);
void osBasilVersion(WrenVM* vm);
void osArgs(WrenVM* vm);
void osExit(WrenVM* vm);

typedef struct
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* screen;
    int screenWidth, screenHeight;
    bool closed;
    SDL_Scancode keysHeld[SDL_NUM_SCANCODES];
    SDL_Scancode keysPressed[SDL_NUM_SCANCODES];
    bool mouseHeld[5];
    bool mousePressed[5];
    int mouseX, mouseY;
    uint64_t prevTime;
    int targetFps;
} Window;

void windowInit(WrenVM* vm);
void windowQuit(WrenVM* vm);
void windowUpdate(WrenVM* vm);
void windowKeyHeld(WrenVM* vm);
void windowKeyPressed(WrenVM* vm);
void windowMouseHeld(WrenVM* vm);
void windowMousePressed(WrenVM* vm);
void windowWidth(WrenVM* vm);
void windowHeight(WrenVM* vm);
void windowTitle(WrenVM* vm);
void windowClosed(WrenVM* vm);
void windowMouseX(WrenVM* vm);
void windowMouseY(WrenVM* vm);
void windowGetIntegerScaling(WrenVM* vm);
void windowSetIntegerScaling(WrenVM* vm);
void windowTime(WrenVM* vm);
void windowTargetFps(WrenVM* vm);

#endif

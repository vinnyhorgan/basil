#ifndef API_H
#define API_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "lib/wren/wren.h"

#define EXPAND(X) ((X) + ((X) > 0))

#define CLIP0(CX, X, X2, W) \
    if (X < CX) {           \
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

void imageSet(WrenVM* vm);
void imageGet(WrenVM* vm);
void imageClear(WrenVM* vm);
void imageBlit(WrenVM* vm);
void imageBlitAlpha(WrenVM* vm);
void imageText(WrenVM* vm);
void imageFill(WrenVM* vm);

void osName(WrenVM* vm);
void osBasilVersion(WrenVM* vm);
void osArgs(WrenVM* vm);
void osExit(WrenVM* vm);

typedef struct
{
    uint64_t start;
    uint64_t lastTick;
    double delta;
} Timer;

void timerAllocate(WrenVM* vm);
void timerTick(WrenVM* vm);
void timerTickFramerate(WrenVM* vm);
void timerTime(WrenVM* vm);
void timerDelta(WrenVM* vm);

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

#endif

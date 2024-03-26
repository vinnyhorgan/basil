#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/wren/wren.h"

#include "api.h"
#include "api.wren.inc"

#include "util.h"

static const char* basePath = NULL;

static void onComplete(WrenVM* vm, const char* name, WrenLoadModuleResult result)
{
    if (result.source)
        free((void*)result.source);
}

static WrenLoadModuleResult wrenLoadModule(WrenVM* vm, const char* name)
{
    WrenLoadModuleResult result = { 0 };

    if (strcmp(name, "meta") == 0 || strcmp(name, "random") == 0)
        return result;

    if (strcmp(name, "basil") == 0) {
        result.source = apiModuleSource;
        return result;
    }

    char fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s.wren", basePath, name);

    result.source = readFile(fullPath);
    result.onComplete = onComplete;

    return result;
}

static WrenForeignMethodFn wrenBindForeignMethod(WrenVM* vm, const char* module, const char* className, bool isStatic, const char* signature)
{
    if (strcmp(className, "Color") == 0) {
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
    } else if (strcmp(className, "Font") == 0) {
        if (strcmp(signature, "init new(_,_)") == 0)
            return fontNew;
    } else if (strcmp(className, "Image") == 0) {
        if (strcmp(signature, "init new(_,_)") == 0)
            return imageNew;
        if (strcmp(signature, "init new(_)") == 0)
            return imageNew2;
        if (strcmp(signature, "width") == 0)
            return imageGetWidth;
        if (strcmp(signature, "height") == 0)
            return imageGetHeight;
        if (strcmp(signature, "clip(_,_,_,_)") == 0)
            return imageClip;
        if (strcmp(signature, "f_get(_,_)") == 0)
            return imageGet;
        if (strcmp(signature, "set(_,_,_)") == 0)
            return imageSet;
        if (strcmp(signature, "clear(_)") == 0)
            return imageClear;
        if (strcmp(signature, "fill(_,_,_,_,_)") == 0)
            return imageFill;
        if (strcmp(signature, "line(_,_,_,_,_)") == 0)
            return imageLine;
        if (strcmp(signature, "rect(_,_,_,_,_)") == 0)
            return imageRect;
        if (strcmp(signature, "fillRect(_,_,_,_,_)") == 0)
            return imageFillRect;
        if (strcmp(signature, "circle(_,_,_,_)") == 0)
            return imageCircle;
        if (strcmp(signature, "fillCircle(_,_,_,_)") == 0)
            return imageFillCircle;
        if (strcmp(signature, "print(_,_,_,_)") == 0)
            return imagePrint;
        if (strcmp(signature, "blit(_,_,_,_,_,_,_)") == 0)
            return imageBlit;
        if (strcmp(signature, "blitAlpha(_,_,_,_,_,_,_,_)") == 0)
            return imageBlitAlpha;
        if (strcmp(signature, "blitTint(_,_,_,_,_,_,_,_)") == 0)
            return imageBlitTint;
    } else if (strcmp(className, "OS") == 0) {
        if (strcmp(signature, "name") == 0)
            return osName;
        if (strcmp(signature, "basilVersion") == 0)
            return osBasilVersion;
        if (strcmp(signature, "args") == 0)
            return osArgs;
        if (strcmp(signature, "f_exit(_)") == 0)
            return osExit;
    } else if (strcmp(className, "Window") == 0) {
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
        if (strcmp(signature, "integerScaling") == 0)
            return windowGetIntegerScaling;
        if (strcmp(signature, "integerScaling=(_)") == 0)
            return windowSetIntegerScaling;
        if (strcmp(signature, "time()") == 0)
            return windowTime;
        if (strcmp(signature, "targetFps=(_)") == 0)
            return windowTargetFps;
    }

    return NULL;
}

static WrenForeignClassMethods wrenBindForeignClass(WrenVM* vm, const char* module, const char* className)
{
    WrenForeignClassMethods methods = { 0 };

    if (strcmp(className, "Color") == 0) {
        methods.allocate = colorAllocate;
    } else if (strcmp(className, "Font") == 0) {
        methods.allocate = fontAllocate;
        methods.finalize = fontFinalize;
    } else if (strcmp(className, "Image") == 0) {
        methods.allocate = imageAllocate;
        methods.finalize = imageFinalize;
    }

    return methods;
}

static void wrenWrite(WrenVM* vm, const char* text)
{
    printf("%s", text);
}

static void wrenError(WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message)
{
    switch (type) {
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

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("\tbasil [file] [arguments...]\n");
        printf("\tbasil version\n");
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "version") == 0) {
        printf("basil %s\n", BASIL_VERSION);
        return 0;
    }

    const char* sourcePath = argv[1];

    char* source = readFile(sourcePath);
    if (source == NULL)
        return 1;

    basePath = getDirectoryPath(sourcePath);

    setArgs(argc, argv);

    WrenConfiguration config;
    wrenInitConfiguration(&config);

    config.loadModuleFn = wrenLoadModule;
    config.bindForeignMethodFn = wrenBindForeignMethod;
    config.bindForeignClassFn = wrenBindForeignClass;
    config.writeFn = wrenWrite;
    config.errorFn = wrenError;

    WrenVM* vm = wrenNewVM(&config);

    wrenInterpret(vm, sourcePath, source);

    free(source);
    wrenFreeVM(vm);

    return getExitCode();
}

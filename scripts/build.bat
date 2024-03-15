@echo off

gcc src/basil.c src/lib/wren/wren.c assets/icon.res -std=c99 -O3 -s -IC:\SDL2\include -LC:\SDL2\lib -lmingw32 -lSDL2main -lSDL2 -o basil.exe

#!/bin/bash

gcc src/basil.c src/lib/wren/wren.c -std=c99 -O3 -s -lSDL2 -lm -o basil

#!/bin/bash

mkdir -p ./build

if [ "$(uname -s)" = "Linux" ]; then
    OUTPUT="./build/Moonaut"
    LIBS="-lm -lX11 -lXrandr -ldl -lpthread"
else
    OUTPUT="./build/Moonaut.exe"
    LIBS="-lm -lgdi32"
fi

GCC_OPTS="-std=gnu99 -O3 -ffast-math -march=native"

gcc $GCC_OPTS \
  -I lua-5.5.0/src -o $OUTPUT main.c \
  lua-5.5.0/src/lapi.c \
  lua-5.5.0/src/lauxlib.c \
  lua-5.5.0/src/lbaselib.c \
  lua-5.5.0/src/lcode.c \
  lua-5.5.0/src/lcorolib.c \
  lua-5.5.0/src/lctype.c \
  lua-5.5.0/src/ldblib.c \
  lua-5.5.0/src/ldebug.c \
  lua-5.5.0/src/ldo.c \
  lua-5.5.0/src/ldump.c \
  lua-5.5.0/src/lfunc.c \
  lua-5.5.0/src/lgc.c \
  lua-5.5.0/src/linit.c \
  lua-5.5.0/src/liolib.c \
  lua-5.5.0/src/llex.c \
  lua-5.5.0/src/lmathlib.c \
  lua-5.5.0/src/lmem.c \
  lua-5.5.0/src/loadlib.c \
  lua-5.5.0/src/lobject.c \
  lua-5.5.0/src/lopcodes.c \
  lua-5.5.0/src/loslib.c \
  lua-5.5.0/src/lparser.c \
  lua-5.5.0/src/lstate.c \
  lua-5.5.0/src/lstring.c \
  lua-5.5.0/src/lstrlib.c \
  lua-5.5.0/src/ltable.c \
  lua-5.5.0/src/ltablib.c \
  lua-5.5.0/src/ltm.c \
  lua-5.5.0/src/lundump.c \
  lua-5.5.0/src/lutf8lib.c \
  lua-5.5.0/src/lvm.c \
  lua-5.5.0/src/lzio.c \
  $LIBS -Wno-multichar 2>&1 | grep -E "undefined|error|warning" | head -30

$OUTPUT
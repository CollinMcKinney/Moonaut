#!/bin/bash

mkdir -p ./build

if [ "$(uname -s)" = "Linux" ]; then
    rm -rf ./build/Moonaut  
    OUTPUT="./build/Moonaut"
    LIBS=(-lm -lX11 -lXrandr -ldl -lpthread)
else
    OUTPUT="./build/Moonaut.exe"
    LIBS=(-lm -lgdi32)
fi

GCC_OPTS=(-std=gnu89 -O3 -ffast-math -march=native)

clang "${GCC_OPTS[@]}" \
  -I libs/lua-5.5.0/src -o $OUTPUT main.c \
  libs/lua-5.5.0/src/lapi.c \
  libs/lua-5.5.0/src/lauxlib.c \
  libs/lua-5.5.0/src/lbaselib.c \
  libs/lua-5.5.0/src/lcode.c \
  libs/lua-5.5.0/src/lcorolib.c \
  libs/lua-5.5.0/src/lctype.c \
  libs/lua-5.5.0/src/ldblib.c \
  libs/lua-5.5.0/src/ldebug.c \
  libs/lua-5.5.0/src/ldo.c \
  libs/lua-5.5.0/src/ldump.c \
  libs/lua-5.5.0/src/lfunc.c \
  libs/lua-5.5.0/src/lgc.c \
  libs/lua-5.5.0/src/linit.c \
  libs/lua-5.5.0/src/liolib.c \
  libs/lua-5.5.0/src/llex.c \
  libs/lua-5.5.0/src/lmathlib.c \
  libs/lua-5.5.0/src/lmem.c \
  libs/lua-5.5.0/src/loadlib.c \
  libs/lua-5.5.0/src/lobject.c \
  libs/lua-5.5.0/src/lopcodes.c \
  libs/lua-5.5.0/src/loslib.c \
  libs/lua-5.5.0/src/lparser.c \
  libs/lua-5.5.0/src/lstate.c \
  libs/lua-5.5.0/src/lstring.c \
  libs/lua-5.5.0/src/lstrlib.c \
  libs/lua-5.5.0/src/ltable.c \
  libs/lua-5.5.0/src/ltablib.c \
  libs/lua-5.5.0/src/ltm.c \
  libs/lua-5.5.0/src/lundump.c \
  libs/lua-5.5.0/src/lutf8lib.c \
  libs/lua-5.5.0/src/lvm.c \
  libs/lua-5.5.0/src/lzio.c \
  libs/C-Thread-Pool/thpool.c \
  "${LIBS[@]}" -Wno-multichar 2>&1 | grep -E "undefined|error|warning" | head -30

$OUTPUT
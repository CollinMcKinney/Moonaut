cmake_minimum_required(VERSION 3.16)
project(Moonaut LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )
endif()

add_compile_definitions(
    _POSIX_C_SOURCE=200809L 
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows SDK headers are not C89-clean (they use // comments, etc.)
    # so we must use gnu89 on this platform. Our own source code is
    # written in strict ANSI C89 subset and compiles on all platforms.
    add_compile_options(
        -std=gnu89 -pedantic -w -g -O3
        -fno-math-errno -ffp-contract=fast -fno-trapping-math
        -fcx-limited-range -fno-strict-aliasing -fomit-frame-pointer -flto
        -march=x86-64-v2 -mtune=generic)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -std=c89 -pedantic -w -g -O3 -Wc++-compat
        -fno-math-errno -ffp-contract=fast -fno-trapping-math
        -fcx-limited-range -fno-strict-aliasing -fomit-frame-pointer -flto
        -march=x86-64-v2 -mtune=generic)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
    add_compile_options(
        -std=c89 -pedantic -Wall -Wextra -Wc++-compat -Wno-unused-function -g -O3
        -fno-math-errno -ffp-contract=fast -fno-trapping-math
        -fcx-limited-range -fno-strict-aliasing -fomit-frame-pointer -flto
        -march=x86-64-v2 -mtune=generic)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LIBS m X11 Xext Xrandr dl pthread asound GL)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_LIBS gdi32.lib ole32.lib avrt.lib uuid.lib opengl32.lib xinput.lib)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_LIBS Cocoa CoreVideo OpenGL IOKit CoreAudio AudioUnit)
endif()

add_executable(Moonaut
    main.c

    libs/jobgraph/jobgraph.c

    libs/audiopipe/audiopipe.c
    libs/audiopipe/audiomixer.c
    libs/audiopipe/wavloader.c

    libs/lua-5.5.0/src/lapi.c
    libs/lua-5.5.0/src/lauxlib.c
    libs/lua-5.5.0/src/lbaselib.c
    libs/lua-5.5.0/src/lcode.c
    libs/lua-5.5.0/src/lcorolib.c
    libs/lua-5.5.0/src/lctype.c
    libs/lua-5.5.0/src/ldblib.c
    libs/lua-5.5.0/src/ldebug.c
    libs/lua-5.5.0/src/ldo.c
    libs/lua-5.5.0/src/ldump.c
    libs/lua-5.5.0/src/lfunc.c
    libs/lua-5.5.0/src/lgc.c
    libs/lua-5.5.0/src/linit.c
    libs/lua-5.5.0/src/liolib.c
    libs/lua-5.5.0/src/llex.c
    libs/lua-5.5.0/src/lmathlib.c
    libs/lua-5.5.0/src/lmem.c
    libs/lua-5.5.0/src/loadlib.c
    libs/lua-5.5.0/src/lobject.c
    libs/lua-5.5.0/src/lopcodes.c
    libs/lua-5.5.0/src/loslib.c
    libs/lua-5.5.0/src/lparser.c
    libs/lua-5.5.0/src/lstate.c
    libs/lua-5.5.0/src/lstring.c
    libs/lua-5.5.0/src/lstrlib.c
    libs/lua-5.5.0/src/ltable.c
    libs/lua-5.5.0/src/ltablib.c
    libs/lua-5.5.0/src/ltm.c
    libs/lua-5.5.0/src/lundump.c
    libs/lua-5.5.0/src/lutf8lib.c
    libs/lua-5.5.0/src/lvm.c
    libs/lua-5.5.0/src/lzio.c
)

target_compile_definitions(Moonaut PRIVATE LUA_USE_C89)
target_compile_definitions(Moonaut PRIVATE LUA_C89_NUMBERS)
target_compile_definitions(Moonaut PRIVATE LUA_32BITS)

target_include_directories(Moonaut PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/lua-5.5.0/src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Moonaut PRIVATE ${PLATFORM_LIBS})
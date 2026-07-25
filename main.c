/*
 * main.c – Moonaut Engine entry point
 *
 * This file exists only to satisfy the operating system.
 * All engine logic lives in src/runtime.h.
 */

#include "src/runtime.h"

int main(void)
{
    scenario_init();
    scenario_shutdown();
    return 0;
}

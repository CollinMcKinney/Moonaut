#ifndef RENDERER_H
#define RENDERER_H

/* Backend selection lives here, and nowhere else.
 *
 * Every engine header that calls into the renderer includes this file, so the
 * dependency is stated where it is used. Relying on some earlier header in the
 * chain having pulled the rasterizer in works only as long as the include order
 * in runtime.h happens to hold, and it makes each header unreadable on its own:
 * a tool parsing defaults.h in isolation cannot see the renderer's API.
 *
 * Exactly one rasterizer is active, so it is included with its IMPLEMENTATION
 * macro defined: the rasterizers are single-header implementations that split a
 * declaration section from a definition section on that macro. The rasterizer's
 * own include guard makes a repeat include harmless.
 *
 * To change backend, define USE_DX11 here. This is a hand edit rather than a
 * CMake option, so the selection is visible in one place instead of being
 * spread between the build system and an in-source define.
 */

#define USE_GL
#if defined(USE_DX11)
#define RASTERIZER_DX11_IMPLEMENTATION
#include "rasterizer_DX11.h"
#elif defined(USE_GL)
#define RASTERIZER_GL_IMPLEMENTATION
#include "rasterizer_GL.h"
#else
#define RASTERIZER_SW_IMPLEMENTATION
#include "rasterizer_SW.h"
#endif

#endif /* RENDERER_H */

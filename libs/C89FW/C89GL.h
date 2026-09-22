/*
 * C89GL.h - OpenGL 3.3 Core Backend for C89FW
 *           Extended with OpenGL 4.3 compute/SSBO support.
 *
 * Usage:
 *   #define C89GL_IMPLEMENTATION
 *   #include "C89GL.h"
 *
 *   C89FW_window_t win;
 *   C89FW_open(&win, 800, 600, "GL Window");
 *
 *   C89GL_Context ctx;
 *   if (C89GL_create_context(&win, &ctx)) {
 *       C89GL_make_current(&ctx);
 *       if (C89GL_load_functions()) {
 *           while (!win.should_close) {
 *               C89FW_update(&win);
 *               C89GL_glClear(GL_COLOR_BUFFER_BIT);
 *               C89GL_swap_buffers(&ctx);
 *           }
 *       }
 *       C89GL_destroy_context(&ctx);
 *   }
 *   C89FW_close(&win);
 */

#ifndef C89GL_H
#define C89GL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "C89FW.h"

/* ---------- Define calling convention ---------- */
#if defined(_WIN32)
    #define C89GL_APIENTRY __stdcall
#else
    #define C89GL_APIENTRY
#endif

/* ---------- Basic OpenGL types ---------- */
#ifndef GLushort
typedef unsigned short GLushort;
#endif
#ifndef GLuint
typedef unsigned int GLuint;
#endif
#ifndef GLint
typedef int GLint;
#endif
#ifndef GLenum
typedef unsigned int GLenum;
#endif
#ifndef GLboolean
typedef unsigned char GLboolean;
#endif
#ifndef GLsizei
typedef int GLsizei;
#endif
#ifndef GLfloat
typedef float GLfloat;
#endif
#ifndef GLclampf
typedef float GLclampf;
#endif
#ifndef GLsync
typedef void* GLsync;
#endif
#ifndef GLbitfield
typedef unsigned int GLbitfield;
#endif
#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif

/* ========================================================================
   OPENGL 3.3 + 4.3 ENUMS
   ======================================================================== */
#define GL_FALSE                                    0
#define GL_TRUE                                     1
#define GL_NONE                                     0
#define GL_NO_ERROR                                 0
#define GL_POINTS                                   0x0000
#define GL_LINES                                    0x0001
#define GL_LINE_STRIP                               0x0003
#define GL_TRIANGLES                                0x0004
#define GL_TRIANGLE_STRIP                           0x0005
#define GL_TRIANGLE_FAN                             0x0006
#define GL_BYTE                                     0x1400
#define GL_UNSIGNED_BYTE                            0x1401
#define GL_SHORT                                    0x1402
#define GL_UNSIGNED_SHORT                           0x1403
#define GL_INT                                      0x1404
#define GL_UNSIGNED_INT                             0x1405
#define GL_FLOAT                                    0x1406
#define GL_DOUBLE                                   0x140A
#define GL_ARRAY_BUFFER                             0x8892
#define GL_ELEMENT_ARRAY_BUFFER                     0x8893
#define GL_STATIC_DRAW                              0x88E4
#define GL_DYNAMIC_DRAW                             0x88E8
#define GL_STREAM_DRAW                              0x88E0
#define GL_STREAM_READ                              0x88E1
#define GL_STREAM_COPY                              0x88E2
#define GL_READ_ONLY                                0x88B8
#define GL_WRITE_ONLY                               0x88B9
#define GL_READ_WRITE                               0x88BA
#define GL_VERTEX_SHADER                            0x8B31
#define GL_FRAGMENT_SHADER                          0x8B30
#define GL_GEOMETRY_SHADER                          0x8DD9
#define GL_COMPILE_STATUS                           0x8B81
#define GL_LINK_STATUS                              0x8B82
#define GL_INFO_LOG_LENGTH                          0x8B84
#define GL_SHADER_SOURCE_LENGTH                     0x8B88
#define GL_FRAMEBUFFER                              0x8D40
#define GL_RENDERBUFFER                             0x8D41
#define GL_COLOR_ATTACHMENT0                        0x8CE0
#define GL_COLOR_ATTACHMENT1                        0x8CE1
#define GL_COLOR_ATTACHMENT2                        0x8CE2
#define GL_DEPTH_ATTACHMENT                         0x8D00
#define GL_STENCIL_ATTACHMENT                       0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT                 0x821A
#define GL_DEPTH24_STENCIL8                         0x88F0

/* ---- Default framebuffer attachment points ---- */
#define GL_FRONT_LEFT                               0x0400
#define GL_FRONT_RIGHT                              0x0401
#define GL_BACK_LEFT                                0x0402
#define GL_BACK_RIGHT                               0x0403

/* ---- FBO attachment query enums ---- */
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE       0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME       0x8CD1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL     0x8CD2
#define GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING    0x8210

/* ---- Color encoding values ---- */
#define GL_SRGB                                     0x8C40
#define GL_SRGB8                                    0x8C41
#define GL_SRGB8_ALPHA8                             0x8C43

#define GL_FRAMEBUFFER_COMPLETE                     0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT        0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_DEPTH_COMPONENT                          0x1902
#define GL_DEPTH_COMPONENT16                        0x81A5
#define GL_DEPTH_COMPONENT24                        0x81A6
#define GL_DEPTH_COMPONENT32F                       0x8CAC
#define GL_TEXTURE_2D                               0x0DE1
#define GL_TEXTURE_CUBE_MAP                         0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X              0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X              0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y              0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y              0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z              0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z              0x851A
#define GL_LINEAR_MIPMAP_LINEAR                     0x2703
#define GL_LINEAR_MIPMAP_NEAREST                    0x2701
#define GL_NEAREST_MIPMAP_LINEAR                    0x2702
#define GL_NEAREST_MIPMAP_NEAREST                   0x2700
#define GL_TEXTURE_MIN_FILTER                       0x2801
#define GL_TEXTURE_MAG_FILTER                       0x2800
#define GL_TEXTURE_MAX_LEVEL                        0x813D
#define GL_TEXTURE_BASE_LEVEL                       0x813C
#define GL_NEAREST                                  0x2600
#define GL_LINEAR                                   0x2601
#define GL_TEXTURE_WRAP_S                           0x2802
#define GL_TEXTURE_WRAP_T                           0x2803
#define GL_TEXTURE_WRAP_R                           0x8072
#define GL_CLAMP_TO_EDGE                            0x812F
#define GL_CLAMP_TO_BORDER                          0x812D
#define GL_REPEAT                                   0x2901
#define GL_MIRRORED_REPEAT                          0x8370
#define GL_TEXTURE_CUBE_MAP_SEAMLESS                0x884F
#define GL_TEXTURE_COMPARE_MODE                     0x884C
#define GL_TEXTURE_COMPARE_FUNC                     0x884D
#define GL_COMPARE_REF_TO_TEXTURE                   0x884E
#define GL_RGBA                                     0x1908
#define GL_RGB                                      0x1907
#define GL_RED                                      0x1903
#define GL_RG                                       0x8227
#define GL_R8                                       0x8229
#define GL_RG8                                      0x822B
#define GL_R16F                                     0x822D
#define GL_RG16F                                    0x822F
#define GL_RGBA16F                                  0x881A
#define GL_RGBA32F                                  0x8814
#define GL_R11F_G11F_B10F                           0x8C3A
#define GL_R32F                                     0x822E
#define GL_HALF_FLOAT                               0x140B
#define GL_TEXTURE0                                 0x84C0
#define GL_TEXTURE1                                 0x84C1
#define GL_TEXTURE2                                 0x84C2
#define GL_TEXTURE3                                 0x84C3
#define GL_TEXTURE4                                 0x84C4
#define GL_TEXTURE5                                 0x84C5
#define GL_TEXTURE6                                 0x84C6
#define GL_TEXTURE7                                 0x84C7
#define GL_TEXTURE8                                 0x84C8
#define GL_TEXTURE9                                 0x84C9
#define GL_TEXTURE10                                0x84CA
#define GL_TEXTURE11                                0x84CB
#define GL_TEXTURE12                                0x84CC
#define GL_TEXTURE13                                0x84CD
#define GL_TEXTURE14                                0x84CE
#define GL_TEXTURE15                                0x84CF
#define GL_TEXTURE16                                0x84D0
#define GL_TEXTURE17                                0x84D1
#define GL_TEXTURE18                                0x84D2
#define GL_TEXTURE19                                0x84D3
#define GL_TEXTURE20                                0x84D4
#define GL_TEXTURE21                                0x84D5
#define GL_TEXTURE22                                0x84D6
#define GL_TEXTURE23                                0x84D7
#define GL_TEXTURE24                                0x84D8
#define GL_TEXTURE25                                0x84D9
#define GL_TEXTURE26                                0x84DA
#define GL_TEXTURE27                                0x84DB
#define GL_TEXTURE28                                0x84DC
#define GL_TEXTURE29                                0x84DD
#define GL_TEXTURE30                                0x84DE
#define GL_TEXTURE31                                0x84DF
#define GL_COLOR_BUFFER_BIT                         0x00004000
#define GL_DEPTH_BUFFER_BIT                         0x00000100
#define GL_COLOR                                    0x1800
#define GL_STENCIL_BUFFER_BIT                       0x00000400
#define GL_BLEND                                    0x0BE2
#define GL_DEPTH_TEST                               0x0B71
#define GL_STENCIL_TEST                             0x0B90
#define GL_CULL_FACE                                0x0B44
#define GL_FRONT                                    0x0404
#define GL_BACK                                     0x0405
#define GL_CCW                                      0x0901
#define GL_CW                                       0x0900
#define GL_MAX_GEOMETRY_INPUT_COMPONENTS            0x9123
#define GL_MAX_GEOMETRY_OUTPUT_COMPONENTS           0x9124
#define GL_MAX_GEOMETRY_SHADER_INVOCATIONS          0x8E5A
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR              0x88FE
#define GL_QUERY_RESULT                             0x8866
#define GL_QUERY_RESULT_AVAILABLE                   0x8867
#define GL_SAMPLES_PASSED                           0x8914
#define GL_TIME_ELAPSED                             0x88BF
#define GL_TRANSFORM_FEEDBACK                       0x8E22
#define GL_INTERLEAVED_ATTRIBS                      0x8C8C
#define GL_SEPARATE_ATTRIBS                         0x8C8D
#define GL_FUNC_ADD                                 0x8006
#define GL_FUNC_SUBTRACT                            0x800A
#define GL_FUNC_REVERSE_SUBTRACT                    0x800B
#define GL_MAX_DRAW_BUFFERS                         0x8824
#define GL_DRAW_BUFFER0                             0x8825
#define GL_SAMPLER_BINDING                          0x8919
#define GL_NUM_EXTENSIONS                           0x821D
#define GL_VENDOR                                   0x1F00
#define GL_RENDERER                                 0x1F01
#define GL_VERSION                                  0x1F02

#define GL_READ_FRAMEBUFFER              0x8CA8
#define GL_DRAW_FRAMEBUFFER              0x8CA9
#define GL_RGBA8                         0x8058
#define GL_R32UI                         0x8236
#define GL_RED_INTEGER                   0x8D94

/* ---- UBO support ---- */
#define GL_BUFFER_SIZE                    0x8764
#define GL_UNIFORM_BUFFER                 0x8A11
#define GL_UNIFORM_BUFFER_BINDING         0x8A28
#define GL_UNIFORM_BLOCK_DATA_SIZE        0x8A3C
#define GL_UNIFORM_OFFSET                 0x8A3D
#define GL_UNIFORM_BLOCK_BINDING          0x8A3F
#define GL_INVALID_INDEX                  0xFFFFFFFFu

/* ---- State query pnames (for glGetIntegerv) ---- */
#define GL_ACTIVE_TEXTURE                 0x84E0
#define GL_CURRENT_PROGRAM                0x8B8D
#define GL_VERTEX_ARRAY_BINDING           0x85B5
#define GL_ARRAY_BUFFER_BINDING           0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING   0x8895
#define GL_FRAMEBUFFER_BINDING            0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING       0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING       0x8CA7
#define GL_BLEND_SRC_RGB                  0x80C9
#define GL_BLEND_DST_RGB                  0x80C8
#define GL_BLEND_SRC_ALPHA                0x80CB
#define GL_BLEND_DST_ALPHA                0x80CA
#define GL_BLEND_SRC                      0x0BE1
#define GL_BLEND_DST                      0x0BE0
#define GL_DEPTH_FUNC                     0x0B74
#define GL_DEPTH_WRITEMASK                0x0B72
#define GL_CULL_FACE_MODE                 0x0B45
#define GL_FRONT_FACE                     0x0B46
#define GL_VIEWPORT                       0x0BA2
#define GL_SCISSOR_BOX                    0x0C10

/* ---- Compute / SSBO (OpenGL 4.3) ---- */
#define GL_COMPUTE_SHADER                 0x91B9
#define GL_SHADER_STORAGE_BUFFER          0x90D2
#define GL_MAP_READ_BIT                   0x0001
#define GL_MAP_WRITE_BIT                  0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT       0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT      0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT         0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT         0x0020

/* glMemoryBarrier barrier bits (OpenGL 4.2+, subset relevant to this engine) */
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT        0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT              0x00000002
#define GL_UNIFORM_BARRIER_BIT                    0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT              0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT        0x00000020
#define GL_COMMAND_BARRIER_BIT                    0x00000040
#define GL_PIXEL_BUFFER_BARRIER_BIT               0x00000080
#define GL_TEXTURE_UPDATE_BARRIER_BIT             0x00000100
#define GL_BUFFER_UPDATE_BARRIER_BIT              0x00000200
#define GL_FRAMEBUFFER_BARRIER_BIT                0x00000400
#define GL_TRANSFORM_FEEDBACK_BARRIER_BIT         0x00000800
#define GL_ATOMIC_COUNTER_BARRIER_BIT             0x00001000
#define GL_SHADER_STORAGE_BARRIER_BIT             0x00002000
#define GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT       0x00004000
#define GL_QUERY_BUFFER_BARRIER_BIT               0x00008000
#define GL_ALL_BARRIER_BITS                       0xFFFFFFFFu

/* ---- Sync object enums ---- */
#define GL_SYNC_FLUSH_COMMANDS_BIT        0x00000001
#define GL_ALREADY_SIGNALED               0x911A
#define GL_CONDITION_SATISFIED            0x911C
#define GL_TIMEOUT_EXPIRED                0x911B
#define GL_WAIT_FAILED                    0x911D
#define GL_SYNC_GPU_COMMANDS_COMPLETE     0x9117
#define GL_TIMEOUT_IGNORED                0xFFFFFFFFFFFFFFFFull

/* ---------- Additional common enums ---------- */
#define GL_ZERO                                     0
#define GL_ONE                                      1
#define GL_SRC_COLOR                                0x0300
#define GL_ONE_MINUS_SRC_COLOR                      0x0301
#define GL_SRC_ALPHA                                0x0302
#define GL_ONE_MINUS_SRC_ALPHA                      0x0303
#define GL_DST_ALPHA                                0x0304
#define GL_ONE_MINUS_DST_ALPHA                      0x0305
#define GL_DST_COLOR                                0x0306
#define GL_ONE_MINUS_DST_COLOR                      0x0307
#define GL_SRC_ALPHA_SATURATE                       0x0308
#define GL_ALWAYS                                   0x0207
#define GL_EQUAL                                    0x0202
#define GL_NEVER                                    0x0200
#define GL_LESS                                     0x0201
#define GL_LEQUAL                                   0x0203
#define GL_GREATER                                  0x0204
#define GL_GEQUAL                                   0x0205
#define GL_NOTEQUAL                                 0x0206
#define GL_KEEP                                     0x1E00
#define GL_REPLACE                                  0x1E01
#define GL_INCR                                     0x1E02
#define GL_DECR                                     0x1E03
#define GL_INVERT                                   0x150A
#define GL_INCR_WRAP                                0x8507
#define GL_DECR_WRAP                                0x8508
#define GL_FRONT_AND_BACK                           0x0408
#define GL_FILL                                     0x1B02
#define GL_LINE                                     0x1B01
#define GL_POINT                                    0x1B00
#define GL_POLYGON_OFFSET_FILL                      0x8037
#define GL_POLYGON_OFFSET_LINE                      0x8038
#define GL_POLYGON_OFFSET_POINT                     0x8039

/* ========================================================================
   OPENGL 3.3 + 4.3 - FUNCTION POINTER TYPES
   ======================================================================== */

/* 1.0 / 1.1 */
typedef void (C89GL_APIENTRY *C89GL_PFN_glClear)(unsigned int mask);
typedef void (C89GL_APIENTRY *C89GL_PFN_glClearColor)(float r, float g, float b, float a);
typedef void (C89GL_APIENTRY *C89GL_PFN_glViewport)(int x, int y, int width, int height);
typedef void (C89GL_APIENTRY *C89GL_PFN_glEnable)(unsigned int cap);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDisable)(unsigned int cap);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDepthFunc)(unsigned int func);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBlendFunc)(unsigned int sfactor, unsigned int dfactor);
typedef void (C89GL_APIENTRY *C89GL_PFN_glCullFace)(unsigned int mode);
typedef void (C89GL_APIENTRY *C89GL_PFN_glFrontFace)(unsigned int mode);
typedef void (C89GL_APIENTRY *C89GL_PFN_glScissor)(int x, int y, int width, int height);
typedef void (C89GL_APIENTRY *C89GL_PFN_glPolygonMode)(unsigned int face, unsigned int mode);
typedef const unsigned char* (C89GL_APIENTRY *C89GL_PFN_glGetString)(unsigned int name);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetIntegerv)(unsigned int pname, int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetFloatv)(unsigned int pname, float* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetBooleanv)(unsigned int pname, unsigned char* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glFlush)(void);
typedef void (C89GL_APIENTRY *C89GL_PFN_glFinish)(void);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDepthRange)(double near, double far);
typedef void (C89GL_APIENTRY *C89GL_PFN_glClearDepth)(double depth);
typedef void (C89GL_APIENTRY *C89GL_PFN_glPolygonOffset)(float factor, float units);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBlendEquation)(unsigned int mode);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBlendColor)(float r, float g, float b, float a);
typedef void (C89GL_APIENTRY *C89GL_PFN_glStencilFunc)(unsigned int func, int ref, unsigned int mask);
typedef void (C89GL_APIENTRY *C89GL_PFN_glStencilOp)(unsigned int sfail, unsigned int dpfail, unsigned int dppass);
typedef void (C89GL_APIENTRY *C89GL_PFN_glStencilMask)(unsigned int mask);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDepthMask)(unsigned char flag);
typedef void (C89GL_APIENTRY *C89GL_PFN_glColorMask)(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawBuffer)(unsigned int buf);
typedef void (C89GL_APIENTRY *C89GL_PFN_glReadBuffer)(unsigned int src);

/* 1.0/1.1 ReadPixels */
typedef void (C89GL_APIENTRY *C89GL_PFN_glReadPixels)(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels);

/* 1.5 Buffers */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenBuffers)(int n, unsigned int* buffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteBuffers)(int n, const unsigned int* buffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindBuffer)(unsigned int target, unsigned int buffer);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBufferData)(unsigned int target, long long size, const void* data, unsigned int usage);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBufferSubData)(unsigned int target, long long offset, long long size, const void* data);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetBufferSubData)(unsigned int target, long long offset, long long size, void* data);
typedef void* (C89GL_APIENTRY *C89GL_PFN_glMapBuffer)(unsigned int target, unsigned int access);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUnmapBuffer)(unsigned int target);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetBufferParameteriv)(unsigned int target, unsigned int pname, int* params);

/* 3.0 MapBufferRange */
typedef void* (C89GL_APIENTRY *C89GL_PFN_glMapBufferRange)(unsigned int target, GLintptr offset, GLsizeiptr length, GLbitfield access);

/* 2.0 Shaders & Uniforms */
typedef unsigned int (C89GL_APIENTRY *C89GL_PFN_glCreateProgram)(void);
typedef unsigned int (C89GL_APIENTRY *C89GL_PFN_glCreateShader)(unsigned int type);
typedef void (C89GL_APIENTRY *C89GL_PFN_glShaderSource)(unsigned int shader, int count, const char** string, const int* length);
typedef void (C89GL_APIENTRY *C89GL_PFN_glCompileShader)(unsigned int shader);
typedef void (C89GL_APIENTRY *C89GL_PFN_glAttachShader)(unsigned int program, unsigned int shader);
typedef void (C89GL_APIENTRY *C89GL_PFN_glLinkProgram)(unsigned int program);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUseProgram)(unsigned int program);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteShader)(unsigned int shader);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteProgram)(unsigned int program);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDetachShader)(unsigned int program, unsigned int shader);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetShaderiv)(unsigned int shader, unsigned int pname, int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetProgramiv)(unsigned int program, unsigned int pname, int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetShaderInfoLog)(unsigned int shader, int bufSize, int* length, char* infoLog);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetProgramInfoLog)(unsigned int program, int bufSize, int* length, char* infoLog);
typedef int (C89GL_APIENTRY *C89GL_PFN_glGetUniformLocation)(unsigned int program, const char* name);

/* ---- Scalar uniform setters ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1f)(int location, float v0);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2f)(int location, float v0, float v1);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3f)(int location, float v0, float v1, float v2);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4f)(int location, float v0, float v1, float v2, float v3);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1i)(int location, int v0);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2i)(int location, int v0, int v1);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3i)(int location, int v0, int v1, int v2);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4i)(int location, int v0, int v1, int v2, int v3);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1ui)(int location, unsigned int v0);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2ui)(int location, unsigned int v0, unsigned int v1);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3ui)(int location, unsigned int v0, unsigned int v1, unsigned int v2);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4ui)(int location, unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3);

/* ---- Vector uniform setters ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1fv)(int location, int count, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2fv)(int location, int count, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3fv)(int location, int count, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4fv)(int location, int count, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1iv)(int location, int count, const int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2iv)(int location, int count, const int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3iv)(int location, int count, const int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4iv)(int location, int count, const int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform1uiv)(int location, int count, const unsigned int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform2uiv)(int location, int count, const unsigned int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform3uiv)(int location, int count, const unsigned int* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniform4uiv)(int location, int count, const unsigned int* value);

/* ---- Matrix uniform setters ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix2fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix3fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix4fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix2x3fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix3x2fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix2x4fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix4x2fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix3x4fv)(int location, int count, unsigned char transpose, const float* value);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformMatrix4x3fv)(int location, int count, unsigned char transpose, const float* value);

typedef void (C89GL_APIENTRY *C89GL_PFN_glVertexAttribPointer)(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void* pointer);
typedef void (C89GL_APIENTRY *C89GL_PFN_glEnableVertexAttribArray)(unsigned int index);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDisableVertexAttribArray)(unsigned int index);

/* ---- Generic vertex attribute constants (GL 2.0) ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glVertexAttrib1f)(unsigned int index, float v0);

/* 3.0 Vertex Arrays, Framebuffers, Draw */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenVertexArrays)(int n, unsigned int* arrays);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteVertexArrays)(int n, const unsigned int* arrays);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindVertexArray)(unsigned int array);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenFramebuffers)(int n, unsigned int* framebuffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteFramebuffers)(int n, const unsigned int* framebuffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindFramebuffer)(unsigned int target, unsigned int framebuffer);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenRenderbuffers)(int n, unsigned int* renderbuffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteRenderbuffers)(int n, const unsigned int* renderbuffers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindRenderbuffer)(unsigned int target, unsigned int renderbuffer);
typedef void (C89GL_APIENTRY *C89GL_PFN_glRenderbufferStorage)(unsigned int target, unsigned int internalformat, int width, int height);
typedef void (C89GL_APIENTRY *C89GL_PFN_glRenderbufferStorageMultisample)(unsigned int target, int samples, unsigned int internalformat, int width, int height);
typedef void (C89GL_APIENTRY *C89GL_PFN_glFramebufferTexture2D)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
typedef void (C89GL_APIENTRY *C89GL_PFN_glFramebufferRenderbuffer)(unsigned int target, unsigned int attachment, unsigned int renderbuffertarget, unsigned int renderbuffer);
typedef unsigned int (C89GL_APIENTRY *C89GL_PFN_glCheckFramebufferStatus)(unsigned int target);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetFramebufferAttachmentParameteriv)(unsigned int target, unsigned int attachment, unsigned int pname, int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBlitFramebuffer)(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask, unsigned int filter);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawArrays)(unsigned int mode, int first, int count);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawElements)(unsigned int mode, int count, unsigned int type, const void* indices);

/* 3.0 FragData & Multiple Render Targets */
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindFragDataLocation)(unsigned int program, unsigned int colorNumber, const char* name);
typedef int (C89GL_APIENTRY *C89GL_PFN_glGetFragDataLocation)(unsigned int program, const char* name);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawBuffers)(GLsizei n, const GLenum *bufs);
typedef void (C89GL_APIENTRY *C89GL_PFN_glClearBufferfv)(GLenum buffer, GLint drawbuffer, const GLfloat *value);

/* 3.1 Uniform Blocks, BaseVertex */
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawElementsBaseVertex)(unsigned int mode, int count, unsigned int type, const void* indices, int basevertex);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetActiveUniformBlockiv)(unsigned int program, unsigned int uniformBlockIndex, unsigned int pname, int* params);
typedef unsigned int (C89GL_APIENTRY *C89GL_PFN_glGetUniformBlockIndex)(unsigned int program, const char* uniformBlockName);
typedef void (C89GL_APIENTRY *C89GL_PFN_glUniformBlockBinding)(unsigned int program, unsigned int uniformBlockIndex, unsigned int uniformBlockBinding);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetUniformIndices)(unsigned int program, int count, const char** names, unsigned int* indices);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetActiveUniformsiv)(unsigned int program, int count, const unsigned int* indices, unsigned int pname, int* params);

/* 3.2 Geometry Shader query */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetIntegeri_v)(unsigned int target, unsigned int index, int* data);

/* 3.3 Instancing */
typedef void (C89GL_APIENTRY *C89GL_PFN_glVertexAttribDivisor)(unsigned int index, unsigned int divisor);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawArraysInstanced)(unsigned int mode, int first, int count, int instancecount);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDrawElementsInstanced)(unsigned int mode, int count, unsigned int type, const void* indices, int instancecount);
typedef const unsigned char* (C89GL_APIENTRY *C89GL_PFN_glGetStringi)(unsigned int name, unsigned int index);

/* 3.3 Sampler Objects */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenSamplers)(int count, unsigned int* samplers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteSamplers)(int count, const unsigned int* samplers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindSampler)(unsigned int unit, unsigned int sampler);
typedef void (C89GL_APIENTRY *C89GL_PFN_glSamplerParameteri)(unsigned int sampler, unsigned int pname, int param);
typedef void (C89GL_APIENTRY *C89GL_PFN_glSamplerParameterf)(unsigned int sampler, unsigned int pname, float param);
typedef void (C89GL_APIENTRY *C89GL_PFN_glSamplerParameteriv)(unsigned int sampler, unsigned int pname, const int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glSamplerParameterfv)(unsigned int sampler, unsigned int pname, const float* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetSamplerParameteriv)(unsigned int sampler, unsigned int pname, int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetSamplerParameterfv)(unsigned int sampler, unsigned int pname, float* params);

/* 3.3 Textures */
typedef void (C89GL_APIENTRY *C89GL_PFN_glActiveTexture)(unsigned int texture);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenTextures)(int n, unsigned int* textures);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteTextures)(int n, const unsigned int* textures);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindTexture)(unsigned int target, unsigned int texture);
typedef void (C89GL_APIENTRY *C89GL_PFN_glTexParameteri)(unsigned int target, unsigned int pname, int param);
typedef void (C89GL_APIENTRY *C89GL_PFN_glTexParameterf)(unsigned int target, unsigned int pname, float param);
typedef void (C89GL_APIENTRY *C89GL_PFN_glTexImage2D)(unsigned int target, int level, int internalformat, int width, int height, int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (C89GL_APIENTRY *C89GL_PFN_glTexSubImage2D)(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, unsigned int type, const void* pixels);
typedef void (C89GL_APIENTRY *C89GL_PFN_glCopyTexSubImage2D)(unsigned int target, int level, int xoffset, int yoffset, int x, int y, int width, int height);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenerateMipmap)(unsigned int target);

/* 1.5 / 3.3 Queries */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenQueries)(int n, unsigned int* ids);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteQueries)(int n, const unsigned int* ids);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBeginQuery)(unsigned int target, unsigned int id);
typedef void (C89GL_APIENTRY *C89GL_PFN_glEndQuery)(unsigned int target);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetQueryObjectuiv)(unsigned int id, unsigned int pname, unsigned int* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glGetQueryObjecti64v)(unsigned int id, unsigned int pname, long long* params);
typedef void (C89GL_APIENTRY *C89GL_PFN_glQueryCounter)(unsigned int id, unsigned int target);

/* 3.2 Sync Objects */
typedef GLsync (C89GL_APIENTRY *C89GL_PFN_glFenceSync)(unsigned int condition, unsigned int flags);
typedef int (C89GL_APIENTRY *C89GL_PFN_glClientWaitSync)(GLsync sync, unsigned int flags, unsigned long long timeout);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteSync)(GLsync sync);

/* 3.0 Transform Feedback */
typedef void (C89GL_APIENTRY *C89GL_PFN_glGenTransformFeedbacks)(int n, unsigned int* ids);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDeleteTransformFeedbacks)(int n, const unsigned int* ids);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindTransformFeedback)(unsigned int target, unsigned int id);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBeginTransformFeedback)(unsigned int primitiveMode);
typedef void (C89GL_APIENTRY *C89GL_PFN_glEndTransformFeedback)(void);
typedef void (C89GL_APIENTRY *C89GL_PFN_glTransformFeedbackVaryings)(unsigned int program, int count, const char** varyings, unsigned int bufferMode);

/* ---- UBO ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindBufferBase)(unsigned int target, unsigned int index, unsigned int buffer);

/* ---- OpenGL 4.0 Per‑attachment blending ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glBlendFunci)(GLuint buf, GLenum sfactor, GLenum dfactor);

/* ---- OpenGL 4.3 Compute / SSBO ---- */
typedef void (C89GL_APIENTRY *C89GL_PFN_glMemoryBarrier)(unsigned int barriers);
typedef void (C89GL_APIENTRY *C89GL_PFN_glDispatchCompute)(unsigned int num_groups_x, unsigned int num_groups_y, unsigned int num_groups_z);
typedef void (C89GL_APIENTRY *C89GL_PFN_glBindImageTexture)(unsigned int unit, unsigned int texture, int level, unsigned char layered, int layer, unsigned int access, unsigned int format);
typedef void (C89GL_APIENTRY *C89GL_PFN_glClearBufferSubData)(GLenum target, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data);
typedef void (C89GL_APIENTRY *C89GL_PFN_glClearBufferData)(GLenum target, GLenum internalformat, GLenum format, GLenum type, const void *data);

/* ========================================================================
   GLOBAL FUNCTION POINTERS
   ======================================================================== */

/* 1.0 / 1.1 */
extern C89GL_PFN_glClear C89GL_glClear;
extern C89GL_PFN_glClearColor C89GL_glClearColor;
extern C89GL_PFN_glViewport C89GL_glViewport;
extern C89GL_PFN_glEnable C89GL_glEnable;
extern C89GL_PFN_glDisable C89GL_glDisable;
extern C89GL_PFN_glDepthFunc C89GL_glDepthFunc;
extern C89GL_PFN_glBlendFunc C89GL_glBlendFunc;
extern C89GL_PFN_glCullFace C89GL_glCullFace;
extern C89GL_PFN_glFrontFace C89GL_glFrontFace;
extern C89GL_PFN_glScissor C89GL_glScissor;
extern C89GL_PFN_glPolygonMode C89GL_glPolygonMode;
extern C89GL_PFN_glGetString C89GL_glGetString;
extern C89GL_PFN_glGetIntegerv C89GL_glGetIntegerv;
extern C89GL_PFN_glGetFloatv C89GL_glGetFloatv;
extern C89GL_PFN_glGetBooleanv C89GL_glGetBooleanv;
extern C89GL_PFN_glFlush C89GL_glFlush;
extern C89GL_PFN_glFinish C89GL_glFinish;
extern C89GL_PFN_glDepthRange C89GL_glDepthRange;
extern C89GL_PFN_glClearDepth C89GL_glClearDepth;
extern C89GL_PFN_glPolygonOffset C89GL_glPolygonOffset;
extern C89GL_PFN_glBlendEquation C89GL_glBlendEquation;
extern C89GL_PFN_glBlendColor C89GL_glBlendColor;
extern C89GL_PFN_glStencilFunc C89GL_glStencilFunc;
extern C89GL_PFN_glStencilOp C89GL_glStencilOp;
extern C89GL_PFN_glStencilMask C89GL_glStencilMask;
extern C89GL_PFN_glDepthMask C89GL_glDepthMask;
extern C89GL_PFN_glColorMask C89GL_glColorMask;
extern C89GL_PFN_glDrawBuffer C89GL_glDrawBuffer;
extern C89GL_PFN_glReadBuffer C89GL_glReadBuffer;

/* 1.0/1.1 ReadPixels */
extern C89GL_PFN_glReadPixels C89GL_glReadPixels;

/* 1.5 */
extern C89GL_PFN_glGenBuffers C89GL_glGenBuffers;
extern C89GL_PFN_glDeleteBuffers C89GL_glDeleteBuffers;
extern C89GL_PFN_glBindBuffer C89GL_glBindBuffer;
extern C89GL_PFN_glBufferData C89GL_glBufferData;
extern C89GL_PFN_glBufferSubData C89GL_glBufferSubData;
extern C89GL_PFN_glGetBufferSubData C89GL_glGetBufferSubData;
extern C89GL_PFN_glMapBuffer C89GL_glMapBuffer;
extern C89GL_PFN_glUnmapBuffer C89GL_glUnmapBuffer;
extern C89GL_PFN_glGetBufferParameteriv C89GL_glGetBufferParameteriv;

/* 3.0 MapBufferRange */
extern C89GL_PFN_glMapBufferRange C89GL_glMapBufferRange;

/* 2.0 */
extern C89GL_PFN_glCreateProgram C89GL_glCreateProgram;
extern C89GL_PFN_glCreateShader C89GL_glCreateShader;
extern C89GL_PFN_glShaderSource C89GL_glShaderSource;
extern C89GL_PFN_glCompileShader C89GL_glCompileShader;
extern C89GL_PFN_glAttachShader C89GL_glAttachShader;
extern C89GL_PFN_glLinkProgram C89GL_glLinkProgram;
extern C89GL_PFN_glUseProgram C89GL_glUseProgram;
extern C89GL_PFN_glDeleteShader C89GL_glDeleteShader;
extern C89GL_PFN_glDeleteProgram C89GL_glDeleteProgram;
extern C89GL_PFN_glDetachShader C89GL_glDetachShader;
extern C89GL_PFN_glGetShaderiv C89GL_glGetShaderiv;
extern C89GL_PFN_glGetProgramiv C89GL_glGetProgramiv;
extern C89GL_PFN_glGetShaderInfoLog C89GL_glGetShaderInfoLog;
extern C89GL_PFN_glGetProgramInfoLog C89GL_glGetProgramInfoLog;
extern C89GL_PFN_glGetUniformLocation C89GL_glGetUniformLocation;

/* ---- Scalar uniform setters ---- */
extern C89GL_PFN_glUniform1f C89GL_glUniform1f;
extern C89GL_PFN_glUniform2f C89GL_glUniform2f;
extern C89GL_PFN_glUniform3f C89GL_glUniform3f;
extern C89GL_PFN_glUniform4f C89GL_glUniform4f;
extern C89GL_PFN_glUniform1i C89GL_glUniform1i;
extern C89GL_PFN_glUniform2i C89GL_glUniform2i;
extern C89GL_PFN_glUniform3i C89GL_glUniform3i;
extern C89GL_PFN_glUniform4i C89GL_glUniform4i;
extern C89GL_PFN_glUniform1ui C89GL_glUniform1ui;
extern C89GL_PFN_glUniform2ui C89GL_glUniform2ui;
extern C89GL_PFN_glUniform3ui C89GL_glUniform3ui;
extern C89GL_PFN_glUniform4ui C89GL_glUniform4ui;

/* ---- Vector uniform setters ---- */
extern C89GL_PFN_glUniform1fv C89GL_glUniform1fv;
extern C89GL_PFN_glUniform2fv C89GL_glUniform2fv;
extern C89GL_PFN_glUniform3fv C89GL_glUniform3fv;
extern C89GL_PFN_glUniform4fv C89GL_glUniform4fv;
extern C89GL_PFN_glUniform1iv C89GL_glUniform1iv;
extern C89GL_PFN_glUniform2iv C89GL_glUniform2iv;
extern C89GL_PFN_glUniform3iv C89GL_glUniform3iv;
extern C89GL_PFN_glUniform4iv C89GL_glUniform4iv;
extern C89GL_PFN_glUniform1uiv C89GL_glUniform1uiv;
extern C89GL_PFN_glUniform2uiv C89GL_glUniform2uiv;
extern C89GL_PFN_glUniform3uiv C89GL_glUniform3uiv;
extern C89GL_PFN_glUniform4uiv C89GL_glUniform4uiv;

/* ---- Matrix uniform setters ---- */
extern C89GL_PFN_glUniformMatrix2fv C89GL_glUniformMatrix2fv;
extern C89GL_PFN_glUniformMatrix3fv C89GL_glUniformMatrix3fv;
extern C89GL_PFN_glUniformMatrix4fv C89GL_glUniformMatrix4fv;
extern C89GL_PFN_glUniformMatrix2x3fv C89GL_glUniformMatrix2x3fv;
extern C89GL_PFN_glUniformMatrix3x2fv C89GL_glUniformMatrix3x2fv;
extern C89GL_PFN_glUniformMatrix2x4fv C89GL_glUniformMatrix2x4fv;
extern C89GL_PFN_glUniformMatrix4x2fv C89GL_glUniformMatrix4x2fv;
extern C89GL_PFN_glUniformMatrix3x4fv C89GL_glUniformMatrix3x4fv;
extern C89GL_PFN_glUniformMatrix4x3fv C89GL_glUniformMatrix4x3fv;

extern C89GL_PFN_glVertexAttribPointer C89GL_glVertexAttribPointer;
extern C89GL_PFN_glEnableVertexAttribArray C89GL_glEnableVertexAttribArray;
extern C89GL_PFN_glDisableVertexAttribArray C89GL_glDisableVertexAttribArray;
extern C89GL_PFN_glVertexAttrib1f C89GL_glVertexAttrib1f;

/* 3.0 */
extern C89GL_PFN_glGenVertexArrays C89GL_glGenVertexArrays;
extern C89GL_PFN_glDeleteVertexArrays C89GL_glDeleteVertexArrays;
extern C89GL_PFN_glBindVertexArray C89GL_glBindVertexArray;
extern C89GL_PFN_glGenFramebuffers C89GL_glGenFramebuffers;
extern C89GL_PFN_glDeleteFramebuffers C89GL_glDeleteFramebuffers;
extern C89GL_PFN_glBindFramebuffer C89GL_glBindFramebuffer;
extern C89GL_PFN_glGenRenderbuffers C89GL_glGenRenderbuffers;
extern C89GL_PFN_glDeleteRenderbuffers C89GL_glDeleteRenderbuffers;
extern C89GL_PFN_glBindRenderbuffer C89GL_glBindRenderbuffer;
extern C89GL_PFN_glRenderbufferStorage C89GL_glRenderbufferStorage;
extern C89GL_PFN_glRenderbufferStorageMultisample C89GL_glRenderbufferStorageMultisample;
extern C89GL_PFN_glFramebufferTexture2D C89GL_glFramebufferTexture2D;
extern C89GL_PFN_glFramebufferRenderbuffer C89GL_glFramebufferRenderbuffer;
extern C89GL_PFN_glCheckFramebufferStatus C89GL_glCheckFramebufferStatus;
extern C89GL_PFN_glGetFramebufferAttachmentParameteriv C89GL_glGetFramebufferAttachmentParameteriv;
extern C89GL_PFN_glBlitFramebuffer C89GL_glBlitFramebuffer;
extern C89GL_PFN_glDrawArrays C89GL_glDrawArrays;
extern C89GL_PFN_glDrawElements C89GL_glDrawElements;
extern C89GL_PFN_glBindFragDataLocation C89GL_glBindFragDataLocation;
extern C89GL_PFN_glGetFragDataLocation C89GL_glGetFragDataLocation;
extern C89GL_PFN_glDrawBuffers C89GL_glDrawBuffers;
extern C89GL_PFN_glClearBufferfv C89GL_glClearBufferfv;

/* 3.1 */
extern C89GL_PFN_glDrawElementsBaseVertex C89GL_glDrawElementsBaseVertex;
extern C89GL_PFN_glGetActiveUniformBlockiv C89GL_glGetActiveUniformBlockiv;
extern C89GL_PFN_glGetUniformBlockIndex C89GL_glGetUniformBlockIndex;
extern C89GL_PFN_glUniformBlockBinding C89GL_glUniformBlockBinding;
extern C89GL_PFN_glGetUniformIndices C89GL_glGetUniformIndices;
extern C89GL_PFN_glGetActiveUniformsiv C89GL_glGetActiveUniformsiv;

/* 3.2 */
extern C89GL_PFN_glGetIntegeri_v C89GL_glGetIntegeri_v;

/* 3.3 */
extern C89GL_PFN_glVertexAttribDivisor C89GL_glVertexAttribDivisor;
extern C89GL_PFN_glDrawArraysInstanced C89GL_glDrawArraysInstanced;
extern C89GL_PFN_glDrawElementsInstanced C89GL_glDrawElementsInstanced;
extern C89GL_PFN_glGetStringi C89GL_glGetStringi;
extern C89GL_PFN_glGenSamplers C89GL_glGenSamplers;
extern C89GL_PFN_glDeleteSamplers C89GL_glDeleteSamplers;
extern C89GL_PFN_glBindSampler C89GL_glBindSampler;
extern C89GL_PFN_glSamplerParameteri C89GL_glSamplerParameteri;
extern C89GL_PFN_glSamplerParameterf C89GL_glSamplerParameterf;
extern C89GL_PFN_glSamplerParameteriv C89GL_glSamplerParameteriv;
extern C89GL_PFN_glSamplerParameterfv C89GL_glSamplerParameterfv;
extern C89GL_PFN_glGetSamplerParameteriv C89GL_glGetSamplerParameteriv;
extern C89GL_PFN_glGetSamplerParameterfv C89GL_glGetSamplerParameterfv;
extern C89GL_PFN_glActiveTexture C89GL_glActiveTexture;
extern C89GL_PFN_glGenTextures C89GL_glGenTextures;
extern C89GL_PFN_glDeleteTextures C89GL_glDeleteTextures;
extern C89GL_PFN_glBindTexture C89GL_glBindTexture;
extern C89GL_PFN_glTexParameteri C89GL_glTexParameteri;
extern C89GL_PFN_glTexParameterf C89GL_glTexParameterf;
extern C89GL_PFN_glTexImage2D C89GL_glTexImage2D;
extern C89GL_PFN_glTexSubImage2D C89GL_glTexSubImage2D;
extern C89GL_PFN_glCopyTexSubImage2D C89GL_glCopyTexSubImage2D;
extern C89GL_PFN_glGenerateMipmap C89GL_glGenerateMipmap;
extern C89GL_PFN_glGenQueries C89GL_glGenQueries;
extern C89GL_PFN_glDeleteQueries C89GL_glDeleteQueries;
extern C89GL_PFN_glBeginQuery C89GL_glBeginQuery;
extern C89GL_PFN_glEndQuery C89GL_glEndQuery;
extern C89GL_PFN_glGetQueryObjectuiv C89GL_glGetQueryObjectuiv;
extern C89GL_PFN_glGetQueryObjecti64v C89GL_glGetQueryObjecti64v;
extern C89GL_PFN_glQueryCounter C89GL_glQueryCounter;
extern C89GL_PFN_glFenceSync C89GL_glFenceSync;
extern C89GL_PFN_glClientWaitSync C89GL_glClientWaitSync;
extern C89GL_PFN_glDeleteSync C89GL_glDeleteSync;
extern C89GL_PFN_glGenTransformFeedbacks C89GL_glGenTransformFeedbacks;
extern C89GL_PFN_glDeleteTransformFeedbacks C89GL_glDeleteTransformFeedbacks;
extern C89GL_PFN_glBindTransformFeedback C89GL_glBindTransformFeedback;
extern C89GL_PFN_glBeginTransformFeedback C89GL_glBeginTransformFeedback;
extern C89GL_PFN_glEndTransformFeedback C89GL_glEndTransformFeedback;
extern C89GL_PFN_glTransformFeedbackVaryings C89GL_glTransformFeedbackVaryings;

/* UBO */
extern C89GL_PFN_glBindBufferBase C89GL_glBindBufferBase;

/* OpenGL 4.0 Per‑attachment blending */
extern C89GL_PFN_glBlendFunci C89GL_glBlendFunci;

/* OpenGL 4.3 Compute / SSBO */
extern C89GL_PFN_glMemoryBarrier C89GL_glMemoryBarrier;
extern C89GL_PFN_glDispatchCompute C89GL_glDispatchCompute;
extern C89GL_PFN_glBindImageTexture C89GL_glBindImageTexture;
extern C89GL_PFN_glClearBufferSubData C89GL_glClearBufferSubData;
extern C89GL_PFN_glClearBufferData C89GL_glClearBufferData;

/* ========================================================================
   CONTEXT STRUCTURE
   ======================================================================== */
typedef struct C89GL_Context {
#if defined(C89FW_WINDOWS)
    void* hdc;
    void* glrc;
#elif defined(C89FW_LINUX)
    void* display;
    unsigned long window;
    void* glx_ctx;
#elif defined(C89FW_MACOS)
    void* ns_context;
#endif
    int initialized;
} C89GL_Context;

/* ========================================================================
   PUBLIC API
   ======================================================================== */
int  C89GL_create_context(C89FW_window_t* window, C89GL_Context* ctx);
void C89GL_destroy_context(C89GL_Context* ctx);
void C89GL_make_current(C89GL_Context* ctx);
void C89GL_swap_buffers(C89GL_Context* ctx);
int  C89GL_load_functions(void);

#ifdef __cplusplus
}
#endif

#endif /* C89GL_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#ifdef C89GL_IMPLEMENTATION

/* ---------- Platform-specific includes ---------- */
#if defined(C89FW_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    typedef HGLRC (C89GL_APIENTRY* C89GL_PFN_wglCreateContextAttribsARB)(HDC hdc, HGLRC share, const int* attribs);
#elif defined(C89FW_LINUX)
    #include <X11/Xlib.h>
    #include <GL/glx.h>
    typedef GLXContext (C89GL_APIENTRY* C89GL_PFN_glXCreateContextAttribsARB)(Display* dpy, GLXFBConfig config, GLXContext share, Bool direct, const int* attribs);
    typedef GLXFBConfig* (C89GL_APIENTRY* C89GL_PFN_glXChooseFBConfig)(Display* dpy, int screen, const int* attribs, int* nelements);
#elif defined(C89FW_MACOS)
    #include <Cocoa/Cocoa.h>
    #include <dlfcn.h>
    #ifndef NSOpenGLProfileVersion3_2Core
        #define NSOpenGLProfileVersion3_2Core 0x3200
    #endif
#endif

/* ---------- Function Loader ---------- */
static void* C89GL_load_proc(const char* name) {
#if defined(C89FW_WINDOWS)
    void* ptr = (void*)wglGetProcAddress(name);
    if (!ptr) {
        static HMODULE opengl32 = NULL;
        if (!opengl32) opengl32 = LoadLibraryA("opengl32.dll");
        if (opengl32) ptr = (void*)GetProcAddress(opengl32, name);
    }
    return ptr;
#elif defined(C89FW_LINUX)
    return (void*)glXGetProcAddressARB((const GLubyte*)name);
#elif defined(C89FW_MACOS)
    return dlsym(RTLD_DEFAULT, name);
#endif
}

#define C89GL_LOAD_FUNC(var, name) \
    do { var = (void*)C89GL_load_proc(name); if (!var) return 0; } while (0)

/* ---------- Function Pointer Definitions ---------- */

/* 1.0 / 1.1 */
C89GL_PFN_glClear C89GL_glClear = NULL;
C89GL_PFN_glClearColor C89GL_glClearColor = NULL;
C89GL_PFN_glViewport C89GL_glViewport = NULL;
C89GL_PFN_glEnable C89GL_glEnable = NULL;
C89GL_PFN_glDisable C89GL_glDisable = NULL;
C89GL_PFN_glDepthFunc C89GL_glDepthFunc = NULL;
C89GL_PFN_glBlendFunc C89GL_glBlendFunc = NULL;
C89GL_PFN_glCullFace C89GL_glCullFace = NULL;
C89GL_PFN_glFrontFace C89GL_glFrontFace = NULL;
C89GL_PFN_glScissor C89GL_glScissor = NULL;
C89GL_PFN_glPolygonMode C89GL_glPolygonMode = NULL;
C89GL_PFN_glGetString C89GL_glGetString = NULL;
C89GL_PFN_glGetIntegerv C89GL_glGetIntegerv = NULL;
C89GL_PFN_glGetFloatv C89GL_glGetFloatv = NULL;
C89GL_PFN_glGetBooleanv C89GL_glGetBooleanv = NULL;
C89GL_PFN_glFlush C89GL_glFlush = NULL;
C89GL_PFN_glFinish C89GL_glFinish = NULL;
C89GL_PFN_glDepthRange C89GL_glDepthRange = NULL;
C89GL_PFN_glClearDepth C89GL_glClearDepth = NULL;
C89GL_PFN_glPolygonOffset C89GL_glPolygonOffset = NULL;
C89GL_PFN_glBlendEquation C89GL_glBlendEquation = NULL;
C89GL_PFN_glBlendColor C89GL_glBlendColor = NULL;
C89GL_PFN_glStencilFunc C89GL_glStencilFunc = NULL;
C89GL_PFN_glStencilOp C89GL_glStencilOp = NULL;
C89GL_PFN_glStencilMask C89GL_glStencilMask = NULL;
C89GL_PFN_glDepthMask C89GL_glDepthMask = NULL;
C89GL_PFN_glColorMask C89GL_glColorMask = NULL;
C89GL_PFN_glDrawBuffer C89GL_glDrawBuffer = NULL;
C89GL_PFN_glReadBuffer C89GL_glReadBuffer = NULL;

C89GL_PFN_glReadPixels C89GL_glReadPixels = NULL;

/* 1.5 */
C89GL_PFN_glGenBuffers C89GL_glGenBuffers = NULL;
C89GL_PFN_glDeleteBuffers C89GL_glDeleteBuffers = NULL;
C89GL_PFN_glBindBuffer C89GL_glBindBuffer = NULL;
C89GL_PFN_glBufferData C89GL_glBufferData = NULL;
C89GL_PFN_glBufferSubData C89GL_glBufferSubData = NULL;
C89GL_PFN_glGetBufferSubData C89GL_glGetBufferSubData = NULL;
C89GL_PFN_glMapBuffer C89GL_glMapBuffer = NULL;
C89GL_PFN_glUnmapBuffer C89GL_glUnmapBuffer = NULL;
C89GL_PFN_glGetBufferParameteriv C89GL_glGetBufferParameteriv = NULL;

/* 3.0 MapBufferRange */
C89GL_PFN_glMapBufferRange C89GL_glMapBufferRange = NULL;

/* 2.0 */
C89GL_PFN_glCreateProgram C89GL_glCreateProgram = NULL;
C89GL_PFN_glCreateShader C89GL_glCreateShader = NULL;
C89GL_PFN_glShaderSource C89GL_glShaderSource = NULL;
C89GL_PFN_glCompileShader C89GL_glCompileShader = NULL;
C89GL_PFN_glAttachShader C89GL_glAttachShader = NULL;
C89GL_PFN_glLinkProgram C89GL_glLinkProgram = NULL;
C89GL_PFN_glUseProgram C89GL_glUseProgram = NULL;
C89GL_PFN_glDeleteShader C89GL_glDeleteShader = NULL;
C89GL_PFN_glDeleteProgram C89GL_glDeleteProgram = NULL;
C89GL_PFN_glDetachShader C89GL_glDetachShader = NULL;
C89GL_PFN_glGetShaderiv C89GL_glGetShaderiv = NULL;
C89GL_PFN_glGetProgramiv C89GL_glGetProgramiv = NULL;
C89GL_PFN_glGetShaderInfoLog C89GL_glGetShaderInfoLog = NULL;
C89GL_PFN_glGetProgramInfoLog C89GL_glGetProgramInfoLog = NULL;
C89GL_PFN_glGetUniformLocation C89GL_glGetUniformLocation = NULL;

/* ---- Scalar uniform setters ---- */
C89GL_PFN_glUniform1f C89GL_glUniform1f = NULL;
C89GL_PFN_glUniform2f C89GL_glUniform2f = NULL;
C89GL_PFN_glUniform3f C89GL_glUniform3f = NULL;
C89GL_PFN_glUniform4f C89GL_glUniform4f = NULL;
C89GL_PFN_glUniform1i C89GL_glUniform1i = NULL;
C89GL_PFN_glUniform2i C89GL_glUniform2i = NULL;
C89GL_PFN_glUniform3i C89GL_glUniform3i = NULL;
C89GL_PFN_glUniform4i C89GL_glUniform4i = NULL;
C89GL_PFN_glUniform1ui C89GL_glUniform1ui = NULL;
C89GL_PFN_glUniform2ui C89GL_glUniform2ui = NULL;
C89GL_PFN_glUniform3ui C89GL_glUniform3ui = NULL;
C89GL_PFN_glUniform4ui C89GL_glUniform4ui = NULL;

/* ---- Vector uniform setters ---- */
C89GL_PFN_glUniform1fv C89GL_glUniform1fv = NULL;
C89GL_PFN_glUniform2fv C89GL_glUniform2fv = NULL;
C89GL_PFN_glUniform3fv C89GL_glUniform3fv = NULL;
C89GL_PFN_glUniform4fv C89GL_glUniform4fv = NULL;
C89GL_PFN_glUniform1iv C89GL_glUniform1iv = NULL;
C89GL_PFN_glUniform2iv C89GL_glUniform2iv = NULL;
C89GL_PFN_glUniform3iv C89GL_glUniform3iv = NULL;
C89GL_PFN_glUniform4iv C89GL_glUniform4iv = NULL;
C89GL_PFN_glUniform1uiv C89GL_glUniform1uiv = NULL;
C89GL_PFN_glUniform2uiv C89GL_glUniform2uiv = NULL;
C89GL_PFN_glUniform3uiv C89GL_glUniform3uiv = NULL;
C89GL_PFN_glUniform4uiv C89GL_glUniform4uiv = NULL;

/* ---- Matrix uniform setters ---- */
C89GL_PFN_glUniformMatrix2fv C89GL_glUniformMatrix2fv = NULL;
C89GL_PFN_glUniformMatrix3fv C89GL_glUniformMatrix3fv = NULL;
C89GL_PFN_glUniformMatrix4fv C89GL_glUniformMatrix4fv = NULL;
C89GL_PFN_glUniformMatrix2x3fv C89GL_glUniformMatrix2x3fv = NULL;
C89GL_PFN_glUniformMatrix3x2fv C89GL_glUniformMatrix3x2fv = NULL;
C89GL_PFN_glUniformMatrix2x4fv C89GL_glUniformMatrix2x4fv = NULL;
C89GL_PFN_glUniformMatrix4x2fv C89GL_glUniformMatrix4x2fv = NULL;
C89GL_PFN_glUniformMatrix3x4fv C89GL_glUniformMatrix3x4fv = NULL;
C89GL_PFN_glUniformMatrix4x3fv C89GL_glUniformMatrix4x3fv = NULL;

C89GL_PFN_glVertexAttribPointer C89GL_glVertexAttribPointer = NULL;
C89GL_PFN_glEnableVertexAttribArray C89GL_glEnableVertexAttribArray = NULL;
C89GL_PFN_glDisableVertexAttribArray C89GL_glDisableVertexAttribArray = NULL;
C89GL_PFN_glVertexAttrib1f C89GL_glVertexAttrib1f = NULL;

/* 3.0 */
C89GL_PFN_glGenVertexArrays C89GL_glGenVertexArrays = NULL;
C89GL_PFN_glDeleteVertexArrays C89GL_glDeleteVertexArrays = NULL;
C89GL_PFN_glBindVertexArray C89GL_glBindVertexArray = NULL;
C89GL_PFN_glGenFramebuffers C89GL_glGenFramebuffers = NULL;
C89GL_PFN_glDeleteFramebuffers C89GL_glDeleteFramebuffers = NULL;
C89GL_PFN_glBindFramebuffer C89GL_glBindFramebuffer = NULL;
C89GL_PFN_glGenRenderbuffers C89GL_glGenRenderbuffers = NULL;
C89GL_PFN_glDeleteRenderbuffers C89GL_glDeleteRenderbuffers = NULL;
C89GL_PFN_glBindRenderbuffer C89GL_glBindRenderbuffer = NULL;
C89GL_PFN_glRenderbufferStorage C89GL_glRenderbufferStorage = NULL;
C89GL_PFN_glRenderbufferStorageMultisample C89GL_glRenderbufferStorageMultisample = NULL;
C89GL_PFN_glFramebufferTexture2D C89GL_glFramebufferTexture2D = NULL;
C89GL_PFN_glFramebufferRenderbuffer C89GL_glFramebufferRenderbuffer = NULL;
C89GL_PFN_glCheckFramebufferStatus C89GL_glCheckFramebufferStatus = NULL;
C89GL_PFN_glGetFramebufferAttachmentParameteriv C89GL_glGetFramebufferAttachmentParameteriv = NULL;
C89GL_PFN_glBlitFramebuffer C89GL_glBlitFramebuffer = NULL;
C89GL_PFN_glDrawArrays C89GL_glDrawArrays = NULL;
C89GL_PFN_glDrawElements C89GL_glDrawElements = NULL;
C89GL_PFN_glBindFragDataLocation C89GL_glBindFragDataLocation = NULL;
C89GL_PFN_glGetFragDataLocation C89GL_glGetFragDataLocation = NULL;
C89GL_PFN_glDrawBuffers C89GL_glDrawBuffers = NULL;
C89GL_PFN_glClearBufferfv C89GL_glClearBufferfv = NULL;

/* 3.1 */
C89GL_PFN_glDrawElementsBaseVertex C89GL_glDrawElementsBaseVertex = NULL;
C89GL_PFN_glGetActiveUniformBlockiv C89GL_glGetActiveUniformBlockiv = NULL;
C89GL_PFN_glGetUniformBlockIndex C89GL_glGetUniformBlockIndex = NULL;
C89GL_PFN_glUniformBlockBinding C89GL_glUniformBlockBinding = NULL;
C89GL_PFN_glGetUniformIndices C89GL_glGetUniformIndices = NULL;
C89GL_PFN_glGetActiveUniformsiv C89GL_glGetActiveUniformsiv = NULL;

/* 3.2 */
C89GL_PFN_glGetIntegeri_v C89GL_glGetIntegeri_v = NULL;

/* 3.3 */
C89GL_PFN_glVertexAttribDivisor C89GL_glVertexAttribDivisor = NULL;
C89GL_PFN_glDrawArraysInstanced C89GL_glDrawArraysInstanced = NULL;
C89GL_PFN_glDrawElementsInstanced C89GL_glDrawElementsInstanced = NULL;
C89GL_PFN_glGetStringi C89GL_glGetStringi = NULL;
C89GL_PFN_glGenSamplers C89GL_glGenSamplers = NULL;
C89GL_PFN_glDeleteSamplers C89GL_glDeleteSamplers = NULL;
C89GL_PFN_glBindSampler C89GL_glBindSampler = NULL;
C89GL_PFN_glSamplerParameteri C89GL_glSamplerParameteri = NULL;
C89GL_PFN_glSamplerParameterf C89GL_glSamplerParameterf = NULL;
C89GL_PFN_glSamplerParameteriv C89GL_glSamplerParameteriv = NULL;
C89GL_PFN_glSamplerParameterfv C89GL_glSamplerParameterfv = NULL;
C89GL_PFN_glGetSamplerParameteriv C89GL_glGetSamplerParameteriv = NULL;
C89GL_PFN_glGetSamplerParameterfv C89GL_glGetSamplerParameterfv = NULL;
C89GL_PFN_glActiveTexture C89GL_glActiveTexture = NULL;
C89GL_PFN_glGenTextures C89GL_glGenTextures = NULL;
C89GL_PFN_glDeleteTextures C89GL_glDeleteTextures = NULL;
C89GL_PFN_glBindTexture C89GL_glBindTexture = NULL;
C89GL_PFN_glTexParameteri C89GL_glTexParameteri = NULL;
C89GL_PFN_glTexParameterf C89GL_glTexParameterf = NULL;
C89GL_PFN_glTexImage2D C89GL_glTexImage2D = NULL;
C89GL_PFN_glTexSubImage2D C89GL_glTexSubImage2D = NULL;
C89GL_PFN_glCopyTexSubImage2D C89GL_glCopyTexSubImage2D = NULL;
C89GL_PFN_glGenerateMipmap C89GL_glGenerateMipmap = NULL;
C89GL_PFN_glGenQueries C89GL_glGenQueries = NULL;
C89GL_PFN_glDeleteQueries C89GL_glDeleteQueries = NULL;
C89GL_PFN_glBeginQuery C89GL_glBeginQuery = NULL;
C89GL_PFN_glEndQuery C89GL_glEndQuery = NULL;
C89GL_PFN_glGetQueryObjectuiv C89GL_glGetQueryObjectuiv = NULL;
C89GL_PFN_glGetQueryObjecti64v C89GL_glGetQueryObjecti64v = NULL;
C89GL_PFN_glQueryCounter C89GL_glQueryCounter = NULL;
C89GL_PFN_glFenceSync C89GL_glFenceSync = NULL;
C89GL_PFN_glClientWaitSync C89GL_glClientWaitSync = NULL;
C89GL_PFN_glDeleteSync C89GL_glDeleteSync = NULL;
C89GL_PFN_glGenTransformFeedbacks C89GL_glGenTransformFeedbacks = NULL;
C89GL_PFN_glDeleteTransformFeedbacks C89GL_glDeleteTransformFeedbacks = NULL;
C89GL_PFN_glBindTransformFeedback C89GL_glBindTransformFeedback = NULL;
C89GL_PFN_glBeginTransformFeedback C89GL_glBeginTransformFeedback = NULL;
C89GL_PFN_glEndTransformFeedback C89GL_glEndTransformFeedback = NULL;
C89GL_PFN_glTransformFeedbackVaryings C89GL_glTransformFeedbackVaryings = NULL;

C89GL_PFN_glBindBufferBase C89GL_glBindBufferBase = NULL;
C89GL_PFN_glBlendFunci C89GL_glBlendFunci = NULL;

/* ---- OpenGL 4.3 Compute / SSBO ---- */
C89GL_PFN_glMemoryBarrier C89GL_glMemoryBarrier = NULL;
C89GL_PFN_glDispatchCompute C89GL_glDispatchCompute = NULL;
C89GL_PFN_glBindImageTexture C89GL_glBindImageTexture = NULL;
C89GL_PFN_glClearBufferSubData C89GL_glClearBufferSubData = NULL;
C89GL_PFN_glClearBufferData C89GL_glClearBufferData = NULL;

/* ---------- Loader Implementation ---------- */
int C89GL_load_functions(void) {
    /* 1.0 / 1.1 */
    C89GL_LOAD_FUNC(C89GL_glClear, "glClear");
    C89GL_LOAD_FUNC(C89GL_glClearColor, "glClearColor");
    C89GL_LOAD_FUNC(C89GL_glViewport, "glViewport");
    C89GL_LOAD_FUNC(C89GL_glEnable, "glEnable");
    C89GL_LOAD_FUNC(C89GL_glDisable, "glDisable");
    C89GL_LOAD_FUNC(C89GL_glDepthFunc, "glDepthFunc");
    C89GL_LOAD_FUNC(C89GL_glBlendFunc, "glBlendFunc");
    C89GL_LOAD_FUNC(C89GL_glCullFace, "glCullFace");
    C89GL_LOAD_FUNC(C89GL_glFrontFace, "glFrontFace");
    C89GL_LOAD_FUNC(C89GL_glScissor, "glScissor");
    C89GL_LOAD_FUNC(C89GL_glPolygonMode, "glPolygonMode");
    C89GL_LOAD_FUNC(C89GL_glGetString, "glGetString");
    C89GL_LOAD_FUNC(C89GL_glGetIntegerv, "glGetIntegerv");
    C89GL_LOAD_FUNC(C89GL_glGetFloatv, "glGetFloatv");
    C89GL_LOAD_FUNC(C89GL_glGetBooleanv, "glGetBooleanv");
    C89GL_LOAD_FUNC(C89GL_glFlush, "glFlush");
    C89GL_LOAD_FUNC(C89GL_glFinish, "glFinish");
    C89GL_LOAD_FUNC(C89GL_glDepthRange, "glDepthRange");
    C89GL_LOAD_FUNC(C89GL_glClearDepth, "glClearDepth");
    C89GL_LOAD_FUNC(C89GL_glPolygonOffset, "glPolygonOffset");
    C89GL_LOAD_FUNC(C89GL_glBlendEquation, "glBlendEquation");
    C89GL_LOAD_FUNC(C89GL_glBlendColor, "glBlendColor");
    C89GL_LOAD_FUNC(C89GL_glStencilFunc, "glStencilFunc");
    C89GL_LOAD_FUNC(C89GL_glStencilOp, "glStencilOp");
    C89GL_LOAD_FUNC(C89GL_glStencilMask, "glStencilMask");
    C89GL_LOAD_FUNC(C89GL_glDepthMask, "glDepthMask");
    C89GL_LOAD_FUNC(C89GL_glColorMask, "glColorMask");
    C89GL_LOAD_FUNC(C89GL_glDrawBuffer, "glDrawBuffer");
    C89GL_LOAD_FUNC(C89GL_glReadBuffer, "glReadBuffer");
    C89GL_LOAD_FUNC(C89GL_glReadPixels, "glReadPixels");

    /* 1.5 */
    C89GL_LOAD_FUNC(C89GL_glGenBuffers, "glGenBuffers");
    C89GL_LOAD_FUNC(C89GL_glDeleteBuffers, "glDeleteBuffers");
    C89GL_LOAD_FUNC(C89GL_glBindBuffer, "glBindBuffer");
    C89GL_LOAD_FUNC(C89GL_glBufferData, "glBufferData");
    C89GL_LOAD_FUNC(C89GL_glBufferSubData, "glBufferSubData");
    C89GL_LOAD_FUNC(C89GL_glGetBufferSubData, "glGetBufferSubData");
    C89GL_LOAD_FUNC(C89GL_glMapBuffer, "glMapBuffer");
    C89GL_LOAD_FUNC(C89GL_glUnmapBuffer, "glUnmapBuffer");
    C89GL_LOAD_FUNC(C89GL_glGetBufferParameteriv, "glGetBufferParameteriv");

    /* 3.0 MapBufferRange */
    C89GL_LOAD_FUNC(C89GL_glMapBufferRange, "glMapBufferRange");

    /* 2.0 */
    C89GL_LOAD_FUNC(C89GL_glCreateProgram, "glCreateProgram");
    C89GL_LOAD_FUNC(C89GL_glCreateShader, "glCreateShader");
    C89GL_LOAD_FUNC(C89GL_glShaderSource, "glShaderSource");
    C89GL_LOAD_FUNC(C89GL_glCompileShader, "glCompileShader");
    C89GL_LOAD_FUNC(C89GL_glAttachShader, "glAttachShader");
    C89GL_LOAD_FUNC(C89GL_glLinkProgram, "glLinkProgram");
    C89GL_LOAD_FUNC(C89GL_glUseProgram, "glUseProgram");
    C89GL_LOAD_FUNC(C89GL_glDeleteShader, "glDeleteShader");
    C89GL_LOAD_FUNC(C89GL_glDeleteProgram, "glDeleteProgram");
    C89GL_LOAD_FUNC(C89GL_glDetachShader, "glDetachShader");
    C89GL_LOAD_FUNC(C89GL_glGetShaderiv, "glGetShaderiv");
    C89GL_LOAD_FUNC(C89GL_glGetProgramiv, "glGetProgramiv");
    C89GL_LOAD_FUNC(C89GL_glGetShaderInfoLog, "glGetShaderInfoLog");
    C89GL_LOAD_FUNC(C89GL_glGetProgramInfoLog, "glGetProgramInfoLog");
    C89GL_LOAD_FUNC(C89GL_glGetUniformLocation, "glGetUniformLocation");

    /* ---- Scalar uniform setters ---- */
    C89GL_LOAD_FUNC(C89GL_glUniform1f, "glUniform1f");
    C89GL_LOAD_FUNC(C89GL_glUniform2f, "glUniform2f");
    C89GL_LOAD_FUNC(C89GL_glUniform3f, "glUniform3f");
    C89GL_LOAD_FUNC(C89GL_glUniform4f, "glUniform4f");
    C89GL_LOAD_FUNC(C89GL_glUniform1i, "glUniform1i");
    C89GL_LOAD_FUNC(C89GL_glUniform2i, "glUniform2i");
    C89GL_LOAD_FUNC(C89GL_glUniform3i, "glUniform3i");
    C89GL_LOAD_FUNC(C89GL_glUniform4i, "glUniform4i");
    C89GL_LOAD_FUNC(C89GL_glUniform1ui, "glUniform1ui");
    C89GL_LOAD_FUNC(C89GL_glUniform2ui, "glUniform2ui");
    C89GL_LOAD_FUNC(C89GL_glUniform3ui, "glUniform3ui");
    C89GL_LOAD_FUNC(C89GL_glUniform4ui, "glUniform4ui");

    /* ---- Vector uniform setters ---- */
    C89GL_LOAD_FUNC(C89GL_glUniform1fv, "glUniform1fv");
    C89GL_LOAD_FUNC(C89GL_glUniform2fv, "glUniform2fv");
    C89GL_LOAD_FUNC(C89GL_glUniform3fv, "glUniform3fv");
    C89GL_LOAD_FUNC(C89GL_glUniform4fv, "glUniform4fv");
    C89GL_LOAD_FUNC(C89GL_glUniform1iv, "glUniform1iv");
    C89GL_LOAD_FUNC(C89GL_glUniform2iv, "glUniform2iv");
    C89GL_LOAD_FUNC(C89GL_glUniform3iv, "glUniform3iv");
    C89GL_LOAD_FUNC(C89GL_glUniform4iv, "glUniform4iv");
    C89GL_LOAD_FUNC(C89GL_glUniform1uiv, "glUniform1uiv");
    C89GL_LOAD_FUNC(C89GL_glUniform2uiv, "glUniform2uiv");
    C89GL_LOAD_FUNC(C89GL_glUniform3uiv, "glUniform3uiv");
    C89GL_LOAD_FUNC(C89GL_glUniform4uiv, "glUniform4uiv");

    /* ---- Matrix uniform setters ---- */
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix2fv, "glUniformMatrix2fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix3fv, "glUniformMatrix3fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix4fv, "glUniformMatrix4fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix2x3fv, "glUniformMatrix2x3fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix3x2fv, "glUniformMatrix3x2fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix2x4fv, "glUniformMatrix2x4fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix4x2fv, "glUniformMatrix4x2fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix3x4fv, "glUniformMatrix3x4fv");
    C89GL_LOAD_FUNC(C89GL_glUniformMatrix4x3fv, "glUniformMatrix4x3fv");

    C89GL_LOAD_FUNC(C89GL_glVertexAttribPointer, "glVertexAttribPointer");
    C89GL_LOAD_FUNC(C89GL_glEnableVertexAttribArray, "glEnableVertexAttribArray");
    C89GL_LOAD_FUNC(C89GL_glDisableVertexAttribArray, "glDisableVertexAttribArray");
    C89GL_LOAD_FUNC(C89GL_glVertexAttrib1f, "glVertexAttrib1f");

    /* 3.0 */
    C89GL_LOAD_FUNC(C89GL_glGenVertexArrays, "glGenVertexArrays");
    C89GL_LOAD_FUNC(C89GL_glDeleteVertexArrays, "glDeleteVertexArrays");
    C89GL_LOAD_FUNC(C89GL_glBindVertexArray, "glBindVertexArray");
    C89GL_LOAD_FUNC(C89GL_glGenFramebuffers, "glGenFramebuffers");
    C89GL_LOAD_FUNC(C89GL_glDeleteFramebuffers, "glDeleteFramebuffers");
    C89GL_LOAD_FUNC(C89GL_glBindFramebuffer, "glBindFramebuffer");
    C89GL_LOAD_FUNC(C89GL_glGenRenderbuffers, "glGenRenderbuffers");
    C89GL_LOAD_FUNC(C89GL_glDeleteRenderbuffers, "glDeleteRenderbuffers");
    C89GL_LOAD_FUNC(C89GL_glBindRenderbuffer, "glBindRenderbuffer");
    C89GL_LOAD_FUNC(C89GL_glRenderbufferStorage, "glRenderbufferStorage");
    C89GL_LOAD_FUNC(C89GL_glRenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
    C89GL_LOAD_FUNC(C89GL_glFramebufferTexture2D, "glFramebufferTexture2D");
    C89GL_LOAD_FUNC(C89GL_glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    C89GL_LOAD_FUNC(C89GL_glCheckFramebufferStatus, "glCheckFramebufferStatus");
    C89GL_LOAD_FUNC(C89GL_glGetFramebufferAttachmentParameteriv, "glGetFramebufferAttachmentParameteriv");
    C89GL_LOAD_FUNC(C89GL_glBlitFramebuffer, "glBlitFramebuffer");
    C89GL_LOAD_FUNC(C89GL_glDrawArrays, "glDrawArrays");
    C89GL_LOAD_FUNC(C89GL_glDrawElements, "glDrawElements");
    C89GL_LOAD_FUNC(C89GL_glBindFragDataLocation, "glBindFragDataLocation");
    C89GL_LOAD_FUNC(C89GL_glGetFragDataLocation, "glGetFragDataLocation");
    C89GL_LOAD_FUNC(C89GL_glDrawBuffers, "glDrawBuffers");
    C89GL_LOAD_FUNC(C89GL_glClearBufferfv, "glClearBufferfv");

    /* 3.1 */
    C89GL_LOAD_FUNC(C89GL_glDrawElementsBaseVertex, "glDrawElementsBaseVertex");
    C89GL_LOAD_FUNC(C89GL_glGetActiveUniformBlockiv, "glGetActiveUniformBlockiv");
    C89GL_LOAD_FUNC(C89GL_glGetUniformBlockIndex, "glGetUniformBlockIndex");
    C89GL_LOAD_FUNC(C89GL_glUniformBlockBinding, "glUniformBlockBinding");
    C89GL_LOAD_FUNC(C89GL_glGetUniformIndices, "glGetUniformIndices");
    C89GL_LOAD_FUNC(C89GL_glGetActiveUniformsiv, "glGetActiveUniformsiv");

    /* 3.2 */
    C89GL_LOAD_FUNC(C89GL_glGetIntegeri_v, "glGetIntegeri_v");

    /* 3.3 */
    C89GL_LOAD_FUNC(C89GL_glVertexAttribDivisor, "glVertexAttribDivisor");
    C89GL_LOAD_FUNC(C89GL_glDrawArraysInstanced, "glDrawArraysInstanced");
    C89GL_LOAD_FUNC(C89GL_glDrawElementsInstanced, "glDrawElementsInstanced");
    C89GL_LOAD_FUNC(C89GL_glGetStringi, "glGetStringi");
    C89GL_LOAD_FUNC(C89GL_glGenSamplers, "glGenSamplers");
    C89GL_LOAD_FUNC(C89GL_glDeleteSamplers, "glDeleteSamplers");
    C89GL_LOAD_FUNC(C89GL_glBindSampler, "glBindSampler");
    C89GL_LOAD_FUNC(C89GL_glSamplerParameteri, "glSamplerParameteri");
    C89GL_LOAD_FUNC(C89GL_glSamplerParameterf, "glSamplerParameterf");
    C89GL_LOAD_FUNC(C89GL_glSamplerParameteriv, "glSamplerParameteriv");
    C89GL_LOAD_FUNC(C89GL_glSamplerParameterfv, "glSamplerParameterfv");
    C89GL_LOAD_FUNC(C89GL_glGetSamplerParameteriv, "glGetSamplerParameteriv");
    C89GL_LOAD_FUNC(C89GL_glGetSamplerParameterfv, "glGetSamplerParameterfv");
    C89GL_LOAD_FUNC(C89GL_glActiveTexture, "glActiveTexture");
    C89GL_LOAD_FUNC(C89GL_glGenTextures, "glGenTextures");
    C89GL_LOAD_FUNC(C89GL_glDeleteTextures, "glDeleteTextures");
    C89GL_LOAD_FUNC(C89GL_glBindTexture, "glBindTexture");
    C89GL_LOAD_FUNC(C89GL_glTexParameteri, "glTexParameteri");
    C89GL_LOAD_FUNC(C89GL_glTexParameterf, "glTexParameterf");
    C89GL_LOAD_FUNC(C89GL_glTexImage2D, "glTexImage2D");
    C89GL_LOAD_FUNC(C89GL_glTexSubImage2D, "glTexSubImage2D");
    C89GL_LOAD_FUNC(C89GL_glCopyTexSubImage2D, "glCopyTexSubImage2D");
    C89GL_LOAD_FUNC(C89GL_glGenerateMipmap, "glGenerateMipmap");
    C89GL_LOAD_FUNC(C89GL_glGenQueries, "glGenQueries");
    C89GL_LOAD_FUNC(C89GL_glDeleteQueries, "glDeleteQueries");
    C89GL_LOAD_FUNC(C89GL_glBeginQuery, "glBeginQuery");
    C89GL_LOAD_FUNC(C89GL_glEndQuery, "glEndQuery");
    C89GL_LOAD_FUNC(C89GL_glGetQueryObjectuiv, "glGetQueryObjectuiv");
    C89GL_LOAD_FUNC(C89GL_glGetQueryObjecti64v, "glGetQueryObjecti64v");
    C89GL_LOAD_FUNC(C89GL_glQueryCounter, "glQueryCounter");
    C89GL_LOAD_FUNC(C89GL_glFenceSync, "glFenceSync");
    C89GL_LOAD_FUNC(C89GL_glClientWaitSync, "glClientWaitSync");
    C89GL_LOAD_FUNC(C89GL_glDeleteSync, "glDeleteSync");
    C89GL_LOAD_FUNC(C89GL_glGenTransformFeedbacks, "glGenTransformFeedbacks");
    C89GL_LOAD_FUNC(C89GL_glDeleteTransformFeedbacks, "glDeleteTransformFeedbacks");
    C89GL_LOAD_FUNC(C89GL_glBindTransformFeedback, "glBindTransformFeedback");
    C89GL_LOAD_FUNC(C89GL_glBeginTransformFeedback, "glBeginTransformFeedback");
    C89GL_LOAD_FUNC(C89GL_glEndTransformFeedback, "glEndTransformFeedback");
    C89GL_LOAD_FUNC(C89GL_glTransformFeedbackVaryings, "glTransformFeedbackVaryings");

    /* UBO */
    C89GL_LOAD_FUNC(C89GL_glBindBufferBase, "glBindBufferBase");

    /* Per‑attachment blending (4.0) */
    C89GL_LOAD_FUNC(C89GL_glBlendFunci, "glBlendFunci");

    /* ---- OpenGL 4.3 ---- */
    C89GL_LOAD_FUNC(C89GL_glMemoryBarrier, "glMemoryBarrier");
    C89GL_LOAD_FUNC(C89GL_glDispatchCompute, "glDispatchCompute");
    C89GL_LOAD_FUNC(C89GL_glBindImageTexture, "glBindImageTexture");
    C89GL_LOAD_FUNC(C89GL_glClearBufferSubData, "glClearBufferSubData");
    C89GL_LOAD_FUNC(C89GL_glClearBufferData, "glClearBufferData");

    return 1;
}

/* ========================================================================
   CONTEXT CREATION - WINDOWS
   ======================================================================== */
#if defined(C89FW_WINDOWS)

int C89GL_create_context(C89FW_window_t* window, C89GL_Context* ctx) {
    C89FW_native_handles_t handles = C89FW_get_native_handles(window);
    HWND hwnd = (HWND)handles.hwnd;
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    HGLRC dummy, glrc = NULL;
    C89GL_PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB;

    if (!hdc) return 0;
    ctx->hdc = hdc;
    ctx->initialized = 0;

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) return 0;

    dummy = wglCreateContext(hdc);
    if (!dummy) return 0;
    wglMakeCurrent(hdc, dummy);

    wglCreateContextAttribsARB = (C89GL_PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

    if (wglCreateContextAttribsARB) {
        int attribs[] = {0x2091, 4, 0x2092, 3, 0x2093, 0x00000001, 0};
        glrc = wglCreateContextAttribsARB(hdc, NULL, attribs);
    }
    if (!glrc) glrc = wglCreateContext(hdc);

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(dummy);

    if (!glrc) return 0;
    ctx->glrc = glrc;
    ctx->initialized = 1;
    return 1;
}

void C89GL_destroy_context(C89GL_Context* ctx) {
    if (ctx && ctx->glrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext((HGLRC)ctx->glrc);
        ctx->glrc = NULL;
        ctx->initialized = 0;
    }
}

void C89GL_make_current(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        wglMakeCurrent((HDC)ctx->hdc, (HGLRC)ctx->glrc);
}

void C89GL_swap_buffers(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        SwapBuffers((HDC)ctx->hdc);
}

#endif /* C89FW_WINDOWS */

/* ========================================================================
   CONTEXT CREATION - LINUX
   ======================================================================== */
#if defined(C89FW_LINUX)

int C89GL_create_context(C89FW_window_t* window, C89GL_Context* ctx) {
    C89FW_native_handles_t handles = C89FW_get_native_handles(window);
    Display* dpy = (Display*)handles.display;
    Window xwin = (Window)handles.window;
    int screen = DefaultScreen(dpy);
    C89GL_PFN_glXChooseFBConfig glXChooseFBConfig;
    C89GL_PFN_glXCreateContextAttribsARB glXCreateContextAttribsARB;
    GLXContext glx_ctx = NULL;

    ctx->display = dpy;
    ctx->window = xwin;
    ctx->initialized = 0;

    glXChooseFBConfig = (C89GL_PFN_glXChooseFBConfig)glXGetProcAddressARB((const GLubyte*)"glXChooseFBConfig");
    glXCreateContextAttribsARB = (C89GL_PFN_glXCreateContextAttribsARB)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

    if (glXChooseFBConfig && glXCreateContextAttribsARB) {
        int fb_attribs[] = {
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_DOUBLEBUFFER, True,
            GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
            GLX_DEPTH_SIZE, 24, None
        };
        int count;
        GLXFBConfig* fb_configs = glXChooseFBConfig(dpy, screen, fb_attribs, &count);
        if (fb_configs && count > 0) {
            int ctx_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
                GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                None
            };
            glx_ctx = glXCreateContextAttribsARB(dpy, fb_configs[0], NULL, True, ctx_attribs);
            XFree(fb_configs);
        }
    }

    if (!glx_ctx) {
        int vis_attribs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None };
        XVisualInfo* vi = glXChooseVisual(dpy, screen, vis_attribs);
        if (vi) {
            glx_ctx = glXCreateContext(dpy, vi, NULL, True);
            XFree(vi);
        }
    }

    if (!glx_ctx) return 0;
    ctx->glx_ctx = glx_ctx;
    ctx->initialized = 1;
    return 1;
}

void C89GL_destroy_context(C89GL_Context* ctx) {
    if (ctx && ctx->glx_ctx) {
        glXMakeCurrent((Display*)ctx->display, None, NULL);
        glXDestroyContext((Display*)ctx->display, (GLXContext)ctx->glx_ctx);
        ctx->glx_ctx = NULL;
        ctx->initialized = 0;
    }
}

void C89GL_make_current(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        glXMakeCurrent((Display*)ctx->display, (GLXDrawable)ctx->window, (GLXContext)ctx->glx_ctx);
}

void C89GL_swap_buffers(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        glXSwapBuffers((Display*)ctx->display, (GLXDrawable)ctx->window);
}

#endif /* C89FW_LINUX */

/* ========================================================================
   CONTEXT CREATION - MACOS
   ======================================================================== */
#if defined(C89FW_MACOS)

int C89GL_create_context(C89FW_window_t* window, C89GL_Context* ctx) {
    C89FW_native_handles_t handles = C89FW_get_native_handles(window);
    NSView* view = (NSView*)handles.ns_view;
    if (!view) return 0;

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFAColorSize, 32,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!pf) {
        NSOpenGLPixelFormatAttribute fallback[] = {
            NSOpenGLPFAColorSize, 32,
            NSOpenGLPFADepthSize, 24,
            NSOpenGLPFADoubleBuffer,
            0
        };
        pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:fallback];
    }
    if (!pf) return 0;

    NSOpenGLContext* ns_ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
    [pf release];
    if (!ns_ctx) return 0;

    [ns_ctx setView:view];
    ctx->ns_context = ns_ctx;
    ctx->initialized = 1;
    return 1;
}

void C89GL_destroy_context(C89GL_Context* ctx) {
    if (ctx && ctx->ns_context) {
        [(NSOpenGLContext*)ctx->ns_context release];
        ctx->ns_context = NULL;
        ctx->initialized = 0;
    }
}

void C89GL_make_current(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        [(NSOpenGLContext*)ctx->ns_context makeCurrentContext];
}

void C89GL_swap_buffers(C89GL_Context* ctx) {
    if (ctx && ctx->initialized)
        [(NSOpenGLContext*)ctx->ns_context flushBuffer];
}

#endif /* C89FW_MACOS */

#endif /* C89GL_IMPLEMENTATION */
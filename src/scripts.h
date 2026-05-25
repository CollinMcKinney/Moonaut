/* Script system */
/* Lua scripting for managing scenario-state */
/*
 * scripts.h – script manager with Lua hot-reload support
 * Requires: vectors.h (include first)
 */

#ifndef SCRIPTS_H
#define SCRIPTS_H

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef B
#undef B
#endif
#ifdef S
#undef S
#endif

#include "../libs/lua-5.5.0/src/lua.h"
#include "../libs/lua-5.5.0/src/lauxlib.h"
#include "../libs/lua-5.5.0/src/lualib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_state
{
    lua_State *L;
    char *error;
} lua_state;

typedef void (*script_update_fn)(void *context, real dt);
typedef void (*script_shutdown_fn)(void *context);
typedef void (*script_lua_bind_fn)(lua_state *state, void *userdata);

typedef struct script_entry
{
    script_update_fn update;
    void *context;
    script_shutdown_fn shutdown;
} script_entry;

typedef struct lua_script_context
{
    lua_state *state;
    char *path;
    char *last_source;          /* previously loaded script content */
    size_t last_source_len;
    script_lua_bind_fn bind;
    void *bind_userdata;
} lua_script_context;

#ifndef SCRIPTS_MAX
#define SCRIPTS_MAX 8
#endif

static script_entry g_scripts[SCRIPTS_MAX];
static int g_script_count = 0;

static char *scripts_strdup(const char *text)
{
    size_t len = strlen(text) + 1;
    char *copy = (char *)malloc(len);
    if (copy != NULL)
    {
        memcpy(copy, text, len);
    }
    return copy;
}

static void lua_clear_error(lua_state *state)
{
    if (state != NULL && state->error != NULL)
    {
        free(state->error);
        state->error = NULL;
    }
}

static void lua_set_error_from_string(lua_state *state, const char *message)
{
    lua_clear_error(state);
    if (state != NULL && message != NULL)
    {
        state->error = scripts_strdup(message);
    }
}

static void lua_set_error_from_stack(lua_state *state)
{
    const char *message;
    if (state == NULL || state->L == NULL)
    {
        return;
    }
    message = lua_tostring(state->L, -1);
    if (message == NULL)
    {
        message = "unknown Lua error";
    }
    lua_set_error_from_string(state, message);
    lua_pop(state->L, 1);
}

static lua_state *lua_create(void)
{
    lua_state *state = (lua_state *)malloc(sizeof(lua_state));
    if (state == NULL)
    {
        return NULL;
    }
    state->L = luaL_newstate();
    state->error = NULL;
    if (state->L == NULL)
    {
        free(state);
        return NULL;
    }
    luaL_openlibs(state->L);
    return state;
}

static void lua_destroy(lua_state *state)
{
    if (state == NULL)
    {
        return;
    }
    if (state->L != NULL)
    {
        lua_close(state->L);
    }
    lua_clear_error(state);
    free(state);
}

static int lua_load_file(lua_state *state, const char *path)
{
    int status;
    if (state == NULL || state->L == NULL)
    {
        return -1;
    }
    lua_clear_error(state);
    status = luaL_loadfile(state->L, path);
    if (status != LUA_OK)
    {
        lua_set_error_from_stack(state);
        return -1;
    }
    status = lua_pcall(state->L, 0, 0, 0);
    if (status != LUA_OK)
    {
        lua_set_error_from_stack(state);
        return -1;
    }
    return 0;
}

static int lua_call_update(lua_state *state, double dt)
{
    int status;
    if (state == NULL || state->L == NULL)
    {
        return -1;
    }
    lua_clear_error(state);
    lua_getglobal(state->L, "update");
    if (lua_isnil(state->L, -1))
    {
        lua_pop(state->L, 1);
        return 0;
    }
    if (!lua_isfunction(state->L, -1))
    {
        lua_pop(state->L, 1);
        lua_set_error_from_string(state, "'update' exists but is not a function");
        return -1;
    }
    lua_pushnumber(state->L, dt);
    status = lua_pcall(state->L, 1, 0, 0);
    if (status != LUA_OK)
    {
        lua_set_error_from_stack(state);
        return -1;
    }
    return 0;
}

static const char *lua_last_error(lua_state *state)
{
    if (state == NULL || state->error == NULL)
    {
        return "";
    }
    return state->error;
}

static void lua_register_builtin(lua_state *state, const char *name, lua_CFunction builtin)
{
    if (state == NULL || state->L == NULL)
    {
        return;
    }
    lua_pushcfunction(state->L, builtin);
    lua_setglobal(state->L, name);
}

static void lua_set_global_number(lua_state *state, const char *name, lua_Number value)
{
    if (state == NULL || state->L == NULL)
    {
        return;
    }
    lua_pushnumber(state->L, value);
    lua_setglobal(state->L, name);
}

static void lua_set_global_integer(lua_state *state, const char *name, lua_Integer value)
{
    if (state == NULL || state->L == NULL)
    {
        return;
    }
    lua_pushinteger(state->L, value);
    lua_setglobal(state->L, name);
}

/* Read the whole file into a dynamically allocated buffer.
   Returns NULL on failure, otherwise a null‑terminated string. */
static char *scripts_read_whole_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    long flen;
    char *buf;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    flen = ftell(fp);
    if (flen < 0) { fclose(fp); return NULL; }
    rewind(fp);   /* same as fseek(fp, 0, SEEK_SET) */

    buf = (char*)malloc((size_t)flen + 1);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)flen, fp) != (size_t)flen) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[flen] = '\0';
    *out_len = (size_t)flen;
    fclose(fp);
    return buf;
}

/* Return 1 if the file content has changed since the last stored
   version, 0 if unchanged, -1 on error. */
static int scripts_file_changed(const char *path,
                                char **inout_last_source,
                                size_t *inout_last_len)
{
    size_t new_len;
    char *new_source = scripts_read_whole_file(path, &new_len);
    if (!new_source) return -1;

    if (*inout_last_source == NULL ||
        new_len != *inout_last_len ||
        memcmp(new_source, *inout_last_source, new_len) != 0)
    {
        /* Content changed – replace stored version */
        free(*inout_last_source);
        *inout_last_source = new_source;
        *inout_last_len   = new_len;
        return 1;
    }

    /* Same content – discard new_source */
    free(new_source);
    return 0;
}

static lua_state *scripts_create_bound_lua_state(const char *path,
                                                 script_lua_bind_fn bind,
                                                 void *bind_userdata)
{
    lua_state *state = lua_create();
    if (state == NULL) return NULL;
    if (bind != NULL) bind(state, bind_userdata);
    if (lua_load_file(state, path) != 0) return state;   /* error is already set */
    return state;
}

static void scripts_init(void)
{
    g_script_count = 0;
}

static int scripts_add(script_update_fn update, void *context, script_shutdown_fn shutdown)
{
    if (g_script_count >= SCRIPTS_MAX) {
        return -1;
    }
    g_scripts[g_script_count].update = update;
    g_scripts[g_script_count].context = context;
    g_scripts[g_script_count].shutdown = shutdown;
    g_script_count += 1;
    return g_script_count - 1;
}

static void scripts_update(real dt)
{
    int i;
    for (i = 0; i < g_script_count; i += 1) {
        if (g_scripts[i].update != NULL) {
            g_scripts[i].update(g_scripts[i].context, dt);
        }
    }
}

static void scripts_shutdown(void)
{
    int i;
    for (i = 0; i < g_script_count; i += 1) {
        if (g_scripts[i].shutdown != NULL) {
            g_scripts[i].shutdown(g_scripts[i].context);
        }
    }
    g_script_count = 0;
}

static int lua_script_try_reload(lua_script_context *context)
{
    lua_state *new_state;

    if (context == NULL || context->path == NULL)
        return 0;

    if (scripts_file_changed(context->path,
                             &context->last_source,
                             &context->last_source_len) <= 0)
        return 0;   /* unchanged or error */

    /* File changed – reload Lua */
    new_state = scripts_create_bound_lua_state(context->path,
                                               context->bind,
                                               context->bind_userdata);  /* no longer needs write time */
    if (new_state == NULL) {
        fprintf(stderr, "Lua reload failed: out of memory\n");
        return -1;
    }
    if (lua_last_error(new_state)[0] != '\0') {
        fprintf(stderr, "Lua reload failed: %s\n", lua_last_error(new_state));
        lua_destroy(new_state);
        return -1;
    }

    lua_destroy(context->state);
    context->state = new_state;
    printf("Lua reloaded: %s\n", context->path);
    return 1;
}

static void lua_script_update(void *context_ptr, real dt)
{
    lua_script_context *context = (lua_script_context *)context_ptr;
    if (context == NULL) {
        return;
    }

    lua_script_try_reload(context);

    if (context->state != NULL) {
        if (lua_call_update(context->state, (double)dt) != 0) {
            fprintf(stderr, "Lua runtime error: %s\n", lua_last_error(context->state));
        }
    }
}

static void lua_script_shutdown(void *context_ptr)
{
    lua_script_context *context = (lua_script_context *)context_ptr;
    if (context == NULL) {
        return;
    }
    if (context->state != NULL) {
        lua_destroy(context->state);
    }
    if (context->path != NULL) {
        free(context->path);
    }
    free(context);
}

static int scripts_add_lua(const char *path, script_lua_bind_fn bind, void *bind_userdata)
{
    lua_script_context *context;
    context = (lua_script_context *)malloc(sizeof(lua_script_context));
    if (context == NULL) return -1;

    context->path = scripts_strdup(path);
    context->bind = bind;
    context->bind_userdata = bind_userdata;
    context->last_source = NULL;
    context->last_source_len = 0;

    context->state = scripts_create_bound_lua_state(path, bind, bind_userdata);
    if (context->path == NULL || context->state == NULL) {
        if (context->state) lua_destroy(context->state);
        free(context->path);
        free(context);
        return -1;
    }

    /* Prime the content cache */
    scripts_file_changed(path, &context->last_source, &context->last_source_len);

    if (lua_last_error(context->state)[0] != '\0') {
        fprintf(stderr, "Lua error: %s\n", lua_last_error(context->state));
        lua_destroy(context->state);
        free(context->last_source);
        free(context->path);
        free(context);
        return -1;
    }

    return scripts_add(lua_script_update, context, lua_script_shutdown);
}

#ifdef __cplusplus
}
#endif

#endif /* SCRIPTS_H */

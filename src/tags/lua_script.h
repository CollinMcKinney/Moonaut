#ifndef LUA_SCRIPT_DEFINITION_H
#define LUA_SCRIPT_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
   A raw byte block – nothing more than a counted array of u8.
   ------------------------------------------------------------------------ */
TAG_BLOCK_BEGIN(lua_byte_block, 65535, sizeof(u8))
    FIELD_TERMINATOR
TAG_BLOCK_END(lua_byte_block, 65535, sizeof(u8))

/* ------------------------------------------------------------------------
   Lua script tag
   ------------------------------------------------------------------------ */
typedef struct lua_script_definition {
    struct tag_block source;    /* raw bytes of the Lua script */
    string_id        name;      /* logical name (optional, for debugging) */
} lua_script_definition;

TAG_GROUP_BEGIN(lua_script, TAG_MAGIC_PACK(lscr), sizeof(struct lua_script_definition))
    FIELD_BLOCK("source", lua_byte_block),
    FIELD_STRING_ID("name"),
    FIELD_TERMINATOR
TAG_GROUP_END(lua_script, sizeof(struct lua_script_definition))

#ifdef __cplusplus
}
#endif

#endif /* LUA_SCRIPT_DEFINITION_H */
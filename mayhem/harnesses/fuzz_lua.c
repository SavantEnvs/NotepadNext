/* Fuzz the vendored Lua 5.3 interpreter (thirdparty/lua), which NotepadNext embeds for its Lua
 * console, extension scripts and language definitions (LuaState::execute -> luaL_loadstring +
 * lua_pcall). The input is a Lua source chunk: it is lexed, parsed, compiled and run.
 *
 * - Text chunks only (mode "t"): Lua's binary-chunk loader does not validate bytecode, by design
 *   (the Lua manual: "maliciously crafted binary chunks can crash the interpreter").
 * - Libraries: base, coroutine, table, string, math, utf8. Not io/os/package/debug (they touch the
 *   file system, the process or bypass the VM's checks); dofile/loadfile/print are removed from base
 *   for the same reason, and `load`, which would accept a binary chunk built from a string literal.
 * - Bounded, deterministically: an allocator capped at 64 MiB (allocation failure is a normal Lua
 *   "not enough memory" error) and a count hook that raises a Lua error after 1,000,000 VM
 *   instructions, so `while true do end` ends with a script error instead of hanging. Both depend
 *   only on the input, not on time.
 * State: a fresh lua_State per input, closed at the end.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define MEM_LIMIT (64u * 1024u * 1024u)

typedef struct {
    size_t used;
} MemState;

static void *limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    MemState *m = (MemState *)ud;
    if (ptr == NULL)
        osize = 0;
    if (nsize == 0) {
        free(ptr);
        m->used -= osize;
        return NULL;
    }
    if (nsize > osize && m->used + (nsize - osize) > MEM_LIMIT)
        return NULL;
    void *p = realloc(ptr, nsize);
    if (p != NULL)
        m->used = m->used - osize + nsize;
    return p;
}

static void count_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    luaL_error(L, "instruction budget exhausted");
}

static const luaL_Reg kLibs[] = {
    {"_G", luaopen_base},          {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8},
    {NULL, NULL},
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    MemState mem = {0};
    lua_State *L = lua_newstate(limited_alloc, &mem);
    if (L == NULL)
        return 0;
    for (const luaL_Reg *lib = kLibs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    static const char *const kRemoved[] = {"dofile", "loadfile", "load", "print"};
    for (size_t i = 0; i < sizeof(kRemoved) / sizeof(kRemoved[0]); i++) {
        lua_pushnil(L);
        lua_setglobal(L, kRemoved[i]);
    }
    lua_sethook(L, count_hook, LUA_MASKCOUNT, 1000000);

    if (luaL_loadbufferx(L, (const char *)data, size, "=fuzz", "t") == LUA_OK)
        lua_pcall(L, 0, 0, 0);
    lua_close(L);
    return 0;
}

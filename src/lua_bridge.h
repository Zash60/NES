#pragma once

#include <stdint.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

// Forward declarations para evitar dependência circular
struct Emulator;
struct GraphicsContext;

// Tipos de comandos de desenho que o Lua pode enviar
enum LuaDrawType {
    LUA_DRAW_BOX,
    LUA_DRAW_TEXT
};

// Estrutura para armazenar um comando de desenho
typedef struct {
    int type;
    int x1, y1, x2, y2;
    uint32_t color;     // 0xAARRGGBB ou similar
    char text[128];
} LuaDrawCommand;

#define MAX_LUA_DRAW_CMDS 512

typedef struct LuaContext {
    lua_State* L;       // Estado principal do Lua
    lua_State* T;       // Thread (corrotina) para o script rodar
    int script_ref;     // Referência para o script carregado
    
    // Fila de desenho (double buffering simples: Lua preenche, GFX desenha)
    LuaDrawCommand draw_queue[MAX_LUA_DRAW_CMDS];
    int draw_count;
    
    struct Emulator* emu; // Referência de volta ao emulador
} LuaContext;

// Funções de gerenciamento
void lua_bridge_init(struct Emulator* emu);
void lua_bridge_load_script(struct Emulator* emu, const char* filename);
void lua_bridge_update(struct Emulator* emu);
void lua_bridge_render(struct Emulator* emu, struct GraphicsContext* g_ctx);
void lua_bridge_free(struct Emulator* emu);

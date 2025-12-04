#include "lua_bridge.h"
#include "emulator.h"
#include "utils.h"
#include "mmu.h"
#include <string.h>
#include <stdlib.h>

// Helper para converter cores de string (comuns em scripts Lua antigos)
static uint32_t parse_color_string(const char* name) {
    // Formato básico RGB (ignorando alpha por enquanto, ou assumindo opaco)
    if (strcasecmp(name, "green") == 0) return 0x00FF00;
    if (strcasecmp(name, "red") == 0) return 0xFF0000;
    if (strcasecmp(name, "blue") == 0) return 0x0000FF;
    if (strcasecmp(name, "white") == 0) return 0xFFFFFF;
    if (strcasecmp(name, "black") == 0) return 0x000000;
    if (strcasecmp(name, "yellow") == 0) return 0xFFFF00;
    if (strcasecmp(name, "clear") == 0) return 0x00000000; // Transparente
    
    // Se começar com #, tenta parsear hex
    if (name[0] == '#') {
        return (uint32_t)strtol(name + 1, NULL, 16);
    }
    return 0xFFFFFF; // Default branco
}

// --- API: memory ---

static int l_memory_readbyte(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "EMULATOR_PTR");
    Emulator* emu = (Emulator*)lua_touserdata(L, -1);
    
    int addr = (int)luaL_checkinteger(L, 1);
    // Usa a função de leitura do emulador (que lida com mirrors)
    uint8_t val = read_mem(&emu->mem, (uint16_t)addr);
    
    lua_pushinteger(L, val);
    return 1;
}

static int l_memory_writebyte(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "EMULATOR_PTR");
    Emulator* emu = (Emulator*)lua_touserdata(L, -1);
    
    int addr = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    write_mem(&emu->mem, (uint16_t)addr, (uint8_t)val);
    
    return 0;
}

// --- API: gui ---

static int l_gui_drawbox(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LUA_CTX_PTR");
    LuaContext* ctx = (LuaContext*)lua_touserdata(L, -1);
    
    if (ctx->draw_count < MAX_LUA_DRAW_CMDS) {
        LuaDrawCommand* cmd = &ctx->draw_queue[ctx->draw_count++];
        cmd->type = LUA_DRAW_BOX;
        cmd->x1 = (int)luaL_checkinteger(L, 1);
        cmd->y1 = (int)luaL_checkinteger(L, 2);
        cmd->x2 = (int)luaL_checkinteger(L, 3);
        cmd->y2 = (int)luaL_checkinteger(L, 4);
        
        if (lua_isstring(L, 5)) {
            cmd->color = parse_color_string(lua_tostring(L, 5));
        } else {
            cmd->color = (uint32_t)luaL_optinteger(L, 5, 0xFFFFFF);
        }
    }
    return 0;
}

static int l_gui_text(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LUA_CTX_PTR");
    LuaContext* ctx = (LuaContext*)lua_touserdata(L, -1);

    if (ctx->draw_count < MAX_LUA_DRAW_CMDS) {
        LuaDrawCommand* cmd = &ctx->draw_queue[ctx->draw_count++];
        cmd->type = LUA_DRAW_TEXT;
        cmd->x1 = (int)luaL_checkinteger(L, 1);
        cmd->y1 = (int)luaL_checkinteger(L, 2);
        const char* txt = luaL_checkstring(L, 3);
        
        // Copia texto com segurança
        strncpy(cmd->text, txt, 127);
        cmd->text[127] = '\0';
        
        // Cor opcional (branco padrão se não passar)
        cmd->color = 0xFFFFFF;
    }
    return 0;
}

static int l_gui_popup(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOG(INFO, "LUA POPUP: %s", msg);
    return 0;
}

// --- API: FCEU ---

static int l_fceu_frameadvance(lua_State* L) {
    // Yield suspende a execução do Lua, devolvendo o controle para o C loop.
    // O C loop desenhará o frame e chamará lua_resume no próximo ciclo.
    return lua_yield(L, 0);
}

// --- API: iup (MOCK) ---
// Como não temos biblioteca gráfica nativa IUP no Android/SDL,
// criamos mocks (funções vazias) para o script não quebrar.

static int l_iup_dummy_ctor(lua_State* L) {
    lua_newtable(L); // Retorna um objeto "handle" vazio
    
    // Adiciona método 'showxy' que o script usa
    lua_pushstring(L, "showxy");
    lua_pushcfunction(L, l_iup_dummy_ctor); // Reutiliza função que retorna table
    lua_settable(L, -3);
    
    return 1;
}

// --- Inicialização ---

void lua_bridge_init(Emulator* emu) {
    emu->lua_ctx = calloc(1, sizeof(LuaContext));
    LuaContext* ctx = emu->lua_ctx;
    ctx->emu = emu;
    ctx->draw_count = 0;
    
    ctx->L = luaL_newstate();
    luaL_openlibs(ctx->L);
    
    // Armazena ponteiros para callbacks
    lua_pushlightuserdata(ctx->L, emu);
    lua_setfield(ctx->L, LUA_REGISTRYINDEX, "EMULATOR_PTR");
    
    lua_pushlightuserdata(ctx->L, ctx);
    lua_setfield(ctx->L, LUA_REGISTRYINDEX, "LUA_CTX_PTR");

    // 1. Tabela 'memory'
    lua_newtable(ctx->L);
    lua_pushcfunction(ctx->L, l_memory_readbyte); lua_setfield(ctx->L, -2, "readbyte");
    lua_pushcfunction(ctx->L, l_memory_writebyte); lua_setfield(ctx->L, -2, "writebyte");
    lua_setglobal(ctx->L, "memory");

    // 2. Tabela 'gui'
    lua_newtable(ctx->L);
    lua_pushcfunction(ctx->L, l_gui_drawbox); lua_setfield(ctx->L, -2, "drawbox");
    lua_pushcfunction(ctx->L, l_gui_text); lua_setfield(ctx->L, -2, "text");
    lua_pushcfunction(ctx->L, l_gui_popup); lua_setfield(ctx->L, -2, "popup");
    lua_setglobal(ctx->L, "gui");

    // 3. Tabela 'FCEU'
    lua_newtable(ctx->L);
    lua_pushcfunction(ctx->L, l_fceu_frameadvance); lua_setfield(ctx->L, -2, "frameadvance");
    lua_setglobal(ctx->L, "FCEU");
    
    // 4. Tabela 'iup' (Mocks para o script não crashar)
    lua_newtable(ctx->L);
    lua_pushinteger(ctx->L, 0); lua_setfield(ctx->L, -2, "CENTER");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "button");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "multiline");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "toggle");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "dialog");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "frame");
    lua_pushcfunction(ctx->L, l_iup_dummy_ctor); lua_setfield(ctx->L, -2, "vbox");
    lua_setglobal(ctx->L, "iup");

    // Variavel global 'dialogs' usada no script
    lua_pushinteger(ctx->L, 0);
    lua_setglobal(ctx->L, "dialogs");
    
    // Tabela 'handles' usada no script
    lua_newtable(ctx->L);
    lua_setglobal(ctx->L, "handles");
}

void lua_bridge_load_script(Emulator* emu, const char* filename) {
    LuaContext* ctx = emu->lua_ctx;
    if (!ctx || !ctx->L) return;

    // Limpa thread anterior se existir
    ctx->T = NULL;

    // Resolve caminho do arquivo
    char full_path[1024];
    char* base_path = SDL_GetPrefPath("Barracoder", "AndroNES");
    if (base_path) {
        snprintf(full_path, 1024, "%s%s", base_path, filename);
        SDL_free(base_path);
    } else {
        snprintf(full_path, 1024, "%s", filename);
    }

    // Cria nova thread para o script
    ctx->T = lua_newthread(ctx->L);
    ctx->script_ref = luaL_ref(ctx->L, LUA_REGISTRYINDEX); // Evita Garbage Collection

    // Carrega o arquivo
    if (luaL_loadfile(ctx->T, full_path) != LUA_OK) {
        LOG(ERROR, "Lua Load Error: %s", lua_tostring(ctx->T, -1));
        ctx->T = NULL;
        return;
    }
    
    LOG(INFO, "Lua Script Loaded: %s", full_path);
    
    // Inicia a execução (vai rodar até o primeiro frameadvance ou erro)
    int result = lua_resume(ctx->T, NULL, 0);
    if (result != LUA_YIELD && result != LUA_OK) {
        LOG(ERROR, "Lua Runtime Error: %s", lua_tostring(ctx->T, -1));
        ctx->T = NULL;
    }
}

void lua_bridge_update(Emulator* emu) {
    LuaContext* ctx = emu->lua_ctx;
    if (!ctx || !ctx->T) return;

    // Limpa a fila de desenho para o novo frame
    ctx->draw_count = 0;

    // Retoma a execução do script a partir do último yield (frameadvance)
    int result = lua_resume(ctx->T, NULL, 0);
    
    if (result != LUA_YIELD && result != LUA_OK) {
        LOG(ERROR, "Lua Script Finished or Error: %s", lua_tostring(ctx->T, -1));
        ctx->T = NULL; // Script terminou
    }
}

void lua_bridge_render(Emulator* emu, GraphicsContext* g_ctx) {
    LuaContext* ctx = emu->lua_ctx;
    if (!ctx || ctx->draw_count == 0) return;

    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);

    // Cálculos de escala para desenhar coordenadas NES (256x240) na tela do dispositivo
#ifdef __ANDROID__
    float scale_x = g_ctx->dest.w / 256.0f;
    float scale_y = g_ctx->dest.h / 240.0f;
    float off_x = g_ctx->dest.x;
    float off_y = g_ctx->dest.y;
#else
    float scale_x = 1.0f; float scale_y = 1.0f;
    float off_x = 0; float off_y = 0;
#endif

    for (int i = 0; i < ctx->draw_count; i++) {
        LuaDrawCommand* cmd = &ctx->draw_queue[i];
        
        if (cmd->type == LUA_DRAW_BOX) {
            SDL_FRect r;
            // Coordenadas do NES
            float x1 = (float)cmd->x1;
            float y1 = (float)cmd->y1;
            float x2 = (float)cmd->x2;
            float y2 = (float)cmd->y2;

            // Converter para tela
            r.x = off_x + (x1 * scale_x);
            r.y = off_y + (y1 * scale_y);
            r.w = (x2 - x1) * scale_x;
            r.h = (y2 - y1) * scale_y;
            
            // Extrair cores (Formato 0xRRGGBB)
            uint8_t cr = (cmd->color >> 16) & 0xFF;
            uint8_t cg = (cmd->color >> 8) & 0xFF;
            uint8_t cb = cmd->color & 0xFF;
            
            // Borda Sólida (grossura 2px para visibilidade)
            SDL_SetRenderDrawColor(g_ctx->renderer, cr, cg, cb, 255);
            SDL_RenderRect(g_ctx->renderer, &r);
            
            // Preenchimento semi-transparente
            SDL_SetRenderDrawColor(g_ctx->renderer, cr, cg, cb, 60);
            SDL_RenderFillRect(g_ctx->renderer, &r);
        }
        else if (cmd->type == LUA_DRAW_TEXT) {
            // Renderização de texto simples usando a fonte carregada no gfx
            if (g_ctx->font) {
                SDL_Color clr = {255, 255, 255, 255}; // Sempre branco por enquanto
                SDL_Surface* surf = TTF_RenderText_Solid(g_ctx->font, cmd->text, clr);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_ctx->renderer, surf);
                    SDL_FRect dst = {
                        off_x + (cmd->x1 * scale_x),
                        off_y + (cmd->y1 * scale_y),
                        (float)surf->w, (float)surf->h
                    };
                    SDL_RenderTexture(g_ctx->renderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                    SDL_DestroySurface(surf);
                }
            }
        }
    }

    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

void lua_bridge_free(Emulator* emu) {
    if (emu->lua_ctx) {
        if (emu->lua_ctx->L) {
            lua_close(emu->lua_ctx->L);
        }
        free(emu->lua_ctx);
        emu->lua_ctx = NULL;
    }
}

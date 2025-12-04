#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include "ppu.h" // Necessário para acessar OAM

// Variavel global para controle do filtro (0=Normal, 1=Scanlines)
extern int video_filter_mode;

typedef struct GraphicsContext{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_AudioStream* audio_stream;
    TTF_Font* font;
    SDL_FRect dest;
    int width;
    int height;
    int screen_width;
    int screen_height;
    float scale;
    int is_tv;
} GraphicsContext;

void free_graphics(GraphicsContext* ctx);
void get_graphics_context(GraphicsContext* ctx);

// Renderiza o buffer do jogo para a textura (NÃO chama present)
void render_graphics_update(GraphicsContext* g_ctx, const uint32_t* buffer, uint8_t mask_reg);

// Desenha as hitboxes por cima da textura atual
void render_hitboxes(GraphicsContext* g_ctx, struct PPU* ppu);

// Desenha FPS, Filtros, Controles e joga tudo na tela (Present)
void render_ui_and_present(GraphicsContext* g_ctx, float fps);

// Desenha apenas o frame atual (para pause)
void render_frame_only(GraphicsContext* g_ctx);

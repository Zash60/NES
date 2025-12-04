#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

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

// Atualizado para receber o registrador de máscara (PPUMASK)
void render_graphics(GraphicsContext* g_ctx, const uint32_t* buffer, float fps, uint8_t mask_reg);

// Nova função para desenhar o jogo pausado sem glitches
void render_frame_only(GraphicsContext* g_ctx);

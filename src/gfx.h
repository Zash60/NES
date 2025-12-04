#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

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

// Renderiza o buffer do jogo para a textura
void render_graphics_update(GraphicsContext* g_ctx, const uint32_t* buffer, uint8_t mask_reg);

// Desenha UI e finaliza
void render_ui_and_present(GraphicsContext* g_ctx, float fps);

// Desenha apenas o frame
void render_frame_only(GraphicsContext* g_ctx);

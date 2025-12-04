#include <SDL.h>
#include <stdio.h>

#include "gfx.h"
#include "utils.h"
#include "font.h"
#include "ppu.h" 

#ifdef __ANDROID__
#include "touchpad.h"
#endif

int video_filter_mode = 0; // 0 = Normal, 1 = Scanlines

// Declarações locais
static void render_fps_text(GraphicsContext* ctx, float fps);
static void render_scanlines(GraphicsContext* g_ctx);
static void apply_masking(GraphicsContext* g_ctx, uint8_t mask_reg);

void get_graphics_context(GraphicsContext* ctx){
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_SENSOR);
    TTF_Init();
#ifdef __ANDROID__
    ctx->font = TTF_OpenFont("asap.ttf", (int)(ctx->screen_height * 0.05));
    if(ctx->font == NULL) LOG(WARN, "Font not found");
    
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    
    ctx->window = SDL_CreateWindow("AndroNES", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    SDL_IOStream* rw = SDL_IOFromMem(font_data, sizeof(font_data));
    ctx->font = TTF_OpenFontIO(rw, 1, 20);
    ctx->window = SDL_CreateWindow("NES Emulator", ctx->width * (int)ctx->scale, ctx->height * (int)ctx->scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif

    if(ctx->window == NULL) quit(EXIT_FAILURE);
    
    ctx->renderer = SDL_CreateRenderer(ctx->window, NULL);
    if(ctx->renderer == NULL) quit(EXIT_FAILURE);

#ifdef __ANDROID__
    // Mantém aspect ratio e centraliza
    float aspect = (float)256 / 240;
    int want_h = ctx->screen_height;
    int want_w = (int)(want_h * aspect); 
    
    ctx->dest.h = (float)want_h;
    ctx->dest.w = (float)want_w;
    ctx->dest.x = (float)(ctx->screen_width - want_w) / 2;
    ctx->dest.y = 0;
#else
    SDL_SetRenderLogicalPresentation(ctx->renderer, ctx->width * ctx->scale, ctx->height * ctx->scale, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#endif

    // Usando STREAMING para permitir atualização rápida, mas usaremos UpdateTexture para segurança
    ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);
    if(ctx->texture == NULL) quit(EXIT_FAILURE);

    SDL_SetTextureScaleMode(ctx->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);
}

void render_graphics(GraphicsContext* g_ctx, const uint32_t* buffer, float fps, uint8_t mask_reg){
    SDL_RenderClear(g_ctx->renderer);
    
    // CORREÇÃO: SDL_UpdateTexture é mais seguro que Lock/Unlock para evitar problemas de pitch/padding
    SDL_UpdateTexture(g_ctx->texture, NULL, buffer, g_ctx->width * sizeof(uint32_t));

#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    
    // Aplica a máscara para esconder o lixo lateral (glitch "JP")
    apply_masking(g_ctx, mask_reg);

    if(video_filter_mode == 1) render_scanlines(g_ctx);
    
    ANDROID_RENDER_TOUCH_CONTROLS(g_ctx);
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif

    if (fps >= 0.0f) {
        render_fps_text(g_ctx, fps);
    }

    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
    SDL_RenderPresent(g_ctx->renderer);
}

// Renderiza apenas o que já está na textura (para o menu de pausa)
void render_frame_only(GraphicsContext* g_ctx) {
    SDL_RenderClear(g_ctx->renderer);
#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    if(video_filter_mode == 1) render_scanlines(g_ctx);
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif
}

static void apply_masking(GraphicsContext* g_ctx, uint8_t mask_reg) {
    // Se o jogo pedir para esconder os 8 pixels da esquerda (comum em Mario 3, etc)
    if (!(mask_reg & SHOW_BG_8) || !(mask_reg & SHOW_SPRITE_8)) {
        SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
        
        float scale_x = g_ctx->dest.w / (float)g_ctx->width;
        float mask_width = 8.0f * scale_x;
        
        SDL_FRect mask_rect = { g_ctx->dest.x, g_ctx->dest.y, mask_width, g_ctx->dest.h };
        SDL_RenderFillRect(g_ctx->renderer, &mask_rect);
    }
}

static void render_scanlines(GraphicsContext* g_ctx) {
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 60); // Transparencia

    int start_y = (int)g_ctx->dest.y;
    int end_y = start_y + (int)g_ctx->dest.h;
    int width = (int)g_ctx->dest.w;
    int start_x = (int)g_ctx->dest.x;
    
    // Pula linhas (efeito CRT)
    int step = (g_ctx->dest.h > 1000) ? 3 : 2;

    for (int y = start_y; y < end_y; y += step) {
        SDL_RenderLine(g_ctx->renderer, start_x, y, start_x + width, y);
    }
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

static void render_fps_text(GraphicsContext* ctx, float fps) {
    if (!ctx->font) return;
    char fps_str[16];
    snprintf(fps_str, sizeof(fps_str), "%.0f", fps); // Apenas o número inteiro

    SDL_Color color = {0, 255, 0, 255};
    SDL_Surface* surface = TTF_RenderText_Solid(ctx->font, fps_str, 0, color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
        if (texture) {
            SDL_FRect dest = {20.0f, 20.0f, (float)surface->w, (float)surface->h}; 
            SDL_RenderTexture(ctx->renderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture);
        }
        SDL_DestroySurface(surface);
    }
}

void free_graphics(GraphicsContext* ctx){
    if(ctx->font) TTF_CloseFont(ctx->font);
    TTF_Quit();
    SDL_DestroyTexture(ctx->texture);
    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_DestroyAudioStream(ctx->audio_stream);
    SDL_Quit();
    LOG(DEBUG, "Graphics clean up complete");
}

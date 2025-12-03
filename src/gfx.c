#include <SDL.h>
#include <stdio.h>

#include "gfx.h"
#include "utils.h"
#include "font.h"

#ifdef __ANDROID__
#include "touchpad.h"
#endif

int video_filter_mode = 0; // 0 = Normal, 1 = Scanlines

static void render_fps_text(GraphicsContext* ctx, float fps);
static void render_scanlines(GraphicsContext* g_ctx);

void get_graphics_context(GraphicsContext* ctx){
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_SENSOR);
    TTF_Init();
#ifdef __ANDROID__
    ctx->font = TTF_OpenFont("asap.ttf", (int)(ctx->screen_height * 0.05));
    if(ctx->font == NULL){ LOG(ERROR, SDL_GetError()); }
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
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
    ctx->dest.h = ctx->screen_height;
    ctx->dest.w = (ctx->width * ctx->dest.h) / ctx->height;
    ctx->dest.x = (ctx->screen_width - ctx->dest.w) / 2;
    ctx->dest.y = 0;
#else
    SDL_SetRenderLogicalPresentation(ctx->renderer, ctx->width * ctx->scale, ctx->height * ctx->scale, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#endif

    ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, ctx->width, ctx->height);
    SDL_SetTextureScaleMode(ctx->texture, SDL_SCALEMODE_NEAREST);
}

void render_graphics(GraphicsContext* g_ctx, const uint32_t* buffer, float fps){
    SDL_RenderClear(g_ctx->renderer);
    SDL_UpdateTexture(g_ctx->texture, NULL, buffer, (int)(g_ctx->width * sizeof(uint32_t)));
    
#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    if(video_filter_mode == 1) render_scanlines(g_ctx);
    ANDROID_RENDER_TOUCH_CONTROLS(g_ctx);
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif

    if (fps >= 0.0f) render_fps_text(g_ctx, fps);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
    SDL_RenderPresent(g_ctx->renderer);
}

// Função usada no Menu de Pausa para não atualizar a textura com lixo da memória
void render_frame_only(GraphicsContext* g_ctx) {
    SDL_RenderClear(g_ctx->renderer);
    // Desenha a textura existente (último frame válido)
#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    if(video_filter_mode == 1) render_scanlines(g_ctx);
    // Não desenha os controles de toque aqui para não sobrepor o menu
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif
    // Nota: Não chama SDL_RenderPresent aqui, o menu fará isso
}

static void render_scanlines(GraphicsContext* g_ctx) {
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 80);
    int start_y = (int)g_ctx->dest.y;
    int end_y = start_y + (int)g_ctx->dest.h;
    int width = (int)g_ctx->dest.w;
    int start_x = (int)g_ctx->dest.x;
    for (int y = start_y; y < end_y; y += 2) {
        SDL_RenderLine(g_ctx->renderer, start_x, y, start_x + width, y);
    }
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

static void render_fps_text(GraphicsContext* ctx, float fps) {
    if (!ctx->font) return;
    char fps_str[32];
    snprintf(fps_str, sizeof(fps_str), "FPS: %.1f", fps);
    SDL_Color color = {0, 255, 0, 255};
    SDL_Surface* surface = TTF_RenderText_Solid(ctx->font, fps_str, 0, color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
        if (texture) {
            SDL_FRect dest = {10.0f, 10.0f, (float)surface->w, (float)surface->h}; 
            SDL_RenderTexture(ctx->renderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture);
        }
        SDL_DestroySurface(surface);
    }
}

void free_graphics(GraphicsContext* ctx){
    TTF_CloseFont(ctx->font);
    TTF_Quit();
    SDL_DestroyTexture(ctx->texture);
    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_DestroyAudioStream(ctx->audio_stream);
    SDL_Quit();
    LOG(DEBUG, "Graphics clean up complete");
}

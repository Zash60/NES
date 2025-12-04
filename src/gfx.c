#include <SDL.h>
#include <stdio.h>
#include "gfx.h"
#include "utils.h"
#include "font.h"
#include "ppu.h" 

#ifdef __ANDROID__
#include "touchpad.h"
#endif

int video_filter_mode = 0; 

// Funções auxiliares locais
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

    ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);
    if(ctx->texture == NULL) quit(EXIT_FAILURE);

    SDL_SetTextureScaleMode(ctx->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);
}

// 1. Atualiza a textura com os pixels do emulador
void render_graphics_update(GraphicsContext* g_ctx, const uint32_t* buffer, uint8_t mask_reg){
    SDL_RenderClear(g_ctx->renderer);
    SDL_UpdateTexture(g_ctx->texture, NULL, buffer, g_ctx->width * sizeof(uint32_t));

#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    apply_masking(g_ctx, mask_reg);
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif
}

// 2. Desenha Hitboxes (Avançado)
void render_hitboxes(GraphicsContext* g_ctx, PPU* ppu) {
    // Habilita blend para transparência
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);

    // Determina altura do sprite (8 ou 16 pixels)
    int sprite_height = (ppu->ctrl & LONG_SPRITE) ? 16 : 8;
    int sprite_width = 8;

    // Fator de escala para desenhar na posição correta da tela
#ifdef __ANDROID__
    float scale_x = g_ctx->dest.w / 256.0f;
    float scale_y = g_ctx->dest.h / 240.0f;
    float offset_x = g_ctx->dest.x;
    float offset_y = g_ctx->dest.y;
#else
    float scale_x = 1.0f; // Logical Presentation cuida disso no Desktop
    float scale_y = 1.0f;
    float offset_x = 0;
    float offset_y = 0;
#endif

    // Itera sobre a OAM (64 sprites)
    for (int i = 0; i < 64; i++) {
        // Formato OAM: [Y] [Tile Index] [Attributes] [X]
        uint8_t y = ppu->OAM[i * 4];
        uint8_t x = ppu->OAM[i * 4 + 3];

        // Sprites com Y >= 240 geralmente são ocultos
        if (y >= 240) continue;

        // O hardware desenha o sprite na linha Y + 1
        float draw_x = offset_x + (x * scale_x);
        float draw_y = offset_y + ((y + 1) * scale_y);
        float draw_w = sprite_width * scale_x;
        float draw_h = sprite_height * scale_y;

        SDL_FRect rect = {draw_x, draw_y, draw_w, draw_h};

        // Sprite 0 (usado para timing) em Verde, outros em Vermelho
        if (i == 0) {
            SDL_SetRenderDrawColor(g_ctx->renderer, 0, 255, 0, 100); // Verde Translúcido
        } else {
            SDL_SetRenderDrawColor(g_ctx->renderer, 255, 0, 0, 80);  // Vermelho Translúcido
        }

        // Preenchimento
        SDL_RenderFillRect(g_ctx->renderer, &rect);
        
        // Borda Sólida
        SDL_SetRenderDrawColor(g_ctx->renderer, 255, 255, 255, 180);
        SDL_RenderRect(g_ctx->renderer, &rect);
    }

    // Restaura blend mode
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

// 3. Desenha UI e finaliza o frame
void render_ui_and_present(GraphicsContext* g_ctx, float fps) {
    if(video_filter_mode == 1) render_scanlines(g_ctx);

#ifdef __ANDROID__
    ANDROID_RENDER_TOUCH_CONTROLS(g_ctx);
#endif

    if (fps >= 0.0f) {
        render_fps_text(g_ctx, fps);
    }

    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
    SDL_RenderPresent(g_ctx->renderer);
}

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
    if (!(mask_reg & SHOW_BG_8) || !(mask_reg & SHOW_SPRITE_8)) {
        SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
        float scale_x = g_ctx->dest.w / (float)g_ctx->width;
        SDL_FRect mask_rect = { g_ctx->dest.x, g_ctx->dest.y, 8.0f * scale_x, g_ctx->dest.h };
        SDL_RenderFillRect(g_ctx->renderer, &mask_rect);
    }
}

static void render_scanlines(GraphicsContext* g_ctx) {
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 60);
    
    float start_x, width, start_y, end_y;
#ifdef __ANDROID__
    start_x = g_ctx->dest.x;
    width = g_ctx->dest.w;
    start_y = g_ctx->dest.y;
    end_y = start_y + g_ctx->dest.h;
#else
    start_x = 0;
    width = g_ctx->width * g_ctx->scale; // Assumindo logical presentation
    start_y = 0;
    end_y = g_ctx->height * g_ctx->scale;
#endif

    int step = (end_y - start_y > 1000) ? 3 : 2;
    for (float y = start_y; y < end_y; y += step) {
        SDL_RenderLine(g_ctx->renderer, start_x, y, start_x + width, y);
    }
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

static void render_fps_text(GraphicsContext* ctx, float fps) {
    if (!ctx->font) return;
    char fps_str[16];
    snprintf(fps_str, sizeof(fps_str), "%.0f", fps);
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
}

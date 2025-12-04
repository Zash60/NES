#include <SDL.h>
#include <stdio.h>

#include "gfx.h"
#include "utils.h"
#include "font.h"
#include "ppu.h" // Necessário para as flags SHOW_BG_8 e SHOW_SPRITE_8

#ifdef __ANDROID__
#include "touchpad.h"
#endif

int video_filter_mode = 0; // 0 = Normal, 1 = Scanlines

// Declarações locais
static void render_fps_text(GraphicsContext* ctx, float fps);
static void render_scanlines(GraphicsContext* g_ctx);
static void apply_masking(GraphicsContext* g_ctx, uint8_t mask_reg);

void get_graphics_context(GraphicsContext* ctx){

    SDL_Init(
        SDL_INIT_AUDIO |
        SDL_INIT_VIDEO |
        SDL_INIT_JOYSTICK |
        SDL_INIT_GAMEPAD |
        SDL_INIT_EVENTS |
        SDL_INIT_SENSOR
    );
    TTF_Init();
#ifdef __ANDROID__
    ctx->font = TTF_OpenFont("asap.ttf", (int)(ctx->screen_height * 0.05));
    if(ctx->font == NULL){
        LOG(WARN, "Font not found, FPS/Menu text disabled");
    }
    // Set on AndroidManifest.xml as well
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
    // Forçar orientação paisagem
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    
    ctx->window = SDL_CreateWindow(
        "AndroNES",
        0,
        0,
        SDL_WINDOW_FULLSCREEN
        | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
#else
    SDL_IOStream* rw = SDL_IOFromMem(font_data, sizeof(font_data));
    ctx->font = TTF_OpenFontIO(rw, 1, 20);
    if(ctx->font == NULL){
        LOG(ERROR, SDL_GetError());
    }
    ctx->window = SDL_CreateWindow(
        "NES Emulator",
        ctx->width * (int)ctx->scale,
        ctx->height * (int)ctx->scale,
        SDL_WINDOW_RESIZABLE
        | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
#endif

    if(ctx->window == NULL){
        LOG(ERROR, SDL_GetError());
        quit(EXIT_FAILURE);
    }
    
    ctx->renderer = SDL_CreateRenderer(ctx->window, NULL);
    if(ctx->renderer == NULL){
        LOG(ERROR, SDL_GetError());
        quit(EXIT_FAILURE);
    }

#ifdef __ANDROID__
    // Ajuste de Aspect Ratio 4:3 no mobile
    // O NES tem resolução 256x240. Em telas wide, deve ter barras laterais pretas ou esticar.
    // Vamos manter o aspect ratio correto (4:3 aproximadamente).
    float aspect = (float)256 / 240;
    int want_h = ctx->screen_height;
    int want_w = (int)(want_h * aspect); 
    
    // Centralizar na tela
    ctx->dest.h = (float)want_h;
    ctx->dest.w = (float)want_w;
    ctx->dest.x = (float)(ctx->screen_width - want_w) / 2;
    ctx->dest.y = 0;
#else
    SDL_SetRenderLogicalPresentation(
        ctx->renderer,
        ctx->width * ctx->scale,
        ctx->height * ctx->scale,
        SDL_LOGICAL_PRESENTATION_LETTERBOX
    );
#endif

    // Texture Streaming para performance
    ctx->texture = SDL_CreateTexture(
        ctx->renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        ctx->width,
        ctx->height
    );

    if(ctx->texture == NULL){
        LOG(ERROR, SDL_GetError());
        quit(EXIT_FAILURE);
    }

    SDL_SetTextureScaleMode(ctx->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);

    LOG(DEBUG, "Initialized SDL subsystem");
}

// Função principal de desenho (Jogo rodando)
void render_graphics(GraphicsContext* g_ctx, const uint32_t* buffer, float fps, uint8_t mask_reg){
    SDL_RenderClear(g_ctx->renderer);
    
    // Atualização otimizada de textura (Lock/Unlock)
    void* pixels;
    int pitch;
    if (SDL_LockTexture(g_ctx->texture, NULL, &pixels, &pitch) == 0) {
        // Copia o buffer linha por linha ou bloco inteiro
        memcpy(pixels, buffer, g_ctx->width * g_ctx->height * sizeof(uint32_t));
        SDL_UnlockTexture(g_ctx->texture);
    }

#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    
    // APLICA A MÁSCARA LATERAL (Esconde o lixo de memória à esquerda)
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

// Função para desenhar apenas o frame atual sem atualizar dados (Jogo Pausado/Menu)
void render_frame_only(GraphicsContext* g_ctx) {
    SDL_RenderClear(g_ctx->renderer);
#ifdef __ANDROID__
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, &g_ctx->dest);
    if(video_filter_mode == 1) render_scanlines(g_ctx);
    // Não desenha controles aqui para não sobrepor o menu
#else
    SDL_RenderTexture(g_ctx->renderer, g_ctx->texture, NULL, NULL);
#endif
    // Nota: SDL_RenderPresent NÃO é chamado aqui, pois o menu será desenhado por cima depois
}

// Desenha uma barra preta de 8 pixels na esquerda se o jogo solicitar (PPU Mask)
static void apply_masking(GraphicsContext* g_ctx, uint8_t mask_reg) {
    // Se o bit de exibir background na esquerda (bit 1) ou sprites na esquerda (bit 2) estiverem 0
    if (!(mask_reg & SHOW_BG_8) || !(mask_reg & SHOW_SPRITE_8)) {
        SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 255);
        
        // Calcular quanto 8 pixels do NES representam na tela do Android
        float scale_x = g_ctx->dest.w / (float)g_ctx->width;
        float mask_width = 8.0f * scale_x;
        
        SDL_FRect mask_rect = {
            g_ctx->dest.x,
            g_ctx->dest.y,
            mask_width,
            g_ctx->dest.h
        };
        SDL_RenderFillRect(g_ctx->renderer, &mask_rect);
    }
}

static void render_scanlines(GraphicsContext* g_ctx) {
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 60); // Alpha 60

    int start_y = (int)g_ctx->dest.y;
    int end_y = start_y + (int)g_ctx->dest.h;
    int width = (int)g_ctx->dest.w;
    int start_x = (int)g_ctx->dest.x;
    
    // Ajuste de densidade
    int step = 2;
    if(g_ctx->dest.h > 1000) step = 3; // Telas muito densas

    for (int y = start_y; y < end_y; y += step) {
        SDL_RenderLine(g_ctx->renderer, start_x, y, start_x + width, y);
    }
    
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

static void render_fps_text(GraphicsContext* ctx, float fps) {
    if (!ctx->font) return;

    char fps_str[16];
    snprintf(fps_str, sizeof(fps_str), "%.0f", fps);

    SDL_Color color = {0, 255, 0, 255}; // Verde
    SDL_Surface* surface = TTF_RenderText_Solid(ctx->font, fps_str, 0, color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
        if (texture) {
            // Canto superior esquerdo, mas respeitando o recorte da tela se houver
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

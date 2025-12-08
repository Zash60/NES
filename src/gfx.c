#include <SDL.h>
#include <stdio.h>
#include "gfx.h"
#include "utils.h"
#include "font.h"
#include "ppu.h" 
#include "emulator.h"

#ifdef __ANDROID__
#include "touchpad.h"
#endif

int video_filter_mode = 0; 

// Protótipos Locais
static void render_fps_text(GraphicsContext* ctx, float fps);
static void render_scanlines(GraphicsContext* g_ctx);
static void apply_masking(GraphicsContext* g_ctx, uint8_t mask_reg);
static void render_tas_osd(GraphicsContext* g_ctx, Emulator* emu);

void get_graphics_context(GraphicsContext* ctx){
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_SENSOR);
    TTF_Init();
#ifdef __ANDROID__
    ctx->font = TTF_OpenFont("asap.ttf", (int)(ctx->screen_height * 0.05));
    if(ctx->font == NULL) LOG(WARN, "Font not found: %s", SDL_GetError());
    
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    
    ctx->window = SDL_CreateWindow("AndroNES", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    SDL_IOStream* rw = SDL_IOFromMem(font_data, sizeof(font_data));
    ctx->font = TTF_OpenFontIO(rw, 1, 20);
    if(ctx->font == NULL) LOG(WARN, "Font not loaded from memory: %s", SDL_GetError());
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
    SDL_SetRenderLogicalPresentation(ctx->renderer, ctx->width * ctx->scale, ctx->height * ctx->scale, SDL_LOGICAL_PRESENTATION_LETTERBOX, SDL_SCALEMODE_NEAREST);
#endif

    ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);
    if(ctx->texture == NULL) quit(EXIT_FAILURE);

    SDL_SetTextureScaleMode(ctx->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);
}

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

void render_ui_and_present(GraphicsContext* g_ctx, float fps, Emulator* emu) {
    if(video_filter_mode == 1) render_scanlines(g_ctx);

#ifdef __ANDROID__
    ANDROID_RENDER_TOUCH_CONTROLS(g_ctx);
#endif

    if (fps >= 0.0f) render_fps_text(g_ctx, fps);
    render_tas_osd(g_ctx, emu);

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
    int w, h;
    SDL_GetRenderOutputSize(g_ctx->renderer, &w, &h);
    start_x = 0; width = (float)w; start_y = 0; end_y = (float)h;
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

// ----------------------------------------------------------------------
// TAS OSD - OTIMIZADO (Sem renderização de texto por frame para inputs)
// ----------------------------------------------------------------------

static void draw_input_indicator(SDL_Renderer* r, float x, float y, float w, float h, int active, SDL_Color on_color) {
    SDL_FRect rect = {x, y, w, h};
    
    if (active) {
        SDL_SetRenderDrawColor(r, on_color.r, on_color.g, on_color.b, 200); // Brilhante quando ativo
        SDL_RenderFillRect(r, &rect);
    } else {
        SDL_SetRenderDrawColor(r, 40, 40, 40, 100); // Escuro transparente quando inativo
        SDL_RenderFillRect(r, &rect);
    }
    
    // Borda
    SDL_SetRenderDrawColor(r, 150, 150, 150, 150);
    SDL_RenderRect(r, &rect);
}

static void render_tas_osd(GraphicsContext* g_ctx, Emulator* emu) {
    if (emu->movie.mode == MOVIE_MODE_INACTIVE && !emu->show_script_selector) return;
    if (!g_ctx->font) return;

    SDL_Renderer* r = g_ctx->renderer;
    
    // 1. Texto de Status (FPS e Frame Count) - Isso é leve pois é só uma string
    char osd_text[64];
    SDL_Color status_color = {255, 255, 255, 255};
    const char* mode_str = "";

    switch(emu->movie.mode) {
        case MOVIE_MODE_RECORDING: 
            mode_str = "REC"; 
            status_color = (SDL_Color){255, 50, 50, 255}; 
            break;
        case MOVIE_MODE_PLAYBACK:  
            mode_str = "PLAY"; 
            status_color = (SDL_Color){50, 255, 50, 255}; 
            if (emu->movie.read_only) mode_str = "PLAY [R/O]";
            break;
        case MOVIE_MODE_FINISHED:  
            mode_str = "FIN";  
            status_color = (SDL_Color){150, 150, 150, 255}; 
            break;
        default: break;
    }

    snprintf(osd_text, 64, "%s [%u / %u]", mode_str, emu->current_frame_index, emu->movie.frame_count);
    
    SDL_Surface* surf = TTF_RenderText_Solid(g_ctx->font, osd_text, 0, status_color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FRect dst = {
            (float)g_ctx->screen_width - surf->w - 20, 
            60.0f, 
            (float)surf->w, 
            (float)surf->h
        };
        SDL_RenderTexture(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    }

    // 2. Visualizador de Input (Otimizado: Sem Texto)
    uint16_t joy = emu->mem.joy1.status;
    
#ifdef __ANDROID__
    float btn_size = g_ctx->screen_height * 0.04f; // Um pouco menor para não poluir
#else
    float btn_size = 20.0f;
#endif
    float gap = 4.0f;
    float start_x = 20.0f;
    float start_y = 100.0f; 

    SDL_Color c_dpad = {200, 200, 200, 255};
    SDL_Color c_sel  = {100, 100, 255, 255};
    SDL_Color c_btn  = {255, 50, 50, 255};

    // D-Pad (Cima, Esquerda, Baixo, Direita)
    draw_input_indicator(r, start_x + btn_size + gap, start_y, btn_size, btn_size, joy & UP, c_dpad);
    draw_input_indicator(r, start_x, start_y + btn_size + gap, btn_size, btn_size, joy & LEFT, c_dpad);
    draw_input_indicator(r, start_x + btn_size + gap, start_y + (btn_size + gap)*2, btn_size, btn_size, joy & DOWN, c_dpad);
    draw_input_indicator(r, start_x + (btn_size + gap)*2, start_y + btn_size + gap, btn_size, btn_size, joy & RIGHT, c_dpad);

    float mid_x = start_x + (btn_size + gap)*3 + 10;
    float mid_y = start_y + btn_size + gap;
    
    // Select / Start (Barras horizontais)
    draw_input_indicator(r, mid_x, mid_y, btn_size * 1.5f, btn_size * 0.7f, joy & SELECT, c_sel);
    draw_input_indicator(r, mid_x + btn_size*1.5f + gap, mid_y, btn_size * 1.5f, btn_size * 0.7f, joy & START, c_sel);

    float action_x = mid_x + (btn_size*1.5f + gap)*2 + 10;
    
    // B / A
    draw_input_indicator(r, action_x, mid_y, btn_size, btn_size, joy & BUTTON_B, c_btn);
    draw_input_indicator(r, action_x + btn_size + gap, mid_y, btn_size, btn_size, joy & BUTTON_A, c_btn);
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

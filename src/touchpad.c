#ifdef __ANDROID__
#include <stdlib.h>
#include <math.h>
#include <SDL_ttf.h>
#include "utils.h"
#include "touchpad.h"
#include "emulator.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAVE_LOAD_COOLDOWN 500 

static TouchPad touch_pad;
static uint32_t last_touch_action_time = 0;

enum{ BUTTON_CIRCLE, BUTTON_LONG, BUTTON_SMALL };

static void init_button(struct TouchButton* button, uint32_t id, size_t index, int type, char* label, int x, int y, TTF_Font* font);
static void init_axis(GraphicsContext* ctx, int x, int y);
static uint8_t is_within_bound(int eventX, int eventY, SDL_FRect* bound);
static uint8_t is_within_circle(int eventX, int eventY, int centerX, int centerY, int radius);
static void to_abs_position(double* x, double* y);
static int angle(int x1, int y1, int x2, int y2);
static void update_joy_pos();

void init_touch_pad(struct Emulator* emulator){
    GraphicsContext* ctx = &emulator->g_ctx;
    if(ctx->is_tv) return;
    
    touch_pad.emulator = emulator;
    touch_pad.g_ctx = ctx;
    touch_pad.edit_mode = 0;
    touch_pad.selected_button = NULL;
    touch_pad.show_tas_toolbar = 0; 
    
    int font_size = (int)((ctx->screen_height * 0.08));
    touch_pad.font = TTF_OpenFont("asap.ttf", (font_size * 4)/3);
    TTF_Font* small_font = TTF_OpenFont("asap.ttf", (int)(font_size * 0.6));

    if(ctx->font == NULL || !touch_pad.font) LOG(ERROR, SDL_GetError());

    int offset = font_size * 3;
    int anchor_y = ctx->screen_height - offset;
    int anchor_x = ctx->screen_width - offset;
    int anchor_mid = (int)(ctx->screen_height * 0.3);
    int top_y = (int)(ctx->screen_height * 0.05); 
    int center_x = ctx->screen_width / 2;
    int tas_y = top_y + (int)(ctx->screen_height * 0.15); 

    init_button(&touch_pad.A, BUTTON_A, 0, BUTTON_CIRCLE, "A", anchor_x, anchor_y - offset/2, touch_pad.font);
    init_button(&touch_pad.turboA, TURBO_A, 1, BUTTON_CIRCLE, "X",  anchor_x, anchor_y + offset/2, touch_pad.font);
    init_button(&touch_pad.B, BUTTON_B, 2, BUTTON_CIRCLE, "B",  anchor_x - offset/2, anchor_y, touch_pad.font);
    init_button(&touch_pad.turboB, TURBO_B, 3, BUTTON_CIRCLE, "Y",  anchor_x + offset/2, anchor_y, touch_pad.font);
    init_button(&touch_pad.select, SELECT, 4, BUTTON_LONG,"Select",  offset, anchor_mid, ctx->font);
    init_button(&touch_pad.start, START, 5, BUTTON_LONG," Start ",  anchor_x, anchor_mid, ctx->font);
    
    init_button(&touch_pad.load, BTN_ID_LOAD, 6, BUTTON_LONG, "LOAD", offset, top_y, ctx->font);
    init_button(&touch_pad.menu, BTN_ID_MENU, 7, BUTTON_LONG, "MENU", center_x, top_y, ctx->font);
    init_button(&touch_pad.save, BTN_ID_SAVE, 8, BUTTON_LONG, "SAVE", ctx->screen_width - offset, top_y, ctx->font);

    // TAS UI
    init_button(&touch_pad.tas_toggle, BTN_ID_TAS_TOGGLE, 9, BUTTON_SMALL, "TAS", center_x + (int)(ctx->screen_width * 0.18), top_y, small_font);
    
    int tas_count = 5;
    int tas_btn_width = (int)(ctx->screen_width * 0.08); 
    int tas_spacing = tas_btn_width + 10;
    int tas_total_width = tas_spacing * tas_count;
    int tas_start_x = (ctx->screen_width - tas_total_width) / 2 + (tas_btn_width/2);
    
    init_button(&touch_pad.tas_rec,  BTN_ID_TAS_REC,  10, BUTTON_SMALL, "REC",  tas_start_x, tas_y, small_font);
    init_button(&touch_pad.tas_play, BTN_ID_TAS_PLAY, 11, BUTTON_SMALL, "PLAY", tas_start_x + tas_spacing, tas_y, small_font);
    init_button(&touch_pad.tas_slow, BTN_ID_TAS_SLOW, 12, BUTTON_SMALL, "SLW",  tas_start_x + tas_spacing*2, tas_y, small_font);
    init_button(&touch_pad.tas_step, BTN_ID_TAS_STEP, 13, BUTTON_SMALL, " >| ", tas_start_x + tas_spacing*3, tas_y, small_font);
    init_button(&touch_pad.tas_box,  BTN_ID_TAS_BOX,  14, BUTTON_SMALL, "LUA",  tas_start_x + tas_spacing*4, tas_y, small_font);

    init_axis(ctx, offset, anchor_y);
    if(small_font) TTF_CloseFont(small_font);
}

static void init_button(struct TouchButton* button, uint32_t id, size_t index, int type, char* label, int x, int y, TTF_Font* font){
    GraphicsContext* ctx = touch_pad.g_ctx;
    touch_pad.buttons[index] = button;
    
    SDL_Color color = {0xF9, 0x58, 0x1A}; 
    if(id >= BTN_ID_TAS_TOGGLE) { color.r=0; color.g=150; color.b=255; } 
    
    SDL_Surface* text_surf = TTF_RenderText_Solid(font, label, 0, color);
    if(!text_surf) {
        LOG(ERROR, "Failed to render text for button");
        return;
    }
    
    int w = text_surf->w + 40, h = text_surf->h + 20;
    if(type == BUTTON_SMALL) { w = text_surf->w + 30; h = text_surf->h + 15; }
    int r = h / 2;
    if(type == BUTTON_CIRCLE) w = h;
    
    button->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(button->texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(ctx->renderer, button->texture);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 0);
    SDL_RenderClear(ctx->renderer);
    
    SDL_SetRenderDrawColor(ctx->renderer, color.r, color.g, color.b, 80);
    
    if(w != h) {
        SDL_FRect rdest = {(float)r, 0, (float)(w - h), (float)h};
        SDL_RenderFillRect(ctx->renderer, &rdest);
        SDL_RenderFillCircle(ctx->renderer, r, r, r-1);
        SDL_RenderFillCircle(ctx->renderer, w-r, r, r-1);
        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 180);
        SDL_RenderLine(ctx->renderer, r, 0, w-r, 0);
        SDL_RenderLine(ctx->renderer, r, h-1, w-r, h-1);
    } else {
        SDL_RenderFillCircle(ctx->renderer, r, r, r-2);
        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 180);
        SDL_RenderDrawCircle(ctx->renderer, r, r, r-1);
    }
    
    SDL_Texture* text = SDL_CreateTextureFromSurface(ctx->renderer, text_surf);
    SDL_FRect dest = {(float)(w - text_surf->w)/2, (float)(h - text_surf->h)/2, (float)text_surf->w, (float)text_surf->h};
    SDL_RenderTexture(ctx->renderer, text, NULL, &dest);
    
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_DestroySurface(text_surf);
    SDL_DestroyTexture(text);

    button->x = x; button->y = y; button->r = r-1;
    button->dest.x = x - w/2; button->dest.y = y - h/2;
    button->dest.w = w; button->dest.h = h;
    button->id = id;
    button->active = 0;
}

static void init_axis(GraphicsContext* ctx, int x, int y){
    TouchAxis* axis = &touch_pad.axis;
    axis->r = (int)(0.09 * ctx->screen_height);
    axis->x = axis->inner_x = axis->origin_x = x;
    axis->y = axis->inner_y = axis->origin_y = y;
    int bg_r = axis->r + 30;
    axis->bg_dest.x = axis->x - bg_r; axis->bg_dest.y = axis->y - bg_r;
    axis->bg_dest.w = axis->bg_dest.h = bg_r * 2;
    axis->joy_dest.w = axis->joy_dest.h = axis->r * 2;
    axis->bg_tx = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, bg_r * 2, bg_r * 2);
    axis->joy_tx = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, axis->r * 2, axis->r * 2);
    SDL_SetTextureBlendMode(axis->bg_tx, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(axis->joy_tx, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(ctx->renderer, axis->bg_tx);
    SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 50);
    SDL_RenderDrawCircle(ctx->renderer, bg_r, bg_r, bg_r - 2);
    SDL_SetRenderTarget(ctx->renderer, axis->joy_tx);
    SDL_SetRenderDrawColor(ctx->renderer, 0xF9, 0x58, 0x1A, 180);
    SDL_RenderFillCircle(ctx->renderer, axis->r, axis->r, axis->r - 2);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void update_joy_pos(){
    touch_pad.axis.joy_dest.x = touch_pad.axis.inner_x - touch_pad.axis.r;
    touch_pad.axis.joy_dest.y = touch_pad.axis.inner_y - touch_pad.axis.r;
}

void render_touch_controls(GraphicsContext* ctx){
    if(ctx->is_tv) return;

    if(!touch_pad.axis.active){
        touch_pad.axis.inner_x = touch_pad.axis.x;
        touch_pad.axis.inner_y = touch_pad.axis.y;
        update_joy_pos();
    }
    SDL_RenderTexture(ctx->renderer, touch_pad.axis.bg_tx, NULL, &touch_pad.axis.bg_dest);
    SDL_RenderTexture(ctx->renderer, touch_pad.axis.joy_tx, NULL, &touch_pad.axis.joy_dest);

    for(int i = 0; i < TOUCH_BUTTON_COUNT; i++){
        TouchButton* button = touch_pad.buttons[i];
        if (!button) continue;

        if (button->id >= BTN_ID_TAS_REC && button->id <= BTN_ID_TAS_BOX && !touch_pad.show_tas_toolbar) {
            continue;
        }

        // --- Cores de Estado ---
        if (button->id == BTN_ID_TAS_REC) {
            if (touch_pad.emulator->is_recording) SDL_SetTextureColorMod(button->texture, 255, 0, 0); 
            else SDL_SetTextureColorMod(button->texture, 255, 255, 255);
        }
        else if (button->id == BTN_ID_TAS_PLAY) {
            if (touch_pad.emulator->is_playing) SDL_SetTextureColorMod(button->texture, 0, 255, 0);
            else SDL_SetTextureColorMod(button->texture, 255, 255, 255);
        }
        else if (button->id == BTN_ID_TAS_SLOW) {
            if (touch_pad.emulator->slow_motion_factor > 1.0f) SDL_SetTextureColorMod(button->texture, 255, 255, 0);
            else SDL_SetTextureColorMod(button->texture, 255, 255, 255);
        }
        else if (button->id == BTN_ID_TAS_BOX) {
            // CORREÇÃO: Usar lua_script_active em vez de show_hitboxes
            if (touch_pad.emulator->lua_script_active) SDL_SetTextureColorMod(button->texture, 255, 0, 255);
            else SDL_SetTextureColorMod(button->texture, 255, 255, 255);
        }

        if(touch_pad.edit_mode) SDL_SetTextureAlphaMod(button->texture, 255);
        else SDL_SetTextureAlphaMod(button->texture, 150); 

        SDL_RenderTexture(ctx->renderer, button->texture, NULL, &button->dest);
        
        SDL_SetTextureColorMod(button->texture, 255, 255, 255);
    }
}

void touchpad_mapper(struct JoyPad* joyPad, SDL_Event* event){
    if(!touch_pad.g_ctx || joyPad->player != 0) return;
    
    uint16_t key = joyPad->status;
    double x = event->tfinger.x, y = event->tfinger.y;
    uint32_t current_time = SDL_GetTicks();
    
    switch (event->type) {
        case SDL_EVENT_FINGER_UP: {
            to_abs_position(&x, &y);
            if (touch_pad.edit_mode) { touch_pad.selected_button = NULL; return; }

            for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
                TouchButton *btn = touch_pad.buttons[i];
                if (btn && btn->active && btn->finger == event->tfinger.fingerID) {
                    btn->active = 0;
                    btn->finger = -1;
                    if (btn->id < BTN_ID_SAVE) {
                         key &= ~btn->id;
                         if(btn->id == TURBO_A) key &= ~BUTTON_A;
                         if(btn->id == TURBO_B) key &= ~BUTTON_B;
                    }
                }
            }
            
            if (touch_pad.axis.finger == event->tfinger.fingerID) {
                touch_pad.axis.active = 0;
                touch_pad.axis.finger = -1;
                key &= ~(RIGHT | LEFT | UP | DOWN);
            }
            break;
        }
        case SDL_EVENT_FINGER_DOWN: {
            to_abs_position(&x, &y);
            int pressed = 0;
            for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
                TouchButton *btn = touch_pad.buttons[i];
                if (!btn) continue;
                
                // Ignora botões TAS escondidos
                if (btn->id >= BTN_ID_TAS_REC && btn->id <= BTN_ID_TAS_BOX && !touch_pad.show_tas_toolbar) continue;

                if ((i < 4 && is_within_circle((int)x, (int)y, btn->x, btn->y, btn->r)) ||
                    (i >= 4 && is_within_bound((int)x, (int)y, &btn->dest))) {
                    
                    if (touch_pad.edit_mode) { touch_pad.selected_button = btn; return; }
                    
                    pressed = 1;
                    btn->active = 1;
                    btn->finger = event->tfinger.fingerID;
                    
                    if (btn->id == BTN_ID_MENU) {
                        touch_pad.emulator->pause = !touch_pad.emulator->pause;
                    } else if (btn->id == BTN_ID_SAVE) {
                        if (current_time > last_touch_action_time + SAVE_LOAD_COOLDOWN) {
                            save_state(touch_pad.emulator, "save.dat");
                            last_touch_action_time = current_time;
                        }
                    } else if (btn->id == BTN_ID_LOAD) {
                         if (current_time > last_touch_action_time + SAVE_LOAD_COOLDOWN) {
                            load_state(touch_pad.emulator, "save.dat");
                            last_touch_action_time = current_time;
                        }
                    } 
                    else if (btn->id == BTN_ID_TAS_TOGGLE) {
                        touch_pad.show_tas_toolbar = !touch_pad.show_tas_toolbar;
                    }
                    else if (btn->id == BTN_ID_TAS_REC) {
                        tas_toggle_recording(touch_pad.emulator);
                    }
                    else if (btn->id == BTN_ID_TAS_PLAY) {
                        tas_toggle_playback(touch_pad.emulator);
                    }
                    else if (btn->id == BTN_ID_TAS_SLOW) {
                        tas_toggle_slow_motion(touch_pad.emulator);
                    }
                    else if (btn->id == BTN_ID_TAS_STEP) {
                        tas_step_frame(touch_pad.emulator);
                    }
                    else if (btn->id == BTN_ID_TAS_BOX) {
                        tas_toggle_lua_script(touch_pad.emulator);
                    }
                    else if (!touch_pad.emulator->pause) {
                        key |= btn->id;
                        if(btn->id == TURBO_A) key |= BUTTON_A;
                        if(btn->id == TURBO_B) key |= BUTTON_B;
                    }
                }
            }
            if(pressed) break;

            if (!touch_pad.emulator->pause && !touch_pad.edit_mode && x < touch_pad.g_ctx->screen_width / 2) {
                touch_pad.axis.active = 1;
                touch_pad.axis.finger = event->tfinger.fingerID;
                touch_pad.axis.x = touch_pad.axis.inner_x = (int)x;
                touch_pad.axis.y = touch_pad.axis.inner_y = (int)y;
            }
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            to_abs_position(&x, &y);
            if (touch_pad.edit_mode && touch_pad.selected_button) {
                touch_pad.selected_button->x = (int)x;
                touch_pad.selected_button->y = (int)y;
                touch_pad.selected_button->dest.x = x - touch_pad.selected_button->dest.w/2;
                touch_pad.selected_button->dest.y = y - touch_pad.selected_button->dest.h/2;
                return;
            }
            if (touch_pad.emulator->pause || !touch_pad.axis.active || touch_pad.axis.finger != event->tfinger.fingerID) break;

            int a = angle(touch_pad.axis.x, touch_pad.axis.y, (int)x, (int)y);
            int d = (int)sqrt(pow(x - touch_pad.axis.x, 2) + pow(y - touch_pad.axis.y, 2));
            int r = touch_pad.axis.r + 30;

            if (d > r) {
                touch_pad.axis.inner_x = (int)(r * cos(a * M_PI / 180) + touch_pad.axis.x);
                touch_pad.axis.inner_y = (int)(r * sin(a * M_PI / 180) + touch_pad.axis.y);
            } else {
                touch_pad.axis.inner_x = (int)x;
                touch_pad.axis.inner_y = (int)y;
            }
            update_joy_pos();

            if (d > touch_pad.axis.r / 2) {
                if(a < 60 || a > 300) key |= RIGHT; else key &= ~RIGHT;
                if(a > 30 && a < 150) key |= DOWN; else key &= ~DOWN;
                if(a > 120 && a < 240) key |= LEFT; else key &= ~LEFT;
                if(a > 210 && a < 330) key |= UP; else key &= ~UP;
            } else {
                 key &= ~(RIGHT | LEFT | UP | DOWN);
            }
        }
    }
    joyPad->status = key;
}

void toggle_edit_mode() { touch_pad.edit_mode = !touch_pad.edit_mode; }
uint8_t is_edit_mode() { return touch_pad.edit_mode; }
int is_tas_toolbar_open() { return touch_pad.show_tas_toolbar; }
static void to_abs_position(double* x, double* y){ *x = *x * (double)touch_pad.g_ctx->screen_width; *y = *y * (double)touch_pad.g_ctx->screen_height; }
static uint8_t is_within_bound(int eventX, int eventY, SDL_FRect* bound){ return eventX > bound->x && eventX < (bound->x + bound->w) && eventY > bound->y && (eventY < bound->y + bound->h); }
static uint8_t is_within_circle(int eventX, int eventY, int centerX, int centerY, int radius){ return abs(centerX - eventX) < radius && abs(centerY - eventY) < radius; }
static int angle(int x1, int y1, int x2, int y2){ int a = (int)(atan2(y2 - y1, x2 - x1) * 180 / M_PI); return a < 0 ? 360 + a : a; }
void free_touch_pad(){
    if(!touch_pad.g_ctx) return;
    for(int i = 0; i < TOUCH_BUTTON_COUNT; i++)
        if(touch_pad.buttons[i] && touch_pad.buttons[i]->texture)
            SDL_DestroyTexture(touch_pad.buttons[i]->texture);
    SDL_DestroyTexture(touch_pad.axis.joy_tx);
    SDL_DestroyTexture(touch_pad.axis.bg_tx);
}
#endif

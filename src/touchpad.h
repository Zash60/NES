#pragma once

#ifdef __ANDROID__

#include <stdint.h>
#include <SDL.h>

#include "gfx.h"
#include "controller.h"

struct Emulator; // Forward declaration

typedef struct TouchAxis{
    int x, y, r;
    int inner_x, inner_y;
    int origin_x, origin_y;
    uint8_t state, active;
    uint8_t h_latch, v_latch;
    SDL_FingerID finger;
    SDL_FRect bg_dest, joy_dest;
    SDL_Texture* bg_tx;
    SDL_Texture* joy_tx;
} TouchAxis;

typedef struct TouchButton{
    SDL_Texture * texture;
    SDL_FRect dest;
    uint32_t id;
    int x, y, r;
    uint8_t active;
    uint8_t auto_render;
    SDL_FingerID finger;
} TouchButton;

// 6 Jogo + 1 Menu + 1 Save + 1 Load = 9 Botões
#define TOUCH_BUTTON_COUNT 9

// IDs Especiais
#define BTN_ID_MENU 0x30000
#define BTN_ID_SAVE 0x10000
#define BTN_ID_LOAD 0x20000

typedef struct TouchPad{
    uint16_t status;
    // Mapeamento direto para facilitar acesso
    TouchButton A, turboA, B, turboB, select, start;
    TouchButton menu, save, load;
    
    TouchButton* buttons[TOUCH_BUTTON_COUNT];
    TouchAxis axis;
    GraphicsContext* g_ctx;
    struct Emulator* emulator; 
    TTF_Font * font;
    
    uint8_t edit_mode; 
    TouchButton* selected_button;
} TouchPad;

struct JoyPad;

void init_touch_pad(struct Emulator* emulator);
void free_touch_pad();
void render_touch_controls(GraphicsContext* ctx);
void touchpad_mapper(struct JoyPad* joyPad, SDL_Event* event);

#define ANDROID_INIT_TOUCH_PAD(EMU) init_touch_pad(EMU)
#define ANDROID_FREE_TOUCH_PAD() free_touch_pad()
#define ANDROID_RENDER_TOUCH_CONTROLS(CTX) render_touch_controls(CTX)
#define ANDROID_TOUCHPAD_MAPPER(JOYPAD, EVENT) touchpad_mapper(JOYPAD, EVENT)

#else
#define ANDROID_INIT_TOUCH_PAD(EMU)
#define ANDROID_FREE_TOUCH_PAD()
#define ANDROID_RENDER_TOUCH_CONTROLS(CTX)
#define ANDROID_TOUCHPAD_MAPPER(JOYPAD, EVENT)
#endif

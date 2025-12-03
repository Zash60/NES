#pragma once

#ifdef __ANDROID__

#include <stdint.h>
#include <SDL.h>

#include "gfx.h"
#include "controller.h"

// Forward declaration
struct Emulator;

typedef struct TouchAxis{
    int x;
    int y;
    int r;
    int inner_x;
    int inner_y;
    int origin_x;
    int origin_y;
    uint8_t state;
    uint8_t active;
    uint8_t h_latch;
    uint8_t v_latch;
    SDL_FingerID finger;
    SDL_FRect bg_dest;
    SDL_FRect joy_dest;
    SDL_Texture* bg_tx;
    SDL_Texture* joy_tx;
} TouchAxis;


typedef struct  TouchButton{
    SDL_Texture * texture;
    SDL_FRect dest;
    uint32_t id;
    int x;
    int y;
    int r;
    uint8_t active;
    uint8_t auto_render;
    SDL_FingerID finger;
} TouchButton;

// Aumentado para incluir Menu (8 botões no total: A, B, X, Y, Select, Start, Menu)
// Save/Load foram removidos daqui para o menu de pausa, mas o Menu Button é essencial
#define TOUCH_BUTTON_COUNT 7
#define BTN_ID_MENU 0x30000

typedef struct TouchPad{
    uint16_t status;
    TouchButton A;
    TouchButton turboA;
    TouchButton B;
    TouchButton turboB;
    TouchButton select;
    TouchButton start;
    TouchButton menu;
    
    TouchButton* buttons[TOUCH_BUTTON_COUNT];
    TouchAxis axis;
    GraphicsContext* g_ctx;
    struct Emulator* emulator;
    TTF_Font * font;
    
    // Flags para modo de edição
    uint8_t edit_mode; 
    TouchButton* selected_button;
} TouchPad;

// forward declaration
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

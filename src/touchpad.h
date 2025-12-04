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

// 6 Jogo + 3 Sistema + 6 TAS = 15 Botões
#define TOUCH_BUTTON_COUNT 15

// IDs Especiais de Sistema
#define BTN_ID_MENU 0x30000
#define BTN_ID_SAVE 0x10000
#define BTN_ID_LOAD 0x20000

// IDs Especiais de TAS
#define BTN_ID_TAS_TOGGLE 0x40000
#define BTN_ID_TAS_REC    0x40001
#define BTN_ID_TAS_PLAY   0x40002
#define BTN_ID_TAS_SLOW   0x40003
#define BTN_ID_TAS_STEP   0x40004
#define BTN_ID_TAS_BOX    0x40005 // Botão LUA/Box

typedef struct TouchPad{
    uint16_t status;
    
    // Botões mapeados para acesso direto
    TouchButton A, turboA, B, turboB, select, start;
    TouchButton menu, save, load;
    
    // Botões TAS
    TouchButton tas_toggle, tas_rec, tas_play, tas_slow, tas_step, tas_box;
    uint8_t show_tas_toolbar; // Flag para mostrar/esconder a barra TAS
    
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
void toggle_edit_mode();
uint8_t is_edit_mode();

// Função auxiliar para o loop principal saber se deve desenhar o menu de pausa
int is_tas_toolbar_open();

#define ANDROID_INIT_TOUCH_PAD(EMU) init_touch_pad(EMU)
#define ANDROID_FREE_TOUCH_PAD() free_touch_pad()
#define ANDROID_RENDER_TOUCH_CONTROLS(CTX) render_touch_controls(CTX)
#define ANDROID_TOUCHPAD_MAPPER(JOYPAD, EVENT) touchpad_mapper(JOYPAD, EVENT)

#else
// Stubs para Desktop
#define ANDROID_INIT_TOUCH_PAD(EMU)
#define ANDROID_FREE_TOUCH_PAD()
#define ANDROID_RENDER_TOUCH_CONTROLS(CTX)
#define ANDROID_TOUCHPAD_MAPPER(JOYPAD, EVENT)
#endif

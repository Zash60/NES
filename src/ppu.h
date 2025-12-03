#pragma once

#include <stdint.h>

#include "utils.h"
#include "mapper.h"

#define VISIBLE_SCANLINES 240
#define VISIBLE_DOTS 256
#define NTSC_SCANLINES_PER_FRAME 261
#define PAL_SCANLINES_PER_FRAME 311
#define DOTS_PER_SCANLINE 341
#define END_DOT 340

enum{
    BG_TABLE        = 1 << 4,
    SPRITE_TABLE    = 1 << 3,
    SHOW_BG_8       = 1 << 1,
    SHOW_SPRITE_8   = 1 << 2,
    SHOW_BG         = 1 << 3,
    SHOW_SPRITE     = 1 << 4,
    LONG_SPRITE     = 1 << 5,
    SPRITE_0_HIT    = 1 << 6,
    FLIP_HORIZONTAL = 1 << 6,
    FLIP_VERTICAL   = 1 << 7,
    V_BLANK         = 1 << 7,
    GENERATE_NMI    = 1 << 7,
    RENDER_ENABLED  = 0x18,
    BASE_NAMETABLE  = 0x3,
    FINE_Y          = 0x7000,
    COARSE_Y        = 0x3E0,
    COARSE_X        = 0x1F,
    VERTICAL_BITS   = 0x7BE0,
    HORIZONTAL_BITS = 0x41F,
    X_SCROLL_BITS   = 0x1f,
    Y_SCROLL_BITS   = 0x73E0
};

// Enum para seleção de paletas
typedef enum {
    PALETTE_DEFAULT = 0,
    PALETTE_SONY_CXA,
    PALETTE_FCEUX,
    PALETTE_COUNT
} PaletteType;

struct Emulator;

typedef struct PPU{
    size_t frames;
    uint32_t *screen;
    uint8_t V_RAM[0x1000];
    uint8_t OAM[256];
    uint8_t OAM_cache[8];
    uint8_t palette[0x20];
    uint8_t OAM_cache_len;
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    size_t dots;
    size_t scanlines;
    uint16_t scanlines_per_frame;

    uint16_t v;
    uint16_t t;
    uint8_t x;
    uint8_t w;
    uint8_t oam_address;
    uint8_t buffer;

    uint8_t render;
    uint8_t bus;

    // Indice da paleta atual
    int current_palette_index;

    struct Emulator* emulator;
    Mapper* mapper;
} PPU;

extern uint32_t nes_palette[64];

void execute_ppu(PPU* ppu);
void reset_ppu(PPU* ppu);
void exit_ppu(PPU* ppu);
void init_ppu(struct Emulator* emulator);
uint8_t read_status(PPU* ppu);
uint8_t read_ppu(PPU* ppu);
void set_ctrl(PPU* ppu, uint8_t ctrl);
void write_ppu(PPU* ppu, uint8_t value);
void dma(PPU* ppu, uint8_t value);
void set_scroll(PPU* ppu, uint8_t coord);
void set_address(PPU* ppu, uint8_t address);
void set_oam_address(PPU* ppu, uint8_t address);
uint8_t read_oam(PPU* ppu);
void write_oam(PPU* ppu, uint8_t value);
uint8_t read_vram(PPU* ppu, uint16_t address);
void write_vram(PPU* ppu, uint16_t address, uint8_t value);

// Nova função para trocar paleta
void set_emulator_palette(PPU* ppu, int palette_index);

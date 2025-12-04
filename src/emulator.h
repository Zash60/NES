#pragma once

#include "cpu6502.h"
#include "ppu.h"
#include "mmu.h"
#include "apu.h"
#include "mapper.h"
#include "gfx.h"
#include "timers.h"


// frame rate in Hz
#define NTSC_FRAME_RATE 60
#define PAL_FRAME_RATE 50

// turbo keys toggle rate (Hz)
#define NTSC_TURBO_RATE 30
#define PAL_TURBO_RATE 25

// sleep time when emulator is paused in milliseconds
#define IDLE_SLEEP 50


typedef struct Emulator{
    c6502 cpu;
    PPU ppu;
    APU apu;
    Memory mem;
    Mapper mapper;
    GraphicsContext g_ctx;
    Timer timer;

    TVSystem type;

    double time_diff;

    uint8_t exit;
    uint8_t pause;
    
    // Novos campos para Save State
    char rom_name[256];     // Nome base da ROM
    int current_save_slot;  // Slot atual (0-9)
} Emulator;


void init_emulator(Emulator* emulator, int argc, char *argv[]);
void reset_emulator(Emulator* emulator);
void run_emulator(Emulator* emulator);
void save_state(Emulator* emulator, const char* filename_unused);
void load_state(Emulator* emulator, const char* filename_unused);
void increment_save_slot(Emulator* emulator);
void run_NSF_player(Emulator* emulator);
void free_emulator(Emulator* emulator);

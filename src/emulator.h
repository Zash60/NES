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

// --- TAS CONSTANTS ---
#define MAX_MOVIE_FRAMES 216000 
#define TAS_HEADER_MAGIC 0x54415331 

// Constantes para o Seletor de Scripts
#define MAX_SCRIPTS 32
#define MAX_FILENAME_LEN 64

typedef struct {
    uint16_t joy1_status;
    uint16_t joy2_status;
} FrameInput;

typedef struct {
    uint32_t magic;
    uint32_t frame_count;
    FrameInput* frames; 
} TASMovie;

// Forward declaration
struct LuaContext;

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
    
    char rom_name[256];
    int current_save_slot;

    // --- TAS FIELDS ---
    TASMovie movie;
    uint32_t current_frame_index;
    uint8_t is_recording;
    uint8_t is_playing;
    uint8_t step_frame;         
    float slow_motion_factor;   
    
    uint8_t lua_script_active; 

    // --- SCRIPT SELECTOR FIELDS ---
    uint8_t show_script_selector;      // Flag para abrir o menu de scripts
    char script_list[MAX_SCRIPTS][MAX_FILENAME_LEN]; // Cache de nomes de arquivos
    int script_count;                  // Total de arquivos encontrados

    // --- LUA CONTEXT ---
    struct LuaContext* lua_ctx; 
} Emulator;

void init_emulator(Emulator* emulator, int argc, char *argv[]);
void reset_emulator(Emulator* emulator);
void run_emulator(Emulator* emulator);
void save_state(Emulator* emulator, const char* filename_unused);
void load_state(Emulator* emulator, const char* filename_unused);
void increment_save_slot(Emulator* emulator);
void run_NSF_player(Emulator* emulator);
void free_emulator(Emulator* emulator);

// Funções TAS
void tas_init(Emulator* emu);
void tas_toggle_recording(Emulator* emu);
void tas_toggle_playback(Emulator* emu);
void tas_toggle_slow_motion(Emulator* emu);
void tas_step_frame(Emulator* emu);
void tas_open_script_selector(Emulator* emu); // Alterado de toggle_lua_script para open_selector
void tas_save_movie(Emulator* emu, const char* filename);
void tas_load_movie(Emulator* emu, const char* filename);

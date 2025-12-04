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

// Modos de Filme TAS
typedef enum {
    MOVIE_MODE_INACTIVE,
    MOVIE_MODE_PLAYBACK,
    MOVIE_MODE_RECORDING,
    MOVIE_MODE_FINISHED
} TASMovieMode;

typedef struct {
    uint16_t joy1_status;
    uint16_t joy2_status;
} FrameInput;

typedef struct {
    uint64_t guid;              // Identificador único do filme
    TASMovieMode mode;          // Modo atual (Inactive, Play, Record, Finished)
    uint8_t read_only;          // Se o filme pode ser modificado ao carregar um estado
    uint32_t frame_count;       // Comprimento total do filme
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
    uint32_t current_frame_index; // "Cabeça de leitura/gravação" do filme
    uint8_t needs_truncation;   // Flag para truncar o filme no próximo frame

    float slow_motion_factor;   
    uint8_t lua_script_active; 
    uint8_t step_frame;

    // --- SCRIPT SELECTOR FIELDS ---
    uint8_t show_script_selector;
    char script_list[MAX_SCRIPTS][MAX_FILENAME_LEN];
    int script_count;

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
void tas_start_recording(Emulator* emu);
void tas_stop_movie(Emulator* emu);
void tas_start_playback(Emulator* emu, int read_only);
void tas_toggle_slow_motion(Emulator* emu);
void tas_step_frame(Emulator* emu);
void tas_open_script_selector(Emulator* emu);

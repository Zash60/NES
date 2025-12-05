#include "emulator.h"
#include "gamepad.h"
#include "touchpad.h"
#include "controller.h"
#include "gfx.h"
#include "mapper.h"
#include "nsf.h"
#include "timers.h"
#include "debugtools.h"
#include "utils.h"
#include "lua_bridge.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

static uint64_t PERIOD;
static uint16_t TURBO_SKIP;

#define SAVE_LOAD_COOLDOWN 1000
#define SAVE_MAGIC 0x4E45535C 
#define SAVE_VERSION 5

#ifdef __ANDROID__
    #define SCRIPT_PATH "/storage/emulated/0/Android/data/com.barracoder.android/files/"
#else
    #define SCRIPT_PATH "./"
#endif

static uint32_t last_state_action_time = 0;

typedef struct {
    char label[32];
    SDL_Rect rect;
    int action_code;
} MenuOption;

#define MENU_COUNT 8
MenuOption menu_options[MENU_COUNT];

// Protótipos
void toggle_edit_mode();
uint8_t is_edit_mode();
void refresh_script_list(Emulator* emu);
void render_script_selector_ui(Emulator* emu);
void handle_script_selector_input(Emulator* emu, int x, int y);
void tas_load_script_absolute(Emulator* emu, const char* full_path);
void init_menu_layout(int screen_w, int screen_h);
void render_pause_menu(GraphicsContext* g_ctx);
void handle_menu_touch(int x, int y, Emulator* emu);
void create_default_script(Emulator* emu);
extern int is_tas_toolbar_open();

// --- FUNÇÕES DE IMPLEMENTAÇÃO ---

void create_default_script(Emulator* emu) {
    char full_path[1024];
    snprintf(full_path, 1024, "%sexample_box.lua", SCRIPT_PATH);

    FILE* f = fopen(full_path, "r");
    if (f) {
        fclose(f);
    } else {
        #ifdef _WIN32
        _mkdir(SCRIPT_PATH);
        #else
        mkdir(SCRIPT_PATH, 0777);
        #endif
        
        f = fopen(full_path, "w");
        if (f) {
            fprintf(f, "-- Exemplo de Hitbox (Mario Bros)\n");
            fprintf(f, "while true do\n");
            fprintf(f, "    local mx = memory.readbyte(0x0086)\n");
            fprintf(f, "    local my = memory.readbyte(0x00CE)\n");
            fprintf(f, "    if mx > 0 and my > 0 then\n");
            fprintf(f, "        gui.drawbox(mx, my, mx+16, my+24, \"green\")\n");
            fprintf(f, "    end\n");
            fprintf(f, "    FCEU.frameadvance()\n");
            fprintf(f, "end\n");
            fclose(f);
        }
    }
}

static uint64_t generate_guid() {
    time_t t;
    srand((unsigned) time(&t));
    uint64_t r1 = rand();
    uint64_t r2 = rand();
    return (r1 << 32) | r2;
}

void tas_init(Emulator* emu) {
    emu->movie.frames = (FrameInput*)calloc(MAX_MOVIE_FRAMES, sizeof(FrameInput));
    if (!emu->movie.frames) { LOG(ERROR, "Failed to allocate memory for TAS movie"); return; }
    emu->movie.mode = MOVIE_MODE_INACTIVE;
    emu->movie.read_only = 0;
    emu->movie.frame_count = 0;
    emu->movie.guid = 0;
    emu->current_frame_index = 0;
    emu->needs_truncation = 0;
    emu->step_frame = 0;
    emu->slow_motion_factor = 1.0f;
    emu->lua_script_active = 0;
    emu->show_script_selector = 0;
    emu->script_count = 0;
    lua_bridge_init(emu);
}

void tas_save_movie(Emulator* emu, const char* filename) {
    char full_path[1024];
    snprintf(full_path, 1024, "%s%s", SCRIPT_PATH, filename);
    FILE* f = fopen(full_path, "wb");
    if (f) {
        uint32_t magic = TAS_HEADER_MAGIC;
        fwrite(&magic, sizeof(uint32_t), 1, f);
        fwrite(&emu->movie.frame_count, sizeof(uint32_t), 1, f);
        fwrite(emu->movie.frames, sizeof(FrameInput), emu->movie.frame_count, f);
        fclose(f);
    }
}

void tas_load_movie(Emulator* emu, const char* filename) {
    char full_path[1024];
    snprintf(full_path, 1024, "%s%s", SCRIPT_PATH, filename);
    FILE* f = fopen(full_path, "rb");
    if (f) {
        uint32_t magic;
        fread(&magic, sizeof(uint32_t), 1, f);
        if (magic == TAS_HEADER_MAGIC) {
            fread(&emu->movie.frame_count, sizeof(uint32_t), 1, f);
            if (emu->movie.frame_count > MAX_MOVIE_FRAMES) emu->movie.frame_count = MAX_MOVIE_FRAMES;
            fread(emu->movie.frames, sizeof(FrameInput), emu->movie.frame_count, f);
        }
        fclose(f);
    }
}

void tas_start_recording(Emulator* emu) {
    LOG(INFO, "TAS: Recording Started");
    emu->movie.mode = MOVIE_MODE_RECORDING;
    emu->movie.read_only = 0;
    emu->movie.guid = generate_guid();
    emu->movie.frame_count = 0;
    emu->current_frame_index = 0;
    reset_emulator(emu);
}

void tas_stop_movie(Emulator* emu) {
    LOG(INFO, "TAS: Movie Stopped");
    if (emu->movie.mode == MOVIE_MODE_RECORDING) {
        tas_save_movie(emu, "recording.tas");
    }
    emu->movie.mode = MOVIE_MODE_INACTIVE;
    emu->movie.guid = 0;
    emu->movie.frame_count = 0;
    emu->current_frame_index = 0;
}

void tas_start_playback(Emulator* emu, int read_only) {
    tas_load_movie(emu, "recording.tas");
    if (emu->movie.frame_count > 0) {
        LOG(INFO, "TAS: Playback Started (Read-Only: %d)", read_only);
        emu->movie.mode = MOVIE_MODE_PLAYBACK;
        emu->movie.read_only = read_only;
        emu->current_frame_index = 0;
        reset_emulator(emu);
    } else {
        LOG(WARN, "TAS: No movie data to play");
    }
}

void tas_toggle_slow_motion(Emulator* emu) {
    if (emu->slow_motion_factor == 1.0f) emu->slow_motion_factor = 2.0f;
    else if (emu->slow_motion_factor == 2.0f) emu->slow_motion_factor = 4.0f;
    else emu->slow_motion_factor = 1.0f;
    LOG(INFO, "TAS: Speed Factor %.0f%%", 100.0/emu->slow_motion_factor);
}

void tas_step_frame(Emulator* emu) {
    emu->step_frame = 1;
    emu->pause = 0; 
}

void tas_open_script_selector(Emulator* emu) {
    if (emu->show_script_selector) {
        emu->show_script_selector = 0;
    } else {
        refresh_script_list(emu);
        emu->show_script_selector = 1;
    }
}

typedef struct { uint32_t magic; uint32_t version; uint64_t movie_guid; uint32_t savestate_frame_count; uint32_t movie_length; } SaveHeader_v5;
typedef struct { uint16_t pc; uint8_t ac, x, y, sr, sp; size_t t_cycles; } CPUSnapshot;
typedef struct { uint8_t V_RAM[0x1000]; uint8_t OAM[256]; uint8_t palette[0x20]; uint8_t ctrl, mask, status; uint8_t oam_address; uint16_t v, t; uint8_t x, w; uint8_t buffer; } PPUSnapshot;
typedef struct { uint64_t prg_ptr_offset; uint64_t chr_ptr_offset; Mirroring mirroring; int has_extension; uint8_t extension_data[2048]; size_t ram_size; } MapperSnapshot;

static void get_slot_filename(Emulator* emu, char* buffer, size_t size) {
    snprintf(buffer, size, "%s%s_slot%d.save", SCRIPT_PATH, emu->rom_name, emu->current_save_slot);
}

void save_state(Emulator* emulator, const char* unused) {
    uint32_t now = SDL_GetTicks();
    if (now < last_state_action_time + SAVE_LOAD_COOLDOWN) return;
    last_state_action_time = now;

    char full_path[1024];
    get_slot_filename(emulator, full_path, sizeof(full_path));
    FILE* f = fopen(full_path, "wb");
    if (!f) return;

    SaveHeader_v5 header = { 
        .magic = SAVE_MAGIC, 
        .version = SAVE_VERSION,
        .movie_guid = emulator->movie.guid,
        .savestate_frame_count = emulator->current_frame_index,
        .movie_length = emulator->movie.frame_count
    };
    fwrite(&header, sizeof(SaveHeader_v5), 1, f);

    CPUSnapshot cpu_snap = { .pc = emulator->cpu.pc, .ac = emulator->cpu.ac, .x = emulator->cpu.x, .y = emulator->cpu.y, .sr = emulator->cpu.sr, .sp = emulator->cpu.sp, .t_cycles = emulator->cpu.t_cycles };
    fwrite(&cpu_snap, sizeof(CPUSnapshot), 1, f);
    fwrite(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);
    PPUSnapshot ppu_snap; memcpy(ppu_snap.V_RAM, emulator->ppu.V_RAM, 0x1000); memcpy(ppu_snap.OAM, emulator->ppu.OAM, 256); memcpy(ppu_snap.palette, emulator->ppu.palette, 0x20); ppu_snap.ctrl = emulator->ppu.ctrl; ppu_snap.mask = emulator->ppu.mask; ppu_snap.status = emulator->ppu.status; ppu_snap.oam_address = emulator->ppu.oam_address; ppu_snap.v = emulator->ppu.v; ppu_snap.t = emulator->ppu.t; ppu_snap.x = emulator->ppu.x; ppu_snap.w = emulator->ppu.w; ppu_snap.buffer = emulator->ppu.buffer;
    fwrite(&ppu_snap, sizeof(PPUSnapshot), 1, f);
    fwrite(&emulator->apu, sizeof(APU), 1, f);
    Mapper* m = &emulator->mapper; MapperSnapshot map_snap = {0}; map_snap.prg_ptr_offset = (m->PRG_ptr && m->PRG_ROM) ? (m->PRG_ptr - m->PRG_ROM) : 0; map_snap.chr_ptr_offset = (m->CHR_ptr && m->CHR_ROM) ? (m->CHR_ptr - m->CHR_ROM) : 0; map_snap.mirroring = m->mirroring; map_snap.ram_size = m->RAM_size; if (m->extension) { map_snap.has_extension = 1; memcpy(map_snap.extension_data, m->extension, sizeof(map_snap.extension_data)); }
    fwrite(&map_snap, sizeof(MapperSnapshot), 1, f);
    if (m->PRG_RAM && m->RAM_size > 0) fwrite(m->PRG_RAM, 1, m->RAM_size, f);
    
    if (emulator->movie.mode != MOVIE_MODE_INACTIVE) {
        fwrite(emulator->movie.frames, sizeof(FrameInput), emulator->movie.frame_count, f);
    }

    fclose(f);
    LOG(INFO, "State Saved!");
}

void load_state(Emulator* emulator, const char* unused) {
    uint32_t now = SDL_GetTicks();
    if (now < last_state_action_time + SAVE_LOAD_COOLDOWN) return;
    last_state_action_time = now;

    char full_path[1024];
    get_slot_filename(emulator, full_path, sizeof(full_path));
    FILE* f = fopen(full_path, "rb");
    if (!f) return;

    SaveHeader_v5 sv_header;
    if (fread(&sv_header, sizeof(SaveHeader_v5), 1, f) != 1 || sv_header.magic != SAVE_MAGIC || sv_header.version != SAVE_VERSION) { 
        LOG(ERROR, "Incompatible save state!");
        fclose(f); return; 
    }

    if (emulator->movie.mode != MOVIE_MODE_INACTIVE) {
        if (emulator->movie.guid != sv_header.movie_guid) { LOG(ERROR, "Savestate movie GUID mismatch! Current: %llu, Savestate: %llu", emulator->movie.guid, sv_header.movie_guid); fclose(f); return; }

        FrameInput* savestate_movie_frames = calloc(sv_header.movie_length, sizeof(FrameInput));
        if (sv_header.movie_guid != 0) {
            fread(savestate_movie_frames, sizeof(FrameInput), sv_header.movie_length, f);
        }
        
        uint32_t min_len = (sv_header.movie_length < emulator->movie.frame_count) ? sv_header.movie_length : emulator->movie.frame_count;
        if (memcmp(emulator->movie.frames, savestate_movie_frames, min_len * sizeof(FrameInput)) != 0) { LOG(ERROR, "Savestate timeline mismatch!"); free(savestate_movie_frames); fclose(f); return; }

        if (emulator->movie.read_only) {
            if (sv_header.movie_length > emulator->movie.frame_count) { LOG(ERROR, "Cannot load future state in read-only mode!"); free(savestate_movie_frames); fclose(f); return; }
        } else {
            emulator->movie.frame_count = sv_header.movie_length;
            memcpy(emulator->movie.frames, savestate_movie_frames, sv_header.movie_length * sizeof(FrameInput));
            emulator->movie.mode = MOVIE_MODE_RECORDING;
            if (sv_header.savestate_frame_count < emulator->movie.frame_count) {
                emulator->needs_truncation = 1;
            }
        }
        free(savestate_movie_frames);
    } else if (sv_header.movie_guid != 0) {
        emulator->movie.guid = sv_header.movie_guid;
        emulator->movie.frame_count = sv_header.movie_length;
        fread(emulator->movie.frames, sizeof(FrameInput), sv_header.movie_length, f);
        emulator->movie.mode = MOVIE_MODE_PLAYBACK;
        emulator->movie.read_only = 1;
    }

    SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(emulator->g_ctx.audio_stream));
    SDL_LockAudioStream(emulator->g_ctx.audio_stream); SDL_ClearAudioStream(emulator->g_ctx.audio_stream);
    
    fseek(f, sizeof(SaveHeader_v5), SEEK_SET);

    CPUSnapshot cpu_snap; fread(&cpu_snap, sizeof(CPUSnapshot), 1, f);
    emulator->cpu.pc = cpu_snap.pc; emulator->cpu.ac = cpu_snap.ac; emulator->cpu.x = cpu_snap.x; emulator->cpu.y = cpu_snap.y; emulator->cpu.sr = cpu_snap.sr; emulator->cpu.sp = cpu_snap.sp; emulator->cpu.t_cycles = cpu_snap.t_cycles;
    
    fread(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);
    PPUSnapshot ppu_snap; fread(&ppu_snap, sizeof(PPUSnapshot), 1, f);
    memcpy(emulator->ppu.V_RAM, ppu_snap.V_RAM, 0x1000); memcpy(emulator->ppu.OAM, ppu_snap.OAM, 256); memcpy(emulator->ppu.palette, ppu_snap.palette, 0x20);
    emulator->ppu.ctrl = ppu_snap.ctrl; emulator->ppu.mask = ppu_snap.mask; emulator->ppu.status = ppu_snap.status; emulator->ppu.oam_address = ppu_snap.oam_address;
    emulator->ppu.v = ppu_snap.v; emulator->ppu.t = ppu_snap.t; emulator->ppu.x = ppu_snap.x; emulator->ppu.w = ppu_snap.w; emulator->ppu.buffer = ppu_snap.buffer;
    fread(&emulator->apu, sizeof(APU), 1, f); emulator->apu.emulator = emulator; emulator->apu.audio_start = 0; emulator->apu.sampler.index = 0;
    MapperSnapshot map_snap; fread(&map_snap, sizeof(MapperSnapshot), 1, f);
    Mapper* m = &emulator->mapper;
    if (m->PRG_ROM) m->PRG_ptr = m->PRG_ROM + map_snap.prg_ptr_offset;
    if (m->CHR_ROM) m->CHR_ptr = m->CHR_ROM + map_snap.chr_ptr_offset;
    set_mirroring(m, map_snap.mirroring);
    if (map_snap.has_extension && m->extension) memcpy(m->extension, map_snap.extension_data, sizeof(map_snap.extension_data));
    if (m->PRG_RAM && m->RAM_size > 0) fread(m->PRG_RAM, 1, m->RAM_size, f);

    emulator->current_frame_index = sv_header.savestate_frame_count;
    if (sv_header.savestate_frame_count >= sv_header.movie_length && sv_header.movie_guid != 0) {
        emulator->movie.mode = MOVIE_MODE_FINISHED;
    }

    emulator->ppu.render = 1;
    fclose(f);
    SDL_UnlockAudioStream(emulator->g_ctx.audio_stream); SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(emulator->g_ctx.audio_stream));
    LOG(INFO, "State Loaded! Resuming at frame %d", emulator->current_frame_index);
}


void increment_save_slot(Emulator* emulator) {
    emulator->current_save_slot = (emulator->current_save_slot + 1) % 10;
}

// --- Menu Functions ---

void init_menu_layout(int screen_w, int screen_h) {
    int btn_w = screen_w * 0.5; 
    int btn_h = screen_h * 0.08; 
    if (btn_h < 40) btn_h = 40;
    
    int start_y = (screen_h - (MENU_COUNT * (btn_h + 10))) / 2;
    int center_x = (screen_w - btn_w) / 2;

    const char* labels[] = { "Resume", "Slot: 0", "Save State", "Load State", "Edit Controls", "Scanlines: Off", "Palette: Default", "Exit" };
    for(int i=0; i<MENU_COUNT; i++) {
        strncpy(menu_options[i].label, labels[i], 32);
        menu_options[i].rect.x = center_x;
        menu_options[i].rect.y = start_y + i * (btn_h + 10);
        menu_options[i].rect.w = btn_w;
        menu_options[i].rect.h = btn_h;
        menu_options[i].action_code = i;
    }
}

void render_pause_menu(GraphicsContext* g_ctx) {
#ifdef __ANDROID__
    if (is_tas_toolbar_open()) return; 
#endif

    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(g_ctx->renderer, NULL);

    SDL_Color txt = {255,255,255,255};
    SDL_Color bg = {60,60,60,255}, act = {100,60,60,255};

    for(int i=0; i<MENU_COUNT; i++) {
        if(i==4 && is_edit_mode()) 
            SDL_SetRenderDrawColor(g_ctx->renderer, act.r, act.g, act.b, 255);
        else 
            SDL_SetRenderDrawColor(g_ctx->renderer, bg.r, bg.g, bg.b, 255);
        
        SDL_FRect r = {(float)menu_options[i].rect.x, (float)menu_options[i].rect.y, (float)menu_options[i].rect.w, (float)menu_options[i].rect.h};
        SDL_RenderFillRect(g_ctx->renderer, &r);
        
        char* lbl = menu_options[i].label;
        if(i==5) lbl = video_filter_mode ? "Scanlines: ON" : "Scanlines: OFF";
        
        SDL_Surface* surf = TTF_RenderText_Solid(g_ctx->font, lbl, 0, txt);
        if(surf){
            SDL_Texture* tx = SDL_CreateTextureFromSurface(g_ctx->renderer, surf);
            SDL_FRect td = {r.x+(r.w-surf->w)/2, r.y+(r.h-surf->h)/2, (float)surf->w, (float)surf->h};
            SDL_RenderTexture(g_ctx->renderer, tx, NULL, &td);
            SDL_DestroyTexture(tx); SDL_DestroySurface(surf);
        }
    }
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_NONE);
}

void handle_menu_touch(int x, int y, Emulator* emu) {
#ifdef __ANDROID__
    if (is_tas_toolbar_open() || emu->show_script_selector) return;
#endif

    for(int i=0; i<MENU_COUNT; i++) {
        SDL_Rect r = menu_options[i].rect;
        if(x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h) {
            switch(i) {
                case 0: emu->pause = 0; break;
                case 1: increment_save_slot(emu); snprintf(menu_options[i].label, 32, "Slot: %d", emu->current_save_slot); break;
                case 2: save_state(emu, NULL); emu->pause = 0; break;
                case 3: load_state(emu, NULL); emu->pause = 0; break;
                case 4: toggle_edit_mode(); emu->pause = 0; strncpy(menu_options[i].label, is_edit_mode() ? "Stop Edit" : "Edit Controls", 32); break;
                case 5: video_filter_mode = !video_filter_mode; break;
                case 6: { int next = (emu->ppu.current_palette_index + 1) % 3; set_emulator_palette(&emu->ppu, next); snprintf(menu_options[i].label, 32, "Palette: %s", next==0?"Default":next==1?"Sony":"FCEUX"); } break;
                case 7: emu->exit = 1; break;
            }
            SDL_Delay(200);
        }
    }
}

void init_emulator(struct Emulator* emulator, int argc, char *argv[]){
    if(argc < 2) quit(EXIT_FAILURE);
    memset(emulator, 0, sizeof(Emulator));
    char* filename = get_file_name(argv[1]);
    strncpy(emulator->rom_name, filename, 255);
    emulator->current_save_slot = 0;
    load_file(argv[1], (argc==3||argc==6)?argv[argc-1]:NULL, &emulator->mapper);
    emulator->type = emulator->mapper.type;
    emulator->mapper.emulator = emulator;
    PERIOD = 1000000000 / (emulator->type==PAL?PAL_FRAME_RATE:NTSC_FRAME_RATE);
    TURBO_SKIP = (emulator->type==PAL?PAL_FRAME_RATE:NTSC_FRAME_RATE) / (emulator->type==PAL?PAL_TURBO_RATE:NTSC_TURBO_RATE);
#ifdef __ANDROID__
    if(argc < 4) return;
    emulator->g_ctx.screen_width = strtol(argv[2], NULL, 10);
    emulator->g_ctx.screen_height = strtol(argv[3], NULL, 10);
    emulator->g_ctx.is_tv = (argc>4)?strtol(argv[4], NULL, 10):0;
#else
    emulator->g_ctx.screen_width = -1; emulator->g_ctx.screen_height = -1; emulator->g_ctx.is_tv = 0;
#endif
    emulator->g_ctx.width = 256; emulator->g_ctx.height = 240; emulator->g_ctx.scale = 2;
    get_graphics_context(&emulator->g_ctx);
    SDL_SetWindowTitle(emulator->g_ctx.window, get_file_name(argv[1]));
    init_mem(emulator); init_ppu(emulator); init_cpu(emulator); init_APU(emulator);
    init_timer(&emulator->timer, PERIOD);
    ANDROID_INIT_TOUCH_PAD(emulator);
    tas_init(emulator);
    create_default_script(emulator);
    if(!emulator->g_ctx.is_tv) init_menu_layout(emulator->g_ctx.screen_width, emulator->g_ctx.screen_height);
}

void run_emulator(struct Emulator* emulator){
    if(emulator->mapper.is_nsf) { run_NSF_player(emulator); return; }
    JoyPad *j1=&emulator->mem.joy1, *j2=&emulator->mem.joy2;
    PPU *ppu=&emulator->ppu; c6502 *cpu=&emulator->cpu; APU *apu=&emulator->apu;
    GraphicsContext *g=&emulator->g_ctx; Timer *tm=&emulator->timer;
    SDL_Event e; Timer ft;
    uint64_t base_period_ns = PERIOD; 
    init_timer(&ft, base_period_ns); 
    mark_start(&ft);
    uint64_t frames=0, last_fps=SDL_GetTicks();
    float fps=0.0f;

    while (!emulator->exit) {
        mark_start(tm);
        while (SDL_PollEvent(&e)) {
            if(emulator->show_script_selector && e.type == SDL_EVENT_FINGER_DOWN) {
                handle_script_selector_input(emulator, (int)(e.tfinger.x * g->screen_width), (int)(e.tfinger.y * g->screen_height));
                continue; 
            }
            if(emulator->pause && !emulator->show_script_selector && e.type == SDL_EVENT_FINGER_DOWN) {
                handle_menu_touch((int)(e.tfinger.x * g->screen_width), (int)(e.tfinger.y * g->screen_height), emulator);
            }
            if (emulator->movie.mode != MOVIE_MODE_PLAYBACK) {
                update_joypad(j1, &e); 
                update_joypad(j2, &e);
            }
            if(e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) emulator->exit = 1;
            if(e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_AC_BACK || e.key.scancode == SDL_SCANCODE_AC_BACK)) {
                if(emulator->show_script_selector) emulator->show_script_selector = 0;
                else emulator->pause = !emulator->pause;
            }
            if(e.type == SDL_EVENT_KEY_DOWN) {
                switch(e.key.key) {
                    case SDLK_F1: tas_start_recording(emulator); break;
                    case SDLK_F2: tas_start_playback(emulator, 0); break;
                    case SDLK_F3: tas_start_playback(emulator, 1); break;
                    case SDLK_F4: tas_stop_movie(emulator); break;
                    case SDLK_F7: tas_toggle_slow_motion(emulator); break;
                    case SDLK_F8: tas_open_script_selector(emulator); break;
                    case SDLK_P:  tas_step_frame(emulator); break;
                }
            }
        }
        if(ppu->frames % TURBO_SKIP == 0 && emulator->movie.mode != MOVIE_MODE_PLAYBACK) { 
            turbo_trigger(j1); turbo_trigger(j2); 
        }

        if((!emulator->pause || emulator->step_frame) && !emulator->show_script_selector){
            
            if (emulator->needs_truncation) {
                LOG(INFO, "TAS: Movie truncated to frame %d", emulator->current_frame_index);
                emulator->movie.frame_count = emulator->current_frame_index;
                emulator->needs_truncation = 0;
            }

            if (emulator->movie.mode == MOVIE_MODE_RECORDING) {
                if (emulator->current_frame_index < MAX_MOVIE_FRAMES) {
                    emulator->movie.frames[emulator->current_frame_index].joy1_status = j1->status;
                    emulator->movie.frames[emulator->current_frame_index].joy2_status = j2->status;
                    emulator->movie.frame_count = emulator->current_frame_index + 1;
                } else { tas_stop_movie(emulator); }
            } else if (emulator->movie.mode == MOVIE_MODE_PLAYBACK) {
                if (emulator->current_frame_index < emulator->movie.frame_count) {
                    j1->status = emulator->movie.frames[emulator->current_frame_index].joy1_status;
                    j2->status = emulator->movie.frames[emulator->current_frame_index].joy2_status;
                } else {
                    emulator->movie.mode = MOVIE_MODE_FINISHED;
                    LOG(INFO, "TAS: Movie playback finished.");
                }
            }
            
            if(emulator->lua_script_active) lua_bridge_update(emulator);

            while(!ppu->render) {
                execute_ppu(ppu); execute_ppu(ppu); execute_ppu(ppu);
                if(emulator->type == PAL) { static int c=0; if(++c==5){ execute_ppu(ppu); c=0; } }
                execute(cpu); execute_apu(apu);
            }

            if (emulator->movie.mode != MOVIE_MODE_FINISHED) {
                emulator->current_frame_index++;
            }
            
            frames++;
            if(SDL_GetTicks() > last_fps + 1000) { fps = frames / ((SDL_GetTicks()-last_fps)/1000.0f); last_fps=SDL_GetTicks(); frames=0; }
            
            render_graphics_update(g, ppu->screen, ppu->mask);
            if(emulator->lua_script_active) {
                lua_bridge_render(emulator, g);
            }
            render_ui_and_present(g, fps, emulator);
            
            ppu->render = 0;
            queue_audio(apu, g);
            
            if (emulator->step_frame) {
                emulator->step_frame = 0;
                emulator->pause = 1;
            }

            mark_end(tm); 
            adjusted_wait(tm);
            if (emulator->slow_motion_factor > 1.0f) {
                double frame_ms = 1000.0 / (emulator->type==PAL?PAL_FRAME_RATE:NTSC_FRAME_RATE);
                double extra_wait = frame_ms * (emulator->slow_motion_factor - 1.0);
                SDL_Delay((uint32_t)extra_wait);
            }

        } else {
            render_frame_only(g);
            if(emulator->show_script_selector) {
                render_script_selector_ui(emulator);
            } else {
                if(emulator->lua_script_active) lua_bridge_render(emulator, g);
                if (!is_tas_toolbar_open()) render_pause_menu(g);
            }
            #ifdef __ANDROID__
            ANDROID_RENDER_TOUCH_CONTROLS(g);
            #endif
            SDL_RenderPresent(g->renderer);
            SDL_Delay(30);
        }
    }
    release_timer(&ft); release_timer(tm);
}

void reset_emulator(Emulator* emulator) {
    if(emulator->g_ctx.is_tv) { emulator->exit=1; return; }
    reset_cpu(&emulator->cpu); 
    reset_APU(&emulator->apu); 
    reset_ppu(&emulator->ppu);
    if(emulator->mapper.reset) emulator->mapper.reset(&emulator->mapper);
}

void free_emulator(struct Emulator* emulator){
    exit_APU(); exit_ppu(&emulator->ppu); free_mapper(&emulator->mapper);
    ANDROID_FREE_TOUCH_PAD(); free_graphics(&emulator->g_ctx); release_timer(&emulator->timer);
    if(emulator->movie.frames) free(emulator->movie.frames);
    lua_bridge_free(emulator);
}

void run_NSF_player(struct Emulator* emulator) {
    LOG(INFO, "Starting NSF player...");
    JoyPad* joy1 = &emulator->mem.joy1; JoyPad* joy2 = &emulator->mem.joy2;
    c6502* cpu = &emulator->cpu; APU* apu = &emulator->apu; NSF* nsf = emulator->mapper.NSF;
    GraphicsContext* g_ctx = &emulator->g_ctx; init_NSF_gfx(g_ctx, nsf);
    Timer* timer = &emulator->timer; SDL_Event e; Timer frame_timer;
    PERIOD = 1000 * emulator->mapper.NSF->speed; double ms_per_frame = emulator->mapper.NSF->speed / 1000.0;
    init_timer(&frame_timer, PERIOD); mark_start(&frame_timer);
    size_t cycles_per_frame, nmi_cycle_start;
    if(emulator->type == PAL) { cycles_per_frame = emulator->mapper.NSF->speed * 1.662607f; nmi_cycle_start = cycles_per_frame - 7459; }
    else { cycles_per_frame = emulator->mapper.NSF->speed * 1.789773f; nmi_cycle_start = cycles_per_frame - 2273; }
    uint8_t status1 = 0, status2 = 0; init_song(emulator, nsf->current_song);
    while (!emulator->exit) {
        mark_start(timer);
        while (SDL_PollEvent(&e)) {
            update_joypad(joy1, &e); update_joypad(joy2, &e);
            if((status1 & RIGHT && !(joy1->status & RIGHT)) || (status2 & RIGHT && !(joy2->status & RIGHT))) next_song(emulator, nsf);
            else if((status1 & LEFT && !(joy1->status & LEFT)) || (status2 & LEFT && !(joy2->status & LEFT))) prev_song(emulator, nsf);
            else if((status1 & START && !(joy1->status & START)) || (status2 & START && !(joy2->status & START))) emulator->pause ^= 1;
            status1 = joy1->status; status2 = joy2->status;
            if((joy1->status & 0xc) == 0xc || (joy2->status & 0xc) == 0xc) { reset_emulator(emulator); nsf->current_song = 1; init_song(emulator, nsf->current_song); }
            if(e.type == SDL_EVENT_QUIT) emulator->exit = 1; if(e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_AC_BACK) emulator->exit = 1;
        }
        if(nsf->times != NULL && !nsf->initializing) {
            double track_dur = nsf->times[nsf->current_song == 0 ? 0 : nsf->current_song - 1];
            if(track_dur < nsf->tick) {
                if(nsf->tick_max < nsf->tick) next_song(emulator, nsf);
                else if(nsf->fade != NULL) { int fade_dur = nsf->fade[nsf->current_song == 0 ? 0 : nsf->current_song - 1]; apu->volume = (nsf->tick - track_dur) / (float)fade_dur; apu->volume = 1 - (apu->volume < 0 ? 0 : apu->volume > 1 ? 1 : apu->volume); }
            }
        }
        if(!emulator->pause){
            if ((nsf->flags & (NSF_NO_PLAY_SR | NSF_NON_RETURN_INIT)) == 0) run_cpu_subroutine(cpu, nsf->play_addr);
            size_t cycles = 0;
            while (cycles < cycles_per_frame) {
                execute(cpu);
                if (!(cpu->mode & CPU_SR_ANY)) { if (nsf->flags & NSF_NON_RETURN_INIT && nsf->init_num == 0) { if (run_cpu_subroutine(cpu, nsf->init_addr) == 0) { cpu->ac = nsf->current_song > 0? nsf->current_song - 1: 0; cpu->x = emulator->type == PAL? 1: 0; cpu->y = 0x81; nsf->init_num = 1; } } }
                if (nsf->flags & NSF_IRQ) nsf_execute(emulator);
                if(!nsf->initializing) execute_apu(apu);
                if (cycles == nmi_cycle_start) { if ((nsf->flags & (NSF_NON_RETURN_INIT|NSF_NO_PLAY_SR)) == NSF_NON_RETURN_INIT) interrupt(cpu, NMI); }
                cycles++;
            }
            if ((nsf->flags & (NSF_NON_RETURN_INIT|NSF_NO_PLAY_SR)) == NSF_NON_RETURN_INIT) interrupt_clear(cpu, NMI);
            render_NSF_graphics(emulator, nsf);
            if(!nsf->initializing) { queue_audio(apu, g_ctx); nsf->tick += ms_per_frame; }
            if((cpu->sub_address == nsf->play_addr || nsf->init_num == 1) && nsf->initializing) { nsf->initializing = 0; nsf->tick = 0; }
            mark_end(timer); adjusted_wait(timer);
        }else{ wait(IDLE_SLEEP); }
    }
    mark_end(&frame_timer); emulator->time_diff = get_diff_ms(&frame_timer); release_timer(&frame_timer);
}

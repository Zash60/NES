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

#include <SDL.h> 
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

static uint64_t PERIOD;
static uint16_t TURBO_SKIP;

// --- Configurações ---
#define SAVE_LOAD_COOLDOWN 1000
#define SAVE_MAGIC 0x4E45535C // Versão C (Atualizada)
#define SAVE_VERSION 3

static uint32_t last_state_action_time = 0;

// --- Estruturas do Menu ---
typedef struct {
    char label[32];
    SDL_Rect rect;
    int action_code;
} MenuOption;

#define MENU_COUNT 8
MenuOption menu_options[MENU_COUNT];

// Forward declarations
void toggle_edit_mode();
uint8_t is_edit_mode();

// --- Estruturas de Snapshot (Serialização) ---

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_crc; // (Opcional para futuro)
    uint32_t padding;
} SaveHeader;

typedef struct {
    uint16_t pc;
    uint8_t ac, x, y, sr, sp;
    size_t t_cycles; // Importante para sincronia
} CPUSnapshot;

// PPU: Salva registradores internos e memórias
typedef struct {
    uint8_t V_RAM[0x1000];
    uint8_t OAM[256];
    uint8_t palette[0x20];
    uint8_t ctrl, mask, status;
    uint8_t oam_address;
    uint16_t v, t;
    uint8_t x, w;
    uint8_t buffer; // Buffer de leitura do PPU
} PPUSnapshot;

// Mapper: Salva offsets em vez de ponteiros crus
typedef struct {
    uint64_t prg_ptr_offset;
    uint64_t chr_ptr_offset;
    Mirroring mirroring;
    int has_extension;
    uint8_t extension_data[2048]; // Buffer genérico para registradores de mappers complexos (MMC1/3/5)
    size_t ram_size;
    // PRG RAM é salva separadamente se existir
} MapperSnapshot;

// --- Funções de Arquivo ---

static void get_slot_filename(Emulator* emu, char* buffer, size_t size) {
    char* base_path = SDL_GetPrefPath("Barracoder", "AndroNES");
    if (base_path) {
        snprintf(buffer, size, "%s%s_slot%d.save", base_path, emu->rom_name, emu->current_save_slot);
        SDL_free(base_path);
    } else {
        snprintf(buffer, size, "%s_slot%d.save", emu->rom_name, emu->current_save_slot);
    }
}

// --- Save State ---

void save_state(Emulator* emulator, const char* unused) {
    uint32_t now = SDL_GetTicks();
    if (now < last_state_action_time + SAVE_LOAD_COOLDOWN) return;
    last_state_action_time = now;

    char full_path[1024];
    get_slot_filename(emulator, full_path, sizeof(full_path));
    LOG(INFO, "Saving state to: %s", full_path);

    FILE* f = fopen(full_path, "wb");
    if (!f) { LOG(ERROR, "Save Failed: Could not open file for writing"); return; }

    // 1. Header
    SaveHeader header = { .magic = SAVE_MAGIC, .version = SAVE_VERSION };
    fwrite(&header, sizeof(SaveHeader), 1, f);

    // 2. CPU
    CPUSnapshot cpu_snap = {
        .pc = emulator->cpu.pc, .ac = emulator->cpu.ac, .x = emulator->cpu.x,
        .y = emulator->cpu.y, .sr = emulator->cpu.sr, .sp = emulator->cpu.sp,
        .t_cycles = emulator->cpu.t_cycles
    };
    fwrite(&cpu_snap, sizeof(CPUSnapshot), 1, f);

    // 3. Main RAM
    fwrite(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);

    // 4. PPU
    PPUSnapshot ppu_snap;
    memcpy(ppu_snap.V_RAM, emulator->ppu.V_RAM, 0x1000);
    memcpy(ppu_snap.OAM, emulator->ppu.OAM, 256);
    memcpy(ppu_snap.palette, emulator->ppu.palette, 0x20);
    ppu_snap.ctrl = emulator->ppu.ctrl;
    ppu_snap.mask = emulator->ppu.mask;
    ppu_snap.status = emulator->ppu.status;
    ppu_snap.oam_address = emulator->ppu.oam_address;
    ppu_snap.v = emulator->ppu.v;
    ppu_snap.t = emulator->ppu.t;
    ppu_snap.x = emulator->ppu.x;
    ppu_snap.w = emulator->ppu.w;
    ppu_snap.buffer = emulator->ppu.buffer;
    fwrite(&ppu_snap, sizeof(PPUSnapshot), 1, f);

    // 5. APU
    // Copia bruta da estrutura APU. Cuidado: Ponteiros internos devem ser restaurados no load.
    fwrite(&emulator->apu, sizeof(APU), 1, f);

    // 6. Mapper e PRG RAM (Save RAM)
    Mapper* m = &emulator->mapper;
    MapperSnapshot map_snap = {0};
    
    // Calcula offsets dos ponteiros atuais
    map_snap.prg_ptr_offset = (m->PRG_ptr && m->PRG_ROM) ? (m->PRG_ptr - m->PRG_ROM) : 0;
    map_snap.chr_ptr_offset = (m->CHR_ptr && m->CHR_ROM) ? (m->CHR_ptr - m->CHR_ROM) : 0;
    map_snap.mirroring = m->mirroring;
    map_snap.ram_size = m->RAM_size;

    if (m->extension) {
        map_snap.has_extension = 1;
        // Salva até 2KB de dados de extensão (registradores do mapper)
        memcpy(map_snap.extension_data, m->extension, sizeof(map_snap.extension_data));
    }

    fwrite(&map_snap, sizeof(MapperSnapshot), 1, f);

    // Se o cartucho tiver RAM (Battery Save), salva o conteúdo dela
    if (m->PRG_RAM && m->RAM_size > 0) {
        fwrite(m->PRG_RAM, 1, m->RAM_size, f);
    }

    fclose(f);
    LOG(INFO, "State Saved Successfully!");
}

// --- Load State ---

void load_state(Emulator* emulator, const char* unused) {
    uint32_t now = SDL_GetTicks();
    if (now < last_state_action_time + SAVE_LOAD_COOLDOWN) return;
    last_state_action_time = now;

    // CONGELAMENTO DO SISTEMA
    SDL_LockAudioStream(emulator->g_ctx.audio_stream);
    SDL_ClearAudioStream(emulator->g_ctx.audio_stream);
    
    // Limpeza visual
    memset(emulator->ppu.screen, 0, 256 * 240 * sizeof(uint32_t));
    render_frame_only(&emulator->g_ctx);

    char full_path[1024];
    get_slot_filename(emulator, full_path, sizeof(full_path));
    LOG(INFO, "Loading from: %s", full_path);

    FILE* f = fopen(full_path, "rb");
    if (!f) { 
        LOG(ERROR, "Load Failed: File not found"); 
        SDL_UnlockAudioStream(emulator->g_ctx.audio_stream);
        return; 
    }

    // 1. Header Check
    SaveHeader header;
    if (fread(&header, sizeof(SaveHeader), 1, f) != 1 || header.magic != SAVE_MAGIC) {
        LOG(ERROR, "Load Failed: Invalid Save File Version");
        fclose(f);
        SDL_UnlockAudioStream(emulator->g_ctx.audio_stream);
        return;
    }

    // 2. CPU
    CPUSnapshot cpu_snap;
    if (fread(&cpu_snap, sizeof(CPUSnapshot), 1, f) == 1) {
        emulator->cpu.pc = cpu_snap.pc;
        emulator->cpu.ac = cpu_snap.ac;
        emulator->cpu.x = cpu_snap.x;
        emulator->cpu.y = cpu_snap.y;
        emulator->cpu.sr = cpu_snap.sr;
        emulator->cpu.sp = cpu_snap.sp;
        emulator->cpu.t_cycles = cpu_snap.t_cycles;
    }

    // 3. RAM
    fread(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);

    // 4. PPU
    PPUSnapshot ppu_snap;
    if (fread(&ppu_snap, sizeof(PPUSnapshot), 1, f) == 1) {
        memcpy(emulator->ppu.V_RAM, ppu_snap.V_RAM, 0x1000);
        memcpy(emulator->ppu.OAM, ppu_snap.OAM, 256);
        memcpy(emulator->ppu.palette, ppu_snap.palette, 0x20);
        emulator->ppu.ctrl = ppu_snap.ctrl;
        emulator->ppu.mask = ppu_snap.mask;
        emulator->ppu.status = ppu_snap.status;
        emulator->ppu.oam_address = ppu_snap.oam_address;
        emulator->ppu.v = ppu_snap.v;
        emulator->ppu.t = ppu_snap.t;
        emulator->ppu.x = ppu_snap.x;
        emulator->ppu.w = ppu_snap.w;
        emulator->ppu.buffer = ppu_snap.buffer;
    }

    // 5. APU
    // Lê a struct bruta sobre a atual
    fread(&emulator->apu, sizeof(APU), 1, f);
    // CRÍTICO: Restaurar o ponteiro para o emulador, pois o valor do arquivo é inválido nesta sessão
    emulator->apu.emulator = emulator;

    // 6. Mapper Restoration
    MapperSnapshot map_snap;
    if (fread(&map_snap, sizeof(MapperSnapshot), 1, f) == 1) {
        Mapper* m = &emulator->mapper;
        
        // Restaura ponteiros de banco usando os offsets
        if (m->PRG_ROM) m->PRG_ptr = m->PRG_ROM + map_snap.prg_ptr_offset;
        if (m->CHR_ROM) m->CHR_ptr = m->CHR_ROM + map_snap.chr_ptr_offset;
        
        set_mirroring(m, map_snap.mirroring);

        // Restaura dados internos do mapper (MMC1/MMC3 registers)
        if (map_snap.has_extension && m->extension) {
            memcpy(m->extension, map_snap.extension_data, sizeof(map_snap.extension_data));
            // NOTA: Em implementações avançadas, alguns mappers (como MMC3) precisam
            // que uma função "refresh" seja chamada para atualizar os ponteiros internos
            // baseados nos registradores restaurados.
            // Para esta implementação, assumimos que a cópia bruta de 'extension' cobre a maioria dos casos.
        }

        // 7. PRG RAM
        if (m->PRG_RAM && m->RAM_size > 0 && map_snap.ram_size == m->RAM_size) {
            fread(m->PRG_RAM, 1, m->RAM_size, f);
        }
    }

    // Força renderização na próxima iteração
    emulator->ppu.render = 1;

    fclose(f);
    SDL_UnlockAudioStream(emulator->g_ctx.audio_stream);
    LOG(INFO, "State Loaded Successfully!");
}

void increment_save_slot(Emulator* emulator) {
    emulator->current_save_slot++;
    if(emulator->current_save_slot > 9) emulator->current_save_slot = 0;
}

// --- Menu Logic ---

void init_menu_layout(int screen_w, int screen_h) {
    int btn_w = screen_w * 0.5; 
    int btn_h = screen_h * 0.08; 
    if (btn_h < 40) btn_h = 40;
    
    int start_y = (screen_h - (MENU_COUNT * (btn_h + 10))) / 2;
    int center_x = (screen_w - btn_w) / 2;

    const char* labels[] = {
        "Resume", "Slot: 0", "Save State", "Load State", 
        "Edit Controls", "Scanlines: Off", "Palette: Default", "Exit"
    };

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
    SDL_SetRenderDrawBlendMode(g_ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ctx->renderer, 0, 0, 0, 220); // Fundo semi-transparente
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
        
        SDL_Surface* surf = TTF_RenderText_Blended(g_ctx->font, lbl, 0, txt);
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
    for(int i=0; i<MENU_COUNT; i++) {
        SDL_Rect r = menu_options[i].rect;
        if(x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h) {
            switch(i) {
                case 0: // Resume
                    emu->pause = 0; 
                    break;
                case 1: // Slot
                    increment_save_slot(emu);
                    snprintf(menu_options[i].label, 32, "Slot: %d", emu->current_save_slot);
                    break;
                case 2: // Save
                    save_state(emu, NULL); 
                    emu->pause = 0; 
                    break;
                case 3: // Load
                    load_state(emu, NULL); 
                    emu->pause = 0; 
                    break;
                case 4: // Edit
                    toggle_edit_mode(); 
                    emu->pause = 0; 
                    strncpy(menu_options[i].label, is_edit_mode() ? "Stop Edit" : "Edit Controls", 32); 
                    break;
                case 5: // Filter
                    video_filter_mode = !video_filter_mode; 
                    break;
                case 6: // Palette
                    {
                        int next = (emu->ppu.current_palette_index + 1) % 3;
                        set_emulator_palette(&emu->ppu, next);
                        snprintf(menu_options[i].label, 32, "Palette: %s", next==0?"Default":next==1?"Sony":"FCEUX");
                    } 
                    break;
                case 7: // Exit
                    emu->exit = 1; 
                    break;
            }
            SDL_Delay(200); // UI Debounce
        }
    }
}

// --- Inicialização e Loop ---

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
    if(!emulator->g_ctx.is_tv) init_menu_layout(emulator->g_ctx.screen_width, emulator->g_ctx.screen_height);
}

void run_emulator(struct Emulator* emulator){
    if(emulator->mapper.is_nsf) { run_NSF_player(emulator); return; }
    
    JoyPad *j1=&emulator->mem.joy1, *j2=&emulator->mem.joy2;
    PPU *ppu=&emulator->ppu; c6502 *cpu=&emulator->cpu; APU *apu=&emulator->apu;
    GraphicsContext *g=&emulator->g_ctx; Timer *tm=&emulator->timer;
    SDL_Event e; Timer ft;
    init_timer(&ft, PERIOD); mark_start(&ft);
    
    uint64_t frames=0, last_fps=SDL_GetTicks();
    float fps=0.0f;

    while (!emulator->exit) {
        mark_start(tm);
        while (SDL_PollEvent(&e)) {
            // Input do Menu
            if(emulator->pause && e.type == SDL_EVENT_FINGER_DOWN) {
                handle_menu_touch((int)(e.tfinger.x * g->screen_width), (int)(e.tfinger.y * g->screen_height), emulator);
            }
            
            update_joypad(j1, &e); update_joypad(j2, &e);
            
            if(e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) emulator->exit = 1;
            if(e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_AC_BACK || e.key.scancode == SDL_SCANCODE_AC_BACK)) {
                // Botão voltar alterna pause
                emulator->pause = !emulator->pause;
            }
        }
        
        if(ppu->frames % TURBO_SKIP == 0) { turbo_trigger(j1); turbo_trigger(j2); }

        if(!emulator->pause){
            // --- EMULAÇÃO ATIVA ---
            while(!ppu->render) {
                execute_ppu(ppu); execute_ppu(ppu); execute_ppu(ppu);
                if(emulator->type == PAL) {
                    static int c=0; if(++c==5){ execute_ppu(ppu); c=0; }
                }
                execute(cpu); execute_apu(apu);
            }
            
            frames++;
            if(SDL_GetTicks() > last_fps + 1000) { 
                fps = frames / ((SDL_GetTicks()-last_fps)/1000.0f); 
                last_fps=SDL_GetTicks(); frames=0; 
            }
            
            // Renderiza com FPS e Máscara
            render_graphics(g, ppu->screen, fps, ppu->mask);
            
            ppu->render = 0;
            queue_audio(apu, g);
            mark_end(tm); adjusted_wait(tm);

        } else {
            // --- PAUSA / MENU ---
            // Redesenha o último frame estático (evita flickering) e o menu
            render_frame_only(g);
            render_pause_menu(g);
            SDL_RenderPresent(g->renderer);
            SDL_Delay(30); // Economia de bateria no menu
        }
    }
    release_timer(&ft); release_timer(tm);
}

void reset_emulator(Emulator* emulator) {
    if(emulator->g_ctx.is_tv) { emulator->exit=1; return; }
    reset_cpu(&emulator->cpu); reset_APU(&emulator->apu); reset_ppu(&emulator->ppu);
    if(emulator->mapper.reset) emulator->mapper.reset(&emulator->mapper);
}

// --- NSF Player (Lógica Completa) ---

void run_NSF_player(struct Emulator* emulator) {
    LOG(INFO, "Starting NSF player...");
    JoyPad* joy1 = &emulator->mem.joy1;
    JoyPad* joy2 = &emulator->mem.joy2;
    c6502* cpu = &emulator->cpu;
    APU* apu = &emulator->apu;
    NSF* nsf = emulator->mapper.NSF;
    GraphicsContext* g_ctx = &emulator->g_ctx;
    
    init_NSF_gfx(g_ctx, nsf);
    Timer* timer = &emulator->timer;
    SDL_Event e;
    Timer frame_timer;
    
    // Configuração de tempo NSF
    PERIOD = 1000 * emulator->mapper.NSF->speed;
    double ms_per_frame = emulator->mapper.NSF->speed / 1000.0;
    
    init_timer(&frame_timer, PERIOD);
    mark_start(&frame_timer);
    
    size_t cycles_per_frame, nmi_cycle_start;
    if(emulator->type == PAL) {
        cycles_per_frame = emulator->mapper.NSF->speed * 1.662607f;
        nmi_cycle_start = cycles_per_frame - 7459;
    }else {
        cycles_per_frame = emulator->mapper.NSF->speed * 1.789773f;
        nmi_cycle_start = cycles_per_frame - 2273;
    }
    
    uint8_t status1 = 0, status2 = 0;
    init_song(emulator, nsf->current_song);

    while (!emulator->exit) {
        mark_start(timer);
        while (SDL_PollEvent(&e)) {
            update_joypad(joy1, &e);
            update_joypad(joy2, &e);
            
            // Controles
            if((status1 & RIGHT && !(joy1->status & RIGHT)) || (status2 & RIGHT && !(joy2->status & RIGHT))) {
                next_song(emulator, nsf);
            } else if((status1 & LEFT && !(joy1->status & LEFT)) || (status2 & LEFT && !(joy2->status & LEFT))) {
                prev_song(emulator, nsf);
            } else if((status1 & START && !(joy1->status & START)) || (status2 & START && !(joy2->status & START))) {
                emulator->pause ^= 1;
            }
            
            status1 = joy1->status;
            status2 = joy2->status;
            
            if((joy1->status & 0xc) == 0xc || (joy2->status & 0xc) == 0xc) {
                reset_emulator(emulator);
                nsf->current_song = 1;
                init_song(emulator, nsf->current_song);
            }

            switch (e.type) {
                case SDL_EVENT_KEY_DOWN:
                    switch (e.key.key) {
                        case SDLK_ESCAPE: emulator->exit = 1; break;
                        case SDLK_MEDIA_PLAY: case SDLK_SPACE: emulator->pause ^= 1; TOGGLE_TIMER_RESOLUTION(); break;
                        case SDLK_MEDIA_NEXT_TRACK: next_song(emulator, nsf); break;
                        case SDLK_MEDIA_PREVIOUS_TRACK: prev_song(emulator, nsf); break;
                        case SDLK_F5: reset_emulator(emulator); nsf->current_song = 1; init_song(emulator, nsf->current_song); break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_QUIT: emulator->exit = 1; break;
                default:
                    if(e.key.key == SDLK_AC_BACK || e.key.scancode == SDL_SCANCODE_AC_BACK) {
                        emulator->exit = 1;
                    }
            }
        }

        // Gerenciamento de Tempo/Fade da Música
        if(nsf->times != NULL && !nsf->initializing) {
            double track_dur = nsf->times[nsf->current_song == 0 ? 0 : nsf->current_song - 1];
            if(track_dur < nsf->tick) {
                if(nsf->tick_max < nsf->tick) {
                    next_song(emulator, nsf);
                } else if(nsf->fade != NULL) {
                    int fade_dur = nsf->fade[nsf->current_song == 0 ? 0 : nsf->current_song - 1];
                    apu->volume = (nsf->tick - track_dur) / (float)fade_dur;
                    apu->volume = 1 - (apu->volume < 0 ? 0 : apu->volume > 1 ? 1 : apu->volume);
                }
            }
        }

        if(!emulator->pause){
            // Lógica de execução NSF: Chama o PLAY routine
            if ((nsf->flags & (NSF_NO_PLAY_SR | NSF_NON_RETURN_INIT)) == 0) {
                run_cpu_subroutine(cpu, nsf->play_addr);
            }

            size_t cycles = 0;
            while (cycles < cycles_per_frame) {
                execute(cpu);
                // Verifica se houve re-init
                if (!(cpu->mode & CPU_SR_ANY)) {
                    if (nsf->flags & NSF_NON_RETURN_INIT && nsf->init_num == 0) {
                        if (run_cpu_subroutine(cpu, nsf->init_addr) == 0) {
                            cpu->ac = nsf->current_song > 0? nsf->current_song - 1: 0;
                            cpu->x = emulator->type == PAL? 1: 0;
                            cpu->y = 0x81;
                            nsf->init_num = 1;
                        }
                    }
                }
                if (nsf->flags & NSF_IRQ) nsf_execute(emulator);
                
                if(!nsf->initializing) execute_apu(apu);
                
                // Simula NMI se necessário
                if (cycles == nmi_cycle_start) {
                    if ((nsf->flags & (NSF_NON_RETURN_INIT|NSF_NO_PLAY_SR)) == NSF_NON_RETURN_INIT) {
                        interrupt(cpu, NMI);
                    }
                }
                cycles++;
            }

            if ((nsf->flags & (NSF_NON_RETURN_INIT|NSF_NO_PLAY_SR)) == NSF_NON_RETURN_INIT) {
                interrupt_clear(cpu, NMI);
            }

            render_NSF_graphics(emulator, nsf);
            if(!nsf->initializing) {
                queue_audio(apu, g_ctx);
                nsf->tick += ms_per_frame;
            }
            if((cpu->sub_address == nsf->play_addr || nsf->init_num == 1) && nsf->initializing) {
                nsf->initializing = 0;
                nsf->tick = 0;
            }
            mark_end(timer);
            adjusted_wait(timer);
        }else{
            wait(IDLE_SLEEP);
        }
    }

    mark_end(&frame_timer);
    emulator->time_diff = get_diff_ms(&frame_timer);
    release_timer(&frame_timer);
}

void free_emulator(struct Emulator* emulator){
    LOG(DEBUG, "Starting emulator clean up");
    exit_APU();
    exit_ppu(&emulator->ppu);
    free_mapper(&emulator->mapper);
    ANDROID_FREE_TOUCH_PAD();
    free_graphics(&emulator->g_ctx);
    release_timer(&emulator->timer);
    LOG(DEBUG, "Emulator session successfully terminated");
}

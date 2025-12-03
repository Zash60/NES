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

static uint64_t PERIOD;
static uint16_t TURBO_SKIP;

// --- Estruturas para Serialização do Save State ---
typedef struct {
    uint16_t pc;
    uint8_t ac, x, y, sr, sp;
} CPUSnapshot;

typedef struct {
    uint8_t V_RAM[0x1000];
    uint8_t OAM[256];
    uint8_t palette[0x20];
    uint8_t ctrl, mask, status;
    uint8_t oam_address;
    uint16_t v, t;
    uint8_t x, w;
} PPUSnapshot;

void save_state(Emulator* emulator, const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        LOG(ERROR, "Failed to save state to %s", filename);
        return;
    }

    // 1. Salvar CPU
    CPUSnapshot cpu_snap = {
        .pc = emulator->cpu.pc, .ac = emulator->cpu.ac, .x = emulator->cpu.x,
        .y = emulator->cpu.y, .sr = emulator->cpu.sr, .sp = emulator->cpu.sp
    };
    fwrite(&cpu_snap, sizeof(CPUSnapshot), 1, f);

    // 2. Salvar RAM Principal
    fwrite(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);

    // 3. Salvar PPU
    PPUSnapshot ppu_snap;
    memcpy(ppu_snap.V_RAM, emulator->ppu.V_RAM, sizeof(ppu_snap.V_RAM));
    memcpy(ppu_snap.OAM, emulator->ppu.OAM, sizeof(ppu_snap.OAM));
    memcpy(ppu_snap.palette, emulator->ppu.palette, sizeof(ppu_snap.palette));
    ppu_snap.ctrl = emulator->ppu.ctrl;
    ppu_snap.mask = emulator->ppu.mask;
    ppu_snap.status = emulator->ppu.status;
    ppu_snap.oam_address = emulator->ppu.oam_address;
    ppu_snap.v = emulator->ppu.v;
    ppu_snap.t = emulator->ppu.t;
    ppu_snap.x = emulator->ppu.x;
    ppu_snap.w = emulator->ppu.w;
    fwrite(&ppu_snap, sizeof(PPUSnapshot), 1, f);
    
    // Nota: Mappers complexos (MMC1, MMC3) precisariam de logica extra para salvar
    // o estado de seus registradores internos e ponteiros de banco.
    // Esta implementação cobre jogos mais simples (NROM, CNROM, etc).

    fclose(f);
    LOG(INFO, "State saved to %s", filename);
}

void load_state(Emulator* emulator, const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        LOG(ERROR, "Failed to load state from %s", filename);
        return;
    }

    // 1. Carregar CPU
    CPUSnapshot cpu_snap;
    if (fread(&cpu_snap, sizeof(CPUSnapshot), 1, f) != 1) { fclose(f); return; }
    emulator->cpu.pc = cpu_snap.pc;
    emulator->cpu.ac = cpu_snap.ac;
    emulator->cpu.x = cpu_snap.x;
    emulator->cpu.y = cpu_snap.y;
    emulator->cpu.sr = cpu_snap.sr;
    emulator->cpu.sp = cpu_snap.sp;

    // 2. Carregar RAM
    fread(emulator->mem.RAM, sizeof(uint8_t), RAM_SIZE, f);

    // 3. Carregar PPU
    PPUSnapshot ppu_snap;
    if (fread(&ppu_snap, sizeof(PPUSnapshot), 1, f) != 1) { fclose(f); return; }
    
    memcpy(emulator->ppu.V_RAM, ppu_snap.V_RAM, sizeof(ppu_snap.V_RAM));
    memcpy(emulator->ppu.OAM, ppu_snap.OAM, sizeof(ppu_snap.OAM));
    memcpy(emulator->ppu.palette, ppu_snap.palette, sizeof(ppu_snap.palette));
    emulator->ppu.ctrl = ppu_snap.ctrl;
    emulator->ppu.mask = ppu_snap.mask;
    emulator->ppu.status = ppu_snap.status;
    emulator->ppu.oam_address = ppu_snap.oam_address;
    emulator->ppu.v = ppu_snap.v;
    emulator->ppu.t = ppu_snap.t;
    emulator->ppu.x = ppu_snap.x;
    emulator->ppu.w = ppu_snap.w;

    fclose(f);
    LOG(INFO, "State loaded from %s", filename);
}

void init_emulator(struct Emulator* emulator, int argc, char *argv[]){
    if(argc < 2) {
        LOG(ERROR, "Input file not provided");
        quit(EXIT_FAILURE);
    }

    char* genie = NULL;
    if(argc == 3 || argc == 6)
        genie = argv[argc - 1];

    memset(emulator, 0, sizeof(Emulator));
    load_file(argv[1], genie, &emulator->mapper);
    emulator->type = emulator->mapper.type;
    emulator->mapper.emulator = emulator;
    if(emulator->type == PAL) {
        PERIOD = 1000000000 / PAL_FRAME_RATE;
        TURBO_SKIP = PAL_FRAME_RATE / PAL_TURBO_RATE;
    }else{
        PERIOD = 1000000000 / NTSC_FRAME_RATE;
        TURBO_SKIP = NTSC_FRAME_RATE / NTSC_TURBO_RATE;
    }

    GraphicsContext* g_ctx = &emulator->g_ctx;

#ifdef __ANDROID__
    if(argc < 4){
        LOG(ERROR, "Window dimensions not provided");
        return;
    }
    char** end_ptr = NULL;
    g_ctx->screen_width = strtol(argv[2], end_ptr, 10);
    g_ctx->screen_height = strtol(argv[3], end_ptr, 10);
    if(argc > 4)
        g_ctx->is_tv = strtol(argv[4], end_ptr, 10);
    else
        g_ctx->is_tv = 0;
#else
    g_ctx->screen_width = -1;
    g_ctx->screen_height = -1;
    g_ctx->is_tv = 0;
#endif

    g_ctx->width = 256;
    g_ctx->height = 240;
    g_ctx->scale = 2;

#if NAMETABLE_MODE
    g_ctx->width = 512;
    g_ctx->height = 480;
    g_ctx->scale = 1;
    if(emulator->mapper.is_nsf) {
        LOG(ERROR, "Can't run NSF Player in Nametable mode");
        quit(EXIT_FAILURE);
    }
    LOG(DEBUG, "RENDERING IN NAMETABLE MODE");
#endif
    get_graphics_context(g_ctx);
    SDL_SetWindowTitle(g_ctx->window, get_file_name(argv[1]));

    init_mem(emulator);
    init_ppu(emulator);
    init_cpu(emulator);
    init_APU(emulator);
    init_timer(&emulator->timer, PERIOD);
    ANDROID_INIT_TOUCH_PAD(g_ctx);

    emulator->exit = 0;
    emulator->pause = 0;
}


void run_emulator(struct Emulator* emulator){
    if(emulator->mapper.is_nsf) {
        run_NSF_player(emulator);
        return;
    }

    struct JoyPad* joy1 = &emulator->mem.joy1;
    struct JoyPad* joy2 = &emulator->mem.joy2;
    struct PPU* ppu = &emulator->ppu;
    struct c6502* cpu = &emulator->cpu;
    struct APU* apu = &emulator->apu;
    struct GraphicsContext* g_ctx = &emulator->g_ctx;
    struct Timer* timer = &emulator->timer;
    SDL_Event e;
    Timer frame_timer;
    init_timer(&frame_timer, PERIOD);
    mark_start(&frame_timer);

    // Variáveis para cálculo de FPS
    uint64_t frame_count = 0;
    uint64_t last_fps_time = SDL_GetTicks();
    float current_fps = 0.0f;

    while (!emulator->exit) {
#if PROFILE
        if(PROFILE_STOP_FRAME && ppu->frames >= PROFILE_STOP_FRAME)
            break;
#endif
        mark_start(timer);
        while (SDL_PollEvent(&e)) {
            update_joypad(joy1, &e);
            update_joypad(joy2, &e);
            if((joy1->status & 0xc) == 0xc || (joy2->status & 0xc) == 0xc) {
                reset_emulator(emulator);
            }
            switch (e.type) {
                case SDL_EVENT_KEY_DOWN:
                    switch (e.key.key) {
                        case SDLK_ESCAPE:
                            emulator->exit = 1;
                            break;
                        case SDLK_MEDIA_PLAY:
                        case SDLK_SPACE:
                            emulator->pause ^= 1;
                            TOGGLE_TIMER_RESOLUTION();
                            break;
                        case SDLK_F5:
                            reset_emulator(emulator);
                            break;
                        case SDLK_F6:
                            save_state(emulator, "save.dat");
                            break;
                        case SDLK_F7:
                            load_state(emulator, "save.dat");
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_EVENT_QUIT:
                    emulator->exit = 1;
                    break;
                default:
                    if(e.key.key == SDLK_AC_BACK
                        || e.key.scancode == SDL_SCANCODE_AC_BACK) {
                        emulator->exit = 1;
                        LOG(DEBUG, "Exiting emulator session (back button pressed)");
                    }
            }
        }

        // trigger turbo events
        if(ppu->frames % TURBO_SKIP == 0) {
            turbo_trigger(joy1);
            turbo_trigger(joy2);
        }

        if(!emulator->pause){
            // if ppu.render is set a frame is complete
            if(emulator->type == NTSC) {
                while (!ppu->render) {
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute(cpu);
                    execute_apu(apu);
                }
            }else{
                // PAL
                uint8_t check = 0;
                while (!ppu->render) {
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    check++;
                    if(check == 5) {
                        // on the fifth run execute an extra ppu clock
                        // this produces 3.2 scanlines per cpu clock
                        execute_ppu(ppu);
                        check = 0;
                    }
                    execute(cpu);
                    execute_apu(apu);
                }
            }
#if NAMETABLE_MODE
            render_name_tables(ppu, ppu->screen);
#endif
            // Cálculo de FPS
            frame_count++;
            uint64_t current_time = SDL_GetTicks();
            if (current_time > last_fps_time + 1000) {
                current_fps = frame_count / ((current_time - last_fps_time) / 1000.0f);
                last_fps_time = current_time;
                frame_count = 0;
            }

            // Passar FPS para a função de renderização
            render_graphics(g_ctx, ppu->screen, current_fps);
            
            ppu->render = 0;
            queue_audio(apu, g_ctx);
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

void reset_emulator(Emulator* emulator) {
    // Use as exit procedure for TV mode
    if(emulator->g_ctx.is_tv){
        emulator->exit = 1;
        LOG(DEBUG, "Exiting emulator session (reset on TV)");
        return;
    }
    LOG(INFO, "Resetting emulator");
    reset_cpu(&emulator->cpu);
    reset_APU(&emulator->apu);
    reset_ppu(&emulator->ppu);
    if(emulator->mapper.reset != NULL) {
        emulator->mapper.reset(&emulator->mapper);
    }
}

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
    PERIOD = 1000 * emulator->mapper.NSF->speed;
    double ms_per_frame = emulator->mapper.NSF->speed / 1000.0;
    init_timer(&frame_timer, PERIOD);
    mark_start(&frame_timer);
    size_t cycles_per_frame, nmi_cycle_start;
    if(emulator->type == PAL) {
        cycles_per_frame = emulator->mapper.NSF->speed * 1.662607f;
        // PAL has 70 scanlines (7459 cpu cycles) V-blank
        nmi_cycle_start = cycles_per_frame - 7459;
    }else {
        // NTSC and others have 20 scanlines (2273 cpu cycles) V-blank
        cycles_per_frame = emulator->mapper.NSF->speed * 1.789773f;
        nmi_cycle_start = cycles_per_frame - 2273;
    }
    uint8_t status1 = 0, status2 = 0;

    // initialize for the first song
    init_song(emulator, nsf->current_song);


    while (!emulator->exit) {
        mark_start(timer);
        while (SDL_PollEvent(&e)) {
            update_joypad(joy1, &e);
            update_joypad(joy2, &e);
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
                        case SDLK_ESCAPE:
                            emulator->exit = 1;
                            break;
                        case SDLK_MEDIA_PLAY:
                        case SDLK_SPACE:
                            emulator->pause ^= 1;
                            TOGGLE_TIMER_RESOLUTION();
                            break;
                        case SDLK_MEDIA_NEXT_TRACK:
                            next_song(emulator, nsf);
                            break;
                        case SDLK_MEDIA_PREVIOUS_TRACK:
                            prev_song(emulator, nsf);
                            break;
                        case SDLK_F5:
                            reset_emulator(emulator);
                            nsf->current_song = 1;
                            init_song(emulator, nsf->current_song);
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_EVENT_QUIT:
                    emulator->exit = 1;
                    break;
                default:
                    if(e.key.key == SDLK_AC_BACK
                        || e.key.scancode == SDL_SCANCODE_AC_BACK) {
                        emulator->exit = 1;
                        LOG(DEBUG, "Exiting emulator session");
                    }
            }
        }

        if(nsf->times != NULL && !nsf->initializing) {
            double track_dur = nsf->times[nsf->current_song == 0 ? 0 : nsf->current_song - 1];
            if(track_dur < nsf->tick) {
                if(nsf->tick_max < nsf->tick) {
                    next_song(emulator, nsf);
                } else if(nsf->fade != NULL) {
                    // fade
                    int fade_dur = nsf->fade[nsf->current_song == 0 ? 0 : nsf->current_song - 1];
                    apu->volume = (nsf->tick - track_dur) / (float)fade_dur;
                    // clamp to range (0,1) then invert
                    apu->volume = 1 - (apu->volume < 0 ? 0 : apu->volume > 1 ? 1 : apu->volume);
                }
            }
        }

        if(!emulator->pause){
            if ((nsf->flags & (NSF_NO_PLAY_SR | NSF_NON_RETURN_INIT)) == 0) {
                // run only if PLAY is not suppressed and non-return init is disabled
                run_cpu_subroutine(cpu, nsf->play_addr);
            }

            size_t cycles = 0;
            while (cycles < cycles_per_frame) {
                execute(cpu);
                if (!(cpu->mode & CPU_SR_ANY)) {
                    if (nsf->flags & NSF_NON_RETURN_INIT && nsf->init_num == 0) {
                        // second INIT
                        // will fail if there is an already running subroutine
                        // we already did the check earlier but just to be safe
                        if (run_cpu_subroutine(cpu, nsf->init_addr) == 0) {
                            cpu->ac = nsf->current_song > 0? nsf->current_song - 1: 0;
                            cpu->x = emulator->type == PAL? 1: 0;
                            cpu->y = 0x81;
                            nsf->init_num = 1;
                        }
                    }
                }
                if (nsf->flags & NSF_IRQ)
                    nsf_execute(emulator);
                if(!nsf->initializing) {
                    execute_apu(apu);
                }
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

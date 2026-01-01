#include <string.h>
#include <stdlib.h>

#include "ppu.h"
#include "utils.h"
#include "mmu.h"
#include "cpu6502.h"
#include "emulator.h"

static uint16_t render_background(PPU* ppu);
static uint16_t render_sprites(PPU* ppu, uint16_t bg_addr, uint8_t* back_priority);
static void update_NMI(PPU* ppu);

// Array global que será usado para renderizar
uint32_t nes_palette[64];
static size_t screen_size;

// --- Definições das Paletas ---

// 1. Padrão (NTSC genérico)
static const uint32_t palette_default[64] = {
    0xff666666, 0xff002a88, 0xff1412a7, 0xff3b00a4, 0xff5c007e, 0xff6e0040, 0xff6c0600, 0xff561d00,
    0xff333500, 0xff0b4800, 0xff005200, 0xff004f08, 0xff00404d, 0xff000000, 0xff000000, 0xff000000,
    0xffadadad, 0xff155fd9, 0xff4240ff, 0xff7527fe, 0xffa01acc, 0xffb71e7b, 0xffb53120, 0xff994e00,
    0xff6b6d00, 0xff388700, 0xff0c9300, 0xff008f32, 0xff007c8d, 0xff000000, 0xff000000, 0xff000000,
    0xfffffeff, 0xff64b0ff, 0xff9290ff, 0xffc676ff, 0xfff36aff, 0xfffe6ecc, 0xfffe8170, 0xffea9e22,
    0xffbcbe00, 0xff88d800, 0xff5ce430, 0xff45e082, 0xff48cdde, 0xff4f4f4f, 0xff000000, 0xff000000,
    0xfffffeff, 0xffc0dfff, 0xffd3d2ff, 0xffe8c8ff, 0xfffbc2ff, 0xfffec4ea, 0xfffeccc5, 0xfff7d8a5,
    0xffe4e594, 0xffcfef96, 0xffbdf4ab, 0xffb3f3cc, 0xffb5ebf2, 0xffb8b8b8, 0xff000000, 0xff000000,
};

// 2. Sony CXA (Mais vibrante/quente)
static const uint32_t palette_sony[64] = {
    0xff585858, 0xff00238c, 0xff00139b, 0xff2d0585, 0xff5d0052, 0xff7a0017, 0xff7a0800, 0xff5f1800,
    0xff352a00, 0xff093900, 0xff003f00, 0xff003c00, 0xff003234, 0xff000000, 0xff000000, 0xff000000,
    0xffa4a4a4, 0xff1850c8, 0xff2d3ad8, 0xff6025c0, 0xff9b128a, 0xffbe103f, 0xffbe2300, 0xff9c3e00,
    0xff665800, 0xff2e6a00, 0xff007200, 0xff006d07, 0xff005d5e, 0xff000000, 0xff000000, 0xff000000,
    0xfffefefe, 0xff6094fe, 0xff7b7afe, 0xffb562fe, 0xfffe49d2, 0xfffe4675, 0xfffe5e23, 0xffda8000,
    0xff9d9f00, 0xff5eb500, 0xff18c000, 0xff22ba46, 0xff16a4a9, 0xff3a3a3a, 0xff000000, 0xff000000,
    0xfffefefe, 0xffbdd4fe, 0xffc9c9fe, 0xffe0c0fe, 0xfffeb4ee, 0xfffeb2ca, 0xfffebca9, 0xffefca97,
    0xffd7d999, 0xffbfe29e, 0xffa4e7a6, 0xffa8e4c0, 0xffa2d9dc, 0xff989898, 0xff000000, 0xff000000
};

// 3. FCEUX (Cores clássicas de emulador)
static const uint32_t palette_fceux[64] = {
    0xff757575, 0xff271b8f, 0xff0000ab, 0xff47009f, 0xff8f0077, 0xffab0013, 0xffa70000, 0xff7f0b00,
    0xff432f00, 0xff004700, 0xff005100, 0xff003f17, 0xff1b3f5f, 0xff000000, 0xff000000, 0xff000000,
    0xffbcbcbc, 0xff0073ef, 0xff233bef, 0xff8300f3, 0xffbf00bf, 0xffe7005b, 0xffdb2b00, 0xffcb4f0f,
    0xff8b7300, 0xff009700, 0xff00ab00, 0xff00933b, 0xff00838b, 0xff000000, 0xff000000, 0xff000000,
    0xfff7f7f7, 0xff3fbfff, 0xff5f97ff, 0xffa78bfd, 0xfff77bff, 0xffff77b7, 0xffff7763, 0xffff9b3b,
    0xfff3bf3f, 0xff83d313, 0xff4fdf4b, 0xff58f898, 0xff00ebdb, 0xff666666, 0xff000000, 0xff000000,
    0xffffffff, 0xffabe7ff, 0xffc7d7ff, 0xffd7cbff, 0xffffc7ff, 0xffffc7db, 0xffffbfb3, 0xffffdbab,
    0xffffe7a3, 0xffe3ffa3, 0xffabf3bf, 0xffb3ffcf, 0xff9ffff3, 0xffdddddd, 0xff000000, 0xff000000
};

static const uint32_t* all_palettes[] = {
    palette_default,
    palette_sony,
    palette_fceux
};

void set_emulator_palette(PPU* ppu, int palette_index) {
    if (palette_index < 0 || palette_index >= PALETTE_COUNT) return;
    ppu->current_palette_index = palette_index;
    // Converte ARGB para ABGR ou o formato usado pela SDL
    to_pixel_format(all_palettes[palette_index], nes_palette, 64, SDL_PIXELFORMAT_ABGR8888);
}

void init_ppu(struct Emulator* emulator){
    PPU* ppu = &emulator->ppu;
    
    // Inicializa com a paleta padrão (0)
    set_emulator_palette(ppu, PALETTE_DEFAULT);
    
#if NAMETABLE_MODE
    screen_size = sizeof(uint32_t) * VISIBLE_SCANLINES * VISIBLE_DOTS * 4;
#else
    screen_size = sizeof(uint32_t) * VISIBLE_SCANLINES * VISIBLE_DOTS;
#endif
    ppu->screen = malloc(screen_size);
    ppu->emulator = emulator;
    ppu->mapper = &emulator->mapper;
    ppu->scanlines_per_frame = emulator->type == NTSC ? NTSC_SCANLINES_PER_FRAME : PAL_SCANLINES_PER_FRAME;

    memset(ppu->palette, 0, sizeof(ppu->palette));
    memset(ppu->OAM_cache, 0, sizeof(ppu->OAM_cache));
    memset(ppu->V_RAM, 0, sizeof(ppu->V_RAM));
    memset(ppu->OAM, 0, sizeof(ppu->OAM));
    ppu->oam_address = 0;
    ppu->v = 0;
    reset_ppu(ppu);
}

void reset_ppu(PPU* ppu){
    ppu->t = ppu->x = ppu->dots = 0;
    ppu->scanlines = 261;
    ppu->w = 1;
    ppu->ctrl &= ~0xFC;
    ppu->mask = 0;
    ppu->status = 0;
    ppu->frames = 0;
    ppu->OAM_cache_len = 0;
    memset(ppu->OAM_cache, 0, 8);
    memset(ppu->screen, 0, screen_size);
}

void exit_ppu(PPU* ppu) {
    if(ppu->screen != NULL) {
        free(ppu->screen);
    }
}

void set_address(PPU* ppu, uint8_t address){
    if(ppu->w){
        // first write
        ppu->t &= 0xff;
        ppu->t |= (address & 0x3f) << 8; // store only upto bit 14
        ppu->w = 0;
    }else{
        // second write
        ppu->t &= 0xff00;
        ppu->t |= address;
        ppu->v = ppu->t;
        ppu->w = 1;
    }
}


void set_oam_address(PPU* ppu, uint8_t address){
    ppu->oam_address = address;
}

uint8_t read_oam(PPU* ppu){
    return ppu->OAM[ppu->oam_address];
}

void write_oam(PPU* ppu, uint8_t value){
    ppu->OAM[ppu->oam_address++] = value;
}

void set_scroll(PPU* ppu, uint8_t coord){
    if(ppu->w){
        // first write
        ppu->t &= ~X_SCROLL_BITS;
        ppu->t |= (coord >> 3) & X_SCROLL_BITS;
        ppu->x = coord & 0x7;
        ppu->w = 0;
    }else{
        // second write
        ppu->t &= ~Y_SCROLL_BITS;
        ppu->t |= ((coord & 0x7) << 12) | ((coord & 0xF8) << 2);
        ppu->w = 1;
    }
}

uint8_t read_ppu(PPU* ppu){
    uint8_t prev_buff = ppu->buffer, data;
    ppu->buffer = read_vram(ppu, ppu->v);

    if(ppu->v >= 0x3F00) {
        data = ppu->buffer;
        // read underlying nametable mirrors into buffer
        // 0x3f00 - 0x3fff maps to 0x2f00 - 0x2fff
        ppu->buffer = read_vram(ppu, ppu->v & 0xefff);
    }else
        data = prev_buff;
    ppu->v += ((ppu->ctrl & BIT_2) ? 32 : 1);
    return data;
}

void write_ppu(PPU* ppu, uint8_t value){
    write_vram(ppu, ppu->v, value);
    ppu->v += ((ppu->ctrl & BIT_2) ? 32 : 1);
}

void dma(PPU* ppu, uint8_t address){
    Memory* memory = &ppu->emulator->mem;
    uint8_t* ptr = get_ptr(memory, address * 0x100);
    // halt CPU for DMA and skip extra cycle if on odd cycle
    do_DMA(&ppu->emulator->cpu, 513 + ppu->emulator->cpu.odd_cycle);
    if(ptr == NULL) {
        // Probably in PRG ROM so it is not possible to resolve a pointer
        // due to bank switching, so we do it the slow hard way
        for(int i = 0; i < 256; i++) {
            ppu->OAM[(ppu->oam_address + i) & 0xff] = read_mem(memory, address * 0x100 + i);
        }
    }else {
        // copy from OAM address to the end (256 bytes)
        memcpy(ppu->OAM + ppu->oam_address, ptr, 256 - ppu->oam_address);
        if(ppu->oam_address) {
            // wrap around and copy from start to OAM address if OAM is not 0x00
            memcpy(ppu->OAM, ptr + (256 - ppu->oam_address), ppu->oam_address);
        }
        // last value
        memory->bus = ptr[255];
    }
}



uint8_t read_vram(PPU* ppu, uint16_t address){
    address = address & 0x3fff;

    if(address < 0x2000) {
        ppu->bus = ppu->mapper->read_CHR(ppu->mapper, address);
        return ppu->bus;
    }

    if(address < 0x3F00){
        address = (address & 0xefff) - 0x2000;
        ppu->bus = ppu->V_RAM[ppu->mapper->name_table_map[address / 0x400] + (address & 0x3ff)];
        return ppu->bus;
    }

    if(address < 0x4000)
        // palette RAM provide first 6 bits and remaining 2 bits are open bus
        return ppu->palette[(address - 0x3F00) % 0x20] & 0x3f | (ppu->bus & 0xc0);

    return 0;
}

void write_vram(PPU* ppu, uint16_t address, uint8_t value){
    address = address & 0x3fff;
    ppu->bus = value;

    if(address < 0x2000)
        ppu->mapper->write_CHR(ppu->mapper, address, value);
    else if(address < 0x3F00){
        address = (address & 0xefff) - 0x2000;
        ppu->V_RAM[ppu->mapper->name_table_map[address / 0x400] + (address & 0x3ff)] = value;
    }

    else if(address < 0x4000) {
        address = (address - 0x3F00) % 0x20;
        if(address % 4 == 0) {
            ppu->palette[address] = value;
            ppu->palette[address ^ 0x10] = value;
        }
        else
            ppu->palette[address] = value;
    }

}

uint8_t read_status(PPU* ppu){
    uint8_t status = ppu->status;
    ppu->w = 1;
    ppu->status &= ~BIT_7; // reset v_blank
    update_NMI(ppu);
    return status;
}

void set_ctrl(PPU* ppu, uint8_t ctrl){
    ppu->ctrl = ctrl;
    update_NMI(ppu);
    // set name table in temp address
    ppu->t &= ~0xc00;
    ppu->t |= (ctrl & BASE_NAMETABLE) << 10;
}

static void update_NMI(PPU* ppu) {
    if(ppu->ctrl & BIT_7 && ppu->status & BIT_7)
        interrupt(&ppu->emulator->cpu, NMI);
    else
        interrupt_clear(&ppu->emulator->cpu, NMI);
}

void execute_ppu(PPU* ppu){
    if(ppu->scanlines < VISIBLE_SCANLINES){
        // render scanlines 0 - 239
        if(ppu->dots > 0 && ppu->dots <= VISIBLE_DOTS){
            int x = (int)ppu->dots - 1;
            uint8_t fine_x = ((uint16_t)ppu->x + x) % 8, palette_addr = 0, palette_addr_sp = 0, back_priority = 0;

            if(ppu->mask & SHOW_BG){
                palette_addr = render_background(ppu);
                if(fine_x == 7) {
                    if ((ppu->v & COARSE_X) == 31) {
                        ppu->v &= ~COARSE_X;
                        // switch horizontal nametable
                        ppu->v ^= 0x400;
                    }
                    else
                        ppu->v++;
                }
            }
            if(ppu->mask & SHOW_SPRITE && ((ppu->mask & SHOW_SPRITE_8) || x >=8)){
                palette_addr_sp = render_sprites(ppu, palette_addr, &back_priority);
            }
            if((!palette_addr && palette_addr_sp) || (palette_addr && palette_addr_sp && !back_priority))
                palette_addr = palette_addr_sp;

            palette_addr = ppu->palette[palette_addr];
            ppu->screen[ppu->scanlines * VISIBLE_DOTS + ppu->dots - 1] = nes_palette[palette_addr];
        }
        if(ppu->dots == VISIBLE_DOTS + 1 && ppu->mask & SHOW_BG){
            if((ppu->v & FINE_Y) != FINE_Y) {
                // increment coarse x
                ppu->v += 0x1000;
            }
            else{
                ppu->v &= ~FINE_Y;
                uint16_t coarse_y = (ppu->v & COARSE_Y) >> 5;
                if(coarse_y == 29){
                    coarse_y = 0;
                    // toggle bit 11 to switch vertical nametable
                    ppu->v ^= 0x800;
                }
                else if(coarse_y == 31){
                    // nametable not switched
                    coarse_y = 0;
                }
                else{
                    coarse_y++;
                }

                ppu->v = (ppu->v & ~COARSE_Y) | (coarse_y << 5);
            }
        }
        else if(ppu->dots == VISIBLE_DOTS + 2 && (ppu->mask & RENDER_ENABLED)){
            ppu->v &= ~HORIZONTAL_BITS;
            ppu->v |= ppu->t & HORIZONTAL_BITS;
        }
        else if(ppu->dots == VISIBLE_DOTS + 4 && ppu->mask & SHOW_SPRITE && ppu->mask & SHOW_BG) {
            ppu->mapper->on_scanline(ppu->mapper);
        }
        else if(ppu->dots == 320 && ppu->mask & RENDER_ENABLED){
            memset(ppu->OAM_cache, 0, 8);
            ppu->OAM_cache_len = 0;
            uint8_t range = ppu->ctrl & LONG_SPRITE ? 16: 8;
            for(size_t i = ppu->oam_address / 4; i < 64; i++){
                int diff = (int)ppu->scanlines - ppu->OAM[i * 4];
                if(diff >= 0 && diff < range){
                    ppu->OAM_cache[ppu->OAM_cache_len++] = i * 4;
                    if(ppu->OAM_cache_len >= 8)
                        break;
                }
            }
        }
    }
    else if(ppu->scanlines == VISIBLE_SCANLINES){
        // post render scanline 240/239
    }
    else if(ppu->scanlines < ppu->scanlines_per_frame){
        // v blanking scanlines 241 - 261/311
        if(ppu->dots == 1 && ppu->scanlines == VISIBLE_SCANLINES + 1){
            // set v-blank
            ppu->status |= V_BLANK;
            update_NMI(ppu);
        }
    }
    else{
        // pre-render scanline 262/312
        if(ppu->dots == 1){
            // reset v-blank and sprite zero hit
            ppu->status &= ~(V_BLANK | SPRITE_0_HIT);
            update_NMI(ppu);
        }
        else if(ppu->dots == VISIBLE_DOTS + 2 && (ppu->mask & RENDER_ENABLED)){
            ppu->v &= ~HORIZONTAL_BITS;
            ppu->v |= ppu->t & HORIZONTAL_BITS;
        }
        else if(ppu->dots == VISIBLE_DOTS + 4 && ppu->mask & SHOW_SPRITE && ppu->mask & SHOW_BG) {
            ppu->mapper->on_scanline(ppu->mapper);
        }
        else if(ppu->dots > 280 && ppu->dots <= 304 && (ppu->mask & RENDER_ENABLED)){
            ppu->v &= ~VERTICAL_BITS;
            ppu->v |= ppu->t & VERTICAL_BITS;
        }
        else if(ppu->dots == END_DOT - 1 && ppu->frames & 1 && ppu->mask & RENDER_ENABLED && ppu->emulator->type == NTSC) {
            // skip one cycle on odd frames if rendering is enabled for NTSC
            ppu->dots++;
        }

        if(ppu->dots >= END_DOT) {
            // inform emulator to render contents of ppu on first dot
            ppu->render = 1;
            ppu->frames++;
        }
    }

    // increment dots and scanlines

    if(++ppu->dots >= DOTS_PER_SCANLINE) {
        if (ppu->scanlines++ >= ppu->scanlines_per_frame)
            ppu->scanlines = 0;
        ppu->dots = 0;
    }
}


static uint16_t render_background(PPU* ppu){
    int x = (int)ppu->dots - 1;
    uint8_t fine_x = ((uint16_t)ppu->x + x) & 0x7;

    if(!(ppu->mask & SHOW_BG_8) && x < 8)
        return 0;

    // Cache de tile para evitar múltiplas leituras de VRAM por pixel
    static uint16_t last_v = 0xFFFF;
    static uint8_t cached_tile_low = 0;
    static uint8_t cached_tile_high = 0;
    static uint8_t cached_attr = 0;

    uint16_t current_v_tile = ppu->v & 0x0FFF; // Ignora fine Y para o endereço do tile
    uint16_t fine_y = (ppu->v >> 12) & 0x7;

    if (last_v != (ppu->v & 0x7FFF)) {
        uint16_t tile_addr = 0x2000 | current_v_tile;
        uint16_t attr_addr = 0x23C0 | (ppu->v & 0x0C00) | ((ppu->v >> 4) & 0x38) | ((ppu->v >> 2) & 0x07);
        uint8_t tile_index = read_vram(ppu, tile_addr);
        uint16_t pattern_addr = (tile_index * 16 + fine_y) | ((ppu->ctrl & BG_TABLE) << 8);
        
        cached_tile_low = read_vram(ppu, pattern_addr);
        cached_tile_high = read_vram(ppu, pattern_addr + 8);
        cached_attr = read_vram(ppu, attr_addr);
        last_v = ppu->v & 0x7FFF;
    }

    uint16_t palette_addr = (cached_tile_low >> (7 ^ fine_x)) & 1;
    palette_addr |= ((cached_tile_high >> (7 ^ fine_x)) & 1) << 1;

    if(!palette_addr)
        return 0;

    return palette_addr | (((cached_attr >> ((ppu->v >> 4) & 4 | ppu->v & 2)) & 0x3) << 2);
}

static uint16_t render_sprites(PPU* restrict ppu, uint16_t bg_addr, uint8_t* restrict back_priority){
    // 4 bytes per sprite
    // byte 0 -> y index
    // byte 1 -> tile index
    // byte 2 -> render info
    // byte 3 -> x index
    int x = (int)ppu->dots - 1, y = (int)ppu->scanlines;
    uint16_t palette_addr = 0;
    uint8_t length = ppu->ctrl & LONG_SPRITE ? 16: 8;
    for(int j = 0; j < ppu->OAM_cache_len; j++) {
        int i = ppu->OAM_cache[j];
        uint8_t tile_x = ppu->OAM[i + 3];

        if (x - tile_x < 0 || x - tile_x >= 8)
            continue;

        uint16_t tile = ppu->OAM[i + 1];
        uint8_t tile_y = ppu->OAM[i] + 1;
        uint8_t attr = ppu->OAM[i + 2];
        int x_off = (x - tile_x) % 8, y_off = (y - tile_y) % length;

        if (!(attr & FLIP_HORIZONTAL))
            x_off ^= 7;
        if (attr & FLIP_VERTICAL)
            y_off ^= (length - 1);

        uint16_t tile_addr;

        if (ppu->ctrl & LONG_SPRITE) {
            y_off = y_off & 7 | ((y_off & 8) << 1);
            tile_addr = (tile >> 1) * 32 + y_off;
            tile_addr |= (tile & 1) << 12;
        } else {
            tile_addr = tile * 16 + y_off + (ppu->ctrl & SPRITE_TABLE ? 0x1000 : 0);
        }

        palette_addr = (read_vram(ppu, tile_addr) >> x_off) & 1;
        palette_addr |= ((read_vram(ppu, tile_addr + 8) >> x_off) & 1) << 1;

        if (!palette_addr)
            continue;

        palette_addr |= 0x10 | ((attr & 0x3) << 2);
        *back_priority = attr & BIT_5;

        // sprite hit evaluation

        if (!(ppu->status & SPRITE_0_HIT)
            && (ppu->mask & SHOW_BG)
            && i == 0
            && palette_addr
            && bg_addr
            && x < 255)
            ppu->status |= SPRITE_0_HIT;
        break;
    }
    return palette_addr;
}

#ifndef TAS_H
#define TAS_H

#include "emulator.h" // Necessário para a struct Emulator

void refresh_script_list(Emulator* emu);
void handle_script_selector_input(Emulator* emu, int x, int y);
void render_script_selector_ui(Emulator* emu);

#endif // TAS_H

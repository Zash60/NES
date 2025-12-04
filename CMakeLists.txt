cmake_minimum_required(VERSION 3.22.1)

project(nes C)

# --- Configuração de Diretórios ---
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LUA_DIR ${SRC_DIR}/lua)
set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# --- SDL3 Setup ---
# Assume que você tem o SDL3 clonado em external/SDL
add_subdirectory(${EXTERNAL_DIR}/SDL)

# --- SDL3_ttf Setup ---
# Assume que você tem o SDL_ttf clonado em external/SDL_ttf
add_subdirectory(${EXTERNAL_DIR}/SDL_ttf)

# --- Lua Setup (Compilando da fonte) ---
# Excluímos lua.c e luac.c pois eles contêm funções main() que conflitam
file(GLOB LUA_SOURCES 
    "${LUA_DIR}/*.c"
)
list(REMOVE_ITEM LUA_SOURCES 
    "${LUA_DIR}/lua.c" 
    "${LUA_DIR}/luac.c"
)

# Cria a biblioteca estática do Lua
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${LUA_DIR})

# Adiciona a flag para compilar como C Linux/Android padrão
target_compile_definitions(lua PRIVATE LUA_USE_LINUX)

# --- NES Emulator Setup ---

# Lista todos os seus arquivos fonte
set(NES_SOURCES
    ${SRC_DIR}/main.c
    ${SRC_DIR}/emulator.c
    ${SRC_DIR}/cpu6502.c
    ${SRC_DIR}/ppu.c
    ${SRC_DIR}/apu.c
    ${SRC_DIR}/mmu.c
    ${SRC_DIR}/mappers/mapper.c
    ${SRC_DIR}/mappers/uxrom.c
    ${SRC_DIR}/mappers/mmc1.c
    ${SRC_DIR}/mappers/cnrom.c
    ${SRC_DIR}/mappers/gnrom.c
    ${SRC_DIR}/mappers/axrom.c
    ${SRC_DIR}/mappers/mmc3.c
    ${SRC_DIR}/mappers/colordreams.c
    ${SRC_DIR}/controller.c
    ${SRC_DIR}/gamepad.c
    ${SRC_DIR}/gfx.c
    ${SRC_DIR}/timers.c
    ${SRC_DIR}/utils.c
    ${SRC_DIR}/nsf.c
    ${SRC_DIR}/genie.c
    ${SRC_DIR}/debugtools.c
    ${SRC_DIR}/biquad.c
    # Novos arquivos TAS
    ${SRC_DIR}/touchpad.c
    ${SRC_DIR}/lua_bridge.c
)

# Cria a biblioteca compartilhada (libnes.so ou libmain.so)
add_library(main SHARED ${NES_SOURCES})

# --- Include Directories ---
target_include_directories(main PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/mappers
    ${LUA_DIR} # Importante para lua_bridge.c encontrar lua.h
)

# --- Definições de Compilação ---
target_compile_definitions(main PRIVATE 
    ANDROID 
    DEBUGGING_ENABLED
)

# --- Linkagem ---
# Linka SDL3, SDL3_ttf, Lua e bibliotecas do Android (log, android)
target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    lua
    android
    log
    m # Math library
)

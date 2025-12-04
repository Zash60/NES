cmake_minimum_required(VERSION 3.22.1)

project(nes C)

# --- Configuração de Diretórios ---
# Define onde estão os códigos fonte
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LUA_DIR ${SRC_DIR}/lua)
set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# --- SDL3 Setup ---
# Adiciona o SDL3 que deve estar em external/SDL
add_subdirectory(${EXTERNAL_DIR}/SDL)

# --- SDL3_ttf Setup ---
# Adiciona o SDL3_ttf que deve estar em external/SDL_ttf
add_subdirectory(${EXTERNAL_DIR}/SDL_ttf)

# --- Lua Setup ---
# Compila o Lua estaticamente a partir da pasta src/lua
file(GLOB LUA_SOURCES 
    "${LUA_DIR}/*.c"
)
# Remove arquivos que contêm 'main' conflitantes
list(REMOVE_ITEM LUA_SOURCES 
    "${LUA_DIR}/lua.c" 
    "${LUA_DIR}/luac.c"
)

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${LUA_DIR})
target_compile_definitions(lua PRIVATE LUA_USE_LINUX)

# --- NES Emulator Setup ---

# Lista de todos os arquivos fonte do emulador
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
    ${SRC_DIR}/touchpad.c
    ${SRC_DIR}/lua_bridge.c
)

# Cria a biblioteca compartilhada (libnes.so / libmain.so)
add_library(main SHARED ${NES_SOURCES})

# Define onde encontrar os headers (.h)
target_include_directories(main PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/mappers
    ${LUA_DIR}
)

# Definições de pré-processador
target_compile_definitions(main PRIVATE 
    ANDROID 
    DEBUGGING_ENABLED
)

# Linka as bibliotecas necessárias
target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    lua
    android
    log
    m
)

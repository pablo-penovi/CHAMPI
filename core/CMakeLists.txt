# The CHOMPI firmware, compiled unchanged against daisycola.

set(TAPE ${CHOMPI_DIR}/firmware/chompi-tape/code)

# DaisySP from TAPE's tree, as its own library.
file(GLOB_RECURSE champi_daisysp_sources CONFIGURE_DEPENDS ${TAPE}/libs/DaisySP/Source/*.cpp)
add_library(champi_daisysp STATIC ${champi_daisysp_sources})
target_include_directories(champi_daisysp PUBLIC ${TAPE}/libs/DaisySP/Source)
foreach(dir Control Drums Dynamics Effects Filters Noise PhysicalModeling Synthesis Utility)
    target_include_directories(champi_daisysp PRIVATE ${TAPE}/libs/DaisySP/Source/${dir})
endforeach()
target_link_libraries(champi_daisysp PRIVATE champi_firmware_flags)

# TAPE itself. Its main() is renamed so a host program can start it on daisycola's firmware thread.
add_library(champi_fw_tape STATIC
    ${TAPE}/src/chompi_main.cpp
    ${TAPE}/src/encoder.cpp
    ${TAPE}/src/FileStreamingManager.cpp
    ${TAPE}/libs/coreJSON/source/core_json.c
)
target_include_directories(champi_fw_tape PUBLIC ${TAPE}/src ${TAPE}/libs/coreJSON/source/include)
target_compile_definitions(champi_fw_tape PRIVATE main=chompi_fw_main)
# daisycola comes before DaisySP so its include directories win (it carries a DelayLine fix).
target_link_libraries(champi_fw_tape
    PUBLIC daisycola champi_daisysp
    PRIVATE champi_firmware_flags)

# The virtual SD card, seeded from the firmware's factory card profile.
add_library(champi_sd STATIC sd_card.cpp sd_cli.cpp)
target_include_directories(champi_sd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(champi_sd PRIVATE
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
target_link_libraries(champi_sd PUBLIC daisycola PRIVATE champi_warnings)

# The CHOMPI board on top of daisycola: panel wiring, the MP2722 charger and the LED layout.
add_library(champi_board STATIC panel_state.cpp mp2722.cpp led_frame.cpp)
target_include_directories(champi_board PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_board PUBLIC daisycola PRIVATE champi_warnings)

# TAPE running on daisycola's virtual MCU, with the board wired up.
add_library(champi_runtime STATIC runtime.cpp)
target_include_directories(champi_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_runtime PUBLIC champi_board PRIVATE champi_fw_tape champi_warnings)

# What a host audio callback needs around the firmware: channel mapping, resampling and the
# firmware's MIDI out split into messages.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
add_library(champi_host STATIC host_audio.cpp midi_splitter.cpp)
target_include_directories(champi_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_host PUBLIC daisycola PRIVATE PkgConfig::SAMPLERATE champi_warnings)

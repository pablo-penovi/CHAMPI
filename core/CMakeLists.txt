# The CHOMPI firmware, compiled unchanged against daisycola.

set(TAPE ${CHOMPI_DIR}/firmware/chompi-tape/code)

# DaisySP from TAPE's tree, as its own library, built to go into the firmware library.
file(GLOB_RECURSE champi_daisysp_sources CONFIGURE_DEPENDS ${TAPE}/libs/DaisySP/Source/*.cpp)
add_library(champi_daisysp STATIC ${champi_daisysp_sources})
target_include_directories(champi_daisysp PUBLIC ${TAPE}/libs/DaisySP/Source)
foreach(dir Control Drums Dynamics Effects Filters Noise PhysicalModeling Synthesis Utility)
    target_include_directories(champi_daisysp PRIVATE ${TAPE}/libs/DaisySP/Source/${dir})
endforeach()
target_link_libraries(champi_daisysp PRIVATE champi_firmware_flags)
daisycola_firmware_library(champi_daisysp)

# TAPE itself, as a firmware library (libchampi_fw_tape.so) that Runtime loads: inserting a card
# power-cycles it, unloading it and loading it again so TAPE starts from a clean state.
# tape_probe.cpp lets Runtime read TAPE's boot flags. The library sits next to the executables.
daisycola_add_firmware(champi_fw_tape SOURCES
    ${TAPE}/src/chompi_main.cpp
    ${TAPE}/src/encoder.cpp
    ${TAPE}/src/FileStreamingManager.cpp
    ${TAPE}/libs/coreJSON/source/core_json.c
    tape_probe.cpp
)
target_include_directories(champi_fw_tape PRIVATE ${TAPE}/src ${TAPE}/libs/coreJSON/source/include)
# daisycola's include directories come before DaisySP's, so its DelayLine fix wins.
target_link_libraries(champi_fw_tape PRIVATE champi_daisysp champi_firmware_flags)
set_target_properties(champi_fw_tape PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# A release links libstdc++ in statically. Its GNU unique symbols (std::string::npos and others),
# if exported, can make glibc keep the library loaded after dlclose, and a power cycle then fails.
# Nothing from a static library needs exporting: the entry point and the boot probe aren't in one.
target_link_options(champi_fw_tape PRIVATE -Wl,--exclude-libs,ALL)

# The virtual SD card: a folder seeded from the firmware's factory card profile, what TAPE expects
# on it, and the folder browser the Insert card dialog uses.
add_library(champi_sd STATIC sd_card.cpp sd_cli.cpp card_check.cpp folder_browser.cpp)
target_include_directories(champi_sd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(champi_sd PRIVATE
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
target_link_libraries(champi_sd PRIVATE champi_warnings)

# The CHOMPI board on top of daisycola's host API: panel wiring, the MP2722 charger and the LED
# layout. Programs link the daisycola they run on: daisycola_host to load the firmware library
# (through champi_runtime), or daisycola itself for tests that drive the board without firmware.
add_library(champi_board STATIC panel_state.cpp mp2722.cpp led_frame.cpp)
target_include_directories(champi_board PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_board PUBLIC daisycola_headers PRIVATE champi_warnings)

# TAPE's library loaded on daisycola_host, with the board wired up and a card in.
add_library(champi_runtime STATIC runtime.cpp)
target_include_directories(champi_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(champi_runtime PRIVATE
    CHAMPI_FW_TAPE="$<TARGET_FILE:champi_fw_tape>"
    CHAMPI_FW_TAPE_NAME="$<TARGET_FILE_NAME:champi_fw_tape>")
target_link_libraries(champi_runtime PUBLIC champi_board champi_sd daisycola_host PRIVATE champi_warnings)
add_dependencies(champi_runtime champi_fw_tape)

# What a host audio callback needs around the firmware: channel mapping, resampling and the
# firmware's MIDI out split into messages.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
add_library(champi_host STATIC host_audio.cpp midi_splitter.cpp)
target_include_directories(champi_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_host PUBLIC daisycola_headers PRIVATE PkgConfig::SAMPLERATE champi_warnings)

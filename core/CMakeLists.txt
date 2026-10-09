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

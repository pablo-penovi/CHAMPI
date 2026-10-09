cmake_minimum_required(VERSION 3.20)
project(champi LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_EXTENSIONS ON)

# --- Options -------------------------------------------------------------------------------------

set(CHOMPI_FIRMWARE "tape" CACHE STRING "Which CHOMPI firmware to build: tape (tempo and wave later)")
set_property(CACHE CHOMPI_FIRMWARE PROPERTY STRINGS tape)
if(NOT CHOMPI_FIRMWARE STREQUAL "tape")
    message(FATAL_ERROR "CHOMPI_FIRMWARE=${CHOMPI_FIRMWARE} is not supported yet; only tape is")
endif()

set(CHAMPI_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(CHOMPI_DIR ${CHAMPI_THIRD_PARTY}/CHOMPI)

foreach(submodule CHOMPI/firmware daisycola/CMakeLists.txt DPF/CMakeLists.txt)
    if(NOT EXISTS ${CHAMPI_THIRD_PARTY}/${submodule})
        message(FATAL_ERROR
            "third_party/${submodule} is missing: run `git submodule update --init --recursive`")
    endif()
endforeach()

# Firmware sources are compiled unchanged, so their warnings are silenced. CHAMPI's own code links
# champi_warnings instead.
add_library(champi_firmware_flags INTERFACE)
target_compile_options(champi_firmware_flags INTERFACE -w)

add_library(champi_warnings INTERFACE)
target_compile_options(champi_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter>)

# --- Third party ---------------------------------------------------------------------------------

# daisycola derives the TAPE libDaisy fork from the CHOMPI checkout.
set(DAISYCOLA_CHOMPI_DIR ${CHOMPI_DIR} CACHE PATH "CHOMPI checkout used by daisycola" FORCE)
set(DAISYCOLA_BUILD_TESTS OFF CACHE BOOL "Build the daisycola tests" FORCE)
add_subdirectory(third_party/daisycola)

# DPF only provides dpf_add_plugin() here; its libraries are built per plugin target.
set(DPF_LIBRARIES OFF CACHE BOOL "Build the DPF libraries" FORCE)
set(DPF_EXAMPLES OFF CACHE BOOL "Build the DPF examples" FORCE)
add_subdirectory(third_party/DPF)

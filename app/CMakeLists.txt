# champi: the standalone JACK app on DPF.

find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)

# The panel's layout, hit-testing, LED look and mouse handling: no DPF, so the tests can use it.
add_library(champi_panel STATIC panel.cpp)
target_include_directories(champi_panel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_panel PUBLIC champi_board PRIVATE champi_warnings)

dpf_add_plugin(champi
    TARGETS jack
    UI_TYPE opengl
    FILES_DSP champi_plugin.cpp
    FILES_UI champi_ui.cpp)
target_include_directories(champi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi PUBLIC champi_runtime champi_host)
target_link_libraries(champi-dsp PRIVATE champi_warnings)
target_link_libraries(champi-ui PRIVATE champi_panel champi_warnings)

# DPF's standalone main() becomes dpf_jack_main, so CHAMPI's options can run first.
set_source_files_properties(${DPF_ROOT_DIR}/distrho/DistrhoPluginMain.cpp
    PROPERTIES COMPILE_DEFINITIONS main=dpf_jack_main)
target_sources(champi-jack PRIVATE main.cpp jack_monitor.cpp)
target_link_libraries(champi-jack PRIVATE champi_sd PkgConfig::JACK)

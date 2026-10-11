# CHAMPI tests.

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

# TAPE's firmware library is linked with --no-undefined (outside sanitizer builds), so building it
# checks that daisycola defines every symbol the firmware uses.
#
# The tests below drive daisycola's board with no firmware, so they link daisycola itself rather
# than daisycola_host: the board is in the program and needs no library loaded.

# TAPE's encoder code, for the board tests to read the panel through, as chompi::Hardware does.
set(TAPE ${CHOMPI_DIR}/firmware/chompi-tape/code)
add_library(champi_test_tape_encoder STATIC ${TAPE}/src/encoder.cpp)
target_include_directories(champi_test_tape_encoder SYSTEM PUBLIC ${TAPE}/src)
target_link_libraries(champi_test_tape_encoder PUBLIC daisycola PRIVATE champi_firmware_flags)

add_executable(champi-tests
    board_test.cpp
    sd_card_test.cpp
    card_check_test.cpp
    folder_browser_test.cpp
    script_test.cpp
    sd_cli_test.cpp
    host_test.cpp
)
target_link_libraries(champi-tests PRIVATE
    champi_board champi_sd champi_script champi_host champi_test_tape_encoder daisycola champi_warnings
    GTest::gtest_main)
target_compile_definitions(champi-tests PRIVATE
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
gtest_discover_tests(champi-tests DISCOVERY_MODE PRE_TEST)

# Insert card: card.toml, the start-up card order, the folder picker and the flow, with the power
# cycle behind its interface.
add_executable(champi-cards-tests insert_card_test.cpp)
target_link_libraries(champi-cards-tests PRIVATE champi_panel champi_board daisycola champi_warnings GTest::gtest_main)
target_compile_definitions(champi-cards-tests PRIVATE
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
gtest_discover_tests(champi-cards-tests DISCOVERY_MODE PRE_TEST)

# TAPE's firmware library power-cycled between cards in one process, as Insert card does.
add_executable(champi-runtime-tests runtime_test.cpp)
target_link_libraries(champi-runtime-tests PRIVATE champi_runtime champi_warnings GTest::gtest_main)
target_compile_definitions(champi-runtime-tests PRIVATE
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
gtest_discover_tests(champi-runtime-tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 300)

# Golden tests: scripts run through champi-headless, one process per run.
add_executable(champi-headless-tests headless_test.cpp)
target_link_libraries(champi-headless-tests PRIVATE champi_warnings GTest::gtest_main)
target_compile_definitions(champi-headless-tests PRIVATE
    CHAMPI_HEADLESS="$<TARGET_FILE:champi-headless>"
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
add_dependencies(champi-headless-tests champi-headless)
gtest_discover_tests(champi-headless-tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 300)

# The on-screen panel's layout, hit-testing, LED look and mouse handling, on a PanelState of its
# own (one per process).
add_executable(champi-panel-tests panel_test.cpp)
target_link_libraries(champi-panel-tests PRIVATE champi_panel champi_board daisycola champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-panel-tests DISCOVERY_MODE PRE_TEST)

# The computer keyboard: the keymap and keymap.toml, and every key into a PanelState of its own.
add_executable(champi-keyboard-tests keyboard_test.cpp)
target_link_libraries(champi-keyboard-tests PRIVATE champi_panel champi_board daisycola champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-keyboard-tests DISCOVERY_MODE PRE_TEST)

# The connections: the routing model on a fake JACK, connections.toml, and the menu's logic.
add_executable(champi-routing-tests routing_test.cpp)
target_link_libraries(champi-routing-tests PRIVATE champi_panel champi_routing champi_board daisycola champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-routing-tests DISCOVERY_MODE PRE_TEST)

# The MIDI controller mapping: mapping files, learning, the menu's table, and mapped messages into
# a PanelState of its own.
add_executable(champi-midi-map-tests midi_map_test.cpp)
target_link_libraries(champi-midi-map-tests PRIVATE champi_panel champi_board daisycola champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-midi-map-tests DISCOVERY_MODE PRE_TEST)

# app/panel_layout.h must be what the extractor makes from the board files.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME champi_panel_layout_up_to_date
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/extract_panel_layout.py --check
                --chompi ${CHOMPI_DIR})
endif()

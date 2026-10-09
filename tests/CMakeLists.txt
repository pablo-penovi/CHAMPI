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

# Links the TAPE firmware into a program. Building it is the check: the link fails on any symbol
# the firmware uses that daisycola doesn't define.
add_executable(champi_fw_tape_link fw_tape_link.cpp)
target_link_libraries(champi_fw_tape_link PRIVATE champi_fw_tape champi_warnings)
add_test(NAME champi_fw_tape_link COMMAND champi_fw_tape_link)

# TAPE's encoder code, for the board tests to read the panel through, as chompi::Hardware does.
set(TAPE ${CHOMPI_DIR}/firmware/chompi-tape/code)
add_library(champi_test_tape_encoder STATIC ${TAPE}/src/encoder.cpp)
target_include_directories(champi_test_tape_encoder SYSTEM PUBLIC ${TAPE}/src)
target_link_libraries(champi_test_tape_encoder PUBLIC daisycola PRIVATE champi_firmware_flags)

add_executable(champi-tests
    board_test.cpp
    sd_card_test.cpp
    script_test.cpp
    sd_cli_test.cpp
)
target_link_libraries(champi-tests PRIVATE
    champi_board champi_sd champi_script champi_test_tape_encoder champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-tests DISCOVERY_MODE PRE_TEST)

# Golden tests: scripts run through champi-headless, one firmware process per run.
add_executable(champi-headless-tests headless_test.cpp)
target_link_libraries(champi-headless-tests PRIVATE daisycola champi_warnings GTest::gtest_main)
target_compile_definitions(champi-headless-tests PRIVATE
    CHAMPI_HEADLESS="$<TARGET_FILE:champi-headless>"
    CHAMPI_FACTORY_CARD_DIR="${CHOMPI_DIR}/firmware/card-profiles/tape-2.0")
add_dependencies(champi-headless-tests champi-headless)
gtest_discover_tests(champi-headless-tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 300)

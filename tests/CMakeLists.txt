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

add_executable(champi-tests
    sd_card_test.cpp
    sd_cli_test.cpp
)
target_link_libraries(champi-tests PRIVATE champi_sd champi_warnings GTest::gtest_main)
gtest_discover_tests(champi-tests DISCOVERY_MODE PRE_TEST)

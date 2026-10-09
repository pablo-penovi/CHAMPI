# champi-headless scripts, as a library so the tests can parse them too.
add_library(champi_script STATIC script.cpp)
target_include_directories(champi_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(champi_script PUBLIC champi_board PRIVATE champi_warnings)

add_executable(champi-headless champi_headless.cpp wav_writer.cpp)
target_link_libraries(champi-headless PRIVATE champi_runtime champi_script champi_sd champi_host champi_warnings)

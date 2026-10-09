add_executable(champi-headless champi_headless.cpp)
target_link_libraries(champi-headless PRIVATE champi_sd champi_warnings)

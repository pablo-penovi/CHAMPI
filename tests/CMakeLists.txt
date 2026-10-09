# CHAMPI tests.

# Links the TAPE firmware into a program. Building it is the check: the link fails on any symbol
# the firmware uses that daisycola doesn't define.
add_executable(champi_fw_tape_link fw_tape_link.cpp)
target_link_libraries(champi_fw_tape_link PRIVATE champi_fw_tape champi_warnings)
add_test(NAME champi_fw_tape_link COMMAND champi_fw_tape_link)

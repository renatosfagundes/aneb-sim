set(SIMAVR_DIR ${CMAKE_SOURCE_DIR}/external/simavr)

# simavr's Makefile builds into obj-<host-triple>/, e.g. obj-x86_64-w64-mingw32/.
# Glob and pick the first match — simavr's build only ever produces one such dir.
file(GLOB _SIMAVR_LIB_CANDIDATES "${SIMAVR_DIR}/simavr/obj-*/libsimavr.a")
list(LENGTH _SIMAVR_LIB_CANDIDATES _SIMAVR_LIB_COUNT)

if(_SIMAVR_LIB_COUNT EQUAL 0)
    message(FATAL_ERROR
        "simavr static library not found under ${SIMAVR_DIR}/simavr/obj-*/\n"
        "Run scripts/bootstrap.sh first to build simavr."
    )
endif()

list(GET _SIMAVR_LIB_CANDIDATES 0 SIMAVR_LIB)

find_package(Threads REQUIRED)

# ----- mcp2515: pure-logic static lib (no simavr dependency) ------------
# Tests link this directly; the engine adds a small glue layer in sim_loop.c
# that bridges simavr's SPI/GPIO/INT IRQs to this library's callbacks.
add_library(mcp2515 STATIC
    src/mcp2515/mcp2515.c
)
target_include_directories(mcp2515 PUBLIC src/mcp2515)

# ----- can_bus: pure-logic multi-node CAN bus -----------------------
# Routes mcp2515_frame_t between attached controllers. Pure C, depends
# only on the mcp2515 module.
add_library(can_bus STATIC
    src/can_bus/can_bus.c
)
target_include_directories(can_bus PUBLIC src/can_bus)
target_link_libraries(can_bus PUBLIC mcp2515)

# ----- i2c_lcd: pure-logic PCF8574 + HD44780 1602 LCD model ---------
# No simavr dependency. Engine glue in sim_loop.c bridges simavr's TWI
# IRQs to this library; tests link it directly.
add_library(i2c_lcd STATIC
    src/i2c_lcd/i2c_lcd.c
)
target_include_directories(i2c_lcd PUBLIC src/i2c_lcd)

# ----- Embed Optiboot bootloader as a C string ---------------------------
# So the engine can pre-load Optiboot into every atmega328p chip on
# startup without depending on a runtime path lookup. The .hex is read
# at configure time, each line wrapped in quotes + \n, and emitted into
# a generated header. Re-run cmake to pick up changes to the .hex file.
set(OPTIBOOT_HEX_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/bootloaders/optiboot_atmega328.hex")
# NO_HEX_CONVERSION is essential: without it CMake's file(STRINGS) sees
# Intel-hex's pairs of hex digits and helpfully "converts" 24 → '$' etc,
# corrupting the bootloader image into a parade of single-byte garbage.
file(STRINGS "${OPTIBOOT_HEX_SRC}" OPTIBOOT_HEX_LINES NO_HEX_CONVERSION)
set(OPTIBOOT_C_LINES "")
foreach(_line ${OPTIBOOT_HEX_LINES})
    string(APPEND OPTIBOOT_C_LINES "    \"${_line}\\n\"\n")
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/optiboot_data.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/optiboot_data.h"
    @ONLY
)

# ----- aneb-sim: the engine binary --------------------------------------
add_executable(aneb-sim
    src/main.c
    src/proto.c
    src/chip.c
    src/cmd.c
    src/pin_names.c
    src/sim_loop.c
    src/uart_server.c
    third_party/cjson/cJSON.c
)

# SYSTEM marks third-party headers as not-our-problem so our -Wpedantic
# doesn't fire on e.g. zero-size arrays in sim_avr.h.
target_include_directories(aneb-sim SYSTEM PRIVATE
    ${SIMAVR_DIR}/simavr/sim
    third_party/cjson
)

target_include_directories(aneb-sim PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(aneb-sim PRIVATE
    mcp2515
    can_bus
    i2c_lcd
    ${SIMAVR_LIB}
    elf
    m
    Threads::Threads
)

# simavr's GDB stub uses BSD socket calls; on MinGW these resolve through
# Winsock2 (ws2_32). Link unconditionally on Windows.
if(WIN32)
    target_link_libraries(aneb-sim PRIVATE ws2_32)
endif()

# ----- test_mcp2515: register/SPI/filter/loopback unit tests ----------
# enable_testing() is called in the root CMakeLists.

add_executable(test_mcp2515
    test/test_main.c
    test/test_mcp2515_regs.c
    test/test_mcp2515_spi.c
    test/test_mcp2515_filters.c
    test/test_mcp2515_loopback.c
    test/test_mcp2515_errors.c
    test/test_can_bus.c
    test/test_pin_names.c
    test/test_i2c_lcd.c
    src/pin_names.c
)
target_include_directories(test_mcp2515 PRIVATE
    src
    src/mcp2515
    src/can_bus
    src/i2c_lcd
    test
)
target_link_libraries(test_mcp2515 PRIVATE mcp2515 can_bus i2c_lcd)

add_test(NAME unit COMMAND test_mcp2515)

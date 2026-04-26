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

add_executable(aneb-sim
    src/main.c
    src/proto.c
    src/chip.c
    src/cmd.c
    src/pin_names.c
    src/sim_loop.c
    third_party/cjson/cJSON.c
)

# SYSTEM marks third-party headers as not-our-problem so our -Wpedantic
# doesn't fire on e.g. zero-size arrays in sim_avr.h.
target_include_directories(aneb-sim SYSTEM PRIVATE
    ${SIMAVR_DIR}/simavr/sim
    third_party/cjson
)

target_include_directories(aneb-sim PRIVATE src)

target_link_libraries(aneb-sim PRIVATE
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

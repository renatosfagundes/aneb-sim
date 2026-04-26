set(SIMAVR_DIR ${CMAKE_SOURCE_DIR}/external/simavr)
set(SIMAVR_LIB ${SIMAVR_DIR}/simavr/libsimavr.a)

if(NOT EXISTS ${SIMAVR_LIB})
    message(FATAL_ERROR
        "simavr static library not found at ${SIMAVR_LIB}\n"
        "Run scripts/bootstrap.sh first to build simavr."
    )
endif()

add_executable(aneb-sim src/main.c)

target_include_directories(aneb-sim PRIVATE
    ${SIMAVR_DIR}/simavr/sim
    ${SIMAVR_DIR}/include
)

target_link_libraries(aneb-sim PRIVATE
    ${SIMAVR_LIB}
    elf
    m
)

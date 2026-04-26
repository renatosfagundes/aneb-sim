cmake_minimum_required(VERSION 3.20)
project(aneb-sim
    VERSION 0.0.1
    DESCRIPTION "ANEB v1.1 simulator engine"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

add_subdirectory(aneb-sim)

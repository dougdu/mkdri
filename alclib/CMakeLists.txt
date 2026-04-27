set(ALCLIB_SOURCES
    cputc.c
    getarhd.c
    getchd.c
    libget.c
    libput.c
    libread.c
    libwrite.c
    putarhd.c
    putchd.c
    fopen_s.c
    strcpy_s.c
    _mktemp_s.c
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(alclib C)
endif()

add_library(alclib STATIC ${ALCLIB_SOURCES})

target_include_directories(alclib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../Include
)

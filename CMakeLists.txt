# mkdri — ELF/COFF/DRI to absolute DRI converter
#
# Phase 1 lands the ELF reader plus the DRI writer plus the CLI driver.
# COFF and DRI reader inputs are stubbed for Phase 2 / Phase 3.

set(MKDRI_SOURCES
    main.cpp
    ElfReader.cpp
    CoffReader.cpp
    DriReader.cpp
    DriWriter.cpp
    DriRelWriter.cpp
    SymFile.cpp
)

set(MKDRI_HEADERS
    Image.h
    ElfReader.h
    CoffReader.h
    DriReader.h
    DriWriter.h
    DriRelWriter.h
    SymFile.h
)

# When this file is used stand-alone (not included from the parent CMakeLists.txt)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(mkdri CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_subdirectory(../alclib alclib)
endif()

add_executable(mkdri ${MKDRI_SOURCES} ${MKDRI_HEADERS})

target_include_directories(mkdri PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../asm68K
)

target_compile_definitions(mkdri PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

if(MSVC)
    target_compile_options(mkdri PRIVATE /W3)
else()
    target_compile_options(mkdri PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-missing-field-initializers
    )
endif()

target_link_libraries(mkdri PRIVATE alclib)

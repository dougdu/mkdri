cmake_minimum_required(VERSION 3.16)
project(mkdri LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(alclib)

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

add_executable(mkdri ${MKDRI_SOURCES} ${MKDRI_HEADERS})

target_include_directories(mkdri PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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

include(CTest)
if(BUILD_TESTING)
    add_executable(mkdri-et-exec-gap-test
        test/DriWriterGapTest.cpp
        DriWriter.cpp
        DriWriter.h
        Image.h
    )
    target_include_directories(mkdri-et-exec-gap-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(mkdri-et-exec-gap-test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )
    target_link_libraries(mkdri-et-exec-gap-test PRIVATE alclib)
    add_test(NAME mkdri-et-exec-gap COMMAND mkdri-et-exec-gap-test)
endif()

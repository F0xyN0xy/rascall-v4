cmake_minimum_required(VERSION 3.16)

project(ChessRL CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SOURCES
    src/main.cpp
    src/bitboard.cpp
    src/board.cpp
    src/movegen.cpp
    src/eval.cpp
    src/search.cpp
    src/uci.cpp
    src/pgn_recorder.cpp
    src/rl.cpp
    src/book.cpp
    src/gensfen.cpp
)

# Windows resources
if(WIN32)
    enable_language(RC)
    list(APPEND SOURCES src/rascall.rc)
endif()

add_executable(rascall-v4 ${SOURCES})

# Compiler-specific options
if(MSVC)
    message(STATUS "Using MSVC compiler")

    target_compile_options(rascall-v4 PRIVATE
        /W4
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Using GCC/Clang compiler")

    target_compile_options(rascall-v4 PRIVATE
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )

    target_link_options(rascall-v4 PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(rascall-v4 PRIVATE Threads::Threads)

# Link-time optimization
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)

if(ipo_ok)
    set_property(
        TARGET rascall-v4
        PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")

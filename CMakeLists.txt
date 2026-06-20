cmake_minimum_required(VERSION 3.16)
project(ChessRL CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address,undefined")

if(NOT CMAKE_BUILD_TYPE)
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
    src/rl.cpp
    src/book.cpp
    src/gensfen.cpp
)

if(WIN32)
    set(CMAKE_RC_COMPILER_INIT windres)
    enable_language(RC)
    set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <FLAGS> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
    set(SOURCES ${SOURCES} src/rascall.rc)
endif()

add_executable(rascall-v4 ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(rascall-v4 Threads::Threads)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok)
    set_property(TARGET rascall-v4 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
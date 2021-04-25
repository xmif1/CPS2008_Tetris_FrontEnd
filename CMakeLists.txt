cmake_minimum_required(VERSION 3.17)
project(CPS2008_Tetris_FrontEnd C)

set(CMAKE_C_STANDARD 99)

add_executable(CPS2008_Tetris_FrontEnd main.c tetris.c tetris.h)

find_package(CPS2008_Tetris_Client)
target_link_libraries(CPS2008_Tetris_FrontEnd pthread curses CPS2008_Tetris_Client)

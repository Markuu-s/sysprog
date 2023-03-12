cmake_minimum_required(VERSION 3.24)
project(SysProga2 C)

set(CMAKE_C_STANDARD 17)

add_subdirectory(Vector)

add_executable(SysProga2 main.c)
target_link_libraries(SysProga2 Vector)

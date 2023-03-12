cmake_minimum_required(VERSION 3.24)
project(SysProga2 C)

set(CMAKE_C_STANDARD 17)

add_subdirectory(Vector)
add_subdirectory(heap_help)

add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)

add_executable(SysProga2 main.c)
#target_link_libraries(SysProga2 Vector HEAP_CHECK)
target_link_libraries(SysProga2 Vector )

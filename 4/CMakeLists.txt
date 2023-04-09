cmake_minimum_required(VERSION 3.24)
project(Sysproga4 C)

set(CMAKE_C_STANDARD 17)

add_library(thread_pool thread_pool.c)
add_library(san utils/heap_help/heap_help.c)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)
#
#add_compile_options(-fsanitize=thread)
#add_link_options(-fsanitize=thread)

add_executable(Sysproga4 test.c)
target_link_libraries(Sysproga4 thread_pool san)
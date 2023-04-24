cmake_minimum_required(VERSION 3.20)
project(SysProga5)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wpedantic -Werror -Wall -Wno-gnu-folding-constant")

add_library(chat chat.c)
add_library(chat_client chat_client.c)
add_library(chat_server chat_server.c)

add_executable(start_client chat_client_exe.c)
target_link_libraries(start_client chat_client chat)

add_executable(start_server chat_server_exe.c)
target_link_libraries(start_server chat_server chat)
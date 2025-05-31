cmake_minimum_required(VERSION 3.10)

project(echo_server_simple)

set (CMAKE_CXX_STANDARD 17)

add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)

add_executable(echo_server_simple main.cpp)

if (MINGW)
    target_link_libraries(echo_server_simple ws2_32)
endif()
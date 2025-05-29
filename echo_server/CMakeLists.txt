cmake_minimum_required(VERSION 3.10)

project(echo_server)

set (CMAKE_CXX_STANDARD 17)

add_executable(echo_server main.cpp)

if (MINGW)
    target_link_libraries(echo_server ws2_32)
endif()
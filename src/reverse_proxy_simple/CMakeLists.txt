cmake_minimum_required(VERSION 3.10)

project(reverse_proxy_simple)

set (CMAKE_CXX_STANDARD 17)

add_executable(reverse_proxy_simple main.cpp)

if (MINGW)
    target_link_libraries(reverse_proxy_simple ws2_32)
endif()
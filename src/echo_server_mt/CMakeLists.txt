cmake_minimum_required(VERSION 3.10)

project(echo_server_mt)

set (CMAKE_CXX_STANDARD 17)

set (SOURCES main.cpp)
set (HEADERS SafeQueue.hpp)

add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)

add_executable(echo_server_mt ${SOURCES} ${HEADERS})

if (MINGW)
    target_link_libraries(echo_server_mt ws2_32)
endif()
cmake_minimum_required(VERSION 3.10)

project(reverse_proxy_mt)

set (CMAKE_CXX_STANDARD 17)

set (SOURCES main.cpp)
set (HEADERS SafeQueue.hpp)

add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)

add_executable(reverse_proxy_mt ${SOURCES} ${HEADERS})

if (MINGW)
    target_link_libraries(reverse_proxy_mt ws2_32)
endif()
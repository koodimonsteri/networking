cmake_minimum_required(VERSION 3.10)

project(reverse_proxy_async)

set (CMAKE_CXX_STANDARD 17)

set (SOURCES main.cpp)

#add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)


add_executable(reverse_proxy_async ${SOURCES})

if (MINGW)
    target_link_libraries(reverse_proxy_async ws2_32)
endif()

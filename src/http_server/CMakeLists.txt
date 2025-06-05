cmake_minimum_required(VERSION 3.10)

project(http_server)

set (CMAKE_CXX_STANDARD 17)

set (SOURCES main.cpp HTTPServer.cpp)
set (HEADERS log.hpp HTTPServer.hpp HTTPParser.hpp)

#add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)
add_compile_options(-Wall -Werror -Wconversion -Wshadow -pedantic)

add_executable(http_server ${SOURCES} ${HEADERS})

if (MINGW)
    target_link_libraries(http_server ws2_32)
endif()
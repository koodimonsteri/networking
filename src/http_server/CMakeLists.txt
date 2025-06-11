cmake_minimum_required(VERSION 3.10)

project(http_server)

set (CMAKE_CXX_STANDARD 17)

file(GLOB CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")
file(GLOB ROUTERS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/routers/*.cpp")

set(SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${ROUTERS_SOURCES}
)

#add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow -pedantic)
add_compile_options(-Wall -Werror -Wconversion -Wshadow -pedantic)

add_executable(http_server ${SOURCES})

target_include_directories(http_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/routers
)

if (MINGW)
    target_link_libraries(http_server ws2_32)
endif()
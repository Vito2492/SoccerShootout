cmake_minimum_required(VERSION 3.22.1)
project(soccer_shootout C)

if(NOT DEFINED SDL2_DIR)
    set(SDL2_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../vendor/SDL2")
endif()

add_subdirectory("${SDL2_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL2")

add_library(main SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../../../../src/soccer.c"
)

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../src"
)

target_link_libraries(main SDL2 dl log m)

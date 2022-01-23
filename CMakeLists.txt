project(utask)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_FGLAGS "-Wall -Wextra -pedantic -std=c99")

#add_library(utask STATIC "utask.c")
add_executable(example1 "example1.c")
#target_link_libraries(example1 utask)
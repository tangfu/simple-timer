cmake_minimum_required(VERSION 2.8)
project(simpletimer-test)

include_directories("/usr/include/google")
find_package(simpletimer REQUIRED)
add_definitions("-g -Wall")

add_executable(test test.c)
target_link_libraries(test rt cmockery pthread simpletimer)

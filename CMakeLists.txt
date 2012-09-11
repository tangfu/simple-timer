project(simple-timer)
cmake_minimum_required(VERSION 2.8.4)


add_definitions("-g -Wall")
add_subdirectory(src)


export(PACKAGE simpletimer)
export(TARGETS simpletimer FILE simpletimer-exports.cmake)
configure_file(simpletimer-config.cmake.in ${CMAKE_BINARY_DIR}/simpletimer-config.cmake)

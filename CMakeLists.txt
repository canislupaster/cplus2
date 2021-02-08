cmake_minimum_required(VERSION 3.17)
project(cplus2 C)

set(CMAKE_C_STANDARD 11)

add_subdirectory(corecommon)
include_directories(./corecommon/src)

add_executable(cplus2 src/main.c src/parse.c src/syntax.c src/emit.c)

add_custom_target(genheader_cplus WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} COMMAND headergen ${CMAKE_CURRENT_SOURCE_DIR}/src --pub)
add_dependencies(cplus2 genheader_cplus corecommon)
target_link_libraries(cplus2 corecommon)

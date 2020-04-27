cmake_minimum_required(VERSION 3.8)
project(ResourceSystem)

if (NOT TARGET clean-core)
    message(FATAL_ERROR "[resource-system] clean-core must be available")
endif()

# =========================================
# define library

file(GLOB_RECURSE SOURCES "src/*.cc")
file(GLOB_RECURSE HEADERS "src/*.hh")

arcana_add_library(RES resource-system SOURCES HEADERS)

target_include_directories(resource-system PUBLIC src/)

target_link_libraries(resource-system PUBLIC
    clean-core
)

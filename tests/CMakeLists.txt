cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(tests-resource-system "${SOURCES}")

target_link_libraries(tests-resource-system PUBLIC
    clean-core
    clean-ranges
    typed-geometry
    rich-log
    ctracer

    glow-extras # for demos

    resource-system
)

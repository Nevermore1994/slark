cmake_minimum_required(VERSION 3.20)

add_subdirectory(base)
add_subdirectory(core)

if(NOT DISABLE_HTTP)
    add_subdirectory(http)
endif()

get_property(LIBRARY_TYPE GLOBAL PROPERTY "LIBRARY_TYPE")

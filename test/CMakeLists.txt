cmake_minimum_required(VERSION 3.20)

add_subdirectory(base_test)
if(NOT DISABLE_HTTP)
    add_subdirectory(http_test)
endif ()

get_property(LIBRARY_TYPE GLOBAL PROPERTY "LIBRARY_TYPE" )


cmake_minimum_required(VERSION 3.20)

# Build-time discovery runs each freshly linked binary and times out under parallel builds.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

add_subdirectory(base)
add_subdirectory(core)

if(NOT DISABLE_CORE)
    add_subdirectory(engine)
endif()

if(NOT DISABLE_HTTP)
    add_subdirectory(http)
endif()

add_subdirectory(platform)
add_subdirectory(fuzz)

get_property(LIBRARY_TYPE GLOBAL PROPERTY "LIBRARY_TYPE")

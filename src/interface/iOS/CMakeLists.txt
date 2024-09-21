cmake_minimum_required(VERSION 3.20)

set(FILE_LISTS *.c *.cpp *.hpp *.h *.mm *.m)

FILE(GLOB INTERFACE_FILES ${FILE_LISTS})
message(STATUS ${INTERFACE_FILES})
add_library(iOS_INTERFACE ${INTERFACE_FILES})
target_include_directories(iOS_INTERFACE PUBLIC .)
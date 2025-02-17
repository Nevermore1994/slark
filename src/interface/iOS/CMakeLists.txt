cmake_minimum_required(VERSION 3.20)

project(iOS_INTERFACE)
set(FILE_LISTS *.c *.cpp *.hpp *.h *.mm *.m)

FILE(GLOB INTERFACE_FILES ${FILE_LISTS})
message(STATUS ${INTERFACE_FILES})
add_library(${PROJECT_NAME} ${INTERFACE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC .)
target_include_directories(${PROJECT_NAME} PUBLIC ../../core/public)
target_include_directories(${PROJECT_NAME} PUBLIC ../../core/platform/iOS)
target_include_directories(${PROJECT_NAME} PUBLIC ../../base)
target_include_directories(${PROJECT_NAME} PUBLIC ../../platform/iOS)
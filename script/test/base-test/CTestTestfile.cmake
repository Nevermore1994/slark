# CMake generated Testfile for 
# Source directory: /Users/rolf.tan/selfspace/slark/test/base-test
# Build directory: /Users/rolf.tan/selfspace/slark/script/test/base-test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[base]=] "/Users/rolf.tan/selfspace/slark/bin/Debug/base-test")
  set_tests_properties([=[base]=] PROPERTIES  LABELS "lib;base" _BACKTRACE_TRIPLES "/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;10;add_test;/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[base]=] "/Users/rolf.tan/selfspace/slark/bin/Release/base-test")
  set_tests_properties([=[base]=] PROPERTIES  LABELS "lib;base" _BACKTRACE_TRIPLES "/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;10;add_test;/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[base]=] "/Users/rolf.tan/selfspace/slark/bin/MinSizeRel/base-test")
  set_tests_properties([=[base]=] PROPERTIES  LABELS "lib;base" _BACKTRACE_TRIPLES "/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;10;add_test;/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[base]=] "/Users/rolf.tan/selfspace/slark/bin/RelWithDebInfo/base-test")
  set_tests_properties([=[base]=] PROPERTIES  LABELS "lib;base" _BACKTRACE_TRIPLES "/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;10;add_test;/Users/rolf.tan/selfspace/slark/test/base-test/CMakeLists.txt;0;")
else()
  add_test([=[base]=] NOT_AVAILABLE)
endif()

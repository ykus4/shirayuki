cmake_minimum_required(VERSION 3.16)
project(ShirayukiCoreTests CXX)

# Host-native test harness for the pure C++ core.
#
# Theos targets arm64 iOS and cannot run tests. The Mach VM APIs the core
# relies on (vm_read_overwrite, vm_region_recurse_64, _dyld_*) are all present
# on macOS, so the core builds and runs here against this process's own memory.
# This is the only automated verification the core has.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ShirayukiMemory)

file(GLOB CORE_SOURCES ${CORE_DIR}/*.cpp)

add_library(shirayuki_core STATIC ${CORE_SOURCES})
target_include_directories(shirayuki_core PUBLIC ${CORE_DIR})
target_compile_options(shirayuki_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

enable_testing()

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} shirayuki_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
    # These tests only touch this process's own memory and should finish in
    # milliseconds. A timeout keeps a runaway loop or an unbounded allocation
    # from wedging CI instead of failing.
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()

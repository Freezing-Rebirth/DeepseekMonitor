# Host test target for the pure logic.
#
# Nothing here needs ESP-IDF, a board, or the network: these files are compiled by the
# workstation compiler and run with ctest. Everything that touches hardware stays in
# firmware/main and is not linked here.
#
#   cmake -S firmware/test -B firmware/test/build-tests
#   cmake --build firmware/test/build-tests
#   ctest --test-dir firmware/test/build-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(deepseek_monitor_tests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

# Add one executable per suite: a failure names the suite, and they run in parallel.
function(add_suite name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                               ${CMAKE_CURRENT_SOURCE_DIR}/../main)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_suite(test_pricing_rules ../main/pricing_rules.cpp)

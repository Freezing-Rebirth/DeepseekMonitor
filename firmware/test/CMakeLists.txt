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
add_suite(test_battery_curve ../main/battery_curve.cpp)
add_suite(test_ledger_math ../main/ledger_math.cpp)

# The balance parser is the first suite that needs a third-party library. cJSON comes from
# the component manager, so `idf.py build` must have run once in firmware/ to fetch it; the
# host build compiles the same sources the firmware does.
set(CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__cjson/cJSON)
if(EXISTS ${CJSON_DIR}/cJSON.c)
    add_executable(test_balance_json test_balance_json.cpp
                   ../main/balance_json.cpp ${CJSON_DIR}/cJSON.c)
    target_include_directories(test_balance_json PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                         ${CMAKE_CURRENT_SOURCE_DIR}/../main
                                                         ${CJSON_DIR})
    add_test(NAME test_balance_json COMMAND test_balance_json)

    # The holiday parsers need cJSON for the same reason.
    add_executable(test_holidays_parse test_holidays_parse.cpp
                   ../main/holidays_parse.cpp ${CJSON_DIR}/cJSON.c)
    target_include_directories(test_holidays_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                          ${CMAKE_CURRENT_SOURCE_DIR}/../main
                                                          ${CJSON_DIR})
    add_test(NAME test_holidays_parse COMMAND test_holidays_parse)
else()
    message(WARNING "cJSON not fetched yet: run `idf.py build` in firmware/ to enable test_balance_json")
endif()

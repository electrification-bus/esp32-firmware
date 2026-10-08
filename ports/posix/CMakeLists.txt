# The POSIX (Linux, macOS) port of the eBus core: a Paho MQTT transport, the clock and log
# bindings, two demo programs and their end-to-end tests. See README.md.
cmake_minimum_required(VERSION 3.18)
project(ebus_posix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EBUS_POSIX_WERROR "Treat warnings in the port, the demos and the tests as errors" OFF)
option(EBUS_POSIX_TESTS "Build the tests" ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ebus_core ebus_core)

find_package(Threads REQUIRED)

# Eclipse Paho MQTT C. `auto` uses an installed package (its CMake config) when there is
# one and otherwise fetches the pinned release; `system` and `fetch` force either.
set(EBUS_POSIX_PAHO "auto" CACHE STRING "Paho MQTT C: auto, system or fetch")
set_property(CACHE EBUS_POSIX_PAHO PROPERTY STRINGS auto system fetch)
set(_paho_target "")
if(NOT EBUS_POSIX_PAHO STREQUAL "fetch")
    find_package(eclipse-paho-mqtt-c CONFIG QUIET)
    if(TARGET eclipse-paho-mqtt-c::paho-mqtt3c)
        set(_paho_target eclipse-paho-mqtt-c::paho-mqtt3c)
    elseif(TARGET eclipse-paho-mqtt-c::paho-mqtt3c-static)
        set(_paho_target eclipse-paho-mqtt-c::paho-mqtt3c-static)
    elseif(EBUS_POSIX_PAHO STREQUAL "system")
        message(FATAL_ERROR "EBUS_POSIX_PAHO=system but no eclipse-paho-mqtt-c package found")
    endif()
endif()
if(_paho_target)
    message(STATUS "Paho MQTT C: system package ${eclipse-paho-mqtt-c_VERSION}")
else()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    # A static, plain-TCP client: no OpenSSL, no install step, no Paho tests.
    set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
    set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(PAHO_ENABLE_CPACK OFF CACHE BOOL "" FORCE)
    set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(PAHO_HIGH_PERFORMANCE ON CACHE BOOL "" FORCE)   # no tracing, no heap tracking
    FetchContent_Declare(paho_mqtt_c
        URL https://github.com/eclipse-paho/paho.mqtt.c/archive/refs/tags/v1.3.16.tar.gz
        URL_HASH SHA256=8b960f51edc7e03507637d987882bc486d8f4be6e79431bf99e2763344fd14c5
    )
    FetchContent_MakeAvailable(paho_mqtt_c)
    set(_paho_target paho-mqtt3c-static)
    message(STATUS "Paho MQTT C: fetched v1.3.16")
endif()

function(ebus_posix_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    if(EBUS_POSIX_WERROR)
        set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    endif()
endfunction()

add_library(ebus_posix STATIC
    src/paho_transport.cpp
    src/posix_port.cpp
    src/publish_hold.cpp
)
target_include_directories(ebus_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ebus_posix PUBLIC ebus_core Threads::Threads PRIVATE ${_paho_target})
ebus_posix_warnings(ebus_posix)

add_executable(ebus-posix-device apps/device_main.cpp)
target_link_libraries(ebus-posix-device PRIVATE ebus_posix)
ebus_posix_warnings(ebus-posix-device)

add_executable(ebus-posix-controller apps/controller_main.cpp)
target_link_libraries(ebus-posix-controller PRIVATE ebus_posix)
ebus_posix_warnings(ebus-posix-controller)

if(EBUS_POSIX_TESTS)
    enable_testing()

    add_executable(test_publish_hold test/test_publish_hold.cpp)
    target_link_libraries(test_publish_hold PRIVATE ebus_posix)
    ebus_posix_warnings(test_publish_hold)
    add_test(NAME publish_hold COMMAND test_publish_hold)

    # The end-to-end tests start their own mosquitto on a free localhost port.
    find_program(EBUS_POSIX_MOSQUITTO mosquitto
                 PATHS /opt/homebrew/sbin /usr/local/sbin /usr/sbin /sbin)
    add_executable(ebus_posix_e2e test/e2e.cpp)
    target_link_libraries(ebus_posix_e2e PRIVATE ebus_posix ${_paho_target})
    ebus_posix_warnings(ebus_posix_e2e)
    if(EBUS_POSIX_MOSQUITTO)
        foreach(_case boot set invalid will reconnect controller)
            add_test(NAME e2e_${_case}
                     COMMAND ebus_posix_e2e ${_case}
                             --mosquitto ${EBUS_POSIX_MOSQUITTO}
                             --device $<TARGET_FILE:ebus-posix-device>
                             --controller $<TARGET_FILE:ebus-posix-controller>
                             --workdir ${CMAKE_CURRENT_BINARY_DIR}/e2e)
            set_tests_properties(e2e_${_case} PROPERTIES TIMEOUT 60)
        endforeach()
    else()
        message(WARNING "mosquitto not found: the end-to-end tests are not registered")
    endif()
endif()

# Builds the core without PlatformIO, for Zephyr, embedded Linux, or a host check.
# Usage from a parent project: add_subdirectory(path/to/ebus_core) and link ebus_core.
cmake_minimum_required(VERSION 3.18)
project(ebus_core LANGUAGES CXX)

add_library(ebus_core STATIC
    src/homie/controller.cpp
    src/homie/controller_inbox.cpp
    src/homie/Device.cpp
    src/homie/homie_clock.cpp
    src/homie/homie_datatype.cpp
    src/homie/homie_id.cpp
    src/homie/homie_json.cpp
    src/homie/homie_log.cpp
    src/homie/homie_transport.cpp
    src/homie/Node.cpp
    src/homie/Property.cpp
    src/platform/broker_discovery.cpp
    src/util/jsonUtils.cpp
)
target_include_directories(ebus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ebus_core PUBLIC cxx_std_17)

# ArduinoJson, the core's one dependency (header-only). A parent project that already has
# an ArduinoJson target keeps it. Otherwise the release PlatformIO uses (library.json,
# platformio.ini) is fetched, pinned by hash, so both builds serialize identically; to
# build offline, point -DFETCHCONTENT_SOURCE_DIR_ARDUINOJSON=<dir> at a checkout of it.
if(NOT TARGET ArduinoJson)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)   # extracted files get the extraction time
    endif()
    include(FetchContent)
    FetchContent_Declare(ArduinoJson
        URL https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v7.4.3.tar.gz
        URL_HASH SHA256=a2a8a277fc3cd0404499199bf1468682ea3688f354be0eaf6af8648366911257
    )
    FetchContent_MakeAvailable(ArduinoJson)
endif()
target_link_libraries(ebus_core PUBLIC ArduinoJson)

# Topic root: homie/5 by default, ebus/5 with USE_EBUS_TOPIC (include/homie/homie.h).
# The ESP32 firmware sets it in platformio.ini's build_flags.
option(EBUS_CORE_EBUS_TOPIC "Define USE_EBUS_TOPIC (publish under ebus/5)" OFF)
if(EBUS_CORE_EBUS_TOPIC)
    target_compile_definitions(ebus_core PUBLIC USE_EBUS_TOPIC)
endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_ebus_core_top_level ON)
else()
    set(_ebus_core_top_level OFF)
endif()

if(_ebus_core_top_level AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ebus_core PRIVATE -Wall -Wextra)
endif()

# Compiles every public header in a translation unit of its own, so a header that needs
# something it does not include (or includes a platform header) fails here.
option(EBUS_CORE_CHECK_HEADERS "Compile each public header on its own" ${_ebus_core_top_level})
if(EBUS_CORE_CHECK_HEADERS)
    file(GLOB_RECURSE _ebus_core_headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/include
         CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
    set(_ebus_core_header_tus "")
    foreach(_header IN LISTS _ebus_core_headers)
        string(MAKE_C_IDENTIFIER ${_header} _tu_name)
        set(_tu ${CMAKE_CURRENT_BINARY_DIR}/header_check/${_tu_name}.cpp)
        file(CONFIGURE OUTPUT ${_tu} CONTENT "#include <${_header}>\n")
        list(APPEND _ebus_core_header_tus ${_tu})
    endforeach()
    add_library(ebus_core_header_check OBJECT ${_ebus_core_header_tus})
    target_link_libraries(ebus_core_header_check PRIVATE ebus_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ebus_core_header_check PRIVATE -Wall -Wextra)
    endif()
endif()

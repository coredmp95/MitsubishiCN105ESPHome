## CMakeLists.txt — host harness: the real ESPHome 2026.10.0 core, built per platform variant
## Role: provides function cn105_harness_variant(VARIANT) creating, per variant <v> in {esp32, esp8266}:
##         harness_flags_<v> (INTERFACE: defines, include order, force-include),
##         esphome_<v>       (STATIC: the ESPHome core/component sources the harness needs).
##       Nothing in the fetched ESPHome tree is patched; the HAL is replaced at link level only.
## Deps: CMake >= 3.14, git (FetchContent), a 32-bit toolchain (g++-multilib, see tests/unit/docker/run.sh)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The characterization harness must be built with -m32 (D-20): unsigned long has to be "
                        "4 bytes as on ESP32/ESP8266. Configure with -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32, "
                        "or run through tests/unit/docker/run.sh.")
endif()

# ESPHome sources use C++20 (std::span, concepts) with GNU extensions; the pure tests stay on C++17.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# ── ESPHome sources, pinned to the version CI `stable` uses (D-02) ──
set(CN105_ESPHOME_TAG    "2026.10.0")
set(CN105_ESPHOME_COMMIT "704551a6c93c29a311802f7ee97d67a9e9bce718")

include(FetchContent)
FetchContent_Declare(
    esphome
    GIT_REPOSITORY https://github.com/esphome/esphome.git
    GIT_TAG        2026.10.0
    GIT_SHALLOW    TRUE
)
# The ESPHome repository has no top-level CMakeLists.txt: this only populates the source tree.
FetchContent_MakeAvailable(esphome)

# Supply-chain check (T-01-01): the immutable release tag must resolve to the audited commit.
if(EXISTS "${esphome_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git -C "${esphome_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE CN105_ESPHOME_HEAD
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CN105_ESPHOME_HEAD_RC)
    if(NOT CN105_ESPHOME_HEAD_RC EQUAL 0 OR NOT CN105_ESPHOME_HEAD STREQUAL CN105_ESPHOME_COMMIT)
        message(FATAL_ERROR "ESPHome ${CN105_ESPHOME_TAG} resolved to '${CN105_ESPHOME_HEAD}', expected "
                            "${CN105_ESPHOME_COMMIT}. Refusing to build against an unaudited tree.")
    endif()
else()
    message(WARNING "ESPHome source dir ${esphome_SOURCE_DIR} is not a git checkout (offline "
                    "FETCHCONTENT_SOURCE_DIR_ESPHOME override?): commit ${CN105_ESPHOME_COMMIT} was NOT verified.")
endif()

set(CN105_HARNESS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ESPHome sources compiled in every variant. core/log.cpp, core/static_task.cpp, core/color.cpp,
# core/component_iterator.cpp and every platform .cpp (components/esp32/*, esp8266/*, host/*) are left
# out on purpose: the test platform stubs are then the only definitions of the HAL time symbols,
# the logger and the preferences (D-05).
set(CN105_ESPHOME_COMMON_SOURCES
    core/alloc_helpers.cpp core/application.cpp core/component.cpp core/entity_base.cpp
    core/helpers.cpp core/scheduler.cpp core/time_64.cpp core/util.cpp core/gpio.cpp core/time.cpp
    components/climate/climate.cpp components/climate/climate_mode.cpp components/climate/climate_traits.cpp
    components/sensor/sensor.cpp components/sensor/filter.cpp
    components/binary_sensor/binary_sensor.cpp components/binary_sensor/filter.cpp
    components/text_sensor/text_sensor.cpp components/text_sensor/filter.cpp
    components/select/select.cpp components/select/select_call.cpp components/select/select_traits.cpp
    components/switch/switch.cpp
    components/number/number.cpp components/number/number_call.cpp components/number/number_traits.cpp
    components/button/button.cpp
    components/uart/uart_component.cpp components/uart/uart.cpp)

# cn105_harness_variant(<esp32|esp8266>)
# Never mix -D defines per translation unit: the ESPHome library, the cn105 objects and the tests of
# one variant are compiled with identical flags (UARTComponent::load_settings, Mutex and hal.h differ).
function(cn105_harness_variant VARIANT)
    string(TOUPPER "${VARIANT}" VARIANT_UPPER)

    add_library(harness_flags_${VARIANT} INTERFACE)
    target_compile_definitions(harness_flags_${VARIANT} INTERFACE USE_${VARIANT_UPPER})
    target_include_directories(harness_flags_${VARIANT} BEFORE INTERFACE
        "${CN105_HARNESS_DIR}/overlay"
        "${CN105_HARNESS_DIR}/shim_${VARIANT}"
        "${CN105_HARNESS_DIR}/platform"
        "${esphome_SOURCE_DIR}"
        "${CN105_SRC_DIR}")
    # Like ESPHome's own precompiled header: without the force-include, climate_mode.h -> log.h would
    # define ESPHOME_LOG_LEVEL as NONE before the overlay defines.h is seen.
    target_compile_options(harness_flags_${VARIANT} INTERFACE
        -include esphome/core/defines.h -O0 -g)

    if(VARIANT STREQUAL "esp32")
        set(WAKE_SOURCE core/wake/wake_freertos.cpp)
    elseif(VARIANT STREQUAL "esp8266")
        set(WAKE_SOURCE core/wake/wake_esp8266.cpp)
    else()
        message(FATAL_ERROR "Unknown harness variant '${VARIANT}'")
    endif()

    set(SOURCES "")
    foreach(src ${CN105_ESPHOME_COMMON_SOURCES} ${WAKE_SOURCE})
        list(APPEND SOURCES "${esphome_SOURCE_DIR}/esphome/${src}")
    endforeach()
    add_library(esphome_${VARIANT} STATIC ${SOURCES})
    target_link_libraries(esphome_${VARIANT} PUBLIC harness_flags_${VARIANT})
endfunction()

cn105_harness_variant(esp32)

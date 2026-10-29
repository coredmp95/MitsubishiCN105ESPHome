## CMakeLists.txt — host harness: the real ESPHome 2026.10.0 core, built per platform variant
## Role: provides function cn105_harness_variant(VARIANT) creating, per variant <v> in {esp32, esp8266}:
##         harness_flags_<v>    (INTERFACE: defines, include order, force-include),
##         esphome_<v>          (STATIC: the ESPHome core/component sources the harness needs),
##         harness_platform_<v> (STATIC: test clock, log capture, preferences, Wi-Fi, HAL stubs),
##         cn105_<v>            (OBJECT: every components/cn105/*.cpp, compiled unmodified),
##         cn105_headers_<v>    (OBJECT, only with CN105_COVERAGE=ON: one generated coverage-only
##                               translation unit per components/cn105 header, never linked).
##       Each instantiated variant is appended to the global property CN105_HARNESS_VARIANTS, which
##       tests/unit/characterization/CMakeLists.txt iterates over.
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

    # Test platform: the clock, logger, preferences and Wi-Fi stubs plus the variant HAL stubs. These
    # are the only definitions of those symbols (esphome_<v> compiles no platform .cpp).
    add_library(harness_platform_${VARIANT} STATIC
        "${CN105_HARNESS_DIR}/platform/test_platform.cpp"
        "${CN105_HARNESS_DIR}/platform/stubs_${VARIANT}.cpp")
    target_link_libraries(harness_platform_${VARIANT} PUBLIC harness_flags_${VARIANT})

    # The unmodified production sources (D-03, D-10): the glob picks up every present and future TU.
    file(GLOB CN105_SOURCES CONFIGURE_DEPENDS "${CN105_SRC_DIR}/*.cpp")
    add_library(cn105_${VARIANT} OBJECT ${CN105_SOURCES})
    target_link_libraries(cn105_${VARIANT} PUBLIC harness_flags_${VARIANT})

    if(CN105_COVERAGE)
        # Only the production objects are instrumented here; the test objects and the executable get
        # --coverage in characterization/CMakeLists.txt. esphome_<v>, harness_platform_<v> and
        # GoogleTest are never instrumented.
        target_compile_options(cn105_${VARIANT} PRIVATE --coverage)

        # ── Coverage-only header translation units (D-10) ──
        # Why: GCC emits an inline function (including a member function defined in a class body) only
        # in a translation unit that uses it. A header whose code no test calls would leave no gcov
        # record and silently drop out of the report, and diff coverage would then pass on it without
        # data. One generated TU per header (cn105.h first, then the header itself) compiled with
        # --coverage -fkeep-inline-functions makes GCC emit every non-template inline function of the
        # header, so its lines exist in a .gcno and count as 0% until a test executes them (gcovr reads
        # a .gcno without .gcda as 0%, and merges counts per file and line with the TUs tests run).
        # These objects are compiled only for their .gcno: they are never linked into an executable and
        # no test references them, because forced emission also emits ESPHome and libstdc++ inline
        # functions that may call symbols the harness does not define.
        # Defined limits: template code produces lines only where a shipped .cpp or a test instantiates
        # it (an uninstantiated template has no object code in the firmware either), and macro bodies
        # are attributed to their expansion site.
        # No -fno-access-control: this is production code (D-21). Nothing is added to components/cn105
        # (D-03): the generated files live in the build tree. file(GENERATE) rewrites a file only when
        # its content changed and keeps the 3.14 minimum of this project.
        file(GLOB CN105_HEADERS CONFIGURE_DEPENDS "${CN105_SRC_DIR}/*.h")
        set(CN105_HEADER_TUS "")
        foreach(header ${CN105_HEADERS})
            get_filename_component(header_name "${header}" NAME)
            set(header_tu "${CMAKE_CURRENT_BINARY_DIR}/header_tu_${VARIANT}/${header_name}.cpp")
            file(GENERATE OUTPUT "${header_tu}" CONTENT
"// generated coverage-only header TU (D-10): compiled for its .gcno, never linked
#include \"cn105.h\"
#include \"${header_name}\"
")
            list(APPEND CN105_HEADER_TUS "${header_tu}")
        endforeach()
        add_library(cn105_headers_${VARIANT} OBJECT ${CN105_HEADER_TUS})
        target_link_libraries(cn105_headers_${VARIANT} PUBLIC harness_flags_${VARIANT})
        target_compile_options(cn105_headers_${VARIANT} PRIVATE --coverage -fkeep-inline-functions)
    endif()

    set_property(GLOBAL APPEND PROPERTY CN105_HARNESS_VARIANTS ${VARIANT})
endfunction()

cn105_harness_variant(esp32)

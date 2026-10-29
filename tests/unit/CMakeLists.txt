## CMakeLists.txt — Tests unitaires CN105 (standalone, sans dépendance ESPHome)
## Role: Build system pour les tests de non-régression sur les fonctions pures du protocole CN105.
## Deps: CMake >= 3.14 (GoogleTest est téléchargé automatiquement via FetchContent)

cmake_minimum_required(VERSION 3.14)
project(cn105_unit_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── GoogleTest via FetchContent (portable: fonctionne en CI sans apt install) ──
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Source du composant (chemin relatif vers components/cn105)
set(CN105_SRC_DIR "${CMAKE_SOURCE_DIR}/../../components/cn105")

# Stubs ESPHome and component headers: attached to the pure-test targets only. They are not
# directory-scoped because the characterization harness (add_subdirectory below) builds against the
# real ESPHome headers, which mocks/ would shadow (version.h, climate_mode.h).

# --- Test executable ---
add_executable(cn105_tests
    "${CN105_SRC_DIR}/heatpump_functions_protocol.cpp"
    test_checksum.cpp
    test_lookup_tables.cpp
    test_temperature.cpp
    test_types.cpp
    test_calculate_temp.cpp
    test_real_frames.cpp
    test_lossnay.cpp
    test_profiles.cpp
    test_frame_parser.cpp
    test_protocol.cpp
    test_localization.cpp
    test_dual_setpoint_recenter.cpp
    test_setpoint_persistence.cpp
    test_mode_preservation.cpp
    test_vane_setpoint_persistence.cpp
    test_climate_action.cpp
)

target_include_directories(cn105_tests PRIVATE ${CMAKE_SOURCE_DIR}/mocks ${CN105_SRC_DIR})

target_link_libraries(cn105_tests
    GTest::gtest
    GTest::gtest_main
)

# Discovery des tests pour ctest
enable_testing()
include(GoogleTest)
gtest_discover_tests(cn105_tests)

# Compile the same production mapping without the newer DEFROSTING enum.
add_executable(cn105_legacy_action_tests test_climate_action.cpp)
target_include_directories(cn105_legacy_action_tests PRIVATE ${CMAKE_SOURCE_DIR}/mocks ${CN105_SRC_DIR})
target_compile_definitions(cn105_legacy_action_tests PRIVATE "ESPHOME_VERSION_CODE=VERSION_CODE(2026,2,0)")
target_link_libraries(cn105_legacy_action_tests GTest::gtest_main)
gtest_discover_tests(cn105_legacy_action_tests TEST_PREFIX "LegacyESPHome.")

# ── Characterization harness (real ESPHome core, unmodified components/cn105, -m32) ──
# ON by default only in a 32-bit build (D-20: unsigned long must be 4 bytes as on ESP32/ESP8266),
# so the 64-bit command `cmake -B tests/unit/build -S tests/unit` still builds the pure tests only.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CN105_HARNESS_DEFAULT ON)
else()
    set(CN105_HARNESS_DEFAULT OFF)
endif()
option(CN105_HARNESS "Build the ESPHome-core characterization harness (needs a 32-bit build)" ${CN105_HARNESS_DEFAULT})
if(CN105_HARNESS)
    add_subdirectory(harness)
endif()

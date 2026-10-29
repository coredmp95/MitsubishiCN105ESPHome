## CMakeLists.txt — characterization test executables, one per harness variant
## Role: builds cn105_char_<v> from fixture.cpp, every test_*.cpp in this directory and the
##       unmodified cn105 objects of the variant; registers the tests with ctest (prefix "Esp32." / "Esp8266.").
##       Later plans only add test_*.cpp files here.
## Deps: tests/unit/harness/CMakeLists.txt (variant targets), GoogleTest (from the parent project)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

file(GLOB CN105_CHAR_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

include(GoogleTest)
get_property(CN105_VARIANTS GLOBAL PROPERTY CN105_HARNESS_VARIANTS)

foreach(VARIANT ${CN105_VARIANTS})
    add_executable(cn105_char_${VARIANT}
        fixture.cpp
        ${CN105_CHAR_TESTS}
        $<TARGET_OBJECTS:cn105_${VARIANT}>)

    # The test translation units reach CN105Climate's private members with -fno-access-control (D-21).
    # The cn105 objects come from the OBJECT library, compiled without it: components/cn105 is untouched.
    target_compile_options(cn105_char_${VARIANT} PRIVATE -fno-access-control)

    # gcov: the test and fixture TUs too, because header-only inline code executed from a test is
    # recorded only in the TU that instantiates it. Never esphome_<v>, harness_platform_<v> or GoogleTest.
    if(CN105_COVERAGE)
        target_compile_options(cn105_char_${VARIANT} PRIVATE --coverage)
        target_link_options(cn105_char_${VARIANT} PRIVATE --coverage)
    endif()

    # Static-library order: the platform stubs resolve what esphome_<v> leaves undefined and vice versa,
    # hence the platform library appears on both sides of it.
    target_link_libraries(cn105_char_${VARIANT} PRIVATE
        harness_flags_${VARIANT}
        harness_platform_${VARIANT}
        esphome_${VARIANT}
        harness_platform_${VARIANT}
        GTest::gtest_main)

    string(SUBSTRING "${VARIANT}" 0 1 VARIANT_FIRST)
    string(TOUPPER "${VARIANT_FIRST}" VARIANT_FIRST)
    string(SUBSTRING "${VARIANT}" 1 -1 VARIANT_REST)
    gtest_discover_tests(cn105_char_${VARIANT}
        TEST_PREFIX "${VARIANT_FIRST}${VARIANT_REST}."
        DISCOVERY_TIMEOUT 60)
endforeach()

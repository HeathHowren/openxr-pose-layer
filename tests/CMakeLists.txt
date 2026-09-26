include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Catch2)

# The test kit: the fake next layer, the frame harness and the sample builder,
# plus the layer objects, so tests drive the real layer with no runtime.
add_library(poselayer_testkit STATIC
    Support.cpp
    $<TARGET_OBJECTS:poselayer_layer_obj>
)
target_link_libraries(poselayer_testkit PUBLIC poselayer_core PRIVATE poselayer_warnings)
target_include_directories(poselayer_testkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(poselayer_tests
    test_config.cpp
    test_layer.cpp
    test_negotiation.cpp
    test_recording.cpp
    test_replay.cpp
    test_transform.cpp
)
# /W4 without /WX on the test sources: Catch2's macros expand to code the layer's
# own targets would not accept, and the tests are not shipped.
target_compile_options(poselayer_tests PRIVATE /W4 /permissive- /utf-8)
target_compile_definitions(poselayer_tests PRIVATE POSELAYER_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(poselayer_tests PRIVATE poselayer_testkit Catch2::Catch2WithMain)

# The README-artifact generator: same harness, run once to capture real output.
add_executable(poseplay_sample_gen sample_gen.cpp)
target_link_libraries(poseplay_sample_gen PRIVATE poselayer_testkit poselayer_warnings)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(poselayer_tests)

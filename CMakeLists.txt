# 3.28 for EXCLUDE_FROM_ALL in FetchContent_Declare, which keeps the OpenXR SDK's
# own tests and loader out of the default build.
cmake_minimum_required(VERSION 3.28)

project(openxr_pose_layer VERSION 1.0.1 LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "openxr-pose-layer builds with MSVC only.")
endif()

get_property(_opl_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _opl_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Static CRT, so the layer DLL and poseplay.exe load on a machine with no VC++
# redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(POSELAYER_BUILD_TESTS "Build the unit tests" ON)
option(POSELAYER_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# OpenXR SDK: the loader-negotiation and core headers (Apache-2.0). Pinned to a
# 1.1 release tag; the layer builds against the 1.1 headers so it can intercept
# xrLocateSpaces. Only OpenXR::headers is used; the loader is what loads a layer,
# so the layer never links it.
# ---------------------------------------------------------------------------
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.43 # commit 781f2eab3698d653c804ecbd11e0aed47eaad1c6
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(openxr)

# ---------------------------------------------------------------------------
# Warnings, first-party targets only
# ---------------------------------------------------------------------------
add_library(poselayer_warnings INTERFACE)
target_compile_options(poselayer_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
if(POSELAYER_WARNINGS_AS_ERRORS)
    target_compile_options(poselayer_warnings INTERFACE /WX)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h
    @ONLY
)

# ---------------------------------------------------------------------------
# poselayer_core: the record format, replay lookup, pose math and config. No
# loader-negotiation header, no global state, so the tests link it directly.
# ---------------------------------------------------------------------------
add_library(poselayer_core STATIC
    src/core/Config.cpp
    src/core/Recording.cpp
    src/core/Replay.cpp
    src/core/Transform.cpp
)
target_compile_features(poselayer_core PUBLIC cxx_std_20)
target_include_directories(poselayer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_definitions(poselayer_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(poselayer_core PUBLIC OpenXR::headers PRIVATE poselayer_warnings)

# ---------------------------------------------------------------------------
# poselayer_layer_obj: the loader negotiation and the interception dispatch.
# Built once as objects, then linked into both the DLL and the test kit so the
# tests exercise the same code the loader loads.
# ---------------------------------------------------------------------------
add_library(poselayer_layer_obj OBJECT
    src/layer/Layer.cpp
    src/layer/Negotiation.cpp
)
target_link_libraries(poselayer_layer_obj PUBLIC poselayer_core PRIVATE poselayer_warnings)

# ---------------------------------------------------------------------------
# The layer DLL. The .def file exports the one symbol the loader looks up,
# xrNegotiateLoaderApiLayerInterface.
# ---------------------------------------------------------------------------
add_library(openxr_pose_layer SHARED
    $<TARGET_OBJECTS:poselayer_layer_obj>
    resources/PoseLayer.def
)
target_link_libraries(openxr_pose_layer PRIVATE poselayer_core)
set_target_properties(openxr_pose_layer PROPERTIES OUTPUT_NAME "openxr_pose_layer" PREFIX "")

# ---------------------------------------------------------------------------
# poseplay: the recording inspector and trimmer.
# ---------------------------------------------------------------------------
add_executable(poseplay src/poseplay/main.cpp)
target_link_libraries(poseplay PRIVATE poselayer_core poselayer_warnings)

# ---------------------------------------------------------------------------
# The layer manifest JSON, filled with the built DLL's name. It sits next to the
# DLL; install.ps1 registers this file's path.
# ---------------------------------------------------------------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/manifest/XR_APILAYER_GRC_pose_layer.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/XR_APILAYER_GRC_pose_layer.json
    @ONLY
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(POSELAYER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Package: a zip with the layer DLL and its manifest, poseplay, the install
# scripts, and the docs.
# ---------------------------------------------------------------------------
install(TARGETS openxr_pose_layer poseplay RUNTIME DESTINATION .)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/XR_APILAYER_GRC_pose_layer.json DESTINATION .)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/install.ps1
    ${CMAKE_CURRENT_SOURCE_DIR}/uninstall.ps1
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    ${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md
    ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    DESTINATION .
)

set(CPACK_PACKAGE_NAME "openxr-pose-layer")
set(CPACK_PACKAGE_VENDOR "Game Reversal Club")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "openxr-pose-layer-v${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Record and replay OpenXR poses through an API layer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)

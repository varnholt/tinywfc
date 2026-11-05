cmake_minimum_required(VERSION 3.25)

project(
    tinywfc
    VERSION 1.0.0
    DESCRIPTION "Wave function collapse on 2d and 3d grids, C++23, single header"
    HOMEPAGE_URL "https://github.com/varnholt/tinywfc"
    LANGUAGES CXX)

option(TINYWFC_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(TINYWFC_BUILD_TESTBED "Build the OpenGL ES 3 / WebGL2 city testbed (fetches SDL3 and Dear ImGui)" ${PROJECT_IS_TOP_LEVEL})
option(TINYWFC_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

add_library(tinywfc INTERFACE)
add_library(tinywfc::tinywfc ALIAS tinywfc)

target_include_directories(tinywfc INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                             $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(tinywfc INTERFACE cxx_std_23)

if(TINYWFC_BUILD_TESTS OR TINYWFC_BUILD_TESTBED)
    add_library(tinywfc_warnings INTERFACE)
    if(MSVC)
        target_compile_options(tinywfc_warnings INTERFACE /W4 /WX /permissive- /utf-8)
    else()
        # partially designated initializers like {._size = ..., ._seed = ...} are the intended way to fill settings
        target_compile_options(tinywfc_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                                                          -Wno-missing-field-initializers)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19)
            target_compile_options(tinywfc_warnings INTERFACE -Wno-missing-designated-field-initializers)
        endif()
    endif()
endif()

if(TINYWFC_BUILD_TESTBED)
    add_subdirectory(testbed)
endif()

if(TINYWFC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(bench)
endif()

if(TINYWFC_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS tinywfc EXPORT tinywfcTargets)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(
        EXPORT tinywfcTargets
        NAMESPACE tinywfc::
        FILE tinywfcConfig.cmake
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinywfc)

    write_basic_package_version_file(${PROJECT_BINARY_DIR}/tinywfcConfigVersion.cmake
                                     COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES ${PROJECT_BINARY_DIR}/tinywfcConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinywfc)
endif()

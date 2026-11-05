# the city testbed: SDL3 window, OpenGL ES 3.0 context (WebGL2 in the browser), Dear ImGui. only built with
# TINYWFC_BUILD_TESTBED; nothing here is part of the installed library.

enable_language(C)
include(FetchContent)

set(SDL_SHARED
    OFF
    CACHE BOOL "" FORCE)
set(SDL_STATIC
    ON
    CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY
    OFF
    CACHE BOOL "" FORCE)

FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.28
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sdl3)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

# third-party code, compiled without our warning flags
add_library(
    tinywfc_testbed_third_party STATIC
    third_party/stb/stb_image_write.c
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_compile_features(tinywfc_testbed_third_party PUBLIC cxx_std_23)
# the opengl3 backend in its es 3.0 flavor, on every platform
target_compile_definitions(tinywfc_testbed_third_party PUBLIC IMGUI_IMPL_OPENGL_ES3)
target_include_directories(tinywfc_testbed_third_party SYSTEM PUBLIC third_party/stb ${imgui_SOURCE_DIR}
                                                                     ${imgui_SOURCE_DIR}/backends)
target_link_libraries(tinywfc_testbed_third_party PUBLIC SDL3::SDL3-static)
if(NOT EMSCRIPTEN)
    # glad loads the opengl es 3.0 functions; the shim lets imgui's es3 backend include <GLES3/gl3.h>
    target_sources(tinywfc_testbed_third_party PRIVATE third_party/glad/src/gles2.c)
    target_include_directories(tinywfc_testbed_third_party SYSTEM PUBLIC third_party/glad/include
                                                                         third_party/gles3_shim)
endif()

add_executable(
    tinywfc_testbed
    app.cpp
    app.h
    city.cpp
    city.h
    generator.cpp
    generator.h
    gl.h
    main.cpp
    math3d.h
    meshes.cpp
    meshes.h
    renderer.cpp
    renderer.h
    ui.cpp)
target_link_libraries(tinywfc_testbed PRIVATE tinywfc::tinywfc tinywfc_warnings tinywfc_testbed_third_party)

if(EMSCRIPTEN)
    set_target_properties(tinywfc_testbed PROPERTIES SUFFIX ".html" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    target_link_options(
        tinywfc_testbed
        PRIVATE
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
elseif(WIN32)
    set_target_properties(tinywfc_testbed PROPERTIES WIN32_EXECUTABLE OFF)
endif()

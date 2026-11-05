#pragma once

// opengl es 3.0 everywhere: emscripten maps it onto webgl2, natively glad loads it from the driver
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gles2.h>
#endif

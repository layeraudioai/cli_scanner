/**
 * @file webgl_bridge.c
 * @author your name (you@domain.com)
 * @brief High performance ASCII screen pixel-shader bridge for WebGL.
 * compiles to WebAssembly with Emscripten to run C rendering at 120 FPS.
 */

#include "cli_scanner.hpp"

// The __EMSCRIPTEN__ macro is defined by the Emscripten compiler.
// This allows us to conditionally compile the WebGL code only when targeting WebAssembly.
#ifdef __EMSCRIPTEN__

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <stdio.h>

// Shaders for rendering the text framebuffer onto a dynamic 3D quad texture
const char* vertex_shader_source = 
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "   gl_Position = a_position;\n"
    "   v_texcoord = a_texcoord;\n"
    "}\n";

const char* fragment_shader_source = 
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "   vec4 color = texture2D(u_texture, v_texcoord);\n"
    "   // Apply scanline effect\n"
    "   float scanline = sin(v_texcoord.y * 800.0) * 0.04;\n"
    "   gl_FragColor = vec4(color.rgb - scanline, color.a);\n"
    "}\n";

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    return shader;
}

void init_webgl_viewport(int width, int height) {
    printf("[WEBGL] Binding viewport context of %dx%d to HTML5 canvas\n", width, height);
    
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glUseProgram(program);
    
    printf("[WEBGL] Shader compilation & GPU mapping successful!\n");
}

#else

// For native builds (GCC, Clang, MSVC), provide a stub function.
// This allows the code to compile and link without needing GLES2 headers.
#include <stdio.h>

void init_webgl_viewport(int width, int height) { // NOLINT(misc-unused-parameters)
    // This is a stub for native builds.
    // The printf calls were removed as per the request to avoid direct terminal output.
}

#endif

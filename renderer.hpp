#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "framebuffer.hpp"

void render_donut_3d(FrameBuffer& fb, float A, float B, const std::string& char_map = ".,-~:;=!*#$@");

#endif // RENDERER_HPP
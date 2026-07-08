#include "framebuffer.hpp"
#include <cmath>
#include <vector>

void render_donut_3d(FrameBuffer& fb, float A, float B, const std::string& char_map = ".,-~:;=!*#$@") {
    int cols = fb.cols;
    int rows = fb.rows;
    std::vector<float> z_buffer(cols * rows, 0.0f);

    for (float j = 0.0f; j < 6.28f; j += 0.07f) {
        for (float i = 0.0f; i < 6.28f; i += 0.02f) {
            float cos_i = std::cos(i), sin_i = std::sin(i);
            float cos_j = std::cos(j), sin_j = std::sin(j);
            float cos_A = std::cos(A), sin_A = std::sin(A);
            float cos_B = std::cos(B), sin_B = std::sin(B);

            float h = cos_j + 2.0f;
            float D = 1.0f / (sin_i * h * sin_A + sin_j * cos_A + 5.0f);
            float t = sin_i * h * cos_A - sin_j * sin_A;

            int x = static_cast<int>(cols / 2 + (cols / 1.5f) * D * (cos_i * h * cos_B - t * sin_B));
            int y = static_cast<int>(rows / 2 + (rows / 1.5f) * D * (cos_i * h * sin_B + t * cos_B));
            int o = x + cols * y;

            float N_calc = (sin_j * sin_A - sin_i * cos_j * cos_A) * cos_B - sin_i * cos_j * sin_A - sin_j * cos_A - cos_i * cos_j * sin_B;
            int N = static_cast<int>(8.0f * N_calc);

            if (y < rows && y >= 0 && x >= 0 && x < cols && D > z_buffer[o]) {
                z_buffer[o] = D;
                char ch = char_map[N > 0 ? (N < (int)char_map.length() ? N : (int)char_map.length() - 1) : 0];
                
                // Color pair IDs mapped for ncurses:
                // 3 = white/bright, 4 = cyan, 5 = blue, 0 = default
                int color_pair = 0;
                if (N > 8) color_pair = 3;      // Brightest
                else if (N > 4) color_pair = 4; // Midtone (cyan)
                else if (N > 0) color_pair = 5; // Shadow (blue)

                fb.buffer[y][x] = TerminalChar(ch, color_pair);
            }
        }
    }
}

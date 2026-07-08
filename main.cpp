#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <ncurses.h>
#include "framebuffer.hpp"

// Forward declaration of renderer functions
void render_donut_3d(FrameBuffer& fb, float A, float B, const std::string& char_map);

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Global state for Emscripten main loop compatibility
int cols = 100;
int rows = 40;
FrameBuffer fb(cols, rows);
float A = 0.0f;
float B = 0.0f;
int active_mode = 0; // 0 = Torus, 1 = Web ASCII
std::string browser_title = "EXAMPLE WEBSITE";
std::string browser_content = "Welcome to the NCURSES C++ compiled HTML5 Browser. "
                              "This text is dynamically mapped to a virtual terminal buffer. "
                              "Use your keyboard to navigate.";

std::string custom_char_map = ".,-~:;=!*#$@";

void render_frame() {
    fb.clear();

    if (active_mode == 0) {
        // Draw standard borders and torus
        fb.draw_box(0, 0, cols, rows, "TERMINX C++ ENGINE v1.0.4 - ACTIVE: TORUS");
        render_donut_3d(fb, A, B, custom_char_map);
        A += 0.05f;
        B += 0.02f;
    } else {
        // Web Browser simulation rendering
        fb.draw_box(0, 0, cols, rows, "TERMINX C++ BROWSER - ACTIVE: WEB ASCII");
        fb.draw_box(2, 2, cols - 4, rows - 4, browser_title);
        fb.draw_wrapped_text(4, 4, cols - 8, rows - 8, browser_content, 0);
    }

    // Draw Bottom bar instructions
    fb.draw_box(0, rows - 3, cols, 3);
    fb.draw_text(2, rows - 2, "Press [1] Torus Mode  [2] Web ASCII Mode  [Q] Exit", 2);

    // Output to ncurses terminal
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            TerminalChar cell = fb.buffer[y][x];
            if (cell.color_pair > 0) {
                attron(COLOR_PAIR(cell.color_pair));
                mvaddch(y, x, cell.ch);
                attroff(COLOR_PAIR(cell.color_pair));
            } else {
                mvaddch(y, x, cell.ch);
            }
        }
    }
    refresh();
}

#ifdef __EMSCRIPTEN__
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void set_mode(int mode) {
        active_mode = mode;
    }

    EMSCRIPTEN_KEEPALIVE
    void set_browser_data(const char* title, const char* content) {
        browser_title = title;
        browser_content = content;
    }

    EMSCRIPTEN_KEEPALIVE
    void set_char_map(const char* char_map) {
        custom_char_map = char_map;
    }
}
#endif

int main() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    // Enable color support
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);
        init_pair(3, COLOR_WHITE, COLOR_BLACK);
        init_pair(4, COLOR_CYAN, COLOR_BLACK);
        init_pair(5, COLOR_BLUE, COLOR_BLACK);
    }

    nodelay(stdscr, TRUE); // Non-blocking user input

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(render_frame, 60, 1);
#else
    bool running = true;
    while (running) {
        int ch = getch();
        if (ch != ERR) {
            if (ch == '1') active_mode = 0;
            if (ch == '2') active_mode = 1;
            if (ch == 'q' || ch == 'Q') running = false;
        }

        render_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
    endwin();
#endif

    return 0;
}

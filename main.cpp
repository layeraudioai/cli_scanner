#include "flabergast.h"
#include <iostream>
#include "project_creator.h"
#include <string>
#include <thread>
#include <chrono>
#include <ncurses.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char* argv[]) {
    // Argument parsing for flabergast and create modes
    bool run_flabergast = false;
    bool create_mode = false;
    bool apply = false;
    std::string create_prompt = "";
    std::string root_dir = FLABERGAST_ROOT_DIR;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--flabergast") {
            run_flabergast = true;
        } else if (arg == "--create") {
            create_mode = true;
            if (i + 1 < argc) {
                create_prompt = argv[++i];
            }
        } else if (arg == "--apply") {
            apply = true;
        } else if (arg == "--root" && i + 1 < argc) {
            root_dir = argv[++i];
        }
    }

    if (run_flabergast) {
        // Safety: if --apply is requested, limit to test project directory
        if (apply) {
            // Force root to the test project subdirectory
            root_dir = "K:/flabergast/cli_scanner/test_project";
        }
        // Execute the flabergast analysis
        analyze_and_fix(root_dir.c_str(), apply);
        return 0;
    }
    if (create_mode) {
        // Create a new project based on the prompt
        create_project_from_prompt(create_prompt);
        return 0;
    }

    // Existing ncurses demo code (unchanged) ...
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

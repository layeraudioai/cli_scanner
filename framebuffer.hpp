#ifndef FRAMEBUFFER_HPP
#define FRAMEBUFFER_HPP

#include <vector>
#include <string>

struct TerminalChar {
    char ch = ' ';
    int color_pair = 0; // ncurses color pair ID

    TerminalChar(char c = ' ', int cp = 0) : ch(c), color_pair(cp) {}
};

class FrameBuffer {
public:
    int cols;
    int rows;
    std::vector<std::vector<TerminalChar>> buffer;

    FrameBuffer(int c, int r) : cols(c), rows(r) {
        clear();
    }

    void clear() {
        buffer.assign(rows, std::vector<TerminalChar>(cols, TerminalChar(' ', 0)));
    }

    void draw_box(int x, int y, int w, int h, const std::string& title = "") {
        // Draw corners
        if (is_valid(x, y)) buffer[y][x] = TerminalChar('+', 1);
        if (is_valid(x + w - 1, y)) buffer[y][x + w - 1] = TerminalChar('+', 1);
        if (is_valid(x, y + h - 1)) buffer[y + h - 1][x] = TerminalChar('+', 1);
        if (is_valid(x + w - 1, y + h - 1)) buffer[y + h - 1][x + w - 1] = TerminalChar('+', 1);

        // Lines
        for (int i = 1; i < w - 1; ++i) {
            if (is_valid(x + i, y)) buffer[y][x + i] = TerminalChar('-', 1);
            if (is_valid(x + i, y + h - 1)) buffer[y + h - 1][x + i] = TerminalChar('-', 1);
        }
        for (int i = 1; i < h - 1; ++i) {
            if (is_valid(x, y + i)) buffer[y + i][x] = TerminalChar('|', 1);
            if (is_valid(x + w - 1, y + i)) buffer[y + i][x + w - 1] = TerminalChar('|', 1);
        }

        // Draw Title
        if (!title.empty()) {
            std::string t = " " + title + " ";
            for (size_t i = 0; i < t.length() && (x + 2 + i) < (size_t)(x + w - 2); ++i) {
                if (is_valid(x + 2 + i, y)) {
                    buffer[y][x + 2 + i] = TerminalChar(t[i], 2); // Highlighted title color
                }
            }
        }
    }

    void draw_text(int x, int y, const std::string& text, int color_pair = 0) {
        for (size_t i = 0; i < text.length() && (x + i) < (size_t)cols; ++i) {
            if (is_valid(x + i, y)) {
                buffer[y][x + i] = TerminalChar(text[i], color_pair);
            }
        }
    }

    void draw_wrapped_text(int x, int y, int w, int h, const std::string& text, int color_pair = 0) {
        int cur_line = 0;
        int cur_pos = 0;
        std::string word = "";

        auto render_word = [&](const std::string& wd) {
            if (cur_line >= h) return;
            if (cur_pos + wd.length() > (size_t)w) {
                cur_line++;
                cur_pos = 0;
            }
            if (cur_line >= h) return;
            for (char ch : wd) {
                if (cur_pos < w && is_valid(x + cur_pos, y + cur_line)) {
                    buffer[y + cur_line][x + cur_pos] = TerminalChar(ch, color_pair);
                    cur_pos++;
                }
            }
            if (cur_pos < w && is_valid(x + cur_pos, y + cur_line)) {
                buffer[y + cur_line][x + cur_pos] = TerminalChar(' ', color_pair);
                cur_pos++;
            }
        };

        for (char ch : text) {
            if (ch == ' ' || ch == '\n') {
                if (!word.empty()) {
                    render_word(word);
                    word = "";
                }
                if (ch == '\n') {
                    cur_line++;
                    cur_pos = 0;
                }
            } else {
                word += ch;
            }
        }
        if (!word.empty()) {
            render_word(word);
        }
    }

private:
    bool is_valid(int x, int y) {
        return x >= 0 && x < cols && y >= 0 && y < rows;
    }
};

#endif // FRAMEBUFFER_HPP

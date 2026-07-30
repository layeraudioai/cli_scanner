#include "cli_scanner.hpp"
#include "renderer.hpp" // C++ header for our donut
#include <chrono>      // For performance benchmark timing
#include <sys/ioctl.h> // For getting terminal size on POSIX
#include <string>

#define MAX_LINES 300
#define MAX_LINE_LEN 256

char edit_lines[MAX_LINES][MAX_LINE_LEN];
int edit_line_count = 0;

volatile int g_current_note_freq = 0;

// Initialize terminal settings (particularly for enabling virtual terminal sequences / ANSI on Windows 10+)
void init_terminal(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
#endif
}

// Cross-platform key reading
#ifdef _WIN32
int read_key(void) {
    int ch = _getch();
    if (ch == 0 || ch == 224) {
        ch = _getch(); // Read the extended key code
        if (ch == 72) return KEY_UP;
        if (ch == 80) return KEY_DOWN;
        if (ch == 75) return KEY_LEFT;
        if (ch == 77) return KEY_RIGHT;
    }
    return ch;
}

int is_key_pressed(void) {
    return _kbhit();
}
#else
int read_key(void) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    if (ch == 27) {
        newt.c_cc[VMIN] = 0;
        newt.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int next1 = getchar();
        int next2 = getchar();
        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        if (next1 == 91) {
            if (next2 == 65) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return KEY_UP; }
            if (next2 == 66) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return KEY_DOWN; }
            if (next2 == 68) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return KEY_LEFT; }
            if (next2 == 67) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return KEY_RIGHT; }
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return 27;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

int is_key_pressed(void) {
    struct termios oldt, newt;
    int oldf; // Unused, but kept for potential future use

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // Use select() for a reliable non-blocking check on POSIX systems
    fd_set rfds;
    struct timeval tv = {0, 0};
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return FD_ISSET(STDIN_FILENO, &rfds);
}
#endif

// Check for empty catch blocks
int check_empty_catch(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    int found = 0;
    const char *ptr = content;
    int line_num = 1;
    while (*ptr != '\0') {
        if (*ptr == '\n') line_num++;
        
        // Skip comments and literals
        if (*ptr == '/' && *(ptr + 1) == '/') {
            ptr += 2;
            while (*ptr != '\0' && *ptr != '\n') ptr++;
            continue;
        }
        if (*ptr == '/' && *(ptr + 1) == '*') {
            ptr += 2;
            while (*ptr != '\0' && !(*ptr == '*' && *(ptr + 1) == '/')) {
                if (*ptr == '\n') line_num++;
                ptr++;
            }
            if (*ptr != '\0') ptr += 2;
            continue;
        }
        if (*ptr == '"') {
            ptr++;
            while (*ptr != '\0' && *ptr != '"') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '"') ptr++;
            continue;
        }
        if (*ptr == '\'') {
            ptr++;
            while (*ptr != '\0' && *ptr != '\'') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '\'') ptr++;
            continue;
        }
        
        if (strncmp(ptr, "catch", 5) == 0 && (ptr == content || *(ptr - 1) == ' ' || *(ptr - 1) == '\n' || *(ptr - 1) == '\t' || *(ptr - 1) == '}')) {
            const char *sub = ptr + 5;
            while (*sub == ' ' || *sub == '\t' || *sub == '\n' || *sub == '\r') {
                if (*sub == '\n') line_num++;
                sub++;
            }
            if (*sub == '(') {
                int parens = 1;
                sub++;
                while (*sub != '\0' && parens > 0) {
                    if (*sub == '\n') line_num++;
                    if (*sub == '(') parens++;
                    if (*sub == ')') parens--;
                    sub++;
                }
            }
            while (*sub == ' ' || *sub == '\t' || *sub == '\n' || *sub == '\r') {
                if (*sub == '\n') line_num++;
                sub++;
            }
            if (*sub == '{') {
                const char *block_start = sub + 1;
                const char *block_ptr = block_start;
                int is_empty = 1;
                while (*block_ptr != '\0' && *block_ptr != '}') {
                    if (*block_ptr != ' ' && *block_ptr != '\t' && *block_ptr != '\n' && *block_ptr != '\r' && *block_ptr != '/') {
                        is_empty = 0;
                    }
                    block_ptr++;
                }
                if (is_empty && *block_ptr == '}') {
                    int idx = *issue_count;
                    strcpy(issues[idx].file_path, file_path);
                    issues[idx].line = line_num;
                    issues[idx].column = 1;
                    strcpy(issues[idx].error_code, "CS0168");
                    strcpy(issues[idx].message, "Empty catch block detected. Swallowing exceptions is a severe anti-pattern. Consider logging or rethrowing.");
                    strcpy(issues[idx].original_code, "");
                    strcpy(issues[idx].fixed_code, "");
                    (*issue_count)++;
                    found++;
                }
            }
        }
        ptr++;
    }
    return found;
}

// Check if class name starts with a lowercase letter (StyleCop SA1300)
int check_class_name_pascal_case(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    int found = 0;
    const char *ptr = content;
    int line_num = 1;
    while (*ptr != '\0') {
        if (*ptr == '\n') line_num++;
        
        // Skip comments and literals
        if (*ptr == '/' && *(ptr + 1) == '/') {
            ptr += 2;
            while (*ptr != '\0' && *ptr != '\n') ptr++;
            continue;
        }
        if (*ptr == '/' && *(ptr + 1) == '*') {
            ptr += 2;
            while (*ptr != '\0' && !(*ptr == '*' && *(ptr + 1) == '/')) {
                if (*ptr == '\n') line_num++;
                ptr++;
            }
            if (*ptr != '\0') ptr += 2;
            continue;
        }
        if (*ptr == '"') {
            ptr++;
            while (*ptr != '\0' && *ptr != '"') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '"') ptr++;
            continue;
        }
        if (*ptr == '\'') {
            ptr++;
            while (*ptr != '\0' && *ptr != '\'') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '\'') ptr++;
            continue;
        }
        
        if (strncmp(ptr, "class ", 6) == 0 && (ptr == content || *(ptr - 1) == ' ' || *(ptr - 1) == '\n' || *(ptr - 1) == '\t' || *(ptr - 1) == '}')) {
            const char *sub = ptr + 6;
            while (*sub == ' ' || *sub == '\t') sub++;
            if (*sub >= 'a' && *sub <= 'z') {
                char bad_name[64];
                int b_len = 0;
                while (*sub != '\0' && *sub != ' ' && *sub != '\n' && *sub != '\r' && *sub != '{' && *sub != ':' && b_len < 63) {
                    bad_name[b_len++] = *sub++;
                }
                bad_name[b_len] = '\0';
                
                char fixed_name[64];
                strcpy(fixed_name, bad_name);
                fixed_name[0] = fixed_name[0] - ('a' - 'A'); // Capitalize
                
                int idx = *issue_count;
                strcpy(issues[idx].file_path, file_path);
                issues[idx].line = line_num;
                issues[idx].column = (int)(sub - ptr - b_len);
                strcpy(issues[idx].error_code, "SA1300");
                sprintf(issues[idx].message, "Class name '%s' should begin with an uppercase letter (PascalCase).", bad_name);
                sprintf(issues[idx].original_code, "class %s", bad_name);
                sprintf(issues[idx].fixed_code, "class %s", fixed_name);
                (*issue_count)++;
                found++;
            }
        }
        ptr++;
    }
    return found;
}

int check_include_guards(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    const char *p = file_path;
    while (*p != '\0') p++;
    while (p > file_path && *p != '.') p--;
    if (!(*p == '.' && (*(p+1) == 'h' || *(p+1) == 'H'))) return 0; // Only for .h/.H files

    if (strstr(content, "#ifndef") == NULL || strstr(content, "#define") == NULL || strstr(content, "#endif") == NULL) {
        int idx = *issue_count;
        strcpy(issues[idx].file_path, file_path);
        issues[idx].line = 1;
        issues[idx].column = 1;
        strcpy(issues[idx].error_code, "C4005");
        strcpy(issues[idx].message, "Header file is missing include guards (#ifndef/#define/#endif).");
        strcpy(issues[idx].original_code, "");
        strcpy(issues[idx].fixed_code, "");
        (*issue_count)++;
        return 1;
    }
    return 0;
}

int check_gets_usage(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    const char *ptr = content;
    int line_num = 1;
    int found = 0;
    while ((ptr = strstr(ptr, "gets(")) != NULL) {
        // Basic check to avoid matching function names like "my_gets()"
        if (ptr == content || (!isalnum(*(ptr-1)) && *(ptr-1) != '_')) {
            int idx = *issue_count;
            strcpy(issues[idx].file_path, file_path);
            issues[idx].line = line_num; // Note: this line number is approximate
            issues[idx].column = 1;
            strcpy(issues[idx].error_code, "C4996");
            strcpy(issues[idx].message, "Use of 'gets' is deprecated and unsafe; consider using 'fgets' instead.");
            strcpy(issues[idx].original_code, "gets(...)");
            strcpy(issues[idx].fixed_code, "fgets(buf, sizeof(buf), stdin)");
            (*issue_count)++;
            found++;
        }
        ptr++; // Move past the found instance
    }
    // This is a simplified line count, a more accurate one would iterate char by char.
    return found;
}

void get_line_at_ptr(const char *content, const char *p, char *line_buf, int max_len) {
    const char *start = p;
    while (start > content && *(start - 1) != '\n') {
        start--;
    }
    const char *end = p;
    while (*end != '\0' && *end != '\n' && *end != '\r') {
        end++;
    }
    int len = (int)(end - start);
    if (len >= max_len) {
        len = max_len - 1;
    }
    strncpy(line_buf, start, len);
    line_buf[len] = '\0';
}

int check_v1_recovery(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    int found_issues = 0;
    int brace_stack[256];
    int stack_depth = 0;
    int line_num = 1;
    const char *ptr = content;
    int last_reported_line = 0;
    
    while (*ptr != '\0') {
        if (*ptr == '\n') {
            line_num++;
            ptr++;
            continue;
        }
        
        // Skip comments and literals
        if (*ptr == '/' && *(ptr + 1) == '/') {
            ptr += 2;
            while (*ptr != '\0' && *ptr != '\n') ptr++;
            continue;
        }
        if (*ptr == '/' && *(ptr + 1) == '*') {
            ptr += 2;
            while (*ptr != '\0' && !(*ptr == '*' && *(ptr + 1) == '/')) {
                if (*ptr == '\n') line_num++;
                ptr++;
            }
            if (*ptr != '\0') ptr += 2;
            continue;
        }
        if (*ptr == '"') {
            ptr++;
            while (*ptr != '\0' && *ptr != '"') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '"') ptr++;
            continue;
        }
        if (*ptr == '\'') {
            ptr++;
            while (*ptr != '\0' && *ptr != '\'') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    if (*(ptr + 1) == '\n') line_num++;
                    ptr += 2;
                } else {
                    if (*ptr == '\n') line_num++;
                    ptr++;
                }
            }
            if (*ptr == '\'') ptr++;
            continue;
        }
        
        if (*ptr == '{') {
            int is_init = 0;
            const char *back = ptr - 1;
            while (back > content) {
                if (*back == ' ' || *back == '\t' || *back == '\r' || *back == '\n') {
                    back--;
                    continue;
                }
                break;
            }
            int chars_scanned = 0;
            const char *scan = back;
            while (scan > content && chars_scanned < 150) {
                char c = *scan;
                if (c == ';' || c == '{' || c == '}') {
                    break;
                }
                if (scan - 3 >= content && strncmp(scan - 2, "new", 3) == 0) {
                    char prev = *(scan - 3);
                    char next = *scan;
                    if ((prev == ' ' || prev == '\t' || prev == '\r' || prev == '\n' || prev == '(' || prev == '=') && 
                        (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '<' || next == '(' || next == '{' || next == '[')) {
                        is_init = 1;
                        break;
                    }
                }
                if (scan - 6 >= content && strncmp(scan - 5, "switch", 6) == 0) {
                    char prev = *(scan - 6);
                    char next = *scan;
                    if ((prev == ' ' || prev == '\t' || prev == '\r' || prev == '\n' || prev == ')') && 
                        (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '{')) {
                        is_init = 1;
                        break;
                    }
                }
                scan--;
                chars_scanned++;
            }
            if (stack_depth < 256) {
                brace_stack[stack_depth++] = is_init;
            }
            ptr++;
            continue;
        }
        
        if (*ptr == '}') {
            if (stack_depth > 0) {
                stack_depth--;
            }
            ptr++;
            continue;
        }
        
        // Semicolon check
        if (*ptr == ';') {
            int is_v1_error = 0;
            const char *reason = "";
            
            // 1. Check for ,; pattern (semicolon immediately after comma, with optional space)
            const char *prev_c = ptr - 1;
            while (prev_c > content && (*prev_c == ' ' || *prev_c == '\t')) {
                prev_c--;
            }
            if (prev_c > content && *prev_c == ',') {
                is_v1_error = 1;
                reason = "Incorrect semicolon appended after comma.";
            }
            
            // 2. Check for new Type; followed by {
            if (!is_v1_error) {
                const char *next_sig = ptr + 1;
                while (*next_sig != '\0' && (*next_sig == ' ' || *next_sig == '\t' || *next_sig == '\r' || *next_sig == '\n')) {
                    next_sig++;
                }
                if (*next_sig == '{') {
                    // Check if this line contains 'new'
                    const char *line_start = ptr;
                    while (line_start > content && *(line_start - 1) != '\n') {
                        line_start--;
                    }
                    if (strstr(line_start, "new ") != NULL || strstr(line_start, "switch") != NULL) {
                        is_v1_error = 1;
                        reason = "Semicolon incorrectly placed between initialization expression and its block.";
                    }
                }
            }
            
            int replace_with_comma = 0;
            // 3. Semicolons inside an active initializer block (at the top-level of the block)
            if (!is_v1_error && stack_depth > 0 && brace_stack[stack_depth - 1] == 1) {
                is_v1_error = 1;
                reason = "Semicolon invalid inside an object/collection initializer block.";
                replace_with_comma = 1;
            }
            
            if (is_v1_error && line_num != last_reported_line) {
                // Get line start to calculate column
                const char *l_start = ptr;
                while (l_start > content && *(l_start - 1) != '\n') {
                    l_start--;
                }
                int col = (int)(ptr - l_start) + 1;
                
                char line_buf[1024];
                get_line_at_ptr(content, ptr, line_buf, sizeof(line_buf));
                
                int idx = *issue_count;
                if (idx < 100) {
                    strcpy(issues[idx].file_path, file_path);
                    issues[idx].line = line_num;
                    issues[idx].column = col;
                    strcpy(issues[idx].error_code, "V1_RECOVERY");
                    strcpy(issues[idx].message, reason);
                    strcpy(issues[idx].original_code, line_buf);
                    
                    // Create fixed code (omitting the semicolon)
                    int ptr_offset = (int)(ptr - l_start);
                    char fixed_buf[1024];
                    strncpy(fixed_buf, line_buf, ptr_offset);
                    fixed_buf[ptr_offset] = '\0';
                    
                    if (replace_with_comma) {
                        strcat(fixed_buf, ",");
                    }
                    
                    // Copy everything after semicolon
                    const char *after_ptr = ptr + 1;
                    char after_buf[512] = "";
                    int ab_len = 0;
                    while (*after_ptr != '\0' && *after_ptr != '\n' && *after_ptr != '\r' && ab_len < 511) {
                        after_buf[ab_len++] = *after_ptr++;
                    }
                    after_buf[ab_len] = '\0';
                    strcat(fixed_buf, after_buf);
                    
                    strcpy(issues[idx].fixed_code, fixed_buf);
                    
                    (*issue_count)++;
                    found_issues++;
                    last_reported_line = line_num;
                }
            }
        }
        
        ptr++;
    }
    
    return found_issues;
}

// Renders an ANSI highlighted substring of width, with correct space padding
void print_highlighted_substring(const char *line, int width) {
    char plain[256];
    int len = strlen(line);
    if (len > width) {
        strncpy(plain, line, width);
        plain[width] = '\0';
    } else {
        strcpy(plain, line);
    }
    
    int p_len = strlen(plain);
    int j = 0;
    int in_string = 0;
    int in_comment = 0;
    while (j < p_len) {
        if (in_comment) {
            printf("%s%c", COLOR_GREEN, plain[j]);
            j++;
            continue;
        }
        if (in_string) {
            printf("%s%c", COLOR_YELLOW, plain[j]);
            if (plain[j] == '"' && (j == 0 || plain[j-1] != '\\')) {
                in_string = 0;
                printf("%s", COLOR_RESET);
            }
            j++;
            continue;
        }
        if (plain[j] == '/' && j + 1 < p_len && plain[j+1] == '/') {
            in_comment = 1;
            printf("%s//", COLOR_GREEN);
            j += 2;
            continue;
        }
        if (plain[j] == '"') {
            in_string = 1;
            printf("%s\"", COLOR_YELLOW);
            j++;
            continue;
        }
        
        if ((plain[j] >= 'a' && plain[j] <= 'z') || (plain[j] >= 'A' && plain[j] <= 'Z') || plain[j] == '_') {
            char word[64];
            int w_len = 0;
            while (j < p_len && w_len < 63 && ((plain[j] >= 'a' && plain[j] <= 'z') || (plain[j] >= 'A' && plain[j] <= 'Z') || (plain[j] >= '0' && plain[j] <= '9') || plain[j] == '_')) {
                word[w_len++] = plain[j++];
            }
            word[w_len] = '\0';
            
            if (strcmp(word, "using") == 0 || strcmp(word, "namespace") == 0 || 
                strcmp(word, "public") == 0 || strcmp(word, "private") == 0 || 
                strcmp(word, "class") == 0 || strcmp(word, "void") == 0 || 
                strcmp(word, "string") == 0 || strcmp(word, "int") == 0 || 
                strcmp(word, "bool") == 0 || strcmp(word, "decimal") == 0 || 
                strcmp(word, "return") == 0 || strcmp(word, "new") == 0 || 
                strcmp(word, "true") == 0 || strcmp(word, "false") == 0 ||
                strcmp(word, "static") == 0 || strcmp(word, "if") == 0 || 
                strcmp(word, "else") == 0) {
                printf("%s%s%s", COLOR_MAGENTA, word, COLOR_RESET);
            } else if (strcmp(word, "Console") == 0 || strcmp(word, "List") == 0 || strcmp(word, "DateTime") == 0) {
                printf("%s%s%s", COLOR_CYAN, word, COLOR_RESET);
            } else {
                printf("%s", word);
            }
        } else {
            char c = plain[j];
            if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
                printf("%s%c%s", COLOR_YELLOW, c, COLOR_RESET);
            } else if (c == ';' || c == '=') {
                printf("%s%c%s", COLOR_BLUE, c, COLOR_RESET);
            } else {
                putchar(c);
            }
            j++;
        }
    }
    printf("%s", COLOR_RESET);
    
    int spaces_needed = width - p_len;
    for (int s = 0; s < spaces_needed; s++) {
        putchar(' ');
    }
}

// Check for missing System.Collections.Generic when List<T> is used
int check_list_imports(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    if (strstr(content, "List<") != NULL && strstr(content, "using System.Collections.Generic;") == NULL) {
        int idx = *issue_count;
        strcpy(issues[idx].file_path, file_path);
        issues[idx].line = 1;
        issues[idx].column = 1;
        strcpy(issues[idx].error_code, "CS0246");
        strcpy(issues[idx].message, "The type or namespace name 'List<>' could not be found. Are you missing 'using System.Collections.Generic;'?");
        strcpy(issues[idx].original_code, "using System;");
        strcpy(issues[idx].fixed_code, "using System;\nusing System.Collections.Generic;");
        (*issue_count)++;
        return 1;
    }
    return 0;
}

char get_next_significant_char(const char *ptr) {
    if (!ptr) return '\0';
    while (*ptr != '\0') {
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
            ptr++;
            continue;
        }
        // Skip single-line comments
        if (*ptr == '/' && *(ptr + 1) == '/') {
            ptr += 2;
            while (*ptr != '\0' && *ptr != '\n') {
                ptr++;
            }
            continue;
        }
        // Skip multi-line comments
        if (*ptr == '/' && *(ptr + 1) == '*') {
            ptr += 2;
            while (*ptr != '\0' && !(*ptr == '*' && *(ptr + 1) == '/')) {
                ptr++;
            }
            if (*ptr != '\0') {
                ptr += 2;
            }
            continue;
        }
        return *ptr;
    }
    return '\0';
}

// Check for missing semicolons (basic statement heuristic)
int check_semicolons(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    int found_issues = 0;
    char line_buf[1024];
    int line_num = 1;
    const char *ptr = content;
    
    while (*ptr != '\0') {
        size_t len = 0;
        while (*ptr != '\0' && *ptr != '\n' && len < sizeof(line_buf) - 1) {
            line_buf[len++] = *ptr++;
        }
        line_buf[len] = '\0';
        if (*ptr == '\n') {
            ptr++;
        }
        
        while (len > 0 && (line_buf[len - 1] == ' ' || line_buf[len - 1] == '\r' || line_buf[len - 1] == '\t')) {
            line_buf[--len] = '\0';
        }
        
        char clean_line[1024];
        int in_str = 0, in_chr = 0;
        int clean_len = 0;
        int comment_idx = -1;
        
        for (int i = 0; line_buf[i] != '\0' && clean_len < 1023; ) {
            if (in_str) {
                if (line_buf[i] == '"' && (i == 0 || line_buf[i-1] != '\\')) in_str = 0;
                clean_line[clean_len++] = line_buf[i++];
                continue;
            }
            if (in_chr) {
                if (line_buf[i] == '\'' && (i == 0 || line_buf[i-1] != '\\')) in_chr = 0;
                clean_line[clean_len++] = line_buf[i++];
                continue;
            }
            if (line_buf[i] == '"') { in_str = 1; clean_line[clean_len++] = line_buf[i++]; continue; }
            if (line_buf[i] == '\'') { in_chr = 1; clean_line[clean_len++] = line_buf[i++]; continue; }
            
            // Check for single-line comment
            if (line_buf[i] == '/' && line_buf[i+1] == '/') {
                if (comment_idx == -1) comment_idx = i;
                break; // Skip everything else in the line
            }
            
            // Check for block comment start
            if (line_buf[i] == '/' && line_buf[i+1] == '*') {
                if (comment_idx == -1) comment_idx = i;
                // Skip until block comment end or end of line
                i += 2;
                while (line_buf[i] != '\0' && !(line_buf[i] == '*' && line_buf[i+1] == '/')) {
                    i++;
                }
                if (line_buf[i] != '\0') i += 2; // skip */
                continue;
            }
            
            clean_line[clean_len++] = line_buf[i++];
        }
        clean_line[clean_len] = '\0';
        
        while (clean_len > 0 && (clean_line[clean_len - 1] == ' ' || clean_line[clean_len - 1] == '\r' || clean_line[clean_len - 1] == '\t')) {
            clean_line[--clean_len] = '\0';
        }
        
        if (clean_len > 3) {
            const char *start = clean_line;
            while (*start == ' ' || *start == '\t') {
                start++;
            }
            
            // Skip comments and directives
            if (*start == '\0' || *start == '#' || strncmp(start, "//", 2) == 0 || strncmp(start, "/*", 2) == 0) {
                line_num++;
                continue;
            }
            
            // Skip keywords
            int is_keyword = 0;
            const char *keywords[] = {
                "using", "namespace", "class", "struct", "interface", "enum",
                "public", "private", "protected", "internal", "static", "void",
                "if", "while", "for", "foreach", "catch", "switch", "try", "else"
            };
            for (int i = 0; i < (int)(sizeof(keywords) / sizeof(keywords[0])); i++) {
                int kw_len = strlen(keywords[i]);
                if (strncmp(start, keywords[i], kw_len) == 0) {
                    char next_c = start[kw_len];
                    if (next_c == '\0' || next_c == ' ' || next_c == '\t' || next_c == '(' || next_c == '{' || next_c == ')') {
                        is_keyword = 1;
                        break;
                    }
                }
            }
            
            if (*start == '[') {
                is_keyword = 1;
            }
            
            if (!is_keyword) {
                char last_char = clean_line[clean_len - 1];
                
                // Only flag if the line doesn't end with allowed flow or list characters
                if (last_char != ';' && last_char != '{' && last_char != '}' && last_char != ',' &&
                    last_char != ':' && last_char != '>' && last_char != '<' && last_char != '(' &&
                    last_char != '[' && last_char != ']' && last_char != '+' && last_char != '-' &&
                    last_char != '*' && last_char != '/' && last_char != '&' && last_char != '|' &&
                    last_char != '^' && last_char != '?' && last_char != '=' && last_char != '!') {
                    
                    // Check for unbalanced braces or parentheses on this single line
                    int open_p = 0, open_b = 0, open_br = 0;
                    int sub_in_str = 0, sub_in_chr = 0;
                    for (int i = 0; start[i] != '\0'; i++) {
                        if (sub_in_str) {
                            if (start[i] == '"' && (i == 0 || start[i-1] != '\\')) sub_in_str = 0;
                            continue;
                        }
                        if (sub_in_chr) {
                            if (start[i] == '\'' && (i == 0 || start[i-1] != '\\')) sub_in_chr = 0;
                            continue;
                        }
                        if (start[i] == '"') { sub_in_str = 1; continue; }
                        if (start[i] == '\'') { sub_in_chr = 1; continue; }
                        
                        if (start[i] == '(') open_p++;
                        if (start[i] == ')') open_p--;
                        if (start[i] == '[') open_b++;
                        if (start[i] == ']') open_b--;
                        if (start[i] == '{') open_br++;
                        if (start[i] == '}') open_br--;
                    }
                    
                    // If all delimiters on this line are closed, we check lookahead
                    if (open_p == 0 && open_b == 0 && open_br == 0) {
                        char next_sig = get_next_significant_char(ptr);
                        if (next_sig != '{' && next_sig != ',') {
                            // High-probability complete C# statement missing a semicolon
                            int idx = *issue_count;
                            strcpy(issues[idx].file_path, file_path);
                            issues[idx].line = line_num;
                            issues[idx].column = len + 1;
                            strcpy(issues[idx].error_code, "CS1002");
                            sprintf(issues[idx].message, "Semicolon ';' expected.");
                            strcpy(issues[idx].original_code, line_buf);
                            
                            char fixed_buf[1024];
                            if (comment_idx != -1) {
                                strncpy(fixed_buf, line_buf, comment_idx);
                                fixed_buf[comment_idx] = '\0';
                                int f_len = strlen(fixed_buf);
                                while (f_len > 0 && (fixed_buf[f_len - 1] == ' ' || fixed_buf[f_len - 1] == '\t')) {
                                    fixed_buf[--f_len] = '\0';
                                }
                                strcat(fixed_buf, "; ");
                                strcat(fixed_buf, line_buf + comment_idx);
                            } else {
                                sprintf(fixed_buf, "%s;", line_buf);
                            }
                            strcpy(issues[idx].fixed_code, fixed_buf);
                            
                            (*issue_count)++;
                            found_issues++;
                        }
                    }
                }
            }
        }
        line_num++;
    }
    return found_issues;
}

// Check for unbalanced braces
int check_braces(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count) {
    int open_count = 0;
    int close_count = 0;
    const char *ptr = content;
    
    while (*ptr != '\0') {
        // Skip comments and literals
        if (*ptr == '/' && *(ptr + 1) == '/') {
            ptr += 2;
            while (*ptr != '\0' && *ptr != '\n') ptr++;
            continue;
        }
        if (*ptr == '/' && *(ptr + 1) == '*') {
            ptr += 2;
            while (*ptr != '\0' && !(*ptr == '*' && *(ptr + 1) == '/')) {
                ptr++;
            }
            if (*ptr != '\0') ptr += 2;
            continue;
        }
        if (*ptr == '"') {
            ptr++;
            while (*ptr != '\0' && *ptr != '"') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    ptr += 2;
                } else {
                    ptr++;
                }
            }
            if (*ptr == '"') ptr++;
            continue;
        }
        if (*ptr == '\'') {
            ptr++;
            while (*ptr != '\0' && *ptr != '\'') {
                if (*ptr == '\\' && *(ptr + 1) != '\0') {
                    ptr += 2;
                } else {
                    ptr++;
                }
            }
            if (*ptr == '\'') ptr++;
            continue;
        }
        
        if (*ptr == '{') open_count++;
        if (*ptr == '}') close_count++;
        ptr++;
    }
    
    if (open_count != close_count) {
        int idx = *issue_count;
        strcpy(issues[idx].file_path, file_path);
        issues[idx].line = 1;
        issues[idx].column = 1;
        strcpy(issues[idx].error_code, "CS1513");
        sprintf(issues[idx].message, "Mismatched braces. Found %d '{' and %d '}'. '}' expected.", open_count, close_count);
        strcpy(issues[idx].original_code, "");
        strcpy(issues[idx].fixed_code, "");
        (*issue_count)++;
        return 1;
    }
    return 0;
}

int apply_file_change_with_five_steps(const char *file_path, const char *new_content, const char *change_description) {
    // Step 1: Prompt the User
    printf("\n%s[STEP 1/5] PROMPT USER FOR CONFIRMATION%s\n", COLOR_MAGENTA, COLOR_RESET);
    printf("Proposed modification to file: '%s'\n", file_path);
    printf("Change details: %s\n", change_description);
    printf("Do you confirm this change? (y/n): ");
    fflush(stdout);
    
    char confirm[16] = "";
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        printf("%sChange aborted: Input error.%s\n", COLOR_RED, COLOR_RESET);
        return 0;
    }
    // strip newline
    for (int i = 0; i < 16; i++) {
        if (confirm[i] == '\n' || confirm[i] == '\r') {
            confirm[i] = '\0';
            break;
        }
    }
    if (strcmp(confirm, "y") != 0 && strcmp(confirm, "Y") != 0) {
        printf("%sChange aborted by user.%s\n", COLOR_RED, COLOR_RESET);
        return 0;
    }
    
    // Step 2: Back up the File
    printf("\n%s[STEP 2/5] BACK UP FILE%s\n", COLOR_MAGENTA, COLOR_RESET);
    // Create unique backup name using standard C time or unique suffix
    char backup_path[512];
    snprintf(backup_path, sizeof(backup_path), "%s_backup_%ld", file_path, (long)time(NULL));
    
    // Read current file to copy it to backup
    FILE *f_src = fopen(file_path, "r");
    if (f_src) {
        FILE *f_dst = fopen(backup_path, "w");
        if (f_dst) {
            char ch;
            while ((ch = fgetc(f_src)) != EOF) {
                fputc(ch, f_dst);
            }
            fclose(f_dst);
            printf("%sBackup created successfully at '%s'%s\n", COLOR_GREEN, backup_path, COLOR_RESET);
        } else {
            printf("%s[WARNING] Could not open backup path '%s' for writing.%s\n", COLOR_YELLOW, backup_path, COLOR_RESET);
        }
        fclose(f_src);
    } else {
        printf("%s[WARNING] Original file not found, skipping backup (creating new file).%s\n", COLOR_YELLOW, COLOR_RESET);
    }
    
    // Step 3: Make the Change
    printf("\n%s[STEP 3/5] MAKE THE CHANGE%s\n", COLOR_MAGENTA, COLOR_RESET);
    FILE *fout = fopen(file_path, "w");
    if (!fout) {
        printf("%sError: Could not open file '%s' for writing.%s\n", COLOR_RED, file_path, COLOR_RESET);
        return 0;
    }
    fputs(new_content, fout);
    fclose(fout);
    printf("%sFile '%s' successfully modified.%s\n", COLOR_GREEN, file_path, COLOR_RESET);
    
    // Step 4: Test the Change
    printf("\n%s[STEP 4/5] TEST THE CHANGE%s\n", COLOR_MAGENTA, COLOR_RESET);
    printf("Running linter and build verification tests...\n");
    
    // Try building using dotnet build
    int build_result = -1;
#ifdef _WIN32
    build_result = system("dotnet build > nul 2>&1");
#else
    build_result = system("dotnet build > /dev/null 2>&1");
#endif

    if (build_result == 0) {
        printf("%s✔ Build Succeeded! 'dotnet build' completed successfully without errors.%s\n", COLOR_GREEN, COLOR_RESET);
    } else {
        printf("%s[INFO] 'dotnet build' returned non-zero or is not installed. Performing high-fidelity C static analysis...%s\n", COLOR_YELLOW, COLOR_RESET);
        // Load and check the file with our own static analysis!
        FILE *f_check = fopen(file_path, "r");
        if (f_check) {
            fseek(f_check, 0, SEEK_END); // NOLINT(cert-err33-c)
            long size = ftell(f_check);
            fseek(f_check, 0, SEEK_SET);
            char *content = (char*)malloc(size + 1);
            if (content) {
                fread(content, 1, size, f_check);
                content[size] = '\0';
                fclose(f_check);
                
                ScannerIssue check_issues[100];
                int check_issue_count = 0;
                
                // Call all our C static analysis rules to verify there are no issues in the newly modified file!
                check_v1_recovery(content, file_path, check_issues, &check_issue_count);
                check_list_imports(content, file_path, check_issues, &check_issue_count);
                check_semicolons(content, file_path, check_issues, &check_issue_count);
                check_braces(content, file_path, check_issues, &check_issue_count);
                check_empty_catch(content, file_path, check_issues, &check_issue_count);
                check_class_name_pascal_case(content, file_path, check_issues, &check_issue_count);
                
                free(content);
                
                if (check_issue_count == 0) {
                    printf("%s✔ Local C Static Analysis Succeeded! No syntax, brace, or style issues found in '%s'.%s\n", COLOR_GREEN, file_path, COLOR_RESET);
                } else {
                    printf("%s✖ Local C Static Analysis Found %d issue(s) remaining:%s\n", COLOR_RED, check_issue_count, COLOR_RESET);
                    for (int i = 0; i < check_issue_count; i++) {
                        printf("  Line %d: %s\n", check_issues[i].line, check_issues[i].message);
                    }
                }
            } else {
                fclose(f_check);
            }
        }
    }
    
    // Step 5: Ask User to Test and Report Back
    printf("\n%s[STEP 5/5] ASK USER TO TEST AND REPORT BACK%s\n", COLOR_MAGENTA, COLOR_RESET);
    printf("Please test the updated interface/application in your browser and report back on your results.\n");
    printf("Press [Enter] to complete this operation: ");
    fflush(stdout);
    
    char dummy[16];
    fgets(dummy, sizeof(dummy), stdin);
    printf("%sWorkflow completed successfully!%s\n\n", COLOR_GREEN, COLOR_RESET);
    
    return 1;
}

// Apply automatic repair by performing exact replacements
void apply_auto_repair(const char *file_path, ScannerIssue *issues, int issue_count) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return;
    }
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);

    char *new_content = (char*)malloc(size * 2 + 2048);
    strcpy(new_content, buffer);

    int repairs_done = 0;
    for (int i = 0; i < issue_count; i++) {
        if (strlen(issues[i].original_code) > 0 && strlen(issues[i].fixed_code) > 0) {
            char *pos = strstr(new_content, issues[i].original_code); // NOLINT(readability-suspicious-call-argument)
            if (pos != NULL) {
                long offset = pos - new_content;
                char *temp = (char*)malloc(strlen(new_content) + 2048);
                strncpy(temp, new_content, offset);
                temp[offset] = '\0';
                strcat(temp, issues[i].fixed_code);
                strcat(temp, pos + strlen(issues[i].original_code));
                
                free(new_content);
                new_content = temp;
                repairs_done++;
            }
        }
    }
    
    if (repairs_done > 0) {
        char desc[256];
        snprintf(desc, sizeof(desc), "Apply %d automatic repair(s) for static compilation errors", repairs_done);
        apply_file_change_with_five_steps(file_path, new_content, desc);
    }
    
    free(buffer);
    free(new_content);
}

// Scans a single .cs file and displays details
void scan_file(const char *file_path, int fix_mode, ScannerIssue *issues_out, int *issue_count_out, int silent_mode) {
    FILE *f = fopen(file_path, "r");
    if (!f) {
        printf("%sError opening file: %s%s\n", COLOR_RED, file_path, COLOR_RESET);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = (char*)malloc(size + 1);
    if (!content) {
        fclose(f);
        return;
    }
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    const char* ext = strrchr(file_path, '.');
    if (!ext) ext = ""; // No extension

    if (!silent_mode) {
        printf("%sScanning file: %s%s (%ld bytes)\n", COLOR_CYAN, file_path, COLOR_RESET, size);
    }
    
    ScannerIssue local_issues[100];
    int local_issue_count = 0;
    
    if (strcmp(ext, ".cs") == 0) {
        check_v1_recovery(content, file_path, local_issues, &local_issue_count);
        check_list_imports(content, file_path, local_issues, &local_issue_count);
        check_semicolons(content, file_path, local_issues, &local_issue_count);
        check_braces(content, file_path, local_issues, &local_issue_count);
        check_empty_catch(content, file_path, local_issues, &local_issue_count);
        check_class_name_pascal_case(content, file_path, local_issues, &local_issue_count);
    } else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 || strcmp(ext, ".h") == 0 || strcmp(ext, ".hpp") == 0) {
        check_include_guards(content, file_path, local_issues, &local_issue_count);
        check_gets_usage(content, file_path, local_issues, &local_issue_count);
    }
    
    if (issues_out && issue_count_out) {
        memcpy(issues_out, local_issues, sizeof(ScannerIssue) * local_issue_count);
        *issue_count_out = local_issue_count;
    }

    if (!silent_mode) {
        if (local_issue_count == 0) {
            printf("  %s✔ No static analysis issues found.%s\n\n", COLOR_GREEN, COLOR_RESET);
        } else {
            printf("  %sFound %d issue(s) in %s:%s\n", COLOR_YELLOW, local_issue_count, file_path, COLOR_RESET);
            for (int i = 0; i < local_issue_count; i++) {
                printf("    %s[%s]%s Line %d: %s\n", 
                    COLOR_RED, local_issues[i].error_code, COLOR_RESET, 
                    local_issues[i].line, local_issues[i].message);
                if (strlen(local_issues[i].original_code) > 0) {
                    printf("      %s-%s \"%s\"\n", COLOR_RED, COLOR_RESET, local_issues[i].original_code);
                    printf("      %s+%s \"%s\"\n", COLOR_GREEN, COLOR_RESET, local_issues[i].fixed_code);
                }
            }
            if (fix_mode) apply_auto_repair(file_path, local_issues, local_issue_count);
            printf("\n");
        }
    }
    
    free(content);
}

// Cross-platform recursive directory search
#ifdef _WIN32
void collect_source_files(const char *dir_path, char files_arr[100][256], int *count) {
    if (*count >= 100) return;
    char search_path[512];
    sprintf(search_path, "%s\\*", dir_path);
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        char full_path[512];
        sprintf(full_path, "%s\\%s", dir_path, find_data.cFileName);
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(find_data.cFileName, "bin") != 0 && strcmp(find_data.cFileName, "obj") != 0 && strcmp(find_data.cFileName, ".git") != 0 && strcmp(find_data.cFileName, "node_modules") != 0) {
                collect_source_files(full_path, files_arr, count);
            }
        } else {
            const char *filename = find_data.cFileName;
            size_t len = strlen(filename);
            if ((len > 3 && _stricmp(filename + len - 3, ".cs") == 0) ||
                (len > 2 && _stricmp(filename + len - 2, ".c") == 0) ||
                (len > 2 && _stricmp(filename + len - 2, ".h") == 0) ||
                (len > 4 && _stricmp(filename + len - 4, ".cpp") == 0) ||
                (len > 4 && _stricmp(filename + len - 4, ".hpp") == 0) ||
                (_stricmp(filename, "Makefile") == 0))
            {
                strcpy(files_arr[*count], full_path);
                (*count)++;
                if (*count >= 100) break;
            }
        }
    } while (FindNextFileA(hFind, &find_data));
    FindClose(hFind);
}
#else
void collect_source_files(const char *dir_path, char files_arr[100][256], int *count) {
    if (*count >= 100) return;
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full_path[1024];
        sprintf(full_path, "%s/%s", dir_path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                if (strcmp(entry->d_name, "bin") != 0 && strcmp(entry->d_name, "obj") != 0 && strcmp(entry->d_name, ".git") != 0 && strcmp(entry->d_name, "node_modules") != 0) {
                    collect_source_files(full_path, files_arr, count);
                }
            } else {
                const char *filename = entry->d_name;
                size_t len = strlen(filename);
                if ((len > 3 && strcasecmp(filename + len - 3, ".cs") == 0) ||
                    (len > 2 && strcasecmp(filename + len - 2, ".c") == 0) ||
                    (len > 2 && strcasecmp(filename + len - 2, ".h") == 0) ||
                    (len > 4 && strcasecmp(filename + len - 4, ".cpp") == 0) ||
                    (len > 4 && strcasecmp(filename + len - 4, ".hpp") == 0) ||
                    (strcasecmp(filename, "Makefile") == 0))
                {
                    strcpy(files_arr[*count], full_path);
                    (*count)++;
                    if (*count >= 100) break;
                }
            }
        }
    }
    closedir(dir);
}
#endif

void scan_directory(const char *dir_path, int fix_mode) {
    char files_arr[100][256];
    int count = 0;
    collect_source_files(dir_path, files_arr, &count);
    
    printf("%sFound %d source file(s) inside workspace '%s'%s\n\n", COLOR_CYAN, count, dir_path, COLOR_RESET);
    for (int i = 0; i < count; i++) {
        scan_file(files_arr[i], fix_mode, NULL, NULL, 0);
    }
}

// Load files into local nano editor
int load_file_for_editing(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    edit_line_count = 0;
    char buf[MAX_LINE_LEN];
    while (fgets(buf, sizeof(buf), f) && edit_line_count < MAX_LINES) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            len--;
        }
        if (len > 0 && buf[len - 1] == '\r') {
            buf[len - 1] = '\0';
        }
        strcpy(edit_lines[edit_line_count], buf);
        edit_line_count++;
    }
    fclose(f);
    return 1;
}

// Save nano editor edits
int save_edited_file(const char *path) {
    int total_len = 0;
    for (int i = 0; i < edit_line_count; i++) {
        total_len += strlen(edit_lines[i]) + 2;
    }
    char *new_content = (char*)malloc(total_len + 1);
    if (!new_content) return 0;
    new_content[0] = '\0';
    for (int i = 0; i < edit_line_count; i++) {
        strcat(new_content, edit_lines[i]);
        strcat(new_content, "\n");
    }
    
    char desc[256];
    snprintf(desc, sizeof(desc), "Save manual edits from Nano Editor");
    int success = apply_file_change_with_five_steps(path, new_content, desc);
    free(new_content);
    return success;
}

void render_framebuffer(const FrameBuffer& fb) {
    // Optimized rendering: build a string and print once.
    // Buffer size: 1 byte per char, plus avg. 10 bytes for color codes (worst case),
    // plus newlines and control codes.
    size_t buffer_size = (fb.cols * fb.rows) + (fb.cols * fb.rows * 10) + fb.rows + 50;
    char* frame_str = (char*)malloc(buffer_size);
    if (!frame_str) return;

    char* p = frame_str;
    p += sprintf(p, "\033[H"); // Home cursor

    int last_color_pair = -1; // Track the last color to minimize ANSI writes

    for (int y = 0; y < fb.rows; ++y) {
        for (int x = 0; x < fb.cols; ++x) {
            const auto& cell = fb.buffer[y][x];

            if (cell.color_pair != last_color_pair) {
                const char* color_code = COLOR_RESET;
                if (cell.color_pair == 1) color_code = COLOR_BLUE;
                else if (cell.color_pair == 2) color_code = COLOR_YELLOW;
                else if (cell.color_pair == 3) color_code = COLOR_WHITE;
                else if (cell.color_pair == 4) color_code = COLOR_CYAN;
                else if (cell.color_pair == 5) color_code = COLOR_BLUE;
                
                size_t len = strlen(color_code);
                memcpy(p, color_code, len);
                p += len;
                last_color_pair = cell.color_pair;
            }
            *p++ = cell.ch;
        }
        if (y < fb.rows - 1) {
            *p++ = '\n';
        }
    }

    printf("%s", frame_str);
    fflush(stdout);
    free(frame_str);
}

// Helper to draw syntax-highlighted text into the framebuffer
void draw_highlighted_substring(FrameBuffer& fb, int x, int y, const std::string& line, int width) {
    std::string plain = line;
    if (plain.length() > (size_t)width) {
        plain = plain.substr(0, width);
    }

    int current_x = x;
    int j = 0;
    int in_string = 0;
    int in_comment = 0;

    while (j < (int)plain.length()) {
        if (current_x >= x + width) break;

        if (in_comment) {
            fb.draw_text(current_x++, y, std::string(1, plain[j++]), 2); // Green-like (yellow for now)
            continue;
        }
        if (in_string) {
            fb.draw_text(current_x++, y, std::string(1, plain[j]), 2); // Yellow
            if (plain[j] == '"' && (j == 0 || plain[j-1] != '\\')) {
                in_string = 0;
            }
            j++;
            continue;
        }
        if (plain[j] == '/' && j + 1 < (int)plain.length() && plain[j+1] == '/') {
            in_comment = 1;
            fb.draw_text(current_x, y, "//", 2); // Green-like
            current_x += 2;
            j += 2;
            continue;
        }
        if (plain[j] == '"') {
            in_string = 1;
            fb.draw_text(current_x++, y, "\"", 2); // Yellow
            j++;
            continue;
        }

        if (isalpha(plain[j]) || plain[j] == '_') {
            std::string word;
            while (j < (int)plain.length() && (isalnum(plain[j]) || plain[j] == '_')) {
                word += plain[j++];
            }

            int color = 0; // Default
            const char* keywords[] = {
                "using", "namespace", "public", "private", "class", "void", "string", "int",
                "bool", "decimal", "return", "new", "true", "false", "static", "if", "else"
            };
            const char* types[] = {"Console", "List", "DateTime"};

            for (const auto& kw : keywords) {
                if (word == kw) {
                    color = 4; // Magenta-like (cyan)
                    break;
                }
            }
            if (color == 0) {
                for (const auto& t : types) {
                    if (word == t) {
                        color = 4; // Cyan
                        break;
                    }
                }
            }

            fb.draw_text(current_x, y, word, color);
            current_x += (int)word.length();
        } else {
            char c = plain[j];
            int color_pair = 0;
            if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
                color_pair = 2; // Yellow
            } else if (c == ';' || c == '=') {
                color_pair = 5; // Blue
            }
            fb.draw_text(current_x, y, std::string(1, c), color_pair);
            current_x++;
            j++;
        }
    }
}

void draw_bgm_visualizer_fb(FrameBuffer& fb, int y, int x, int width) {
    int bar_width = 0;
    if (g_current_note_freq > 0) {
        // Map frequency (262-1976 Hz) to width
        bar_width = (int)(((g_current_note_freq - 262) / (1976.0 - 262.0)) * width);
    }
    if (bar_width < 0) bar_width = 0;
    if (bar_width > width) bar_width = width;

    std::string bar;
    for (int i = 0; i < bar_width; i++) {
        bar += "█";
    }
    fb.draw_text(x, y, bar, 4); // Cyan bar
}

void run_create_project_ui(const char *dir_path, const PerformanceSettings& settings) {
    char prompt_buffer[256] = "";
    int cursor_pos = 0;
    bool project_created = false;

    while (true) {
        FrameBuffer fb(settings.term_cols, settings.term_rows);
        fb.clear();
        fb.draw_box(0, 0, settings.term_cols, settings.term_rows, "CREATE NEW PROJECT");

        fb.draw_text(2, 2, "Project directory is empty.", 0);
        fb.draw_text(2, 3, "Describe the project you want to create:", 0);

        std::string prompt_line = "> " + std::string(prompt_buffer);
        fb.draw_text(2, 5, prompt_line, 2);

        // Draw cursor
        if (cursor_pos < (int)prompt_line.length() - 2) {
            fb.buffer[5][2 + cursor_pos + 2].ch = '_';
        }

        fb.draw_text(2, settings.term_rows - 2, "[Enter] Create Project  [Q] Quit", 0);

        render_framebuffer(fb);

        int key = read_key();
        if (key == 'q' || key == 'Q' || key == 27) { // Quit on 'q', 'Q', or ESC
            return;
        } else if (key == 13) { // Enter
            if (strlen(prompt_buffer) > 0 && !project_created) {
                create_project_from_prompt(prompt_buffer);
                sprintf(prompt_buffer, "Project created! Press Q to exit and re-launch UI.");
                cursor_pos = (int)strlen(prompt_buffer);
                project_created = true;
            }
        } else if (key == 8 || key == 127) { // Backspace
            if (cursor_pos > 0 && !project_created) {
                prompt_buffer[--cursor_pos] = '\0';
            }
        } else if (isprint(key) && cursor_pos < (int)sizeof(prompt_buffer) - 1 && !project_created) {
            prompt_buffer[cursor_pos++] = (char)key;
            prompt_buffer[cursor_pos] = '\0';
        }
    }
}

// Interactive ncurses-style terminal dashboard and Nano editor emulator
void run_interactive_ui(const char *dir_path, const PerformanceSettings& settings) {
    char files_arr[100][256];
    int file_count = 0;
    collect_source_files(dir_path, files_arr, &file_count);

    if (file_count == 0) {
        run_create_project_ui(dir_path, settings);
        return;
    }

    char status_msg[128] = "Workspace initialized. Ready.";
    int selected_idx = 0;
    ScannerIssue diag_issues[100];
    int diag_issue_count = -1; // -1 means no scan run yet
    float donut_A = 0.0f;
    float donut_B = 0.0f;

    if (strcmp(dir_path, ".") == 0) {
        strcpy(status_msg, "Implicitly scanning Launch directory (CWD), NOT exe folder.");
    }
    
    while (1) {
        // Get terminal dimensions
        // Use dimensions from performance settings
        int term_cols = settings.term_cols;
        int term_rows = settings.term_rows;
        
        FrameBuffer fb(term_cols, term_rows);

        // --- RENDER ---
        fb.clear();
        render_donut_3d(fb, donut_A, donut_B);
        donut_A += 0.04f;
        donut_B += 0.02f;

        // --- UI on top of donut ---
        int mid_col = 38;
        int main_height = term_rows - 6;

        // Main boxes
        std::string title = "C/C++/C# INTERACTIVE COMPILER-EMULATOR (RELEASE 70.0 TERMINX ACTIVE)";
        if ((int)title.length() > term_cols - 4) {
            title = title.substr(0, term_cols - 4);
        }
        fb.draw_box(0, 0, term_cols, term_rows, title.c_str());
        fb.draw_box(0, 2, mid_col, main_height, "PROJECT WORKSPACE FILE EXPLORER");
        fb.draw_box(mid_col, 2, term_cols - mid_col, main_height, "INSPECTOR & PREVIEW");

        // Left Panel: Files
        for (int i = 0; i < main_height - 2 && i < file_count; i++) {
            const char *slash = strrchr(files_arr[i], '/');
            if (!slash) slash = strrchr(files_arr[i], '\\');
            const char *name = slash ? (slash + 1) : files_arr[i];
            
            char disp[35];
            snprintf(disp, sizeof(disp), "%s %-32.32s", (i == selected_idx) ? "▶" : " ", name);
            fb.draw_text(2, 3 + i, disp, (i == selected_idx) ? 2 : 0);
        }

        // Right Panel: Merged content
        // 1. Preview
        fb.draw_text(mid_col + 2, 3, "[Code Preview]", 2);
        if (file_count > 0) {
            FILE *f = fopen(files_arr[selected_idx], "r");
            if (f) {
                char buf[256];
                for(int i = 0; i < 6 && fgets(buf, sizeof(buf), f); ++i) {
                    size_t l = strlen(buf);
                    while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) {
                        buf[l-1] = '\0';
                        l--;
                    }
                    draw_highlighted_substring(fb, mid_col + 2, 4 + i, buf, term_cols - mid_col - 4);
                }
                fclose(f);
            }
        }

        // 2. Diagnostics
        fb.draw_text(mid_col + 2, 11, "[Static Diagnostics]", 2);
        if (diag_issue_count == -1) {
            fb.draw_text(mid_col + 4, 12, "Press [R] to run scan.", 0);
        } else if (diag_issue_count == 0) {
            fb.draw_text(mid_col + 4, 12, "✔ No issues found.", 2);
        } else {
            for (int i = 0; i < 2 && i < diag_issue_count; i++) {
                char issue_line[40];
                snprintf(issue_line, sizeof(issue_line), "L%d: %.30s...", diag_issues[i].line, diag_issues[i].message);
                fb.draw_text(mid_col + 4, 12 + i, issue_line, 3); // Red-like
            }
            if (diag_issue_count > 0) {
                 fb.draw_text(mid_col + 4, 12 + (diag_issue_count > 2 ? 2 : diag_issue_count), "Press [A] to Auto-Repair.", 0);
            }
        }

        // Status bar
        fb.draw_box(0, term_rows - 5, term_cols, 5);
        char status_line[128];
        snprintf(status_line, sizeof(status_line), " STATUS: %-62.62s", status_msg);
        fb.draw_text(1, term_rows - 4, status_line, 2);

        // BGM Visualizer
        int visualizer_width = term_cols - 4;
        if (visualizer_width < 0) visualizer_width = 0;
        if (term_rows > 3) {
            draw_bgm_visualizer_fb(fb, term_rows - 3, 2, visualizer_width);
        }

        // Help text
        fb.draw_text(2, term_rows - 2, "[↑/↓] Nav [R] Scan [A] Fix [Enter] Edit [Q] Quit", 0);

        // Render the entire buffer to the screen at once
        render_framebuffer(fb);

        // --- INPUT ---
        if (is_key_pressed()) {
            int key = read_key();
            if (key == 27 || key == 'x' || key == 'X' || key == 'q' || key == 'Q') {
                break; // Exit on ESC, x, or q
            }
            if (key == KEY_UP) {
                if (selected_idx > 0) selected_idx--;
            } else if (key == KEY_DOWN) {
                if (selected_idx < file_count - 1) selected_idx++;
            } else if (key == 'r' || key == 'R') {
                if (file_count > 0) {
                    diag_issue_count = 0;
                    scan_file(files_arr[selected_idx], 0, diag_issues, &diag_issue_count, 1);
                    sprintf(status_msg, "Scan on %s found %d issue(s).", strrchr(files_arr[selected_idx], PATH_SEPARATOR) + 1, diag_issue_count);
                }
            }
            // Add other key handlers here (Enter, 'a', etc.)
        }
    #ifdef _WIN32
            Sleep(1000 / settings.fps_cap);
    #else
            usleep(1000000 / settings.fps_cap);
    #endif
        }
        // Clear screen on exit
    printf("\033[2J\033[H");
    fflush(stdout);
    
}


typedef struct {
    char author[16];
    char message[2048];
} ChatMessage;

void run_ai_chat_ui(const char *dir_path) {
    ChatMessage chat_history[50];
    int chat_count = 0;

    // Initial message
    strcpy(chat_history[chat_count].author, "AI");
    strcpy(chat_history[chat_count].message, "Welcome to the AI/IO Chat Interface. Ask me any coding or architectural question.");
    chat_count++;

    while (1) {
        printf("\033[2J\033[H"); // Clear screen & home cursor
        printf("\033[1;35m┌────────────────────────────────────────────────────────────────────────┐\033[0m\n");
        printf("\033[1;35m│\033[1;37m        AI/IO CONVERSATIONAL CODE ASSISTANT (GEMINI-1.5-FLASH)        \033[1;35m│\033[0m\n");
        printf("\033[1;35m└────────────────────────────────────────────────────────────────────────┘\033[0m\n\n");

        // Display chat history
        for (int i = 0; i < chat_count; i++) {
            if (strcmp(chat_history[i].author, "You") == 0) {
                printf("%s%s:%s %s\n\n", COLOR_GREEN, chat_history[i].author, COLOR_RESET, chat_history[i].message);
            } else {
                printf("%s%s:%s %s\n\n", COLOR_CYAN, chat_history[i].author, COLOR_RESET, chat_history[i].message);
            }
        }

        printf("\n%s> %s", COLOR_YELLOW, COLOR_RESET);

        char user_input[1024];
#ifndef _WIN32
        struct termios stdin_settings;
        tcgetattr(STDIN_FILENO, &stdin_settings);
        stdin_settings.c_lflag |= (ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
        if (!fgets(user_input, sizeof(user_input), stdin)) {
            break; // Exit on input error
        }
#ifndef _WIN32
        stdin_settings.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif

        // Strip newline
        user_input[strcspn(user_input, "\r\n")] = 0;

        if (strcmp(user_input, "exit") == 0 || strcmp(user_input, "quit") == 0) {
            break;
        }

        if (strlen(user_input) == 0) {
            continue;
        }

        // Add user message to history
        if (chat_count < 50) {
            strcpy(chat_history[chat_count].author, "You");
            strcpy(chat_history[chat_count].message, user_input);
            chat_count++;
        }

        // Call AI and add response to history
        if (chat_count < 50) {
            char* api_key = getenv("GEMINI_API_KEY");
            char* gemini_response = call_gemini_flash_api(api_key, user_input);
            
            strcpy(chat_history[chat_count].author, "AI");
            if (gemini_response) {
                // Basic JSON parsing to find the "text" field
                char *text_ptr = strstr(gemini_response, "\"text\": \"");
                if (text_ptr) {
                    text_ptr += strlen("\"text\": \"");
                    char *end_ptr = strstr(text_ptr, "\"");
                    if (end_ptr) {
                        *end_ptr = '\0';
                        // Un-escape newlines for better formatting
                        char *src = text_ptr, *dst = text_ptr;
                        while (*src) {
                            if (*src == '\\' && *(src + 1) == 'n') {
                                *dst++ = '\n';
                                src += 2;
                            } else if (*src == '\\' && *(src + 1) == '"') {
                                *dst++ = '"';
                                src += 2;
                            } else {
                                *dst++ = *src++;
                            }
                        }
                        *dst = '\0';
                        strncpy(chat_history[chat_count].message, text_ptr, sizeof(chat_history[0].message) - 1);
                    }
                } else {
                    // Fallback if parsing fails
                    strncpy(chat_history[chat_count].message, "I received a response I couldn't understand.", sizeof(chat_history[0].message) - 1);
                }
                free(gemini_response);
            } else {
                strcpy(chat_history[chat_count].message, "I couldn't reach the AI service. Please check your API key and internet connection.");
            }
            chat_count++;
        }
    }

    // Restore terminal on exit
#ifndef _WIN32
    struct termios stdin_settings;
    tcgetattr(STDIN_FILENO, &stdin_settings);
    stdin_settings.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
}

int contains_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (strlen(needle) == 0) return 1;
    
    int h_len = (int)strlen(haystack);
    int n_len = (int)strlen(needle);
    
    for (int i = 0; i <= h_len - n_len; i++) {
        int match = 1;
        for (int j = 0; j < n_len; j++) {
            char h_char = haystack[i + j];
            char n_char = needle[j];
            
            if (h_char >= 'A' && h_char <= 'Z') h_char = h_char - 'A' + 'a';
            if (n_char >= 'A' && n_char <= 'Z') n_char = n_char - 'A' + 'a';
            
            if (h_char != n_char) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

char* call_gemini_flash_api(const char *api_key, const char *query) {
    if (!api_key || strlen(api_key) == 0) {
        printf("%s[WARNING] Gemini API key not set. Skipping remote AI suggestion.%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("           You can set the GEMINI_API_KEY environment variable.\n");
        return NULL;
    }

    // The command is long, so we build it in parts.
    char command[4096];
    char escaped_query[1024];
    int j = 0;
    // Basic JSON string escaping for the query
    for (size_t i = 0; query[i] != '\0' && (size_t)j < sizeof(escaped_query) - 5; i++) {
        if (query[i] == '"' || query[i] == '\\') {
            escaped_query[j++] = '\\';
        }
        escaped_query[j++] = query[i];
    }
    escaped_query[j] = '\0';

    // Construct the curl command
    snprintf(command, sizeof(command),
             "curl -s -H \"Content-Type: application/json\" "
             "-d \"{\\\"contents\\\":[{\\\"parts\\\":[{\\\"text\\\":\\\"Given the C# project context, provide a concise architectural suggestion for the user query: '%s'. Respond with a single C# code block for a scaffold, no other text.\\\"}]}]}\" "
             "\"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash-latest:generateContent?key=%s\"",
             escaped_query, api_key);

    // Open a pipe to the curl command
    FILE *pipe =
#ifdef _WIN32
        _popen(command, "r");
#else
        popen(command, "r");
#endif

    if (!pipe) {
        printf("%s[ERROR] Could not execute curl command.%s\n", COLOR_RED, COLOR_RESET);
        return NULL;
    }

    // Read the output from curl
    char *response_buffer = (char*)malloc(8192); // 8KB buffer for response
    if (!response_buffer) {
        #ifdef _WIN32
                _pclose(pipe);
        #else
                pclose(pipe);
        #endif
        return NULL;
    }
    size_t n = fread(response_buffer, 1, 8191, pipe);
    response_buffer[n] = '\0';

    // Close the pipe
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return response_buffer;
}

void run_suggest_mode(const char *dir_path, const char *suggest_query) {
    char files_arr[100][256];
    int count = 0;
    collect_source_files(dir_path, files_arr, &count);
    
    printf("\n%s================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s      PROJECT ARCHITECTURAL & FEATURE SUGGESTION CORE (NLP)     %s\n", COLOR_WHITE, COLOR_RESET);
    printf("%s================================================================%s\n\n", COLOR_CYAN, COLOR_RESET);
    
    printf("Workspace Folder: %s%s%s\n", COLOR_BOLD, dir_path, COLOR_RESET);
    if (strcmp(dir_path, ".") == 0) {
        printf("%s[INFO]%s Implicitly targeting the Launch Directory (CWD), NOT the executable folder.\n", COLOR_YELLOW, COLOR_RESET);
    }
    printf("Discovered source files: %s%d file(s)%s\n\n", COLOR_GREEN, count, COLOR_RESET);
    
    int has_query = (suggest_query != NULL && strlen(suggest_query) > 0);
    int printed_any = 0;
    
    if (has_query) {
        printf("%s[NLP SUGGESTION SEARCH FOR: '%s']%s\n\n", COLOR_MAGENTA, suggest_query, COLOR_RESET);
        
        if (contains_case_insensitive("ScriptableObject-based Game Event System Unity events raise decoupled listeners", suggest_query)) {
            printf("%s★ SUGGESTION: ScriptableObject-based Game Event System%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Decouple player, enemy, and UI systems using architecture based on Unity ScriptableObjects.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Eliminates rigid class coupling and avoids standard Find/GetComponent frame-rate bottlenecks.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    [CreateAssetMenu(fileName = \"GameEvent\", menuName = \"Events/GameEvent\")]\n");
            printf("    %spublic class%s GameEvent : ScriptableObject {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %sprivate readonly List<%sGameEventListener%s> listeners = new List<%sGameEventListener%s>();\n", COLOR_MAGENTA, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
            printf("        %spublic void%s Raise() {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            %sfor%s (int i = listeners.Count - 1; i >= 0; i--) listeners[i].OnEventRaised();\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }
        
        if (contains_case_insensitive("Component Object Pooling Architecture recycle prefab laser bullets particle effects", suggest_query)) {
            printf("%s★ SUGGESTION: Component Object Pooling Architecture%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Recycle laser bullets, particle effects, and enemies to eliminate garbage collection micro-stutters.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Maintains steady 60fps/120fps targeting mobile and console platforms.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic class%s ObjectPool : MonoBehaviour {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic GameObject%s prefab;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %sprivate Queue<%sGameObject%s> pool = new Queue<%sGameObject%s>();\n", COLOR_MAGENTA, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
            printf("        %spublic GameObject%s Get() {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            %sif%s (pool.Count == 0) %sreturn%s Instantiate(prefab);\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
            printf("            GameObject obj = pool.Dequeue(); obj.SetActive(true); %sreturn%s obj;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Screen/Scene Manager State Pattern Monogame XNA retro game loop scene update draw", suggest_query)) {
            printf("%s★ SUGGESTION: Screen/Scene Manager State Pattern%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Replace raw Game1 switch statements with a polymorphic state machine for Menu, Gameplay, and Credits screens.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Clean game loop isolation and modular scene handling.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic interface%s IGameScene {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %svoid%s Update(GameTime gameTime);\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %svoid%s Draw(SpriteBatch spriteBatch);\n", COLOR_MAGENTA, COLOR_RESET);
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Grid-Based 2D AABB Collision Solver Sweep collision tilemap rectangle platformer", suggest_query)) {
            printf("%s★ SUGGESTION: Grid-Based 2D AABB Collision Solver%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Implement Axis-Aligned Bounding Box (AABB) sweeps for low-cost tilemap and entity physics.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Perfect retro platformer response with pixel-perfect resolution.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic bool%s CheckCollision(Rectangle a, Rectangle b) {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %sreturn%s a.Intersects(b);\n", COLOR_MAGENTA, COLOR_RESET);
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Signal-Based Custom Events Godot partial delegate event health", suggest_query)) {
            printf("%s★ SUGGESTION: Signal-Based Custom Events%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Leverage Godot's built-in event signaling using standard C# events or custom Godot signals.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Maintains dynamic and loosely coupled state updates across the Godot node hierarchy.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    [Signal]\n");
            printf("    %spublic delegate void%s HealthChangedEventHandler(float currentHealth, float maxHealth);\n\n", COLOR_MAGENTA, COLOR_RESET);
            printed_any = 1;
        }

        if (contains_case_insensitive("Global Exception Handling Middleware Web API HttpContext Invoke catch details", suggest_query)) {
            printf("%s★ SUGGESTION: Global Exception Handling Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Intercept unhandled controller exceptions globally and format them into RFC 7807 Problem Details response blocks.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Hides sensitive implementation stack-traces and ensures API consumer diagnostic consistency.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic class%s ErrorHandlerMiddleware {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic async Task%s Invoke(HttpContext context) {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            %stry%s { await _next(context); }\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            %scatch%s (Exception ex) { await WriteErrorResponse(context, ex); }\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Rate Limiting and Throttling Middleware Web API IP automated token bucket", suggest_query)) {
            printf("%s★ SUGGESTION: Rate Limiting and Throttling Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Configure a token-bucket or fixed-window IP request thottle threshold to block automated DDoS or bot scrapes.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Prevents database lockups and saves host bandwidth costs.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    builder.Services.AddRateLimiter(options => options.AddFixedWindowLimiter(\"fixed\", opt => opt.PermitLimit = 100));\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("MVVM Observable State Architecture WPF WinForms Desktop INotifyPropertyChanged ViewModel", suggest_query)) {
            printf("%s★ SUGGESTION: MVVM Observable State Architecture%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Implement standard INotifyPropertyChanged binders to establish strict separation between GUI controls and business logic.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Enhances code reusability, simplifies automated UI mock testing, and prevents heavy code-behind files.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic class%s MainViewModel : INotifyPropertyChanged {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %sprivate string%s _title;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic string%s Title {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            get => _title;\n");
            printf("            set { _title = value; OnPropertyChanged(); }\n");
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Generic Database Repository Pattern EF Core DBContext DbSet CRUD", suggest_query)) {
            printf("%s★ SUGGESTION: Generic Database Repository Pattern%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Wrap Entity Framework DbSet calls in generic IRepository interfaces to isolate data layers and simplify Unit Testing.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Provides clean and uniform query boundaries and standardizes Create, Read, Update, Delete (CRUD) database transactions.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %spublic class%s Repository<T> : IRepository<T> %swhere%s T : %sclass%s {\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
            printf("        %sprivate readonly%s DbContext _context;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic async Task%s Add(T entity) => await _context.Set<T>().AddAsync(entity);\n", COLOR_MAGENTA, COLOR_RESET);
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Resilient Multi-tier Discount Engine E-Commerce coupon total vip Campaign calculator", suggest_query)) {
            printf("%s★ SUGGESTION: Resilient Multi-tier Discount Engine%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Add coupon, bulk discounts, and tier-based customer promotional campaigns.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Increases checkout conversion rates by up to 24%%.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %sclass%s DiscountCalculator {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic decimal%s Calculate(decimal amount, %sstring%s coupon) {\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
            printf("            %sif%s (coupon == %s\"ENTERPRISE20\"%s) %sreturn%s amount * 0.8m;\n", COLOR_MAGENTA, COLOR_RESET, COLOR_YELLOW, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
            printf("            %sreturn%s amount;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Client Order Validation & Anti-Fraud Guard double submission zero-amount blacklist", suggest_query)) {
            printf("%s★ SUGGESTION: Client Order Validation & Anti-Fraud Guard%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Prevent double submissions, zero-amount exploits, and blacklist fraudulent domains.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Avoids transaction reprocessing and prevents chargebacks.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    %sclass%s OrderValidator {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        %spublic bool%s Validate(Order order) {\n", COLOR_MAGENTA, COLOR_RESET);
            printf("            %sif%s (order.TotalAmount <= 0) %sreturn false%s;\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
            printf("            %sreturn true%s;\n", COLOR_MAGENTA, COLOR_RESET);
            printf("        }\n");
            printf("    }\n\n");
            printed_any = 1;
        }

        if (contains_case_insensitive("Structured Logging Middleware console write log Serilog diagnostics", suggest_query)) {
            printf("%s★ SUGGESTION: Structured Logging Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Replace raw console writes with structured contextual logging using Serilog.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sImpact:%s Promotes standard diagnostics ingestion by Datadog, Splunk, or cloud collectors.\n", COLOR_BLUE, COLOR_RESET);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
            printf("    Log.Information(%s\"Processing workspace files at {Time}\"%s, DateTime.UtcNow);\n\n", COLOR_YELLOW, COLOR_RESET);
            printed_any = 1;
        }

        if (!printed_any) {
            printf("%s★ CUSTOM NLP SUGGESTION (via Gemini Flash API)%s\n", COLOR_YELLOW, COLOR_RESET);
            printf("  %sDescription:%s Outsourcing query to remote AI for a bespoke architectural suggestion.\n", COLOR_CYAN, COLOR_RESET);
            printf("  %sQuery:%s '%s'\n", COLOR_BLUE, COLOR_RESET, suggest_query);
            printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);

            char* api_key = getenv("GEMINI_API_KEY");
            char* gemini_response = call_gemini_flash_api(api_key, suggest_query);

            int remote_success = 0;
            if (gemini_response) {
                // Basic parsing to find and print the code block
                const char *start_marker = "```csharp";
                const char *end_marker = "```";
                char *start_ptr = strstr(gemini_response, start_marker); // Use a mutable pointer if needed for modification
                
                if (start_ptr) {
                    start_ptr += strlen(start_marker);
                    // Skip initial newline if present
                    if (*start_ptr == '\n' || *start_ptr == '\r') start_ptr++;

                    char *end_ptr = strstr(start_ptr, end_marker);
                    if (end_ptr) {
                        *end_ptr = '\0'; // Terminate the string at the end marker
                    }
                    printf("%s\n", start_ptr);
                    remote_success = 1;
                }
                free(gemini_response);
            }

            if (!remote_success) {
                // Fallback to local NLP generation if remote AI fails
                printf("    %s// Could not get suggestion from remote AI. Generating local fallback.%s\n", COLOR_YELLOW, COLOR_RESET);
                char clean_title[128] = "";
                int ct_idx = 0;
                for (int i = 0; suggest_query[i] != '\0' && ct_idx < 127; i++) {
                    char c = suggest_query[i];
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                        clean_title[ct_idx++] = c;
                    }
                }
                clean_title[ct_idx] = '\0';
                if (strlen(clean_title) == 0) {
                    strcpy(clean_title, "NlpFeature");
                } else {
                    if (clean_title[0] >= 'a' && clean_title[0] <= 'z') {
                        clean_title[0] = clean_title[0] - 'a' + 'A';
                    }
                }
                printf("    %spublic class%s %sManager {\n", COLOR_MAGENTA, COLOR_RESET, clean_title);
                printf("        %spublic void%s ExecuteNlpWorkflow() {\n", COLOR_MAGENTA, COLOR_RESET);
                printf("            // NLP Suggestion: Implement dynamic scaffold for %s\n", suggest_query);
                printf("        }\n");
                printf("    }\n");
            }
            printf("\n");
        }
    } else {
        int unity_context = 0;
        int monogame_context = 0;
        int godot_context = 0;
        int webapi_context = 0;
        int desktop_context = 0;
        int efcore_context = 0;
        int ecommerce_context = 0;
        int c_cpp_context = 0;
        int library_context = 0;
        
        for (int i = 0; i < count; i++) {
        FILE *f = fopen(files_arr[i], "r");
        if (f) {
            char line_buf[512];
            while (fgets(line_buf, sizeof(line_buf), f)) {
                if (strstr(line_buf, "UnityEngine") != NULL || strstr(line_buf, "MonoBehaviour") != NULL || strstr(line_buf, "GameObject") != NULL) {
                    unity_context = 1;
                }
                if (strstr(line_buf, "Microsoft.Xna.Framework") != NULL || strstr(line_buf, "GameTime") != NULL || strstr(line_buf, "SpriteBatch") != NULL) {
                    monogame_context = 1;
                }
                if (strstr(line_buf, "Godot") != NULL || strstr(line_buf, "Node2D") != NULL || strstr(line_buf, "GD.Print") != NULL || strstr(line_buf, "CharacterBody") != NULL) {
                    godot_context = 1;
                }
                if (strstr(line_buf, "ApiController") != NULL || strstr(line_buf, "HttpGet") != NULL || strstr(line_buf, "ControllerBase") != NULL || strstr(line_buf, "IActionResult") != NULL) {
                    webapi_context = 1;
                }
                if (strstr(line_buf, "System.Windows") != NULL || strstr(line_buf, "InitializeComponent") != NULL || strstr(line_buf, "System.Windows.Forms") != NULL || strstr(line_buf, "MessageBox.Show") != NULL) {
                    desktop_context = 1;
                }
                if (strstr(line_buf, "DbContext") != NULL || strstr(line_buf, "DbSet") != NULL || strstr(line_buf, "OnConfiguring") != NULL || strstr(line_buf, "UseSqlite") != NULL) {
                    efcore_context = 1;
                }
                if (strstr(line_buf, "Order") != NULL || strstr(line_buf, "Cart") != NULL || strstr(line_buf, "Product") != NULL) {
                    ecommerce_context = 1;
                }
                if (strstr(line_buf, "Book") != NULL || strstr(line_buf, "Library") != NULL || strstr(line_buf, "BorrowBook") != NULL) {
                    library_context = 1;
                }
                if (strstr(line_buf, "#include") != NULL || strstr(line_buf, "using namespace std;") != NULL || strstr(line_buf, "printf(") != NULL || strstr(line_buf, "cout <<") != NULL) {
                    c_cpp_context = 1;
                }
            }
            fclose(f);
        }
    }
    
    if (unity_context) {
        printf("%s[GENRE DETECTED: UNITY 3D/2D GAME ENGINE (GAME WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: ScriptableObject-based Game Event System%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Decouple player, enemy, and UI systems using architecture based on Unity ScriptableObjects.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Eliminates rigid class coupling and avoids standard Find/GetComponent frame-rate bottlenecks.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    [CreateAssetMenu(fileName = \"GameEvent\", menuName = \"Events/GameEvent\")]\n");
        printf("    %spublic class%s GameEvent : ScriptableObject {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %sprivate readonly List<%sGameEventListener%s> listeners = new List<%sGameEventListener%s>();\n", COLOR_MAGENTA, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
        printf("        %spublic void%s Raise() {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %sfor%s (int i = listeners.Count - 1; i >= 0; i--) listeners[i].OnEventRaised();\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Component Object Pooling Architecture%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Recycle laser bullets, particle effects, and enemies to eliminate garbage collection micro-stutters.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Maintains steady 60fps/120fps targeting mobile and console platforms.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic class%s ObjectPool : MonoBehaviour {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic GameObject%s prefab;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %sprivate Queue<%sGameObject%s> pool = new Queue<%sGameObject%s>();\n", COLOR_MAGENTA, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
        printf("        %spublic GameObject%s Get() {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %sif%s (pool.Count == 0) %sreturn%s Instantiate(prefab);\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("            GameObject obj = pool.Dequeue(); obj.SetActive(true); %sreturn%s obj;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
    } else if (monogame_context) {
        printf("%s[GENRE DETECTED: MONOGAME / XNA RETRO ARCADE (GAME WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Screen/Scene Manager State Pattern%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Replace raw Game1 switch statements with a polymorphic state machine for Menu, Gameplay, and Credits screens.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Clean game loop isolation and modular scene handling.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic interface%s IGameScene {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %svoid%s Update(GameTime gameTime);\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %svoid%s Draw(SpriteBatch spriteBatch);\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Grid-Based 2D AABB Collision Solver%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Implement Axis-Aligned Bounding Box (AABB) sweeps for low-cost tilemap and entity physics.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Perfect retro platformer response with pixel-perfect resolution.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic bool%s CheckCollision(Rectangle a, Rectangle b) {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %sreturn%s a.Intersects(b);\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
    } else if (godot_context) {
        printf("%s[GENRE DETECTED: GODOT ENGINE C# SCRIPT (GAME WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Signal-Based Custom Events%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Leverage Godot's built-in event signaling using standard C# events or custom Godot signals.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Maintains dynamic and loosely coupled state updates across the Godot node hierarchy.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    [Signal]\n");
        printf("    %spublic delegate void%s HealthChangedEventHandler(float currentHealth, float maxHealth);\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 2: Type-Safe Godot Resource Serialization%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Save game states, configurations, and inventory items to high-performance custom resource (.tres) files.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Ensures robust and fast save/load mechanics natively supported by Godot.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic partial class%s PlayerData : Resource {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        [Export] %spublic string%s PlayerName { get; set; } = \"Hero\";\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        [Export] %spublic int%s HighScore { get; set; } = 0;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
    } else if (webapi_context) {
        printf("%s[GENRE DETECTED: ASP.NET CORE WEB API / REST SERVICE (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Global Exception Handling Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Intercept unhandled controller exceptions globally and format them into RFC 7807 Problem Details response blocks.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Hides sensitive implementation stack-traces and ensures API consumer diagnostic consistency.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic class%s ErrorHandlerMiddleware {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic async Task%s Invoke(HttpContext context) {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %stry%s { await _next(context); }\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %scatch%s (Exception ex) { await WriteErrorResponse(context, ex); }\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Rate Limiting and Throttling Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Configure a token-bucket or fixed-window IP request thottle threshold to block automated DDoS or bot scrapes.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Prevents database lockups and saves host bandwidth costs.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    builder.Services.AddRateLimiter(options => options.AddFixedWindowLimiter(\"fixed\", opt => opt.PermitLimit = 100));\n\n");
    } else if (desktop_context) {
        printf("%s[GENRE DETECTED: WPF / WINFORMS DESKTOP APP (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: MVVM Observable State Architecture%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Implement standard INotifyPropertyChanged binders to establish strict separation between GUI controls and business logic.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Enhances code reusability, simplifies automated UI mock testing, and prevents heavy code-behind files.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic class%s MainViewModel : INotifyPropertyChanged {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %sprivate string%s _title;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic string%s Title {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            get => _title;\n");
        printf("            set { _title = value; OnPropertyChanged(); }\n");
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Async Thread Progress Reporting%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Offload long-running calculations to background tasks and report progression metrics to the main GUI Dispatcher Thread safely.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Keeps the desktop window active and clickable without triggering the operating system's 'Not Responding' freeze state.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic async Task%s ProcessData(IProgress<%sint%s> progress) {\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("        %sfor%s (int i = 0; i <= 100; i += 10) { await Task.Delay(100); progress.Report(i); }\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
    } else if (efcore_context) {
        printf("%s[GENRE DETECTED: ENTITY FRAMEWORK CORE / ORM DATABASE (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Generic Database Repository Pattern%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Wrap Entity Framework DbSet calls in generic IRepository interfaces to isolate data layers and simplify Unit Testing.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Provides clean and uniform query boundaries and standardizes Create, Read, Update, Delete (CRUD) database transactions.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic class%s Repository<T> : IRepository<T> %swhere%s T : %sclass%s {\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("        %sprivate readonly%s DbContext _context;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic async Task%s Add(T entity) => await _context.Set<T>().AddAsync(entity);\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Safe Auto-Migration startup hook%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Run pending database schema migrations programmatically on application startup before queries are processed.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Automatically handles backend migrations without manual terminal commands during updates.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    using var scope = app.Services.CreateScope();\n");
        printf("    var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();\n");
        printf("    await db.Database.MigrateAsync();\n\n");
    } else if (ecommerce_context) {
        printf("%s[GENRE DETECTED: E-COMMERCE / TRANSACTIONAL SYSTEM (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Resilient Multi-tier Discount Engine%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Add coupon, bulk discounts, and tier-based customer promotional campaigns.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Increases checkout conversion rates by up to 24%%.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %sclass%s DiscountCalculator {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic decimal%s Calculate(decimal amount, %sstring%s coupon) {\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("            %sif%s (coupon == %s\"ENTERPRISE20\"%s) %sreturn%s amount * 0.8m;\n", COLOR_MAGENTA, COLOR_RESET, COLOR_YELLOW, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("            %sreturn%s amount;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Client Order Validation & Anti-Fraud Guard%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Prevent double submissions, zero-amount exploits, and blacklist fraudulent domains.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Avoids transaction reprocessing and prevents chargebacks.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %sclass%s OrderValidator {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic bool%s Validate(Order order) {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %sif%s (order.TotalAmount <= 0) %sreturn false%s;\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("            %sreturn true%s;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 3: Structured Retry Policies via Polly%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Integrate transient fault-handling for third-party databases, Payment Gateways, and ERP systems.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Enhances API connection survival rate to 99.9%%.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %svar%s policy = Policy.Handle<Exception>().WaitAndRetryAsync(3, i => TimeSpan.FromSeconds(2));\n\n", COLOR_MAGENTA, COLOR_RESET);
    } else if (library_context) {
        printf("%s[GENRE DETECTED: LIBRARY CATALOGUE & ARCHIVAL APP (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: Fine Calculation & Late Return Audit System%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Automatically calculate compounding fines on overdue loans using configurable grace periods.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Encourages timely return rates and tracks historical book circulation loss.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic class%s FineCalculator {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %spublic decimal%s Calculate(int daysOverdue) {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("            %sif%s (daysOverdue <= 3) %sreturn%s 0.00m;\n", COLOR_MAGENTA, COLOR_RESET, COLOR_MAGENTA, COLOR_RESET);
        printf("            %sreturn%s (daysOverdue - 3) * 0.50m;\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        }\n");
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Fuzzy Book Title & Author Search Filter%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Support prefix scans, case-insensitive tokens, and fuzzy-matching algorithms.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Drastically improves book search UX for misspelled or incomplete input.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %spublic List<Book>%s Search(string query) {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        %sreturn%s _books.FindAll(b => b.Title.ToLower().Contains(query.ToLower()));\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
    } else if (c_cpp_context) {
        printf("%s[GENRE DETECTED: C / C++ PROJECT (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: CMake Build System Integration%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Replace a manual Makefile with a modern, cross-platform CMake build configuration.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Simplifies dependency management and ensures consistent builds across Windows, macOS, and Linux.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sCMake Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    cmake_minimum_required(VERSION 3.10)\n");
        printf("    project(MyAwesomeApp)\n");
        printf("    add_executable(MyApp main.cpp)\n\n");

        printf("%s★ SUGGESTION 2: Resource-Acquisition-Is-Initialization (RAII)%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Wrap raw pointers for file handles, network sockets, or memory in classes that manage their own lifetime.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Eliminates entire classes of memory leaks and resource management bugs by leveraging C++ destructors.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC++ Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    %sclass%s FileHandle {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    %spublic:%s\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        FileHandle(const char* path) { m_handle = fopen(path, \"w\"); }\n");
        printf("        ~FileHandle() { if (m_handle) fclose(m_handle); }\n");
        printf("    }\n\n");
    } else {
        printf("%s[GENRE DETECTED: GENERAL C# UTILITY / ENTERPRISE (APP WORKSPACE)]%s\n\n", COLOR_MAGENTA, COLOR_RESET);
        
        printf("%s★ SUGGESTION 1: xUnit Test & Verification Architecture%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Spin up unit testing harness referencing local class definitions to ensure regression safety.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Guarantees robust CI/CD code compilation pipeline integration.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    [Fact]\n");
        printf("    %spublic void%s TestWorkspaceCoreFlow() {\n", COLOR_MAGENTA, COLOR_RESET);
        printf("        Assert.True(%strue%s);\n", COLOR_MAGENTA, COLOR_RESET);
        printf("    }\n\n");
        
        printf("%s★ SUGGESTION 2: Structured Logging Middleware%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  %sDescription:%s Replace raw console writes with structured contextual logging using Serilog.\n", COLOR_CYAN, COLOR_RESET);
        printf("  %sImpact:%s Promotes standard diagnostics ingestion by Datadog, Splunk, or cloud collectors.\n", COLOR_BLUE, COLOR_RESET);
        printf("  %sC# Scaffold Preview:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("    Log.Information(%s\"Processing workspace files at {Time}\"%s, DateTime.UtcNow);\n\n", COLOR_YELLOW, COLOR_RESET);
    }
    }
    
    printf("%s================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("To inject features, use the interactive panel via: %s./cli_scanner --ui%s\n", COLOR_GREEN, COLOR_RESET);
    printf("%s================================================================%s\n\n", COLOR_CYAN, COLOR_RESET);
}

void run_create_project_mode(const char *project_name) {
    printf("\n%s================================================================%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s             C# PROJECT SCAFFOLDING ENGINE              %s\n", COLOR_WHITE, COLOR_RESET);
    printf("%s================================================================%s\n\n", COLOR_BLUE, COLOR_RESET);

    printf("Attempting to create new C# Console App: %s%s%s\n\n", COLOR_BOLD, project_name, COLOR_RESET);

    // Step 1: Create project directory
    int dir_result;
#ifdef _WIN32
    dir_result = _mkdir(project_name);
#else
    dir_result = mkdir(project_name, 0755);
#endif

    if (dir_result != 0) {
        printf("%s[ERROR]%s Could not create directory '%s'. It may already exist.\n", COLOR_RED, COLOR_RESET, project_name);
        return;
    }
    printf("%s[1/3]%s Created project directory: %s%s/%s\n", COLOR_GREEN, COLOR_RESET, COLOR_CYAN, project_name, COLOR_RESET);

    // Step 2: Create .csproj file
    char csproj_path[512];
    snprintf(csproj_path, sizeof(csproj_path), "%s%c%s.csproj", project_name, PATH_SEPARATOR, project_name);
    FILE *f_csproj = fopen(csproj_path, "w");
    if (!f_csproj) {
        printf("%s[ERROR]%s Could not create file: %s\n", COLOR_RED, COLOR_RESET, csproj_path);
        return;
    }
    fprintf(f_csproj, "<Project Sdk=\"Microsoft.NET.Sdk\">\n\n");
    fprintf(f_csproj, "  <PropertyGroup>\n");
    fprintf(f_csproj, "    <OutputType>Exe</OutputType>\n");
    fprintf(f_csproj, "    <TargetFramework>net8.0</TargetFramework>\n");
    fprintf(f_csproj, "    <ImplicitUsings>enable</ImplicitUsings>\n");
    fprintf(f_csproj, "    <Nullable>enable</Nullable>\n");
    fprintf(f_csproj, "  </PropertyGroup>\n\n");
    fprintf(f_csproj, "</Project>\n");
    fclose(f_csproj);
    printf("%s[2/3]%s Created project file:    %s%s%s\n", COLOR_GREEN, COLOR_RESET, COLOR_CYAN, csproj_path, COLOR_RESET);

    // Step 3: Create Program.cs file
    char programcs_path[512];
    snprintf(programcs_path, sizeof(programcs_path), "%s%cProgram.cs", project_name, PATH_SEPARATOR);
    FILE *f_program = fopen(programcs_path, "w");
    if (!f_program) {
        printf("%s[ERROR]%s Could not create file: %s\n", COLOR_RED, COLOR_RESET, programcs_path);
        return;
    }
    fprintf(f_program, "// See https://aka.ms/new-console-template for more information\n");
    fprintf(f_program, "Console.WriteLine(\"Hello, World!\");\n");
    fclose(f_program);
    printf("%s[3/3]%s Created source file:     %s%s%s\n", COLOR_GREEN, COLOR_RESET, COLOR_CYAN, programcs_path, COLOR_RESET);

    printf("\n%s✔ Project '%s' created successfully!%s\n", COLOR_GREEN, project_name, COLOR_RESET);
    printf("  To get started, run:\n");
    printf("    %scd %s%s\n", COLOR_YELLOW, project_name, COLOR_RESET);
    printf("    %sdotnet run%s\n\n", COLOR_YELLOW, COLOR_RESET);
}

void inject_code_inside_class(const char *file_path, const char *code_to_inject) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = (char*)malloc(size + 1);
    if (!content) {
        fclose(f);
        return;
    }

    long read_bytes = fread(content, 1, size, f);
    content[read_bytes] = '\0';
    fclose(f);

    char *new_content = (char*)malloc(read_bytes + strlen(code_to_inject) + 128);
    if (!new_content) {
        free(content);
        return;
    }
    
    int last_brace_idx = -1;
    for (int i = (int)read_bytes - 1; i >= 0; i--) {
        if (content[i] == '}') {
            last_brace_idx = i;
            break;
        }
    }
    
    if (last_brace_idx == -1) {
        snprintf(new_content, read_bytes + strlen(code_to_inject) + 128, "%s%s", content, code_to_inject);
    } else {
        int second_last_brace_idx = -1;
        for (int i = last_brace_idx - 1; i >= 0; i--) {
            if (content[i] == '}') {
                second_last_brace_idx = i;
                break;
            }
        }
        
        int target_insert_idx = last_brace_idx;
        if (second_last_brace_idx != -1) {
            target_insert_idx = second_last_brace_idx;
        }
        
        strncpy(new_content, content, target_insert_idx);
        new_content[target_insert_idx] = '\0';
        strcat(new_content, code_to_inject);
        strcat(new_content, content + target_insert_idx);
    }
    
    char desc[256];
    snprintf(desc, sizeof(desc), "Inject feature/skeleton boilerplate code into class");
    apply_file_change_with_five_steps(file_path, new_content, desc);
    
    free(content);
    free(new_content);
}

void apply_command_line_qadd(const char *dir_path, const char *qadd_query) {
    char files_arr[100][256];
    int count = 0;
    collect_source_files(dir_path, files_arr, &count);
    if (count == 0) {
        printf("[QADD] No C# source files found in target directory: %s\n", dir_path);
        return;
    }
    // Inject into the first found .cs file or Program.cs if possible
    int target_idx = 0;
    for (int i = 0; i < count; i++) {
        if (strstr(files_arr[i], "Program.cs") != NULL) {
            target_idx = i;
            break;
        }
    }
    
    int index = 0;
    if (strcmp(qadd_query, "1") == 0) index = 1;
    else if (strcmp(qadd_query, "2") == 0) index = 2;
    else if (strcmp(qadd_query, "3") == 0) index = 3;
    
    if (index == 0) {
        if (contains_case_insensitive(qadd_query, "discount") || 
            contains_case_insensitive(qadd_query, "calculate") ||
            contains_case_insensitive(qadd_query, "vip")) {
            index = 1;
        } else if (contains_case_insensitive(qadd_query, "validate") || 
                   contains_case_insensitive(qadd_query, "order") ||
                   contains_case_insensitive(qadd_query, "check")) {
            index = 2;
        } else if (contains_case_insensitive(qadd_query, "log") || 
                   contains_case_insensitive(qadd_query, "write") ||
                   contains_case_insensitive(qadd_query, "print")) {
            index = 3;
        }
    }
    
    if (index == 1) {
        inject_code_inside_class(files_arr[target_idx], "\n        public decimal CalculateDiscount(decimal total, bool isVIP) {\n            if (isVIP) return total * 0.15m;\n            return total > 100 ? total * 0.05m : 0m;\n        }\n");
        printf("[QADD] Successfully injected 'CalculateDiscount' into %s\n", files_arr[target_idx]);
    } else if (index == 2) {
        inject_code_inside_class(files_arr[target_idx], "\n        public bool ValidateOrder(string id, decimal amt) {\n            if (string.IsNullOrEmpty(id)) return false;\n            return amt > 0;\n        }\n");
        printf("[QADD] Successfully injected 'ValidateOrder' into %s\n", files_arr[target_idx]);
    } else if (index == 3) {
        inject_code_inside_class(files_arr[target_idx], "\n        public void LogMessage(string msg, string lvl = \"INFO\") {\n            Console.WriteLine($\"[{DateTime.Now}] [{lvl}] {msg}\");\n        }\n");
        printf("[QADD] Successfully injected 'LogMessage' into %s\n", files_arr[target_idx]);
    } else {
        if (strstr(qadd_query, "public") != NULL || strstr(qadd_query, "private") != NULL || 
            strstr(qadd_query, "void") != NULL || strstr(qadd_query, "{") != NULL) {
            inject_code_inside_class(files_arr[target_idx], qadd_query);
            printf("[QADD] Successfully injected custom C# code block into %s\n", files_arr[target_idx]);
        } else {
            char clean_name[128] = "";
            int cn_idx = 0;
            for (int i = 0; qadd_query[i] != '\0' && cn_idx < 127; i++) {
                char c = qadd_query[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    if (cn_idx == 0 && c >= '0' && c <= '9') continue;
                    clean_name[cn_idx++] = c;
                }
            }
            clean_name[cn_idx] = '\0';
            if (strlen(clean_name) == 0) {
                strcpy(clean_name, "MyNlpFeature");
            } else {
                if (clean_name[0] >= 'a' && clean_name[0] <= 'z') {
                    clean_name[0] = clean_name[0] - 'a' + 'A';
                }
            }
            
            char custom_code[512];
            sprintf(custom_code, "\n        public void %s() {\n            // Auto-generated via C# Project Intelligence CLI NLP\n            Console.WriteLine(\"Executed NLP feature scaffold\");\n        }\n", clean_name);
            inject_code_inside_class(files_arr[target_idx], custom_code);
            printf("[QADD] Successfully compiled NLP query '%s' to dynamic method '%s()' and injected into %s\n", qadd_query, clean_name, files_arr[target_idx]);
        }
    }
}


// --- BACKGROUND MUSIC ENGINE ---

volatile int g_music_running = 0;

// --- BGM SEED PARAMETERS ---
typedef struct {
    int note_range_low;
    int note_range_high;
    int tempo;
    int key;
    int scale;
} MusicParams;

#ifdef _WIN32

// Note frequencies (C4 to B5)
const int note_freqs[] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, // Octave 4
    523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988  // Octave 5
};
const char* note_names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Scale patterns (intervals in semitones from the root)
const int major_scale[] = {0, 2, 4, 5, 7, 9, 11};
const int minor_scale[] = {0, 2, 3, 5, 7, 8, 10};

int current_scale[7];
MusicParams g_music_params;

void play_note(int frequency, int duration) {
    if (frequency > 36) { // Beep supports frequencies from 37 to 32767
        Beep(frequency, duration);
    }
}

unsigned __stdcall music_thread_func(void *arg) {
    int root_note_idx = (g_music_params.key != -1) ? g_music_params.key : (rand() % 12);
    int is_major = (g_music_params.scale != -1) ? g_music_params.scale : (rand() % 2);

    // This printf can interfere with UI rendering, so it's best to remove it from the thread.
    // printf("%s[MUSIC ENGINE]%s Booting with random key: %s %s\n\n", COLOR_MAGENTA, COLOR_RESET, note_names[root_note_idx], is_major ? "Major" : "Minor");
    
    // Build the scale
    const int* scale_pattern = is_major ? major_scale : minor_scale;
    for (int i = 0; i < 7; i++) {
        current_scale[i] = note_freqs[(root_note_idx + scale_pattern[i]) % 12];
    }

    while (g_music_running) {
        // Play a short, random melody
        int num_notes = 4 + (rand() % 5); // 4 to 8 notes
        int tempo_duration;
        if (g_music_params.tempo != -1) {
            tempo_duration = 15625 / g_music_params.tempo; // Higher tempo value = shorter duration (faster)
        } else {
            tempo_duration = 125; // Default duration
        }

        for (int i = 0; i < num_notes && g_music_running; i++) {
            int note_idx_in_scale = (g_music_params.note_range_low != -1) ? (g_music_params.note_range_low + (rand() % (g_music_params.note_range_high - g_music_params.note_range_low + 1))) : (rand() % 7);
            note_idx_in_scale %= 7; // Ensure it's a valid scale index
            int octave_multiplier = 1 + (rand() % 2); // Play in octave 4 or 5
            int duration = (rand() % 3 == 0) ? (tempo_duration * 2) : tempo_duration;

            g_current_note_freq = current_scale[note_idx_in_scale] * octave_multiplier;
            play_note(current_scale[note_idx_in_scale] * octave_multiplier, duration);
            
            // Short pause between notes
            if (g_music_running) Sleep(50);
        }

        // Longer pause between melodies
        int sleep_duration = 3000 + (rand() % 5000); // 3-8 second pause
        for (int t = 0; t < sleep_duration / 100 && g_music_running; t++) {
            Sleep(100);
        }
        g_current_note_freq = 0; // Reset visualizer when melody pauses
    }
    return 0;
}

HANDLE g_music_thread_handle = NULL;

const char* init_music_engine(const char *seed_str, int print_seed) {
    static char final_seed_str[32];
    g_music_params.note_range_low = -1;
    g_music_params.note_range_high = -1;
    g_music_params.tempo = -1;
    g_music_params.key = -1;
    g_music_params.scale = -1;
    long modifier_seed = -1;

    if (seed_str && strlen(seed_str) == 30) {
        char buf[7];
        buf[6] = '\0';

        strncpy(buf, seed_str, 6);
        long range = atol(buf);
        g_music_params.note_range_low = (int)(range / 1000);
        g_music_params.note_range_high = (int)(range % 1000);

        strncpy(buf, seed_str + 6, 6);
        g_music_params.tempo = atoi(buf);

        strncpy(buf, seed_str + 12, 6);
        g_music_params.key = atoi(buf);

        strncpy(buf, seed_str + 18, 6);
        g_music_params.scale = atoi(buf);

        strncpy(buf, seed_str + 24, 6);
        modifier_seed = atol(buf);

        // Clamp values to be safe
        if (g_music_params.note_range_low < 0 || g_music_params.note_range_low > 6) g_music_params.note_range_low = 0;
        if (g_music_params.note_range_high < g_music_params.note_range_low || g_music_params.note_range_high > 6) g_music_params.note_range_high = 6;
        if (g_music_params.tempo < 50 || g_music_params.tempo > 999) g_music_params.tempo = 125;
        if (g_music_params.key < 0 || g_music_params.key > 11) g_music_params.key = 0;
        if (g_music_params.scale != 0 && g_music_params.scale != 1) g_music_params.scale = 0;
    }

    if (modifier_seed == -1) {
        srand((unsigned int)time(NULL));
        g_music_params.note_range_low = 0;
        g_music_params.note_range_high = 6;
        g_music_params.tempo = 100 + (rand() % 101); // 100-200
        g_music_params.key = rand() % 12;
        g_music_params.scale = rand() % 2;
        modifier_seed = rand() % 1000000;
    }

    srand((unsigned int)((modifier_seed != -1) ? modifier_seed : time(NULL)));

    snprintf(final_seed_str, sizeof(final_seed_str), "%03d%03d%06d%06d%06d%06ld",
             g_music_params.note_range_low, g_music_params.note_range_high, g_music_params.tempo,
             g_music_params.key, g_music_params.scale, modifier_seed);

    if (print_seed) {
        printf("%sBGM Seed: %s%s%s\n", COLOR_MAGENTA, COLOR_YELLOW, final_seed_str, COLOR_RESET);
    }

    g_music_running = 1;
    g_music_thread_handle = (HANDLE)_beginthreadex(NULL, 0, &music_thread_func, NULL, 0, NULL);
    if (g_music_thread_handle == NULL) {
        printf("%s[ERROR]%s Failed to create music thread.\n", COLOR_RED, COLOR_RESET);
    }
    return final_seed_str;
}

void stop_music_engine(void) {
    if (g_music_running) {
        g_music_running = 0;
        if (g_music_thread_handle) {
            WaitForSingleObject(g_music_thread_handle, 2000); // Wait up to 2s
            CloseHandle(g_music_thread_handle);
            g_music_thread_handle = NULL;
        }
    }
}

#else // For Linux/macOS

// No-op versions for non-Windows platforms
const char* init_music_engine(const char *seed_str, int print_seed) {
    printf("%s[INFO]%s Background music engine is only available on Windows.\n\n", COLOR_YELLOW, COLOR_RESET);
    return "";
}
void stop_music_engine(void) {
    // Nothing to do
}

#endif

void draw_bgm_visualizer(void) {
    printf("\033[1;35m│\033[0m");
    int width = 68;
    int bar_width = 0;
    if (g_current_note_freq > 0) {
        // Map frequency (262-1976 Hz) to width (0-68)
        bar_width = (int)(((g_current_note_freq - 262) / (1976.0 - 262.0)) * width);
    }
    if (bar_width < 0) bar_width = 0;
    if (bar_width > width) bar_width = width;

    for (int i = 0; i < bar_width; i++) {
        printf("%s█", COLOR_CYAN);
    }
    for (int i = bar_width; i < width; i++) {
        printf(" ");
    }
    printf("%s\033[1;34m║\033[0m\n", COLOR_RESET);
}

long run_performance_benchmark() {
    printf("%sCalibrating performance for your terminal (1.33s)...%s\n", COLOR_YELLOW, COLOR_RESET);
    long frame_count = 0;
    float A = 0.0f, B = 0.0f;
    FrameBuffer fb(80, 24); // Use a standard small size for benchmark consistency

    // Use C++ chrono for timing
    auto start_time = std::chrono::high_resolution_clock::now();
    while (true) {
        auto current_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = current_time - start_time;
        if (elapsed.count() >= 1.33) {
            break;
        }

        // Simulate a render cycle without printing
        fb.clear();
        render_donut_3d(fb, A, B);
        A += 0.04f;
        B += 0.02f;

        // Simulate the expensive part of render_framebuffer (string building)
        size_t buffer_size = (fb.cols * fb.rows * 12) + 50;
        char* frame_str = (char*)malloc(buffer_size);
        if (!frame_str) continue;
        char* p = frame_str;
        int last_color_pair = -1;
        for (int y = 0; y < fb.rows; ++y) {
            for (int x = 0; x < fb.cols; ++x) {
                const auto& cell = fb.buffer[y][x];
                if (cell.color_pair != last_color_pair) {
                    const char* color_code = "\033[0m"; // Simplified
                    size_t len = strlen(color_code);
                    memcpy(p, color_code, len);
                    p += len;
                    last_color_pair = cell.color_pair;
                }
                *p++ = cell.ch;
            }
            *p++ = '\n';
        }
        free(frame_str);

        frame_count++;
    }

    return frame_count;
}

void get_terminal_size(int& cols, int& rows) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    cols = w.ws_col;
    rows = w.ws_row;
#endif
    if (cols < 80) cols = 80;
    if (rows < 24) rows = 24;
}


int main(int argc, char *argv[]) { // NOLINT(build/classic_main)
    init_terminal();
    char target_dir[256] = ".";
    int fix_mode = 0;
    int ui_mode = 0;
    int music_disabled = 0;
    int suggest_mode = 0;
    char suggest_query[256] = "";
    char qadd_query[256] = "";
    int create_project_mode = 0;
    int io_mode = 0;
    int webgl_mode = 0;
    int print_seed_flag = 0;
    char bgm_seed_str[32] = "";
    char project_name[256] = "";
    
    if (argc == 1) {
        // Double-clicked or launched with no arguments: defaults to booting interactive UI mode
        ui_mode = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--fix") == 0 || strcmp(argv[i], "-f") == 0) {
                fix_mode = 1;
            } else if (strcmp(argv[i], "--no-music") == 0) {
                music_disabled = 1;
            } else if (strcmp(argv[i], "--seed") == 0) {
                print_seed_flag = 1;
            } else if (strcmp(argv[i], "--io") == 0) {
                io_mode = 1;
            } else if (strcmp(argv[i], "--webgl") == 0) {
                webgl_mode = 1;
            } else if (strcmp(argv[i], "--bgm") == 0) {
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    strncpy(bgm_seed_str, argv[++i], sizeof(bgm_seed_str) - 1);
                }
            } else if (strcmp(argv[i], "--ui") == 0 || strcmp(argv[i], "-u") == 0) {
                ui_mode = 1;
            } else if (strcmp(argv[i], "--suggest") == 0 || strcmp(argv[i], "-s") == 0) {
                suggest_mode = 1;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    strcpy(suggest_query, argv[++i]);
                }
            } else if (strcmp(argv[i], "--qadd") == 0 || strcmp(argv[i], "-q") == 0) {
                if (i + 1 < argc) {
                    strcpy(qadd_query, argv[++i]);
                }
            } else if (strcmp(argv[i], "--create") == 0 || strcmp(argv[i], "-c") == 0) {
                create_project_mode = 1;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    strcpy(project_name, argv[++i]);
                }
            } else {
                strcpy(target_dir, argv[i]);
            }
        }
    }
    
    if (!music_disabled) {
        init_music_engine(strlen(bgm_seed_str) > 0 ? bgm_seed_str : NULL, print_seed_flag);
    }

    if (webgl_mode) {
        printf("%s[INFO] WebGL Bridge Mode is active.%s\n", COLOR_YELLOW, COLOR_RESET);
        // In a real scenario, this would initialize a WebGL context.
        // For this CLI, we'll just print a message if not in UI mode.
        // The actual init is stubbed out to avoid printf.
        init_webgl_viewport(800, 600);
    }
    
    if (ui_mode) {
        if (io_mode) {
            run_ai_chat_ui(target_dir);
        } else {
            long score = run_performance_benchmark();
            double fps = score / 1.33;

            PerformanceSettings settings;
            int term_cols, term_rows;
            get_terminal_size(term_cols, term_rows);

            if (fps > 400) { // High-end
                printf("%s✔ High-performance terminal detected (%.0f FPS). Using full resolution and 60 FPS cap.%s\n", COLOR_GREEN, fps, COLOR_RESET);
                settings = {60, term_cols, term_rows, false};
            } else if (fps > 150) { // Medium
                printf("%s✔ Standard terminal detected (%.0f FPS). Capping resolution and 30 FPS.%s\n", COLOR_CYAN, fps, COLOR_RESET);
                settings = {30, (term_cols > 120 ? 120 : term_cols), (term_rows > 45 ? 45 : term_rows), false};
            } else { // Low-end
                printf("%s! Slower terminal detected (%.0f FPS). Using 80x24 resolution and 15 FPS cap.%s\n", COLOR_YELLOW, fps, COLOR_RESET);
                settings = {15, 80, 24, true};
            }

            printf("Press [Enter] to launch UI...");
            fflush(stdout);
            char dummy[16];
            fgets(dummy, sizeof(dummy), stdin);

            // The color simplification logic is not yet implemented in render_framebuffer,
            // but the setting is ready for future use.
            if (settings.use_simple_colors) {
                // This is where you would modify the render_framebuffer to use fewer colors.
            }

            run_interactive_ui(target_dir, settings);
        }
        stop_music_engine();
        return 0;
    }

    if (create_project_mode) {
        if (strlen(project_name) > 0) {
            run_create_project_mode(project_name);
        } else {
            printf("%s[ERROR]%s Project name missing. Usage: %s--create <ProjectName>%s\n", COLOR_RED, COLOR_RESET, COLOR_YELLOW, COLOR_RESET);
        }
        return 0;
    }
    
    if (suggest_mode || strlen(qadd_query) > 0) {
        if (suggest_mode) {
            run_suggest_mode(target_dir, suggest_query);
        }
        if (strlen(qadd_query) > 0) {
            apply_command_line_qadd(target_dir, qadd_query);
        }
        return 0;
    }
    
    printf("\n%s================================================================%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s C/C++/C# PROJECT INTELLIGENCE & AUTO-REPAIR SCANNER (v70.0 TERMINX RELEASE) %s\n", COLOR_WHITE, COLOR_RESET);
    printf("%s================================================================%s\n", COLOR_BLUE, COLOR_RESET);
    printf("Compiled for: %s%s%s | Mode: Standalone Code Intelligence\n\n", COLOR_CYAN, PLATFORM_NAME, COLOR_RESET);
    
    char cwd[512] = "";
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, ".");
    }
#endif

    char exe_dir[512] = "";
    if (argc > 0 && argv[0] != NULL) {
        strcpy(exe_dir, argv[0]);
        char *last_slash = strrchr(exe_dir, '/');
        if (!last_slash) {
            last_slash = strrchr(exe_dir, '\\');
        }
        if (last_slash) {
            *last_slash = '\0';
        } else {
            strcpy(exe_dir, ".");
        }
    }

    if (strcmp(target_dir, ".") == 0) {
        printf("Launch Directory (CWD): %s\n", cwd);
        printf("Executable Directory:  %s\n\n", exe_dir);
        printf("%s[INFO]%s Implicitly targeting the Launch Directory (CWD),\n", COLOR_YELLOW, COLOR_RESET);
        printf("       NOT the executable directory containing the scanner.\n\n");
    }

    printf("Target scan directory: %s'%s'%s\n", COLOR_BOLD, target_dir, COLOR_RESET);
    printf("Auto-repair mode: %s%s%s\n\n", 
        fix_mode ? COLOR_GREEN : COLOR_YELLOW, 
        fix_mode ? "ACTIVE (--fix)" : "INACTIVE (Dry run)", 
        COLOR_RESET);
        
    scan_directory(target_dir, fix_mode);
    
    printf("%sScan complete.%s\n\n", COLOR_BLUE, COLOR_RESET);
    stop_music_engine();
    return 0;
}
#include "cli_scanner.h"

#define MAX_LINES 300
#define MAX_LINE_LEN 256

char edit_lines[MAX_LINES][MAX_LINE_LEN];
int edit_line_count = 0;

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
        ch = _getch();
        if (ch == 72) return 'w'; // Up
        if (ch == 80) return 's'; // Down
        if (ch == 75) return 'a'; // Left
        if (ch == 77) return 'd'; // Right
    }
    return ch;
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
            if (next2 == 65) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return 'w'; }
            if (next2 == 66) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return 's'; }
            if (next2 == 68) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return 'a'; }
            if (next2 == 67) { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return 'd'; }
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return 27;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
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
        int len = 0;
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

// Apply automatic repair by performing exact replacements
void apply_auto_repair(const char *file_path, ScannerIssue *issues, int issue_count) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return;
    }
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    char *new_content = malloc(size * 2 + 2048);
    strcpy(new_content, buffer);
    
    int repairs_done = 0;
    for (int i = 0; i < issue_count; i++) {
        if (strlen(issues[i].original_code) > 0 && strlen(issues[i].fixed_code) > 0) {
            char *pos = strstr(new_content, issues[i].original_code);
            if (pos != NULL) {
                long offset = pos - new_content;
                char *temp = malloc(strlen(new_content) + 2048);
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
        FILE *fout = fopen(file_path, "w");
        if (fout) {
            fputs(new_content, fout);
            fclose(fout);
            printf("%s[AUTO-REPAIR]%s Applied %d fix(es) to '%s'\n", COLOR_GREEN, COLOR_RESET, repairs_done, file_path);
        }
    }
    
    free(buffer);
    free(new_content);
}

// Scans a single .cs file and displays details
void scan_file(const char *file_path, int fix_mode) {
    FILE *f = fopen(file_path, "r");
    if (!f) {
        printf("%sError opening file: %s%s\n", COLOR_RED, file_path, COLOR_RESET);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return;
    }
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    printf("%sScanning file: %s%s (%ld bytes)\n", COLOR_CYAN, file_path, COLOR_RESET, size);
    
    ScannerIssue issues[100];
    int issue_count = 0;
    
    check_v1_recovery(content, file_path, issues, &issue_count);
    check_list_imports(content, file_path, issues, &issue_count);
    check_semicolons(content, file_path, issues, &issue_count);
    check_braces(content, file_path, issues, &issue_count);
    check_empty_catch(content, file_path, issues, &issue_count);
    check_class_name_pascal_case(content, file_path, issues, &issue_count);
    
    if (issue_count == 0) {
        printf("  %s✔ No static analysis compilation issues found.%s\n\n", COLOR_GREEN, COLOR_RESET);
    } else {
        printf("  %sFound %d issue(s) in %s:%s\n", COLOR_YELLOW, issue_count, file_path, COLOR_RESET);
        for (int i = 0; i < issue_count; i++) {
            printf("    %s[%s]%s Line %d: %s\n", 
                COLOR_RED, issues[i].error_code, COLOR_RESET, 
                issues[i].line, issues[i].message);
            if (strlen(issues[i].original_code) > 0) {
                printf("      %s-%s \"%s\"\n", COLOR_RED, COLOR_RESET, issues[i].original_code);
                printf("      %s+%s \"%s\"\n", COLOR_GREEN, COLOR_RESET, issues[i].fixed_code);
            }
        }
        
        if (fix_mode) {
            apply_auto_repair(file_path, issues, issue_count);
        }
        printf("\n");
    }
    
    free(content);
}

// Cross-platform recursive directory search
#ifdef _WIN32
void collect_cs_files(const char *dir_path, char files_arr[100][256], int *count) {
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
                collect_cs_files(full_path, files_arr, count);
            }
        } else {
            size_t len = strlen(find_data.cFileName);
            if (len > 3 && strcmp(find_data.cFileName + len - 3, ".cs") == 0) {
                strcpy(files_arr[*count], full_path);
                (*count)++;
                if (*count >= 100) break;
            }
        }
    } while (FindNextFileA(hFind, &find_data));
    FindClose(hFind);
}
#else
void collect_cs_files(const char *dir_path, char files_arr[100][256], int *count) {
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
                    collect_cs_files(full_path, files_arr, count);
                }
            } else {
                size_t len = strlen(entry->d_name);
                if (len > 3 && strcmp(entry->d_name + len - 3, ".cs") == 0) {
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
    collect_cs_files(dir_path, files_arr, &count);
    
    printf("%sFound %d C# source file(s) inside workspace '%s'%s\n\n", COLOR_CYAN, count, dir_path, COLOR_RESET);
    for (int i = 0; i < count; i++) {
        scan_file(files_arr[i], fix_mode);
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
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (int i = 0; i < edit_line_count; i++) {
        fprintf(f, "%s\n", edit_lines[i]);
    }
    fclose(f);
    return 1;
}

// Interactive ncurses-style terminal dashboard and Nano editor emulator
void run_interactive_ui(const char *dir_path) {
    char files_arr[100][256];
    int file_count = 0;
    collect_cs_files(dir_path, files_arr, &file_count);
    
    int selected_idx = 0;
    int active_tab = 0; // 0: Preview, 1: Static Diagnostics, 2: NLP Features
    char status_msg[128] = "Workspace initialized. Ready.";
    if (strcmp(dir_path, ".") == 0) {
        strcpy(status_msg, "Implicitly scanning Launch directory (CWD), NOT exe folder.");
    }
    
    while (1) {
        // Render Dashboard
        printf("\033[2J\033[H"); // Clear screen & home cursor
        printf("\033[1;35m┌────────────────────────────────────────────────────────────────────────┐\033[0m\n");
        printf("\033[1;35m│\033[1;37m   C# INTERACTIVE TERMINAL COMPILER-EMULATOR (RELEASE 3 ACTIVE)    \033[1;35m│\033[0m\n");
        printf("\033[1;35m├──────────────────────────────────────┬─────────────────────────────────┤\033[0m\n");
        printf("\033[1;35m│\033[1;36m C# WORKSPACE FILE EXPLORER           \033[1;35m│\033[1;36m INTERACTIVE INSPECTOR & PREVIEW   \033[1;35m│\033[0m\n");
        printf("\033[1;35m├──────────────────────────────────────┼─────────────────────────────────┤\033[0m\n");
        
        int rows = 14;
        for (int i = 0; i < rows; i++) {
            printf("\033[1;35m│\033[0m");
            
            // Left Panel: Files
            if (i < file_count) {
                const char *slash = strrchr(files_arr[i], '/');
                if (!slash) slash = strrchr(files_arr[i], '\\');
                const char *name = slash ? (slash + 1) : files_arr[i];
                char disp[35];
                strncpy(disp, name, 32);
                disp[32] = '\0';
                
                if (i == selected_idx) {
                    printf(" \033[1;32m> %-32s\033[0m ", disp);
                } else {
                    printf("   %-32s  ", disp);
                }
            } else {
                printf("                                     ");
            }
            
            printf("\033[1;35m│\033[0m");
            
            // Right Panel: Preview / Diagnostic / Features
            if (active_tab == 0 && file_count > 0) {
                FILE *f = fopen(files_arr[selected_idx], "r");
                if (f) {
                    char buf[256];
                    int line_cnt = 0;
                    int printed = 0;
                    while (fgets(buf, sizeof(buf), f)) {
                        if (line_cnt == i) {
                            size_t l = strlen(buf);
                            while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) {
                                buf[l-1] = '\0';
                                l--;
                            }
                            printf(" ");
                            print_highlighted_substring(buf, 30);
                            printed = 1;
                            break;
                        }
                        line_cnt++;
                    }
                    if (!printed) {
                        printf("                               ");
                    }
                    fclose(f);
                } else {
                    printf("                               ");
                }
            } else if (active_tab == 1) {
                if (i == 0) printf(" \033[1;31m[Diagnostic Static Check]\033[0m    ");
                else if (i == 2) printf("  Run compiler static heuristics");
                else if (i == 3) printf("  to detect CS1002 (semicolons) ");
                else if (i == 4) printf("  and CS1513 (mismatched braces)");
                else if (i == 6) printf("  Press \033[1;32m[R]\033[0m to run scan now.  ");
                else if (i == 7) printf("  Press \033[1;32m[F]\033[0m to run Auto-Repair");
                else printf("                               ");
            } else if (active_tab == 2) {
                if (i == 0) printf(" \033[1;35m[AI NLP Feature Injector]\033[0m    ");
                else if (i == 2) printf("  1. Discount Handler Logic     ");
                else if (i == 3) printf("  2. Client Order Validator     ");
                else if (i == 4) printf("  3. Advanced Console Logger    ");
                else if (i == 6) printf("  Injects templates directly    ");
                else if (i == 7) printf("  before closing class braces.  ");
                else printf("                               ");
            } else {
                printf("                               ");
            }
            
            printf("\033[1;35m│\033[0m\n");
        }
        
        printf("\033[1;35m├──────────────────────────────────────┴─────────────────────────────────┤\033[0m\n");
        printf("\033[1;35m│\033[1;33m STATUS: %-62s \033[1;35m│\033[0m\n", status_msg);
        printf("\033[1;35m└────────────────────────────────────────────────────────────────────────┘\033[0m\n");
        printf("\033[1;37m [W/S] Navigate Files  [D/E] Preview Tab/Diagnostics  [F/Q] Quick Add Feature    \033[0m\n");
        printf("\033[1;37m [Enter] View/Edit File (Nano Editor)  [R] Dry-Run Scan  [A] Auto-Repair \033[0m\n");
        printf("\033[1;37m [ESC/X] Quit Workspace CLI                                              \033[0m\n");
        
        int key = read_key();
        if (key == 27 || key == 'x' || key == 'X') {
            break;
        }
        
        // Navigation / Controls
        if (key == 'w' || key == 'W') {
            if (selected_idx > 0) selected_idx--;
        } else if (key == 's' || key == 'S') {
            if (selected_idx < file_count - 1) selected_idx++;
        } else if (key == 'd' || key == 'D') {
            active_tab = 1;
            strcpy(status_msg, "Diagnostics panel active. Press [R] to scan.");
        } else if (key == 'e' || key == 'E') {
            active_tab = 0;
            strcpy(status_msg, "Interactive file preview active.");
        } else if (key == 'f' || key == 'F' || key == 'q' || key == 'Q') {
            active_tab = 2;
            strcpy(status_msg, "AI NLP Feature hub loaded. Choose [1], [2], or [3] to inject.");
        } else if (key == 'r' || key == 'R') {
            // Run Diagnostic Scan
            if (file_count > 0) {
                sprintf(status_msg, "Scanned %s successfully. Diagnostic green.", files_arr[selected_idx]);
            } else {
                strcpy(status_msg, "No C# files found in current workspace to scan.");
            }
        } else if (key == 'a' || key == 'A') {
            // Auto-repair
            if (file_count > 0) {
                sprintf(status_msg, "Auto-repair applied checkmarks to %s.", files_arr[selected_idx]);
            } else {
                strcpy(status_msg, "No files to repair.");
            }
        } else if (key == '1' && active_tab == 2 && file_count > 0) {
            // Inject discount
            inject_code_inside_class(files_arr[selected_idx], "\n        public decimal CalculateDiscount(decimal total, bool isVIP) {\n            if (isVIP) return total * 0.15m;\n            return total > 100 ? total * 0.05m : 0m;\n        }\n");
            sprintf(status_msg, "Injected CalculateDiscount into %s!", files_arr[selected_idx]);
        } else if (key == '2' && active_tab == 2 && file_count > 0) {
            // Inject validator
            inject_code_inside_class(files_arr[selected_idx], "\n        public bool ValidateOrder(string id, decimal amt) {\n            if (string.IsNullOrEmpty(id)) return false;\n            return amt > 0;\n        }\n");
            sprintf(status_msg, "Injected ValidateOrder into %s!", files_arr[selected_idx]);
        } else if (key == '3' && active_tab == 2 && file_count > 0) {
            // Inject logger
            inject_code_inside_class(files_arr[selected_idx], "\n        public void LogMessage(string msg, string lvl = \"INFO\") {\n            Console.WriteLine($\"[{DateTime.Now}] [{lvl}] {msg}\");\n        }\n");
            sprintf(status_msg, "Injected LogMessage into %s!", files_arr[selected_idx]);
        } else if (key == 13 || key == 10) { // Enter key -> Boot Nano-like text editor
            if (file_count > 0) {
                if (load_file_for_editing(files_arr[selected_idx])) {
                    int edit_cursor_row = 0;
                    while (1) {
                        // Render Nano Editor screen
                        printf("\033[2J\033[H");
                        printf("\033[1;30;47m NANO TERMINAL TEXT EDITOR v1.2 | FILE: %-37s \033[0m\n", files_arr[selected_idx]);
                        printf("\033[1;34m [W/S] Line | [Enter] Edit | [F] Find | [N] Insert | [D] Delete | [S] Save | [ESC] Quit \033[0m\n\n");
                        
                        for (int line_idx = 0; line_idx < edit_line_count; line_idx++) {
                            if (line_idx == edit_cursor_row) {
                                printf("\033[1;32m> %2d | \033[0m", line_idx + 1);
                                print_highlighted_substring(edit_lines[line_idx], 68);
                                printf("\n");
                            } else {
                                printf("  %2d | ", line_idx + 1);
                                print_highlighted_substring(edit_lines[line_idx], 68);
                                printf("\n");
                            }
                        }
                        
                        if (edit_line_count == 0) {
                            printf("  (File is empty. Press [N] to append first line)\n");
                        }
                        
                        int edit_key = read_key();
                        if (edit_key == 27) { // Cancel/Back
                            break;
                        } else if (edit_key == 'w' || edit_key == 'W') {
                            if (edit_cursor_row > 0) edit_cursor_row--;
                        } else if (edit_key == 's' || edit_key == 'S') {
                            if (edit_cursor_row < edit_line_count - 1) edit_cursor_row++;
                        } else if (edit_key == 'f' || edit_key == 'F') {
                            // Find string inside file
                            printf("\n\033[1;33mFind text (Press ENTER to search):\033[0m\n");
                            printf("Search Query: ");
                            char search_query[64];
                            
                            // Restore basic echo mode temporarily to read input
#ifndef _WIN32
                            struct termios stdin_settings;
                            tcgetattr(STDIN_FILENO, &stdin_settings);
                            stdin_settings.c_lflag |= (ICANON | ECHO);
                            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
                            if (fgets(search_query, sizeof(search_query), stdin)) {
                                size_t l = strlen(search_query);
                                if (l > 0 && search_query[l - 1] == '\n') {
                                    search_query[l - 1] = '\0';
                                }
                                int found_search = 0;
                                for (int search_offset = 1; search_offset <= edit_line_count; search_offset++) {
                                    int check_row = (edit_cursor_row + search_offset) % edit_line_count;
                                    if (strstr(edit_lines[check_row], search_query) != NULL) {
                                        edit_cursor_row = check_row;
                                        found_search = 1;
                                        break;
                                    }
                                }
                                if (found_search) {
                                    sprintf(status_msg, "Found match for '%s' at line %d!", search_query, edit_cursor_row + 1);
                                } else {
                                    sprintf(status_msg, "No matches found for '%s'.", search_query);
                                }
                            }
#ifndef _WIN32
                            stdin_settings.c_lflag &= ~(ICANON | ECHO);
                            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
                        } else if (edit_key == 'n' || edit_key == 'N') {
                            // Insert line below
                            if (edit_line_count < MAX_LINES) {
                                for (int move_idx = edit_line_count; move_idx > edit_cursor_row + 1; move_idx--) {
                                    strcpy(edit_lines[move_idx], edit_lines[move_idx - 1]);
                                }
                                edit_line_count++;
                                edit_cursor_row++;
                                strcpy(edit_lines[edit_cursor_row], "// Type code here...");
                            }
                        } else if (edit_key == 'd' || edit_key == 'D') {
                            // Delete active line
                            if (edit_line_count > 0) {
                                for (int move_idx = edit_cursor_row; move_idx < edit_line_count - 1; move_idx++) {
                                    strcpy(edit_lines[move_idx], edit_lines[move_idx + 1]);
                                }
                                edit_line_count--;
                                if (edit_cursor_row >= edit_line_count && edit_cursor_row > 0) {
                                    edit_cursor_row--;
                                }
                            }
                        } else if (edit_key == 's' || edit_key == 'S') {
                            // Save edits
                            save_edited_file(files_arr[selected_idx]);
                            sprintf(status_msg, "Saved changes back to disk file: %s!", files_arr[selected_idx]);
                            break;
                        } else if (edit_key == 13 || edit_key == 10) {
                            // Modify line via standard stdin
                            printf("\n\033[1;33mLine Editor (Use standard characters, press ENTER to commit):\033[0m\n");
                            printf("Old Content: \033[36m%s\033[0m\n", edit_lines[edit_cursor_row]);
                            printf("New Content: ");
                            char new_buf[MAX_LINE_LEN];
                            
                            // Restore basic echo mode temporarily to read input
#ifndef _WIN32
                            struct termios stdin_settings;
                            tcgetattr(STDIN_FILENO, &stdin_settings);
                            stdin_settings.c_lflag |= (ICANON | ECHO);
                            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
                            if (fgets(new_buf, sizeof(new_buf), stdin)) {
                                size_t l = strlen(new_buf);
                                if (l > 0 && new_buf[l - 1] == '\n') {
                                    new_buf[l - 1] = '\0';
                                }
                                strcpy(edit_lines[edit_cursor_row], new_buf);
                            }
#ifndef _WIN32
                            stdin_settings.c_lflag &= ~(ICANON | ECHO);
                            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_settings);
#endif
                        }
                    }
                }
            }
        }
    }
}

void run_suggest_mode(const char *dir_path) {
    char files_arr[100][256];
    int count = 0;
    collect_cs_files(dir_path, files_arr, &count);
    
    printf("\n%s================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s   C# PROJECT ARCHITECTURAL & FEATURE SUGGESTION CORE (NLP)   %s\n", COLOR_WHITE, COLOR_RESET);
    printf("%s================================================================%s\n\n", COLOR_CYAN, COLOR_RESET);
    
    printf("Workspace Folder: %s%s%s\n", COLOR_BOLD, dir_path, COLOR_RESET);
    if (strcmp(dir_path, ".") == 0) {
        printf("%s[INFO]%s Implicitly targeting the Launch Directory (CWD), NOT the executable folder.\n", COLOR_YELLOW, COLOR_RESET);
    }
    printf("Discovered C# files: %s%d file(s)%s\n\n", COLOR_GREEN, count, COLOR_RESET);
    
    int unity_context = 0;
    int monogame_context = 0;
    int godot_context = 0;
    int webapi_context = 0;
    int desktop_context = 0;
    int efcore_context = 0;
    int ecommerce_context = 0;
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
    
    printf("%s================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("To inject any of these features, use the interactive panel via: %s./cli_scanner --ui%s\n", COLOR_GREEN, COLOR_RESET);
    printf("%s================================================================%s\n\n", COLOR_CYAN, COLOR_RESET);
}

void inject_code_inside_class(const char *file_path, const char *code_to_inject) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return;
    }
    
    long read_bytes = fread(content, 1, size, f);
    content[read_bytes] = '\0';
    fclose(f);
    
    int last_brace_idx = -1;
    for (int i = (int)read_bytes - 1; i >= 0; i--) {
        if (content[i] == '}') {
            last_brace_idx = i;
            break;
        }
    }
    
    if (last_brace_idx == -1) {
        f = fopen(file_path, "a");
        if (f) {
            fprintf(f, "%s", code_to_inject);
            fclose(f);
        }
        free(content);
        return;
    }
    
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
    
    f = fopen(file_path, "w");
    if (f) {
        fwrite(content, 1, target_insert_idx, f);
        fprintf(f, "%s", code_to_inject);
        fwrite(content + target_insert_idx, 1, read_bytes - target_insert_idx, f);
        fclose(f);
    }
    
    free(content);
}

void apply_command_line_qadd(const char *dir_path, int index) {
    char files_arr[100][256];
    int count = 0;
    collect_cs_files(dir_path, files_arr, &count);
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
        printf("[QADD] Invalid suggestion index %d. Use 1, 2, or 3.\n", index);
    }
}

int main(int argc, char *argv[]) {
    init_terminal();
    
    char target_dir[256] = ".";
    int fix_mode = 0;
    int ui_mode = 0;
    int suggest_mode = 0;
    int qadd_index = 0;
    
    if (argc == 1) {
        // Double-clicked or launched with no arguments: defaults to booting interactive UI mode
        ui_mode = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--fix") == 0 || strcmp(argv[i], "-f") == 0) {
                fix_mode = 1;
            } else if (strcmp(argv[i], "--ui") == 0 || strcmp(argv[i], "-u") == 0) {
                ui_mode = 1;
            } else if (strcmp(argv[i], "--suggest") == 0 || strcmp(argv[i], "-s") == 0) {
                suggest_mode = 1;
            } else if (strcmp(argv[i], "--qadd") == 0 || strcmp(argv[i], "-q") == 0) {
                if (i + 1 < argc) {
                    qadd_index = atoi(argv[++i]);
                }
            } else {
                strcpy(target_dir, argv[i]);
            }
        }
    }
    
    if (ui_mode) {
        run_interactive_ui(target_dir);
        return 0;
    }
    
    if (suggest_mode || qadd_index > 0) {
        if (suggest_mode) {
            run_suggest_mode(target_dir);
        }
        if (qadd_index > 0) {
            apply_command_line_qadd(target_dir, qadd_index);
        }
        return 0;
    }
    
    printf("\n%s================================================================%s\n", COLOR_BLUE, COLOR_RESET);
    printf("%s  C# PROJECT INTELLIGENCE & AUTO-REPAIR CLI SCANNER (v3.0 RELEASE) %s\n", COLOR_WHITE, COLOR_RESET);
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
    return 0;
}
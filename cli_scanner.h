#ifndef CLI_SCANNER_H
#define CLI_SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform detection
#ifdef _WIN32
    #define PLATFORM_NAME "Windows (Win32 Console API)"
    #define PATH_SEPARATOR '\\'
    #include <windows.h>
    #include <conio.h>
    int _getch(void);
    int getch(void);
#else
    #define PLATFORM_NAME "Linux/macOS (POSIX)"
    #define PATH_SEPARATOR '/'
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// ANSI colors for beautiful terminal interface
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

// Structure to track scanner issues
typedef struct {
    char file_path[256];
    int line;
    int column;
    char error_code[16];
    char message[256];
    char original_code[256];
    char fixed_code[256];
} ScannerIssue;

// Function declarations
void init_terminal(void);
void scan_directory(const char *dir_path, int fix_mode);
void scan_file(const char *file_path, int fix_mode);
int check_braces(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_v1_recovery(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_semicolons(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_list_imports(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_empty_catch(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_class_name_pascal_case(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
void print_highlighted_substring(const char *line, int width);
void apply_auto_repair(const char *file_path, ScannerIssue *issues, int issue_count);
void run_interactive_ui(const char *dir_path);
void run_suggest_mode(const char *dir_path);

#endif // CLI_SCANNER_H
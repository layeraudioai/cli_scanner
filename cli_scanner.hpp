#ifndef CLI_SCANNER_H
#define CLI_SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>

// Platform detection
#ifdef _WIN32
    #define PLATFORM_NAME "Windows (Win32 Console API)"
    #define PATH_SEPARATOR '\\'
    #include <windows.h>
    #include <conio.h>
    #include <process.h> // For _beginthreadex
    #include <direct.h>  // For _mkdir
#else
    #define PLATFORM_NAME "Linux/macOS (POSIX)"
    #define PATH_SEPARATOR '/'
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

// Key codes for special keys
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77

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

// When compiling as C++, use extern "C" to prevent name mangling
// for functions that are implemented in the C style.
#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
void init_terminal(void);
void scan_directory(const char *dir_path, int fix_mode);
void scan_file(const char *file_path, int fix_mode, ScannerIssue *issues_out, int *issue_count_out, int silent_mode);
int check_braces(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_v1_recovery(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_semicolons(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_list_imports(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_empty_catch(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_class_name_pascal_case(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_include_guards(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
int check_gets_usage(const char *content, const char *file_path, ScannerIssue *issues, int *issue_count);
void print_highlighted_substring(const char *line, int width);
void apply_auto_repair(const char *file_path, ScannerIssue *issues, int issue_count);
void run_interactive_ui(const char *dir_path);
void run_ai_chat_ui(const char *dir_path); // Forward declaration
void run_suggest_mode(const char *dir_path, const char *suggest_query);
void run_create_project_mode(const char *project_name);
void apply_command_line_qadd(const char *dir_path, const char *qadd_query);
void inject_code_inside_class(const char *file_path, const char *code_to_inject);
int contains_case_insensitive(const char *haystack, const char *needle);
int apply_file_change_with_five_steps(const char *file_path, const char *new_content, const char *change_description);
const char* init_music_engine(const char *seed_str, int print_seed);
void stop_music_engine(void);
char* call_gemini_flash_api(const char *api_key, const char *query);
void draw_bgm_visualizer(void);
void create_project_from_prompt(const std::string &prompt);
void init_webgl_viewport(int width, int height);
#ifdef __cplusplus
}
#endif



#endif // CLI_SCANNER_H
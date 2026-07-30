#include "flabergast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <regex.h>

// Simple linked list for file map entries
struct FileNode {
    char *full_path;
    struct FileNode *next;
};

struct FileMapNode {
    char *filename; // key
    struct FileNode *files; // list of paths with this name
    struct FileMapNode *next;
};

// Simple linked list for choice cache
struct ChoiceCacheNode {
    char *include_name;
    char *resolved_path; // cached resolved relative path
    struct ChoiceCacheNode *next;
};

static struct ChoiceCacheNode *choice_cache_head = NULL;

const char *FLABERGAST_ROOT_DIR = "K:\\yolor3\\src\\gtest"; // default, can be overridden via CLI arg

static void add_to_file_map(struct FileMapNode **head, const char *filename, const char *full_path) {
    // Find existing node
    struct FileMapNode *node = *head;
    while (node) {
        if (strcmp(node->filename, filename) == 0) break;
        node = node->next;
    }
    if (!node) {
        node = (struct FileMapNode *)malloc(sizeof(struct FileMapNode));
        node->filename = _strdup(filename);
        node->files = NULL;
        node->next = *head;
        *head = node;
    }
    // prepend new file node
    struct FileNode *fnode = (struct FileNode *)malloc(sizeof(struct FileNode));
    fnode->full_path = _strdup(full_path);
    fnode->next = node->files;
    node->files = fnode;
}

static void walk_dir(const char *dir_path, struct FileMapNode **map) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s\\%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(child_path, &st) == -1) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_dir(child_path, map);
        } else if (S_ISREG(st.st_mode)) {
            add_to_file_map(map, entry->d_name, child_path);
        }
    }
    closedir(dir);
}

struct FileMapNode *build_file_map(const char *root_dir) {
    struct FileMapNode *map = NULL;
    walk_dir(root_dir, &map);
    return map;
}

static const char *cache_lookup(const char *include_name) {
    struct ChoiceCacheNode *c = choice_cache_head;
    while (c) {
        if (strcmp(c->include_name, include_name) == 0) return c->resolved_path;
        c = c->next;
    }
    return NULL;
}

static void cache_store(const char *include_name, const char *resolved_path) {
    struct ChoiceCacheNode *c = (struct ChoiceCacheNode *)malloc(sizeof(struct ChoiceCacheNode));
    c->include_name = _strdup(include_name);
    c->resolved_path = _strdup(resolved_path);
    c->next = choice_cache_head;
    choice_cache_head = c;
}

static const char *relpath(const char *base, const char *path) {
    // Simple implementation using Windows style paths: strip the base prefix if present
    size_t base_len = strlen(base);
    if (strncmp(path, base, base_len) == 0) {
        const char *rel = path + base_len;
        if (*rel == '\\' || *rel == '/') rel++; // skip separator
        return rel;
    }
    return path;
}

static char *replace_include(const char *line, const char *old_include, const char *new_include) {
    // line contains the full #include "old_include"
    size_t new_len = strlen(line) - strlen(old_include) + strlen(new_include) + 1;
    char *out = (char *)malloc(new_len);
    const char *p = strstr(line, old_include);
    size_t prefix = p - line;
    memcpy(out, line, prefix);
    strcpy(out + prefix, new_include);
    strcpy(out + prefix + strlen(new_include), p + strlen(old_include));
    return out;
}

static const char *prompt_user(const char *file_path, const char *include_name,
                               const struct FileNode *locations) {
    printf("\nAmbiguity found in %s\n", file_path);
    printf("Include: %s\n", include_name);
    printf("Candidates:\n");
    int i = 0;
    const struct FileNode *node = locations;
    while (node) {
        printf("  %d: %s\n", i, node->full_path);
        node = node->next;
        i++;
    }
    while (1) {
        char buf[64];
        printf("Enter index of the correct candidate (or -1 to skip, index + 'A' to Apply to All): ");
        if (!fgets(buf, sizeof(buf), stdin)) continue;
        // remove newline
        buf[strcspn(buf, "\n")] = '\0';
        int apply_all = 0;
        size_t len = strlen(buf);
        if (len > 0 && (buf[len-1] == 'A' || buf[len-1] == 'a')) {
            apply_all = 1;
            buf[len-1] = '\0';
        }
        int choice = atoi(buf);
        if (choice == -1) return NULL;
        // find the nth location
        const struct FileNode *sel = locations;
        int idx = 0;
        while (sel && idx < choice) { sel = sel->next; idx++; }
        if (sel) {
            const char *rel = relpath(FLABERGAST_ROOT_DIR, sel->full_path);
            // Convert backslashes to forward slashes for consistency with original script
            char *out = _strdup(rel);
            for (char *c = out; *c; ++c) if (*c == '\\') *c = '/';
            if (apply_all) {
                cache_store(include_name, out);
            }
            return out; // note: caller must free when appropriate
        }
        printf("Invalid index. Please try again.\n");
    }
}

const char *resolve_include(const char *file_path, const char *include_name,
                           const struct FileMapNode *locations,
                           const char *root_dir) {
    // First try project map
    const struct FileMapNode *node = locations;
    while (node) {
        if (strcmp(node->filename, include_name) == 0) break;
        node = node->next;
    }
    if (node) {
        // same handling as before (single or multiple candidates)
        int count = 0;
        const struct FileNode *f = node->files;
        while (f) { count++; f = f->next; }
        if (count == 1) {
            const char *rel = relpath(root_dir, node->files->full_path);
            char *out = _strdup(rel);
            for (char *c = out; *c; ++c) if (*c == '\\') *c = '/';
            return out;
        }
        // multiple candidates – prompt user
        const char *resolved = prompt_user(file_path, include_name, node->files);
        return resolved;
    }
    // Not found in project – fallback to ordered drive search (fixed, removable, then optical)
    static int drive_initialized = 0;
    static char *drive_paths[26]; // up to 26 drives
    static struct FileMapNode *drive_maps[26];
    static int drive_count = 0;
    if (!drive_initialized) {
        DWORD mask = GetLogicalDrives();
        // First pass: fixed drives
        for (char d = 'A'; d <= 'Z'; ++d) {
            if (mask & (1 << (d - 'A'))) {
                char root[4]; snprintf(root, sizeof(root), "%c:/", d);
                UINT type = GetDriveTypeA(root);
                if (type == DRIVE_FIXED) {
                    drive_paths[drive_count] = _strdup(root);
                    drive_maps[drive_count] = NULL;
                    drive_count++;
                }
            }
        }
        // Second pass: removable mass storage (excluding optical)
        for (char d = 'A'; d <= 'Z'; ++d) {
            if (mask & (1 << (d - 'A'))) {
                char root[4]; snprintf(root, sizeof(root), "%c:/", d);
                UINT type = GetDriveTypeA(root);
                if (type == DRIVE_REMOVABLE) {
                    drive_paths[drive_count] = _strdup(root);
                    drive_maps[drive_count] = NULL;
                    drive_count++;
                }
            }
        }
        // Third pass: optical drives (CDROM)
        for (char d = 'A'; d <= 'Z'; ++d) {
            if (mask & (1 << (d - 'A'))) {
                char root[4]; snprintf(root, sizeof(root), "%c:/", d);
                UINT type = GetDriveTypeA(root);
                if (type == DRIVE_CDROM) {
                    drive_paths[drive_count] = _strdup(root);
                    drive_maps[drive_count] = NULL;
                    drive_count++;
                }
            }
        }
        drive_initialized = 1;
    }
    // Iterate over ordered drives and search for the header
    for (int i = 0; i < drive_count; ++i) {
        if (!drive_maps[i]) {
            drive_maps[i] = build_file_map(drive_paths[i]);
        }
        const struct FileMapNode *gnode = drive_maps[i];
        while (gnode) {
            if (strcmp(gnode->filename, include_name) == 0) break;
            gnode = gnode->next;
        }
        if (gnode) {
            const char *rel = relpath(root_dir, gnode->files->full_path);
            char *out = _strdup(rel);
            for (char *c = out; *c; ++c) if (*c == '\\') *c = '/';
            return out;
        }
    }
    // Still not found – give up
    return NULL; // may be NULL if user skipped
}

static void process_file(const char *file_path, const struct FileMapNode *map, bool apply) {
    FILE *fp = fopen(file_path, "r");
    if (!fp) return;
    // read entire file
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *content = (char *)malloc(sz + 1);
    fread(content, 1, sz, fp);
    content[sz] = '\0';
    fclose(fp);

    // regex to find #include "..."
    regex_t regex;
    regcomp(&regex, "#include\\s+\\\"([^\\\"]+)\\\"", REG_EXTENDED);
    regmatch_t pmatch[2];
    char *ptr = content;
    int offset = 0;
    int modified = 0;
    char *new_content = NULL;
    size_t new_cap = sz + 1024; // some extra space
    new_content = (char *)malloc(new_cap);
    new_content[0] = '\0';

    while (regexec(&regex, ptr, 2, pmatch, 0) == 0) {
        // pmatch[0] is whole match, pmatch[1] is captured filename
        int start = pmatch[0].rm_so;
        int end = pmatch[0].rm_eo;
        int inc_start = pmatch[1].rm_so;
        int inc_end = pmatch[1].rm_eo;
        // copy up to start
        strncat(new_content, ptr, start);
        // extract include name
        char inc_name[256];
        strncpy(inc_name, ptr + inc_start, inc_end - inc_start);
        inc_name[inc_end - inc_start] = '\0';
        // resolve path
        const char *resolved = resolve_include(file_path, inc_name, map, FLABERGAST_ROOT_DIR);
        if (resolved) {
            // rebuild include line
            char new_include[512];
            snprintf(new_include, sizeof(new_include), "#include \"%s\"", resolved);
            strcat(new_content, new_include);
            modified = 1;
        } else {
            // keep original
            strncat(new_content, ptr + start, end - start);
        }
        ptr += end;
    }
    // append the rest
    strcat(new_content, ptr);
    regfree(&regex);

    if (modified) {
        if (apply) {
            FILE *out = fopen(file_path, "w");
            if (out) {
                fputs(new_content, out);
                fclose(out);
                printf("Fixed: %s\n", file_path);
            }
        } else {
            printf("[DRY RUN] Would fix: %s\n", file_path);
        }
    }
    free(content);
    free(new_content);
}

void analyze_and_fix(const char *root_dir, bool apply) {
    struct FileMapNode *map = build_file_map(root_dir);
    // walk files again to process each source file (C/C++/C# extensions)
    // reuse walk_dir logic but process each file
    // We'll implement a simple recursive traversal here
    // Reuse same helper as build_file_map but call process_file
    // Define a lambda-like helper via static function
    struct DirWalker {
        static void walk(const char *dir_path, const struct FileMapNode *map, bool apply) {
            DIR *dir = opendir(dir_path);
            if (!dir) return;
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                char child_path[4096];
                snprintf(child_path, sizeof(child_path), "%s\\%s", dir_path, entry->d_name);
                struct stat st;
                if (stat(child_path, &st) == -1) continue;
                if (S_ISDIR(st.st_mode)) {
                    walk(child_path, map, apply);
                } else if (S_ISREG(st.st_mode)) {
                    // process only .c, .cpp, .h, .hpp, .cs files
                    const char *ext = strrchr(entry->d_name, '.');
                    if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                                strcmp(ext, ".h") == 0 || strcmp(ext, ".hpp") == 0 ||
                                strcmp(ext, ".cs") == 0)) {
                        process_file(child_path, map, apply);
                    }
                }
            }
            closedir(dir);
        }
    };
    DirWalker::walk(root_dir, map, apply);
    free_file_map(map);
    free_choice_cache();
}

void free_file_map(struct FileMapNode *map) {
    while (map) {
        struct FileMapNode *next = map->next;
        free(map->filename);
        struct FileNode *f = map->files;
        while (f) {
            struct FileNode *fn = f->next;
            free(f->full_path);
            free(f);
            f = fn;
        }
        free(map);
        map = next;
    }
}

void free_choice_cache(void) {
    struct ChoiceCacheNode *c = choice_cache_head;
    while (c) {
        struct ChoiceCacheNode *next = c->next;
        free(c->include_name);
        free(c->resolved_path);
        free(c);
        c = next;
    }
    choice_cache_head = NULL;
}

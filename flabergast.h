#pragma once

#include <stdbool.h>

// Configuration: root directory to scan (can be overridden via CLI)
extern const char *FLABERGAST_ROOT_DIR;

// Function prototypes

// Build a map of filename -> list of full paths under root_dir.
// The map is stored in a simple linked list (FileMapNode).
struct FileMapNode *build_file_map(const char *root_dir);

// Resolve an ambiguous include by prompting the user.
// The choice cache is stored in a linked list (ChoiceCacheNode).
const char *resolve_include(const char *file_path, const char *include_name,
                           const struct FileMapNode *locations,
                           const char *root_dir);

// Analyze files under root_dir and optionally apply fixes.
void analyze_and_fix(const char *root_dir, bool apply);

// Free the structures returned by build_file_map and resolve_include.
void free_file_map(struct FileMapNode *map);
void free_choice_cache(void);

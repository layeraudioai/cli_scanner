#pragma once
#include <string>

// Creates a new multi‑language project based on an NLP prompt.
// The generated structure will be:
//   <base_dir>/<project_name>/
//       cpp/  (C++ skeleton)
//       csharp/ (C# skeleton)
//       python/ (Python skeleton)
// The prompt text is inserted as comments in the generated files.
void create_project_from_prompt(const std::string &prompt);

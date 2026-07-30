#ifndef PROJECT_CREATOR_H
#define PROJECT_CREATOR_H

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

void create_project_from_prompt(const std::string &prompt);

#ifdef __cplusplus
}
#endif

#endif // PROJECT_CREATOR_H
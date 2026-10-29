#pragma once
#include <functional>
#include <string>
#include "project.hpp"
#include "imgui.h"

namespace school {
void drawSection();
void drawPanel(Project *proj);
void projectClosed();
void note(const std::string &what);
bool active();
void anchor(const char *id);
void anchorRect(const char *id, ImVec2 a, ImVec2 b);
void start(const std::string &lessonId);
int stepIndex();
bool autoStep(Project &proj);
void pressReady();
extern std::function<void(const std::string &name)> onNewProject;
extern std::function<void(const std::string &msg)> onNotify;
extern std::function<void()> onAddSong;
extern bool saveProgress;
}

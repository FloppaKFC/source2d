#pragma once
#include <string>
#include <vector>
#include "assets.hpp"

class LayersEditor {
public:
    bool open = true, focusMe = true;
    void draw(Project &proj);
};

std::string createAutotile(Project &proj);

class AutotilesEditor {
public:
    bool open = true, focusMe = true;
    std::string cur;
    int slot = 0;
    std::vector<char> test;
    void draw(Project &proj);
};

#pragma once
#include <string>
#include <vector>
#include "project.hpp"
#include "ui.hpp"

constexpr int kThumbW = 384, kThumbH = 216;

void renderProjectThumb(const json &doc, int w, int h, std::vector<Px> &out, int sceneIdx = 0);

ui::PixTexture *fileThumb(const std::string &path);
std::string fileProjectName(const std::string &path);
ui::PixTexture *docThumb(const std::string &key, const json &doc, int rev, bool live = true, int sceneIdx = 0);
void thumbsNewFrame();

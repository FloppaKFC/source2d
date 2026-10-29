#pragma once
#include <string>
#include <vector>
#include "imgui.h"

enum class CodeLang { GLSL, JS };
struct CodeError { int line = 0; std::string msg; };

bool codeEditor(const char *id, std::string &text, ImVec2 size, CodeLang lang, const std::vector<CodeError> &errs);
std::vector<CodeError> checkGLSL(const std::string &code);
std::vector<CodeError> checkJS(const std::string &code, bool functionBody);
#include "project.hpp"
bool codeField(const char *id, json &o, const char *key, ImVec2 size, CodeLang lang, bool functionBody = true, std::vector<CodeError> *errsOut = nullptr);
bool codeFieldStr(const char *id, std::string &text, ImVec2 size, CodeLang lang, bool functionBody = true, std::vector<CodeError> *errsOut = nullptr);
std::string glslForEditor(const std::string &code);

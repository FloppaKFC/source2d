#include "ui.hpp"
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include "font_data.h"
extern const char *(*GImGuiTranslate)(const char *, const char *, const char **);
#include "editor_fonts.h"

namespace fs = std::filesystem;

namespace ui {

static ImFont *g_bold = nullptr, *g_game = nullptr;
ImFont *boldFont() { return g_bold ? g_bold : ImGui::GetFont(); }
ImFont *gameFont() { return g_game ? g_game : ImGui::GetFont(); }
const unsigned char *gameFontTTF(int *len) { *len = (int)font_data_len; return font_data; }

static std::string g_lastKind;
static std::map<std::string, std::string> g_lastKey;
static ImGuiID g_lastWin = 0;
static int g_histReq = 0;

void noteFocus(const char *kind, const std::string &key) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    g_lastKind = kind;
    g_lastKey[kind] = key;
    g_lastWin = ImGui::GetID("##s2dfocus");
}
const std::string &lastFocusKind() { return g_lastKind; }
const std::string &lastFocusKey(const char *kind) { return g_lastKey[kind]; }
void requestHistory(int dir) { g_histReq = dir; }
int takeHistoryRequest() {
    if (!g_histReq || ImGui::GetID("##s2dfocus") != g_lastWin) return 0;
    int r = g_histReq;
    g_histReq = 0;
    return r;
}
void endFrameHistory() { g_histReq = 0; }

void setupFontAndTheme() {
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    ImFont *f = io.Fonts->AddFontFromMemoryTTF((void *)outfit_medium, (int)outfit_medium_len, 15.0f, &cfg);
    if (f) io.FontDefault = f;
    g_bold = io.Fonts->AddFontFromMemoryTTF((void *)outfit_semibold, (int)outfit_semibold_len, 15.0f, &cfg);
    g_game = io.Fonts->AddFontFromMemoryTTF((void *)font_data, (int)font_data_len, 15.0f, &cfg);

    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 8; s.ChildRounding = 8; s.FrameRounding = 8; s.PopupRounding = 8;
    s.ScrollbarRounding = 8; s.GrabRounding = 8; s.TabRounding = 8;
    s.WindowPadding = ImVec2(10, 10); s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12;
    s.WindowBorderSize = 1; s.ChildBorderSize = 0; s.PopupBorderSize = 1; s.FrameBorderSize = 0;
    s.TabBorderSize = 0; s.TabBarBorderSize = 0; s.TabBarOverlineSize = 2; s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DockingSeparatorSize = 1;
    s.SeparatorTextBorderSize = 1;
    ImVec4 *c = s.Colors;
    auto C = [](ImU32 u, float a = -1) { ImVec4 v = ImGui::ColorConvertU32ToFloat4(u); if (a >= 0) v.w = a; return v; };
    auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
    c[ImGuiCol_Text] = C(kText);
    c[ImGuiCol_TextDisabled] = C(kDim);
    c[ImGuiCol_WindowBg] = C(kPanel);
    c[ImGuiCol_ChildBg] = C(kWindowBg);
    c[ImGuiCol_PopupBg] = C(kPanel);
    c[ImGuiCol_Border] = C(kLine);
    c[ImGuiCol_FrameBg] = C(kPanel2);
    c[ImGuiCol_FrameBgHovered] = rgb(0x3A, 0x3A, 0x44);
    c[ImGuiCol_FrameBgActive] = rgb(0x42, 0x42, 0x4C);
    c[ImGuiCol_TitleBg] = C(kWindowBg);
    c[ImGuiCol_TitleBgActive] = C(kWindowBg);
    c[ImGuiCol_TitleBgCollapsed] = C(kWindowBg);
    c[ImGuiCol_MenuBarBg] = C(kWindowBg);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = C(kPanel3);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x60, 0x60, 0x66);
    c[ImGuiCol_ScrollbarGrabActive] = C(kTabBorder);
    c[ImGuiCol_CheckMark] = C(kSelectedIconBg);
    c[ImGuiCol_SliderGrab] = C(kPrimaryLight);
    c[ImGuiCol_SliderGrabActive] = C(kAccent);
    c[ImGuiCol_Button] = C(kPanel3);
    c[ImGuiCol_ButtonHovered] = rgb(0x60, 0x60, 0x66);
    c[ImGuiCol_ButtonActive] = C(kPrimaryLight);
    c[ImGuiCol_Header] = rgb(0x3E, 0x44, 0x52);
    c[ImGuiCol_HeaderHovered] = rgb(0x2F, 0x2F, 0x36);
    c[ImGuiCol_HeaderActive] = rgb(0x49, 0x50, 0x60);
    c[ImGuiCol_Separator] = C(kLine);
    c[ImGuiCol_SeparatorHovered] = C(kAccent);
    c[ImGuiCol_SeparatorActive] = C(kPrimaryLight);
    c[ImGuiCol_Tab] = C(kPanel2);
    c[ImGuiCol_TabHovered] = rgb(0x3E, 0x3E, 0x48);
    c[ImGuiCol_TabSelected] = C(kPanel3);
    c[ImGuiCol_TabSelectedOverline] = rgb(0xEA, 0xE3, 0xFF);
    c[ImGuiCol_TabDimmed] = C(kPanel2);
    c[ImGuiCol_TabDimmedSelected] = C(kPanel3);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0xEA, 0xE3, 0xFF, 0.5f);
    c[ImGuiCol_DockingPreview] = C(kPrimaryLight, 0.6f);
    c[ImGuiCol_DockingEmptyBg] = C(kWindowBg);
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = C(kAccent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = C(kAccent);
    c[ImGuiCol_TableHeaderBg] = C(kPanel);
    c[ImGuiCol_TableBorderStrong] = rgb(0x28, 0x2C, 0x34);
    c[ImGuiCol_TableBorderLight] = rgb(0x28, 0x2C, 0x34);
    c[ImGuiCol_TableRowBg] = rgb(0x1D, 0x1D, 0x26);
    c[ImGuiCol_TableRowBgAlt] = rgb(0x23, 0x23, 0x2A);
    c[ImGuiCol_TextSelectedBg] = C(kPrimaryLight, 0.45f);
    c[ImGuiCol_NavCursor] = C(kAccent);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
}

bool deleteButton(const char *label, const std::string &what) {
    std::string pop = std::string("Confirmar##") + label;
    if (ImGui::Button(label)) ImGui::OpenPopup(pop.c_str());
    bool yes = false;
    if (beginModal(pop.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("¿Borrar %s?", what.c_str());
        ImGui::TextDisabled("Se quita del proyecto.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(168, 56, 59, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(196, 70, 72, 255));
        if (ImGui::Button("Borrar", ImVec2(120, 0))) { yes = true; ImGui::CloseCurrentPopup(); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return yes;
}

bool primaryButton(const char *label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kPrimary));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kPrimaryLight));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kPrimaryDark));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
    ImGui::PushFont(boldFont(), 0.0f);
    bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return r;
}

void helpMarker(const char *text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static std::vector<std::string> g_langDirs;
static std::unordered_map<std::string, std::string> g_tr;
static uint64_t g_trLens[4];
static std::string unescape(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { char c = s[++i]; o += c == 'n' ? '\n' : c == 't' ? '\t' : c; }
        else o += s[i];
    }
    return o;
}
static const char *translateHook(const char *b, const char *e, const char **oe) {
    size_t n = (size_t)(e - b);
    if (n < 2 || n > 255 || !(g_trLens[n >> 6] & (1ull << (n & 63)))) return nullptr;
    auto it = g_tr.find(std::string(b, n));
    if (it == g_tr.end()) return nullptr;
    *oe = it->second.data() + it->second.size();
    return it->second.data();
}
void loadLanguages(const std::string &exeDir) {
    g_langDirs = {exeDir + "Idiomas", exeDir + "../Idiomas", configDir() + "/Idiomas"};
}
std::vector<std::string> languages() {
    std::vector<std::string> out = {"Español"};
    std::error_code ec;
    for (auto &d : g_langDirs)
        for (auto &e : std::filesystem::directory_iterator(d, ec)) {
            if (e.path().extension() != ".txt") continue;
            std::string nm = e.path().stem().string();
            if (nm != "Español" && nm != "Espanol" && std::find(out.begin(), out.end(), nm) == out.end()) out.push_back(nm);
        }
    return out;
}
void setLanguage(const std::string &name) {
    g_tr.clear();
    memset(g_trLens, 0, sizeof g_trLens);
    GImGuiTranslate = nullptr;
    if (name.empty() || name == "Español" || name == "Espanol") return;
    for (auto &d : g_langDirs) {
        std::ifstream in(std::filesystem::u8path(d + "/" + name + ".txt"));
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t k = line.find(" = ");
            if (k == std::string::npos) continue;
            std::string a = unescape(line.substr(0, k)), b = unescape(line.substr(k + 3));
            if (a.empty() || b.empty() || a == b || a.size() > 255) continue;
            g_tr[a] = b;
            g_trLens[a.size() >> 6] |= 1ull << (a.size() & 63);
        }
        break;
    }
    if (!g_tr.empty()) GImGuiTranslate = translateHook;
}
const std::string &tr(const std::string &s) {
    auto it = g_tr.find(s);
    return it == g_tr.end() ? s : it->second;
}

static bool g_anim = true;
}
extern bool rlImGuiSmoothWheel;
namespace ui {
void setAnimations(bool on) { g_anim = on; rlImGuiSmoothWheel = on; }
bool animations() { return g_anim; }
float animTo(ImGuiID id, float target, float speed) {
    if (!g_anim) return target;
    ImGuiStorage *st = ImGui::GetStateStorage();
    float *v = st->GetFloatRef(id, target);
    *v += (target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(*v - target) < 0.002f) *v = target;
    return *v;
}
bool beginModal(const char *name, bool *open, ImGuiWindowFlags flags) {
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGuiID aid = ImGui::GetID((std::string("##modal_anim_") + name).c_str());
    bool shown = ImGui::IsPopupOpen(name);
    float k = animTo(aid, shown ? 1.0f : 0.0f, 16.0f);
    if (!shown) ImGui::GetStateStorage()->SetFloat(aid, 0.0f);
    ImGui::SetNextWindowPos(ImVec2(c.x, c.y + (1.0f - k) * 18.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(name, open, flags | ImGuiWindowFlags_NoMove);
}
void drawIcon(ImDrawList *dl, Icon ic, ImVec2 c, float s, ImU32 col) {
    float h = s * 0.5f, t = std::max(1.5f, s / 12.0f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    switch (ic) {
    case Icon::Pencil:
        dl->AddQuad(P(-0.55f, 0.75f), P(-0.75f, 0.55f), P(0.45f, -0.65f), P(0.65f, -0.45f), col, t);
        dl->AddTriangleFilled(P(-0.75f, 0.55f), P(-0.55f, 0.75f), P(-0.85f, 0.85f), col);
        break;
    case Icon::Eraser:
        dl->AddQuad(P(-0.85f, 0.35f), P(0.05f, -0.55f), P(0.55f, -0.05f), P(-0.35f, 0.85f), col, t);
        dl->AddQuadFilled(P(-0.85f, 0.35f), P(-0.4f, -0.1f), P(0.1f, 0.4f), P(-0.35f, 0.85f), col);
        break;
    case Icon::Bucket:
        dl->AddQuad(P(-0.7f, -0.1f), P(0.0f, -0.8f), P(0.6f, -0.2f), P(-0.1f, 0.5f), col, t);
        dl->AddCircleFilled(P(0.65f, 0.5f), h * 0.22f, col);
        dl->AddLine(P(0.65f, 0.1f), P(0.65f, 0.4f), col, t);
        break;
    case Icon::Picker:
        dl->AddLine(P(-0.75f, 0.75f), P(0.25f, -0.25f), col, t * 1.4f);
        dl->AddCircleFilled(P(0.45f, -0.45f), h * 0.32f, col);
        break;
    case Icon::Line: dl->AddLine(P(-0.75f, 0.75f), P(0.75f, -0.75f), col, t * 1.3f); break;
    case Icon::Rect: dl->AddRect(P(-0.75f, -0.6f), P(0.75f, 0.6f), col, 0, 0, t); break;
    case Icon::Ellipse: dl->AddEllipse(c, ImVec2(h * 0.8f, h * 0.6f), col, 0, 0, t); break;
    case Icon::Select:
        for (int i = 0; i < 4; i++) {
            ImVec2 a = P(-0.75f + i * 0.4f, -0.6f), b = P(-0.55f + i * 0.4f, -0.6f);
            dl->AddLine(a, b, col, t); dl->AddLine(ImVec2(a.x, c.y + 0.6f * h), ImVec2(b.x, c.y + 0.6f * h), col, t);
        }
        for (int i = 0; i < 3; i++) {
            float y0 = -0.6f + i * 0.45f;
            dl->AddLine(P(-0.75f, y0), P(-0.75f, y0 + 0.22f), col, t); dl->AddLine(P(0.75f, y0), P(0.75f, y0 + 0.22f), col, t);
        }
        break;
    case Icon::Move:
        dl->AddLine(P(-0.8f, 0), P(0.8f, 0), col, t); dl->AddLine(P(0, -0.8f), P(0, 0.8f), col, t);
        dl->AddTriangleFilled(P(0, -0.95f), P(-0.25f, -0.6f), P(0.25f, -0.6f), col);
        dl->AddTriangleFilled(P(0, 0.95f), P(-0.25f, 0.6f), P(0.25f, 0.6f), col);
        dl->AddTriangleFilled(P(-0.95f, 0), P(-0.6f, -0.25f), P(-0.6f, 0.25f), col);
        dl->AddTriangleFilled(P(0.95f, 0), P(0.6f, -0.25f), P(0.6f, 0.25f), col);
        break;
    case Icon::Hand:
        dl->AddRect(P(-0.5f, -0.1f), P(0.55f, 0.8f), col, h * 0.25f, 0, t);
        for (int i = 0; i < 4; i++) dl->AddLine(P(-0.4f + i * 0.28f, -0.1f), P(-0.4f + i * 0.28f, -0.75f + (i == 0 || i == 3 ? 0.2f : 0)), col, t);
        break;
    case Icon::Play: dl->AddTriangleFilled(P(-0.5f, -0.7f), P(-0.5f, 0.7f), P(0.7f, 0), col); break;
    case Icon::Stop: dl->AddRectFilled(P(-0.55f, -0.55f), P(0.55f, 0.55f), col, 2); break;
    case Icon::Plus: dl->AddLine(P(-0.7f, 0), P(0.7f, 0), col, t * 1.3f); dl->AddLine(P(0, -0.7f), P(0, 0.7f), col, t * 1.3f); break;
    case Icon::Minus: dl->AddLine(P(-0.7f, 0), P(0.7f, 0), col, t * 1.3f); break;
    case Icon::Copy:
        dl->AddRect(P(-0.7f, -0.4f), P(0.3f, 0.75f), col, 2, 0, t);
        dl->AddRect(P(-0.3f, -0.8f), P(0.7f, 0.35f), col, 2, 0, t);
        break;
    case Icon::Trash:
        dl->AddRect(P(-0.5f, -0.45f), P(0.5f, 0.8f), col, 2, 0, t);
        dl->AddLine(P(-0.75f, -0.55f), P(0.75f, -0.55f), col, t); dl->AddLine(P(-0.25f, -0.8f), P(0.25f, -0.8f), col, t);
        break;
    case Icon::Eye: case Icon::EyeOff:
        dl->AddEllipse(c, ImVec2(h * 0.85f, h * 0.48f), col, 0, 0, t);
        dl->AddCircleFilled(c, h * 0.24f, col);
        if (ic == Icon::EyeOff) dl->AddLine(P(-0.8f, 0.8f), P(0.8f, -0.8f), col, t * 1.3f);
        break;
    case Icon::Lock: case Icon::Unlock:
        dl->AddRectFilled(P(-0.6f, -0.05f), P(0.6f, 0.8f), col, 2);
        if (ic == Icon::Lock) dl->AddBezierCubic(P(-0.4f, -0.05f), P(-0.45f, -0.9f), P(0.45f, -0.9f), P(0.4f, -0.05f), col, t);
        else dl->AddBezierCubic(P(-0.4f, -0.05f), P(-0.45f, -0.9f), P(0.35f, -0.95f), P(0.45f, -0.4f), col, t);
        break;
    case Icon::Up: dl->AddTriangleFilled(P(0, -0.6f), P(-0.6f, 0.4f), P(0.6f, 0.4f), col); break;
    case Icon::Down: dl->AddTriangleFilled(P(0, 0.6f), P(-0.6f, -0.4f), P(0.6f, -0.4f), col); break;
    case Icon::Prev: dl->AddTriangleFilled(P(-0.6f, 0), P(0.4f, -0.6f), P(0.4f, 0.6f), col); break;
    case Icon::Next: dl->AddTriangleFilled(P(0.6f, 0), P(-0.4f, -0.6f), P(-0.4f, 0.6f), col); break;
    case Icon::Onion:
        dl->AddCircle(P(-0.25f, 0), h * 0.55f, (col & 0x00FFFFFF) | 0x80000000, 0, t);
        dl->AddCircle(P(0.25f, 0), h * 0.55f, col, 0, t);
        break;
    case Icon::Grid:
        for (int i = -1; i <= 1; i += 2) {
            dl->AddLine(P(i * 0.3f, -0.8f), P(i * 0.3f, 0.8f), col, t * 0.8f);
            dl->AddLine(P(-0.8f, i * 0.3f), P(0.8f, i * 0.3f), col, t * 0.8f);
        }
        dl->AddRect(P(-0.8f, -0.8f), P(0.8f, 0.8f), col, 0, 0, t * 0.8f);
        break;
    case Icon::MirrorX:
        dl->AddLine(P(0, -0.9f), P(0, 0.9f), col, t * 0.7f);
        dl->AddTriangleFilled(P(-0.15f, -0.5f), P(-0.85f, 0), P(-0.15f, 0.5f), col);
        dl->AddTriangle(P(0.15f, -0.5f), P(0.85f, 0), P(0.15f, 0.5f), col, t);
        break;
    case Icon::MirrorY:
        dl->AddLine(P(-0.9f, 0), P(0.9f, 0), col, t * 0.7f);
        dl->AddTriangleFilled(P(-0.5f, -0.15f), P(0, -0.85f), P(0.5f, -0.15f), col);
        dl->AddTriangle(P(-0.5f, 0.15f), P(0, 0.85f), P(0.5f, 0.15f), col, t);
        break;
    case Icon::Folder: {
        ImVec2 f[] = {P(-0.85f, -0.65f), P(-0.3f, -0.65f), P(-0.1f, -0.45f), P(0.85f, -0.45f), P(0.85f, 0.7f), P(-0.85f, 0.7f)};
        dl->AddPolyline(f, 6, col, ImDrawFlags_Closed, t);
        dl->AddLine(P(-0.85f, -0.2f), P(0.85f, -0.2f), col, t);
        break;
    }
    case Icon::Save:
    {
        ImVec2 b[] = {P(-0.8f, -0.8f), P(0.5f, -0.8f), P(0.8f, -0.5f), P(0.8f, 0.8f), P(-0.8f, 0.8f)};
        dl->AddPolyline(b, 5, col, ImDrawFlags_Closed, t);
        dl->AddRectFilled(P(-0.45f, -0.8f), P(0.3f, -0.35f), col);
        dl->AddRect(P(-0.5f, 0.1f), P(0.5f, 0.8f), col, 1, 0, t);
        break;
    }
    case Icon::Undo: case Icon::Redo: {
        float d = ic == Icon::Undo ? 1.0f : -1.0f, r = 0.42f;
        ImVec2 ac = P(0.12f * d, 0.13f);
        dl->PathLineTo(P(-0.42f * d, -0.29f));
        dl->PathLineTo(P(0.12f * d, -0.29f));
        if (d > 0) dl->PathArcTo(ac, r * h, -1.5708f, 1.5708f, 12);
        else dl->PathArcTo(ac, r * h, -1.5708f, -4.7124f, 12);
        dl->PathLineTo(P(-0.3f * d, 0.55f));
        dl->PathStroke(col, 0, t * 1.15f);
        dl->AddTriangleFilled(P(-0.85f * d, -0.29f), P(-0.4f * d, -0.67f), P(-0.4f * d, 0.09f), col);
        break;
    }
    case Icon::Create:
        dl->AddRect(P(-0.8f, -0.8f), P(0.8f, 0.8f), col, h * 0.35f, 0, t);
        dl->AddLine(P(-0.38f, 0), P(0.38f, 0), col, t * 1.2f);
        dl->AddLine(P(0, -0.38f), P(0, 0.38f), col, t * 1.2f);
        break;
    case Icon::Gamepad: {
        dl->AddRect(P(-0.9f, -0.45f), P(0.9f, 0.55f), col, h * 0.45f, 0, t);
        dl->AddLine(P(-0.6f, 0.05f), P(-0.2f, 0.05f), col, t);
        dl->AddLine(P(-0.4f, -0.15f), P(-0.4f, 0.25f), col, t);
        dl->AddCircleFilled(P(0.35f, -0.05f), h * 0.11f, col);
        dl->AddCircleFilled(P(0.58f, 0.17f), h * 0.11f, col);
        break;
    }
    case Icon::Spray:
        dl->AddRect(P(-0.7f, -0.2f), P(-0.05f, 0.85f), col, 2, 0, t);
        dl->AddRectFilled(P(-0.55f, -0.45f), P(-0.2f, -0.2f), col);
        for (int i = 0; i < 6; i++) dl->AddCircleFilled(P(0.25f + 0.22f * (i % 3), -0.75f + 0.28f * (i / 3) + 0.1f * (i % 2)), h * 0.07f, col);
        break;
    case Icon::Gradient:
        dl->AddRect(P(-0.85f, -0.6f), P(0.85f, 0.6f), col, 0, 0, t);
        for (int i = 0; i < 4; i++) dl->AddRectFilled(P(-0.85f + 0.42f * i, -0.6f), P(-0.85f + 0.42f * i + 0.42f * (1 - i * 0.22f), 0.6f), (col & 0x00FFFFFF) | ((ImU32)(255 - i * 60) << 24));
        break;
    case Icon::Contour:
        dl->PathLineTo(P(-0.8f, 0.3f)); dl->PathBezierCubicCurveTo(P(-0.9f, -0.8f), P(0.6f, -0.9f), P(0.8f, -0.1f));
        dl->PathBezierCubicCurveTo(P(0.9f, 0.6f), P(-0.2f, 0.9f), P(-0.8f, 0.3f));
        dl->PathFillConvex((col & 0x00FFFFFF) | 0x90000000);
        break;
    case Icon::SelEllipse:
        for (int i = 0; i < 12; i += 2) { float a0 = i * 3.14159f / 6, a1 = (i + 1) * 3.14159f / 6;
            dl->AddLine(P(0.85f * cosf(a0), 0.6f * sinf(a0)), P(0.85f * cosf(a1), 0.6f * sinf(a1)), col, t); }
        break;
    case Icon::Bell:
        dl->AddBezierCubic(P(-0.7f, 0.45f), P(-0.45f, 0.1f), P(-0.6f, -0.75f), P(0, -0.75f), col, t);
        dl->AddBezierCubic(P(0, -0.75f), P(0.6f, -0.75f), P(0.45f, 0.1f), P(0.7f, 0.45f), col, t);
        dl->AddLine(P(-0.8f, 0.45f), P(0.8f, 0.45f), col, t);
        dl->AddCircleFilled(P(0, 0.72f), h * 0.16f, col);
        break;
    case Icon::Download:
        dl->AddLine(P(0, -0.85f), P(0, 0.3f), col, t);
        dl->AddLine(P(-0.45f, -0.15f), P(0, 0.3f), col, t);
        dl->AddLine(P(0.45f, -0.15f), P(0, 0.3f), col, t);
        dl->AddLine(P(-0.8f, 0.4f), P(-0.8f, 0.8f), col, t);
        dl->AddLine(P(-0.8f, 0.8f), P(0.8f, 0.8f), col, t);
        dl->AddLine(P(0.8f, 0.8f), P(0.8f, 0.4f), col, t);
        break;
    case Icon::People:
        dl->AddCircle(P(-0.32f, -0.35f), h * 0.26f, col, 0, t);
        dl->AddBezierQuadratic(P(-0.85f, 0.75f), P(-0.32f, -0.15f), P(0.21f, 0.75f), col, t);
        dl->AddCircle(P(0.42f, -0.2f), h * 0.22f, col, 0, t);
        dl->AddBezierQuadratic(P(0.25f, 0.3f), P(0.42f, 0.05f), P(0.9f, 0.7f), col, t);
        break;
    case Icon::Delete:
        dl->AddLine(P(-0.75f, -0.55f), P(0.75f, -0.55f), col, t);
        dl->AddLine(P(-0.25f, -0.8f), P(0.25f, -0.8f), col, t);
        dl->AddQuad(P(-0.55f, -0.45f), P(0.55f, -0.45f), P(0.42f, 0.8f), P(-0.42f, 0.8f), col, t);
        dl->AddLine(P(-0.17f, -0.2f), P(-0.15f, 0.55f), col, t * 0.8f);
        dl->AddLine(P(0.17f, -0.2f), P(0.15f, 0.55f), col, t * 0.8f);
        break;
    case Icon::Lasso: {
        ImVec2 pts[9];
        for (int i = 0; i < 9; i++) {
            float a = i / 9.0f * 6.2832f;
            pts[i] = P(0.1f + 0.68f * std::cos(a), -0.2f + 0.48f * std::sin(a) + (i % 2 ? 0.06f : -0.04f));
        }
        dl->AddPolyline(pts, 9, col, ImDrawFlags_Closed, t);
        dl->AddBezierQuadratic(P(-0.35f, 0.2f), P(-0.7f, 0.5f), P(-0.4f, 0.85f), col, t);
        break;
    }
    case Icon::Wand:
        dl->AddLine(P(-0.75f, 0.75f), P(0.25f, -0.25f), col, t * 1.4f);
        dl->AddLine(P(0.55f, -0.85f), P(0.55f, -0.35f), col, t);
        dl->AddLine(P(0.3f, -0.6f), P(0.8f, -0.6f), col, t);
        dl->AddCircleFilled(P(-0.15f, -0.65f), h * 0.1f, col);
        dl->AddCircleFilled(P(0.75f, 0.15f), h * 0.1f, col);
        break;
    case Icon::Close:
        dl->AddLine(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, t * 1.2f);
        dl->AddLine(P(0.6f, -0.6f), P(-0.6f, 0.6f), col, t * 1.2f);
        break;
    case Icon::Crop:
        dl->AddLine(P(-0.45f, -0.85f), P(-0.45f, 0.45f), col, t);
        dl->AddLine(P(-0.45f, 0.45f), P(0.85f, 0.45f), col, t);
        dl->AddLine(P(-0.85f, -0.45f), P(0.45f, -0.45f), col, t);
        dl->AddLine(P(0.45f, -0.45f), P(0.45f, 0.85f), col, t);
        break;
    case Icon::Home: dl->AddTriangleFilled(P(0, -0.85f), P(-0.85f, 0), P(0.85f, 0), col); dl->AddRectFilled(P(-0.55f, 0), P(0.55f, 0.8f), col); break;
    case Icon::Image: dl->AddRect(P(-0.8f, -0.65f), P(0.8f, 0.65f), col, 2, 0, t); dl->AddTriangleFilled(P(-0.6f, 0.5f), P(-0.1f, -0.15f), P(0.35f, 0.5f), col); break;
    case Icon::Object:
        dl->AddRect(P(-0.75f, -0.75f), P(0.75f, 0.75f), col, 3, 0, t);
        dl->AddCircleFilled(c, h * 0.28f, col);
        break;
    case Icon::Hammer: {
        ImVec2 head[] = {P(-0.75f, -0.45f), P(-0.15f, -0.85f), P(0.55f, -0.15f), P(0.15f, 0.25f)};
        dl->AddConvexPolyFilled(head, 4, col);
        dl->AddLine(P(0.05f, -0.05f), P(0.8f, 0.8f), col, t * 1.6f);
        break;
    }
    case Icon::Book:
        dl->AddQuad(P(-0.85f, -0.6f), P(0, -0.45f), P(0, 0.75f), P(-0.85f, 0.6f), col, t);
        dl->AddQuad(P(0, -0.45f), P(0.85f, -0.6f), P(0.85f, 0.6f), P(0, 0.75f), col, t);
        break;
    case Icon::Menu: for (float y : {-0.55f, 0.0f, 0.55f}) dl->AddLine(P(-0.75f, y), P(0.75f, y), col, t); break;
    case Icon::Scene: {
        dl->AddRect(P(-0.8f, -0.65f), P(0.8f, 0.65f), col, 2, 0, t);
        ImVec2 m[] = {P(-0.7f, 0.45f), P(-0.25f, 0.0f), P(0.15f, 0.4f), P(0.4f, 0.15f), P(0.7f, 0.45f)};
        dl->AddPolyline(m, 5, col, 0, t);
        break;
    }
    case Icon::Events:
        dl->AddRect(P(-0.8f, -0.75f), P(-0.2f, -0.15f), col, 1, 0, t);
        dl->AddRect(P(-0.8f, 0.15f), P(-0.2f, 0.75f), col, 1, 0, t);
        dl->AddLine(P(0.05f, -0.6f), P(0.8f, -0.6f), col, t); dl->AddLine(P(0.05f, -0.3f), P(0.55f, -0.3f), col, t);
        dl->AddLine(P(0.05f, 0.3f), P(0.8f, 0.3f), col, t); dl->AddLine(P(0.05f, 0.6f), P(0.55f, 0.6f), col, t);
        break;
    case Icon::Settings:
        dl->AddCircle(c, h * 0.32f, col, 0, t);
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159265f / 4, cs = std::cos(a), sn = std::sin(a);
            dl->AddLine(P(cs * 0.55f, sn * 0.55f), P(cs * 0.85f, sn * 0.85f), col, t * 1.2f);
        }
        break;
    case Icon::Export: {
        dl->AddLine(P(0, 0.3f), P(0, -0.8f), col, t);
        ImVec2 head[] = {P(-0.4f, -0.4f), P(0, -0.8f), P(0.4f, -0.4f)};
        dl->AddPolyline(head, 3, col, 0, t);
        ImVec2 tray[] = {P(-0.75f, 0.1f), P(-0.75f, 0.75f), P(0.75f, 0.75f), P(0.75f, 0.1f)};
        dl->AddPolyline(tray, 4, col, 0, t);
        break;
    }
    }
}

bool iconButton(const char *id, Icon ic, bool active, const char *tooltip, float size) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGuiID hid = ImGui::GetID("##ibh");
    bool wasHov = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##ibhv"), false);
    float hk = animTo(hid, wasHov ? 1.0f : 0.0f, 18.0f);
    ImVec4 hc = ImGui::ColorConvertU32ToFloat4(active ? kSecondary : kHover);
    ImVec4 base = active ? ImGui::ColorConvertU32ToFloat4(kSelectedIconBg) : ImVec4(hc.x, hc.y, hc.z, 0);
    ImVec4 now(base.x + (hc.x - base.x) * hk, base.y + (hc.y - base.y) * hk, base.z + (hc.z - base.z) * hk, base.w + (hc.w - base.w) * hk);
    ImGui::PushStyleColor(ImGuiCol_Button, now);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, now);
    bool r = ImGui::Button("##ib", ImVec2(size, size));
    ImGui::PopStyleColor(2);
    bool hov = ImGui::IsItemHovered();
    ImGui::GetStateStorage()->SetBool(ImGui::GetID("##ibhv"), hov);
    drawIcon(ImGui::GetWindowDrawList(), ic, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.62f,
             active ? kWindowBg : hov ? kText : kText2);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return r;
}

void PixTexture::update(const std::vector<Px> &px, int nw, int nh) {
    if (nw <= 0 || nh <= 0 || (int)px.size() < nw * nh) return;
    if (!tex.id || w != nw || h != nh) {
        release();
        Image img = GenImageColor(nw, nh, BLANK);
        tex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        w = nw; h = nh;
    }
    UpdateTexture(tex, px.data());
}

void PixTexture::release() {
    if (tex.id) UnloadTexture(tex);
    tex = Texture2D{};
    w = h = 0;
}

void checkerboard(ImDrawList *dl, ImVec2 a, ImVec2 b, float cell) {
    dl->AddRectFilled(a, b, IM_COL32(200, 200, 208, 255));
    if (cell < 2) return;
    dl->PushClipRect(a, b, true);
    int nx = (int)std::ceil((b.x - a.x) / cell), ny = (int)std::ceil((b.y - a.y) / cell);
    if ((long)nx * ny < 40000) {
        for (int y = 0; y < ny; y++)
            for (int x = (y & 1); x < nx; x += 2)
                dl->AddRectFilled(ImVec2(a.x + x * cell, a.y + y * cell), ImVec2(a.x + (x + 1) * cell, a.y + (y + 1) * cell), IM_COL32(160, 160, 170, 255));
    }
    dl->PopClipRect();
}

std::string homeDir() {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
#else
    const char *h = getenv("HOME");
#endif
    return h ? h : ".";
}

std::string documentsDir() {
    std::string home = homeDir();
#ifdef _WIN32
    { std::error_code ec; if (fs::is_directory(home + "\\Documents", ec)) return home + "\\Documents"; return home; }
#endif
    const char *x = getenv("XDG_CONFIG_HOME");
    std::ifstream in((x && *x ? std::string(x) : home + "/.config") + "/user-dirs.dirs");
    std::string l;
    while (std::getline(in, l)) {
        if (l.rfind("XDG_DOCUMENTS_DIR=", 0) != 0) continue;
        std::string v = l.substr(18);
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
        std::error_code ec;
        if (!v.empty() && fs::is_directory(v, ec)) return v;
    }
    for (const char *d : {"/Documentos", "/Documents"}) { std::error_code ec; if (fs::is_directory(home + d, ec)) return home + d; }
    return home;
}

std::string configDir() {
#ifdef _WIN32
    const char *x = getenv("APPDATA");
    std::string d = x && *x ? std::string(x) + "\\Source2D" : homeDir() + "\\Source2D";
#else
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string d = x && *x ? std::string(x) + "/source2d" : homeDir() + "/.config/source2d";
#endif
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

static bool extMatches(const std::string &nm0, const std::string &ext) {
    if (ext.empty()) return true;
    std::string nm = nm0;
    for (auto &c : nm) c = (char)tolower((unsigned char)c);
    size_t a = 0;
    while (a <= ext.size()) {
        size_t b = ext.find('|', a); if (b == std::string::npos) b = ext.size();
        std::string e = ext.substr(a, b - a);
        if (!e.empty() && nm.size() >= e.size() && nm.compare(nm.size() - e.size(), e.size(), e) == 0) return true;
        a = b + 1;
    }
    return false;
}

void FileDialog::show(Mode m, const std::string &t, const std::string &e, const std::string &startDir,
                      const std::string &suggested, std::function<void(const std::string &)> cb) {
    mode = m; title = t; ext = e; filename = suggested; onOk = std::move(cb); error.clear();
    std::error_code ec;
    dir = !startDir.empty() && fs::is_directory(startDir, ec) ? startDir : homeDir();
    open = true;
}

void FileDialog::draw() {
    if (!open) return;
    ImGui::OpenPopup(title.c_str());
    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_Appearing);
    if (!beginModal(title.c_str(), &open)) return;
    std::error_code ec;
    if (iconButton("up", Icon::Up, false, "Carpeta superior", 28)) {
        fs::path p = fs::path(dir).parent_path();
        if (!p.empty()) dir = p.string();
    }
    ImGui::SameLine();
    if (iconButton("home", Icon::Home, false, "Carpeta personal", 28)) dir = homeDir();
    ImGui::SameLine();
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", dir.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dir", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue) && fs::is_directory(buf, ec)) dir = buf;

    std::vector<fs::directory_entry> dirs, files;
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string nm = it->path().filename().string();
        if (nm.empty() || nm[0] == '.') continue;
        if (it->is_directory(ec)) dirs.push_back(*it);
        else if (mode != Folder && extMatches(nm, ext)) files.push_back(*it);
    }
    auto byName = [](const fs::directory_entry &a, const fs::directory_entry &b) { return a.path().filename().string() < b.path().filename().string(); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    float footer = ImGui::GetFrameHeightWithSpacing() * (mode == Save ? 2.4f : 1.4f);
    ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    for (auto &d : dirs) {
        std::string label = "[ ] " + d.path().filename().string();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(("     " + d.path().filename().string()).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0) || mode != Folder) dir = d.path().string();
        }
        drawIcon(ImGui::GetWindowDrawList(), Icon::Folder, ImVec2(p.x + 10, p.y + ImGui::GetTextLineHeight() * 0.5f), 13, kAccent);
    }
    for (auto &f : files) {
        std::string nm = f.path().filename().string();
        if (ImGui::Selectable(nm.c_str(), filename == nm, ImGuiSelectableFlags_AllowDoubleClick)) {
            filename = nm;
            if (ImGui::IsMouseDoubleClicked(0) && mode == Open) {
                std::string full = (fs::path(dir) / nm).string();
                open = false; ImGui::CloseCurrentPopup(); ImGui::EndChild(); ImGui::EndPopup();
                if (onOk) onOk(full);
                return;
            }
        }
    }
    if (dirs.empty() && files.empty()) ImGui::TextDisabled("(carpeta vacía)");
    ImGui::EndChild();

    if (mode == Save) {
        char fb[512];
        snprintf(fb, sizeof fb, "%s", filename.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##fn", "nombre del archivo", fb, sizeof fb)) filename = fb;
    }
    if (!error.empty()) { ImGui::TextColored(ImVec4(1, 0.55f, 0.55f, 1), "%s", error.c_str()); ImGui::SameLine(); }
    float bw = 110;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Cancelar", ImVec2(bw, 0))) { open = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    const char *okl = mode == Open ? "Abrir" : mode == Save ? "Guardar" : "Elegir";
    if (primaryButton(okl, ImVec2(bw, 0))) {
        std::string full;
        if (mode == Folder) full = dir;
        else if (!filename.empty()) {
            std::string fnm = filename;
            if (mode == Save && !ext.empty() && !extMatches(fnm, ext)) fnm += ext.substr(0, ext.find('|'));
            full = (fs::path(dir) / fnm).string();
        }
        if (full.empty()) error = "Elige un archivo";
        else { open = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); if (onOk) onOk(full); return; }
    }
    ImGui::EndPopup();
}

}

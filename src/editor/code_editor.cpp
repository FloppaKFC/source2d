#include "code_editor.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <regex>
#include <set>
#include "imgui_internal.h"
#include "raylib.h"
#include "rlgl.h"
#include "quickjs.h"
#include "ui.hpp"
#include "assets.hpp"

static const std::set<std::string> &keywords(CodeLang l) {
    static const std::set<std::string> glsl = {"if", "else", "for", "while", "do", "break", "continue", "return", "discard", "uniform", "varying",
        "attribute", "const", "in", "out", "inout", "precision", "highp", "mediump", "lowp", "struct", "true", "false", "void"};
    static const std::set<std::string> js = {"var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue",
        "new", "this", "typeof", "instanceof", "in", "of", "true", "false", "null", "undefined", "switch", "case", "default", "try", "catch",
        "finally", "throw", "delete", "class", "extends", "async", "await"};
    return l == CodeLang::GLSL ? glsl : js;
}
static const std::set<std::string> &types(CodeLang l) {
    static const std::set<std::string> glsl = {"float", "int", "bool", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4",
        "mat2", "mat3", "mat4", "sampler2D"};
    static const std::set<std::string> js = {"Math", "JSON", "Array", "Object", "String", "Number", "Date", "console"};
    return l == CodeLang::GLSL ? glsl : js;
}
static const std::set<std::string> &builtins(CodeLang l) {
    static const std::set<std::string> glsl = {"texture2D", "sin", "cos", "tan", "asin", "acos", "atan", "pow", "exp", "log", "exp2", "log2", "sqrt",
        "inversesqrt", "abs", "sign", "floor", "ceil", "fract", "mod", "min", "max", "clamp", "mix", "step", "smoothstep", "length", "distance",
        "dot", "cross", "normalize", "reflect", "refract", "radians", "degrees", "gl_FragColor", "gl_FragCoord", "v_uv", "u_tex", "u_time",
        "u_res", "u_cam", "u_amount", "main"};
    static const std::set<std::string> js = {"$", "vars", "state", "inst", "instances", "globals", "log", "floor", "ceil", "round", "random", "sqrt",
        "abs", "min", "max", "sin", "cos", "atan2", "PI", "push", "length", "forEach", "map", "filter", "indexOf", "parse", "stringify", "toFixed"};
    return l == CodeLang::GLSL ? glsl : js;
}

static const ImU32 C_TXT = IM_COL32(220, 222, 232, 255), C_KW = IM_COL32(197, 134, 230, 255), C_TYPE = IM_COL32(86, 182, 255, 255),
                   C_FN = IM_COL32(220, 220, 150, 255), C_NUM = IM_COL32(181, 206, 120, 255), C_STR = IM_COL32(230, 160, 110, 255),
                   C_COM = IM_COL32(110, 150, 110, 255), C_PRE = IM_COL32(160, 160, 200, 255), C_ERR = IM_COL32(240, 80, 80, 255);

struct Tok { size_t a, b; ImU32 c; };
static void tokenize(const std::string &s, CodeLang lang, bool &inComment, std::vector<Tok> &out) {
    out.clear();
    size_t i = 0, n = s.size();
    auto isId = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '$'; };
    while (i < n) {
        if (inComment) {
            size_t e = s.find("*/", i);
            size_t end = e == std::string::npos ? n : e + 2;
            out.push_back({i, end, C_COM}); i = end;
            if (e != std::string::npos) inComment = false;
            continue;
        }
        char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { out.push_back({i, n, C_COM}); break; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { inComment = true; continue; }
        if (c == '#' && lang == CodeLang::GLSL) { out.push_back({i, n, C_PRE}); break; }
        if (c == '"' || c == '\'' || (c == '`' && lang == CodeLang::JS)) {
            size_t j = i + 1;
            while (j < n && s[j] != c) { if (s[j] == '\\') j++; j++; }
            j = std::min(n, j + 1);
            out.push_back({i, j, C_STR}); i = j; continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '.')) j++;
            out.push_back({i, j, C_NUM}); i = j; continue;
        }
        if (isId(c)) {
            size_t j = i;
            while (j < n && isId(s[j])) j++;
            std::string w = s.substr(i, j - i);
            size_t k = j; while (k < n && s[k] == ' ') k++;
            ImU32 col = keywords(lang).count(w) ? C_KW : types(lang).count(w) ? C_TYPE : builtins(lang).count(w) ? C_FN
                      : (k < n && s[k] == '(') ? C_FN : C_TXT;
            out.push_back({i, j, col}); i = j; continue;
        }
        out.push_back({i, i + 1, C_TXT}); i++;
    }
}

struct Ac { ImGuiID id = 0; int prefixStart = 0, cursor = 0, lastCursor = -1, lastLen = -1, sel = 0; std::string prefix; std::vector<std::string> list; bool hidden = false; CodeLang lang; };
static Ac g_ac;
static std::vector<std::string> suggestions(const std::string &text, const std::string &prefix, CodeLang lang) {
    std::vector<std::string> out;
    if (prefix.size() < 2) return out;
    std::set<std::string> words;
    for (auto *s : {&keywords(lang), &types(lang), &builtins(lang)}) words.insert(s->begin(), s->end());
    for (size_t i = 0; i < text.size();) {
        if (std::isalpha((unsigned char)text[i]) || text[i] == '_') {
            size_t j = i; while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_')) j++;
            if (j - i >= 3) words.insert(text.substr(i, j - i));
            i = j;
        } else i++;
    }
    for (auto &w : words) if (w != prefix && w.compare(0, prefix.size(), prefix) == 0) out.push_back(w);
    if (out.size() < 8)
        for (auto &w : words) if (w != prefix && w.find(prefix) != std::string::npos && w.compare(0, prefix.size(), prefix) != 0) out.push_back(w);
    if (out.size() > 8) out.resize(8);
    return out;
}
static int callback(ImGuiInputTextCallbackData *d) {
    std::string *str = (std::string *)d->UserData;
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        str->resize(d->BufTextLen);
        d->Buf = str->data();
        return 0;
    }
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        if (!g_ac.list.empty() && !g_ac.hidden) {
            const std::string &w = g_ac.list[std::clamp(g_ac.sel, 0, (int)g_ac.list.size() - 1)];
            d->DeleteChars(g_ac.prefixStart, d->CursorPos - g_ac.prefixStart);
            d->InsertChars(g_ac.prefixStart, w.c_str());
            g_ac.list.clear();
        } else d->InsertChars(d->CursorPos, "    ");
        return 0;
    }
    if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        int c = d->CursorPos;
        if (c > 0 && d->Buf[c - 1] == '\n' && g_ac.lastLen >= 0 && d->BufTextLen == g_ac.lastLen + 1 && c == g_ac.lastCursor + 1) {
            int ls = c - 2;
            while (ls >= 0 && d->Buf[ls] != '\n') ls--;
            ls++;
            std::string ind;
            for (int k = ls; k < c - 1 && (d->Buf[k] == ' ' || d->Buf[k] == '\t'); k++) ind += d->Buf[k];
            int e = c - 2; while (e >= ls && d->Buf[e] == ' ') e--;
            if (e >= ls && d->Buf[e] == '{') ind += "    ";
            if (!ind.empty()) d->InsertChars(c, ind.c_str());
            c = d->CursorPos;
        }
        if (d->BufTextLen != g_ac.lastLen) g_ac.hidden = false;
        int s = c;
        while (s > 0 && (std::isalnum((unsigned char)d->Buf[s - 1]) || d->Buf[s - 1] == '_')) s--;
        g_ac.prefixStart = s; g_ac.cursor = c;
        g_ac.prefix.assign(d->Buf + s, d->Buf + c);
        if (d->BufTextLen != g_ac.lastLen || c != g_ac.lastCursor) { g_ac.list = suggestions(std::string(d->Buf, d->BufTextLen), g_ac.prefix, g_ac.lang); g_ac.sel = 0; }
        g_ac.lastLen = d->BufTextLen; g_ac.lastCursor = c;
    }
    return 0;
}

bool codeEditor(const char *id, std::string &text, ImVec2 size, CodeLang lang, const std::vector<CodeError> &errs) {
    ImGui::PushID(id);
    for (char &c : text) if (c == '\t') c = ' ';
    const float gutter = 44, lh = ImGui::GetTextLineHeight();
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = ImGui::GetContentRegionAvail().y;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + gutter, p0.y + size.y), IM_COL32(24, 25, 32, 255), 6, ImDrawFlags_RoundCornersLeft);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + gutter, p0.y));
    ImGuiID iid = ImGui::GetID("##ce");
    bool active = ImGui::GetActiveID() == iid;
    if (active) g_ac.id = iid, g_ac.lang = lang;
    bool acOpen = active && g_ac.id == iid && !g_ac.list.empty() && !g_ac.hidden;
    if (acOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { g_ac.sel = (g_ac.sel + 1) % (int)g_ac.list.size(); ImGui::SetKeyOwner(ImGuiKey_DownArrow, ImGui::GetID("##acnav"), ImGuiInputFlags_LockThisFrame); }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { g_ac.sel = (g_ac.sel + (int)g_ac.list.size() - 1) % (int)g_ac.list.size(); ImGui::SetKeyOwner(ImGuiKey_UpArrow, ImGui::GetID("##acnav"), ImGuiInputFlags_LockThisFrame); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_ac.hidden = true; ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##acnav"), ImGuiInputFlags_LockThisFrame); }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 31, 40, 255));
    if (text.capacity() < text.size() + 1) text.reserve(text.size() + 1);
    bool changed = ImGui::InputTextMultiline("##ce", text.data(), text.capacity() + 1, ImVec2(size.x - gutter, size.y),
                                             ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways,
                                             callback, &text);
    ImGui::PopStyleColor(2);
    ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
    ImGuiInputTextState *st = ImGui::GetInputTextState(iid);
    const char *buf = st && active ? st->GetText() : text.c_str();
    std::string cur = buf;
    float scrollY = 0, scrollX = 0;
    if (st && active) { scrollX = st->Scroll.x; scrollY = st->Scroll.y; }
    else {
        char wn[512]; snprintf(wn, sizeof wn, "%s/%s_%08X", ImGui::GetCurrentWindow()->Name, "##ce", iid);
        if (ImGuiWindow *w = ImGui::FindWindowByName(wn)) scrollY = w->Scroll.y;
    }
    ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImFont *font = ImGui::GetFont();
    float fs = ImGui::GetFontSize();
    dl->PushClipRect(rmin, rmax, true);
    std::map<int, std::string> errAt;
    for (auto &e : errs) if (e.line > 0) errAt[e.line] += (errAt[e.line].empty() ? "" : "\n") + e.msg;
    std::vector<Tok> toks;
    bool inCom = false;
    int line = 1;
    size_t ls = 0;
    int curLine = 1; float curX = 0;
    int cpos = st && active ? st->GetCursorPos() : -1;
    while (ls <= cur.size()) {
        size_t le = cur.find('\n', ls); if (le == std::string::npos) le = cur.size();
        std::string L = cur.substr(ls, le - ls);
        float y = rmin.y + pad.y + (line - 1) * lh - scrollY;
        if (cpos >= (int)ls && cpos <= (int)le) { curLine = line; curX = font->CalcTextSizeA(fs, FLT_MAX, 0, L.c_str(), L.c_str() + (cpos - ls)).x; }
        if (y > rmin.y - lh && y < rmax.y) {
            tokenize(L, lang, inCom, toks);
            float x = rmin.x + pad.x - scrollX;
            for (auto &t : toks) {
                dl->AddText(font, fs, ImVec2(x, y), t.c, L.c_str() + t.a, L.c_str() + t.b);
                x += font->CalcTextSizeA(fs, FLT_MAX, 0, L.c_str() + t.a, L.c_str() + t.b).x;
            }
            if (errAt.count(line)) {
                float x0 = rmin.x + pad.x - scrollX, x1 = std::max(x0 + 30, x);
                for (float xx = x0; xx < x1; xx += 4) dl->AddLine(ImVec2(xx, y + lh), ImVec2(xx + 2, y + lh - 2), C_ERR), dl->AddLine(ImVec2(xx + 2, y + lh - 2), ImVec2(xx + 4, y + lh), C_ERR);
            }
        } else { std::vector<Tok> tmp; tokenize(L, lang, inCom, tmp); }
        line++;
        if (le >= cur.size()) break;
        ls = le + 1;
    }
    int lines = line - 1;
    if (active && cpos >= 0 && fmodf((float)ImGui::GetTime(), 1.0f) < 0.6f) {
        float cx = rmin.x + pad.x - scrollX + curX, cy = rmin.y + pad.y + (curLine - 1) * lh - scrollY;
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + lh), IM_COL32(240, 240, 255, 255), 1.5f);
    }
    dl->PopClipRect();
    dl->PushClipRect(p0, ImVec2(p0.x + gutter, p0.y + size.y), true);
    std::string hoverErr;
    for (int l = 1; l <= lines; l++) {
        float y = rmin.y + pad.y + (l - 1) * lh - scrollY;
        if (y < p0.y - lh || y > p0.y + size.y) continue;
        char nb[12]; snprintf(nb, sizeof nb, "%d", l);
        bool bad = errAt.count(l) > 0;
        float tw = ImGui::CalcTextSize(nb).x;
        dl->AddText(ImVec2(p0.x + gutter - 8 - tw, y), bad ? C_ERR : (l == curLine && active ? IM_COL32(210, 210, 225, 255) : IM_COL32(110, 112, 130, 255)), nb);
        if (bad) {
            dl->AddCircleFilled(ImVec2(p0.x + 8, y + lh / 2), 4, C_ERR);
            if (ImGui::IsMouseHoveringRect(ImVec2(p0.x, y), ImVec2(rmax.x, y + lh))) hoverErr = "Línea " + std::to_string(l) + ": " + errAt[l];
        }
    }
    dl->PopClipRect();
    if (!hoverErr.empty()) ImGui::SetTooltip("%s", hoverErr.c_str());
    if (active && g_ac.id == iid && !g_ac.list.empty() && !g_ac.hidden) {
        float cx = rmin.x + pad.x - scrollX + curX, cy = rmin.y + pad.y + curLine * lh - scrollY;
        ImGui::SetNextWindowPos(ImVec2(cx, cy + 2));
        ImGui::SetNextWindowBgAlpha(0.97f);
        if (ImGui::Begin("##autocompletar", nullptr, ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            for (int i = 0; i < (int)g_ac.list.size(); i++) {
                ImU32 col = keywords(lang).count(g_ac.list[i]) ? C_KW : types(lang).count(g_ac.list[i]) ? C_TYPE : builtins(lang).count(g_ac.list[i]) ? C_FN : C_TXT;
                if (i == g_ac.sel) {
                    ImVec2 q = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q.x - 4, q.y), ImVec2(q.x + 220, q.y + lh), IM_COL32(60, 64, 110, 255), 3);
                }
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", g_ac.list[i].c_str());
            }
            ImGui::TextDisabled("Tab: completar   flechas: elegir   Esc: cerrar");
        }
        ImGui::End();
    }
    ImGui::PopID();
    return changed;
}

static int glslHeaderLines(const char *h) { int n = 0; for (; *h; h++) n += *h == '\n'; return n; }
static const char *glslHeader() {
    return rlGetVersion() == RL_OPENGL_21 ?
        "#version 120\nvarying vec2 fragTexCoord;\nvarying vec4 fragColor;\nuniform sampler2D texture0;\nuniform float u_time;\nuniform vec2 u_res;\nuniform vec3 u_cam;\nuniform float u_amount;\n" :
        "#version 330\nin vec2 fragTexCoord;\nin vec4 fragColor;\nuniform sampler2D texture0;\nuniform float u_time;\nuniform vec2 u_res;\nuniform vec3 u_cam;\nuniform float u_amount;\nout vec4 finalColor;\n";
}
static void replaceAll(std::string &s, const std::string &a, const std::string &b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
}
std::string glslForEditor(const std::string &code) {
    bool gl21 = rlGetVersion() == RL_OPENGL_21;
    std::string s = code;
    for (const char *d : {"precision highp float;", "precision mediump float;", "precision lowp float;", "varying vec2 v_uv;", "uniform sampler2D u_tex;",
                          "uniform float u_time;", "uniform vec2 u_res;", "uniform vec3 u_cam;", "uniform float u_amount;"}) replaceAll(s, d, "");
    replaceAll(s, "texture2D(", gl21 ? "texture2D(" : "texture(");
    replaceAll(s, "gl_FragColor", gl21 ? "gl_FragColor" : "finalColor");
    replaceAll(s, "v_uv", "fragTexCoord");
    replaceAll(s, "u_tex", "texture0");
    return std::string(glslHeader()) + s;
}
static std::vector<std::string> g_log;
static void logCapture(int level, const char *fmt, va_list ap) {
    char b[2048];
    vsnprintf(b, sizeof b, fmt, ap);
    g_log.push_back(b);
    (void)level;
}
std::vector<CodeError> checkGLSL(const std::string &code) {
    std::vector<CodeError> out;
    if (!IsWindowReady()) return out;
    std::string fs = glslForEditor(code);
    g_log.clear();
    SetTraceLogLevel(LOG_WARNING);
    SetTraceLogCallback(logCapture);
    unsigned int prog = rlLoadShaderCode(nullptr, fs.c_str());
    SetTraceLogCallback(nullptr);
    if (prog && prog != rlGetShaderIdDefault()) rlUnloadShaderProgram(prog);
    if (prog && prog != rlGetShaderIdDefault()) return out;
    int head = glslHeaderLines(glslHeader());
    std::regex mesa(R"(\d+:(\d+)\(\d+\):\s*error:\s*(.*))"), nv(R"(\d+\((\d+)\)\s*:\s*error\s*\w*:\s*(.*))"), amd(R"(ERROR:\s*\d+:(\d+):\s*(.*))");
    for (auto &l : g_log) {
        size_t a = 0;
        while (a < l.size()) {
            size_t b = l.find('\n', a); if (b == std::string::npos) b = l.size();
            std::string one = l.substr(a, b - a);
            std::smatch m;
            if (std::regex_search(one, m, mesa) || std::regex_search(one, m, nv) || std::regex_search(one, m, amd)) {
                int ln = std::stoi(m[1].str()) - head;
                out.push_back({ln > 0 ? ln : 1, m[2].str()});
            }
            a = b + 1;
        }
    }
    if (out.empty()) out.push_back({0, "El shader no compila (revisa la sintaxis)"});
    return out;
}

std::vector<CodeError> checkJS(const std::string &code, bool functionBody) {
    static JSRuntime *rt = nullptr;
    static JSContext *ctx = nullptr;
    if (!rt) { rt = JS_NewRuntime(); ctx = JS_NewContext(rt); }
    std::vector<CodeError> out;
    std::string src = functionBody ? "(function ($) {\n" + code + "\n})" : code;
    JSValue r = JS_Eval(ctx, src.c_str(), src.size(), "<codigo>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(r)) {
        JSValue e = JS_GetException(ctx);
        std::string msg = "Error de sintaxis";
        if (const char *m = JS_ToCString(ctx, e)) { msg = m; JS_FreeCString(ctx, m); }
        int line = 0;
        JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
        if (const char *s = JS_ToCString(ctx, stack)) {
            std::cmatch m;
            if (std::regex_search(s, m, std::regex(R"(<codigo>:(\d+))"))) line = std::stoi(m[1].str()) - (functionBody ? 1 : 0);
            JS_FreeCString(ctx, s);
        }
        JSValue ln = JS_GetPropertyStr(ctx, e, "lineNumber");
        int32_t lnv = 0;
        if (!line && JS_IsNumber(ln) && !JS_ToInt32(ctx, &lnv, ln)) line = lnv - (functionBody ? 1 : 0);
        JS_FreeValue(ctx, ln); JS_FreeValue(ctx, stack); JS_FreeValue(ctx, e);
        if (msg.rfind("SyntaxError: ", 0) == 0) msg = "Error de sintaxis: " + msg.substr(13);
        out.push_back({std::max(1, line), msg});
    }
    JS_FreeValue(ctx, r);
    return out;
}

struct FieldState { std::string checked; double changedAt = 0; std::vector<CodeError> errs; };
bool codeFieldStr(const char *id, std::string &text, ImVec2 size, CodeLang lang, bool functionBody, std::vector<CodeError> *errsOut) {
    static std::map<std::string, FieldState> states;
    std::string key = std::to_string(ImGui::GetID(id)) + id;
    FieldState &fs = states[key];
    float listH = fs.errs.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * std::min<size_t>(3, fs.errs.size()) + 4;
    if (size.y <= 0) size.y = ImGui::GetContentRegionAvail().y;
    bool ch = codeEditor(id, text, ImVec2(size.x, std::max(60.0f, size.y - listH)), lang, fs.errs);
    double now = ImGui::GetTime();
    if (ch) fs.changedAt = now;
    if (text != fs.checked && now - fs.changedAt > 0.5) {
        fs.checked = text;
        fs.errs = text.find_first_not_of(" \n\t") == std::string::npos ? std::vector<CodeError>()
                : lang == CodeLang::GLSL ? checkGLSL(text) : checkJS(text, functionBody);
    }
    for (size_t i = 0; i < fs.errs.size() && i < 3; i++) {
        const CodeError &e = fs.errs[i];
        if (e.line > 0) ImGui::TextColored(ImVec4(1, 0.42f, 0.42f, 1), "Línea %d: %s", e.line, e.msg.c_str());
        else ImGui::TextColored(ImVec4(1, 0.42f, 0.42f, 1), "%s", e.msg.c_str());
    }
    if (errsOut) *errsOut = fs.errs;
    return ch;
}
bool codeField(const char *id, json &o, const char *key, ImVec2 size, CodeLang lang, bool functionBody, std::vector<CodeError> *errsOut) {
    std::string t = jstr(o, key);
    bool ch = codeFieldStr(id, t, size, lang, functionBody, errsOut);
    if (ch) o[key] = t;
    return ch;
}

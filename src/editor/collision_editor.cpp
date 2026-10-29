#include "collision_editor.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
struct DragState { int what = -1; ImVec2 grab{0, 0}; json start; };
std::map<ImGuiID, DragState> g_drag;

float r2(float v) { return std::round(v * 2) / 2; }
json defaultMask(const std::string &shape, int sw, int sh) {
    float w = (float)sw, h = (float)sh;
    if (shape == "square" || shape == "circle") { float s = std::min(w, h); return {{"shape", shape}, {"x", r2((w - s) / 2)}, {"y", r2((h - s) / 2)}, {"w", s}, {"h", s}}; }
    if (shape == "poly") {
        return {{"shape", "poly"}, {"points", json::array({json::array({r2(w / 2), 0}), json::array({w, r2(h / 2)}), json::array({r2(w / 2), h}), json::array({0, r2(h / 2)})})}};
    }
    return {{"shape", shape}, {"x", 0}, {"y", 0}, {"w", w}, {"h", h}};
}
void bounds(const json &m, float &x0, float &y0, float &x1, float &y1) {
    if (jstr(m, "shape") == "poly" && m.contains("points") && m["points"].is_array() && !m["points"].empty()) {
        x0 = y0 = 1e9f; x1 = y1 = -1e9f;
        for (auto &p : m["points"]) {
            float x = p[0].get<float>(), y = p[1].get<float>();
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
        return;
    }
    x0 = (float)jnum(m, "x", 0); y0 = (float)jnum(m, "y", 0); x1 = x0 + (float)jnum(m, "w", 0); y1 = y0 + (float)jnum(m, "h", 0);
}
}

bool collisionEditor(const char *id, json &mask, ui::PixTexture *tex, int sw, int sh, float maxSize) {
    ImGui::PushID(id);
    bool changed = false;
    std::string shape = mask.is_object() ? jstr(mask, "shape") : "";
    static const char *ids[] = {"", "rect", "square", "circle", "ellipse", "poly"};
    static const char *names[] = {"Todo el dibujo", "Rectángulo", "Cuadrado", "Círculo", "Elipse", "Puntos"};
    for (int i = 0; i < 6; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(names[i], shape == ids[i]) && shape != ids[i]) {
            mask = i ? defaultMask(ids[i], sw, sh) : json(nullptr);
            shape = ids[i];
            changed = true;
        }
    }
    float k = std::max(1.0f, std::floor(std::min(maxSize / std::max(1, sw), maxSize / std::max(1, sh))));
    if (k * std::max(sw, sh) > maxSize) k = maxSize / std::max(sw, sh);
    float pad = 24;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImVec2 size(sw * k + pad * 2, sh * k + pad * 2);
    ImGui::InvisibleButton("##col", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hov = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a(o.x + pad, o.y + pad), b(a.x + sw * k, a.y + sh * k);
    dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(24, 24, 30, 255), 6);
    ui::checkerboard(dl, a, b, std::max(4.0f, k * 2));
    if (tex && tex->tex.id) dl->AddImage(tex->id(), a, b);
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 50));
    auto S = [&](float x, float y) { return ImVec2(a.x + x * k, a.y + y * k); };
    ImVec2 mp = ImGui::GetIO().MousePos;
    float mx = (mp.x - a.x) / k, my = (mp.y - a.y) / k;
    const ImU32 fill = IM_COL32(255, 90, 90, 70), line = IM_COL32(255, 110, 110, 255), handle = IM_COL32(255, 255, 255, 255);
    DragState &D = g_drag[ImGui::GetID("##col")];
    if (!ImGui::IsMouseDown(0)) D.what = -1;
    if (shape.empty()) {
        dl->AddRectFilled(a, b, fill);
        dl->AddRect(a, b, line, 0.0f, 0, 2.0f);
    } else if (shape == "poly") {
        json &pts = mask["points"];
        if (!pts.is_array()) pts = json::array();
        std::vector<ImVec2> sp;
        for (auto &p : pts) sp.push_back(S(p[0].get<float>(), p[1].get<float>()));
        if (sp.size() >= 3) dl->AddConcavePolyFilled(sp.data(), (int)sp.size(), fill);
        if (!sp.empty()) dl->AddPolyline(sp.data(), (int)sp.size(), line, ImDrawFlags_Closed, 2.0f);
        int over = -1;
        for (int i = 0; i < (int)sp.size(); i++) if (std::fabs(mp.x - sp[i].x) < 7 && std::fabs(mp.y - sp[i].y) < 7) over = i;
        for (int i = 0; i < (int)sp.size(); i++) dl->AddCircleFilled(sp[i], i == over || D.what == 10 + i ? 6.5f : 5, i == over ? ui::kPrimaryLight : handle);
        if (hov && ImGui::IsMouseClicked(0)) {
            if (over >= 0) D.what = 10 + over;
            else {
                int best = (int)pts.size(); float bd = 1e18f;
                for (int i = 0; i < (int)sp.size(); i++) {
                    ImVec2 p0 = sp[i], p1 = sp[(i + 1) % sp.size()];
                    float dx = p1.x - p0.x, dy = p1.y - p0.y, l2 = dx * dx + dy * dy, t = l2 > 0 ? std::clamp(((mp.x - p0.x) * dx + (mp.y - p0.y) * dy) / l2, 0.0f, 1.0f) : 0;
                    float ex = p0.x + dx * t - mp.x, ey = p0.y + dy * t - mp.y, d = ex * ex + ey * ey;
                    if (d < bd) { bd = d; best = i + 1; }
                }
                pts.insert(pts.begin() + best, json::array({r2(mx), r2(my)}));
                D.what = 10 + best; changed = true;
            }
        }
        if (hov && ImGui::IsMouseClicked(1) && over >= 0 && pts.size() > 3) { pts.erase(pts.begin() + over); changed = true; }
        if (D.what >= 10 && D.what - 10 < (int)pts.size() && ImGui::IsMouseDown(0)) {
            json np = json::array({r2(std::clamp(mx, -(float)sw, 2.0f * sw)), r2(std::clamp(my, -(float)sh, 2.0f * sh))});
            if (pts[D.what - 10] != np) { pts[D.what - 10] = np; changed = true; }
        }
        if (hov) ImGui::SetTooltip("Arrastra los puntos · clic en un hueco: punto nuevo · clic derecho en un punto: quitarlo");
    } else {
        float x0, y0, x1, y1;
        bounds(mask, x0, y0, x1, y1);
        ImVec2 p0 = S(x0, y0), p1 = S(x1, y1);
        if (shape == "circle" || shape == "ellipse") {
            ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
            dl->AddEllipseFilled(c, ImVec2((p1.x - p0.x) / 2, (p1.y - p0.y) / 2), fill, 0, 40);
            dl->AddEllipse(c, ImVec2((p1.x - p0.x) / 2, (p1.y - p0.y) / 2), line, 0, 40, 2);
            dl->AddRect(p0, p1, IM_COL32(255, 110, 110, 70));
        } else {
            dl->AddRectFilled(p0, p1, fill);
            dl->AddRect(p0, p1, line, 0.0f, 0, 2.0f);
        }
        ImVec2 cs[4] = {p0, ImVec2(p1.x, p0.y), p1, ImVec2(p0.x, p1.y)};
        int over = -1;
        for (int i = 0; i < 4; i++) if (std::fabs(mp.x - cs[i].x) < 7 && std::fabs(mp.y - cs[i].y) < 7) over = i;
        bool inside = mp.x > p0.x && mp.x < p1.x && mp.y > p0.y && mp.y < p1.y;
        for (int i = 0; i < 4; i++) dl->AddRectFilled(ImVec2(cs[i].x - 5, cs[i].y - 5), ImVec2(cs[i].x + 5, cs[i].y + 5), i == over || D.what == i ? ui::kPrimaryLight : handle, 2);
        if (hov && over >= 0) ImGui::SetMouseCursor(over % 2 ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeNWSE);
        else if (hov && inside) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (hov && ImGui::IsMouseClicked(0)) {
            if (over >= 0) D.what = over;
            else if (inside) { D.what = 4; D.grab = ImVec2(mx - x0, my - y0); }
            D.start = mask;
        }
        if (D.what >= 0 && D.what <= 4 && ImGui::IsMouseDown(0)) {
            float nx0 = x0, ny0 = y0, nx1 = x1, ny1 = y1, qx = r2(mx), qy = r2(my);
            if (D.what == 4) { float w = x1 - x0, h = y1 - y0; nx0 = r2(mx - D.grab.x); ny0 = r2(my - D.grab.y); nx1 = nx0 + w; ny1 = ny0 + h; }
            else {
                if (D.what == 0 || D.what == 3) nx0 = std::min(qx, x1 - 1); else nx1 = std::max(qx, x0 + 1);
                if (D.what == 0 || D.what == 1) ny0 = std::min(qy, y1 - 1); else ny1 = std::max(qy, y0 + 1);
                if (shape == "square" || shape == "circle") {
                    float s = std::max(nx1 - nx0, ny1 - ny0);
                    if (D.what == 0 || D.what == 3) nx0 = nx1 - s; else nx1 = nx0 + s;
                    if (D.what == 0 || D.what == 1) ny0 = ny1 - s; else ny1 = ny0 + s;
                }
            }
            json nm = mask;
            nm["x"] = nx0; nm["y"] = ny0; nm["w"] = nx1 - nx0; nm["h"] = ny1 - ny0;
            if (nm != mask) { mask = nm; changed = true; }
        }
        if (hov && over < 0) ImGui::SetTooltip("Arrastra las esquinas para cambiar el tamaño; desde dentro, para moverla");
    }
    if (!shape.empty() && shape != "poly") {
        float v[4] = {(float)jnum(mask, "x", 0), (float)jnum(mask, "y", 0), (float)jnum(mask, "w", 0), (float)jnum(mask, "h", 0)};
        ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::DragFloat4("X  Y  Ancho  Alto", v, 0.5f, -4096, 4096, "%.1f")) {
            if (shape == "square" || shape == "circle") v[3] = v[2] = std::max(v[2] != (float)jnum(mask, "w", 0) ? v[2] : v[3], 1.0f);
            mask["x"] = v[0]; mask["y"] = v[1]; mask["w"] = std::max(1.0f, v[2]); mask["h"] = std::max(1.0f, v[3]);
            changed = true;
        }
    } else if (shape == "poly") ImGui::TextDisabled("%d puntos. Se usa como forma convexa (sin entrantes).", (int)mask["points"].size());
    else ImGui::TextDisabled("Choca con todo el rectángulo del dibujo.");
    ImGui::PopID();
    return changed;
}

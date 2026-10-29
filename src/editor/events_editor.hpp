#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <map>
#include <string>
#include <vector>
#include "assets.hpp"

struct InstrDefs {
    json d;
    json ext = {{"cond", json::object()}, {"act", json::object()}};
    static InstrDefs &get();
    const json *def(const std::string &kind, const std::string &type) const;
    std::vector<std::pair<std::string, const json *>> all(const std::string &kind) const;
    std::string gdCat(const json &def) const;
};
std::map<std::string, std::string> refreshExtensionDefs(Project &proj);

struct Seg { std::string text; ImU32 col; };
std::vector<Seg> sentence(Project &proj, const std::string &kind, const json &row);
std::string sentencePlain(Project &proj, const std::string &kind, const json &row);
json newInstruction(Project &proj, const std::string &kind, const std::string &type, const std::string &objId = "");
std::string checkExpression(Project &proj, const std::string &expr);

class EventsEditor {
public:
    std::string sceneId;
    bool open = true, focusMe = true, scrollToEnd = false;
    void draw(Project &proj);
    std::string title(Project &proj);
    json &events(Project &proj);

    int sel = -1;
    int addEvent(Project &proj, const std::string &type, bool asSub);
    void addComment(Project &proj);
    void addGroup(Project &proj);
    json *instrList(json &ev, const std::string &path, bool create);
    json *instrAt(json &ev, const std::string &path, json **list, int *idx);
    void deleteEvent(Project &proj, int i);
    bool undo(Project &proj) { return hist.doUndo(events(proj)); }
    bool redo(Project &proj) { return hist.doRedo(events(proj)); }
    void openDialog(Project &proj, const std::string &kind, int ev, const std::string &path, bool edit);
    void dialogPick(Project &proj, const std::string &type);
    void dialogAccept(Project &proj);

private:
    JsonHistory hist;
    char search[96] = {0};
    void drawPresence(json &evs, const std::map<std::string, ImRect> &rects);
    int selEv = -1; std::string selPath;
    json clipEvents = json::array(); json clipInstr; std::string clipKind;
    struct Dialog {
        bool open = false, form = false, edit = false, justOpened = false;
        std::string kind, path, objId, cat, q;
        int ev = -1, tab = 0;
        json row;
    } dlg;
    std::vector<std::string> pendingFocus;

    int blockEnd(json &evs, int i);
    void drawToolbar(Project &proj);
    void drawEvent(Project &proj, json &evs, int i);
    void drawInstrColumn(Project &proj, json &ev, int evIdx, const std::string &kind, const std::string &root, json &list, float width);
    bool drawInstr(Project &proj, json &ev, int evIdx, const std::string &kind, const std::string &path, json &row, float width);
    void drawDialog(Project &proj);
    void dialogList(Project &proj);
    void dialogForm(Project &proj);
    void eventContext(Project &proj, json &evs, int i);
    bool matchesSearch(Project &proj, const json &ev);
    void shortcuts(Project &proj);
};

#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "assets.hpp"
#include "tiles_obj.hpp"
#include "object_editor.hpp"
#include <memory>

class SceneEditor {
public:
    enum Mode { Select, Place, Tiles };
    std::string sceneId;
    bool open = true, focusMe = true;
    std::function<void(const std::string &)> onOpenObject, onPlayScene;
    std::function<void()> onNewObject;
    std::function<void(const std::string &)> onOpenTileset;
    std::function<void()> onOpenLayers;
    std::function<void(ObjectEditor &)> onSetupObject;
    std::unique_ptr<ObjectEditor> objView;
    void editObject(const std::string &objId);
    std::function<std::string()> onNewTileObject;

    void draw(Project &proj);
    std::string title(Project &proj);

    Mode mode = Select;
    std::string placeObj, paintObj;
    int paintTile = 0;
    bool newPiece = false;
    std::vector<std::string> sel;
    std::string placeAt(Project &proj, json &sc, float wx, float wy);
    void deleteSelected(json &sc);
    void duplicateSelected(json &sc);
    void paintCell(Project &proj, json &sc, float wx, float wy, int tile);
    std::string pickAt(Project &proj, json &sc, float wx, float wy);
    void moveSelected(json &sc, float dx, float dy);
    bool undo(json &sc) { return hist.doUndo(sc); }
    bool redo(json &sc) { return hist.doRedo(sc); }

private:
    float zoom = 1, panX = 40, panY = 40;
    bool fitPending = true, grid = true, snap = true, userView = false;
    ImVec2 lastAvail{0, 0};
    enum Drag { DNone, DMove, DBox, DScale, DPan, DPaint } drag = DNone;
    ImVec2 dragStart, boxA;
    std::map<std::string, ImVec2> startPos;
    float startSX = 1, startSY = 1;
    char search[64] = {0};
    std::vector<json> clip;
    JsonHistory hist;
    ImVec2 canvasPos;
    std::string popId;
    double popAt = -1;
    ImVec2 ghost{0, 0};
    bool ghostOn = false;

    ImVec2 toScreen(float x, float y) const { return ImVec2(canvasPos.x + panX + x * zoom, canvasPos.y + panY + y * zoom); }
    ImVec2 toWorld(ImVec2 s) const { return ImVec2((s.x - canvasPos.x - panX) / zoom, (s.y - canvasPos.y - panY) / zoom); }
    float snapV(float v, float g) const;
    void drawObjectsPanel(Project &proj, json &sc);
    void drawToolbar(Project &proj, json &sc);
    void drawCanvas(Project &proj, json &sc);
    void drawProps(Project &proj, json &sc);
    void instanceProps(Project &proj, json &sc, json &inst);
    void sceneProps(Project &proj, json &sc);
    void tilePaintProps(Project &proj, json &sc);
    json *paintTarget(Project &proj, json &sc, bool create);
    void layersProps(Project &proj);
    void shortcuts(Project &proj, json &sc);
};

json &projectLayers(Project &proj);

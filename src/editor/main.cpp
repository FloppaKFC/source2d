// Source 2D - editor nativo (un solo ejecutable, sin dependencias: raylib estatico + Dear ImGui).
// Uso: source2d [proyecto.source2d.json] [--script guion.txt]
#include "s2d_fallback.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include "imgui.h"
#include "imgui_internal.h"
#include "raylib.h"
#include "s2d_diag.h"
#include "rlgl.h"
#include "rlImGui.h"
#include "project.hpp"
#include "sprite_editor.hpp"
#include "object_editor.hpp"
#include "scene_editor.hpp"
#include "events_editor.hpp"
#include "tileset_editor.hpp"
#include "layers_autotiles.hpp"
#include "sound_studio.hpp"
#include "tiles_obj.hpp"
#include "fonts.hpp"
#include "project_docs.hpp"
#include "assets.hpp"
#include "ui.hpp"
#include "thumbnails.hpp"
#include "collab.hpp"
#include "exporter.hpp"
#include "licencias_editor.h"
#include "sys.hpp"
#include "version.hpp"
#include "school.hpp"
#include "presets.h"
#include "novedades.hpp"
#include "firma_publica.hpp"

namespace fs = std::filesystem;

/* ---------------- estado de la aplicacion ---------------- */
struct App {
    Project proj;
    bool hasProject = false;
    std::vector<std::unique_ptr<SpriteEditor>> sprites;
    std::vector<std::unique_ptr<ObjectEditor>> objects;
    std::vector<std::unique_ptr<SceneEditor>> scenes;
    std::vector<std::unique_ptr<EventsEditor>> sheets;   // una hoja por escena y la de eventos comunes
    std::vector<std::unique_ptr<TilesetEditor>> tilesets;
    std::vector<std::unique_ptr<FontEditor>> fonts;
    std::unique_ptr<SettingsEditor> settings;
    std::unique_ptr<LayersEditor> layersDoc;
    std::unique_ptr<AutotilesEditor> autotiles;
    std::vector<std::unique_ptr<sound::Editor>> sounds;   // Source Sound dentro del proyecto (uno por sonido)
    std::unique_ptr<ShadersEditor> shaders;
    std::unique_ptr<ExtensionsEditor> extensions;
    ImportImage importer;
    std::string importTileObj;                            // objeto Tile que recibe el conjunto importado
    std::string pendingDeleteTileset, pendingDeleteMusic, pendingDeleteFont, pendingDeleteProject, deletingProject;
    bool scrollEventsToEnd = false, scriptCommon = false;   // guion: ev.* sobre los eventos comunes
    bool showNewObject = false;
    std::string pendingDeleteObj, pendingDeleteScene;
    ui::FileDialog fileDlg;
    std::string toast; double toastUntil = 0; bool toastErr = false;
    std::vector<std::string> recent;
    bool askClose = false, quit = false, wantLayout = true, openFirstScene = false;
    bool vulkan = false;          // version experimental "(Vulkan)": los juegos se dibujan con Vulkan
    bool showExperimental = false;
    bool showTree = false, focusWelcome = false;         // panel del proyecto (cajon por encima de todo) abierto, ir a Inicio
    bool inPanel = false, closePanel = false;            // abrir algo desde el panel lo cierra
    ImVec2 treeBtnMin, treeBtnMax;                       // boton que abre y cierra el panel
    bool eventsFirst = false;                             // al abrir: la hoja de eventos delante (Ver por dentro)
    int focusEventsIn = 0;                                // fotogramas que faltan para ponerla delante (tras rehacer los paneles)
    std::vector<std::string> tabOrder; std::string activeTab;   // fila de pestanas
    std::map<std::string, ui::PixTexture> thumbs;
    std::map<std::string, size_t> thumbHash;
    std::vector<sys::Proc> children;
    std::string tmpDir;
    char newName[96] = "Mi juego";
    int newRes = 0, newW = 1280, newH = 720;       // resolucion del proyecto nuevo (0-2 hechas, 3 a mano)
    bool liteExport = true;               // Publicar: exportacion ligera (comprimida) o normal
    bool showExport = false;              // ventana Exportar
    bool exportFolder = false;            // exportar a una carpeta (motor + datos/juego.source2d.json)
    int exportKind = 0;                   // 0 ligera, 1 pesada, 2 optimizada
    bool exWin = true, exLinux = true, exWeb = false, exAndroid = false, exSource = false;
    std::string exportDir;
    int homeSection = 0;                                  // pagina de inicio: 0 Crear, 1 Juegos, 2 Plantillas, 3 Escuela, 5 Noticias
    std::string pendingLink;                              // enlace de una noticia: se confirma antes de abrirlo
    double lastAutosave = 0;                              // guardado automatico (cada Opciones > segundos)
    double lastRecovery = 0;                              // experimental: copia de recuperacion
    bool showOptions = false, showVersions = false, scriptVersions = false;
    std::vector<std::string> recoveries;                  // al arrancar: lo que quedo de un cierre de golpe
    bool askNew = false, askSwitch = false;
    std::function<void()> pendingSwitch;                   // lo que se hara tras confirmar los cambios sin guardar
    bool askTrust = false, trustHasCode = false, scriptTrustCheck = false;   // (guion: "openajeno" comprueba la confianza)
    bool showLicenses = false;                            // Ayuda > Licencias de terceros
    // trabajar juntos (collab.hpp): ventana, pestana (0 compartir, 1 unirse), modo de conexion y datos para entrar
    bool showCollab = false; int collabTab = 0, collabMode = 0, scriptWaitTries = 0;
    char joinAddr[128] = "", joinCode[16] = "";
    std::string collabMsg; bool collabMsgErr = false;
    std::string scriptPresDoc; float scriptPresX = 0, scriptPresY = 0;   // guion: presencia fija (colab.pres)
    std::string pendingTrust;                             // proyecto ajeno a la espera de que el usuario confie en el
    std::string forgetRecent;
    bool showNewSprite = false; char nsName[64] = "Sprite"; int nsW = 16, nsH = 16;
    std::string pendingDelete;
    ImGuiID dockCenter = 0;
    // pruebas automaticas
    std::vector<std::string> script; size_t scriptPos = 0; int scriptWait = 0, scriptRetry = 0; std::string shotPath;
    int mouseUpIn = 0;                                    // guion: fotogramas hasta soltar el boton tras "click"
    int perfFrames = 0, perfN = 0; double perfStart = 0, perfMax = 0, perfLast = 0;   // guion: "perf N" mide N fotogramas
    int fails = 0;
} app;

static void notify(const std::string &msg, bool err = false) {
    app.toast = msg; app.toastErr = err; app.toastUntil = GetTime() + 3.5;
    fprintf(err ? stderr : stdout, "Source 2D: %s\n", msg.c_str());
}

/* ---------------- recientes ---------------- */
static std::string recentFile() { return ui::configDir() + "/recientes.txt"; }
static void loadRecent() {
    app.recent.clear();
    if (!app.script.empty() && !getenv("S2D_PRUEBA_RECIENTES")) return;   // las pruebas usan un HOME propio
    std::ifstream in(recentFile());
    std::string l;
    while (std::getline(in, l)) if (!l.empty() && fs::exists(l)) app.recent.push_back(l);
}
static void pushRecent(const std::string &p) {
    if (!app.script.empty()) return;            // las pruebas automaticas no tocan la configuracion del usuario
    std::string abs = fs::absolute(p).string();
    app.recent.erase(std::remove(app.recent.begin(), app.recent.end(), abs), app.recent.end());
    app.recent.insert(app.recent.begin(), abs);
    if (app.recent.size() > 12) app.recent.resize(12);
    std::ofstream out(recentFile());
    for (auto &r : app.recent) out << r << "\n";
}

/* ---------------- proyecto ---------------- */
static void syncEditors() {
    for (auto &e : app.sprites) {
        if (!e->dirtyForProject) continue;
        if (json *sp = app.proj.spriteById(e->spriteId)) { e->writeTo(*sp); app.proj.dirty = true; app.proj.rev++; }
        e->dirtyForProject = false;
    }
}

/* ---------------- proyectos de confianza ----------------
   Un proyecto lleva eventos y JavaScript que al probarlo o exportarlo corren en el jugador, y el jugador puede leer,
   escribir y borrar archivos (__native.fileWrite...). Por eso, antes de abrir uno que no es del usuario (fuera de la
   carpeta de proyectos, nunca abierto ni guardado aqui) se pide confirmacion. */
static std::string projectsDir();
static std::string trustedFile() { return ui::configDir() + "/confiables.txt"; }
static std::string normPath(const std::string &p) {
    std::error_code ec;
    fs::path a = fs::weakly_canonical(fs::absolute(p, ec), ec);
    return (ec ? fs::absolute(p) : a).lexically_normal().string();
}
static bool isTrusted(const std::string &file) {
    std::string f = normPath(file), dir = normPath(projectsDir());
    if (!dir.empty() && f.size() > dir.size() && f.compare(0, dir.size(), dir) == 0 && (f[dir.size()] == '/' || f[dir.size()] == '\\')) return true;
    for (auto &r : app.recent) if (normPath(r) == f) return true;
    std::ifstream in(trustedFile());
    std::string l;
    while (std::getline(in, l)) if (l == f) return true;
    return false;
}
static void addTrusted(const std::string &file) {
    if (!app.script.empty() || isTrusted(file)) return;
    std::ofstream out(trustedFile(), std::ios::app);
    out << normPath(file) << "\n";
}
// true si el proyecto trae codigo JavaScript a la vista (extensiones, plugins o acciones "Codigo JavaScript")
static bool projectHasCode(const json &doc) {
    for (const char *k : {"extensions", "plugins"})
        if (doc.contains(k) && doc[k].is_array() && !doc[k].empty()) return true;
    std::function<bool(const json &)> walk = [&](const json &j) {
        if (j.is_object()) {
            auto t = j.find("type");
            if (t != j.end() && t->is_string() && *t == "jsCode") return true;
            for (auto &v : j) if ((v.is_object() || v.is_array()) && walk(v)) return true;
        } else if (j.is_array()) {
            for (auto &v : j) if ((v.is_object() || v.is_array()) && walk(v)) return true;
        }
        return false;
    };
    return doc.contains("scenes") && walk(doc["scenes"]);
}

static std::string newProjectPath(const std::string &name);
static void refreshProjectList();
static void openProject(std::string file, bool trusted = false) {
    Project p;
    std::string err;
    if (!p.load(file, err)) { notify(err, true); return; }
    if (!trusted && (app.script.empty() || app.scriptTrustCheck) && !isTrusted(file)) {
        app.pendingTrust = file; app.trustHasCode = projectHasCode(p.doc); app.askTrust = true;
        return;
    }
    if (collab::role() != collab::Role::None) { collab::stop(); notify("Se terminó la sesión compartida"); }
    addTrusted(file);
    app.proj = std::move(p);
    app.hasProject = true;
    if (fs::path(file).extension() == ".source2d") {   // recibido de otro Source 2D: pasa a ser uno de tus proyectos
        std::string dst = newProjectPath(app.proj.name());
        std::error_code ec;
        fs::create_directories(fs::path(dst).parent_path(), ec);
        app.proj.path = dst;
        std::string err;
        if (app.proj.save(dst, err)) { addTrusted(dst); refreshProjectList(); }
        file = dst;
    }
    if (jnum(app.proj.doc, "engineNum", 0) > S2D_VERSION_NUM)
        notify("Este proyecto se guardó con " + jstr(app.proj.doc, "engine") + ", más nueva que esta (" S2D_VERSION "): elige esa versión en Inicio", true);
    app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear(); app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset(); musicPreview(app.proj, "");
    loadPaletteFrom(app.proj.doc);
    pushRecent(file);
    app.proj.rev++;
    app.openFirstScene = true;
    app.wantLayout = true;
    notify("Proyecto abierto: " + app.proj.name());
}

// cerrar el proyecto y volver a la pagina de inicio (si hay cambios sin guardar, pregunta antes)
static void guardSwitch(std::function<void()> f);
static void refreshProjectList();
static void closeProject() {
    if (!app.hasProject) return;
    guardSwitch([] {
        std::string nm = app.proj.name();
        school::projectClosed();
        collab::stop();
        musicPreview(app.proj, "");
        app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear();
        app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset();
        app.proj = Project{};
        app.hasProject = false; app.showTree = false; app.activeTab = "Inicio"; app.homeSection = 0;
        refreshProjectList();
        notify("Proyecto cerrado: " + nm);
    });
}

static void saveVersion(const std::string &kind);
static void clearRecovery();
static bool saveProject(const std::string &file) {
    syncEditors();
    app.proj.doc["engine"] = S2D_VERSION;
    app.proj.doc["engineNum"] = S2D_VERSION_NUM;
    std::string err;
    if (!app.proj.save(file, err)) { notify(err, true); return false; }
    saveVersion("guardado");
    clearRecovery();
    pushRecent(file);
    addTrusted(file);
    notify("Guardado: " + fs::path(file).filename().string());
    return true;
}

static std::string projectsDir();

static void saveAs() {
    std::string start = app.proj.path.empty() ? projectsDir() : fs::path(app.proj.path).parent_path().string();
    std::string sug = app.proj.path.empty() ? app.proj.name() + ".source2d.json" : fs::path(app.proj.path).filename().string();
    for (char &c : sug) if (c == ' ') c = '_';
    app.fileDlg.show(ui::FileDialog::Save, "Guardar proyecto", ".source2d.json", start, sug, [](const std::string &f) { saveProject(f); });
}
static void save() { if (app.proj.path.empty()) saveAs(); else saveProject(app.proj.path); }

static void openDialog() {
    std::string start = app.hasProject && !app.proj.path.empty() ? fs::path(app.proj.path).parent_path().string() : ui::homeDir();
    app.fileDlg.show(ui::FileDialog::Open, "Abrir proyecto", ".json|.source2d", start, "", [](const std::string &f) { openProject(f); });
}
// Exportar > Solo para Source 2D: el proyecto entero en un .source2d (el mismo JSON) para pasarlo a otro Source 2D
// (comprimido como la exportacion ligera; en segundo plano, que un proyecto grande tarda unos segundos)
static std::future<std::string> g_sourceExport;   // "" si salio bien, si no el error
static std::string g_sourceExportFile;
static void exportSourceGame(const std::string &f) {
    if (g_sourceExport.valid()) { notify("Ya se está exportando un .source2d", true); return; }
    syncEditors();
    json d = app.proj.doc;
    d["engine"] = S2D_VERSION;
    d["engineNum"] = S2D_VERSION_NUM;
    g_sourceExportFile = f;
    notify("Comprimiendo " + fs::path(f).filename().string() + "...");
    g_sourceExport = std::async(std::launch::async, [d, f]() -> std::string {
        std::string bytes = exporter::sourcePack(d);
        if (bytes.empty()) return "No se pudo comprimir el proyecto";
        std::ofstream out(f, std::ios::binary);
        if (!(out << bytes)) return "No se pudo escribir " + f;
        return "";
    });
}
static bool sourceExportTick(bool wait = false) {   // true mientras siga
    if (!g_sourceExport.valid()) return false;
    if (!wait && g_sourceExport.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    std::string err = g_sourceExport.get();
    std::error_code ec;
    if (err.empty()) notify("Exportado para Source 2D: " + fs::path(g_sourceExportFile).filename().string() + " (" + exporter::kb((long)fs::file_size(g_sourceExportFile, ec)) + ")");
    else notify(err, true);
    return false;
}

static void newProject(const std::string &name) {
    app.proj = Project{};
    app.proj.newBlank(name.empty() ? "Mi juego" : name);
    app.hasProject = true;
    app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear(); app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset(); musicPreview(app.proj, "");
    app.wantLayout = true;
    app.openFirstScene = true;
    loadPaletteFrom(app.proj.doc);
    notify("Proyecto nuevo. Guárdalo con Ctrl+S");
}

/* ---------------- jugador incrustado: probar y exportar ---------------- */
// jugador de un sistema ("linux" o "windows") descomprimido de los que lleva el editor a la carpeta temporal ('' si no lo lleva)
static std::string playerFor(const std::string &os) {
    if (app.tmpDir.empty()) app.tmpDir = sys::makeTempDir();
    if (app.tmpDir.empty()) return "";
    bool win = os == "windows";
    std::string p = app.tmpDir + (win ? "/source2d-player.exe" : "/source2d-player");
    std::error_code ec;
    unsigned long want = exporter::playerSize(os);
    if (!want) return "";
    if (!fs::exists(p) || fs::file_size(p, ec) != want) {   // si una escritura anterior fallo, se vuelve a escribir
        std::vector<unsigned char> d;
        if (!exporter::unpackPlayer(os, d)) return "";
        std::ofstream out(p, std::ios::binary);
        out.write((const char *)d.data(), (std::streamsize)d.size());
        out.close();
        if (!out || fs::file_size(p, ec) != d.size()) { fs::remove(p, ec); return ""; }
        sys::setExecutable(p);
    }
    return p;
}
#ifdef _WIN32
static const char *kHostOS = "windows";
#else
static const char *kHostOS = "linux";
#endif
static std::string playerPath() { return playerFor(kHostOS); }   // el que funciona en este sistema

static std::string writeTempProject(const std::string &firstScene = "") {
    syncEditors();
    std::string pl = playerPath();
    if (pl.empty()) return "";
    std::string f = app.tmpDir + "/prueba.source2d.json";
    json doc = app.proj.doc;
    if (!firstScene.empty() && doc.contains("scenes")) {      // probar desde una escena: se pone primera
        json &scs = doc["scenes"];
        for (size_t i = 0; i < scs.size(); i++) if (jstr(scs[i], "id") == firstScene) { json s = scs[i]; scs.erase(scs.begin() + i); scs.insert(scs.begin(), s); break; }
    }
    std::ofstream out(f);
    out << doc.dump();
    return f;
}

static void spawnPlayer(const std::string &pl, const std::string &f) {
    sys::Proc pr;
    std::vector<std::string> args = {pl};
    if (app.vulkan) args.push_back("--vulkan");
    args.push_back(f);
    if (sys::spawn(args, &pr)) { app.children.push_back(pr); notify("Ejecutando el juego..."); }
    else notify("No se pudo ejecutar el jugador", true);
}

// prueba un proyecto que no esta abierto (los juegos de ejemplo del apartado Prueba)
static void playDoc(const json &doc) {
    std::string pl = playerPath();
    if (pl.empty()) { notify("No se pudo preparar la prueba", true); return; }
    std::string f = app.tmpDir + "/ejemplo.source2d.json";
    std::ofstream(f) << doc.dump();
    spawnPlayer(pl, f);
}

// antes de jugar: condiciones y acciones que usan objetos borrados (el juego las ignoraria sin decir nada)
static void checkProjectProblems() {
    std::set<std::string> ids;
    for (auto &o : app.proj.doc["objects"]) ids.insert(jstr(o, "id"));
    int bad = 0; std::string first;
    std::function<void(const json &, const std::string &)> scan = [&](const json &list, const std::string &where) {
        if (!list.is_array()) return;
        for (auto &row : list) {
            if (!row.is_object()) continue;
            if (row.contains("params") && row["params"].is_object())
                for (const char *k : {"object", "with", "target", "image"}) {
                    std::string v = jstr(row["params"], k);
                    if (!v.empty() && v.rfind("o_", 0) == 0 && !ids.count(v)) { if (!bad++) first = where; }
                }
            if (row.contains("sub")) scan(row["sub"], where);
        }
    };
    auto evs = [&](const json &L, const std::string &where) {
        if (!L.is_array()) return;
        for (auto &e : L) { scan(e.value("conditions", json::array()), where); scan(e.value("actions", json::array()), where); }
    };
    evs(app.proj.doc.value("events", json::array()), "eventos globales");
    for (auto &sc : app.proj.doc["scenes"]) evs(sc.value("events", json::array()), "escena " + jstr(sc, "name"));
    if (bad) notify(std::to_string(bad) + " instrucción(es) usan objetos que ya no existen (en " + first + ")", true);
}

static void playProject(const std::string &firstScene = "") {
    checkProjectProblems();
    school::note("preview");
    std::string pl = playerPath(), f = writeTempProject(firstScene);
    if (pl.empty() || f.empty()) { notify("No se pudo preparar la prueba", true); return; }
    // vista previa con el depurador: F3 panel, F6 pausa, F7 un fotograma
    sys::Proc pr;
    std::vector<std::string> args = {pl, "--debug"};
    if (app.vulkan) args.push_back("--vulkan");
    args.push_back(f);
    if (sys::spawn(args, &pr)) { app.children.push_back(pr); notify("Ejecutando el juego (F3: depurador)"); }
    else notify("No se pudo ejecutar el jugador", true);
}

// exporta un unico ejecutable para Linux o Windows (ver exporter.hpp): normal o ligera (todo comprimido). Va en
// segundo plano: comprimir un proyecto grande tarda unos segundos y el editor sigue respondiendo.
struct ExportJob { std::future<std::string> done; std::string out; };
static std::vector<ExportJob> g_exports;
static json optimizedDoc(json doc);
static void exportGame(const std::string &out, const std::string &os, bool lite, bool optimize = false, bool folder = false) {
    if (!app.hasProject) { notify("Abre un proyecto para exportarlo", true); return; }
    syncEditors();
    notify("Exportando " + fs::path(out).filename().string() + "...");
    school::note("export");
    json edoc = optimize ? optimizedDoc(app.proj.doc) : app.proj.doc;
    if (app.vulkan && os != "html") edoc["settings"]["renderer"] = "vulkan";   // el juego exportado arranca con Vulkan
    g_exports.push_back({std::async(std::launch::async, [doc = std::move(edoc), out, os, lite, folder] {
        std::string err, info;
        if (folder) {
            std::string nm = fs::path(out).filename().string();
            nm = nm.substr(0, nm.rfind('-'));
            if (!exporter::exportFolder(doc, out, os, nm, err, info)) return "!" + err;
            if (os == "linux") sys::setExecutable((fs::path(out) / (nm + ".x86_64")).string());
            return "Juego exportado (" + info + "): " + out;
        }
        if (!exporter::exportGame(doc, out, os, lite, err, info)) return "!" + err;
        sys::setExecutable(out);
        return "Juego exportado (" + info + "): " + out;
    }), out});
}
static bool exportsRunning() { return !g_exports.empty(); }
static void exportTick() {
    for (size_t i = 0; i < g_exports.size();) {
        if (g_exports[i].done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { i++; continue; }
        std::string r = g_exports[i].done.get();
        if (!r.empty() && r[0] == '!') notify(r.substr(1), true); else notify(r);
        g_exports.erase(g_exports.begin() + i);
    }
}
static bool canExport(const std::string &os) { return exporter::available(os, false); }

static void reapChildren() {
    app.children.erase(std::remove_if(app.children.begin(), app.children.end(), [](sys::Proc &p) { return sys::finished(p); }), app.children.end());
}

/* ---------------- sprites del proyecto ---------------- */
static std::string newSprite(const std::string &name, int w, int h);
static void openStudio();
static void openedFromPanel() { if (app.inPanel) app.closePanel = true; }
static SpriteEditor *openSprite(const std::string &id) {
    openedFromPanel();
    school::note("sprite");
    for (auto &e : app.sprites) if (e->spriteId == id) { e->focusMe = true; return e.get(); }
    json *sp = app.proj.spriteById(id);
    if (!sp) return nullptr;
    auto e = std::make_unique<SpriteEditor>();
    e->load(*sp);
    if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);
    app.sprites.push_back(std::move(e));
    return app.sprites.back().get();
}

static void openTileset(const std::string &id);
static void importImageDialog(bool tiles);
static void openSound(const std::string &musicId);
static std::string newProjectSound(bool sfx);
static void addFloppaSong();
template <class T> static void openDoc(std::unique_ptr<T> &d);
static void setupObjectEditor(ObjectEditor &e) {
    e.onOpenSprite = [](const std::string &sid) { openSprite(sid); };
    e.onNewSprite = [](const std::string &nm, int w, int h) { return newSprite(nm, w, h); };
    e.onOpenTileset = [](const std::string &tid) { openTileset(tid); };
    e.onImportTiles = [](const std::string &oid) { app.importTileObj = oid; importImageDialog(true); };
    e.onSpriteChanged = [](const std::string &sid) {   // sus frames cambiaron fuera de Source Sprite: se recargan
        if (json *sp = app.proj.spriteById(sid)) for (auto &se : app.sprites) if (se->spriteId == sid) se->load(*sp);
    };    e.onStatus = [](const std::string &m) { notify(m); };
}

static void openObject(const std::string &id) {
    openedFromPanel();
    for (auto &e : app.objects) if (e->objectId == id) { e->focusMe = true; return; }
    if (!findById(app.proj.doc["objects"], id)) return;
    auto e = std::make_unique<ObjectEditor>();
    e->objectId = id;
    setupObjectEditor(*e);
    app.objects.push_back(std::move(e));
}

static void openScene(const std::string &id) {
    openedFromPanel();
    for (auto &e : app.scenes) if (e->sceneId == id) { e->focusMe = true; return; }
    if (!findById(app.proj.doc["scenes"], id)) return;
    auto e = std::make_unique<SceneEditor>();
    e->sceneId = id;
    e->onOpenObject = [](const std::string &oid) { openObject(oid); };
    e->onPlayScene = [](const std::string &sid) { playProject(sid); };
    e->onNewObject = []() { app.showNewObject = true; };
    e->onOpenTileset = [](const std::string &tid) { openTileset(tid); };
    e->onOpenLayers = []() { openDoc(app.layersDoc); };
    e->onSetupObject = [](ObjectEditor &oe) { setupObjectEditor(oe); };
    e->onNewTileObject = [id]() {
        std::string oid = createObject(app.proj, "tile");
        if (json *o = findById(app.proj.doc["objects"], oid)) (*o)["scene"] = id;
        return oid;
    };
    app.scenes.push_back(std::move(e));
}

// escena con la que se esta trabajando: la de la ultima hoja de eventos o escena usada, o la primera
static std::string currentScene() {
    const json &scs = app.proj.doc["scenes"];
    auto exists = [&](const std::string &id) { for (auto &sc : scs) if (jstr(sc, "id") == id) return true; return false; };
    std::string id = ui::lastFocusKind() == "events" ? ui::lastFocusKey("events") : ui::lastFocusKey("scene");
    if (id.empty() || !exists(id)) id = ui::lastFocusKey("scene");
    if ((id.empty() || !exists(id)) && !scs.empty()) id = jstr(scs[0], "id");
    return id;
}

// hoja de eventos de una escena ("" = eventos comunes)
static EventsEditor &openEventsFor(const std::string &sceneId, bool focus = true) {
    openedFromPanel();
    for (auto &e : app.sheets) if (e->sceneId == sceneId) { e->open = true; if (focus) e->focusMe = true; return *e; }
    auto e = std::make_unique<EventsEditor>();
    e->sceneId = sceneId;
    e->focusMe = focus;
    app.sheets.push_back(std::move(e));
    return *app.sheets.back();
}
static void openEvents(bool focus = true) { openEventsFor(currentScene(), focus); }

static void openFont(const std::string &id) {
    openedFromPanel();
    for (auto &e : app.fonts) if (e->fontId == id) { e->focusMe = true; return; }
    if (!app.proj.doc.contains("fonts") || !findById(app.proj.doc["fonts"], id)) return;
    auto e = std::make_unique<FontEditor>();
    e->fontId = id;
    app.fonts.push_back(std::move(e));
}

static void openTileset(const std::string &id) {
    openedFromPanel();
    for (auto &e : app.tilesets) if (e->tilesetId == id) { e->focusMe = true; return; }
    if (!findById(app.proj.doc["tilesets"], id)) return;
    auto e = std::make_unique<TilesetEditor>();
    e->tilesetId = id;
    app.tilesets.push_back(std::move(e));
}
template <class T> static void openDoc(std::unique_ptr<T> &d) { openedFromPanel(); if (!d) d = std::make_unique<T>(); d->open = true; d->focusMe = true; }

static void importImageDialog(bool tiles) {
    app.fileDlg.show(ui::FileDialog::Open, "Importar imagen", ".png|.jpg|.jpeg|.gif|.bmp|.tga|.qoi", ui::homeDir(), "", [tiles](const std::string &f) {
        app.importer.asTiles = tiles;
        if (!app.importer.load(f)) notify(app.importer.error, true);
    });
}

static std::string newScene() {
    json &scs = app.proj.doc["scenes"];
    std::string id = makeUid("sc");
    int w = (int)jnum(app.proj.doc["settings"], "width", 960), h = (int)jnum(app.proj.doc["settings"], "height", 540);
    scs.push_back({{"id", id}, {"name", "Escena " + std::to_string(scs.size() + 1)}, {"width", w}, {"height", h}, {"background", "#202329"},
                   {"instances", json::array()}, {"tilemaps", json::array()}, {"effects", json::array()}});
    app.proj.dirty = true;
    return id;
}

static int objectUses(const std::string &id) {
    int n = 0;
    for (auto &sc : app.proj.doc["scenes"]) if (sc.contains("instances")) for (auto &it : sc["instances"]) if (jstr(it, "objectId") == id) n++;
    std::string ev = app.proj.doc.contains("events") ? app.proj.doc["events"].dump() : "";
    for (auto &sc : app.proj.doc["scenes"]) if (sc.contains("events")) ev += sc["events"].dump();
    for (size_t p = ev.find("\"" + id + "\""); p != std::string::npos; p = ev.find("\"" + id + "\"", p + 1)) n++;
    return n;
}

static std::string newSprite(const std::string &name, int w, int h) {
    w = std::clamp(w, 1, 512); h = std::clamp(h, 1, 512);
    json px = json::array();
    for (int i = 0; i < w * h; i++) px.push_back("");
    std::string id = makeUid("s");
    app.proj.sprites().push_back({{"id", id}, {"name", name}, {"width", w}, {"height", h}, {"frames", json::array({{{"pixels", px}}})}});
    app.proj.dirty = true;
    return id;
}

static int spriteUses(const std::string &id) {
    int n = 0;
    for (auto &o : app.proj.doc["objects"]) {
        if (o.value("spriteId", "") == id) n++;
        if (o.contains("animations")) for (auto &a : o["animations"]) if (a.value("spriteId", "") == id) n++;
    }
    return n;
}

static ui::PixTexture &thumbFor(json &sp) {
    std::string id = sp.value("id", "");
    auto &tex = app.thumbs[id];
    size_t h = std::hash<std::string>{}(sp.contains("frames") && !sp["frames"].empty() ? sp["frames"][0].dump() : std::string()) ^ (size_t)sp.value("width", 0) * 31;
    if (!tex.tex.id || app.thumbHash[id] != h) {
        int w = std::max(1, sp.value("width", 16)), hh = std::max(1, sp.value("height", 16));
        std::vector<Px> px(w * hh);
        if (sp.contains("frames") && !sp["frames"].empty() && sp["frames"][0].contains("pixels")) {
            auto &arr = sp["frames"][0]["pixels"];
            for (int i = 0; i < w * hh && i < (int)arr.size(); i++) if (arr[i].is_string()) px[i] = parseColor(arr[i].get<std::string>());
        }
        tex.update(px, w, hh);
        app.thumbHash[id] = h;
    }
    return tex;
}

/* ---------------- paneles ---------------- */
/* gestor del proyecto: un cajon que se abre por encima del editor (no le quita sitio) y se cierra al hacer clic fuera
   o al abrir algo desde el */
static int g_drawerOpenedAt = -1;
static bool g_drawerWasOpen = false;
static void closedProjectPanel() { g_drawerWasOpen = false; }
// trabajar juntos: un punto del color de cada persona que tiene abierto ese documento ("sprite:<id>", "scene:<id>",
// "events:<id de escena>"), alineados a la derecha que termina en 'right'; con el raton encima, sus nombres
static void peerDots(const std::string &key, ImVec2 right, float r = 4.5f) {
    std::vector<const collab::Peer *> here;
    for (auto &p : collab::peers()) if (p.doc == key) here.push_back(&p);
    if (here.empty()) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    std::string names;
    float x = right.x - r;
    for (auto *p : here) {
        dl->AddCircleFilled(ImVec2(x, right.y), r, p->color);
        dl->AddCircle(ImVec2(x, right.y), r, IM_COL32(20, 20, 26, 255), 0, 1.0f);
        x -= r * 2 + 2;
        names += (names.empty() ? "" : ", ") + p->name;
    }
    ImVec2 m = ImGui::GetIO().MousePos;
    if (m.x >= x && m.x <= right.x && std::fabs(m.y - right.y) <= r + 2) ImGui::SetTooltip("Aquí está: %s", names.c_str());
}

static void drawProjectPanel() {
    int &openedAt = g_drawerOpenedAt;
    bool &wasOpen = g_drawerWasOpen;
    if (!wasOpen) openedAt = ImGui::GetFrameCount();
    wasOpen = true;
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float h = vp->WorkSize.y;
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, h), ImVec2(vp->WorkSize.x * 0.6f, h));
    if (ImGui::GetFrameCount() == openedAt) ImGui::SetNextWindowFocus();
    ImGui::Begin("Proyecto###drawer", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking |
                                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    app.inPanel = true;
    school::note("panel");
    school::anchorRect("panel", ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + std::min(ImGui::GetWindowSize().y, 420.0f)));
    char nb[96];
    snprintf(nb, sizeof nb, "%s", app.proj.name().c_str());
    ImGui::SetNextItemWidth(-36);
    if (ImGui::InputText("##pname", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && nb[0]) { app.proj.doc["name"] = nb; app.proj.dirty = true; }
    ImGui::SameLine();
    if (ui::iconButton("closeproj", ui::Icon::Close, false, "Cerrar el proyecto (Ctrl+W)", 28)) { app.closePanel = true; closeProject(); }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Sprites", ImGuiTreeNodeFlags_DefaultOpen)) {
        float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ui::primaryButton("+ Nuevo sprite", ImVec2(hw, 0))) { app.showNewSprite = true; snprintf(app.nsName, sizeof app.nsName, "Sprite %d", (int)app.proj.sprites().size() + 1); }
        ImGui::SameLine();
        if (ImGui::Button("Importar imagen", ImVec2(hw, 0))) importImageDialog(false);
        auto &arr = app.proj.sprites();
        for (size_t i = 0; i < arr.size(); i++) {
            json &sp = arr[i];
            std::string id = sp.value("id", ""), nm = sp.value("name", "Sprite");
            ImGui::PushID(id.c_str());
            ui::PixTexture &tx = thumbFor(sp);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool isOpen = std::any_of(app.sprites.begin(), app.sprites.end(), [&](auto &e) { return e->spriteId == id; });
            if (ImGui::Selectable("##row", isOpen, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 36)) && ImGui::IsMouseDoubleClicked(0)) openSprite(id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para editar");
            peerDots("sprite:" + id, ImVec2(ImGui::GetItemRectMax().x - 6, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f));
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Editar")) openSprite(id);
                if (ImGui::MenuItem("Duplicar")) {
                    json copy = sp; std::string nid = makeUid("s");
                    copy["id"] = nid; copy["name"] = nm + " copia";
                    arr.insert(arr.begin() + i + 1, copy); app.proj.dirty = true;
                }
                if (ImGui::MenuItem("Eliminar")) app.pendingDelete = id;
                ImGui::EndPopup();
            }
            ImDrawList *dl = ImGui::GetWindowDrawList();
            float s = 30, sc = s / std::max(tx.w, tx.h);
            ImVec2 a(p.x + 3 + (s - tx.w * sc) / 2, p.y + 3 + (s - tx.h * sc) / 2), b(a.x + tx.w * sc, a.y + tx.h * sc);
            dl->AddRectFilled(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + 3 + s, p.y + 3 + s), IM_COL32(22, 22, 28, 255), 4);
            dl->AddImage(tx.id(), a, b);
            dl->AddText(ImVec2(p.x + 42, p.y + 3), IM_COL32(235, 235, 242, 255), nm.c_str());
            char meta[48]; snprintf(meta, sizeof meta, "%dx%d  %d frame(s)", sp.value("width", 0), sp.value("height", 0), sp.contains("frames") ? (int)sp["frames"].size() : 0);
            dl->AddText(ImVec2(p.x + 42, p.y + 19), ui::kDim, meta);
            ImGui::PopID();
        }
        if (arr.empty()) ImGui::TextDisabled("Sin sprites todavía.");
    }
    if (ImGui::CollapsingHeader("Escenas", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ui::primaryButton("+ Nueva escena", ImVec2(-1, 0))) openScene(newScene());
        json &scs = app.proj.doc["scenes"];
        for (size_t i = 0; i < scs.size(); i++) {
            std::string id = jstr(scs[i], "id"), nm = jstr(scs[i], "name", "Escena");
            ImGui::PushID(id.c_str());
            bool isOpen = std::any_of(app.scenes.begin(), app.scenes.end(), [&](auto &e) { return e->sceneId == id; });
            // con su vista previa (la primera pantalla, como la ve el juego)
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##scn", isOpen, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 58)) && ImGui::IsMouseDoubleClicked(0)) openScene(id);
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                float tw = 96, th = 54;
                ImVec2 a(p.x + 3, p.y + 2), b(a.x + tw, a.y + th);
                dl->AddRectFilled(a, b, IM_COL32(22, 22, 28, 255), 4);
                if (ui::PixTexture *tx = docThumb("scene:" + id, app.proj.doc, app.proj.rev, true, (int)i)) dl->AddImageRounded(tx->id(), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4);
                dl->AddRect(a, b, IM_COL32(255, 255, 255, 30), 4);
                dl->AddText(ImVec2(b.x + 10, p.y + 8), IM_COL32(235, 235, 242, 255), nm.c_str());
                char meta[64]; snprintf(meta, sizeof meta, "%gx%g%s", jnum(scs[i], "width", 0), jnum(scs[i], "height", 0), i == 0 ? "  ·  inicial" : "");
                dl->AddText(ImVec2(b.x + 10, p.y + 28), ui::kDim, meta);
            }
            peerDots("scene:" + id, ImVec2(ImGui::GetItemRectMax().x - 6, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para editar\n%gx%g", jnum(scs[i], "width", 0), jnum(scs[i], "height", 0));
            if (ImGui::BeginPopupContextItem("##sctx")) {
                if (ImGui::MenuItem("Editar")) openScene(id);
                if (ImGui::MenuItem("Escena inicial", nullptr, i == 0, i != 0)) { json s0 = scs[i]; scs.erase(scs.begin() + i); scs.insert(scs.begin(), s0); app.proj.dirty = true; }
                if (ImGui::MenuItem("Duplicar")) {
                    json c = scs[i]; c["id"] = makeUid("sc"); c["name"] = nm + " copia";
                    if (c.contains("instances")) for (auto &it : c["instances"]) it["id"] = makeUid("i");
                    // sus objetos se copian con id nuevo y la copia (instancias y eventos) usa esos ids
                    std::map<std::string, std::string> ren;
                    json &objs = app.proj.doc["objects"];
                    for (size_t k = 0, n = objs.size(); k < n; k++) if (jstr(objs[k], "scene") == id) {
                        json no = objs[k]; std::string nid = makeUid("o");
                        ren[jstr(no, "id")] = nid; no["id"] = nid; no["scene"] = c["id"];
                        objs.push_back(no);
                    }
                    std::string dump = c.dump();
                    for (auto &kv : ren) for (size_t p = dump.find("\"" + kv.first + "\""); p != std::string::npos; p = dump.find("\"" + kv.first + "\"", p + 1))
                        dump.replace(p + 1, kv.first.size(), kv.second);
                    c = json::parse(dump);
                    scs.insert(scs.begin() + i + 1, c); app.proj.dirty = true; app.proj.rev++;
                }
                if (ImGui::MenuItem("Eliminar", nullptr, false, scs.size() > 1)) app.pendingDeleteScene = id;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Objetos", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string cs = currentScene();
        ImGui::TextDisabled("Escena: %s (y los globales)", sceneName(app.proj, cs).c_str());
        if (ui::primaryButton("+ Nuevo objeto", ImVec2(-1, 0))) app.showNewObject = true;
        json &objs = app.proj.doc["objects"];
        for (size_t i = 0; i < objs.size(); i++) {
            json &o = objs[i];
            if (!objectInScene(o, cs)) continue;
            std::string id = jstr(o, "id"), nm = jstr(o, "name"), type = jstr(o, "type", "sprite");
            ImGui::PushID(id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool isOpen = std::any_of(app.objects.begin(), app.objects.end(), [&](auto &e) { return e->objectId == id; });
            if (ImGui::Selectable("##o", isOpen, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 34)) && ImGui::IsMouseDoubleClicked(0)) openObject(id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para editar");
            if (ImGui::BeginPopupContextItem("##octx")) {
                if (ImGui::MenuItem("Editar")) openObject(id);
                if (ImGui::MenuItem("Duplicar")) {
                    json c = o; c["id"] = makeUid("o"); c["name"] = nm + "2";
                    objs.insert(objs.begin() + i + 1, c); app.proj.dirty = true;
                }
                if (ImGui::MenuItem("Eliminar")) app.pendingDeleteObj = id;
                ImGui::EndPopup();
            }
            objectThumbAt(ImGui::GetWindowDrawList(), app.proj, o, ImVec2(p.x + 3, p.y + 2), 30);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 42, p.y + 1), IM_COL32(235, 235, 242, 255), nm.c_str());
            auto bh = objBehaviorTypes(o);
            std::string meta = objTypeLabel(type);
            if (!bh.empty()) meta += "  ·  " + std::to_string(bh.size()) + " comportamiento(s)";
            dl->AddText(ImVec2(p.x + 42, p.y + 17), ui::kDim, meta.c_str());
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Eventos", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string cs = currentScene();
        int n = (int)sceneEvents(app.proj, cs).size(), nc = (int)sceneEvents(app.proj, "").size();
        if (ui::primaryButton(("Eventos de " + sceneName(app.proj, cs)).c_str(), ImVec2(-1, 0))) openEventsFor(cs);
        ImGui::TextDisabled("%d evento(s): solo se ejecutan en esta escena", n);
        if (ImGui::Button("Eventos comunes", ImVec2(-1, 0))) openEventsFor("");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Se ejecutan en todas las escenas; buen sitio para las funciones que usan varias escenas");
        ImGui::TextDisabled("%d evento(s) en todas las escenas", nc);
    }
    if (ImGui::CollapsingHeader("Autotiles")) {
        // los mapas de tiles ahora son objetos Tile (se pintan en la escena); aqui se crean y prueban los autotiles
        if (ui::primaryButton("+ Nuevo autotile", ImVec2(-1, 0))) { openDoc(app.autotiles); app.autotiles->cur = createAutotile(app.proj); app.autotiles->test.clear(); }
        json &ats = projectAutotiles(app.proj);
        for (size_t i = 0; i < ats.size(); i++) {
            std::string id = jstr(ats[i], "id"), nm = jstr(ats[i], "name");
            ImGui::PushID(id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##a", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 34)) && ImGui::IsMouseDoubleClicked(0)) {
                openDoc(app.autotiles); app.autotiles->cur = id; app.autotiles->test.clear();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para editar y probar");
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(p.x + 3, p.y + 2), ImVec2(p.x + 33, p.y + 32), IM_COL32(22, 22, 28, 255), 4);
            int t0 = ats[i].contains("tiles") && ats[i]["tiles"].is_array() && ats[i]["tiles"].size() > 15 && ats[i]["tiles"][15].is_number() ? ats[i]["tiles"][15].get<int>() : -1;
            if (ui::PixTexture *tx = t0 >= 0 ? tileTex(app.proj, jstr(ats[i], "tilesetId"), t0) : nullptr) dl->AddImage(tx->id(), ImVec2(p.x + 5, p.y + 4), ImVec2(p.x + 31, p.y + 30));
            dl->AddText(ImVec2(p.x + 42, p.y + 1), IM_COL32(235, 235, 242, 255), nm.c_str());
            json *ts = findById(app.proj.doc["tilesets"], jstr(ats[i], "tilesetId"));
            dl->AddText(ImVec2(p.x + 42, p.y + 17), ui::kDim, ts ? jstr(*ts, "name").c_str() : "sin mapa de tiles");
            ImGui::PopID();
        }
        if (ats.empty()) ImGui::TextDisabled("Pinta bordes y esquinas solos. Úsalo en un objeto Tile.");
    }
    if (ImGui::CollapsingHeader("Fuentes")) {
        float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ui::primaryButton("+ De píxeles", ImVec2(hw, 0))) {
            int n = app.proj.doc.contains("fonts") ? (int)app.proj.doc["fonts"].size() + 1 : 1;
            openFont(createPixelFont(app.proj, "Fuente " + std::to_string(n), 8, 12, true));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dibuja tu propia fuente letra a letra (empieza con las letras de la fuente del juego)");
        ImGui::SameLine();
        if (ImGui::Button("Importar...##font", ImVec2(hw, 0)))
            app.fileDlg.show(ui::FileDialog::Open, "Importar fuente", ".ttf|.otf", ui::homeDir(), "", [](const std::string &f) {
                std::string err, id = importFont(app.proj, f, err);
                if (id.empty()) notify(err, true); else { notify("Fuente importada"); openFont(id); }
            });
        if (!app.proj.doc.contains("fonts") || !app.proj.doc["fonts"].is_array()) app.proj.doc["fonts"] = json::array();
        json &fts = app.proj.doc["fonts"];
        for (size_t i = 0; i < fts.size(); i++) {
            std::string id = jstr(fts[i], "id"), nm = jstr(fts[i], "name");
            bool pixel = jstr(fts[i], "kind") == "pixel";
            ImGui::PushID(id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##f", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 34)) && ImGui::IsMouseDoubleClicked(0)) openFont(id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para editar");
            if (ImGui::BeginPopupContextItem("##fctx")) {
                if (ImGui::MenuItem("Editar")) openFont(id);
                if (ImGui::MenuItem("Duplicar")) { json c = fts[i]; c["id"] = makeUid("f"); c["name"] = nm + " copia"; fts.insert(fts.begin() + i + 1, c); app.proj.dirty = true; }
                if (ImGui::MenuItem("Eliminar")) app.pendingDeleteFont = id;
                ImGui::EndPopup();
            }
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(p.x + 3, p.y + 2), ImVec2(p.x + 33, p.y + 32), IM_COL32(22, 22, 28, 255), 4);
            float sc = 1;
            if (ui::PixTexture *tx = textTexture(app.proj, id, "Aa", 18, "#f0e6d2", false, &sc)) {
                float w = tx->w * sc, h = tx->h * sc;
                dl->AddImage(tx->id(), ImVec2(p.x + 18 - w / 2, p.y + 17 - h / 2), ImVec2(p.x + 18 + w / 2, p.y + 17 + h / 2));
            }
            dl->AddText(ImVec2(p.x + 42, p.y + 1), IM_COL32(235, 235, 242, 255), nm.c_str());
            std::string meta = pixel ? "De píxeles, " + std::to_string((int)jnum(fts[i], "cellW", 8)) + "x" + std::to_string((int)jnum(fts[i], "cellH", 12)) : "TTF/OTF importada";
            dl->AddText(ImVec2(p.x + 42, p.y + 17), ui::kDim, meta.c_str());
            ImGui::PopID();
        }
        if (fts.empty()) ImGui::TextDisabled("Sin fuentes: los textos usan la del juego.");
    }
    bool musOpen = ImGui::CollapsingHeader("Música y sonidos");
    school::anchor("panel.musica");
    if (musOpen) {
        float hw3 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ui::primaryButton("+ Crear música", ImVec2(hw3, 0))) openSound(newProjectSound(false));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Con Source Sound: piano, batería, sintetizadores...");
        ImGui::SameLine();
        if (ui::primaryButton("+ Crear efecto", ImVec2(hw3, 0))) openSound(newProjectSound(true));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Con Source Sound: saltos, monedas, disparos, explosiones...");
        bool songHit = ImGui::Button("+ Canción Test", ImVec2(-1, 0));
        school::anchor("panel.cancion");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Una canción de ejemplo de 32 segundos, para ver cómo está hecha y cambiarla");
        if (songHit) addFloppaSong();
        if (ImGui::Button("Importar audio...", ImVec2(-1, 0)))
            app.fileDlg.show(ui::FileDialog::Open, "Importar audio", ".ogg|.mp3|.wav|.flac|.qoa", ui::homeDir(), "", [](const std::string &f) {
                std::string err;
                if (importMusic(app.proj, f, err).empty()) notify(err, true); else notify("Audio importado");
            });
        if (!app.proj.doc.contains("music")) app.proj.doc["music"] = json::array();
        json &mus = app.proj.doc["music"];
        for (size_t i = 0; i < mus.size(); i++) {
            std::string id = jstr(mus[i], "id");
            ImGui::PushID(id.c_str());
            bool playing = musicPlaying() == id;
            if (ui::iconButton("play", playing ? ui::Icon::Stop : ui::Icon::Play, playing, playing ? "Detener" : "Escuchar", 24)) musicPreview(app.proj, playing ? "" : id);
            ImGui::SameLine();
            bool own = mus[i].contains("song");
            ImGui::SetNextItemWidth(own ? -112 : -60);
            if (editText("##mn", mus[i], "name")) app.proj.dirty = true;
            if (own) { ImGui::SameLine(); if (ImGui::SmallButton("Editar")) openSound(id); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Quitar")) app.pendingDeleteMusic = id;
            ImGui::PopID();
        }
        if (mus.empty()) ImGui::TextDisabled("Sin audio. Úsalo con la acción \"Reproducir audio\".");
    }
    if (ImGui::CollapsingHeader("Archivos iniciales")) {
        // archivos que viajan dentro del juego (por ejemplo, niveles en .json); los eventos los leen con
        // "Lista de archivos iniciales" y "Leer un archivo inicial"
        if (ui::primaryButton("Añadir archivo...", ImVec2(-1, 0)))
            app.fileDlg.show(ui::FileDialog::Open, "Añadir archivo inicial", ".json|.txt|.csv|.png|.jpg|.mp3|.ogg|.wav", ui::homeDir(), "", [](const std::string &f) {
                std::string err;
                if (!addInitialFile(app.proj, f, err)) notify(err, true); else notify("Archivo añadido a los archivos iniciales");
            });
        if (!app.proj.doc.contains("files") || !app.proj.doc["files"].is_array()) app.proj.doc["files"] = json::array();
        json &fl = app.proj.doc["files"];
        int quitar = -1, subir = -1;
        for (size_t i = 0; i < fl.size(); i++) {
            ImGui::PushID((int)i);
            size_t bytes = fl[i].contains("text") ? jstr(fl[i], "text").size() : jstr(fl[i], "data").size() * 3 / 4;
            ImGui::TextUnformatted(jstr(fl[i], "name").c_str());
            ImGui::SameLine(); ImGui::TextDisabled("%.1f KB", bytes / 1024.0);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 90);
            if (i > 0 && ImGui::SmallButton("Subir")) subir = (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Quitar")) quitar = (int)i;
            ImGui::PopID();
        }
        if (subir > 0) { std::swap(fl[subir], fl[subir - 1]); app.proj.dirty = true; }
        if (quitar >= 0) { fl.erase(fl.begin() + quitar); app.proj.dirty = true; }
        if (fl.empty()) ImGui::TextDisabled("Sin archivos. Los que añadas (niveles .json, imágenes...) van dentro del juego, en este orden.");
    }
    if (ImGui::CollapsingHeader("Juego", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool ajHit = ImGui::Button("Ajustes y variables globales", ImVec2(-1, 0));
        school::anchor("panel.ajustes");
        if (ajHit) { openDoc(app.settings); school::note("settings"); }
        if (ImGui::Button("Shaders", ImVec2(-1, 0))) openDoc(app.shaders);
        if (ImGui::Button("Capas", ImVec2(-1, 0))) openDoc(app.layersDoc);
        if (ImGui::Button("Extensiones y plugins", ImVec2(-1, 0))) openDoc(app.extensions);
    }
    ImGui::End();
    app.inPanel = false;
    // clic fuera (sin arrastrar nada desde el panel ni con un menu o ventana emergente abiertos): se cierra
    bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    bool onButton = ImGui::IsMouseHoveringRect(app.treeBtnMin, app.treeBtnMax, false);
    if (ImGui::GetFrameCount() != openedAt && !hovered && !onButton && !popup && !ImGui::GetDragDropPayload() &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        app.closePanel = true;
    if (app.closePanel) { app.showTree = false; app.closePanel = false; wasOpen = false; }
}

/* ---------------- pagina de inicio (como la HomePage de GDevelop) ---------------- */
// Menu lateral: "Crear" (tus proyectos, que viven en la carpeta de proyectos) y "Prueba" (juegos completos de ejemplo).
// La misma pagina se ve antes de abrir un proyecto y en la pestana Inicio.

/* ---------------- ajustes del editor: carpeta de proyectos y guardado automatico ---------------- */
struct EditorPrefs {
    std::string projectsDir; bool autosave = true; std::string name; int color = 0;
    int autosaveSecs = 30, keepVersions = 20;     // cada cuanto se guarda solo y cuantas versiones se guardan
    bool animations = true;                       // animaciones suaves del editor
    bool safeSave = false;                        // experimental: recuperacion si se apaga el equipo
    std::string lang;                             // idioma del editor ("" = Espanol)
    bool news = true;                             // buscar noticias y actualizaciones (Inicio > Noticias)
};
static EditorPrefs &prefs() {
    static EditorPrefs p;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(ui::configDir() + "/ajustes.json");
        json j = json::parse(in, nullptr, false);
        if (j.is_object()) {
            p.projectsDir = jstr(j, "carpetaProyectos"); p.autosave = jbool(j, "guardadoAutomatico", true);
            p.name = jstr(j, "nombre"); p.color = (int)jnum(j, "color", 0);
            p.autosaveSecs = std::clamp((int)jnum(j, "segundosGuardado", 30), 5, 3600); p.keepVersions = std::clamp((int)jnum(j, "versiones", 20), 0, 200);
            p.animations = jbool(j, "animaciones", true); p.safeSave = jbool(j, "guardadoSeguro", false); p.lang = jstr(j, "idioma");
            p.news = jbool(j, "buscarNoticias", true);
        }
        if (p.name.empty()) p.name = collab::defaultName();
        if (p.projectsDir.empty()) p.projectsDir = ui::documentsDir() + "/Source 2D";
        // las pruebas automaticas usan una carpeta temporal y no tocan los Documentos del usuario
        if (!app.script.empty()) p.projectsDir = (fs::temp_directory_path() / ("source2d-pruebas-" + std::to_string(sys::processId()))).string();
        if (!app.script.empty() && getenv("S2D_PRUEBA_PROYECTOS")) p.projectsDir = getenv("S2D_PRUEBA_PROYECTOS");   // carpeta elegida por la prueba
    }
    return p;
}
static void savePrefs() {
    if (!app.script.empty()) return;
    std::ofstream(ui::configDir() + "/ajustes.json") << json{{"carpetaProyectos", prefs().projectsDir}, {"guardadoAutomatico", prefs().autosave},
                                                             {"nombre", prefs().name}, {"color", prefs().color},
                                                             {"segundosGuardado", prefs().autosaveSecs}, {"versiones", prefs().keepVersions},
                                                             {"animaciones", prefs().animations}, {"guardadoSeguro", prefs().safeSave},
                                                             {"idioma", prefs().lang}, {"buscarNoticias", prefs().news}}.dump(1);
}
// la carpeta se crea sola la primera vez que hace falta
static std::string projectsDir() {
    std::error_code ec;
    fs::create_directories(prefs().projectsDir, ec);
    return prefs().projectsDir;
}
static std::string safeFileName(std::string n) {
    for (char &c : n) if (strchr("/\\:*?\"<>|", c)) c = '_';
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    return n.empty() ? "Mi juego" : n;
}
// cada proyecto en su carpeta: <carpeta de proyectos>/<nombre>/<nombre>.source2d.json (sin pisar uno que ya exista)
static std::string newProjectPath(const std::string &name) {
    std::string base = safeFileName(name), dir = projectsDir() + "/" + base;
    for (int k = 2; fs::exists(dir); k++) dir = projectsDir() + "/" + base + " " + std::to_string(k);
    return dir + "/" + fs::path(dir).filename().string() + ".source2d.json";
}
// proyectos de la carpeta (y los recientes de otros sitios), del mas nuevo al mas viejo; se vuelve a mirar cada 2 s
static bool g_projListStale = true;
static const std::vector<std::string> &projectList() {
    static std::vector<std::string> list;
    static double at = -10;
    if (!g_projListStale && GetTime() - at < 2) return list;
    g_projListStale = false;
    at = GetTime();
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    std::error_code ec;
    auto add = [&](const fs::path &f) {
        std::string abs = fs::absolute(f).string();
        for (auto &x : found) if (x.second == abs) return;
        found.push_back({fs::last_write_time(f, ec), abs});
    };
    for (auto it = fs::recursive_directory_iterator(projectsDir(), ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(ec) && it->path().string().size() > 15 && it->path().string().rfind(".source2d.json") == it->path().string().size() - 14) add(it->path());
    }
    for (auto &r : app.recent) if (fs::exists(r, ec)) add(r);
    std::sort(found.begin(), found.end(), [](auto &x, auto &y) { return x.first > y.first; });
    list.clear();
    for (auto &x : found) list.push_back(x.second);
    return list;
}
static void refreshProjectList() { g_projListStale = true; }

static void focusScene();
extern bool rlImGuiScriptMouse, rlImGuiScriptDown;   // rlImGui.cpp: raton manejado por el guion de pruebas
extern float rlImGuiScriptX, rlImGuiScriptY;

/* Juegos de prueba: proyectos completos para jugarlos y ver por dentro como estan hechos. Para anadir uno:
   genera su .source2d.json, anadelo a TEMPLATES en el Makefile (y a Makefile.win) y anadelo aqui. */
struct ExampleGame { const char *name, *desc, *level; std::vector<const char *> tags; json doc; bool tpl = false; };

// carpeta del editor: la real aunque el editor comprimido se abra desde memoria o desde una copia temporal (el lanzador
// de src/stub.c la deja en S2D_EXE_DIR)
// ---------------- versiones (como el lanzador de Minecraft) ----------------
// Cada version del editor que se abre se copia en <config>/versiones/<version>/. En la pagina de inicio se elige cual
// abrir; los proyectos guardan con que version se hicieron y avisan si se abren en una mas vieja.
static std::string versionsDir() { return ui::configDir() + "/versiones"; }
static std::string selfExe() { const char *e = getenv("S2D_EXE"); return e && *e ? e : ""; }
static void installThisVersion() {
    std::string me = selfExe();
    if (me.empty()) return;                                   // sin comprimir (compilado a mano): no se instala
    std::error_code ec;
    fs::path dst = fs::path(versionsDir()) / S2D_VERSION / fs::path(me).filename();
    if (fs::exists(dst, ec) && fs::file_size(dst, ec) == fs::file_size(me, ec)) return;
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(me, dst, fs::copy_options::overwrite_existing, ec);
    sys::setExecutable(dst.string());
}
struct InstalledVersion { std::string name, exe; };
static std::vector<InstalledVersion> installedVersions() {
    std::vector<InstalledVersion> out;
    std::error_code ec;
    for (auto &d : fs::directory_iterator(versionsDir(), ec)) {
        if (!d.is_directory(ec)) continue;
        for (auto &f : fs::directory_iterator(d.path(), ec)) {
            std::string fn = f.path().filename().string();
            if (fn.rfind("Source2D", 0) == 0 && fs::path(fn).extension() != ".parcial") { out.push_back({d.path().filename().string(), f.path().string()}); break; }
        }
    }
    bool here = false;
    for (auto &v : out) if (v.name == S2D_VERSION) here = true;
    if (!here) out.push_back({S2D_VERSION, selfExe()});
    std::sort(out.begin(), out.end(), [](const InstalledVersion &a, const InstalledVersion &b) { return a.name > b.name; });
    return out;
}

// abre otra version (o esta misma en otro modo) y cierra esta, preguntando si hay cambios sin guardar
static void launchVersion(const std::string &exe, bool vk) {
    std::string e = exe.empty() ? selfExe() : exe;
    if (e.empty()) { notify("Esta copia del editor no puede abrir otra versión (no está comprimida)", true); return; }
    guardSwitch([e, vk] {
        sys::Proc pr;
        std::vector<std::string> args = {e};
        if (vk) args.push_back("--vulkan");
        if (sys::spawn(args, &pr)) app.quit = true; else notify("No se pudo abrir esa versión", true);
    });
}
static bool hasDownloadable() {
    for (auto &v : novedades::versiones()) if (!novedades::instalada(v.nombre) && (v.canal == "estable" || app.showExperimental)) return true;
    return false;
}

static std::string exeDir() {
    const char *e = getenv("S2D_EXE_DIR");
    std::string d = e && *e ? e : GetApplicationDirectory();
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

// Los juegos de prueba no van dentro del editor: solo los examples_*.source2d.json de la raiz del codigo (el editor de
// pruebas esta en build/). Ya no se lee ninguna carpeta Ejemplos. Los que no estan no salen en la pagina de inicio.
static json exampleDoc(const char *file, const char *devFile) {
    (void)file;
    std::string dir = exeDir();
    for (std::string p : {dir + "../" + devFile, dir + devFile}) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        try { return json::parse(in); } catch (...) { fprintf(stderr, "Source 2D: %s no es un proyecto valido\n", p.c_str()); }
    }
    return json::object();
}

static std::vector<ExampleGame> &exampleGames() {
    static std::vector<ExampleGame> g;
    if (g.empty()) {
        g.push_back({"Ajedrez", "",
                     "Avanzado", {"Funciones de eventos", "Bot con búsqueda alfa-beta", "Arrays", "Tweens", "Shaders"},
                     exampleDoc("Ajedrez.source2d.json", "examples_ajedrez.source2d.json")});
        g.push_back({"Maxwell el gato", "",
                     "Fácil", {"Animación por eventos", "TiempoDelta()"},
                     exampleDoc("Maxwell el gato.source2d.json", "examples_maxwell.source2d.json")});
        g.push_back({"Platform Test", "",
                     "Difícil", {"Doble salto y dash", "Mapa grande", "Parallax y capas", "Shader de viento", "Partículas"},
                     exampleDoc("Platform Test.source2d.json", "examples_platform_test.source2d.json")});
        g.push_back({"Project Ry-mik", "",
                     "Avanzado", {"Juego de ritmo", "Archivos iniciales y JSON", "Editor de niveles", "Shaders CRT y brillo", "Funciones de eventos"},
                     exampleDoc("Project Ry-mik.source2d.json", "examples_rymik.source2d.json")});
        g.push_back({"El Faro de Ámbar", "",
                     "Intermedio", {"Menos de 1 MB", "Diálogos y elecciones", "Movimiento cenital", "Jefe final", "Guardar partida"},
                     exampleDoc("El Faro de Ámbar.source2d.json", "examples_rpg.source2d.json")});
        g.push_back({"Prueba de rendimiento", "",
                     "Medir", {"CPU", "GPU", "Física", "Repetir y funciones"},
                     exampleDoc("Prueba de rendimiento.source2d.json", "examples_benchmark.source2d.json")});
        // plantillas (tools/make_templates.py): solo "Ver por dentro"; cada evento lleva su comentario
        struct Tpl { const char *name, *desc, *file, *dev; std::vector<const char *> tags; };
        static const Tpl tpls[] = {
            {"Plataformas", "", "Plantilla Plataformas.source2d.json", "examples_tpl_plataformas.source2d.json", {"Plataformero", "Colisiones", "Cámara"}},
            {"Aventura", "", "Plantilla Aventura.source2d.json", "examples_tpl_aventura.source2d.json", {"Cenital", "Seguir", "Partículas"}},
            {"Disparos", "", "Plantilla Disparos.source2d.json", "examples_tpl_disparos.source2d.json", {"Crear objetos", "Salud", "Temporizador"}},
            {"Clics y arrastrar", "", "Plantilla Clics.source2d.json", "examples_tpl_clics.source2d.json", {"Ratón", "Arrastrable", "Táctil"}}};
        for (auto &t : tpls) { ExampleGame eg{t.name, t.desc, "Plantilla", t.tags, exampleDoc(t.file, t.dev)}; eg.tpl = true; g.push_back(eg); }
    }
    return g;
}

// proyecto nuevo guardado ya en la carpeta de proyectos (asi el guardado automatico funciona desde el principio)
static void createProject(const std::string &name, int w = 960, int h = 540) {
    newProject(name);
    app.proj.doc["settings"]["width"] = w; app.proj.doc["settings"]["height"] = h;
    for (auto &sc : app.proj.doc["scenes"]) { sc["width"] = w; sc["height"] = h; }
    std::string f = newProjectPath(name);
    std::error_code ec;
    fs::create_directories(fs::path(f).parent_path(), ec);
    saveProject(f);
}

// cambiar de proyecto con cambios sin guardar pide confirmacion
static void guardSwitch(std::function<void()> f) {
    if (app.hasProject && app.proj.dirty) { app.pendingSwitch = std::move(f); app.askSwitch = true; }
    else f();
}

static std::string lastEdited(const std::string &path) {
    time_t now = time(nullptr), t = sys::fileMTime(path);
    if (!t) return "";
    tm a, b;
    sys::localTime(t, a); sys::localTime(now, b);
    char hm[16], d[32];
    strftime(hm, sizeof hm, "%H:%M", &a);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return std::string("Última edición: hoy, ") + hm;
    time_t y = now - 86400; tm c; sys::localTime(y, c);
    if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday) return std::string("Última edición: ayer, ") + hm;
    strftime(d, sizeof d, "%d/%m/%Y", &a);
    return std::string("Última edición: ") + d;
}

static void sectionTitle(const char *t, float size) {
    ImGui::PushFont(ui::boldFont(), size);
    ImGui::TextUnformatted(t);
    ImGui::PopFont();
}

// boton con texto en negrita; primary = relleno morado, si no, contorno (como los RaisedButton/FlatButton de GDevelop)
static bool homeButton(const char *label, bool primary, float h = 34) {
    ImFont *bold = ui::boldFont();
    float fs = ImGui::GetFontSize(), w = bold->CalcTextSizeA(fs, FLT_MAX, 0, label).x + 32;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(label, ImVec2(w, h));
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 e(p.x + w, p.y + h);
    if (primary) dl->AddRectFilled(p, e, act ? ui::kPrimaryDark : hov ? ui::kPrimaryLight : ui::kPrimary, 8);
    else {
        if (hov) dl->AddRectFilled(p, e, ui::kHover, 8);
        dl->AddRect(p, e, ui::kSecondary, 8.0f, 0, 1.0f);
    }
    dl->AddText(bold, fs, ImVec2(p.x + 16, p.y + (h - fs) * 0.5f), primary ? ui::kText : ui::kSecondary, label);
    return r;
}

static float homeButtonWidth(const char *label) { return ui::boldFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, label).x + 32; }

static void thumbImage(ImDrawList *dl, ui::PixTexture *tx, ImVec2 a, ImVec2 b, ImDrawFlags corners = ImDrawFlags_RoundCornersAll) {
    dl->AddRectFilled(a, b, ui::kPanel2, 8, corners);
    if (tx && tx->tex.id) dl->AddImageRounded(tx->id(), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8, corners);
    else ui::drawIcon(dl, ui::Icon::Scene, ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2), 28, ui::kText2);
}

// tarjeta de proyecto (GameDashboardCard): miniatura, ultima edicion, nombre, ruta y botones
static void projectCard(const std::string &path, bool current, const std::string &extra = "") {
    const float pad = 16, w = ImGui::GetContentRegionAvail().x;
    float tw = std::clamp(w / 4, 130.0f, 245.0f), th = tw * 9 / 16, h = th + pad * 2;
    ImVec2 p = ImGui::GetCursorScreenPos(), e(p.x + w, p.y + h);
    ImGui::PushID(current ? "##actual" : path.c_str());
    ImGui::SetNextItemAllowOverlap();   // los botones de la tarjeta (Abrir...) van encima y se quedan el clic
    ImGui::InvisibleButton("##card", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered(), dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, e, hov ? IM_COL32(0x2B, 0x2B, 0x35, 255) : ui::kPanel, 8);
    dl->AddRect(p, e, current ? ui::kPrimaryLight : ui::kLine, 8, 0, current ? 2.0f : 1.0f);
    ui::PixTexture *tx = current ? docThumb("actual", app.proj.doc, app.proj.rev) : fileThumb(path);
    thumbImage(dl, tx, ImVec2(p.x + pad, p.y + pad), ImVec2(p.x + pad + tw, p.y + pad + th));
    float x = p.x + pad + tw + pad, y = p.y + pad, fs = ImGui::GetFontSize();
    std::string when = current ? (app.proj.dirty ? "Proyecto abierto · cambios sin guardar" : "Proyecto abierto") : lastEdited(path);
    dl->AddText(ImVec2(x, y), ui::kText2, when.c_str());
    std::string name = current ? app.proj.name() : fileProjectName(path);
    dl->AddText(ui::boldFont(), 20, ImVec2(x, y + fs + 6), ui::kText, name.c_str());
    std::string where = current && app.proj.path.empty() ? "Aún no se ha guardado" : (current ? app.proj.path : path);
    ImGui::PushClipRect(ImVec2(x, p.y), ImVec2(e.x - pad, e.y), true);
    dl->AddText(ImVec2(x, y + fs + 36), ui::kDim, where.c_str());
    if (!extra.empty()) dl->AddText(ui::boldFont(), fs, ImVec2(x, y + fs + 60), IM_COL32(120, 210, 140, 255), extra.c_str());
    ImGui::PopClipRect();
    // botones abajo a la derecha
    const char *main = current ? "Seguir editando" : "Abrir";
    float bw = homeButtonWidth(main), bx = e.x - pad - bw, by = e.y - pad - 34;
    if (current) {
        const char *play = "Vista previa", *cls = "Cerrar proyecto";
        float pw = homeButtonWidth(play), cw = homeButtonWidth(cls);
        ImGui::SetCursorScreenPos(ImVec2(bx - 8 - pw, by));
        if (homeButton(play, false)) playProject();
        ImGui::SetCursorScreenPos(ImVec2(bx - 16 - pw - cw, by));
        if (homeButton(cls, false)) closeProject();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cierra el proyecto y vuelve al inicio (Ctrl+W)");
    }
    ImGui::SetCursorScreenPos(ImVec2(bx, by));
    bool open = homeButton(main, true) || dbl;
    if (!current) {   // borrar el proyecto (pide confirmacion)
        ImGui::SetCursorScreenPos(ImVec2(bx - 8 - 34, by));
        if (ui::iconButton("##del", ui::Icon::Delete, false, "Borrar el proyecto", 34)) app.pendingDeleteProject = path;
    }
    if (!current && ImGui::BeginPopupContextItem("##cardctx")) {
        if (ImGui::MenuItem("Abrir")) open = true;
        if (ImGui::MenuItem("Borrar el proyecto...")) app.pendingDeleteProject = path;
        ImGui::EndPopup();
    }
    if (open) {
        if (current) focusScene();
        else { std::string f = path; guardSwitch([f] { openProject(f); }); }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, e.y + 12));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
}

// cada 30 s, si hay cambios y el proyecto tiene archivo, se guarda solo
/* ---------------- versiones del proyecto ----------------
   Cada guardado (a mano o automatico) deja una copia con fecha en <carpeta del proyecto>/.versiones/<nombre>/; se
   guardan las ultimas N (Opciones). Archivo > Versiones del proyecto las muestra y abre la que elijas. */
static std::string versionsFolder(const std::string &projPath) {
    fs::path p = projPath;
    std::string stem = p.filename().string();
    size_t k = stem.find(".source2d");
    if (k != std::string::npos) stem = stem.substr(0, k);
    return (p.parent_path() / ".versiones" / stem).string();
}
static void saveVersion(const std::string &kind) {
    if (!app.script.empty() && !app.scriptVersions) return;
    if (prefs().keepVersions <= 0 || app.proj.path.empty()) return;
    std::error_code ec;
    std::string dir = versionsFolder(app.proj.path);
    fs::create_directories(dir, ec);
    time_t t = time(nullptr); tm lt; sys::localTime(t, lt);
    char nm[64]; strftime(nm, sizeof nm, "%Y-%m-%d %H-%M-%S", &lt);
    std::string dst = dir + "/" + nm + " " + kind + ".source2d.json";
    fs::copy_file(app.proj.path, dst, fs::copy_options::overwrite_existing, ec);
    std::vector<fs::path> all;
    for (auto &e : fs::directory_iterator(dir, ec)) if (e.path().extension() == ".json") all.push_back(e.path());
    std::sort(all.begin(), all.end());
    while ((int)all.size() > prefs().keepVersions) { fs::remove(all.front(), ec); all.erase(all.begin()); }
}
struct ProjVersion { std::string file, date, kind; uintmax_t size; };
static std::vector<ProjVersion> projectVersions() {
    std::vector<ProjVersion> out;
    if (app.proj.path.empty()) return out;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(versionsFolder(app.proj.path), ec)) {
        std::string fn = e.path().filename().string();
        if (fn.size() < 20 || e.path().extension() != ".json") continue;
        ProjVersion v;
        v.file = e.path().string();
        v.date = fn.substr(8, 2) + "/" + fn.substr(5, 2) + "/" + fn.substr(0, 4) + "  " + fn.substr(11, 2) + ":" + fn.substr(14, 2) + ":" + fn.substr(17, 2);
        v.kind = fn.find(" auto.") != std::string::npos ? "guardado automático" : fn.find(" recuperado.") != std::string::npos ? "recuperado" : "guardado a mano";
        v.size = fs::file_size(e.path(), ec);
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const ProjVersion &a, const ProjVersion &b) { return a.file > b.file; });
    return out;
}

/* ---------------- recuperacion (experimental): si se va la luz ----------------
   Con "guardado a prueba de apagones", cada pocos segundos lo que hay sin guardar se escribe (forzado al disco) en
   <config>/recuperacion/. Al cerrar bien o al guardar se borra; si al abrir el editor sigue ahi, el editor se cerro
   de golpe y ofrece recuperarlo. */
static std::string recoveryDir() { return ui::configDir() + "/recuperacion"; }
static std::string recoveryFile() { return recoveryDir() + "/" + (jstr(app.proj.doc, "id").empty() ? "sin-id" : jstr(app.proj.doc, "id")) + ".json"; }
static void clearRecovery() { std::error_code ec; if (app.hasProject) fs::remove(recoveryFile(), ec); }
static void recoveryTick() {
    if (!prefs().safeSave || !app.hasProject || !app.proj.dirty || !app.script.empty()) return;
    double now = GetTime();
    if (now - app.lastRecovery < 5) return;
    app.lastRecovery = now;
    syncEditors();
    std::error_code ec;
    fs::create_directories(recoveryDir(), ec);
    json r = {{"path", app.proj.path}, {"name", app.proj.name()}, {"time", (long long)time(nullptr)}, {"doc", app.proj.doc}};
    sys::writeDurable(recoveryFile(), r.dump());
}

static void autosaveTick() {
    recoveryTick();
    double now = GetTime();
    if (now - app.lastAutosave < prefs().autosaveSecs) return;
    app.lastAutosave = now;
    if (!prefs().autosave || !app.hasProject || !app.proj.dirty || app.proj.path.empty() || app.fileDlg.open) return;
    syncEditors();
    std::string err;
    if (app.proj.save(app.proj.path, err)) { saveVersion("auto"); clearRecovery(); notify("Guardado automático"); }
    else notify("No se pudo guardar: " + err, true);
}

static void openNewProjectDialog() {
    snprintf(app.newName, sizeof app.newName, "%s", "Mi juego");
    app.askNew = true;
}

static void drawCreateSection() {
    sectionTitle("Crear", 30);
    // carpeta de proyectos y guardado automatico
    if (ImGui::SmallButton("Carpeta de proyectos"))
        app.fileDlg.show(ui::FileDialog::Folder, "Carpeta de los proyectos", "", projectsDir(), "", [](const std::string &d) {
            prefs().projectsDir = d; savePrefs(); refreshProjectList(); notify("Los proyectos nuevos se guardarán en " + d);
        });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", prefs().projectsDir.c_str());
    ImGui::SameLine(0, 10);
    if (ImGui::SmallButton("Opciones")) app.showOptions = true;
    ImGui::Dummy(ImVec2(0, 10));

    // cabecera de "Mis proyectos" con los botones a la derecha
    float avail = ImGui::GetContentRegionAvail().x, y0 = ImGui::GetCursorPosY();
    sectionTitle("Mis proyectos", 21);
    const char *imp = "Abrir un proyecto…", *cre = "+ Crear un proyecto", *spr = "Abrir Source Sprite", *jn = "Unirse a una sesión";
    float bx = ImGui::GetCursorPosX() + avail - homeButtonWidth(cre) - 8 - homeButtonWidth(imp) - 8 - homeButtonWidth(spr) - 8 - homeButtonWidth(jn);
    ImGui::SetCursorPos(ImVec2(bx, y0 - 3));
    if (homeButton(jn, false)) { app.collabTab = 1; app.showCollab = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trabajar en el proyecto de otra persona, en tiempo real");
    ImGui::SameLine(0, 8);
    if (homeButton(spr, false)) app.homeSection = 4;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo el editor de sprites: dibuja, anima y guarda en PNG, GIF, hojas de sprites...");
    ImGui::SameLine(0, 8);
    if (homeButton(imp, false)) guardSwitch([] { openDialog(); });
    ImGui::SameLine(0, 8);
    if (homeButton(cre, true)) openNewProjectDialog();
    ImGui::Dummy(ImVec2(0, 8));

    int shown = 0;
    std::string curAbs = app.hasProject && !app.proj.path.empty() ? fs::absolute(app.proj.path).string() : "";
    if (app.hasProject) { projectCard("", true); shown++; }
    for (auto &r : std::vector<std::string>(projectList())) {
        if (r == curAbs) continue;
        projectCard(r, false);
        shown++;
    }
    if (!shown) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(avail, 70));
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + avail, p.y + 70), ui::kLine, 8);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 18, p.y + 25), ui::kText2, "Todavía no tienes proyectos. Crea uno o mira los juegos del apartado Prueba.");
        ImGui::Dummy(ImVec2(0, 4));
    }
}

// "Ver por dentro": abre una copia del juego como proyecto nuevo (sin guardar) con la hoja de eventos delante
static void openExampleCopy(int i) {
    guardSwitch([i] {
        ExampleGame &g = exampleGames()[i];
        newProject(g.name);
        app.proj.doc = g.doc;
        migrateSceneTilemaps(app.proj.doc);   // los juegos de prueba traen mapas de tiles de escena
        app.proj.doc["id"] = makeUid("p");
        app.proj.doc["name"] = std::string(g.name) + " (mi versión)";
        app.proj.rev++;
        loadPaletteFrom(app.proj.doc);
        app.eventsFirst = true;
        std::string f = newProjectPath(jstr(app.proj.doc, "name"));
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        saveProject(f);
        refreshProjectList();
        notify(std::string("Copia de ") + g.name + " guardada en tus proyectos: cámbiala como quieras");
    });
}

// tarjeta de un juego de prueba: miniatura grande, etiquetas, descripcion, cifras y botones
static void exampleCard(int i, float w) {
    ExampleGame &g = exampleGames()[i];
    const float pad = 18, th = (w - pad * 2) * 9 / 16;
    float fs = ImGui::GetFontSize();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    // alto: miniatura + titulo + etiquetas + descripcion (envuelta) + cifras + botones
    float textW = w - pad * 2;
    (void)textW;
    float h = pad + th + 14 + 26 + 30 + fs + 18 + 38 + pad;
    ImVec2 e(p.x + w, p.y + h);
    dl->AddRectFilled(p, e, ui::kPanel, 10);
    dl->AddRect(p, e, ui::kLine, 10);
    thumbImage(dl, docThumb(std::string("ej:") + g.name, g.doc, 0, false), ImVec2(p.x + pad, p.y + pad), ImVec2(e.x - pad, p.y + pad + th));
    float y = p.y + pad + th + 14;
    dl->AddText(ui::boldFont(), 22, ImVec2(p.x + pad, y), ui::kText, g.name);
    // nivel a la derecha del titulo
    ImVec2 ls = ui::boldFont()->CalcTextSizeA(fs - 1, FLT_MAX, 0, g.level);
    dl->AddRectFilled(ImVec2(e.x - pad - ls.x - 16, y + 2), ImVec2(e.x - pad, y + 24), IM_COL32(176, 122, 42, 60), 11);
    dl->AddText(ui::boldFont(), fs - 1, ImVec2(e.x - pad - ls.x - 8, y + 13 - ls.y / 2), IM_COL32(230, 180, 90, 255), g.level);
    y += 34;
    float x = p.x + pad;
    for (const char *t : g.tags) {   // etiquetas
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs - 2, FLT_MAX, 0, t);
        if (x + ts.x + 16 > e.x - pad) break;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ts.x + 16, y + 22), ui::kPanel2, 11);
        dl->AddText(ImGui::GetFont(), fs - 2, ImVec2(x + 8, y + 11 - ts.y / 2), ui::kText2, t);
        x += ts.x + 22;
    }
    y += 30;
    int nev = 0, nfn = 0;
    auto count = [&](const json &list) { for (auto &ev : list) { if (ev.contains("kind")) continue; nev++; if (jstr(ev, "type") == "function") nfn++; } };
    count(g.doc["events"]);
    for (auto &sc : g.doc["scenes"]) if (sc.contains("events") && sc["events"].is_array()) count(sc["events"]);
    char stats[160];
    snprintf(stats, sizeof stats, "%d eventos · %d funciones · %d objetos · %d escenas", nev, nfn, (int)g.doc["objects"].size(), (int)g.doc["scenes"].size());
    dl->AddText(ImVec2(p.x + pad, y), ui::kDim, stats);
    y += fs + 18;
    ImGui::PushID(i);
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, y));
    if (!g.tpl) {
        if (homeButton("    Jugar", true, 38)) playDoc(g.doc);
        ui::drawIcon(dl, ui::Icon::Play, ImVec2(ImGui::GetItemRectMin().x + 20, ImGui::GetItemRectMin().y + 19), 12, ui::kText);
        // posicion explicita: con SameLine, en la tarjeta de la derecha (que ya va en la misma linea que la de la izquierda)
        // el boton subia a la altura del principio de la tarjeta
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + 10, y));
    }
    if (homeButton("Ver por dentro", g.tpl, 38)) openExampleCopy(i);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abre una copia del juego: mira sus eventos, objetos y escenas, o cámbiala y guárdala como tu propia versión");
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, e.y));
    ImGui::Dummy(ImVec2(w, 0));
}

static void drawTestsSection(bool templates) {
    sectionTitle(templates ? "Plantillas" : "Juegos", 30);
    if (templates) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Juegos pequeños para empezar: cada evento tiene un comentario que explica cómo funciona.");
    ImGui::Dummy(ImVec2(0, 6));
    float avail = ImGui::GetContentRegionAvail().x, gap = 24;
    int cols = avail > 1100 ? 2 : 1;
    float w = std::min(620.0f, (avail - gap * (cols - 1)) / cols);
    int k = 0;
    for (int i = 0; i < (int)exampleGames().size(); i++) {
        if (exampleGames()[i].doc.empty() || exampleGames()[i].tpl != templates) continue;      // no esta junto al editor
        if (k++ % cols) ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        exampleCard(i, w);
        ImGui::EndGroup();
    }
    if (!k) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 640);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Todavía no hay juegos aquí.");
        ImGui::PopTextWrapPos();
    }
}

static std::string creationsDir(const char *sub);   // <carpeta de proyectos>/Sprites o /Sonidos
static void refreshCreations();

/* ---------------- Source Sprite: el editor de sprites solo, sin proyecto ----------------
   Se abre desde la pagina de inicio. Abre imagenes (.png, .gif animado, .bmp, .jpg, .tga, .qoi) o .s2dsprite (capas y
   frames editables), guarda en .s2dsprite y exporta a imagen, hoja de sprites (con su JSON al estilo Aseprite), un
   archivo por frame o GIF animado. Si hay un proyecto abierto, el sprite se puede anadir a el. */
struct SpriteStudio {
    bool on = false, askNew = false, askExport = false, askLeave = false;
    std::unique_ptr<SpriteEditor> ed;
    Project proj;                       // proyecto de usar y tirar (el editor guarda ahi la paleta)
    std::string path;                   // .s2dsprite actual ("" si nunca se guardo)
    bool dirty = false;
    int nw = 32, nh = 32, fmt = 0, layout = 1;
    bool withJson = true;
};
static SpriteStudio g_studio;

static void studioNew(int w, int h) {
    auto &S = g_studio;
    S.ed = std::make_unique<SpriteEditor>();
    S.ed->load(json{{"id", "studio"}, {"name", "Sprite"}, {"width", std::clamp(w, 1, 2048)}, {"height", std::clamp(h, 1, 2048)}});
    S.ed->onStatus = [](const std::string &m) { notify(m); };
    S.path.clear(); S.dirty = false;
}
static void openStudio() {
    auto &S = g_studio;
    if (!S.ed) studioNew(32, 32);
    S.proj.doc = json::object();
    savePaletteTo(S.proj.doc);
    S.on = true;
    app.homeSection = 4;   // al volver, a la lista de sprites
}
static bool studioLoad(const std::string &f) {
    auto &S = g_studio;
    if (!S.ed) studioNew(32, 32);
    std::string err;
    if (!S.ed->loadImageFile(f, err)) { notify(err, true); return false; }
    S.path = fs::path(f).extension() == ".s2dsprite" || fs::path(f).extension() == ".s2sprite" ? f : "";
    S.dirty = false; S.ed->dirtyForProject = false;
    notify("Abierto: " + fs::path(f).filename().string() + " (" + std::to_string(S.ed->st.w) + "x" + std::to_string(S.ed->st.h) + ", " +
           std::to_string(S.ed->st.layers[0].cels.size()) + " frames)");
    return true;
}
static bool studioWrite(const std::string &f, int layout, bool withJson) {
    std::string err;
    if (!g_studio.ed->exportFile(f, layout, withJson, err)) { notify(err, true); return false; }
    return true;
}
static void studioSaveAs() {
    app.fileDlg.show(ui::FileDialog::Save, "Guardar sprite", ".s2dsprite", creationsDir("Sprites"), g_studio.ed->name + ".s2dsprite", [](const std::string &f) {
        if (studioWrite(f, 0, false)) { g_studio.path = f; g_studio.dirty = false; refreshCreations(); notify("Guardado en " + f); }
    });
}
static void studioSave() {
    if (g_studio.path.empty()) { studioSaveAs(); return; }
    if (studioWrite(g_studio.path, 0, false)) { g_studio.dirty = false; refreshCreations(); notify("Guardado"); }
}
static const char *kStudioFormats[] = {"PNG", "GIF animado", "BMP", "TGA", "QOI", "JPG", "Source Sprite (.s2sprite, solo para Source 2D)"};
static const char *kStudioExt[] = {".png", ".gif", ".bmp", ".tga", ".qoi", ".jpg", ".s2sprite"};
static const char *kStudioLayouts[] = {"Solo el frame actual", "Hoja horizontal (un frame al lado del otro)", "Hoja vertical",
                                       "Hoja en cuadrícula", "Un archivo por frame"};

/* ---------------- Source Sound ---------------- */
// suelto (desde Inicio, como Source Sprite) o dentro del proyecto (Musica y sonidos > Crear con Source Sound)
struct SoundStudio { bool on = false; std::unique_ptr<sound::Editor> ed; };
static SoundStudio g_sound;
static void setupSoundEditor(sound::Editor &e) {
    e.onStatus = [](const std::string &m) { notify(m, m.find("No ") == 0 || m.find("Para ") == 0); };
    e.onSaveDialog = [](const char *title, const char *ext, const std::string &name, std::function<void(const std::string &)> done) {
        app.fileDlg.show(ui::FileDialog::Save, title, ext, ui::documentsDir(), name, done);
    };
}
static void openSoundStudio() {
    if (!g_sound.ed) { g_sound.ed = std::make_unique<sound::Editor>(); g_sound.ed->song = sound::newSong("Mi canción"); setupSoundEditor(*g_sound.ed); }
    sound::clipStop();
    g_sound.on = true;
    app.homeSection = 6;   // al volver, a la lista de sonidos
}
// el sonido de "Musica y sonidos" en su documento (si no tiene cancion, uno nuevo)
static void openSound(const std::string &musicId) {
    openedFromPanel();
    for (auto &e : app.sounds) if (e->musicId == musicId) { e->focusMe = true; return; }
    json *m = findById(app.proj.doc["music"], musicId);
    if (!m) return;
    auto e = std::make_unique<sound::Editor>();
    e->musicId = musicId;
    e->song = m->contains("song") ? (*m)["song"] : sound::newSong(jstr(*m, "name", "Sonido"));
    setupSoundEditor(*e);
    sound::Editor *ep = e.get();
    e->onChanged = [ep]() { sound::saveToProject(app.proj, ep->song, ep->musicId); };
    app.sounds.push_back(std::move(e));
}
static std::string newProjectSound(bool sfx) {
    json song = sound::newSong(sfx ? "Efecto " + std::to_string(app.proj.doc["music"].size() + 1) : "Música " + std::to_string(app.proj.doc["music"].size() + 1), sfx);
    return sound::saveToProject(app.proj, song, "");
}
// la Cancion de Floppa (src/editor/presets/cancion_floppa.s2dsound, tools/make_cancion_floppa.py) en el proyecto
static void addFloppaSong() {
    json song = json::parse(std::string((const char *)preset_cancion, preset_cancion_len), nullptr, false);
    if (!song.is_object()) return;
    openSound(sound::saveToProject(app.proj, song, ""));
    notify("Canción Test añadida a Música y sonidos");
}
static void drawSoundStudio() {
    auto &S = g_sound;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##soundstudio", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ui::iconButton("ss_home", ui::Icon::Home, false, "Volver a tus sonidos", 30)) { S.ed->stop(); S.on = false; }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui::boldFont(), 19);
    ImGui::TextUnformatted("Source Sound");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "%s%s", S.ed->path.empty() ? jstr(S.ed->song, "name").c_str() : fs::path(S.ed->path).filename().string().c_str(), S.ed->dirty ? " *" : "");
    ImGui::SameLine(0, 24);
    if (ImGui::Button("Nueva música")) { S.ed->stop(); S.ed->song = sound::newSong("Mi canción"); S.ed->path.clear(); S.ed->dirty = false; }
    ImGui::SameLine();
    if (ImGui::Button("Nuevo efecto")) { S.ed->stop(); S.ed->song = sound::newSong("Mi efecto", true); S.ed->path.clear(); S.ed->dirty = false; }
    ImGui::SameLine();
    if (ImGui::Button("Abrir…"))
        app.fileDlg.show(ui::FileDialog::Open, "Abrir sonido", ".s2dsound|.s2sound", creationsDir("Sonidos"), "", [](const std::string &f) {
            std::ifstream in(f);
            json j = json::parse(in, nullptr, false);
            if (!j.is_object() || jstr(j, "format") != "source2d-sound") { notify("No es un sonido de Source Sound", true); return; }
            g_sound.ed->stop(); g_sound.ed->song = j; g_sound.ed->path = f; g_sound.ed->dirty = false;
        });
    ImGui::SameLine();
    auto saveTo = [](const std::string &f) {
        std::ofstream out(f);
        out << g_sound.ed->song.dump(1);
        if (out) { g_sound.ed->path = f; g_sound.ed->dirty = false; refreshCreations(); notify("Guardado en " + f); } else notify("No se pudo guardar " + f, true);
    };
    if (ui::primaryButton("Guardar", ImVec2(0, 0))) {
        if (S.ed->path.empty()) app.fileDlg.show(ui::FileDialog::Save, "Guardar sonido", ".s2dsound", creationsDir("Sonidos"), jstr(S.ed->song, "name", "sonido") + ".s2dsound", saveTo);
        else saveTo(S.ed->path);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Guarda en .s2dsound para seguir editándolo");
    ImGui::SameLine();
    if (ImGui::Button("Exportar .s2sound"))
        app.fileDlg.show(ui::FileDialog::Save, "Exportar para Source 2D", ".s2sound", ui::documentsDir(), jstr(S.ed->song, "name", "sonido") + ".s2sound", [](const std::string &f) {
            std::ofstream out(f);
            out << g_sound.ed->song.dump(1);
            if (out) notify("Exportado: " + f); else notify("No se pudo escribir " + f, true);
        });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo para Source 2D: la música o el efecto entero, para abrirlo en otro Source 2D (arrástralo a su ventana)");
    if (app.hasProject) {
        ImGui::SameLine();
        if (ImGui::Button("Añadir al proyecto")) { sound::saveToProject(app.proj, S.ed->song, ""); notify("Sonido añadido a Música y sonidos de " + app.proj.name()); }
    }
    ImGui::Separator();
    S.ed->drawBody();
    ImGui::End();
}

static void drawStudio() {
    auto &S = g_studio;
    if (!S.ed) studioNew(32, 32);
    if (S.ed->dirtyForProject) { S.dirty = true; S.ed->dirtyForProject = false; }
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##studio", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // barra superior
    if (ui::iconButton("st_home", ui::Icon::Home, false, "Volver a tus sprites", 30)) { if (S.dirty) S.askLeave = true; else S.on = false; }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui::boldFont(), 19);
    ImGui::TextUnformatted("Source Sprite");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "%s%s", S.path.empty() ? S.ed->name.c_str() : fs::path(S.path).filename().string().c_str(), S.dirty ? " *" : "");
    ImGui::SameLine(0, 24);
    if (ImGui::Button("Nuevo")) S.askNew = true;
    ImGui::SameLine();
    if (ImGui::Button("Abrir…"))
        app.fileDlg.show(ui::FileDialog::Open, "Abrir sprite o imagen", ".s2dsprite|.png|.gif|.bmp|.jpg|.jpeg|.tga|.qoi", ui::documentsDir(), "",
                         [](const std::string &f) { studioLoad(f); });
    ImGui::SameLine();
    if (ui::primaryButton("Guardar", ImVec2(0, 0))) studioSave();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Guarda en .s2dsprite: capas, frames y duraciones, para seguir editándolo (Ctrl+S)");
    ImGui::SameLine();
    if (ImGui::Button("Guardar como…")) studioSaveAs();
    ImGui::SameLine();
    if (ImGui::Button("Exportar…")) S.askExport = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("PNG, GIF animado, BMP, TGA, QOI o JPG; la animación en una sola hoja o un archivo por frame");
    if (app.hasProject) {
        ImGui::SameLine();
        if (ImGui::Button("Añadir al proyecto")) {
            std::string id = newSprite(S.ed->name, S.ed->st.w, S.ed->st.h);
            if (json *sp = app.proj.spriteById(id)) { S.ed->writeTo(*sp); app.proj.dirty = true; app.proj.rev++; notify("Sprite añadido a " + app.proj.name()); }
        }
    }
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) studioSaveAs(); else studioSave(); }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
        app.fileDlg.show(ui::FileDialog::Open, "Abrir sprite o imagen", ".s2dsprite|.png|.gif|.bmp|.jpg|.jpeg|.tga|.qoi", ui::documentsDir(), "",
                         [](const std::string &f) { studioLoad(f); });
    ImGui::Separator();
    S.ed->drawBody(S.proj);

    // dialogos
    if (S.askNew) { ImGui::OpenPopup("Nuevo sprite##studio"); S.askNew = false; }
    if (ui::beginModal("Nuevo sprite##studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Tamaño del lienzo");
        ImGui::SetNextItemWidth(90); ImGui::InputInt("##w", &S.nw, 0); ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputInt("##h", &S.nh, 0);
        for (int sz : {16, 32, 48, 64, 128}) {
            ImGui::PushID(sz);
            if (ImGui::SmallButton((std::to_string(sz) + "x" + std::to_string(sz)).c_str())) S.nw = S.nh = sz;
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (S.dirty) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Los cambios sin guardar del sprite actual se perderán.");
        if (ui::primaryButton("Crear", ImVec2(110, 0))) { studioNew(S.nw, S.nh); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (S.askExport) { ImGui::OpenPopup("Exportar sprite"); S.askExport = false; }
    if (ui::beginModal("Exportar sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int frames = (int)S.ed->st.layers[0].cels.size();
        ImGui::SetNextItemWidth(280);
        ImGui::Combo("Formato", &S.fmt, kStudioFormats, IM_ARRAYSIZE(kStudioFormats));
        bool gif = S.fmt == 1, own = S.fmt == 6;
        if (own) ImGui::TextDisabled("Todo el sprite (capas, frames y duraciones) para abrirlo en otro Source 2D:\narrástralo a su ventana o ábrelo en Source Sprite.");
        else if (gif) ImGui::TextDisabled("La animación entera, con la duración de cada frame y transparencia.");
        else {
            ImGui::SetNextItemWidth(280);
            ImGui::Combo("Contenido", &S.layout, kStudioLayouts, IM_ARRAYSIZE(kStudioLayouts));
            if (S.layout >= 1 && S.layout <= 3) ImGui::Checkbox("JSON con la posición de cada frame (formato de Aseprite)", &S.withJson);
            int W = 0, H = 0;
            if (S.layout != 4) { S.ed->sheet(S.layout, W, H); ImGui::TextDisabled("Imagen de %d x %d (%d frames)", W, H, S.layout == 0 ? 1 : frames); }
            else ImGui::TextDisabled("%d archivos: nombre_1, nombre_2...", frames);
        }
        if (ui::primaryButton("Exportar…", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            int fmt = S.fmt, layout = gif ? 1 : own ? 0 : S.layout;
            bool js = S.withJson && !gif && !own;
            app.fileDlg.show(ui::FileDialog::Save, "Exportar sprite", kStudioExt[fmt], ui::documentsDir(), S.ed->name + kStudioExt[fmt],
                             [layout, js](const std::string &f) { if (studioWrite(f, layout, js)) notify("Exportado: " + f); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (S.askLeave) { ImGui::OpenPopup("Salir de Source Sprite"); S.askLeave = false; }
    if (ui::beginModal("Salir de Source Sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("El sprite tiene cambios sin guardar.");
        if (ui::primaryButton("Guardar", ImVec2(110, 0))) { ImGui::CloseCurrentPopup(); studioSave(); if (!S.dirty) S.on = false; }
        ImGui::SameLine();
        if (ImGui::Button("Salir sin guardar", ImVec2(140, 0))) { ImGui::CloseCurrentPopup(); S.on = false; }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

/* ---------------- Inicio > Source Sprite y Source Sound: tus creaciones ----------------
   Los sprites (.s2dsprite o .s2sprite) y los sonidos (.s2dsound o .s2sound) se guardan por defecto en
   <carpeta de proyectos>/Sprites y /Sonidos. Cada apartado los muestra y los abre en su editor. */
static std::string creationsDir(const char *sub) {
    std::string d = projectsDir() + "/" + sub;
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}
static std::string lowerExtOf(const std::string &f) {
    std::string e = fs::path(f).extension().string();
    for (char &c : e) c = (char)tolower((unsigned char)c);
    return e;
}
static bool isSpriteFile(const std::string &f) { std::string e = lowerExtOf(f); return e == ".s2dsprite" || e == ".s2sprite"; }
static bool isSoundFile(const std::string &f) { std::string e = lowerExtOf(f); return e == ".s2dsound" || e == ".s2sound"; }
struct Creation { std::string path, name; long long bytes = 0; fs::file_time_type mtime; };
// del mas nuevo al mas viejo; se vuelve a mirar cada 2 s
static int g_creationsGen = 0;          // sube al guardar uno: la lista se vuelve a leer ya
static const std::vector<Creation> &creations(const char *sub, bool sprites) {
    struct Cache { std::vector<Creation> list; double at = -10; int gen = -1; };
    static std::map<std::string, Cache> caches;
    Cache &c = caches[sub];
    if (GetTime() - c.at < 2 && c.gen == g_creationsGen) return c.list;
    c.at = GetTime(); c.gen = g_creationsGen;
    c.list.clear();
    std::error_code ec;
    for (auto &e : fs::directory_iterator(creationsDir(sub), ec)) {
        std::string p = e.path().string();
        if (!e.is_regular_file(ec) || !(sprites ? isSpriteFile(p) : isSoundFile(p))) continue;
        Creation k;
        k.path = p; k.name = e.path().stem().string();
        k.bytes = (long long)e.file_size(ec); k.mtime = e.last_write_time(ec);
        c.list.push_back(k);
    }
    std::sort(c.list.begin(), c.list.end(), [](const Creation &a, const Creation &b) { return a.mtime > b.mtime; });
    return c.list;
}
static std::string fmtBytes(long long n) {
    char b[32];
    if (n < 1024) snprintf(b, sizeof b, "%lld B", n);
    else if (n < 1024 * 1024) snprintf(b, sizeof b, "%.0f KB", n / 1024.0);
    else snprintf(b, sizeof b, "%.1f MB", n / 1048576.0);
    return b;
}
static std::string fmtDate(const std::string &path) {
    std::string s = lastEdited(path);
    const char *pre = "Última edición: ";
    return s.rfind(pre, 0) == 0 ? s.substr(strlen(pre)) : s;
}

// miniatura de un sprite guardado: sus frames (hasta 24) y su tamano
struct SpriteThumb { std::vector<std::unique_ptr<ui::PixTexture>> frames; int w = 0, h = 0, n = 0; fs::file_time_type mtime; bool read = false; };
static std::map<std::string, SpriteThumb> g_sprThumbs;
static void releaseCreationThumbs() {
    for (auto &kv : g_sprThumbs) for (auto &t : kv.second.frames) t->release();
    g_sprThumbs.clear();
}
static SpriteThumb &spriteThumb(const Creation &c) {
    SpriteThumb &t = g_sprThumbs[c.path];
    if (t.read && t.mtime == c.mtime) return t;
    for (auto &f : t.frames) f->release();
    t = SpriteThumb{};
    t.read = true; t.mtime = c.mtime;
    std::ifstream in(c.path, std::ios::binary);
    json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return t;
    t.w = (int)jnum(j, "width", 0); t.h = (int)jnum(j, "height", 0);
    if (t.w <= 0 || t.h <= 0 || (long long)t.w * t.h > 4096LL * 4096 || !j.contains("frames") || !j["frames"].is_array()) return t;
    t.n = (int)j["frames"].size();
    for (int f = 0; f < std::min(t.n, 24); f++) {
        const json &fr = j["frames"][f];
        if (!fr.is_object() || !fr.contains("pixels") || !fr["pixels"].is_array()) continue;
        std::vector<Px> px((size_t)t.w * t.h);
        const json &pa = fr["pixels"];
        for (size_t i = 0; i < px.size() && i < pa.size(); i++) if (pa[i].is_string()) px[i] = parseColor(pa[i].get<std::string>());
        auto tex = std::make_unique<ui::PixTexture>();
        tex->update(px, t.w, t.h);
        t.frames.push_back(std::move(tex));
    }
    return t;
}

static std::string g_deleteCreation;   // a la espera de confirmar
// entrada escalonada: cada tarjeta aparece un poco despues que la anterior, subiendo
static float cardIntro(int i, double since) {
    if (!ui::animations()) return 1;
    float t = (float)std::clamp((GetTime() - since - i * 0.035) / 0.3, 0.0, 1.0);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
static double sectionShownAt(int section) {
    static int last = -1;
    static double at = 0;
    if (last != section) { last = section; at = GetTime(); }
    return at;
}

static void openSpriteFile(const std::string &f) {
    openStudio();
    if (studioLoad(f)) g_studio.on = true;
}

static void drawSpriteGallery() {
    double since = sectionShownAt(4);
    sectionTitle("Source Sprite", 30);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Tus sprites. Dibuja, anima y exporta a PNG, GIF, hojas de sprites o .s2sprite.");
    ImGui::Dummy(ImVec2(0, 6));
    if (homeButton("+ Nuevo sprite", true)) { openStudio(); g_studio.askNew = true; }
    ImGui::SameLine();
    if (homeButton("Abrir…", false))
        app.fileDlg.show(ui::FileDialog::Open, "Abrir sprite o imagen", ".s2dsprite|.s2sprite|.png|.gif|.bmp|.jpg|.jpeg|.tga|.qoi", creationsDir("Sprites"), "",
                         [](const std::string &f) { openSpriteFile(f); });
    if (g_studio.ed && g_studio.dirty) {
        ImGui::SameLine();
        if (homeButton("Seguir con el que no guardaste", false)) openStudio();
    }
    ImGui::Dummy(ImVec2(0, 10));
    const auto &list = creations("Sprites", true);
    if (list.empty()) {
        ImGui::TextDisabled("Todavía no tienes sprites. Los que guardes salen aquí.");
        return;
    }
    const float cw = 196, ph = 150, ch = ph + 84, gap = 18;
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + gap) / (cw + gap)));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float fs = ImGui::GetFontSize();
    for (int i = 0; i < (int)list.size(); i++) {
        const Creation &c = list[i];
        if (i % cols) ImGui::SameLine(0, gap);
        ImGui::PushID(c.path.c_str());
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        bool hit = ImGui::InvisibleButton("##spr", ImVec2(cw, ch));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("##sprctx")) {
            if (ImGui::MenuItem("Abrir")) hit = true;
            if (ImGui::MenuItem("Borrar…")) g_deleteCreation = c.path;
            ImGui::EndPopup();
        }
        float in = cardIntro(i, since), lift = ui::animTo(ImGui::GetID("##lift"), hov ? 1.0f : 0.0f, 16.0f);
        ImVec2 p(p0.x, p0.y + (1 - in) * 18 - lift * 4), e(p.x + cw, p.y + ch);
        int alpha = (int)(255 * in);
        if (lift > 0.01f) dl->AddRectFilled(ImVec2(p.x + 2, p.y + 6), ImVec2(e.x + 2, e.y + 6), IM_COL32(0, 0, 0, (int)(60 * lift * in)), 12);   // sombra
        dl->AddRectFilled(p, e, (ui::kPanel & 0x00FFFFFF) | ((ImU32)alpha << 24), 12);
        dl->AddRect(p, e, hov ? ui::kPrimary : ((ui::kLine & 0x00FFFFFF) | ((ImU32)alpha << 24)), 12, 0, hov ? 2.0f : 1.0f);
        ImVec2 a(p.x + 10, p.y + 10), b(e.x - 10, p.y + 10 + ph);
        ui::checkerboard(dl, a, b, 8);
        SpriteThumb &t = spriteThumb(c);
        if (!t.frames.empty()) {
            int f = (int)t.frames.size() > 1 ? (int)(GetTime() * 8) % (int)t.frames.size() : 0;
            ui::PixTexture *tx = t.frames[f].get();
            float sc = std::min((b.x - a.x - 12) / tx->w, (b.y - a.y - 12) / tx->h);
            if (sc >= 1) sc = std::floor(sc);
            float w = tx->w * sc, h = tx->h * sc, cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
            dl->AddImage(tx->id(), ImVec2(cx - w / 2, cy - h / 2), ImVec2(cx + w / 2, cy + h / 2), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
        }
        float y = b.y + 8;
        dl->PushClipRect(ImVec2(p.x + 10, y), ImVec2(e.x - 10, e.y), true);
        dl->AddText(ui::boldFont(), fs + 1, ImVec2(p.x + 12, y), (ui::kText & 0x00FFFFFF) | ((ImU32)alpha << 24), c.name.c_str());
        char info[96];
        if (t.w > 0) snprintf(info, sizeof info, "%d × %d px%s", t.w, t.h, t.n > 1 ? ("  ·  " + std::to_string(t.n) + " frames").c_str() : "");
        else snprintf(info, sizeof info, "No se pudo leer");
        dl->AddText(ImVec2(p.x + 12, y + fs + 6), ui::kText2, info);
        std::string meta = fmtBytes(c.bytes) + "  ·  " + fmtDate(c.path);
        dl->AddText(ImVec2(p.x + 12, y + 2 * fs + 12), ui::kDim, meta.c_str());
        dl->PopClipRect();
        if (hov) ImGui::SetTooltip("Clic: abrir en Source Sprite · Clic derecho: más");
        if (hit) openSpriteFile(c.path);
        ImGui::PopID();
    }
}

// un sonido guardado: la cancion, su forma de onda y cuanto dura (se calcula una vez)
struct SoundInfo { json song; std::vector<float> peaks; double secs = 0; fs::file_time_type mtime; bool read = false; };
static std::map<std::string, SoundInfo> g_sndInfo;
static SoundInfo &soundInfo(const Creation &c) {
    SoundInfo &s = g_sndInfo[c.path];
    if (s.read && s.mtime == c.mtime) return s;
    s = SoundInfo{};
    s.read = true; s.mtime = c.mtime;
    std::ifstream in(c.path, std::ios::binary);
    json j = json::parse(in, nullptr, false);
    if (j.is_object() && jstr(j, "format") == "source2d-sound") { s.song = j; s.peaks = sound::peaks(j, 44, &s.secs); }
    return s;
}
static void openSoundFile(const std::string &f) {
    std::ifstream in(f);
    json j = json::parse(in, nullptr, false);
    if (!j.is_object() || jstr(j, "format") != "source2d-sound") { notify("No es un sonido de Source Sound", true); return; }
    sound::clipStop();
    openSoundStudio();
    g_sound.ed->stop(); g_sound.ed->song = j; g_sound.ed->path = f; g_sound.ed->dirty = false;
}
static std::string fmtSecs(double s) {   // 0,4 s para los efectos cortos; 1:05 para la musica
    char b[16];
    if (s < 10) { snprintf(b, sizeof b, "%.1f s", s); for (char &c : b) if (c == '.') c = ','; return b; }
    int t = (int)std::round(s);
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

static void drawSoundGallery() {
    double since = sectionShownAt(6);
    sectionTitle("Source Sound", 30);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Tu música y tus efectos. Escúchalos aquí o ábrelos para seguir editándolos.");
    ImGui::Dummy(ImVec2(0, 6));
    auto fresh = [](bool sfx) {
        sound::clipStop();
        openSoundStudio();
        g_sound.ed->stop(); g_sound.ed->song = sound::newSong(sfx ? "Mi efecto" : "Mi canción", sfx); g_sound.ed->path.clear(); g_sound.ed->dirty = false;
    };
    if (homeButton("+ Nueva música", true)) fresh(false);
    ImGui::SameLine();
    if (homeButton("+ Nuevo efecto", true)) fresh(true);
    ImGui::SameLine();
    if (homeButton("Canción Test", false)) {   // la de ejemplo (32 s), para ver como esta hecha
        json song = json::parse(std::string((const char *)preset_cancion, preset_cancion_len), nullptr, false);
        if (song.is_object()) { sound::clipStop(); openSoundStudio(); g_sound.ed->stop(); g_sound.ed->song = song; g_sound.ed->path.clear(); g_sound.ed->dirty = false; }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Una canción de ejemplo de 32 segundos: ábrela, escúchala y mira cómo está hecha");
    ImGui::SameLine();
    if (homeButton("Abrir…", false))
        app.fileDlg.show(ui::FileDialog::Open, "Abrir sonido", ".s2dsound|.s2sound", creationsDir("Sonidos"), "", [](const std::string &f) { openSoundFile(f); });
    if (g_sound.ed && g_sound.ed->dirty) {
        ImGui::SameLine();
        if (homeButton("Seguir con el que no guardaste", false)) openSoundStudio();
    }
    ImGui::Dummy(ImVec2(0, 10));
    const auto &list = creations("Sonidos", false);
    if (list.empty()) {
        ImGui::TextDisabled("Todavía no tienes sonidos. Los que guardes salen aquí.");
        return;
    }
    // cada sonido como una nota de voz: play, forma de onda que se va llenando, duracion y editar
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float w = std::min(620.0f, ImGui::GetContentRegionAvail().x), h = 70, fs = ImGui::GetFontSize();
    std::string playing = sound::clipKey();
    for (int i = 0; i < (int)list.size(); i++) {
        const Creation &c = list[i];
        ImGui::PushID(c.path.c_str());
        SoundInfo &si = soundInfo(c);
        float in = cardIntro(i, since);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        ImVec2 p(p0.x + (1 - in) * 24, p0.y), e(p.x + w, p.y + h);
        int alpha = (int)(255 * in);
        bool sfx = jstr(si.song, "mode") == "sfx", on = playing == c.path;
        dl->AddRectFilled(p, e, (ui::kPanel2 & 0x00FFFFFF) | ((ImU32)alpha << 24), 16);
        // play / parar
        ImVec2 pc(p.x + 36, p.y + h / 2);
        ImGui::SetCursorScreenPos(ImVec2(pc.x - 22, pc.y - 22));
        if (ImGui::InvisibleButton("##play", ImVec2(44, 44)) && !si.song.is_null()) { if (on) sound::clipStop(); else sound::clipPlay(c.path, si.song); }
        bool ph = ImGui::IsItemHovered();
        float pulse = on ? 1 + 0.05f * std::sin((float)GetTime() * 6) : 1;
        dl->AddCircleFilled(pc, 21 * pulse, ph ? ui::kPrimaryLight : ui::kPrimary, 32);
        ui::drawIcon(dl, on ? ui::Icon::Stop : ui::Icon::Play, ImVec2(pc.x + (on ? 0 : 2), pc.y), 15, IM_COL32_WHITE);
        if (ph) ImGui::SetTooltip(on ? "Parar" : "Escuchar");
        // nombre y forma de onda
        float wx0 = p.x + 72, wx1 = e.x - 118;
        dl->AddText(ui::boldFont(), fs, ImVec2(wx0, p.y + 9), ui::kText, c.name.c_str());
        const char *kind = sfx ? "Efecto" : "Música";
        dl->AddText(ImVec2(wx0 + ui::boldFont()->CalcTextSizeA(fs, FLT_MAX, 0, c.name.c_str()).x + 10, p.y + 9), ui::kDim, kind);
        float prog = on ? sound::clipProgress() : 0;
        int nb = (int)si.peaks.size();
        float bw = nb ? (wx1 - wx0) / nb : 0, cy = p.y + 44;
        for (int k = 0; k < nb; k++) {
            float bh = 3 + si.peaks[k] * 20, x = wx0 + k * bw + bw * 0.2f;
            bool done = on && (k + 0.5f) / nb <= prog;
            dl->AddRectFilled(ImVec2(x, cy - bh / 2), ImVec2(x + bw * 0.6f, cy + bh / 2), done ? ui::kPrimaryLight : IM_COL32(255, 255, 255, (int)(70 * in)), 2);
        }
        if (on) dl->AddCircleFilled(ImVec2(wx0 + (wx1 - wx0) * prog, cy), 6, ui::kPrimaryLight, 16);
        std::string t = on ? fmtSecs(si.secs * prog) : fmtSecs(si.secs);
        dl->AddText(ImVec2(wx0, e.y - fs - 4), ui::kDim, t.c_str());
        std::string meta = fmtBytes(c.bytes) + " · " + fmtDate(c.path);
        float mw = ImGui::CalcTextSize(meta.c_str()).x;
        dl->AddText(ImVec2(wx1 - mw, e.y - fs - 4), ui::kDim, meta.c_str());
        // editar
        ImGui::SetCursorScreenPos(ImVec2(e.x - 104, p.y + h / 2 - 17));
        if (homeButton("Editar", false)) openSoundFile(c.path);
        ImGui::SameLine(0, 4);
        if (ui::iconButton("##del", ui::Icon::Trash, false, "Borrar", 30)) g_deleteCreation = c.path;
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 10));
        ImGui::PopID();
    }
}

// confirmacion para borrar un sprite o un sonido guardado (va a la papelera si se puede)
static void drawDeleteCreationModal() {
    if (!g_deleteCreation.empty() && !ImGui::IsPopupOpen("Borrar creación")) ImGui::OpenPopup("Borrar creación");
    if (ui::beginModal("Borrar creación", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("¿Borrar \"%s\"?", fs::path(g_deleteCreation).stem().string().c_str());
        ImGui::TextDisabled("No se puede deshacer.");
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(168, 56, 59, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(196, 70, 72, 255));
        if (ImGui::Button("Borrar", ImVec2(110, 0))) {
            if (sound::clipKey() == g_deleteCreation) sound::clipStop();
            std::error_code ec;
            fs::remove(g_deleteCreation, ec);
            if (ec) notify("No se pudo borrar", true); else notify("Borrado");
            g_deleteCreation.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) { g_deleteCreation.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static void refreshCreations() { g_creationsGen++; }

/* ---------------- noticias (Inicio > Noticias) ----------------
   Anuncios y versiones firmados por @floppa-1550 (ver novedades.cpp). Las tarjetas llevan una franja del color de su
   tipo; las de una version nueva traen el boton para bajarla y abrirla. */
static ImU32 newsColor(const std::string &tipo) {
    if (tipo == "actualizacion") return IM_COL32(0x4C, 0xC3, 0x8A, 255);
    if (tipo == "evento") return ui::kAccent;
    if (tipo == "consejo") return IM_COL32(0x55, 0xA8, 0xF0, 255);
    return IM_COL32(0xF0, 0xA0, 0x40, 255);   // aviso
}
static const char *newsKind(const std::string &tipo) {
    if (tipo == "actualizacion") return "Actualización";
    if (tipo == "evento") return "Evento";
    if (tipo == "consejo") return "Consejo";
    return "Aviso";
}
static std::string fechaBonita(const std::string &f) {   // 2026-10-28 -> 28/10/2026
    return f.size() == 10 ? f.substr(8, 2) + "/" + f.substr(5, 2) + "/" + f.substr(0, 4) : f;
}
static const Texture2D *newsImage(const std::string &path) {
    static std::map<std::string, Texture2D> cache;
    if (path.empty()) return nullptr;
    auto it = cache.find(path);
    if (it == cache.end()) {
        Texture2D t = LoadTexture(path.c_str());
        if (t.id) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        it = cache.emplace(path, t).first;
    }
    return it->second.id ? &it->second : nullptr;
}
// descargar / ver el progreso / abrir una version del repositorio
static void versionButton(const novedades::Version &v) {
    ImGui::PushID(v.nombre.c_str());
    if (v.nombre == S2D_VERSION) ImGui::TextDisabled("Ya estás usando esta versión.");
    else if (novedades::instalada(v.nombre)) {
        if (homeButton(("Abrir " + v.nombre).c_str(), true))
            for (auto &iv : installedVersions()) if (iv.name == v.nombre) launchVersion(iv.exe, false);
    } else {
        float pr = novedades::progreso(v.nombre);
        if (pr >= 0) {
            ImGui::ProgressBar(pr, ImVec2(320, 0), (std::to_string((int)(pr * 100)) + " %").c_str());
            ImGui::SameLine(); ImGui::TextDisabled("Descargando %.1f MB", v.tam / 1048576.0);
        } else {
            std::string err = novedades::errorDescarga(v.nombre);
            if (homeButton((err.empty() ? "Descargar " + v.nombre : "Reintentar").c_str(), true)) novedades::descargar(v);
            ImGui::SameLine(); ImGui::AlignTextToFramePadding();
            if (!err.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s", err.c_str());
            else ImGui::TextDisabled("%.1f MB · se queda en el selector de versiones", v.tam / 1048576.0);
        }
    }
    ImGui::PopID();
}
// encuesta de una noticia: antes de votar, una fila por opcion; al votar (o al cerrarse), las barras con los votos
static std::string tiempoQueda(long long seg) {
    if (seg >= 2 * 86400) return std::to_string(seg / 86400) + " días";
    if (seg >= 3600) return std::to_string(seg / 3600) + " h " + std::to_string(seg % 3600 / 60) + " min";
    if (seg >= 60) return std::to_string(seg / 60) + " min";
    return std::to_string(std::max(1LL, seg)) + " s";
}
static void newsPoll(const novedades::Noticia &n) {
    const novedades::Encuesta &e = n.encuesta;
    novedades::mirarEncuesta(n);
    std::vector<long long> v = novedades::votos(n.id);
    int mio = novedades::miVoto(n.id);
    bool cerrada = novedades::encuestaCerrada(n), resultados = cerrada || mio >= 0;
    long long total = 0;
    bool sabe = v.size() == e.opciones.size();
    for (long long x : v) if (x >= 0) total += x; else sabe = false;
    ImGui::PushID("##encuesta");
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushFont(ui::boldFont(), 0);
    ImGui::TextUnformatted(e.pregunta.c_str());
    ImGui::PopFont();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float w = std::min(ImGui::GetContentRegionAvail().x, 520.0f), h = ImGui::GetFrameHeight() + 6;
    for (size_t i = 0; i < e.opciones.size(); i++) {
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool hit = ImGui::InvisibleButton("##op", ImVec2(w, h), resultados ? ImGuiButtonFlags_None : ImGuiButtonFlags_MouseButtonLeft);
        bool hov = !resultados && ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImVec2 q(p.x + w, p.y + h);
        dl->AddRectFilled(p, q, hov ? ui::kPanel3 : ui::kPanel2, 6);
        std::string der;
        if (resultados) {
            long long x = i < v.size() ? v[i] : -1;
            float f = sabe && total > 0 ? (float)x / total : 0;
            if (f > 0) dl->AddRectFilled(p, ImVec2(p.x + std::max(12.0f, w * f), q.y), (int)i == mio ? ui::kPrimary : ui::kLine, 6);
            der = x < 0 ? "..." : (sabe && total > 0 ? std::to_string((int)std::lround(100.0 * x / total)) + " %" : "0 %");
        }
        if (!resultados || (int)i != mio) dl->AddRect(p, q, (int)i == mio ? ui::kAccent : ui::kLine, 6);
        std::string txt = e.opciones[i] + ((int)i == mio ? "  · tu voto" : "");
        float ty = p.y + (h - ImGui::GetFontSize()) / 2;
        ImGui::PushClipRect(p, ImVec2(q.x - 60, q.y), true);
        dl->AddText(ImVec2(p.x + 12, ty), ui::kText, txt.c_str());
        ImGui::PopClipRect();
        if (!der.empty()) dl->AddText(ImVec2(q.x - 12 - ImGui::CalcTextSize(der.c_str()).x, ty), ui::kText2, der.c_str());
        if (hit && !resultados) novedades::votar(n, (int)i);
        ImGui::PopID();
    }
    std::string pie = sabe ? std::to_string(total) + (total == 1 ? " voto" : " votos") : "Votos: ...";
    if (cerrada) pie += " · Encuesta terminada";
    else if (e.cierra > 0) pie += " · Quedan " + tiempoQueda(e.cierra - (long long)time(nullptr));
    else pie += " · Sin límite de tiempo";
    if (!resultados) pie += " · Elige una opción";
    ImGui::TextDisabled("%s", pie.c_str());
    std::string err = novedades::errorEncuesta(n.id);
    if (!err.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s", err.c_str());
    ImGui::PopID();
}

// texto de una noticia: {lista:Uno, Dos, Tres} sale como lista con puntos y lo de fuera como texto normal;
// {encuesta} es donde va la encuesta de la noticia (si no esta, la encuesta sale al final del texto)
static void newsRich(const std::string &s, const novedades::Noticia *poll = nullptr) {
    if (poll && poll->hayEncuesta) {
        size_t m = s.find("{encuesta}");
        if (m == std::string::npos) { newsRich(s); newsPoll(*poll); }
        else { newsRich(s.substr(0, m)); newsPoll(*poll); newsRich(s.substr(m + 10)); }
        return;
    }
    auto trim = [](std::string t) {
        size_t a = t.find_first_not_of(" \t\n"), b = t.find_last_not_of(" \t\n");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    size_t pos = 0;
    while (pos < s.size()) {
        size_t a = s.find("{lista:", pos);
        std::string plain = trim(s.substr(pos, a == std::string::npos ? std::string::npos : a - pos));
        if (!plain.empty()) ImGui::TextUnformatted(plain.c_str());
        if (a == std::string::npos) break;
        size_t b = s.find('}', a);
        if (b == std::string::npos) { ImGui::TextUnformatted(s.c_str() + a); break; }
        std::string body = s.substr(a + 7, b - a - 7);
        size_t q = 0;
        while (q <= body.size()) {
            size_t c = body.find(',', q);
            std::string item = trim(body.substr(q, c == std::string::npos ? std::string::npos : c - q));
            if (!item.empty()) ImGui::BulletText("%s", item.c_str());
            if (c == std::string::npos) break;
            q = c + 1;
        }
        pos = b + 1;
    }
}

static void newsCard(const novedades::Noticia &n, float w) {
    ImGui::PushID(n.id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 16));
    ImGui::BeginChild("##card", ImVec2(w, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImU32 col = newsColor(n.tipo);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", newsKind(n.tipo));
    ImGui::SameLine(); ImGui::TextDisabled("· %s", fechaBonita(n.fecha).c_str());
    if (n.fijada) { ImGui::SameLine(); ImGui::TextDisabled("· Fijada"); }
    if (novedades::nueva(n.id)) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kAccent), "Nuevo"); }
    ImGui::PushFont(ui::boldFont(), 20);
    ImGui::PushTextWrapPos(w - 22);
    ImGui::TextUnformatted(n.titulo.c_str());
    ImGui::PopFont();
    if (!n.texto.empty() || n.hayEncuesta) newsRich(n.texto, &n);   // titulo, texto (con la encuesta), imagen y debajo lo "inferior"
    if (const Texture2D *t = newsImage(n.imagen)) {
        float iw = std::min((float)t->width, w - 44), ih = iw * t->height / std::max(1, t->width);
        ImGui::Image((ImTextureID)(intptr_t)t->id, ImVec2(iw, ih));
    }
    if (!n.inferior.empty()) newsRich(n.inferior);
    ImGui::PopTextWrapPos();
    if (!n.version.empty())
        for (auto &v : novedades::versiones()) if (v.nombre == n.version) { ImGui::Dummy(ImVec2(0, 2)); versionButton(v); }
    if (!n.enlace.empty()) { ImGui::Dummy(ImVec2(0, 2)); if (homeButton("Abrir enlace", false, 30)) app.pendingLink = n.enlace; }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Publicado por " S2D_FIRMA_AUTOR " · firma verificada");
    ImGui::EndChild();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + 5, b.y), col, 10, ImDrawFlags_RoundCornersLeft);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PopID();
}
static void drawNewsSection() {
    sectionTitle("Noticias", 30);
    std::string st = novedades::estado();
    if (!st.empty()) ImGui::TextDisabled("%s", st.c_str());
    ImGui::Dummy(ImVec2(0, 6));
    float w = std::min(ImGui::GetContentRegionAvail().x, 760.0f);
    // version estable mas nueva que esta: arriba de todo (si no tiene ya su propia noticia)
    auto &ns = novedades::noticias();
    for (auto &v : novedades::versiones()) {
        if (v.canal != "estable" || v.num <= S2D_VERSION_NUM) continue;
        bool hasNews = false;
        for (auto &n : ns) if (n.version == v.nombre) hasNews = true;
        if (hasNews) break;
        novedades::Noticia n;
        n.id = -1; n.tipo = "actualizacion"; n.fecha = v.fecha; n.titulo = "Nueva actualización: " + v.nombre; n.texto = v.notas; n.version = v.nombre;
        newsCard(n, w);
        break;
    }
    if (ns.empty() && st.empty()) ImGui::TextDisabled("Todavía no hay noticias.");
    for (auto &n : ns) newsCard(n, w);
    novedades::marcarLeidas();
}

static void drawStartPage() {
    const float menuW = 210;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    // menu lateral (HomePageMenu)
    ImGui::BeginChild("##homemenu", ImVec2(menuW, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (!app.hasProject) {
        ImVec2 lp = ImGui::GetCursorScreenPos();
        ImDrawList *ml = ImGui::GetWindowDrawList();
        ml->AddText(ui::boldFont(), 21, ImVec2(lp.x + 18, lp.y + 18), ui::kText, "Source 2D");
        ImGui::Dummy(ImVec2(0, 62));
    } else ImGui::Dummy(ImVec2(0, 12));
    // id: el apartado (4 abre Source Sprite); Noticias solo si este editor busca noticias
    struct Item { const char *label; ui::Icon icon; int id; };
    std::vector<Item> items = {{"Crear", ui::Icon::Create, 0}, {"Juegos", ui::Icon::Gamepad, 1}, {"Plantillas", ui::Icon::Book, 2}, {"Escuela", ui::Icon::People, 3}};
    if (novedades::activo()) items.push_back({"Noticias", ui::Icon::Bell, 5});
    items.push_back({"Source Sprite", ui::Icon::Image, 4});
    items.push_back({"Source Sound", ui::Icon::Bell, 6});
    for (int k = 0; k < (int)items.size(); k++) {
        const Item &it = items[k];
        int i = it.id;
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.x += 8;
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##sec", ImVec2(menuW - 16, 40))) app.homeSection = i;
        if (i == 6 && ImGui::IsItemHovered()) ImGui::SetTooltip("Tu música y tus efectos de sonido (piano, batería, sintetizadores...)");
        if (i == 4 && ImGui::IsItemHovered()) ImGui::SetTooltip("Tus sprites: dibújalos, anímalos y guárdalos en cualquier formato");
        bool hov = ImGui::IsItemHovered(), sel = app.homeSection == i;
        ImDrawList *ml = ImGui::GetWindowDrawList();
        if (sel || hov) ml->AddRectFilled(p, ImVec2(p.x + menuW - 16, p.y + 40), sel ? ui::kPanel2 : ui::kHover, 8);
        ImU32 c = sel ? ui::kSelectedIconBg : ui::kText2;
        ui::drawIcon(ml, it.icon, ImVec2(p.x + 24, p.y + 20), 18, c);
        ml->AddText(sel ? ui::boldFont() : ImGui::GetFont(), ImGui::GetFontSize() + 1, ImVec2(p.x + 46, p.y + 11), sel ? ui::kText : ui::kText2, it.label);
        if (i == 5 && novedades::sinLeer() > 0) ml->AddCircleFilled(ImVec2(p.x + menuW - 40, p.y + 20), 4.5f, ui::kAccent);   // hay noticias sin leer
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 2));
    }
    // version: al pie; elegir otra la abre (y cierra esta, preguntando si hay cambios sin guardar)
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), avail.y - 86));
    ImGui::SetCursorPosX(16);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kDim), "Versión");
    ImGui::SetCursorPosX(16);
    ImGui::SetNextItemWidth(menuW - 32);
    std::string curVer = std::string(S2D_VERSION) + (app.vulkan ? " (Vulkan)" : "");
    if (ImGui::BeginCombo("##ver", curVer.c_str())) {
        static std::vector<InstalledVersion> vers;
        if (ImGui::IsWindowAppearing()) vers = installedVersions();
        auto launch = launchVersion;
        for (auto &v : vers) {
            bool cur = v.name == S2D_VERSION && !app.vulkan;
            if (ImGui::Selectable(v.name.c_str(), cur) && !cur) launch(v.name == S2D_VERSION ? selfExe() : v.exe, false);
        }
        if (vers.size() <= 1 && !app.showExperimental && !hasDownloadable()) ImGui::TextDisabled("Las versiones que abras se guardan aquí.");
        // las del repositorio que no estan en este equipo: se bajan aqui mismo y luego salen arriba
        bool title = false;
        for (auto &v : novedades::versiones()) {
            if (novedades::instalada(v.nombre) || (v.canal != "estable" && !app.showExperimental)) continue;
            if (!title) { ImGui::SeparatorText("Para descargar"); title = true; }
            ImGui::PushID(v.nombre.c_str());
            float pr = novedades::progreso(v.nombre);
            std::string lbl = v.nombre + (v.canal != "estable" ? " (experimental)" : "");
            if (pr >= 0) { ImGui::TextUnformatted(lbl.c_str()); ImGui::ProgressBar(pr, ImVec2(-1, 0)); }
            else if (ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) novedades::descargar(v);
            else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Descargar (%.1f MB)", v.tam / 1048576.0);
            std::string err = novedades::errorDescarga(v.nombre);
            if (!err.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s", err.c_str());
            ImGui::PopID();
        }
        if (title && ImGui::IsWindowFocused()) vers = installedVersions();   // la que acaba de bajarse sale arriba
        ImGui::Separator();
        if (ImGui::Selectable(app.showExperimental ? "- Experimentales" : "+ Experimentales", false, ImGuiSelectableFlags_DontClosePopups))
            app.showExperimental = !app.showExperimental;
        if (app.showExperimental) {
            std::string vk = std::string("   ") + S2D_VERSION + " (Vulkan)";
            if (ImGui::Selectable(vk.c_str(), app.vulkan) && !app.vulkan) launch(selfExe(), true);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Los juegos (probar y exportar) se dibujan con Vulkan en vez de OpenGL.\n"
                                  "Si un equipo no tiene Vulkan, el juego sigue con OpenGL solo.\n"
                                  "Los shaders propios necesitan shaderc instalado. El editor sigue con OpenGL.");
        }
        ImGui::EndCombo();
    }
    if (app.vulkan) { ImGui::SetCursorPosX(16); ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Experimental"); }
    ImGui::EndChild();
    dl->AddLine(ImVec2(wp.x + menuW, wp.y), ImVec2(wp.x + menuW, wp.y + avail.y), ui::kLine);
    // contenido
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 28));
    ImGui::BeginChild("##homebody", ImVec2(0, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    /* al cambiar de apartado, el nuevo entra con un fundido */
    static int lastSection = -1;
    ImGuiID fid = ImGui::GetID("##secfade");
    if (lastSection != app.homeSection) { ImGui::GetStateStorage()->SetFloat(fid, 0.0f); lastSection = app.homeSection; }
    float fa = ui::animTo(fid, 1.0f, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.15f + 0.85f * fa);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - fa) * 10.0f);
    if (app.homeSection == 1) drawTestsSection(false); else if (app.homeSection == 2) drawTestsSection(true);
    else if (app.homeSection == 3) { sectionTitle("Escuela", 30); school::drawSection(); }
    else if (app.homeSection == 5) drawNewsSection();
    else if (app.homeSection == 4) drawSpriteGallery();
    else if (app.homeSection == 6) drawSoundGallery();
    else drawCreateSection();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::EndChild();
}

static void drawWelcomeDoc() {
    if (app.focusWelcome) { ImGui::SetNextWindowFocus(); app.focusWelcome = false; }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);
    ImGui::Begin("Inicio");
    ImGui::PopStyleVar();
    drawStartPage();
    ImGui::End();
}

static void drawHome() {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##home", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    drawStartPage();
    ImGui::End();
}

// dialogos de la pagina de inicio: nombre del proyecto nuevo y cambios sin guardar
static void drawStartDialogs() {
    if (!app.forgetRecent.empty()) {
        app.recent.erase(std::remove(app.recent.begin(), app.recent.end(), app.forgetRecent), app.recent.end());
        std::ofstream out(recentFile());
        for (auto &r : app.recent) out << r << "\n";
        app.forgetRecent.clear();
    }
    if (!app.pendingLink.empty() && !ImGui::IsPopupOpen("Abrir enlace")) ImGui::OpenPopup("Abrir enlace");
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (ui::beginModal("Abrir enlace", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted("Se abrirá esta página en el navegador:");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kAccent), "%s", app.pendingLink.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        if (ui::primaryButton("Abrir", ImVec2(120, 0))) { OpenURL(app.pendingLink.c_str()); app.pendingLink.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) { app.pendingLink.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    // anuncio importante: sale una sola vez en una ventana al abrir el editor
    static int shownImportant = 0;
    if (const novedades::Noticia *imp = novedades::importantePendiente(); imp && !ImGui::IsPopupOpen("Aviso", ImGuiPopupFlags_AnyPopupId) && !ImGui::GetTopMostPopupModal()) {
        shownImportant = imp->id;
        ImGui::OpenPopup("Aviso");
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    if (ui::beginModal("Aviso", nullptr, ImGuiWindowFlags_NoResize)) {
        const novedades::Noticia *imp = nullptr;
        for (auto &n : novedades::noticias()) if (n.id == shownImportant) imp = &n;
        if (imp) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(newsColor(imp->tipo)), "%s", newsKind(imp->tipo));
            ImGui::PushTextWrapPos();
            ImGui::PushFont(ui::boldFont(), 20); ImGui::TextUnformatted(imp->titulo.c_str()); ImGui::PopFont();
            newsRich(imp->texto, imp);
            if (const Texture2D *t = newsImage(imp->imagen)) {
                float iw = std::min((float)t->width, 516.0f), ih = iw * t->height / std::max(1, t->width);
                ImGui::Image((ImTextureID)(intptr_t)t->id, ImVec2(iw, ih));
            }
            newsRich(imp->inferior);
            ImGui::PopTextWrapPos();
            ImGui::TextDisabled("Publicado por " S2D_FIRMA_AUTOR " · firma verificada");
        }
        ImGui::Dummy(ImVec2(0, 4));
        bool go = ui::primaryButton("Ver noticias", ImVec2(140, 0));
        ImGui::SameLine();
        if (go || ImGui::Button("Cerrar", ImVec2(110, 0))) {
            novedades::importanteVisto(shownImportant);
            if (go) { app.homeSection = 5; app.focusWelcome = app.hasProject; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (app.askNew) { ImGui::OpenPopup("Nuevo proyecto"); app.askNew = false; }
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    if (ui::beginModal("Nuevo proyecto", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Nombre del juego");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##nn", app.newName, sizeof app.newName, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("Se guardará en %s", newProjectPath(app.newName).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("Resolución");
        static const struct { const char *name, *desc; int w, h; } res[] = {
            {"HD 1280 x 720", "Juegos con dibujos detallados", 1280, 720},
            {"960 x 540", "Lo normal de los ejemplos", 960, 540},
            {"Pixel art 480 x 270", "Se ve grande y nítido en pantalla completa", 480, 270}};
        for (int i = 0; i < 3; i++) {
            if (ImGui::RadioButton(res[i].name, app.newRes == i)) { app.newRes = i; app.newW = res[i].w; app.newH = res[i].h; }
            ImGui::SameLine(200); ImGui::TextDisabled("%s", res[i].desc);
        }
        if (ImGui::RadioButton("Personalizada", app.newRes == 3)) app.newRes = 3;
        if (app.newRes == 3) {
            ImGui::SameLine(200); ImGui::SetNextItemWidth(90); ImGui::InputInt("##rw", &app.newW, 0);
            ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("##rh", &app.newH, 0);
            app.newW = std::clamp(app.newW, 64, 4096); app.newH = std::clamp(app.newH, 64, 4096);
        }
        ImGui::Dummy(ImVec2(0, 4));
        if (ui::primaryButton("Crear", ImVec2(120, 0)) || enter) {
            ImGui::CloseCurrentPopup();
            std::string nm = app.newName;
            int w = app.newRes == 3 ? app.newW : res[app.newRes].w, h = app.newRes == 3 ? app.newH : res[app.newRes].h;
            guardSwitch([nm, w, h] { createProject(nm, w, h); refreshProjectList(); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!app.pendingDeleteProject.empty()) { ImGui::OpenPopup("Borrar proyecto"); app.deletingProject = app.pendingDeleteProject; app.pendingDeleteProject.clear(); }
    ImGui::SetNextWindowSize(ImVec2(500, 0));
    if (ui::beginModal("Borrar proyecto", nullptr, ImGuiWindowFlags_NoResize)) {
        std::string f = app.deletingProject;
        ImGui::PushTextWrapPos();
        ImGui::Text("¿Borrar \"%s\"?", fileProjectName(f).c_str());
        ImGui::TextDisabled("Se borra el archivo %s. No se puede deshacer.", f.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(168, 56, 59, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(196, 70, 72, 255));
        if (ImGui::Button("Borrar", ImVec2(120, 0))) {
            std::error_code ec;
            fs::remove(f, ec);
            // si el proyecto tenia su propia carpeta dentro de la de proyectos y ya esta vacia, se borra tambien
            fs::path dir = fs::path(f).parent_path();
            if (fs::absolute(dir.parent_path(), ec) == fs::absolute(projectsDir(), ec) && fs::is_empty(dir, ec)) fs::remove(dir, ec);
            app.recent.erase(std::remove(app.recent.begin(), app.recent.end(), f), app.recent.end());
            std::ofstream out(recentFile());
            for (auto &r : app.recent) out << r << "\n";
            refreshProjectList();
            notify("Proyecto borrado");
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (app.showExport) { ImGui::OpenPopup("Exportar"); app.showExport = false; if (app.exportDir.empty()) app.exportDir = projectsDir(); }
    ImGui::SetNextWindowSize(ImVec2(620, 0));
    if (ui::beginModal("Exportar", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::SeparatorText("Tipo");
        static const char *kinds[][2] = {
            {"Ligera", "Motor y juego comprimidos sin perder calidad: ocupa muy poco (un juego pequeño, ~600 KB)."},
            {"Pesada", "Sin comprimir: ocupa más, pero abre al instante."},
            {"Optimizada", "Ligera y además quita los sprites, audios y fuentes que el juego no usa."}};
        for (int i = 0; i < 3; i++) {
            if (ImGui::RadioButton(kinds[i][0], app.exportKind == i)) app.exportKind = i;
            ImGui::SameLine(130); ImGui::TextDisabled("%s", kinds[i][1]);
        }
        ImGui::SeparatorText("Formato");
        if (ImGui::RadioButton("Un solo archivo", !app.exportFolder)) app.exportFolder = false;
        ImGui::SameLine(170);
        if (ImGui::RadioButton("Carpeta", app.exportFolder)) app.exportFolder = true;
        ImGui::SameLine(290);
        ImGui::TextDisabled(app.exportFolder ? "El motor y el juego por separado (datos/juego.source2d.json)." : "Todo dentro de un ejecutable.");
        ImGui::SeparatorText("Para");
        ImGui::BeginDisabled(!canExport("windows")); ImGui::Checkbox("Windows (.exe)", &app.exWin); ImGui::EndDisabled();
        ImGui::SameLine(170); ImGui::BeginDisabled(!canExport("linux")); ImGui::Checkbox("Linux", &app.exLinux); ImGui::EndDisabled();
        ImGui::SameLine(290); ImGui::Checkbox("Web (HTML)", &app.exWeb);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Un .zip con index.html, listo para itch.io o una game jam");
        ImGui::SameLine(430); ImGui::BeginDisabled(true); ImGui::Checkbox("Android", &app.exAndroid); ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Todavía no: hace falta el kit de Android (SDK y NDK) para crear el .apk");
        ImGui::Checkbox("Solo para Source 2D (.source2d)", &app.exSource);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("El proyecto entero en un archivo, para abrirlo en otro Source 2D:\narrástralo a su ventana o ábrelo con Abrir. No se juega fuera de Source 2D.");
        ImGui::SeparatorText("Carpeta");
        ImGui::TextUnformatted(app.exportDir.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cambiar")) {
            ImGui::CloseCurrentPopup();
            app.fileDlg.show(ui::FileDialog::Folder, "Carpeta para exportar", "", app.exportDir, "", [](const std::string &d) { app.exportDir = d; app.showExport = true; });
        }
        std::string nm = app.proj.name(); for (char &c : nm) if (c == ' ' || c == '/' || c == '\\') c = '_';
        if (app.exportFolder)
            ImGui::TextDisabled("Se crean las carpetas: %s%s%s", app.exWin ? (nm + "-windows/  ").c_str() : "", app.exLinux ? (nm + "-linux/  ").c_str() : "",
                                app.exWeb ? (nm + "-web/").c_str() : "");
        else
            ImGui::TextDisabled("Se crean: %s%s%s%s", app.exWin ? (nm + ".exe  ").c_str() : "", app.exLinux ? (nm + ".x86_64  ").c_str() : "",
                                app.exWeb ? (nm + "-web.zip  ").c_str() : "", app.exSource ? (nm + ".source2d").c_str() : "");
        ImGui::Dummy(ImVec2(0, 6));
        bool any = (app.exWin && canExport("windows")) || (app.exLinux && canExport("linux")) || app.exWeb || app.exSource;
        ImGui::BeginDisabled(!any);
        if (ui::primaryButton("Exportar", ImVec2(130, 0))) {
            bool lite = app.exportKind != 1, opt = app.exportKind == 2;
            fs::path dir = app.exportDir;
            if (app.exportFolder) {
                if (app.exWin && canExport("windows")) exportGame((dir / (nm + "-windows")).string(), "windows", false, opt, true);
                if (app.exLinux && canExport("linux")) exportGame((dir / (nm + "-linux")).string(), "linux", false, opt, true);
                if (app.exWeb) exportGame((dir / (nm + "-web")).string(), "html", lite, opt, true);
            } else {
                if (app.exWin && canExport("windows")) exportGame((dir / (nm + ".exe")).string(), "windows", lite, opt);
                if (app.exLinux && canExport("linux")) exportGame((dir / (nm + ".x86_64")).string(), "linux", lite, opt);
                if (app.exWeb) exportGame((dir / (nm + "-web.zip")).string(), "html", lite, opt);
            }
            if (app.exSource) exportSourceGame((dir / (nm + ".source2d")).string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (app.showOptions) { ImGui::OpenPopup("Opciones"); app.showOptions = false; }
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    if (ui::beginModal("Opciones", nullptr, ImGuiWindowFlags_NoResize)) {
        EditorPrefs &P = prefs();
        ImGui::SeparatorText("Guardado");
        ImGui::Checkbox("Guardado automático", &P.autosave);
        ImGui::SameLine(220); ImGui::SetNextItemWidth(160); ImGui::BeginDisabled(!P.autosave);
        ImGui::SliderInt("##secs", &P.autosaveSecs, 10, 600, "cada %d s"); ImGui::EndDisabled();
        ImGui::TextUnformatted("Versiones que se guardan"); ImGui::SameLine(220); ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("##vers", &P.keepVersions, 0, 100, P.keepVersions ? "%d" : "ninguna");
        ImGui::TextDisabled("Cada guardado deja una copia con fecha: Archivo > Versiones del proyecto.");
        ImGui::SeparatorText("Editor");
        if (ImGui::Checkbox("Animaciones suaves", &P.animations)) ui::setAnimations(P.animations);
        std::vector<std::string> langs = ui::languages();
        ImGui::TextUnformatted("Idioma"); ImGui::SameLine(220); ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##lang", P.lang.empty() ? "Español" : P.lang.c_str())) {
            for (auto &l : langs) if (ImGui::Selectable(l.c_str(), (P.lang.empty() ? "Español" : P.lang) == l)) { P.lang = l == "Español" ? "" : l; ui::setLanguage(P.lang); }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Los idiomas son los .txt de la carpeta Idiomas (junto al editor).");
        if (novedades::activo()) {
            if (ImGui::Checkbox("Buscar noticias y actualizaciones", &P.news)) novedades::setBuscar(P.news);
            ImGui::TextDisabled("Al abrir y cada 3 horas. Solo se descargan archivos públicos; no se envía nada.");
        }
        ImGui::SeparatorText("Experimental");
        ImGui::Checkbox("Guardado a prueba de apagones", &P.safeSave);
        ImGui::TextDisabled("Cada 5 s copia lo que no has guardado (forzado al disco). Si se va la luz o el\n"
                            "equipo se apaga de golpe, al volver a abrir el editor te ofrece recuperarlo.");
        ImGui::Dummy(ImVec2(0, 4));
        if (ui::primaryButton("Listo", ImVec2(120, 0))) { savePrefs(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (app.showVersions) { ImGui::OpenPopup("Versiones del proyecto"); app.showVersions = false; }
    ImGui::SetNextWindowSize(ImVec2(620, 460));
    if (ui::beginModal("Versiones del proyecto", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::vector<ProjVersion> vers;
        if (ImGui::IsWindowAppearing()) vers = projectVersions();
        ImGui::TextDisabled("Cada guardado deja una copia. Abrir una la pone en el editor (sin guardar) para que la revises.");
        ImGui::BeginChild("##vl", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 6), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < vers.size(); i++) {
            ImGui::PushID((int)i);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", vers[i].date.c_str()); ImGui::SameLine(200);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(vers[i].kind == "guardado automático" ? ui::kDim : ui::kText2), "%s", vers[i].kind.c_str());
            ImGui::SameLine(380); ImGui::TextDisabled("%.1f MB", vers[i].size / 1048576.0);
            ImGui::SameLine(480);
            if (ImGui::SmallButton("Abrir esta")) {
                Project p; std::string err;
                if (p.load(vers[i].file, err)) {
                    std::string keep = app.proj.path;
                    std::string vf = vers[i].file;
                    guardSwitch([p, keep, vf]() mutable {
                        app.proj.doc = p.doc; app.proj.path = keep; app.proj.dirty = true; app.proj.rev++;
                        app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear();
                        app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset();
                        app.openFirstScene = true; app.wantLayout = true;
                        notify("Versión abierta: " + fs::path(vf).filename().string() + ". Guarda (Ctrl+S) para quedarte con ella");
                    });
                    ImGui::CloseCurrentPopup();
                } else notify(err, true);
            }
            ImGui::PopID();
        }
        if (vers.empty()) ImGui::TextDisabled("Todavía no hay versiones: se crean al guardar.");
        ImGui::EndChild();
        if (ImGui::Button("Cerrar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!app.recoveries.empty() && !ImGui::IsPopupOpen("Recuperar")) ImGui::OpenPopup("Recuperar");
    ImGui::SetNextWindowSize(ImVec2(560, 0));
    if (ui::beginModal("Recuperar", nullptr, ImGuiWindowFlags_NoResize)) {
        std::string rf = app.recoveries.empty() ? "" : app.recoveries.front();
        std::ifstream in(rf);
        json r = json::parse(in, nullptr, false);
        ImGui::PushTextWrapPos();
        ImGui::Text("El editor se cerró de golpe y quedaron cambios sin guardar de \"%s\".", r.is_object() ? jstr(r, "name").c_str() : "?");
        ImGui::TextDisabled("Si los recuperas se abren en el editor; guárdalos para quedarte con ellos.");
        ImGui::PopTextWrapPos();
        auto next = [rf] { std::error_code ec; fs::remove(rf, ec); if (!app.recoveries.empty()) app.recoveries.erase(app.recoveries.begin()); ImGui::CloseCurrentPopup(); };
        if (ui::primaryButton("Recuperar", ImVec2(130, 0)) && r.is_object() && r.contains("doc")) {
            app.proj = Project{}; app.proj.doc = r["doc"]; migrateSceneTilemaps(app.proj.doc); app.proj.path = jstr(r, "path"); app.proj.dirty = true; app.hasProject = true;
            app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear();
            app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset();
            app.openFirstScene = true; app.wantLayout = true;
            notify("Cambios recuperados: guarda con Ctrl+S");
            next();
        }
        ImGui::SameLine();
        if (ImGui::Button("Descartar", ImVec2(110, 0))) next();
        ImGui::EndPopup();
    }
    if (app.showLicenses) { ImGui::OpenPopup("Licencias de terceros"); app.showLicenses = false; }
    ImGui::SetNextWindowSize(ImVec2(700, 520), ImGuiCond_Appearing);
    if (ui::beginModal("Licencias de terceros")) {
        ImGui::TextUnformatted("Los juegos exportados llevan raylib, GLFW, QuickJS y DejaVu Sans (./juego --licencias las muestra).");
        ImGui::BeginChild("##lic", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(licencias_editor);
        ImGui::EndChild();
        if (ImGui::Button("Cerrar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (app.askTrust) { ImGui::OpenPopup("Proyecto de otra persona"); app.askTrust = false; }
    if (ui::beginModal("Proyecto de otra persona", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("\"%s\" no se creó ni se abrió antes en este editor.", fs::path(app.pendingTrust).filename().string().c_str());
        ImGui::Dummy(ImVec2(0, 4));
        if (app.trustHasCode) ImGui::TextUnformatted("Trae código JavaScript (extensiones o acciones de código).");
        ImGui::TextUnformatted("Al probarlo o exportarlo, sus eventos y su código se ejecutan en tu equipo\n"
                               "y pueden leer, cambiar o borrar tus archivos.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Ábrelo solo si confías en quien te lo pasó.");
        ImGui::Dummy(ImVec2(0, 4));
        if (ui::primaryButton("Confiar y abrir", ImVec2(150, 0))) {
            std::string f = app.pendingTrust; app.pendingTrust.clear();
            ImGui::CloseCurrentPopup();
            openProject(f, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) { app.pendingTrust.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (app.askSwitch) { ImGui::OpenPopup("Cambios sin guardar"); app.askSwitch = false; }
    if (ui::beginModal("Cambios sin guardar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Hay cambios sin guardar en \"%s\".", app.proj.name().c_str());
        auto go = [] { auto f = std::move(app.pendingSwitch); app.pendingSwitch = nullptr; ImGui::CloseCurrentPopup(); if (f) f(); };
        if (ui::primaryButton("Guardar y seguir", ImVec2(150, 0))) {
            if (app.proj.path.empty()) { ImGui::CloseCurrentPopup(); app.pendingSwitch = nullptr; saveAs(); }
            else if (saveProject(app.proj.path)) go();
        }
        ImGui::SameLine();
        if (ImGui::Button("Seguir sin guardar", ImVec2(150, 0))) go();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110, 0))) { app.pendingSwitch = nullptr; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

/* optimizada: fuera los sprites, audios, fuentes y conjuntos de tiles que nada usa (se busca su id y su nombre en
   el resto del proyecto); despues se exporta ligera */
static json optimizedDoc(json doc) {
    std::string all = doc.dump();
    auto count = [&](const std::string &needle) { size_t n = 0; for (size_t p = all.find(needle); p != std::string::npos; p = all.find(needle, p + 1)) n++; return n; };
    int removed = 0;
    for (const char *key : {"sprites", "music", "fonts", "tilesets"}) {
        if (!doc.contains(key) || !doc[key].is_array()) continue;
        json keep = json::array();
        for (auto &it : doc[key]) {
            std::string id = jstr(it, "id"), nm = jstr(it, "name");
            bool used = id.empty() || count("\"" + id + "\"") > 1 || (!nm.empty() && count(nm) > 1);
            if (used) keep.push_back(it); else removed++;
        }
        doc[key] = keep;
    }
    if (removed) fprintf(stderr, "Source 2D: exportacion optimizada, %d recurso(s) sin usar fuera\n", removed);
    return doc;
}
static void exportDialog(const std::string &os) {
    std::string nm = app.proj.name(); for (char &c : nm) if (c == ' ') c = '_';
    bool win = os == "windows", web = os == "html";
    app.fileDlg.show(ui::FileDialog::Save, web ? "Exportar para la web" : win ? "Exportar para Windows" : "Exportar para Linux", "", projectsDir(),
                     nm + (web ? ".zip" : win ? ".exe" : ".x86_64"),
                     [os](const std::string &f) { exportGame(f, os, app.liteExport); });
}

// la escena que se uso por ultimo o, si no hay, la primera del proyecto
static void focusScene() {
    const json &scs = app.proj.doc["scenes"];
    std::string id = ui::lastFocusKey("scene");
    bool exists = false;
    for (auto &sc : scs) if (jstr(sc, "id") == id) exists = true;
    if (!exists && !scs.empty()) id = jstr(scs[0], "id");
    if (!id.empty()) openScene(id);
}

static void drawMenus() {
    bool p = app.hasProject;
    bool menuArchivo = ImGui::BeginMenu("Archivo");
    school::anchor("menu.archivo");
    if (menuArchivo) {
        school::note("archivo");
        if (ImGui::MenuItem("Cerrar proyecto", "Ctrl+W", false, app.hasProject)) closeProject();
        if (ImGui::MenuItem("Abrir...", "Ctrl+O")) openDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Guardar", "Ctrl+S", false, p)) save();
        if (ImGui::MenuItem("Guardar como...", "Ctrl+Mayús+S", false, p)) saveAs();
        if (ImGui::MenuItem("Versiones del proyecto...", nullptr, false, p && !app.proj.path.empty())) app.showVersions = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Opciones...")) app.showOptions = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exportar...", nullptr, false, p)) app.showExport = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Salir")) app.askClose = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editar", p)) {
        if (ImGui::MenuItem("Deshacer", "Ctrl+Z")) ui::requestHistory(-1);
        if (ImGui::MenuItem("Rehacer", "Ctrl+Y")) ui::requestHistory(1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver", p)) {
        if (ImGui::MenuItem("Inicio")) app.focusWelcome = true;
        if (ImGui::MenuItem("Escena")) focusScene();
        if (ImGui::MenuItem("Eventos")) openEvents();
        if (ImGui::MenuItem("Ajustes del proyecto")) { openDoc(app.settings); school::note("settings"); }
        if (ImGui::MenuItem("Shaders")) openDoc(app.shaders);
        if (ImGui::MenuItem("Extensiones y plugins")) openDoc(app.extensions);
        ImGui::Separator();
        ImGui::MenuItem("Panel del proyecto", nullptr, &app.showTree);
        if (ImGui::MenuItem("Restablecer paneles")) { app.showTree = true; app.wantLayout = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ejecutar", p)) {
        if (ImGui::MenuItem("Vista previa", "F5")) playProject();
        if (ImGui::MenuItem("Exportar...", nullptr, false, true)) app.showExport = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        ImGui::MenuItem("Source 2D - editor nativo", nullptr, false, false);
        ImGui::MenuItem("Editor de sprites: B lápiz, E goma, G cubo, I cuentagotas,", nullptr, false, false);
        ImGui::MenuItem("L línea, U rectángulo, O elipse, M selección, V mover, H mano", nullptr, false, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Licencias de terceros...")) app.showLicenses = true;
        ImGui::EndMenu();
    }
}

/* ---------------- pestanas de documentos (como las ClosableTabs de GDevelop) ---------------- */
// Las pestanas de acoplamiento de ImGui van ocultas: esta fila las sustituye y cada pestana enfoca
// o cierra su ventana con los mismos open/focusMe de siempre.
struct DocTab { std::string win, label; ui::Icon icon; bool *open; bool *focus; std::string key; };   // key: documento para trabajar juntos

static std::vector<DocTab> collectTabs() {
    std::vector<DocTab> t;
    auto add = [&](const std::string &win, ui::Icon ic, bool *open, bool *focus, const std::string &key = "") {
        std::string l = win.substr(0, win.find("###"));
        size_t c = l.find(": ");                       // "Escena: Nivel 1" -> "Nivel 1" (el icono ya dice el tipo)
        if (c != std::string::npos) l = l.substr(c + 2);
        t.push_back({win, l, ic, open, focus, key});
    };
    for (auto &e : app.sheets) if (e->open) add(e->title(app.proj), ui::Icon::Events, &e->open, &e->focusMe, "events:" + e->sceneId);
    for (auto &e : app.scenes) add(e->title(app.proj), ui::Icon::Scene, &e->open, &e->focusMe, "scene:" + e->sceneId);
    // la escena y sus eventos se llaman igual: el nombre dice cual es cual ("Nivel 1 · Eventos", "Nivel 1 · Escena")
    for (auto &d : t) {
        if (d.key.rfind("events:", 0) == 0 && !d.label.empty() && d.label != "Eventos comunes") d.label += " · Eventos";
        else if (d.key.rfind("scene:", 0) == 0) d.label += " · Escena";
    }
    for (auto &e : app.objects) add(e->title(app.proj), ui::Icon::Object, &e->open, &e->focusMe);
    for (auto &e : app.sprites) add("Sprite: " + e->name + "###spr_" + e->spriteId, ui::Icon::Image, &e->open, &e->focusMe, "sprite:" + e->spriteId);
    for (auto &e : app.tilesets) add(e->title(app.proj), ui::Icon::Grid, &e->open, &e->focusMe);
    for (auto &e : app.fonts) add(e->title(app.proj), ui::Icon::Pencil, &e->open, &e->focusMe);
    if (app.settings && app.settings->open) add("Ajustes del proyecto###settings", ui::Icon::Settings, &app.settings->open, &app.settings->focusMe);
    if (app.shaders && app.shaders->open) add("Shaders###shaders", ui::Icon::Eye, &app.shaders->open, &app.shaders->focusMe);
    if (app.extensions && app.extensions->open) add("Extensiones###extensions", ui::Icon::Plus, &app.extensions->open, &app.extensions->focusMe);
    if (app.layersDoc && app.layersDoc->open) add("Capas###layers", ui::Icon::Onion, &app.layersDoc->open, &app.layersDoc->focusMe);
    for (auto &e : app.sounds) if (e->open) add(e->title(app.proj), ui::Icon::Bell, &e->open, &e->focusMe);
    if (app.autotiles && app.autotiles->open) add("Autotiles###autotiles", ui::Icon::Grid, &app.autotiles->open, &app.autotiles->focusMe);
    // en el orden en que se abrieron, como en GDevelop
    for (auto &d : t) if (std::find(app.tabOrder.begin(), app.tabOrder.end(), d.win) == app.tabOrder.end()) app.tabOrder.push_back(d.win);
    app.tabOrder.erase(std::remove_if(app.tabOrder.begin(), app.tabOrder.end(), [&](const std::string &w) {
        return std::none_of(t.begin(), t.end(), [&](const DocTab &d) { return d.win == w; }); }), app.tabOrder.end());
    auto pos = [](const std::string &w) { return std::find(app.tabOrder.begin(), app.tabOrder.end(), w) - app.tabOrder.begin(); };
    std::stable_sort(t.begin(), t.end(), [&](const DocTab &a, const DocTab &b) { return pos(a.win) < pos(b.win); });
    t.insert(t.begin(), DocTab{"Inicio", "", ui::Icon::Home, nullptr, &app.focusWelcome, ""});
    return t;
}

// la pestana activa es la ventana que tiene el foco (o, al empezar, la visible en el centro)
static void updateActiveTab(const std::vector<DocTab> &tabs) {
    auto known = [&](const char *n) { return std::any_of(tabs.begin(), tabs.end(), [&](const DocTab &d) { return d.win == n; }); };
    ImGuiWindow *nw = GImGui->NavWindow;
    if (nw && nw->RootWindow && known(nw->RootWindow->Name)) app.activeTab = nw->RootWindow->Name;
    else if (std::none_of(tabs.begin(), tabs.end(), [](const DocTab &d) { return d.win == app.activeTab; })) {
        ImGuiDockNode *n = app.dockCenter ? ImGui::DockBuilderGetNode(app.dockCenter) : nullptr;
        app.activeTab = n && n->VisibleWindow ? n->VisibleWindow->Name : "Inicio";
    }
    // un nodo sin barra de pestanas solo muestra su primera ventana: la activa pasa al principio
    ImGuiWindow *w = ImGui::FindWindowByName(app.activeTab.c_str());
    ImGuiDockNode *n = w ? w->DockNode : nullptr;
    if (n && n->Windows.Size > 1 && n->Windows[0] != w) {
        n->Windows.erase(n->Windows.find(w));
        n->Windows.push_front(w);
    }
}

// dibuja una pestana; devuelve 1 si se pulso, 2 si se pidio cerrarla
static int docTab(const DocTab &d, bool active, float maxW, float h) {
    const float padL = 10, iconW = 16, gap = 7, closeW = d.open ? 22 : 0, padR = d.open ? 4 : 10;
    float labelW = d.label.empty() ? 0 : ImGui::CalcTextSize(d.label.c_str()).x;
    float fixed = padL + iconW + (labelW > 0 ? gap : 0) + closeW + padR;
    labelW = std::min(labelW, std::max(0.0f, maxW - fixed));
    ImVec2 p = ImGui::GetCursorScreenPos(), sz(fixed + labelW, h);
    ImGui::PushID(d.win.c_str());
    ImGui::InvisibleButton("##tab", sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hov = ImGui::IsItemHovered();
    int r = 0;
    ImVec2 e(p.x + sz.x, p.y + sz.y), cc(e.x - padR - closeW * 0.5f, p.y + h * 0.5f);
    bool overClose = d.open && hov && std::fabs(ImGui::GetIO().MousePos.x - cc.x) < 10 && std::fabs(ImGui::GetIO().MousePos.y - cc.y) < 10;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) r = overClose ? 2 : 1;
    if (d.open && ImGui::IsItemClicked(ImGuiMouseButton_Middle)) r = 2;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 bg = active ? ui::kPanel : hov ? IM_COL32(0x3A, 0x3A, 0x44, 255) : ui::kPanel2;
    dl->AddRectFilled(p, e, bg, 8, ImDrawFlags_RoundCornersTop);
    if (active) {
        dl->PathLineTo(ImVec2(p.x + 0.5f, e.y));
        dl->PathArcTo(ImVec2(p.x + 8.5f, p.y + 8.5f), 8, 3.14159265f, 4.71238898f);
        dl->PathArcTo(ImVec2(e.x - 8.5f, p.y + 8.5f), 8, 4.71238898f, 6.28318531f);
        dl->PathLineTo(ImVec2(e.x - 0.5f, e.y));
        dl->PathStroke(ui::kTabBorder, 0, 1.0f);
    }
    ImU32 tc = active ? ui::kText : ui::kText2;
    ui::drawIcon(dl, d.icon, ImVec2(p.x + padL + iconW * 0.5f, p.y + h * 0.5f), 14, tc);
    if (labelW > 0) {
        float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f, tx = p.x + padL + iconW + gap;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tc));
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, ty), ImVec2(tx + labelW, ty + h), tx + labelW, d.label.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    if (d.open) {
        if (overClose) dl->AddCircleFilled(cc, 9, ui::kHover);
        float k = 3.5f;
        dl->AddLine(ImVec2(cc.x - k, cc.y - k), ImVec2(cc.x + k, cc.y + k), tc, 1.4f);
        dl->AddLine(ImVec2(cc.x - k, cc.y + k), ImVec2(cc.x + k, cc.y - k), tc, 1.4f);
    }
    if (hov && labelW > 0 && labelW < ImGui::CalcTextSize(d.label.c_str()).x && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", d.label.c_str());
    ImGui::PopID();
    return r;
}

static void beginBar(const char *id, float h, ImU32 bg, float padY) {
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(bg));
    ImGui::BeginViewportSideBar(id, ImGui::GetMainViewport(), ImGuiDir_Up, h, fl);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

static void drawTabsBar() {
    const float barH = 40, tabH = 30;
    beginBar("##tabsbar", barH, ui::kWindowBg, 0);
    std::vector<DocTab> tabs = collectTabs();
    updateActiveTab(tabs);
    ImGui::SetCursorPosY((barH - 32) * 0.5f);
    if (ui::iconButton("tree", ui::Icon::Menu, app.showTree, "Gestor del proyecto", 32)) app.showTree = !app.showTree;
    school::anchor("menu.proyecto");
    app.treeBtnMin = ImGui::GetItemRectMin(); app.treeBtnMax = ImGui::GetItemRectMax();   // su clic no cierra el cajon "por fuera"
    ImGui::SameLine(0, 8);
    ImVec2 tabsA = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x - 8;
    school::anchorRect("tabs", tabsA, ImVec2(tabsA.x + std::min(avail, 520.0f), tabsA.y + 36));
    float maxW = std::clamp((avail - 50) / std::max<size_t>(1, tabs.size() - 1) - 2, 70.0f, 200.0f);
    for (auto &d : tabs) {
        ImGui::SetCursorPosY(barH - tabH);
        int r = docTab(d, d.win == app.activeTab, maxW, tabH);
        if (d.key.rfind("scene:", 0) == 0) school::anchor("tab.scene");
        else if (d.key.rfind("events:", 0) == 0) { school::anchor("tab.events"); if (d.win == app.activeTab) school::note("events"); }
        else if (d.win.rfind("Inicio", 0) == 0) school::anchor("tab.home");
        if (!d.key.empty()) peerDots(d.key, ImVec2(ImGui::GetItemRectMax().x - 4, ImGui::GetItemRectMin().y + 5), 3.5f);
        if (r == 1) { *d.focus = true; app.activeTab = d.win; }
        if (r == 2 && d.open) *d.open = false;
        ImGui::SameLine();
    }
    ImGui::End();
}

// barra de herramientas principal (MainFrame/Toolbar de GDevelop): guardar e historial a la izquierda,
// vista previa y publicar en el centro
static void drawToolbar() {
    const float barH = 44, btnH = 32;
    beginBar("##toolbar", barH, ui::kPanel, (barH - btnH) * 0.5f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(wp.x, wp.y + ws.y - 1), ImVec2(wp.x + ws.x, wp.y + ws.y - 1), ui::kLine);
    ImVec2 sp = ImGui::GetCursorScreenPos();
    if (ui::iconButton("save", ui::Icon::Save, false, "Guardar (Ctrl+S)", btnH)) save();
    school::anchor("toolbar.save");
    if (app.proj.dirty) dl->AddCircleFilled(ImVec2(sp.x + btnH - 7, sp.y + 7), 3.5f, ui::kSelectedIconBg);
    ImGui::SameLine();
    ImVec2 undoA = ImGui::GetCursorScreenPos();
    if (ui::iconButton("undo", ui::Icon::Undo, false, "Deshacer (Ctrl+Z)", btnH)) ui::requestHistory(-1);
    ImGui::SameLine();
    if (ui::iconButton("redo", ui::Icon::Redo, false, "Rehacer (Ctrl+Y)", btnH)) ui::requestHistory(1);
    school::anchorRect("toolbar.undo", undoA, ImGui::GetItemRectMax());
    ImGui::SameLine();
    bool inSession = collab::role() != collab::Role::None;
    if (ui::iconButton("collab", ui::Icon::People, inSession, inSession ? collab::status().c_str() : "Trabajar juntos: compartir el proyecto o unirse a una sesión", btnH)) {
        app.collabTab = 0; app.showCollab = true;
    }
    school::anchor("toolbar.collab");
    // quienes estan en la sesion: un circulo con la inicial y, al pasar el raton, que tienen abierto
    for (auto &p : collab::peers()) {
        ImGui::SameLine(0, 4);
        ImVec2 c = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(("##peer" + std::to_string(p.id)).c_str(), ImVec2(btnH, btnH));
        ImVec2 ctr(c.x + btnH * 0.5f, c.y + btnH * 0.5f);
        dl->AddCircleFilled(ctr, btnH * 0.38f, p.color);
        char ini[8] = {0};
        if (!p.name.empty()) { size_t n = 1; while (n < p.name.size() && (p.name[n] & 0xC0) == 0x80) n++; std::memcpy(ini, p.name.data(), std::min<size_t>(n, 4)); }
        ImVec2 ts = ImGui::CalcTextSize(ini);
        dl->AddText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f), IM_COL32(20, 20, 26, 255), ini);
        if (ImGui::IsItemHovered()) {
            std::string where = p.doc;
            if (where.rfind("scene:", 0) == 0) { json *sc = findById(app.proj.doc["scenes"], where.substr(6)); where = "en la escena " + (sc ? jstr(*sc, "name") : where.substr(6)); }
            else if (where.rfind("events:", 0) == 0) { json *sc = findById(app.proj.doc["scenes"], where.substr(7)); where = sc ? "en los eventos de " + jstr(*sc, "name") : "en los eventos comunes"; }
            else if (where.rfind("sprite:", 0) == 0) { json *sp = app.proj.spriteById(where.substr(7)); where = "editando el sprite " + (sp ? jstr(*sp, "name") : where.substr(7)); }
            ImGui::SetTooltip("%s%s%s", p.name.c_str(), where.empty() ? "" : "  -  ", where.c_str());
        }
    }

    // vista previa (boton plano) y publicar (boton primario), centrados
    ImFont *bold = ui::boldFont();
    float fs = ImGui::GetFontSize();
    const char *playL = "Vista previa", *pubL = "Exportar";
    float playW = 12 + 16 + 8 + bold->CalcTextSizeA(fs, FLT_MAX, 0, playL).x + 14;
    float pubW = 12 + 16 + 8 + bold->CalcTextSizeA(fs, FLT_MAX, 0, pubL).x + 14;
    float x = std::max(ImGui::GetItemRectMax().x - wp.x + 16, (ws.x - playW - 8 - pubW) * 0.5f);
    auto bigButton = [&](const char *id, const char *label, ui::Icon ic, float w, bool filled, const char *tip) {
        ImGui::SameLine(x);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool r = ImGui::InvisibleButton(id, ImVec2(w, btnH));
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImU32 bg = filled ? (act ? ui::kPrimaryDark : hov ? ui::kPrimaryLight : ui::kPrimary) : (hov ? ui::kHover : 0);
        if (bg) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + btnH), bg, 8);
        ImU32 fg = filled ? ui::kText : ui::kSecondary;
        ui::drawIcon(dl, ic, ImVec2(p.x + 12 + 8, p.y + btnH * 0.5f), 14, fg);
        dl->AddText(bold, fs, ImVec2(p.x + 12 + 16 + 8, p.y + (btnH - fs) * 0.5f), fg, label);
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
        x += w + 8;
        return r;
    };
    bool playHit = bigButton("##play", playL, ui::Icon::Play, playW, false, "Probar el juego (F5)");
    school::anchor("toolbar.preview");
    if (playHit) playProject();
    bool pubHit = bigButton("##publish", pubL, ui::Icon::Export, pubW, true, "Exportar el juego para Windows, Linux o la web");
    school::anchor("toolbar.export");
    if (pubHit) app.showExport = true;

    // a la derecha: nombre del proyecto
    std::string st = app.proj.name() + (app.proj.dirty ? "  (sin guardar)" : "");
    float stW = ImGui::CalcTextSize(st.c_str()).x;
    if (ws.x - 14 - stW > x + 16) {
        ImGui::SameLine(ws.x - 14 - stW);
        ImGui::SetCursorPosY((barH - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextDisabled("%s", st.c_str());
    }
    ImGui::End();
}

static void drawMenuAndToolbar() {
    if (ImGui::BeginMainMenuBar()) {
        drawMenus();
        ImGui::EndMainMenuBar();
    }
    if (app.hasProject && !g_studio.on && !g_sound.on) { drawTabsBar(); drawToolbar(); }
}

static void buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock;     // el gestor del proyecto es un cajon por encima: todo el ancho es para los documentos
    ImGui::DockBuilderDockWindow("Inicio", center);
    for (auto &e : app.sprites) ImGui::DockBuilderDockWindow(("Sprite: " + e->name + "###spr_" + e->spriteId).c_str(), center);
    for (auto &e : app.objects) ImGui::DockBuilderDockWindow(e->title(app.proj).c_str(), center);
    for (auto &e : app.scenes) ImGui::DockBuilderDockWindow(e->title(app.proj).c_str(), center);
    for (auto &e : app.sheets) ImGui::DockBuilderDockWindow(e->title(app.proj).c_str(), center);
    for (auto &e : app.tilesets) ImGui::DockBuilderDockWindow(e->title(app.proj).c_str(), center);
    for (auto &e : app.fonts) ImGui::DockBuilderDockWindow(e->title(app.proj).c_str(), center);
    for (const char *w : {"Ajustes del proyecto###settings", "Shaders###shaders", "Extensiones###extensions"}) ImGui::DockBuilderDockWindow(w, center);
    // sin las pestanas de ImGui: las sustituye la fila de pestanas de arriba
    ImGui::DockBuilderGetNode(center)->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dock);
    app.dockCenter = center;
}

/* ---------------- trabajar juntos (collab.hpp) ---------------- */
// cada fotograma: manda y recibe cambios y presencia, y pone el editor al dia con lo que llega
static void collabTick() {
    collab::Events ev;
    if (app.hasProject) syncEditors();
    if (!app.scriptPresDoc.empty()) collab::setPresence(app.scriptPresDoc, app.scriptPresX, app.scriptPresY, true, {});
    collab::update(app.proj, ev);
    if (!ev.notice.empty()) notify(ev.notice, ev.noticeErr);
    static bool adopted = false;                         // cliente: el proyecto de la sesion ya se abrio en el editor
    if (collab::role() != collab::Role::Client) adopted = false;
    if (ev.docReplaced) {
        bool first = !adopted;                            // al entrar se abre de cero; al resincronizar solo se recarga
        adopted = true;
        if (first) {
            app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.tilesets.clear(); app.fonts.clear(); app.sounds.clear();
            app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset();
            musicPreview(app.proj, "");
            app.hasProject = true; app.showTree = false;
            app.openFirstScene = true; app.wantLayout = true;
        }
        loadPaletteFrom(app.proj.doc);
        ev.sprites.insert("*");
        ev.textures = true;
    }
    if (ev.changed && collab::role() == collab::Role::Host) app.proj.dirty = true;   // el guardado automatico guarda lo de todos
    if (ev.textures) app.proj.rev++;
    for (auto &e : app.sprites) {
        if (e->dirtyForProject || (!ev.sprites.count("*") && !ev.sprites.count(e->spriteId))) continue;
        if (json *sp = app.proj.spriteById(e->spriteId)) e->reload(*sp);
        else e->open = false;                                  // alguien lo borro
    }
    collab::defaultPresence(app.hasProject ? app.activeTab : "");
}

static const char *const kCollabModes[] = { "Misma red WiFi", "Zona WiFi", "Tailscale", "Bluetooth" };
static const char *const kCollabKinds[] = { "WiFi", "Zona WiFi", "Tailscale", "Bluetooth" };
static void collabModeHelp(int mode, bool hosting) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
    switch (mode) {
    case 0:
        ImGui::TextUnformatted("Las PCs conectadas al mismo router (o por cable a la misma red). La sesión aparece sola en la otra PC, en \"Unirse\".");
        break;
    case 1:
        ImGui::TextUnformatted("Una PC crea su propia red WiFi y las demás se conectan a ella: no hace falta router ni internet y es mucho más rápido que Bluetooth. "
                               "Mientras la zona está encendida, esa PC puede quedarse sin su WiFi de siempre.");
        if (hosting) {
#ifdef _WIN32
            if (ImGui::Button("Abrir la zona con cobertura de Windows")) { std::string a, b; collab::hotspotStart(a, b, app.collabMsg); app.collabMsgErr = false; }
#else
            if (ImGui::Button("Crear zona WiFi")) { std::string a, b; app.collabMsgErr = !collab::hotspotStart(a, b, app.collabMsg); }
            ImGui::SameLine();
            if (ImGui::Button("Apagar zona WiFi")) { collab::hotspotStop(app.collabMsg); app.collabMsgErr = false; }
#endif
        } else ImGui::TextUnformatted("Conecta esta PC a la red WiFi que creó el anfitrión; después la sesión aparece abajo.");
        break;
    case 2:
        ImGui::TextUnformatted("Para trabajar a distancia, cada uno en su casa. Instala Tailscale (gratis para uso personal) en las PCs y entra con la misma cuenta "
                               "(o comparte el equipo con la otra persona). Después únete con la dirección 100.x.x.x o el nombre del anfitrión en Tailscale.");
        break;
    case 3:
        ImGui::TextUnformatted("Empareja las PCs y activa la red por Bluetooth del sistema: en Windows, Configuración > Bluetooth > Dispositivos > "
                               "\"Unirse a una red de área personal\"; en Linux, la opción Red/Acceso a la red del dispositivo. Después funciona como la WiFi. "
                               "Es lento (1-2 Mbps): un proyecto grande tarda en llegar.");
        break;
    }
    ImGui::PopTextWrapPos();
}

static void drawCollabDialog() {
    if (app.showCollab) { ImGui::OpenPopup("Trabajar juntos"); app.showCollab = false; }
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    if (!ui::beginModal("Trabajar juntos", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { collab::discover(false); return; }
    collab::Role role = collab::role();
    auto peerList = [] {
        for (auto &p : collab::peers()) {
            ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(c.x + 7, c.y + ImGui::GetTextLineHeight() * 0.5f), 6, p.color);
            ImGui::Dummy(ImVec2(18, 0)); ImGui::SameLine();
            ImGui::Text("%s", p.name.c_str());
        }
    };
    if (role == collab::Role::None) {
        // quien soy
        char nm[48]; snprintf(nm, sizeof nm, "%s", prefs().name.c_str());
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tu nombre"); ImGui::SameLine(110);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("##nombre", nm, sizeof nm)) { prefs().name = nm; savePrefs(); }
        ImGui::SameLine();
        for (int i = 0; i < 8; i++) {
            ImGui::PushID(i);
            ImVec2 c = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##col", ImVec2(22, 22))) { prefs().color = i; savePrefs(); }
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(c.x + 11, c.y + 11), 8, collab::colorFor(i));
            if (prefs().color == i) ImGui::GetWindowDrawList()->AddCircle(ImVec2(c.x + 11, c.y + 11), 10.5f, IM_COL32_WHITE, 0, 2);
            ImGui::PopID();
            ImGui::SameLine(0, 2);
        }
        ImGui::NewLine();
        ImGui::Separator();
        if (ImGui::BeginTabBar("##collabtabs")) {
            if (ImGui::BeginTabItem("Compartir este proyecto", nullptr, app.collabTab == 0 && ImGui::IsWindowAppearing() ? ImGuiTabItemFlags_SetSelected : 0)) {
                collab::discover(false);
                if (!app.hasProject) ImGui::TextDisabled("Abre o crea un proyecto para compartirlo.");
                else {
                    ImGui::TextWrapped("Las demás personas verán y editarán \"%s\" contigo en tiempo real, con sus cursores y lo que seleccionan.", app.proj.name().c_str());
                    ImGui::Dummy(ImVec2(0, 4));
                    for (int m = 0; m < 4; m++) { if (m) ImGui::SameLine(); ImGui::RadioButton(kCollabModes[m], &app.collabMode, m); }
                    collabModeHelp(app.collabMode, true);
                    ImGui::Dummy(ImVec2(0, 6));
                    if (ui::primaryButton("Empezar a compartir", ImVec2(190, 0))) {
                        std::string err;
                        if (collab::host(app.proj, prefs().name, collab::colorFor(prefs().color), err)) { app.collabMsg.clear(); notify("Compartiendo el proyecto. Código: " + collab::code()); }
                        else { app.collabMsg = "No se pudo compartir: " + err; app.collabMsgErr = true; }
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Unirse a una sesión", nullptr, app.collabTab == 1 && ImGui::IsWindowAppearing() ? ImGuiTabItemFlags_SetSelected : 0)) {
                collab::discover(true);
                for (int m = 0; m < 4; m++) { if (m) ImGui::SameLine(); ImGui::RadioButton(kCollabModes[m], &app.collabMode, m); }
                collabModeHelp(app.collabMode, false);
                ImGui::Dummy(ImVec2(0, 6));
                auto f = collab::found();
                if (f.empty()) ImGui::TextDisabled(app.collabMode >= 2 ? "Con Tailscale o Bluetooth escribe la dirección del anfitrión abajo." : "Buscando sesiones en esta red...");
                for (auto &e : f) {
                    ImGui::PushID(e.addr.c_str());
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s  -  %s  (%s)", e.project.c_str(), e.host.c_str(), e.addr.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Elegir")) snprintf(app.joinAddr, sizeof app.joinAddr, "%s:%d", e.addr.c_str(), e.port);
                    ImGui::PopID();
                }
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Dirección"); ImGui::SameLine(110);
                ImGui::SetNextItemWidth(240); ImGui::InputTextWithHint("##addr", "IP o nombre (Tailscale)", app.joinAddr, sizeof app.joinAddr);
                ImGui::SameLine(); ImGui::TextUnformatted("Código"); ImGui::SameLine();
                ImGui::SetNextItemWidth(90); ImGui::InputText("##code", app.joinCode, sizeof app.joinCode, ImGuiInputTextFlags_CharsDecimal);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
                ImGui::TextDisabled("El proyecto del anfitrión puede ejecutar código en tu PC al probarlo: únete solo a personas de confianza.");
                ImGui::PopTextWrapPos();
                if (ui::primaryButton("Unirse", ImVec2(140, 0))) {
                    std::string addr = app.joinAddr, cd = app.joinCode;
                    guardSwitch([addr, cd] {
                        std::string err;
                        if (collab::join(addr, cd, prefs().name, collab::colorFor(prefs().color), err)) { app.collabMsg.clear(); }
                        else { app.collabMsg = err; app.collabMsgErr = true; app.showCollab = true; }
                    });
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else if (role == collab::Role::Host) {
        collab::discover(false);
        ImGui::PushFont(ui::boldFont(), 28);
        ImGui::Text("Código: %s", collab::code().c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("Las demás personas lo escriben al unirse.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Direcciones de esta PC:");
        auto addrs = collab::localAddrs();
        if (addrs.empty()) ImGui::TextDisabled("  (sin red: conecta esta PC a una WiFi, una zona WiFi, Tailscale o Bluetooth)");
        for (auto &a : addrs) {
            bool match = a.kind == kCollabKinds[app.collabMode] || (app.collabMode == 0 && a.kind == "Cable");
            ImGui::TextColored(match ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.68f, 1), "  %s   %s (%s)", a.ip.c_str(), a.kind.c_str(), a.iface.c_str());
        }
        ImGui::Dummy(ImVec2(0, 4));
        for (int m = 0; m < 4; m++) { if (m) ImGui::SameLine(); ImGui::RadioButton(kCollabModes[m], &app.collabMode, m); }
        collabModeHelp(app.collabMode, true);
        static std::string fw = collab::firewallHint();
        if (!fw.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
            ImGui::TextColored(ImVec4(1, 0.78f, 0.35f, 1), "%s", fw.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton("Copiar la orden")) ImGui::SetClipboardText(fw.substr(fw.find("sudo")).c_str());
        }
        ImGui::Separator();
        ImGui::Text("%s", collab::status().c_str());
        peerList();
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Dejar de compartir", ImVec2(170, 0))) { collab::stop(); notify("Ya no compartes el proyecto"); }
    } else {
        ImGui::Text("%s", collab::status().c_str());
        peerList();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Al salir, el proyecto sigue abierto como una copia sin guardar.");
        if (ImGui::Button("Salir de la sesión", ImVec2(170, 0))) { collab::stop(); notify("Saliste de la sesión"); }
    }
    if (!app.collabMsg.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
        ImGui::TextColored(app.collabMsgErr ? ImVec4(1, 0.45f, 0.45f, 1) : ImVec4(0.5f, 0.9f, 0.6f, 1), "%s", app.collabMsg.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Cerrar", ImVec2(110, 0))) { collab::discover(false); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

static void drawModals() {
    drawCollabDialog();
    if (app.showNewSprite) { ImGui::OpenPopup("Nuevo sprite"); app.showNewSprite = false; }
    if (ui::beginModal("Nuevo sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Nombre", app.nsName, sizeof app.nsName);
        ImGui::InputInt("Ancho", &app.nsW); ImGui::InputInt("Alto", &app.nsH);
        if (ImGui::Button("16x16")) app.nsW = app.nsH = 16;
        ImGui::SameLine();
        if (ImGui::Button("32x32")) app.nsW = app.nsH = 32;
        ImGui::SameLine();
        if (ImGui::Button("64x64")) app.nsW = app.nsH = 64;
        ImGui::Separator();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ui::primaryButton("Crear", ImVec2(120, 0))) { openSprite(newSprite(app.nsName, app.nsW, app.nsH)); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (app.showNewObject) { ImGui::OpenPopup("Nuevo objeto"); app.showNewObject = false; }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (ui::beginModal("Nuevo objeto", nullptr)) {
        static const char *kinds[][3] = {
            {"sprite", "Sprite", "Objeto con imagen o animaciones: personajes, enemigos, monedas..."},
            {"text", "Texto", "Texto en pantalla: puntuación, mensajes, diálogos."},
            {"button", "Botón", "Botón clicable con texto y colores."},
            {"tile", "Tile", "Un mapa de tiles que pintas en la escena: crece solo, se mueve y choca. Admite autotile."},
            {"light", "Luz", "Ilumina la oscuridad de las capas: antorchas, lámparas, linternas..."},
            {"group", "Grupo", "Varios objetos juntos (Enemigos = Murciélago + Slime): un evento sirve para todos."}};
        for (auto &k : kinds) {
            ImGui::PushID(k[0]);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool kHit = ImGui::Button("##k", ImVec2(-1, 52));
            school::anchor((std::string("newobj.") + k[0]).c_str());
            if (kHit) {
                std::string id = createObject(app.proj, k[0]);
                if (json *no = findById(app.proj.doc["objects"], id)) (*no)["scene"] = currentScene();   // el objeto es de la escena actual
                for (auto &e : app.scenes) { if (!strcmp(k[0], "tile")) { e->paintObj = id; e->mode = SceneEditor::Tiles; } else e->placeObj = id; }
                openObject(id);
                ImGui::CloseCurrentPopup();
            }
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 12, p.y + 7), IM_COL32(235, 235, 242, 255), k[1]);
            dl->AddText(ImVec2(p.x + 12, p.y + 27), ui::kDim, k[2]);
            ImGui::PopID();
        }
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!app.pendingDeleteObj.empty()) ImGui::OpenPopup("Eliminar objeto");
    if (ui::beginModal("Eliminar objeto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int uses = objectUses(app.pendingDeleteObj);
        ImGui::Text("Se eliminará el objeto y todas sus instancias en las escenas.");
        if (uses) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Aparece %d vez/veces en escenas o eventos.", uses);
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) { app.pendingDeleteObj.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Eliminar", ImVec2(120, 0))) {
            std::string id = app.pendingDeleteObj;
            json &objs = app.proj.doc["objects"];
            for (size_t i = 0; i < objs.size(); i++) if (jstr(objs[i], "id") == id) { objs.erase(objs.begin() + i); break; }
            for (auto &sc : app.proj.doc["scenes"]) if (sc.contains("instances")) {
                json &arr = sc["instances"];
                for (size_t i = arr.size(); i-- > 0;) if (jstr(arr[i], "objectId") == id) arr.erase(arr.begin() + i);
            }
            app.objects.erase(std::remove_if(app.objects.begin(), app.objects.end(), [&](auto &e) { return e->objectId == id; }), app.objects.end());
            app.proj.dirty = true; app.pendingDeleteObj.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!app.pendingDeleteScene.empty()) ImGui::OpenPopup("Eliminar escena");
    if (ui::beginModal("Eliminar escena", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Se eliminará la escena con sus objetos, eventos, instancias y mapas de tiles.");
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) { app.pendingDeleteScene.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Eliminar", ImVec2(120, 0))) {
            std::string id = app.pendingDeleteScene;
            json &scs = app.proj.doc["scenes"];
            for (size_t i = 0; i < scs.size(); i++) if (jstr(scs[i], "id") == id) { scs.erase(scs.begin() + i); break; }
            app.scenes.erase(std::remove_if(app.scenes.begin(), app.scenes.end(), [&](auto &e) { return e->sceneId == id; }), app.scenes.end());
            app.sheets.erase(std::remove_if(app.sheets.begin(), app.sheets.end(), [&](auto &e) { return e->sceneId == id; }), app.sheets.end());
            json &objs = app.proj.doc["objects"];   // los objetos de la escena se van con ella
            for (size_t i = objs.size(); i-- > 0;) if (jstr(objs[i], "scene") == id) {
                std::string oid = jstr(objs[i], "id");
                app.objects.erase(std::remove_if(app.objects.begin(), app.objects.end(), [&](auto &e) { return e->objectId == oid; }), app.objects.end());
                objs.erase(objs.begin() + i);
            }
            app.proj.dirty = true; app.proj.rev++; app.pendingDeleteScene.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    app.importer.draw(app.proj, [](const std::string &id, bool tiles) {
        if (tiles && !app.importTileObj.empty()) if (json *o = findById(app.proj.doc["objects"], app.importTileObj)) { (*o)["tilesetId"] = id; app.proj.rev++; }
        app.importTileObj.clear();
        if (tiles) openTileset(id); else openSprite(id);
        notify("Imagen importada");
    });
    if (!app.pendingDeleteMusic.empty()) {
        json &mus = app.proj.doc["music"];
        if (musicPlaying() == app.pendingDeleteMusic) musicPreview(app.proj, "");
        for (size_t i = 0; i < mus.size(); i++) if (jstr(mus[i], "id") == app.pendingDeleteMusic) { mus.erase(mus.begin() + i); break; }
        app.sounds.erase(std::remove_if(app.sounds.begin(), app.sounds.end(), [](auto &e) { return e->musicId == app.pendingDeleteMusic; }), app.sounds.end());
        app.pendingDeleteMusic.clear(); app.proj.dirty = true;
    }
    if (!app.pendingDeleteFont.empty()) {
        std::string id = app.pendingDeleteFont;
        json &fts = app.proj.doc["fonts"];
        for (size_t i = 0; i < fts.size(); i++) if (jstr(fts[i], "id") == id) { fts.erase(fts.begin() + i); break; }
        for (auto &o : app.proj.doc["objects"]) for (const char *k : {"text", "button"}) if (o.contains(k) && jstr(o[k], "font") == id) o[k].erase("font");
        app.fonts.erase(std::remove_if(app.fonts.begin(), app.fonts.end(), [&](auto &e) { return e->fontId == id; }), app.fonts.end());
        app.proj.dirty = true; app.proj.rev++; app.pendingDeleteFont.clear();
        notify("Fuente eliminada: los textos que la usaban vuelven a la del juego");
    }
    if (!app.pendingDeleteTileset.empty()) ImGui::OpenPopup("Eliminar conjunto de tiles");
    if (ui::beginModal("Eliminar conjunto de tiles", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int uses = 0;
        for (auto &o : app.proj.doc["objects"]) if (jstr(o, "type") == "tile" && jstr(o, "tilesetId") == app.pendingDeleteTileset) uses++;
        ImGui::Text("Se eliminará el conjunto de tiles.");
        if (uses) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Lo usan %d objeto(s) Tile; sus mapas dejarán de verse.", uses);
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) { app.pendingDeleteTileset.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Eliminar", ImVec2(120, 0))) {
            json &tss = app.proj.doc["tilesets"];
            for (size_t i = 0; i < tss.size(); i++) if (jstr(tss[i], "id") == app.pendingDeleteTileset) { tss.erase(tss.begin() + i); break; }
            app.tilesets.erase(std::remove_if(app.tilesets.begin(), app.tilesets.end(), [](auto &e) { return e->tilesetId == app.pendingDeleteTileset; }), app.tilesets.end());
            app.proj.dirty = true; app.proj.rev++; app.pendingDeleteTileset.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!app.pendingDelete.empty()) ImGui::OpenPopup("Eliminar sprite");
    if (ui::beginModal("Eliminar sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int uses = spriteUses(app.pendingDelete);
        ImGui::Text("Se eliminará el sprite del proyecto.");
        if (uses) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Lo usan %d objeto(s) o animación(es).", uses);
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) { app.pendingDelete.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Eliminar", ImVec2(120, 0))) {
            auto &arr = app.proj.sprites();
            for (size_t i = 0; i < arr.size(); i++) if (arr[i].value("id", "") == app.pendingDelete) { arr.erase(arr.begin() + i); break; }
            app.sprites.erase(std::remove_if(app.sprites.begin(), app.sprites.end(), [](auto &e) { return e->spriteId == app.pendingDelete; }), app.sprites.end());
            app.proj.dirty = true; app.pendingDelete.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawStartDialogs();
    school::drawPanel(app.hasProject ? &app.proj : nullptr);   // leccion de la escuela en curso (sin proyecto: se despide)
    if (app.askClose) { ImGui::OpenPopup("Salir"); app.askClose = false; }
    if (ui::beginModal("Salir", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!app.hasProject || !app.proj.dirty) { app.quit = true; ImGui::CloseCurrentPopup(); }
        else {
            ImGui::Text("Hay cambios sin guardar en \"%s\".", app.proj.name().c_str());
            if (ui::primaryButton("Guardar y salir", ImVec2(150, 0))) {
                ImGui::CloseCurrentPopup();
                if (!app.proj.path.empty()) { if (saveProject(app.proj.path)) app.quit = true; } else saveAs();
            }
            ImGui::SameLine();
            if (ImGui::Button("Salir sin guardar", ImVec2(150, 0))) { app.quit = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void drawToast() {
    if (GetTime() > app.toastUntil || app.toast.empty()) return;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    // arriba y al centro, bajo las barras: abajo a la derecha tapaba filas de la hoja de eventos
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + (app.hasProject ? 96 : 16)), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    if (app.toastErr) ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "%s", app.toast.c_str());
    else ImGui::TextUnformatted(app.toast.c_str());
    ImGui::End();
}

/* ---------------- notificaciones (noticias con "Notificar") ----------------
   Baja suave desde arriba con el titulo y se va sola; un clic lleva a Inicio > Noticias. */
static std::string plainNews(std::string t) {   // sin las llaves de {lista:...} ni la marca de la encuesta
    for (size_t p; (p = t.find("{encuesta}")) != std::string::npos;) t.replace(p, 10, "(encuesta)");
    for (size_t p; (p = t.find("{lista:")) != std::string::npos;) t.erase(p, 7);
    t.erase(std::remove(t.begin(), t.end(), '}'), t.end());
    size_t nl = t.find('\n');
    return nl == std::string::npos ? t : t.substr(0, nl);
}
static std::string g_notifTest;   // guion: notif.prueba <titulo>
static void drawNewsNotification() {
    static double at = -1;
    static std::string tit, txt, tipo;
    double now = GetTime();
    if (at < 0 && !g_notifTest.empty()) { tit = g_notifTest; txt = "Esto es una notificación de prueba"; tipo = "evento"; at = now; g_notifTest.clear(); }
    if (at < 0) {
        if (!app.script.empty() && !getenv("S2D_NOVEDADES_URL")) return;
        const novedades::Noticia *n = novedades::notificacionPendiente();
        if (!n) return;
        tit = n->titulo; txt = plainNews(n->texto); tipo = n->tipo; at = now;
        novedades::notificada(n->id);
    }
    const double life = 8;
    double t = now - at;
    if (t > life + 0.45) { at = -1; return; }
    float in = (float)std::min(1.0, t / 0.45), out = t > life ? (float)std::max(0.0, 1 - (t - life) / 0.45) : 1.0f;
    in = 1 - (1 - in) * (1 - in) * (1 - in);
    if (!ui::animations()) { in = 1; out = t > life ? 0 : 1; }
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float W = std::min(460.0f, vp->WorkSize.x - 32), H = txt.empty() ? 58 : 76;
    ImVec2 p(vp->WorkPos.x + (vp->WorkSize.x - W) / 2, vp->WorkPos.y + 14 - (1 - in) * (H + 24) - (1 - out) * 18);
    ImGui::SetNextWindowPos(p);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, out);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, newsColor(tipo));
    ImGui::Begin("##notificacion", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float ring = 0.5f + 0.5f * (float)std::sin(now * 9) * (float)std::max(0.0, 1 - t / 1.5);   // la campana se mueve al llegar
    dl->AddCircleFilled(ImVec2(wp.x + 34, wp.y + H / 2), 18, (newsColor(tipo) & 0x00FFFFFF) | 0x40000000, 24);
    ui::drawIcon(dl, ui::Icon::Bell, ImVec2(wp.x + 34 + (ring - 0.5f) * 3, wp.y + H / 2), 16, newsColor(tipo));
    float fs = ImGui::GetFontSize();
    dl->AddText(ImVec2(wp.x + 64, wp.y + 10), ui::kDim, (std::string(newsKind(tipo)) + " · Noticia nueva").c_str());
    dl->PushClipRect(ImVec2(wp.x + 64, wp.y), ImVec2(wp.x + W - 34, wp.y + H), true);
    dl->AddText(ui::boldFont(), fs + 2, ImVec2(wp.x + 64, wp.y + 12 + fs), ui::kText, tit.c_str());
    if (!txt.empty()) dl->AddText(ImVec2(wp.x + 64, wp.y + 18 + 2 * fs), ui::kText2, txt.c_str());
    dl->PopClipRect();
    // barra de tiempo restante
    float left = (float)std::clamp(1 - t / life, 0.0, 1.0);
    dl->AddRectFilled(ImVec2(wp.x + 14, wp.y + H - 4), ImVec2(wp.x + 14 + (W - 28) * left, wp.y + H - 2), (newsColor(tipo) & 0x00FFFFFF) | 0x90000000, 1);
    ImGui::SetCursorScreenPos(ImVec2(wp.x + W - 30, wp.y + 8));
    bool closeHit = ImGui::InvisibleButton("##ncerrar", ImVec2(22, 22));
    ui::drawIcon(dl, ui::Icon::Close, ImVec2(wp.x + W - 19, wp.y + 19), 10, ImGui::IsItemHovered() ? ui::kText : ui::kDim);
    ImGui::SetCursorScreenPos(wp);
    bool hit = ImGui::InvisibleButton("##nver", ImVec2(W - 36, H));
    if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (t > 0.45 && t < life) at = now - std::min(t, 1.0); }   // con el raton encima no se va
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (hit) { app.homeSection = 5; app.focusWelcome = app.hasProject; g_studio.on = false; if (g_sound.ed) g_sound.ed->stop(); g_sound.on = false; }
    if ((hit || closeHit) && t < life) at = now - life;   // se va (con su animacion)
}

/* ---------------- archivos arrastrados a la ventana ----------------
   Mientras se arrastran por encima (en Linux; en Windows el sistema no avisa hasta soltar) sale un recuadro como el de
   Steam; al soltar, el recuadro se encoge hacia el centro y se abre cada archivo donde toca. */
extern "C" int s2d_drag_hover;   // glfw/input.c (parche): 1 mientras hay archivos arrastrandose encima
static double g_dropAt = -1;
static std::string g_dropName;
static void openDroppedFile(const std::string &f) {
    std::string e = lowerExtOf(f), name = fs::path(f).filename().string();
    bool proj = e == ".source2d" || e == ".json";   // .source2d.json o .source2d (openProject comprueba que sea un proyecto)
    if (proj) { guardSwitch([f] { openProject(f); }); return; }
    if (isSoundFile(f)) { openSoundFile(f); return; }
    bool img = e == ".png" || e == ".gif" || e == ".bmp" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".qoi";
    if (isSpriteFile(f) || (img && (!app.hasProject || g_studio.on))) { openSpriteFile(f); return; }
    if (img) { app.importer.asTiles = false; if (!app.importer.load(f)) notify(app.importer.error, true); return; }   // al proyecto
    bool audio = e == ".wav" || e == ".ogg" || e == ".mp3" || e == ".flac" || e == ".qoa";
    if (audio && app.hasProject) {
        std::string err;
        if (importMusic(app.proj, f, err).empty()) notify(err, true); else notify("Audio añadido a Música y sonidos: " + name);
        return;
    }
    notify("No sé abrir " + name + (audio ? " sin un proyecto abierto" : ""), true);
}
static void dropFilesTick() {
    if (!IsFileDropped()) return;
    FilePathList l = LoadDroppedFiles();
    std::vector<std::string> files;
    for (unsigned i = 0; i < l.count; i++) files.push_back(l.paths[i]);
    UnloadDroppedFiles(l);
    s2d_drag_hover = 0;
    if (files.empty()) return;
    g_dropAt = GetTime();
    g_dropName = files.size() == 1 ? fs::path(files[0]).filename().string() : std::to_string(files.size()) + " archivos";
    for (auto &f : files) openDroppedFile(f);
}
static void drawDropOverlay() {
    static float a = 0;
    float dt = ImGui::GetIO().DeltaTime;
    bool hover = s2d_drag_hover != 0;
    a += ((hover ? 1.0f : 0.0f) - a) * std::min(1.0f, dt * (hover ? 14.0f : 9.0f));
    double since = g_dropAt < 0 ? 1e9 : GetTime() - g_dropAt;
    if (since > 0.6) g_dropAt = -1;
    if (a < 0.01f && g_dropAt < 0) return;
    if (!ui::animations()) a = hover ? 1 : 0;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 p0 = vp->Pos, p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
    float now = (float)GetTime();
    if (a > 0.01f) {   // arrastrando: fondo oscuro y recuadro de rayas que corren, con la flecha que rebota
        dl->AddRectFilled(p0, p1, IM_COL32(10, 10, 16, (int)(170 * a)));
        float in = 26 + (1 - a) * 30;
        ImVec2 a0(p0.x + in, p0.y + in), a1(p1.x - in, p1.y - in);
        dl->AddRectFilled(a0, a1, (ui::kPrimary & 0x00FFFFFF) | ((ImU32)(28 * a) << 24), 22);
        // borde de rayas (marcha de hormigas)
        float per = 2 * ((a1.x - a0.x) + (a1.y - a0.y)), dash = 18, off = std::fmod(now * 40, dash * 2);
        auto at = [&](float d) {
            d = std::fmod(d + per, per);
            float w = a1.x - a0.x, h = a1.y - a0.y;
            if (d < w) return ImVec2(a0.x + d, a0.y);
            if ((d -= w) < h) return ImVec2(a1.x, a0.y + d);
            if ((d -= h) < w) return ImVec2(a1.x - d, a1.y);
            return ImVec2(a0.x, a1.y - (d - w));
        };
        ImU32 bc = (ui::kPrimaryLight & 0x00FFFFFF) | ((ImU32)(230 * a) << 24);
        for (float d = off; d < per + off; d += dash * 2) dl->AddLine(at(d), at(d + dash * 0.9f), bc, 3);
        float bob = std::sin(now * 5) * 6, s = 0.85f + 0.15f * a;
        dl->AddCircleFilled(ImVec2(c.x, c.y - 30), 46 * s, (ui::kPrimary & 0x00FFFFFF) | ((ImU32)(220 * a) << 24), 48);
        ui::drawIcon(dl, ui::Icon::Download, ImVec2(c.x, c.y - 30 + bob * 0.5f), 34 * s, IM_COL32(255, 255, 255, (int)(255 * a)));
        const char *t1 = "Suelta para abrir";
        const char *t2 = "Proyectos (.source2d), sprites, imágenes y sonidos";
        ImVec2 s1 = ui::boldFont()->CalcTextSizeA(26, FLT_MAX, 0, t1), s2 = ImGui::CalcTextSize(t2);
        dl->AddText(ui::boldFont(), 26, ImVec2(c.x - s1.x / 2, c.y + 30), IM_COL32(255, 255, 255, (int)(255 * a)), t1);
        dl->AddText(ImVec2(c.x - s2.x / 2, c.y + 64), (ui::kText2 & 0x00FFFFFF) | ((ImU32)(255 * a) << 24), t2);
    }
    if (g_dropAt >= 0) {   // al soltar: una tarjeta con el nombre se encoge hacia el centro y se desvanece
        float t = (float)std::clamp(since / 0.6, 0.0, 1.0), k = 1 - t * t, sc = 1.15f - 0.55f * t;
        ImVec2 ts = ImGui::CalcTextSize(g_dropName.c_str());
        float w = (std::max(ts.x, 120.0f) + 70) * sc, h = 58 * sc;
        ImVec2 q0(c.x - w / 2, c.y - h / 2 + t * 30), q1(c.x + w / 2, c.y + h / 2 + t * 30);
        dl->AddRectFilled(ImVec2(q0.x + 3, q0.y + 6), ImVec2(q1.x + 3, q1.y + 6), IM_COL32(0, 0, 0, (int)(80 * k)), 14);
        dl->AddRectFilled(q0, q1, (ui::kPanel & 0x00FFFFFF) | ((ImU32)(250 * k) << 24), 14);
        dl->AddRect(q0, q1, (ui::kPrimaryLight & 0x00FFFFFF) | ((ImU32)(255 * k) << 24), 14.0f, 0, 2.0f);
        ui::drawIcon(dl, ui::Icon::Folder, ImVec2(q0.x + 26 * sc, (q0.y + q1.y) / 2), 18 * sc, (ui::kPrimaryLight & 0x00FFFFFF) | ((ImU32)(255 * k) << 24));
        if (sc > 0.7f) dl->AddText(ImVec2(q0.x + 48 * sc, (q0.y + q1.y) / 2 - ts.y / 2), IM_COL32(255, 255, 255, (int)(255 * k)), g_dropName.c_str());
    }
}

static void globalShortcuts() {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openDialog();
    if (!app.hasProject) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) saveAs(); else save(); }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) closeProject();
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) playProject();
}

/* ---------------- guion de pruebas (--script) ---------------- */
static SpriteEditor *activeSprite() { return g_studio.on && g_studio.ed ? g_studio.ed.get() : app.sprites.empty() ? nullptr : app.sprites.back().get(); }
static Tool toolByName(const std::string &n) {
    static const std::map<std::string, Tool> m = {{"pencil", Tool::Pencil}, {"eraser", Tool::Eraser}, {"bucket", Tool::Bucket}, {"picker", Tool::Picker},
        {"line", Tool::Line}, {"rect", Tool::Rect}, {"ellipse", Tool::Ellipse}, {"select", Tool::Select}, {"move", Tool::Move}, {"hand", Tool::Hand},
        {"lasso", Tool::Lasso}, {"wand", Tool::Wand}, {"spray", Tool::Spray}, {"gradient", Tool::Gradient}, {"contour", Tool::Contour},
        {"selellipse", Tool::SelEllipse}};
    auto it = m.find(n);
    return it == m.end() ? Tool::Pencil : it->second;
}
static void runScriptStep() {
    if (app.scriptWait > 0) { app.scriptWait--; return; }
    if (app.scriptPos >= app.script.size()) {
        /* guion terminado sin "quit": se cierra igual (si no, la bateria esperaria hasta su limite de tiempo) */
        if (!app.quit) {
            if (prefs().projectsDir.find("source2d-pruebas-") != std::string::npos) { std::error_code ec; fs::remove_all(prefs().projectsDir, ec); }
            printf("S2DEDIT FIN fallos=%d (el guion no acaba en quit)\n", app.fails); fflush(stdout); app.quit = true;
        }
        return;
    }
    std::string line = app.script[app.scriptPos++];
    std::istringstream in(line);
    std::string cmd; in >> cmd;
    if (cmd.empty() || cmd[0] == '#') return;
    auto &t = spriteTools();
    SpriteEditor *e = activeSprite();
    auto need = [&]() { if (!e) { printf("S2DEDIT FAIL %s: no hay sprite abierto\n", line.c_str()); app.fails++; } return e != nullptr; };
    if (cmd == "open") { std::string p; in >> p; openProject(p); }
    else if (cmd == "openajeno") { std::string p; in >> p; app.scriptTrustCheck = true; openProject(p); app.scriptTrustCheck = false; }
    else if (cmd == "licencias") app.showLicenses = true;
    else if (cmd.rfind("colab.", 0) == 0) {
        /* trabajar juntos: dos editores en la misma PC (uno comparte y el otro se une por 127.0.0.1) */
        auto waitFor = [&](bool ok, int maxFrames, const std::string &what) {
            if (ok) { app.scriptWaitTries = 0; printf("S2DEDIT OK %s\n", line.c_str()); return; }
            if (++app.scriptWaitTries < maxFrames) { app.scriptPos--; return; }
            app.scriptWaitTries = 0; app.fails++;
            printf("S2DEDIT FAIL %s: %s\n", line.c_str(), what.c_str());
        };
        if (cmd == "colab.host") {
            std::string nm; in >> nm; std::string err;
            if (collab::host(app.proj, nm, collab::colorFor(1), err)) printf("S2DEDIT colab codigo %s\n", collab::code().c_str());
            else { printf("S2DEDIT FAIL %s: %s\n", line.c_str(), err.c_str()); app.fails++; }
        } else if (cmd == "colab.join") {
            std::string addr, cd, nm, err; in >> addr >> cd >> nm;
            if (!collab::join(addr, cd, nm, collab::colorFor(2), err)) { printf("S2DEDIT FAIL %s: %s\n", line.c_str(), err.c_str()); app.fails++; }
        } else if (cmd == "colab.waitjoined") {
            waitFor(collab::connected() && app.hasProject, 900, collab::status());
        } else if (cmd == "colab.waitpeers") {
            int n = 0; in >> n;
            waitFor((int)collab::peers().size() == n, 900, std::to_string(collab::peers().size()) + " personas");
        } else if (cmd == "colab.wait") {
            // colab.wait <puntero JSON> <valor JSON>: espera (hasta 15 s) a que el proyecto tenga ese valor
            std::string ptr, val; in >> ptr; std::getline(in >> std::ws, val);
            json::json_pointer jp(ptr);
            json v = json::parse(val, nullptr, false);
            bool ok = app.proj.doc.contains(jp) && app.proj.doc[jp] == v;
            waitFor(ok, 900, app.proj.doc.contains(jp) ? app.proj.doc[jp].dump() : "(no existe)");
        } else if (cmd == "colab.pres") {
            in >> app.scriptPresDoc >> app.scriptPresX >> app.scriptPresY;
        } else if (cmd == "colab.waitpres") {
            // colab.waitpres <nombre> <documento>: espera a ver a esa persona en ese documento (con cursor)
            std::string nm, doc; in >> nm >> doc;
            bool ok = false;
            for (auto &p : collab::peers()) if (p.name == nm && p.doc == doc && p.cursor) ok = true;
            waitFor(ok, 900, "no se ve a " + nm + " en " + doc);
        } else if (cmd == "colab.print") {
            std::string ptr; in >> ptr; json::json_pointer jp(ptr);
            printf("S2DEDIT colab valor %s %s\n", ptr.c_str(), app.proj.doc.contains(jp) ? app.proj.doc[jp].dump().c_str() : "(no existe)");
        } else if (cmd == "colab.dialog") {
            int tab = 0, mode = 0; in >> tab >> mode;
            app.collabTab = tab; app.collabMode = mode; app.showCollab = true;
        } else if (cmd == "colab.waitfound") {
            // la sesion del otro editor aparece en "Unirse" (aviso por UDP en la red local)
            collab::discover(true);
            waitFor(!collab::found().empty(), 900, "no se encontro ninguna sesion");
        } else if (cmd == "colab.waitdoc") {
            // colab.waitdoc <nombre> <documento> [paint|sel]: espera a ver a esa persona ahi (pintando o con algo seleccionado)
            std::string nm, doc, what; in >> nm >> doc >> what;
            bool ok = false;
            for (auto &p : collab::peers()) {
                if (p.name != nm || p.doc != doc) continue;
                if (what == "paint") ok = p.extra.is_object() && p.extra.contains("paint") && !p.extra["paint"].empty();
                else if (what == "sel") ok = !p.sel.empty();
                else ok = true;
            }
            std::string seen;
            for (auto &p : collab::peers()) if (p.name == nm) seen = " (esta en '" + p.doc + "', " + p.extra.dump().substr(0, 80) + ")";
            waitFor(ok, 900, "no se ve a " + nm + " en " + doc + (what.empty() ? "" : " (" + what + ")") + seen);
        } else if (cmd == "colab.stop") {
            collab::stop();
        } else if (cmd == "colab.expectended") {
            waitFor(collab::role() == collab::Role::None, 900, collab::status());
        } else { printf("S2DEDIT FAIL comando desconocido: %s\n", line.c_str()); app.fails++; }
    }
    else if (cmd == "confiar") { std::string f = app.pendingTrust; app.pendingTrust.clear(); if (!f.empty()) openProject(f, true); }
    else if (cmd == "sprite") {
        std::string nm; std::getline(in >> std::ws, nm);
        for (auto &sp : app.proj.sprites()) if (sp.value("name", "") == nm) { openSprite(sp.value("id", "")); break; }
    }
    else if (cmd == "newsprite") { std::string nm; int w, h; in >> nm >> w >> h; openSprite(newSprite(nm, w, h)); }
    else if (cmd == "tool") { std::string n; in >> n; t.tool = toolByName(n); }
    else if (cmd == "color") { std::string c; in >> c; t.fg = parseColor(c); }
    else if (cmd == "bg") { std::string c; in >> c; t.bg = c == "none" ? Px{} : parseColor(c); }
    else if (cmd == "brush") in >> t.brush;
    else if (cmd == "opt") {
        std::string n; int v; in >> n >> v;
        bool *b = n == "pixelperfect" ? &t.pixelPerfect : n == "fill" ? &t.fillShapes : n == "contiguous" ? &t.contiguous : n == "mirrorx" ? &t.mirrorX :
                  n == "mirrory" ? &t.mirrorY : n == "onion" ? &t.onion : n == "grid" ? &t.grid : nullptr;
        if (b) *b = v != 0;
    }
    else if (cmd == "stroke" || cmd == "rstroke") {
        if (!need()) return;
        std::vector<int> v; int x;
        while (in >> x) v.push_back(x);
        if (v.size() < 2) return;
        e->strokeBegin(v[0], v[1], cmd == "rstroke");
        for (size_t i = 2; i + 1 < v.size(); i += 2) e->strokeMove(v[i], v[i + 1]);
        e->strokeEnd();
    }
    else if (cmd == "strokehold" && need()) {   // strokehold x y x y...: trazo sin soltar el boton (trabajar juntos)
        std::vector<int> v; int x;
        while (in >> x) v.push_back(x);
        if (v.size() < 2) return;
        e->strokeBegin(v[0], v[1], false);
        for (size_t i = 2; i + 1 < v.size(); i += 2) e->strokeMove(v[i], v[i + 1]);
        e->scriptHold = true;
    }
    else if (cmd == "strokerelease" && need()) { e->scriptHold = false; e->strokeEnd(); }
    else if (cmd == "addframe" && need()) e->addFrame(false);
    else if (cmd == "dupframe" && need()) e->addFrame(true);
    else if (cmd == "delframe" && need()) e->deleteFrame();
    else if (cmd == "addlayer" && need()) e->addLayer();
    else if (cmd == "fx" && need()) { int k; float a = 0, b = 0; in >> k >> a >> b; e->applyFx(k, false, a, b, t.fg, t.bg); }
    else if (cmd == "duplayer" && need()) e->duplicateLayer();
    else if (cmd == "mergedown" && need()) e->mergeDown();
    else if (cmd == "scale" && need()) { int pc; in >> pc; e->scaleSprite(pc); }
    else if (cmd == "dellayer" && need()) e->deleteLayer();
    else if (cmd == "frame" && need()) { int f; in >> f; e->curFrame = std::clamp(f, 0, (int)e->st.layers[0].cels.size() - 1); }
    else if (cmd == "layer" && need()) { int l; in >> l; e->curLayer = std::clamp(l, 0, (int)e->st.layers.size() - 1); }
    else if (cmd == "hide" && need()) { int l; in >> l; e->st.layers[l].visible = false; e->dirtyForProject = true; }
    else if (cmd == "undo" && need()) e->undo();
    else if (cmd == "redo" && need()) e->redo();
    else if (cmd == "selectall" && need()) e->selectAll();
    else if (cmd == "copy" && need()) e->copySel(false);
    else if (cmd == "cut" && need()) e->copySel(true);
    else if (cmd == "paste" && need()) e->paste();
    else if (cmd == "clear" && need()) e->clearSelection();
    else if (cmd == "resize" && need()) { int w, h; in >> w >> h; e->resizeCanvas(w, h, false); }
    else if (cmd == "flipx" && need()) e->flip(true);
    else if (cmd == "rot90" && need()) e->rotate90();
    // Source Sprite y herramientas de corte
    else if (cmd == "ini.load") { std::string f; in >> f; ImGui::LoadIniSettingsFromDisk(f.c_str()); app.wantLayout = true; }   // disposicion guardada de un usuario
    else if (cmd == "closeproject") closeProject();
    else if (cmd == "export.source") { std::string f; std::getline(in >> std::ws, f); exportSourceGame(f); sourceExportTick(true); }   // Exportar > Solo para Source 2D
    else if (cmd == "expectfile") { std::string f; std::getline(in >> std::ws, f); bool ok = fs::exists(f); if (!ok) app.fails++; printf("S2DEDIT %s %s\n", ok ? "OK" : "FAIL", line.c_str()); }
    else if (cmd == "expectstudio") { int v; in >> v; bool ok = (g_studio.on ? 1 : g_sound.on ? 2 : 0) == v; if (!ok) app.fails++; printf("S2DEDIT %s %s\n", ok ? "OK" : "FAIL", line.c_str()); }   // 0 ninguno, 1 Source Sprite, 2 Source Sound
    else if (cmd == "expectproject") { int v; in >> v; bool ok = app.hasProject == (v != 0); if (!ok) app.fails++; printf("S2DEDIT %s %s\n", ok ? "OK" : "FAIL", line.c_str()); }
    else if (cmd == "studio") openStudio();
    else if (cmd == "studio.close") g_studio.on = false;
    else if (cmd == "studio.new") { int w, h; in >> w >> h; openStudio(); studioNew(w, h); }
    else if (cmd == "studio.load") { std::string f; in >> f; openStudio(); if (!studioLoad(f)) { printf("S2DEDIT FAIL %s\n", line.c_str()); app.fails++; } }
    else if (cmd == "studio.export") {
        std::string f; int layout = 1, js = 0; in >> f >> layout >> js;
        bool ok = g_studio.ed && studioWrite(f, layout, js != 0);
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s\n", ok ? "OK" : "FAIL", line.c_str());
    }
    else if (cmd == "wand" && need()) { int x, y, m = 0; in >> x >> y >> m; e->selectByColor(x, y, t.wandContiguous, m); }
    else if (cmd == "selrect" && need()) { int x, y, w, h, m = 0; in >> x >> y >> w >> h >> m; e->selectRect(x, y, w, h, m); }
    else if (cmd == "invertsel" && need()) e->invertSelection();
    else if (cmd == "cropsel" && need()) e->cropToSelection();
    else if (cmd == "trim" && need()) e->trimCanvas();
    else if (cmd == "cutlayer" && need()) e->cutToNewLayer();
    else if (cmd == "slice" && need()) { int w, h, sk = 1; in >> w >> h >> sk; e->sliceToFrames(w, h, sk != 0); }
    else if (cmd == "expectsel" && need()) {
        int x, y, w, h; in >> x >> y >> w >> h;
        SelRect r = e->selection();
        bool ok = e->selActive() && r.x == x && r.y == y && r.w == w && r.h == h;
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s -> %d %d %d %d (%s)\n", ok ? "OK" : "FAIL", line.c_str(), r.x, r.y, r.w, r.h, e->selActive() ? "activa" : "sin selección");
    }
    else if (cmd == "pal.load" || cmd == "pal.save" || cmd == "pal.expect") {
        std::string f; in >> f;
        std::string err; bool ok;
        if (cmd == "pal.load") { std::vector<Px> pal; ok = loadPaletteFile(f, pal, err); if (ok) t.palette = pal; }
        else if (cmd == "pal.save") ok = savePaletteFile(f, t.palette, err);
        else { int n = std::stoi(f); ok = (int)t.palette.size() == n; err = std::to_string(t.palette.size()) + " colores"; }
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s %s\n", ok ? "OK" : "FAIL", line.c_str(), err.c_str());
    }
    else if (cmd == "scene") {
        std::string nm; std::getline(in >> std::ws, nm);
        for (auto &sc : app.proj.doc["scenes"]) if (jstr(sc, "name") == nm) openScene(jstr(sc, "id"));
    }
    else if (cmd == "object") {
        std::string nm; std::getline(in >> std::ws, nm);
        for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == nm) openObject(jstr(o, "id"));
    }
    else if (cmd == "objtab") { int t; in >> t; if (!app.objects.empty()) app.objects.back()->forceTab = t; }
    else if (cmd == "newobject") { std::string k; in >> k; std::string id = createObject(app.proj, k); if (json *no = findById(app.proj.doc["objects"], id)) (*no)["scene"] = currentScene(); openObject(id); }
    else if (cmd == "ev.comunes") { int v; in >> v; app.scriptCommon = v != 0; }   // ev.* sobre los eventos comunes (1) o los de la escena (0)
    else if (cmd == "newscene") openScene(newScene());
    else if (cmd == "addbh" || cmd == "delbh") {
        std::string t; in >> t;
        if (!app.objects.empty()) if (json *o = findById(app.proj.doc["objects"], app.objects.back()->objectId)) {
            if (cmd == "addbh") objectAddBehavior(*o, t); else objectRemoveBehavior(*o, t);
            app.proj.dirty = true;
        }
    }
    else if (cmd == "jset" || cmd == "jexpect") {
        // jset <puntero JSON> <valor JSON>   (por ejemplo: jset /objects/0/name "Heroe")
        std::string ptr, val; in >> ptr; std::getline(in >> std::ws, val);
        try {
            json v = json::parse(val);
            json::json_pointer jp(ptr);
            if (cmd == "jset") { app.proj.doc[jp] = v; app.proj.dirty = true; }
            else {
                bool ok = app.proj.doc.contains(jp) && app.proj.doc[jp] == v;
                if (!ok && app.proj.doc.contains(jp) && v.is_number() && app.proj.doc[jp].is_number()) ok = std::fabs(app.proj.doc[jp].get<double>() - v.get<double>()) < 1e-6;
                if (!ok) app.fails++;
                printf("S2DEDIT %s %s -> %s\n", ok ? "OK" : "FAIL", line.c_str(), app.proj.doc.contains(jp) ? app.proj.doc[jp].dump().c_str() : "(no existe)");
            }
        } catch (const std::exception &ex) { printf("S2DEDIT FAIL %s: %s\n", line.c_str(), ex.what()); app.fails++; }
    }
    else if (cmd.rfind("sc.", 0) == 0) {
        // acciones del editor de escena abierto (el ultimo)
        if (app.scenes.empty()) { printf("S2DEDIT FAIL %s: no hay escena abierta\n", line.c_str()); app.fails++; return; }
        SceneEditor &se = *app.scenes.back();
        json *sc = findById(app.proj.doc["scenes"], se.sceneId);
        if (!sc) return;
        std::string a = cmd.substr(3);
        if (a == "placeobj") { std::string nm; std::getline(in >> std::ws, nm); for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == nm) se.placeObj = jstr(o, "id"); }
        else if (a == "place") { float x, y; in >> x >> y; se.placeAt(app.proj, *sc, x, y); }
        else if (a == "pick") { float x, y; in >> x >> y; std::string id = se.pickAt(app.proj, *sc, x, y); se.sel.clear(); if (!id.empty()) se.sel.push_back(id); printf("S2DEDIT pick %s\n", id.c_str()); }
        else if (a == "move") { float dx, dy; in >> dx >> dy; se.moveSelected(*sc, dx, dy); }
        else if (a == "dup") se.duplicateSelected(*sc);
        else if (a == "del") se.deleteSelected(*sc);
        else if (a == "tileobj") { std::string nm; std::getline(in >> std::ws, nm); for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == nm) se.paintObj = jstr(o, "id"); se.mode = SceneEditor::Tiles; }
        else if (a == "newpiece") se.newPiece = true;
        else if (a == "editobj") { std::string nm; std::getline(in >> std::ws, nm); for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == nm) se.editObject(jstr(o, "id")); }
        else if (a == "objtab") { int t; in >> t; if (se.objView) se.objView->forceTab = t; }
        else if (a == "frameop") {   // pruebas: sc.frameop <m|d|c|n> i [j] sobre la animacion elegida del objeto abierto
            std::string op; int i = 0, j = 0; in >> op >> i >> j;
            if (se.objView) if (json *o = findById(app.proj.doc["objects"], se.objView->objectId)) {
                json &an = (*o)["animations"];
                std::string sid = an.is_array() && se.objView->curAnim < (int)an.size() ? jstr(an[se.objView->curAnim], "spriteId") : jstr(*o, "spriteId");
                if (json *sp = app.proj.spriteById(sid)) { objectFrameOp(*sp, op[0], i, j); app.proj.rev++; }
            }
        }
        else if (a == "tile") { in >> se.paintTile; }
        else if (a == "paint") { float x, y; int t; in >> x >> y >> t; se.paintCell(app.proj, *sc, x, y, t); }
        else if (a == "mode") { std::string m; in >> m; se.mode = m == "place" ? SceneEditor::Place : m == "tiles" ? SceneEditor::Tiles : SceneEditor::Select; }
        else if (a == "undo") se.undo(*sc);
        else if (a == "redo") se.redo(*sc);
        else if (a == "count") {
            int want; in >> want;
            int got = (int)(*sc)["instances"].size();
            if (got != want) app.fails++;
            printf("S2DEDIT %s %s -> %d\n", got == want ? "OK" : "FAIL", line.c_str(), got);
        }
        else if (a == "expectsel") {
            std::string key; double want; in >> key >> want;
            json *it = se.sel.size() == 1 ? findById((*sc)["instances"], se.sel[0]) : nullptr;
            double got = it ? jnum(*it, key.c_str(), -99999) : -99999;
            bool ok = std::fabs(got - want) < 1e-6;
            if (!ok) app.fails++;
            printf("S2DEDIT %s %s -> %g\n", ok ? "OK" : "FAIL", line.c_str(), got);
        }
        else { printf("S2DEDIT FAIL comando de escena desconocido: %s\n", line.c_str()); app.fails++; }
        app.proj.dirty = true;
    }
    else if (cmd.rfind("ev.", 0) == 0) {
        EventsEditor &ee = openEventsFor(app.scriptCommon ? "" : currentScene(), false);
        json &evs = ee.events(app.proj);
        std::string a = cmd.substr(3);
        auto setParams = [&](json &row, const std::string &kind) {
            // pares clave=valor; "@Nombre" se convierte en el id del objeto o escena
            std::string kv;
            while (in >> kv) {
                size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                for (char &c : v) if (c == '~') c = ' ';
                json val = v;
                if (!v.empty() && v[0] == '@') {
                    for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == v.substr(1)) val = jstr(o, "id");
                    for (auto &sc : app.proj.doc["scenes"]) if (jstr(sc, "name") == v.substr(1)) val = jstr(sc, "id");
                } else { char *e = nullptr; double d = strtod(v.c_str(), &e); if (!v.empty() && e && *e == 0) val = d == (long long)d ? json((long long)d) : json(d); }
                row["params"][k] = val;
            }
            (void)kind;
        };
        if (a == "show") openEvents(true);
        else if (a == "add") { std::string t; in >> t; ee.addEvent(app.proj, t, false); }
        else if (a == "sub") ee.addEvent(app.proj, "", true);
        else if (a == "select") in >> ee.sel;
        else if (a == "unfold") {   // ev.unfold <nombre del grupo>: lo despliega y deja la hoja en ese grupo
            std::string nm; std::getline(in >> std::ws, nm);
            for (size_t k = 0; k < evs.size(); k++) if (jstr(evs[k], "kind") == "folder" && jstr(evs[k], "name") == nm) { evs[k]["collapsed"] = false; ee.sel = (int)k; }
        }
        else if (a == "comment") ee.addComment(app.proj);
        else if (a == "group") ee.addGroup(app.proj);
        else if (a == "addcond" || a == "addact") {
            int ei; std::string path, type;
            in >> ei;
            if (a == "addcond") in >> path; else path = "a";
            in >> type;
            std::string kind = a == "addcond" ? "cond" : "act";
            if (ei < 0 || ei >= (int)evs.size()) { printf("S2DEDIT FAIL %s: evento fuera de rango\n", line.c_str()); app.fails++; return; }
            json row = newInstruction(app.proj, kind, type);
            setParams(row, kind);
            if (json *l = ee.instrList(evs[ei], path, true)) l->push_back(row);
        }
        else if (a == "invert") { int ei; std::string path; in >> ei >> path; if (json *r = ee.instrAt(evs[ei], path, nullptr, nullptr)) (*r)["invert"] = !jbool(*r, "invert", false); }
        else if (a == "delins") { int ei; std::string path; in >> ei >> path; json *l = nullptr; int i = 0; if (ee.instrAt(evs[ei], path, &l, &i)) l->erase(l->begin() + i); }
        else if (a == "del") { int ei; in >> ei; ee.deleteEvent(app.proj, ei); }
        else if (a == "undo") ee.undo(app.proj);
        else if (a == "redo") ee.redo(app.proj);
        else if (a == "sentence") {
            int ei; std::string path, want; in >> ei >> path; std::getline(in >> std::ws, want);
            std::string kind = path[0] == 'a' ? "act" : "cond";
            json *r = ei < (int)evs.size() ? ee.instrAt(evs[ei], path, nullptr, nullptr) : nullptr;
            std::string got = r ? sentencePlain(app.proj, kind, *r) : "(no existe)";
            bool ok = got == want;
            if (!ok) app.fails++;
            printf("S2DEDIT %s %s -> %s\n", ok ? "OK" : "FAIL", line.c_str(), got.c_str());
        }
        else if (a == "expr") {
            std::string rest; std::getline(in >> std::ws, rest);
            size_t bar = rest.find(" => ");
            std::string ex = rest.substr(0, bar), want = bar == std::string::npos ? "ok" : rest.substr(bar + 4);
            std::string got = checkExpression(app.proj, ex);
            bool ok = want == "ok" ? got.empty() : got.find(want) != std::string::npos;
            if (!ok) app.fails++;
            printf("S2DEDIT %s expr [%s] -> %s\n", ok ? "OK" : "FAIL", ex.c_str(), got.empty() ? "valida" : got.c_str());
        }
        else if (a == "dialog") {
            std::string kind, path, type; int ei; in >> kind >> ei >> path >> type;
            ee.openDialog(app.proj, kind, ei, path, false);
            if (!type.empty()) ee.dialogPick(app.proj, type);
        }
        else if (a == "dlgaccept") ee.dialogAccept(app.proj);
        else { printf("S2DEDIT FAIL comando de eventos desconocido: %s\n", line.c_str()); app.fails++; }
        app.proj.dirty = true;
    }
    else if (cmd == "scrolldown") app.scrollEventsToEnd = true;
    else if (cmd == "tileset") { std::string nm; std::getline(in >> std::ws, nm); for (auto &t : app.proj.doc["tilesets"]) if (jstr(t, "name") == nm) openTileset(jstr(t, "id")); }
    else if (cmd == "newtileset") { std::string nm; int T, c, r; in >> nm >> T >> c >> r; openTileset(createTileset(app.proj, nm, T, c, r)); }
    else if (cmd.rfind("ts.", 0) == 0) {
        if (app.tilesets.empty()) { printf("S2DEDIT FAIL %s: no hay conjunto abierto\n", line.c_str()); app.fails++; return; }
        TilesetEditor &te = *app.tilesets.back();
        std::string a = cmd.substr(3);
        if (a == "tile") in >> te.pix.curFrame;
        else if (a == "stroke") {
            std::vector<int> v; int x; while (in >> x) v.push_back(x);
            if (v.size() >= 2) { te.pix.strokeBegin(v[0], v[1], false); for (size_t i = 2; i + 1 < v.size(); i += 2) te.pix.strokeMove(v[i], v[i + 1]); te.pix.strokeEnd(); te.writeBack(app.proj); }
        }
        else if (a == "grid") { int T, c, r; in >> T >> c >> r; te.setGrid(app.proj, T, c, r); }
        else if (a == "flag") { std::string f; int v; in >> f >> v; if (json *t = findById(app.proj.doc["tilesets"], te.tilesetId)) { (*t)["tiles"][te.pix.curFrame][f] = v != 0; app.proj.dirty = true; } }
    }
    else if (cmd == "import") {
        std::string f, mode; int a = 16, b = 16; in >> f >> mode >> a >> b;
        app.importer.asTiles = mode == "tiles";
        if (app.importer.load(f)) { app.importer.fw = a; app.importer.fh = mode == "tiles" ? a : b; std::string id = app.importer.commit(app.proj); printf("S2DEDIT import %s\n", id.c_str()); }
        else { printf("S2DEDIT FAIL import: %s\n", app.importer.error.c_str()); app.fails++; }
    }
    else if (cmd == "newpixelfont") { std::string nm = "Fuente"; int w = 8, h = 8; in >> nm >> w >> h; openFont(createPixelFont(app.proj, nm, w, h, true)); }
    else if (cmd == "font.import") { std::string f, err; in >> f; std::string id = importFont(app.proj, f, err); if (id.empty()) { printf("S2DEDIT FAIL %s\n", err.c_str()); app.fails++; } else openFont(id); }
    else if (cmd == "font.use") {   // font.use <objeto> <fuente>: pone la fuente a un texto o boton
        std::string on, fn; in >> on >> fn;
        std::string fid;
        for (auto &f : app.proj.doc["fonts"]) if (jstr(f, "name") == fn) fid = jstr(f, "id");
        for (auto &o : app.proj.doc["objects"]) if (jstr(o, "name") == on) { o[jstr(o, "type") == "button" ? "button" : "text"]["font"] = fid; app.proj.dirty = true; app.proj.rev++; }
    }
    else if (cmd == "font.stroke") {   // font.stroke x0 y0 x1 y1: dibuja en la letra actual del ultimo editor de fuentes
        if (app.fonts.empty()) { printf("S2DEDIT FAIL %s: no hay fuente abierta\n", line.c_str()); app.fails++; return; }
        int x0, y0, x1, y1; in >> x0 >> y0 >> x1 >> y1;
        auto &fe = *app.fonts.back();
        fe.pix.strokeBegin(x0, y0, false); fe.pix.strokeMove(x1, y1); fe.pix.strokeEnd();
    }
    else if (cmd == "font.glyph") { int g; in >> g; if (!app.fonts.empty()) app.fonts.back()->pix.curFrame = g; }
    else if (cmd == "files.import") { std::string f, err; in >> f; if (!addInitialFile(app.proj, f, err)) { printf("S2DEDIT FAIL %s\n", err.c_str()); app.fails++; } }
    else if (cmd == "music.import") { std::string f, err; in >> f; std::string id = importMusic(app.proj, f, err); if (id.empty()) { printf("S2DEDIT FAIL %s\n", err.c_str()); app.fails++; } }
    else if (cmd == "music.play") { std::string nm; std::getline(in >> std::ws, nm); for (auto &m : app.proj.doc["music"]) if (jstr(m, "name") == nm) musicPreview(app.proj, jstr(m, "id")); printf("S2DEDIT musica sonando: %s\n", musicPlaying().empty() ? "no" : "si"); }
    else if (cmd == "doc") {
        // doc <settings|shaders|extensions> [id a seleccionar]
        std::string d, id; in >> d >> id;
        if (d == "settings") openDoc(app.settings);
        else if (d == "shaders") { openDoc(app.shaders); if (!id.empty()) app.shaders->cur = id; }
        else if (d == "extensions") { openDoc(app.extensions); if (!id.empty()) app.extensions->cur = id; }
        else if (d == "layers") openDoc(app.layersDoc);
        else if (d == "autotiles") { openDoc(app.autotiles); if (id == "new") app.autotiles->cur = createAutotile(app.proj); }
    }
    else if (cmd == "ext.add") { std::string k; in >> k; addExtension(app.proj, k == "plugin", k == "estilo"); }
    else if (cmd == "shader.rain") addRainShader(app.proj);
    else if (cmd == "ext.steam") addSteamExtension(app.proj);
    else if (cmd == "school.start") { std::string id; in >> id; school::start(id); }
    else if (cmd == "spriteobj") {                     // pruebas: abre el sprite del ultimo objeto abierto ("Editar sprite")
        if (!app.objects.empty()) if (json *o = findById(app.proj.doc["objects"], app.objects.back()->objectId)) openSprite(jstr(*o, "spriteId"));
    }
    else if (cmd == "placelast") {                     // pruebas: una instancia del ultimo objeto en la primera escena
        json &objs = app.proj.doc["objects"];
        if (!objs.empty() && !app.proj.doc["scenes"].empty())
            app.proj.doc["scenes"][0]["instances"].push_back({{"id", makeUid("i")}, {"objectId", jstr(objs.back(), "id")}, {"x", 100}, {"y", 100},
                                                          {"angle", 0}, {"scaleX", 1}, {"scaleY", 1}, {"opacity", 100}, {"frame", 0}});
        app.proj.dirty = true; app.proj.rev++;
    }
    else if (cmd == "school.ready") school::pressReady();
    else if (cmd == "school.auto") { if (!school::autoStep(app.proj)) { printf("S2DEDIT FAIL school.auto: este paso no tiene \"Hazlo por mi\"\n"); app.fails++; } }
    else if (cmd.rfind("sound.", 0) == 0) {
        // Source Sound: sound.studio | sound.close | sound.newproj music|sfx | sound.note paso nota largo | sound.track i inst
        //               sound.preset <nombre del efecto> | sound.export archivo(.wav|.mp3) | sound.play
        std::string a = cmd.substr(6);
        sound::Editor *e = !app.sounds.empty() ? app.sounds.back().get() : g_sound.ed.get();
        if (a == "studio") { openSoundStudio(); e = g_sound.ed.get(); }
        else if (a == "close") { if (g_sound.ed) g_sound.ed->stop(); g_sound.on = false; }
        else if (a == "newproj") { std::string k; in >> k; openSound(newProjectSound(k == "sfx")); }
        else if (!e) { printf("S2DEDIT FAIL %s: no hay Source Sound abierto\n", line.c_str()); app.fails++; }
        else if (a == "note") {
            int st, m, l; in >> st >> m >> l;
            e->song["tracks"][e->track]["notes"].push_back(json::array({st, m, l}));
            if (e->onChanged) e->onChanged();
        }
        else if (a == "track") { std::string inst; in >> e->track >> inst; if (!inst.empty()) e->song["tracks"][e->track]["inst"] = inst; }
        else if (a == "preset") { std::string nm; std::getline(in >> std::ws, nm); e->song["mode"] = "sfx"; e->song["sfx"]["wave"] = nm == "Explosión" ? "noise" : "square"; if (e->onChanged) e->onChanged(); }
        else if (a == "play") e->play();
        else if (a == "save") { std::string f; std::getline(in >> std::ws, f); std::ofstream(f) << e->song.dump(1); refreshCreations(); }
        else if (a == "sfx") { e->song = sound::newSong("Mi efecto", true); }
        else if (a == "load") { std::string f; std::getline(in >> std::ws, f); std::ifstream sin(f); json j = json::parse(sin, nullptr, false); if (j.is_object()) e->song = j; else { printf("S2DEDIT FAIL %s\n", line.c_str()); app.fails++; } }
        else if (a == "export") {
            std::string f, err; in >> f;
            bool ok = f.size() > 4 && f.substr(f.size() - 4) == ".mp3" ? sound::exportMp3(e->song, f, err) : sound::exportWav(e->song, f, err);
            if (!ok) app.fails++;
            printf("S2DEDIT %s sound.export %s %s\n", ok ? "OK" : "FAIL", f.c_str(), err.c_str());
        }
    }
    else if (cmd == "school.note") { std::string w; in >> w; school::note(w); }
    else if (cmd == "school.expectstep") { int k; in >> k; bool ok = school::stepIndex() == k; if (!ok) app.fails++; printf("S2DEDIT %s school.expectstep %d (esta en %d)\n", ok ? "ok" : "FAIL", k, school::stepIndex()); }
    else if (cmd == "expectdef") {
        std::string kind, type; in >> kind >> type;
        refreshExtensionDefs(app.proj);
        bool ok = InstrDefs::get().def(kind, type) != nullptr;
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s\n", ok ? "OK" : "FAIL", line.c_str());
    }
    else if (cmd == "play") { std::string nm; std::getline(in >> std::ws, nm); std::string id; for (auto &sc : app.proj.doc["scenes"]) if (jstr(sc, "name") == nm) id = jstr(sc, "id"); writeTempProject(id); printf("S2DEDIT temp %s/prueba.source2d.json\n", app.tmpDir.c_str()); }
    else if (cmd == "save") { std::string p; in >> p; saveProject(p.empty() ? app.proj.path : p); }   // sin ruta: la del proyecto
    else if (cmd == "export") { std::string p, os = "linux", mode; in >> p >> os >> mode; exportGame(p, os, mode == "ligera", false, mode == "carpeta"); }   // export <salida> [linux|windows|html] [ligera|carpeta]
    else if (cmd == "exportwait") { if (exportsRunning()) app.scriptPos--; }   // espera a que terminen las exportaciones
    else if (cmd.rfind("bar.", 0) == 0) {
        // botones de la barra superior
        std::string a = cmd.substr(4);
        if (a == "undo") ui::requestHistory(-1);
        else if (a == "redo") ui::requestHistory(1);
        else if (a == "scene") focusScene();
        else if (a == "events") openEvents();
        else if (a == "settings") openDoc(app.settings);
        else if (a == "home") app.focusWelcome = true;
        else if (a == "tree") app.showTree = !app.showTree;
        else if (a == "expectkind") {
            std::string k; in >> k;
            bool ok = ui::lastFocusKind() == k;
            if (!ok) app.fails++;
            printf("S2DEDIT %s %s (activo: %s)\n", ok ? "OK" : "FAIL", line.c_str(), ui::lastFocusKind().c_str());
        }
        else { printf("S2DEDIT FAIL boton de la barra desconocido [%s]: %s\n", a.c_str(), line.c_str()); app.fails++; }
    }
    else if (cmd == "home.section") in >> app.homeSection;   // 0 Crear, 1 Prueba
    // noticias (con S2D_NOVEDADES_URL=file://... y S2D_FIRMA_PUBLICA de una clave de prueba)
    else if (cmd == "noticias.espera") { if (novedades::estado() == "Buscando noticias...") app.scriptPos--; }
    else if (cmd == "noticias.expect") {   // noticias.expect <noticias> <versiones> <importante 0|1>
        int n, v, imp; in >> n >> v >> imp;
        int gn = (int)novedades::noticias().size(), gv = (int)novedades::versiones().size(), gi = novedades::importantePendiente() != nullptr;
        bool ok = gn == n && gv == v && gi == imp;
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s -> noticias=%d versiones=%d importante=%d estado=[%s]\n", ok ? "OK" : "FAIL", line.c_str(), gn, gv, gi, novedades::estado().c_str());
    }
    else if (cmd == "encuesta.votar") {   // encuesta.votar <id> <opcion>
        int id, op; in >> id >> op;
        for (auto &x : novedades::noticias()) if (x.id == id) novedades::votar(x, op);
    }
    else if (cmd == "encuesta.expect") {   // encuesta.expect <id> <mi voto> <votos,separados,por,comas>: espera a que lleguen
        int id, mio; std::string want; in >> id >> mio >> want;
        std::string got;
        for (long long x : novedades::votos(id)) got += (got.empty() ? "" : ",") + std::to_string(x);
        if (got != want && app.scriptRetry++ < 600) app.scriptPos--;
        else {
            app.scriptRetry = 0;
            bool ok = got == want && novedades::miVoto(id) == mio;
            if (!ok) app.fails++;
            printf("S2DEDIT %s %s -> voto=%d votos=%s [%s]\n", ok ? "OK" : "FAIL", line.c_str(), novedades::miVoto(id), got.c_str(), novedades::errorEncuesta(id).c_str());
        }
    }
    else if (cmd == "noticias.descargar") {
        std::string nm; std::getline(in >> std::ws, nm);
        for (auto &v : novedades::versiones()) if (v.nombre == nm) novedades::descargar(v);
    }
    else if (cmd == "noticias.instalada") {   // espera a que acabe la descarga y comprueba que quedo bien
        std::string nm; std::getline(in >> std::ws, nm);
        if (novedades::progreso(nm) >= 0) app.scriptPos--;
        else {
            bool ok = novedades::instalada(nm);
            if (!ok) app.fails++;
            printf("S2DEDIT %s %s -> %s\n", ok ? "OK" : "FAIL", line.c_str(), novedades::errorDescarga(nm).c_str());
        }
    }
    else if (cmd == "lang") { std::string l; std::getline(in >> std::ws, l); ui::setLanguage(l); }   // idioma del editor (pruebas)
    else if (cmd == "home.remix") { int i; in >> i; if (i >= 0 && i < (int)exampleGames().size() && !exampleGames()[i].doc.empty()) openExampleCopy(i); }   // "Ver por dentro"
    else if (cmd == "expectsaved") {   // el proyecto abierto tiene archivo y esta guardado
        bool ok = !app.proj.path.empty() && fs::exists(app.proj.path) && !app.proj.dirty;
        if (!ok) app.fails++;
        printf("S2DEDIT %s expectsaved -> %s\n", ok ? "OK" : "FAIL", app.proj.path.c_str());
    }
    else if (cmd == "home.new") { std::string nm; std::getline(in >> std::ws, nm); createProject(nm); refreshProjectList(); }   // como "Crear un proyecto"
    else if (cmd == "shot") { in >> app.shotPath; }
    else if (cmd == "wait") in >> app.scriptWait;
    else if (cmd == "move") in >> rlImGuiScriptX >> rlImGuiScriptY;
    else if (cmd == "perf") { in >> app.perfN; app.perfFrames = app.perfN; app.perfStart = GetTime(); app.perfLast = 0; app.perfMax = 0; }   // raton del guion (no mueve el del sistema)
    else if (cmd == "click") { in >> rlImGuiScriptX >> rlImGuiScriptY; rlImGuiScriptDown = true; app.mouseUpIn = 3; app.scriptWait = 4; }
    else if (cmd == "press") { in >> rlImGuiScriptX >> rlImGuiScriptY; rlImGuiScriptDown = true; app.mouseUpIn = 0; }   // mantener (arrastrar con move)
    else if (cmd == "release") rlImGuiScriptDown = false;
    else if (cmd == "drop.hover") in >> s2d_drag_hover;                               // como si hubiera archivos arrastrandose encima
    else if (cmd == "drop.file") { std::string f; std::getline(in >> std::ws, f); g_dropAt = GetTime(); g_dropName = fs::path(f).filename().string(); s2d_drag_hover = 0; openDroppedFile(f); }
    else if (cmd == "notif.prueba") { std::getline(in >> std::ws, g_notifTest); }      // una notificacion de mentira
    else if (cmd == "expect" && need()) {
        // expect <frame> <x> <y> <color|none>: pixel compuesto
        int f, x, y; std::string c; in >> f >> x >> y >> c;
        std::vector<Px> comp; e->composite(f, comp, false);
        Px want = c == "none" ? Px{} : parseColor(c), got = (x >= 0 && y >= 0 && x < e->st.w && y < e->st.h) ? comp[y * e->st.w + x] : Px{};
        bool ok = want == got || (want.a == 0 && got.a == 0);
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s -> %s\n", ok ? "OK" : "FAIL", line.c_str(), colorToString(got).empty() ? "none" : colorToString(got).c_str());
    }
    else if (cmd == "expectsize" && need()) {
        int w, h, fr, ly; in >> w >> h >> fr >> ly;
        bool ok = e->st.w == w && e->st.h == h && (int)e->st.layers[0].cels.size() == fr && (int)e->st.layers.size() == ly;
        if (!ok) app.fails++;
        printf("S2DEDIT %s %s -> %dx%d frames=%d capas=%d\n", ok ? "OK" : "FAIL", line.c_str(), e->st.w, e->st.h, (int)e->st.layers[0].cels.size(), (int)e->st.layers.size());
    }
    else if (cmd == "quit") {
        if (prefs().projectsDir.find("source2d-pruebas-") != std::string::npos) { std::error_code ec; fs::remove_all(prefs().projectsDir, ec); } printf("S2DEDIT FIN fallos=%d\n", app.fails); app.quit = true; }
    else { printf("S2DEDIT FAIL comando desconocido: %s\n", line.c_str()); app.fails++; }
    fflush(stdout);
}

/* ---------------- principal ---------------- */
int main(int argc, char **argv) {
    std::string openPath, scriptPath;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) { scriptPath = argv[++i]; rlImGuiScriptMouse = true; }
        else if (a == "--help" || a == "-h") { printf("Source 2D - editor\n  source2d [proyecto.source2d.json]\n  --diagnostico  informe de por que no abre (en ~/source2d-diagnostico.txt)\n"); return 0; }
        else if (a == "--vulkan") app.vulkan = true;
        else if (a == "--diagnostico") {
            s2d_diag_begin("editor");
            SetConfigFlags(FLAG_WINDOW_RESIZABLE);
            InitWindow(320, 200, "Source 2D - diagnostico");
            bool ok = IsWindowReady();
            std::string res = ok ? "Ventana y OpenGL: bien. El editor deberia abrir." :
                                   "No se pudo abrir la ventana: falta OpenGL 2.1 (controladores de video) o el servidor grafico (X11).";
            s2d_diag_end(ok, res.c_str());
            if (ok) CloseWindow();
            s2d_message((res + "\n\nEl informe completo quedó en source2d-diagnostico.txt, en tu carpeta personal.").c_str());
            return ok ? 0 : 1;
        }
        else if (a[0] != '-') openPath = a;
    }
    if (!scriptPath.empty()) {
        std::ifstream in(scriptPath);
        std::string l;
        while (std::getline(in, l)) app.script.push_back(l);
    }
    SetTraceLogLevel(LOG_WARNING);
    /* sin MSAA: en graficas viejas pedirlo puede impedir que se abra la ventana (ImGui ya suaviza sus bordes) */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1400, 860, "Source 2D " S2D_VERSION);
    if (!IsWindowReady()) { s2d_window_failed(argv); return 1; }
    ui::setAnimations(prefs().animations && app.script.empty());   // las pruebas, sin animaciones (capturas iguales)
    ui::loadLanguages(exeDir());
    school::onNotify = [](const std::string &m) { notify(m); };
    school::onAddSong = [] { addFloppaSong(); };
    school::saveProgress = app.script.empty();
    school::onNewProject = [](const std::string &nm) {          // la leccion sigue en el proyecto abierto o en uno nuevo
        if (!app.hasProject) { createProject(nm); refreshProjectList(); }
        app.openFirstScene = true;
    };
    if (!prefs().lang.empty()) ui::setLanguage(prefs().lang);
    novedades::iniciar(prefs().news, versionsDir(), !app.script.empty());
    if (app.script.empty()) {
        installThisVersion();
        std::error_code ec;                              // lo que quedo de un cierre de golpe (luz, apagado)
        for (auto &e : fs::directory_iterator(recoveryDir(), ec)) if (e.path().extension() == ".json") app.recoveries.push_back(e.path().string());
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    rlImGuiBeginInitImGui();
    ui::setupFontAndTheme();
    rlImGuiEndInitImGui();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string iniPath = ui::configDir() + "/editor.ini";
    io.IniFilename = app.script.empty() ? iniPath.c_str() : nullptr;
    loadRecent();
    if (!openPath.empty()) openProject(openPath);

    while (!app.quit) {
        if (WindowShouldClose()) app.askClose = true;
        reapChildren();
        musicUpdate();
        BeginDrawing();
        ClearBackground(Color{24, 24, 31, 255});
        rlImGuiBegin();
        thumbsNewFrame();
        fontsNewFrame();
        if (app.mouseUpIn > 0 && --app.mouseUpIn == 0) rlImGuiScriptDown = false;
        autosaveTick();
        if (app.perfFrames > 0) {
            double now = GetTime();
            if (app.perfLast > 0) app.perfMax = std::max(app.perfMax, now - app.perfLast);
            app.perfLast = now;
            if (--app.perfFrames == 0) printf("S2DEDIT perf media %.1f ms, peor %.1f ms\n", (now - app.perfStart) * 1000 / app.perfN, app.perfMax * 1000);
        }
        if (!app.script.empty()) runScriptStep();
        novedades::tick();
        dropFilesTick();
        globalShortcuts();
        drawMenuAndToolbar();
        if (g_sound.on) drawSoundStudio();
        else if (g_studio.on) drawStudio();
        else if (!app.hasProject) drawHome();
        else {
            ImGuiID dock = ImGui::GetID("MainDock2");     // 2: sin la columna del panel (los .ini viejos no la traen)
            if (app.wantLayout || !ImGui::DockBuilderGetNode(dock)) { buildLayout(dock); app.wantLayout = false; }
            ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport());
            if (!app.dockCenter) app.dockCenter = dock;

            drawWelcomeDoc();
            if (app.openFirstScene) {
                app.openFirstScene = false;
                openEvents(false);
                if (!app.proj.doc["scenes"].empty()) openScene(jstr(app.proj.doc["scenes"][0], "id"));
                if (app.eventsFirst) { app.eventsFirst = false; app.focusEventsIn = 4; }   // la escena queda en su pestana
            }
            if (app.focusEventsIn > 0 && --app.focusEventsIn == 0) openEvents(true);
            for (auto &e : app.sheets) {
                if (!e->open) continue;
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                if (app.scrollEventsToEnd) { e->scrollToEnd = true; }
                e->draw(app.proj);
            }
            app.scrollEventsToEnd = false;
            for (auto &e : app.scenes) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            for (auto &e : app.objects) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            for (auto &e : app.tilesets) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            for (auto &e : app.fonts) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            auto drawDoc = [](auto &d) {
                if (!d || !d->open) return;
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                d->draw(app.proj);
            };
            drawDoc(app.settings); drawDoc(app.shaders); drawDoc(app.extensions); drawDoc(app.layersDoc); drawDoc(app.autotiles);
            for (auto &e : app.sounds) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            app.sounds.erase(std::remove_if(app.sounds.begin(), app.sounds.end(), [](auto &e) { return !e->open; }), app.sounds.end());
            refreshExtensionDefs(app.proj);
            for (auto &e : app.sprites) {
                if (app.dockCenter) ImGui::SetNextWindowDockID(app.dockCenter, ImGuiCond_Always);   // nunca flotando
                e->draw(app.proj);
            }
            if (app.showTree) drawProjectPanel();   // por encima de los documentos (tambien de Inicio)
            else closedProjectPanel();
            syncEditors();
            app.sprites.erase(std::remove_if(app.sprites.begin(), app.sprites.end(), [](auto &e) { return !e->open; }), app.sprites.end());
            app.objects.erase(std::remove_if(app.objects.begin(), app.objects.end(), [](auto &e) { return !e->open; }), app.objects.end());
            app.sheets.erase(std::remove_if(app.sheets.begin(), app.sheets.end(), [](auto &e) { return !e->open; }), app.sheets.end());
            app.scenes.erase(std::remove_if(app.scenes.begin(), app.scenes.end(), [](auto &e) { return !e->open; }), app.scenes.end());
            app.tilesets.erase(std::remove_if(app.tilesets.begin(), app.tilesets.end(), [](auto &e) { return !e->open; }), app.tilesets.end());
            app.fonts.erase(std::remove_if(app.fonts.begin(), app.fonts.end(), [](auto &e) { return !e->open; }), app.fonts.end());
        }
        collabTick();
        exportTick();
        sourceExportTick();
        app.fileDlg.draw();
        drawModals();
        drawDeleteCreationModal();
        drawToast();
        drawNewsNotification();
        drawDropOverlay();
        ui::endFrameHistory();
        rlImGuiEnd();
        if (!app.shotPath.empty()) {
            rlDrawRenderBatchActive();
            Image im = LoadImageFromScreen();
            ExportImage(im, app.shotPath.c_str());
            UnloadImage(im);
            printf("S2DEDIT shot %s\n", app.shotPath.c_str());
            app.shotPath.clear();
        }
        std::string vname = std::string(S2D_VERSION) + (app.vulkan ? " (Vulkan)" : "");
        std::string title = app.hasProject ? "Source 2D " + vname + " - " + app.proj.name() + (app.proj.dirty ? " *" : "") : "Source 2D " + vname;
        static std::string lastTitle;
        if (title != lastTitle) { SetWindowTitle(title.c_str()); lastTitle = title; }
        EndDrawing();
    }
    for (auto &kv : app.thumbs) kv.second.release();
    releaseTextures();
    releaseFontCache();
    /* todo lo que tenga texturas se destruye antes de cerrar la ventana (si no, sus destructores las liberan despues de
       CloseWindow y el editor se cae al salir): editores, Source Sprite y documentos del proyecto */
    collab::stop();
    app.tilesets.clear();
    clearRecovery();                                     // cierre normal: no hay nada que recuperar
    musicShutdown();
    app.sprites.clear(); app.objects.clear(); app.scenes.clear(); app.sheets.clear(); app.fonts.clear();
    app.settings.reset(); app.shaders.reset(); app.extensions.reset(); app.layersDoc.reset(); app.autotiles.reset();
    g_studio = SpriteStudio{};
    releaseCreationThumbs();
    sound::clipStop();
    rlImGuiShutdown();
    CloseWindow();
    if (!app.tmpDir.empty()) { std::error_code ec; fs::remove_all(app.tmpDir, ec); }
    return app.fails ? 1 : 0;
}

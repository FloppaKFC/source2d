// Source 2D - utilidades de interfaz: tema, fuente, iconos dibujados y dialogo de archivos propio.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "imgui.h"
#include "raylib.h"
#include "project.hpp"

namespace ui {

// colores del tema: los del tema oscuro por defecto de GDevelop (newIDE, MIT; DefaultDarkTheme/theme.json)
constexpr ImU32 kWindowBg = IM_COL32(0x1D, 0x1D, 0x26, 255);   // fondo de la ventana y lienzos
constexpr ImU32 kPanel = IM_COL32(0x25, 0x25, 0x2E, 255);      // paneles, barras de herramientas, dialogos
constexpr ImU32 kPanel2 = IM_COL32(0x32, 0x32, 0x3B, 255);     // campos de texto, pestanas inactivas
constexpr ImU32 kPanel3 = IM_COL32(0x49, 0x49, 0x52, 255);     // pestana seleccionada, tooltips
constexpr ImU32 kLine = IM_COL32(0x3E, 0x44, 0x52, 255);       // separadores
constexpr ImU32 kTabBorder = IM_COL32(0x7F, 0x7F, 0x85, 255);  // borde de la pestana activa
constexpr ImU32 kText = IM_COL32(0xFA, 0xFA, 0xFA, 255);
constexpr ImU32 kText2 = IM_COL32(0xC5, 0xC5, 0xC9, 255);      // texto secundario
constexpr ImU32 kDim = IM_COL32(0x9A, 0xA1, 0xAD, 255);        // texto desactivado
constexpr ImU32 kPrimary = IM_COL32(0x4F, 0x28, 0xCD, 255);
constexpr ImU32 kPrimaryLight = IM_COL32(0x70, 0x46, 0xEC, 255);
constexpr ImU32 kPrimaryDark = IM_COL32(0x37, 0x23, 0x8F, 255);
constexpr ImU32 kAccent = IM_COL32(0x99, 0x79, 0xF1, 255);     // contornos de seleccion
constexpr ImU32 kSelectedIconBg = IM_COL32(0xC9, 0xB6, 0xFC, 255);
constexpr ImU32 kSecondary = IM_COL32(0xDD, 0xD1, 0xFF, 255);  // texto de botones planos primarios
constexpr ImU32 kHover = IM_COL32(255, 255, 255, 20);

void setupFontAndTheme();             // llamar entre rlImGuiBeginInitImGui y rlImGuiEndInitImGui
ImFont *boldFont();                   // Outfit SemiBold, para titulos y el boton de vista previa
ImFont *gameFont();                   // la fuente del jugador: los textos del juego se previsualizan tal cual
const unsigned char *gameFontTTF(int *len);   // el TTF de esa fuente (para dibujar textos fuera de ImGui)
bool primaryButton(const char *label, ImVec2 size = ImVec2(0, 0));
// boton de borrar que primero pregunta ("¿Borrar <que>?"); devuelve true solo al confirmar
bool deleteButton(const char *label, const std::string &what);
void helpMarker(const char *text);

// iconos vectoriales para botones de herramientas (sin fuentes de iconos)
enum class Icon { Pencil, Eraser, Bucket, Picker, Line, Rect, Ellipse, Select, Move, Hand, Play, Stop, Plus, Minus,
                  Copy, Trash, Eye, EyeOff, Lock, Unlock, Up, Down, Onion, Grid, MirrorX, MirrorY, Prev, Next, Folder, Save, Undo, Redo, Home, Image,
                  Menu, Scene, Events, Settings, Export, Object, Hammer, Book, Create, Gamepad, Delete, Lasso, Wand, Crop, Close, People,
                  Spray, Gradient, Contour, SelEllipse, Bell, Download };
void drawIcon(ImDrawList *dl, Icon ic, ImVec2 center, float size, ImU32 col);
// idiomas del editor: archivos <carpeta>/Idiomas/<Nombre>.txt con lineas "texto original = traduccion"
void loadLanguages(const std::string &exeDir);       // al arrancar: donde buscar la carpeta Idiomas
std::vector<std::string> languages();                // nombres (siempre "Español" primero)
void setLanguage(const std::string &name);           // "" o "Español" = sin traducir
const std::string &tr(const std::string &s);         // traduccion de un texto (o el mismo)
// animaciones suaves del editor (Opciones): botones que cambian de color poco a poco, ventanas que entran deslizandose,
// secciones con fundido y rueda suave
void setAnimations(bool on);
bool animations();
float animTo(ImGuiID id, float target, float speed = 14.0f);   // valor que va hacia target (por elemento)
// ventana emergente (modal) siempre en el centro de la pantalla y sin poder moverla
bool beginModal(const char *name, bool *open = nullptr, ImGuiWindowFlags flags = 0);
bool iconButton(const char *id, Icon ic, bool active = false, const char *tooltip = nullptr, float size = 30.0f);

// la barra superior necesita saber que editor se uso por ultimo (para resaltar su boton y para
// mandarle deshacer/rehacer). Cada editor llama a noteFocus dentro de su ventana en cada fotograma.
void noteFocus(const char *kind, const std::string &key = "");
const std::string &lastFocusKind();
const std::string &lastFocusKey(const char *kind);
void requestHistory(int dir);         // -1 deshacer, +1 rehacer
int takeHistoryRequest();             // llamar justo despues de noteFocus: -1/+1 si la peticion es para este editor
void endFrameHistory();               // al final del fotograma descarta peticiones que nadie atendio

// textura de raylib a partir de pixeles RGBA (se reutiliza si el tamano no cambia)
struct PixTexture {
    Texture2D tex{};
    int w = 0, h = 0;
    void update(const std::vector<Px> &px, int w, int h);
    void release();
    ImTextureID id() const { return (ImTextureID)(intptr_t)tex.id; }
};
void checkerboard(ImDrawList *dl, ImVec2 a, ImVec2 b, float cell);

// dialogo de archivos dentro del editor (no usa GTK ni el sistema)
struct FileDialog {
    enum Mode { Open, Save, Folder };
    bool open = false;
    Mode mode = Open;
    std::string title, dir, filename, ext, error;
    std::function<void(const std::string &)> onOk;
    void show(Mode m, const std::string &title, const std::string &ext, const std::string &startDir,
              const std::string &suggested, std::function<void(const std::string &)> cb);
    void draw();
};

std::string homeDir();
std::string configDir();                                    // ~/.config/source2d
std::string documentsDir();                                 // carpeta Documentos del usuario

} // namespace ui

// Source 2D - lo que cambia entre Linux y Windows en el editor: procesos, carpeta temporal, permisos y fechas.
#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace sys {
struct Proc { long long h = 0; };                                    // proceso lanzado (pid o HANDLE)
bool spawn(const std::vector<std::string> &args, Proc *out, bool hidden = false); // sin esperar (hidden: sin consola en Windows)
int run(const std::vector<std::string> &args);
// como spawn, pero lo que hay en input le llega al programa por la entrada estandar (y no toca el disco)
bool spawnFeed(const std::vector<std::string> &args, const std::string &input, Proc *out);
bool randomBytes(uint8_t *b, size_t n);                               // del sistema (getrandom / BCryptGenRandom)                       // espera; devuelve el codigo de salida (-1 si falla)
bool finished(Proc &p, int *code = nullptr);                         // true si ya termino (y lo recoge); code: su codigo de salida
std::string makeTempDir();                                           // carpeta temporal nueva
void setExecutable(const std::string &path);                         // chmod 755 (nada en Windows)
void localTime(time_t t, tm &out);
time_t fileMTime(const std::string &path);                           // 0 si no existe
int processId();
bool processAlive(long long pid);                                    // ese proceso sigue abierto
const char *exeSuffix();                                             // ".exe" en Windows, "" en Linux
// escribe el archivo entero en uno temporal, lo fuerza al disco (fsync) y lo cambia por el de verdad: si se va la
// luz a medias queda el anterior o el nuevo, nunca uno roto
bool writeDurable(const std::string &path, const std::string &data);
}

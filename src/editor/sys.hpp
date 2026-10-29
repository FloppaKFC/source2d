#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace sys {
struct Proc { long long h = 0; };
bool spawn(const std::vector<std::string> &args, Proc *out, bool hidden = false);
int run(const std::vector<std::string> &args);
bool spawnFeed(const std::vector<std::string> &args, const std::string &input, Proc *out);
bool randomBytes(uint8_t *b, size_t n);
bool finished(Proc &p, int *code = nullptr);
std::string makeTempDir();
void setExecutable(const std::string &path);
void localTime(time_t t, tm &out);
time_t fileMTime(const std::string &path);
int processId();
bool processAlive(long long pid);
const char *exeSuffix();
bool writeDurable(const std::string &path, const std::string &data);
}

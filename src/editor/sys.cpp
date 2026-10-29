#include "sys.hpp"
#include <cstdio>
#include <filesystem>
#include <random>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <process.h>
#include <io.h>
#else
#include <spawn.h>
#include <sys/random.h>
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace sys {
#ifdef _WIN32
// linea de ordenes de Windows: cada argumento entre comillas (con las comillas internas escapadas)
static std::wstring cmdLine(const std::vector<std::string> &args) {
    std::string s;
    for (auto &a : args) {
        if (!s.empty()) s += ' ';
        s += '"';
        for (char c : a) { if (c == '"') s += '\\'; s += c; }
        s += '"';
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
static bool create(const std::vector<std::string> &args, PROCESS_INFORMATION &pi, DWORD flags = 0) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    std::wstring cl = cmdLine(args);
    return CreateProcessW(nullptr, cl.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi) != 0;
}
bool spawn(const std::vector<std::string> &args, Proc *out, bool hidden) {
    PROCESS_INFORMATION pi{};
    if (!create(args, pi, hidden ? CREATE_NO_WINDOW : 0)) return false;
    CloseHandle(pi.hThread);
    if (out) out->h = (long long)(intptr_t)pi.hProcess; else CloseHandle(pi.hProcess);
    return true;
}
bool spawnFeed(const std::vector<std::string> &args, const std::string &input, Proc *out) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rd;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::wstring cl = cmdLine(args);
    PROCESS_INFORMATION pi{};
    bool ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi) != 0;
    CloseHandle(rd);
    if (!ok) { CloseHandle(wr); return false; }
    CloseHandle(pi.hThread);
    if (out) out->h = (long long)(intptr_t)pi.hProcess; else CloseHandle(pi.hProcess);
    std::thread([wr, input] {
        size_t pos = 0;
        while (pos < input.size()) {
            DWORD n = 0, want = (DWORD)std::min<size_t>(input.size() - pos, 1 << 20);
            if (!WriteFile(wr, input.data() + pos, want, &n, nullptr) || !n) break;
            pos += n;
        }
        CloseHandle(wr);
    }).detach();
    return true;
}
bool randomBytes(uint8_t *b, size_t n) {
    return BCryptGenRandom(nullptr, b, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
int run(const std::vector<std::string> &args) {
    PROCESS_INFORMATION pi{};
    if (!create(args, pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}
bool finished(Proc &p, int *code) {
    HANDLE h = (HANDLE)(intptr_t)p.h;
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return false;
    DWORD c = 1;
    if (code) { GetExitCodeProcess(h, &c); *code = (int)c; }
    CloseHandle(h);
    return true;
}
void setExecutable(const std::string &) {}
void localTime(time_t t, tm &out) { localtime_s(&out, &t); }
int processId() { return (int)GetCurrentProcessId(); }
bool processAlive(long long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
bool writeDurable(const std::string &path, const std::string &data) {
    std::string tmp = path + ".tmp";
    FILE *f = _wfopen(std::filesystem::u8path(tmp).wstring().c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size() && fflush(f) == 0 && _commit(_fileno(f)) == 0;
    fclose(f);
    if (!ok) return false;
    return MoveFileExW(std::filesystem::u8path(tmp).wstring().c_str(), std::filesystem::u8path(path).wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
const char *exeSuffix() { return ".exe"; }
#else
static std::vector<char *> argvOf(const std::vector<std::string> &args) {
    std::vector<char *> v;
    for (auto &a : args) v.push_back((char *)a.c_str());
    v.push_back(nullptr);
    return v;
}
bool spawn(const std::vector<std::string> &args, Proc *out, bool) {
    pid_t pid;
    auto v = argvOf(args);
    if (posix_spawnp(&pid, v[0], nullptr, nullptr, v.data(), environ) != 0) return false;
    if (out) out->h = pid;
    return true;
}
bool spawnFeed(const std::vector<std::string> &args, const std::string &input, Proc *out) {
    static bool sigOnce = (signal(SIGPIPE, SIG_IGN), true);   // si el juego se cierra antes de leerlo todo
    (void)sigOnce;
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) != 0) return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[0], 0);
    pid_t pid;
    auto v = argvOf(args);
    int r = posix_spawnp(&pid, v[0], &fa, nullptr, v.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    if (r != 0) { close(fd[1]); return false; }
    if (out) out->h = pid;
    int w = fd[1];
    std::thread([w, input] {
        size_t pos = 0;
        while (pos < input.size()) {
            ssize_t n = write(w, input.data() + pos, input.size() - pos);
            if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
            pos += (size_t)n;
        }
        close(w);
    }).detach();
    return true;
}
bool randomBytes(uint8_t *b, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom(b + got, n - got, 0);
        if (r < 0) { if (errno == EINTR) continue; return false; }
        got += (size_t)r;
    }
    return true;
}
int run(const std::vector<std::string> &args) {
    pid_t pid;
    auto v = argvOf(args);
    if (posix_spawnp(&pid, v[0], nullptr, nullptr, v.data(), environ) != 0) return -1;
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
bool finished(Proc &p, int *code) {
    int st;
    if (waitpid((pid_t)p.h, &st, WNOHANG) != (pid_t)p.h) return false;
    if (code) *code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return true;
}
void setExecutable(const std::string &path) { chmod(path.c_str(), 0755); }
void localTime(time_t t, tm &out) { localtime_r(&t, &out); }
int processId() { return (int)getpid(); }
bool processAlive(long long pid) { return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM); }
bool writeDurable(const std::string &path, const std::string &data) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size() && fflush(f) == 0 && fsync(fileno(f)) == 0;
    fclose(f);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) return false;
    std::string dir = std::filesystem::path(path).parent_path().string();   // tambien la carpeta (el cambio de nombre)
    int d = open(dir.empty() ? "." : dir.c_str(), O_RDONLY);
    if (d >= 0) { fsync(d); close(d); }
    return true;
}
const char *exeSuffix() { return ""; }
#endif

std::string makeTempDir() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    std::random_device rd;
    for (int i = 0; i < 100; i++) {
        fs::path d = base / ("source2d-" + std::to_string(rd() % 1000000000u));
        if (fs::create_directory(d, ec)) return d.string();
    }
    return "";
}

time_t fileMTime(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}
}  // namespace sys

#pragma once
#include <cstdlib>
#include <string>

namespace servidor {
inline const char *kR2 = "https://pub-45b3e9e445dc4ba8bc581524cfc68bf0.r2.dev/";
inline const char *kApi = "https://source2d-api.acontrerasvalensia.workers.dev";

inline std::string r2() {
    if (const char *e = getenv("S2D_R2_URL")) {
        std::string s = e;
        if (!s.empty() && s.back() != '/') s += '/';
        return s;
    }
    return kR2;
}
inline std::string api() {
    if (const char *e = getenv("S2D_API_URL")) {
        std::string s = e;
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    return kApi;
}
inline bool urlDeR2(const std::string &url) {
    std::string base = r2();
    return url.rfind(base, 0) == 0 && url.find("..") == std::string::npos;
}
}

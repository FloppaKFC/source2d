#include "cerrado.hpp"
#include "sys.hpp"
#include "../../third_party/monocypher/monocypher.h"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cerrado {
namespace {
const char kPlay[] = "S2DPLAY1", kLock[] = "S2DLOCK1";
const uint8_t kAd[] = "source2d";
bool fromHex(const std::string &s, uint8_t *b, size_t n) {
    if (s.size() != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        char t[3] = {s[2 * i], s[2 * i + 1], 0}, *end;
        long v = strtol(t, &end, 16);
        if (*end) return false;
        b[i] = (uint8_t)v;
    }
    return true;
}
std::string toHex(const uint8_t *b, size_t n) {
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
    return s;
}
std::string seal(const char *magic, const uint8_t *extra, size_t extraN, const uint8_t key[32], const std::string &plain) {
    uint8_t nonce[24], mac[16];
    if (!sys::randomBytes(nonce, 24)) return "";
    std::string out(magic, 8);
    out.append((const char *)extra, extraN);
    out.append((const char *)nonce, 24);
    size_t at = out.size();
    out.append(16, '\0');
    out.append(plain.size(), '\0');
    crypto_aead_lock((uint8_t *)&out[at + 16], mac, key, nonce, kAd, sizeof kAd - 1, (const uint8_t *)plain.data(), plain.size());
    memcpy(&out[at], mac, 16);
    return out;
}
bool open(const std::string &data, size_t extraN, const uint8_t key[32], std::string &plain) {
    size_t head = 8 + extraN + 24 + 16;
    if (data.size() < head) return false;
    const uint8_t *nonce = (const uint8_t *)data.data() + 8 + extraN, *mac = nonce + 24;
    std::string p(data.size() - head, '\0');
    if (crypto_aead_unlock((uint8_t *)p.data(), mac, key, nonce, kAd, sizeof kAd - 1, (const uint8_t *)data.data() + head, p.size()) != 0) {
        crypto_wipe(p.data(), p.size());
        return false;
    }
    plain = std::move(p);
    return true;
}
bool derive(const std::string &pass, const uint8_t salt[16], uint8_t key[32]) {
    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 65536, 3, 1};
    void *work = malloc((size_t)cfg.nb_blocks * 1024);
    if (!work) return false;
    crypto_argon2_inputs in = {(const uint8_t *)pass.data(), salt, (uint32_t)pass.size(), 16};
    crypto_argon2(key, 32, work, cfg, in, crypto_argon2_no_extras);
    free(work);
    return true;
}
void stripComments(json &a) {
    if (a.is_array()) {
        json keep = json::array();
        for (auto &e : a) {
            if (e.is_object() && e.value("kind", "") == "comment") continue;
            stripComments(e);
            keep.push_back(std::move(e));
        }
        a = std::move(keep);
    } else if (a.is_object()) {
        for (auto &kv : a.items()) stripComments(kv.value());
    }
}
}

bool claveValida(const std::string &pass, std::string &err) {
    size_t n = 0;
    for (unsigned char c : pass) if ((c & 0xC0) != 0x80) n++;
    if (n < kMinClave) { err = "La clave necesita al menos 10 caracteres"; return false; }
    return true;
}

std::string lockPlay(const std::string &plain, std::string &keyHex) {
    uint8_t key[32];
    if (!sys::randomBytes(key, 32)) return "";
    std::string out = seal(kPlay, nullptr, 0, key, plain);
    keyHex = out.empty() ? "" : toHex(key, 32);
    crypto_wipe(key, 32);
    return out;
}
bool unlockPlay(const std::string &data, const std::string &keyHex, std::string &plain) {
    uint8_t key[32];
    if (!isPlay(data) || !fromHex(keyHex, key, 32)) return false;
    bool ok = open(data, 0, key, plain);
    crypto_wipe(key, 32);
    return ok;
}
bool isPlay(const std::string &data) { return data.compare(0, 8, kPlay) == 0; }

std::string lockSource(const std::string &plain, const std::string &pass) {
    uint8_t salt[16], key[32];
    if (!sys::randomBytes(salt, 16) || !derive(pass, salt, key)) return "";
    std::string out = seal(kLock, salt, 16, key, plain);
    crypto_wipe(key, 32);
    return out;
}
bool unlockSource(const std::string &data, const std::string &pass, std::string &plain) {
    if (!isLocked(data) || data.size() < 8 + 16) return false;
    uint8_t key[32];
    if (!derive(pass, (const uint8_t *)data.data() + 8, key)) return false;
    bool ok = open(data, 16, key, plain);
    crypto_wipe(key, 32);
    return ok;
}
bool isLocked(const std::string &data) { return data.compare(0, 8, kLock) == 0; }

json playCopy(const json &doc) {
    json d = doc;
    if (d.contains("sprites") && d["sprites"].is_array())
        for (auto &sp : d["sprites"]) if (sp.is_object()) sp.erase("layers");
    for (const char *k : {"events", "scenes", "objects", "functions"}) if (d.contains(k)) stripComments(d[k]);
    return d;
}
}

#include "cifra.hpp"
#include "sys.hpp"
#include "ui.hpp"
#include "../../third_party/monocypher/monocypher.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace cifra {
namespace {
const char kMagia[4] = {'S', '2', 'M', '1'};
const size_t kCabecera = 4 + 24 + 32 + 16 + 24 + 16;

std::string archivoClave(const std::string &login) {
    std::string l = login;
    for (auto &c : l) c = (char)tolower((unsigned char)c);
    return ui::configDir() + "/claves/" + l + ".clave";
}
bool clavePublicaValida(const std::string &b64, uint8_t out[32]) {
    std::string b;
    if (!deBase64(b64, b) || b.size() != 32) return false;
    memcpy(out, b.data(), 32);
    return true;
}
void llaveComun(const uint8_t sk[32], const uint8_t pk[32], const uint8_t su[32], uint8_t llave[32]) {
    uint8_t compartido[32], mezcla[96];
    crypto_x25519(compartido, sk, su);
    bool menor = memcmp(pk, su, 32) < 0;
    memcpy(mezcla, compartido, 32);
    memcpy(mezcla + 32, menor ? pk : su, 32);
    memcpy(mezcla + 64, menor ? su : pk, 32);
    crypto_blake2b(llave, 32, mezcla, sizeof mezcla);
    crypto_wipe(compartido, sizeof compartido);
    crypto_wipe(mezcla, sizeof mezcla);
}
bool partes(const std::string &blob64, std::string &b) {
    return deBase64(blob64, b) && b.size() >= kCabecera && memcmp(b.data(), kMagia, 4) == 0;
}
bool abrirCuerpo(const std::string &b, const uint8_t kMsg[32], std::string &texto) {
    const uint8_t *p = (const uint8_t *)b.data() + 4 + 24 + 32 + 16;
    size_t n = b.size() - kCabecera;
    std::string plano(n, '\0');
    if (crypto_aead_unlock((uint8_t *)plano.data(), p + 24, kMsg, p, nullptr, 0, p + 40, n) != 0) return false;
    texto.swap(plano);
    return true;
}
}

std::string base64(const std::string &in) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0);
        o += t[v >> 18]; o += t[(v >> 12) & 63];
        o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        o += '=';
    }
    return o;
}
bool deBase64(const std::string &s, std::string &out) {
    out.clear();
    if (s.size() % 4) return false;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 4) {
        int a = val(s[i]), b = val(s[i + 1]), c = s[i + 2] == '=' ? 0 : val(s[i + 2]), d = s[i + 3] == '=' ? 0 : val(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        unsigned v = (unsigned)a << 18 | (unsigned)b << 12 | (unsigned)c << 6 | (unsigned)d;
        out += (char)(v >> 16);
        if (s[i + 2] != '=') out += (char)((v >> 8) & 255);
        if (s[i + 3] != '=') out += (char)(v & 255);
    }
    return true;
}

bool claves(const std::string &login, std::string &secreta, std::string &publica) {
    if (login.empty()) return false;
    std::string f = archivoClave(login);
    std::ifstream in(fs::u8path(f), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    secreta = ss.str();
    if (secreta.size() != 32) {
        uint8_t sk[32];
        if (!sys::randomBytes(sk, 32)) return false;
        secreta.assign((const char *)sk, 32);
        crypto_wipe(sk, 32);
        std::error_code ec;
        fs::create_directories(fs::u8path(f).parent_path(), ec);
        fs::permissions(fs::u8path(f).parent_path(), fs::perms::owner_all, fs::perm_options::replace, ec);
        if (!sys::writeDurable(f, secreta)) return false;
        fs::permissions(fs::u8path(f), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    }
    uint8_t pk[32];
    crypto_x25519_public_key(pk, (const uint8_t *)secreta.data());
    publica.assign((const char *)pk, 32);
    return true;
}
std::string publica64(const std::string &login) {
    std::string sk, pk;
    if (!claves(login, sk, pk)) return "";
    crypto_wipe(sk.data(), sk.size());
    return base64(pk);
}
std::string cifrar(const std::string &login, const std::string &suPublica64, const std::string &texto, std::string &llave64) {
    std::string sk, pk;
    uint8_t su[32];
    if (!clavePublicaValida(suPublica64, su) || !claves(login, sk, pk)) return "";
    uint8_t llave[32], kMsg[32], n1[24], n2[24];
    if (!sys::randomBytes(kMsg, 32) || !sys::randomBytes(n1, 24) || !sys::randomBytes(n2, 24)) return "";
    llaveComun((const uint8_t *)sk.data(), (const uint8_t *)pk.data(), su, llave);
    std::string b(kCabecera + texto.size(), '\0');
    uint8_t *p = (uint8_t *)b.data();
    memcpy(p, kMagia, 4);
    memcpy(p + 4, n2, 24);
    crypto_aead_lock(p + 28, p + 60, llave, n2, nullptr, 0, kMsg, 32);
    memcpy(p + 76, n1, 24);
    crypto_aead_lock(p + kCabecera, p + 100, kMsg, n1, nullptr, 0, (const uint8_t *)texto.data(), texto.size());
    llave64 = base64(std::string((const char *)kMsg, 32));
    crypto_wipe(llave, 32); crypto_wipe(kMsg, 32); crypto_wipe(sk.data(), sk.size());
    return base64(b);
}
bool descifrar(const std::string &login, const std::string &suPublica64, const std::string &blob64, std::string &texto, std::string &llave64) {
    std::string sk, pk, b;
    uint8_t su[32];
    if (!partes(blob64, b) || !clavePublicaValida(suPublica64, su) || !claves(login, sk, pk)) return false;
    uint8_t llave[32], kMsg[32];
    llaveComun((const uint8_t *)sk.data(), (const uint8_t *)pk.data(), su, llave);
    crypto_wipe(sk.data(), sk.size());
    const uint8_t *p = (const uint8_t *)b.data();
    bool ok = crypto_aead_unlock(kMsg, p + 60, llave, p + 4, nullptr, 0, p + 28, 32) == 0 && abrirCuerpo(b, kMsg, texto);
    if (ok) llave64 = base64(std::string((const char *)kMsg, 32));
    crypto_wipe(llave, 32); crypto_wipe(kMsg, 32);
    return ok;
}
bool abrirConLlave(const std::string &blob64, const std::string &llave64, std::string &texto) {
    std::string b, k;
    if (!partes(blob64, b) || !deBase64(llave64, k) || k.size() != 32) return false;
    return abrirCuerpo(b, (const uint8_t *)k.data(), texto);
}
}

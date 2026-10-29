#include "monocypher.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define AUTOR "@floppa-1550"

static void die(const char *m) { fprintf(stderr, "firma: %s\n", m); exit(1); }

static void randomBytes(uint8_t *b, size_t n) {
    while (n) {
        ssize_t r = getrandom(b, n, 0);
        if (r < 0) { if (errno == EINTR) continue; die("no se pudo leer el generador aleatorio del sistema"); }
        b += r; n -= (size_t)r;
    }
}

static void toHex(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}
static int fromHex(const char *s, uint8_t *b, size_t n) {
    if (strlen(s) != 2 * n) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        b[i] = (uint8_t)v;
    }
    return 1;
}

static char g_dir[1024];
static const char *keyDir(void) {
    const char *d = getenv("S2D_FIRMA_DIR");
    if (d && *d) snprintf(g_dir, sizeof g_dir, "%s", d);
    else {
        const char *h = getenv("HOME");
        if (!h) die("no se sabe cual es tu carpeta personal (HOME)");
        snprintf(g_dir, sizeof g_dir, "%s/.config/source2d-firma", h);
    }
    return g_dir;
}
static void keyPath(char *out, size_t n) { snprintf(out, n, "%s/clave", keyDir()); }

static void askPassword(const char *prompt, char *out, size_t n) {
    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) die("hace falta una terminal para escribir la contrasena");
    struct termios old, quiet;
    tcgetattr(fileno(tty), &old);
    quiet = old;
    quiet.c_lflag &= ~(tcflag_t)ECHO;
    fprintf(tty, "%s", prompt);
    fflush(tty);
    tcsetattr(fileno(tty), TCSAFLUSH, &quiet);
    if (!fgets(out, (int)n, tty)) out[0] = 0;
    tcsetattr(fileno(tty), TCSAFLUSH, &old);
    fprintf(tty, "\n");
    fclose(tty);
    out[strcspn(out, "\r\n")] = 0;
}

static void deriveKey(const char *pass, const uint8_t salt[16], uint8_t key[32]) {
    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 65536, 3, 1};
    void *work = malloc((size_t)cfg.nb_blocks * 1024);
    if (!work) die("no hay memoria para Argon2 (64 MB)");
    crypto_argon2_inputs in = {(const uint8_t *)pass, salt, (uint32_t)strlen(pass), 16};
    crypto_argon2(key, 32, work, cfg, in, crypto_argon2_no_extras);
    crypto_wipe(work, (size_t)cfg.nb_blocks * 1024);
    free(work);
}

typedef struct { uint8_t pub[32], salt[16], nonce[24], mac[16], enc[64]; } KeyFile;

static int readKeyFile(KeyFile *k) {
    char p[1100], line[512], name[32], val[256];
    keyPath(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int got = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%31s %255s", name, val) != 2) continue;
        if (!strcmp(name, "publica")) got += fromHex(val, k->pub, 32);
        else if (!strcmp(name, "sal")) got += fromHex(val, k->salt, 16);
        else if (!strcmp(name, "nonce")) got += fromHex(val, k->nonce, 24);
        else if (!strcmp(name, "mac")) got += fromHex(val, k->mac, 16);
        else if (!strcmp(name, "cifrada")) got += fromHex(val, k->enc, 64);
    }
    fclose(f);
    if (got != 5) die("el archivo de la clave esta roto");
    return 1;
}

static void cmdNueva(void) {
    KeyFile k;
    char p[1100];
    keyPath(p, sizeof p);
    if (readKeyFile(&k)) die("ya tienes una clave; si la cambias, los editores que ya estan repartidos dejaran de aceptar tus anuncios");
    char a[256], b[256];
    askPassword("Contrasena nueva para la clave (minimo 10 letras): ", a, sizeof a);
    if (strlen(a) < 10) die("la contrasena es demasiado corta");
    askPassword("Repitela: ", b, sizeof b);
    if (strcmp(a, b)) die("no coinciden");
    uint8_t seed[32], secret[64], key[32];
    randomBytes(seed, 32);
    crypto_eddsa_key_pair(secret, k.pub, seed);
    randomBytes(k.salt, 16);
    randomBytes(k.nonce, 24);
    deriveKey(a, k.salt, key);
    crypto_aead_lock(k.enc, k.mac, key, k.nonce, (const uint8_t *)AUTOR, strlen(AUTOR), secret, 64);
    crypto_wipe(secret, 64); crypto_wipe(key, 32); crypto_wipe(a, sizeof a); crypto_wipe(b, sizeof b);
    mkdir(keyDir(), 0700);
    FILE *f = fopen(p, "w");
    if (!f) die("no se pudo guardar la clave");
    chmod(p, 0600);
    char h[129];
    fprintf(f, "source2d-firma 1\nautor %s\n", AUTOR);
    toHex(k.pub, 32, h); fprintf(f, "publica %s\n", h);
    toHex(k.salt, 16, h); fprintf(f, "sal %s\n", h);
    toHex(k.nonce, 24, h); fprintf(f, "nonce %s\n", h);
    toHex(k.mac, 16, h); fprintf(f, "mac %s\n", h);
    toHex(k.enc, 64, h); fprintf(f, "cifrada %s\n", h);
    fclose(f);
    toHex(k.pub, 32, h);
    printf("Clave creada en %s\nPublica: %s\nHaz una copia de esa carpeta en un USB: si la pierdes no podras publicar mas.\n", p, h);
}

static uint8_t *readFile(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz > 0 ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static void cmdFirmar(int argc, char **argv) {
    KeyFile k;
    if (!readKeyFile(&k)) die("todavia no tienes clave: usa 'firma nueva'");
    char pass[256];
    const char *env = getenv("S2D_FIRMA_CONTRASENA");
    if (env) snprintf(pass, sizeof pass, "%s", env); else askPassword("Contrasena de la clave de " AUTOR ": ", pass, sizeof pass);
    uint8_t key[32], secret[64];
    deriveKey(pass, k.salt, key);
    crypto_wipe(pass, sizeof pass);
    int bad = crypto_aead_unlock(secret, k.mac, key, k.nonce, (const uint8_t *)AUTOR, strlen(AUTOR), k.enc, 64);
    crypto_wipe(key, 32);
    if (bad) die("contrasena incorrecta");
    for (int i = 0; i < argc; i++) {
        size_t n;
        uint8_t *msg = readFile(argv[i], &n), sig[64];
        if (!msg) { crypto_wipe(secret, 64); die("no se pudo leer un archivo"); }
        crypto_eddsa_sign(sig, secret, msg, n);
        free(msg);
        char out[1100], h[129];
        snprintf(out, sizeof out, "%s.firma", argv[i]);
        FILE *f = fopen(out, "w");
        if (!f) { crypto_wipe(secret, 64); die("no se pudo escribir la firma"); }
        toHex(sig, 64, h);
        fprintf(f, "%s\n", h);
        fclose(f);
        printf("firmado: %s\n", argv[i]);
    }
    crypto_wipe(secret, 64);
}

static int cmdVerificar(const char *path) {
    KeyFile k;
    if (!readKeyFile(&k)) die("todavia no tienes clave: usa 'firma nueva'");
    size_t n, sn;
    char sp[1100];
    snprintf(sp, sizeof sp, "%s.firma", path);
    uint8_t *msg = readFile(path, &n), *s = readFile(sp, &sn), sig[64];
    if (!msg || !s) die("falta el archivo o su firma");
    char hex[129] = {0};
    memcpy(hex, s, sn < 128 ? sn : 128);
    int ok = fromHex(hex, sig, 64) && crypto_eddsa_check(sig, k.pub, msg, n) == 0;
    printf("%s: %s\n", path, ok ? "firma correcta" : "FIRMA FALSA");
    return ok ? 0 : 2;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "nueva")) { cmdNueva(); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "publica")) {
        KeyFile k;
        if (!readKeyFile(&k)) die("todavia no tienes clave: usa 'firma nueva'");
        char h[65];
        toHex(k.pub, 32, h);
        printf("%s\n", h);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "firmar")) { cmdFirmar(argc - 2, argv + 2); return 0; }
    if (argc == 3 && !strcmp(argv[1], "verificar")) return cmdVerificar(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "hash")) {
        size_t n;
        uint8_t *b = readFile(argv[2], &n), h[32];
        if (!b) die("no se pudo leer el archivo");
        crypto_blake2b(h, 32, b, n);
        char x[65];
        toHex(h, 32, x);
        printf("%s\n", x);
        return 0;
    }
    fprintf(stderr, "uso: firma nueva | publica | firmar <archivo>... | verificar <archivo> | hash <archivo>\n");
    return 1;
}

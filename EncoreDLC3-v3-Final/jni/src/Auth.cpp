#include "Esp.h"
#include "Auth.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <atomic>

// ========== АВТОРИЗАЦИЯ ОТКЛЮЧЕНА — ключ "saveliy" всегда проходит.
// Никаких запросов на сервер, никакой сети, не нужен интернет вообще.
// Любой ввод >3 символов засчитывается как ОК; ключ "saveliy" работает сразу.

namespace auth {

enum Status { UNKNOWN, NO_KEY, BAD_KEY, NET_ERROR, OK };

static std::atomic<Status> g_status{UNKNOWN};
static std::mutex g_mu;
static std::string g_key;

void init() {
    g_status = OK;  // с первого запуска всё активировано
    printf("[auth] off, any key accepted (saveliy = all unlocked)\n");
    fflush(stdout);
}

void tick() {
    // Ничего не делаем — всегда ОК.
    if (g_status.load() != OK) g_status = OK;
}

Status status() { return OK; }
const char* statusText() { return "Активирован (saveliy)"; }
bool needsInput() { return false; }  // не просим ввода

void setKey(const std::string& k) {
    if (k.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_key = k;
    g_status = OK;
}

std::string getKey() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_key.empty() ? std::string("saveliy") : g_key;
}

std::string hwid() { return "saveliy-hwid"; }
void setServerUrl(const std::string&) {}
std::string getServerUrl() { return "local"; }

} // namespace auth

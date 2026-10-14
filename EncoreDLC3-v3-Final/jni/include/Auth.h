#pragma once
// Auth.h — HWID + проверка ключа через HTTP.
// Чит при первом запуске запрашивает ключ у пользователя, сохраняет его в
// /data/local/tmp/67/key.txt, привязывает к HWID, и затем каждые N минут
// проверяет статус на сервере. При невалидном ключе функционал отключается.
#include <string>

namespace auth {

// Результат последней проверки.
enum Status {
    UNKNOWN = 0,   // ещё не проверяли
    OK,            // ключ валиден, можно пользоваться
    NO_KEY,        // нет ключа — показать ввод
    BAD_KEY,       // неверный ключ / забанен / истёк / чужой HWID
    NET_ERROR      // нет сети — временно пропускаем (чтобы не отрубать в оффлайне кратковременно)
};

// Инициализация и тик: вызывается из main-цикла раз в ~1с. Делает сетевые запросы
// в отдельном потоке и не блокирует рендер.
void init();
void tick();

Status status();
const char* statusText();

// Показать ли диалог ввода ключа в меню (если нет ключа или он невалиден).
bool needsInput();

// Установить/сменить ключ (из меню или из cmd.txt).
void setKey(const std::string& key);
std::string getKey();

// HWID устройства (вычисляется один раз и кэшируется).
std::string hwid();

// URL бэкенда (можно переопределить в cmd.txt: auth_url https://…).
void setServerUrl(const std::string& url);
std::string getServerUrl();

} // namespace auth

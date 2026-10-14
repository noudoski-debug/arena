#pragma once
// Bind.h — внешний монитор клавиш/кнопок мыши (только чтение /dev/input).
// ПКМ по строке Aim в меню — захват бинда; аим работает пока бинд зажат.
namespace bind {
// Вызывать каждый кадр до menu::render (кормит ПКМ в ImGui).
void poll();
// Зажата ли кнопка/клавиша с evdev-кодом (0 = нет бинда -> true).
bool isDown(int code);
// Захват: первый нажатый код с прошлого вызова (BTN_LEFT/TOUCH игнорятся). -1 если нет.
int consumePress();
// True for the configured menu toggle keys (F10 or Insert).
bool consumeMenuToggle();
// Имя кода для бейджа ("RMB", "F", "Space"...).
const char* keyName(int code);
} // namespace bind

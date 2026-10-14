// Bind.cpp — чтение клавиш/мыши из evdev (read-only, игре не мешаем).
// Следим за устройствами с кнопками (клава + мышь), тачскрин игнорим.
#include "Bind.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <cstdint>

#ifndef KEY_MAX
#define KEY_MAX 0x2ff
#endif

namespace bind {
namespace {

int s_fds[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
int s_nfd = 0;
bool s_down[KEY_MAX + 1] = {false};
int s_pressed = -1; // очередь захвата: последний нажатый код
bool s_opened = false;

bool hasKeyBit(int fd, int code) {
    uint8_t bits[(KEY_MAX + 7) / 8] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    return (bits[code / 8] & (1 << (code % 8))) != 0;
}

void openAll() {
    if (s_opened) return;
    s_opened = true;
    DIR* d = opendir("/dev/input");
    if (!d) return;
    struct dirent* e;
    char path[64];
    while ((e = readdir(d)) && s_nfd < 16) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        uint8_t mtBits[(ABS_MAX + 7) / 8] = {0};
        bool isTouch = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(mtBits)), mtBits) >= 0 &&
                       (mtBits[ABS_MT_POSITION_X / 8] & (1 << (ABS_MT_POSITION_X % 8)));
        // нужны кнопки мыши/клавы (тачскрин тоже смотрим — для RAW-диагностики ПКМ)
        if (!hasKeyBit(fd, BTN_LEFT) && !hasKeyBit(fd, KEY_A) && !hasKeyBit(fd, BTN_RIGHT) && !isTouch) {
            close(fd);
            continue;
        }
        s_fds[s_nfd++] = fd;
        printf("[bind] watch %s\n", path);
        fflush(stdout);
    }
    closedir(d);
    printf("[bind] watching %d input devs\n", s_nfd);
    fflush(stdout);
}

const char* nameFor(int code) {
    static char buf[16];
    switch (code) {
        case BTN_RIGHT: return "RMB";
        case BTN_MIDDLE: return "MMB";
        case BTN_SIDE: return "M4";
        case BTN_EXTRA: return "M5";
        case KEY_SPACE: return "Space";
        case KEY_ESC: return "Esc";
        case KEY_TAB: return "Tab";
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT: return "Shift";
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: return "Ctrl";
        case KEY_LEFTALT:
        case KEY_RIGHTALT: return "Alt";
        case KEY_CAPSLOCK: return "Caps";
        default: break;
    }
    if (code >= KEY_F1 && code <= KEY_F10) { snprintf(buf, sizeof(buf), "F%d", code - KEY_F1 + 1); return buf; }
    if (code == KEY_F11) return "F11";
    if (code == KEY_F12) return "F12";
    static const char qwerty[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    static const int qcode[] = {16,17,18,19,20,21,22,23,24,25,30,31,32,33,34,35,36,37,38,44,45,46,47,48,49,50};
    for (int i = 0; i < 26; i++)
        if (code == qcode[i]) { buf[0] = qwerty[i]; buf[1] = 0; return buf; }
    if (code >= KEY_1 && code <= KEY_9) { buf[0] = (char)('1' + code - KEY_1); buf[1] = 0; return buf; }
    if (code == KEY_0) return "0";
    snprintf(buf, sizeof(buf), "K%d", code);
    return buf;
}

} // namespace

void poll() {
    openAll();
    struct input_event ev;
    for (int i = 0; i < s_nfd; i++) {
        int fd = s_fds[i];
        if (fd < 0) continue;
        for (int n = 0; n < 256; n++) {
            ssize_t r = read(fd, &ev, sizeof(ev));
            if (r != (ssize_t)sizeof(ev)) break;
            if (ev.type != EV_KEY) continue;
            if (ev.code < 0 || ev.code > KEY_MAX) continue;
            if (ev.value == 1) {
                s_down[ev.code] = true;
                // в захват: всё кроме ЛКМ/тача (ими жмут по меню)
                if (ev.code != BTN_LEFT && ev.code != BTN_TOUCH && ev.code != BTN_TOOL_FINGER)
                    s_pressed = ev.code;
            } else if (ev.value == 0) {
                s_down[ev.code] = false;
            }
            // value==2 (autorepeat) игнорим
        }
    }
    // ПКМ в ImGui (для бинда по правому клику в меню)
    ImGuiIO& io = ImGui::GetIO();
    if (BTN_RIGHT <= KEY_MAX) io.MouseDown[1] = s_down[BTN_RIGHT];
}

bool isDown(int code) {
    if (code <= 0) return true; // бинда нет — всегда активен
    if (code < 0 || code > KEY_MAX) return false;
    return s_down[code];
}

int consumePress() {
    int c = s_pressed;
    s_pressed = -1;
    return c;
}

bool consumeMenuToggle() {
    const int c = s_pressed;
    if (c == KEY_F10 || c == KEY_INSERT) {
        s_pressed = -1;
        return true;
    }
    return false;
}

const char* keyName(int code) {
    if (code <= 0) return "Always";
    return nameFor(code);
}

} // namespace bind

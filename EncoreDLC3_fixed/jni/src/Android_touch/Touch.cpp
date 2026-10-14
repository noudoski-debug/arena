// Touch.cpp — чтение тачскрина из /dev/input и перевод в экранные координаты ImGui.
//
// Координаты: нормализация (value - min) / (max - min) по каждой оси, затем
// поворот в экранное пространство. Преобразование координат выполняется только внутри обработчика ввода
// использует триггербот, чтобы тап попадал ровно туда, куда рисует оверлей.
//
// Ввод идёт через очередь, а не прямой записью в io:
//   ImGui 1.89 считает клик по ОТПУСКАНИЮ (ButtonBehavior: release_in =
//   hovered && PressedOnClickRelease). Если писать io.MouseDown напрямую из
//   фонового потока, быстрый тап (~50мс) успевает пройти целиком между двумя
//   кадрами, и главный поток видит только итоговое down=false: кнопка
//   подсвечивается, но нажатие теряется. Поток складывает события в очередь,
//   pump() из главного потока отдаёт их в io.AddMouse*Event до NewFrame(),
//   а штатный trickle раскладывает tap на кадры (hover -> down -> up).
//
// Детект нажатия НЕ полагается на ioctl(ABS_MT_TRACKING_ID): на части панелей
// этот ioctl не отдаёт валидный диапазон, хотя сами события ABS_MT_TRACKING_ID
// или BTN_TOUCH приходят исправно. Раньше из-за этого down навсегда оставался
// false и меню не реагировало. Теперь источник нажатия определяется по факту
// приходящих событий, с фолбэком на "есть координаты в пакете".
#include <cstdio>
#include <cstring>
#include <thread>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <cmath>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <sys/ioctl.h>
#include "imgui.h"

#include "Touch.hpp"

namespace touch {
namespace {

constexpr int kMaxSlots = 10;

struct Panel {
    int fd{-1};
    bool canBeUsed{false};
    input_absinfo absX{}, absY{};

    float rangeX() const {
        float r = (float)(absX.maximum - absX.minimum);
        return r > 1.f ? r : 1.f;
    }
    float rangeY() const {
        float r = (float)(absY.maximum - absY.minimum);
        return r > 1.f ? r : 1.f;
    }
};

struct Ev {
    bool isButton;   // false = позиция, true = кнопка
    float x, y;
    bool down;
};

std::vector<Panel> g_panels;
std::atomic<bool> g_running{false};
std::atomic<int> g_orientation{0};
std::atomic<int> g_screenW{0};
std::atomic<int> g_screenH{0};

std::mutex g_evMutex;
std::vector<Ev> g_events;

void pushPos(float x, float y) {
    std::lock_guard<std::mutex> lk(g_evMutex);
    // Схлопываем подряд идущие перемещения: важна последняя точка.
    if (!g_events.empty() && !g_events.back().isButton) {
        g_events.back().x = x;
        g_events.back().y = y;
        return;
    }
    if (g_events.size() > 256) return;
    g_events.push_back(Ev{false, x, y, false});
}

void pushButton(float x, float y, bool down) {
    std::lock_guard<std::mutex> lk(g_evMutex);
    if (g_events.size() > 256) g_events.clear(); // сам факт нажатия важнее истории
    // Перед сменой состояния кнопки фиксируем точку касания, иначе ImGui
    // засчитает клик по старым координатам.
    g_events.push_back(Ev{false, x, y, false});
    g_events.push_back(Ev{true, x, y, down});
}

// Панель может быть повёрнута относительно экрана, а система при залоченном
// ландшафте иногда отдаёт rotation=0. Если ориентация панели после поворота
// не совпала с ориентацией экрана — доворачиваем на 90°.
int effectiveRotation(const Panel& p, int w, int h) {
    int rot = g_orientation.load() & 3;
    if (w <= 0 || h <= 0) return rot;

    const float pw = p.rangeX(), ph = p.rangeY();
    const float panelAspect = pw / ph;
    const float screenAspect = (float)w / (float)h;
    if (panelAspect > 0.9f && panelAspect < 1.1f) return rot;
    if (screenAspect > 0.9f && screenAspect < 1.1f) return rot;

    const bool panelLandscape = pw > ph;
    const bool screenLandscape = w > h;
    const bool mappedLandscape = (rot == 1 || rot == 3) ? !panelLandscape : panelLandscape;
    if (mappedLandscape != screenLandscape) rot = (rot + 1) & 3;
    return rot;
}

void rawToScreen(const Panel& p, float rawX, float rawY, float& outX, float& outY) {
    const int w = g_screenW.load(), h = g_screenH.load();
    float nx = (rawX - (float)p.absX.minimum) / p.rangeX();
    float ny = (rawY - (float)p.absY.minimum) / p.rangeY();
    if (nx < 0.f) nx = 0.f; else if (nx > 1.f) nx = 1.f;
    if (ny < 0.f) ny = 0.f; else if (ny > 1.f) ny = 1.f;

    switch (effectiveRotation(p, w, h)) {
        case 1: // 90°
            outX = ny * (float)w;
            outY = (1.f - nx) * (float)h;
            break;
        case 2: // 180°
            outX = (1.f - nx) * (float)w;
            outY = (1.f - ny) * (float)h;
            break;
        case 3: // 270°
            outX = (1.f - ny) * (float)w;
            outY = nx * (float)h;
            break;
        default: // 0°
            outX = nx * (float)w;
            outY = ny * (float)h;
            break;
    }
}

struct SlotState {
    bool active = false;
    float rawX = 0.f, rawY = 0.f;
};

void input_thread(Panel panel) {
    input_event events[64]{};
    SlotState slots[kMaxSlots];
    int curSlot = 0;

    // Что панель реально присылает — выясняем на ходу, не по ioctl.
    bool sawTrackingId = false;   // приходил ABS_MT_TRACKING_ID
    bool sawBtnTouch = false;     // приходил BTN_TOUCH / BTN_TOOL_FINGER
    bool btnDown = false;
    bool posInPacket = false;     // в текущем пакете были координаты
    float rawX = 0.f, rawY = 0.f;
    bool prevDown = false;
    bool haveAnyPos = false;

    while (g_running.load() && panel.fd >= 0) {
        auto readBytes = read(panel.fd, events, sizeof(events));
        if (readBytes <= 0) continue;
        const long count = (long)(readBytes / (ssize_t)sizeof(input_event));

        for (long j = 0; j < count; j++) {
            const auto& event = events[j];

            if (event.type == EV_ABS) {
                if (event.code == ABS_MT_SLOT) {
                    curSlot = (event.value >= 0 && event.value < kMaxSlots) ? event.value : 0;
                    continue;
                }
                if (event.code == ABS_MT_TRACKING_ID) {
                    sawTrackingId = true;
                    slots[curSlot].active = (event.value != -1);
                    continue;
                }
                if (event.code == ABS_MT_POSITION_X || event.code == ABS_X) {
                    slots[curSlot].rawX = (float)event.value;
                    rawX = (float)event.value;
                    posInPacket = true;
                    haveAnyPos = true;
                    continue;
                }
                if (event.code == ABS_MT_POSITION_Y || event.code == ABS_Y) {
                    slots[curSlot].rawY = (float)event.value;
                    rawY = (float)event.value;
                    posInPacket = true;
                    haveAnyPos = true;
                    continue;
                }
                continue;
            }

            if (event.type == EV_KEY &&
                (event.code == BTN_TOUCH || event.code == BTN_TOOL_FINGER)) {
                sawBtnTouch = true;
                btnDown = (event.value != 0);
                continue;
            }

            // Конец пакета: только EV_SYN/SYN_REPORT (SYN_REPORT == 0 == ABS_X,
            // поэтому проверка типа обязательна).
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                bool down;
                float useX = rawX, useY = rawY;

                if (sawTrackingId) {
                    // Точнее всего: ведём первый активный слот, поэтому второй
                    // палец (игра целится) не таскает курсор меню.
                    down = false;
                    for (int s = 0; s < kMaxSlots; s++) {
                        if (slots[s].active) {
                            down = true;
                            useX = slots[s].rawX;
                            useY = slots[s].rawY;
                            break;
                        }
                    }
                } else if (sawBtnTouch) {
                    down = btnDown;
                } else {
                    // Фолбэк как в исходном коде: пакет с координатами =
                    // палец на экране, пустой SYN = отпускание.
                    down = posInPacket;
                }

                if (haveAnyPos) {
                    float sx = 0.f, sy = 0.f;
                    rawToScreen(panel, useX, useY, sx, sy);

                    if (down != prevDown) {
                        pushButton(sx, sy, down);
                        prevDown = down;
                    } else if (down) {
                        pushPos(sx, sy); // перетаскивание слайдеров/окна
                    }
                }
                posInPacket = false;
            }
        }
    }
}

} // namespace

// Отдаём накопленные события в ImGui. Вызывать из главного потока до NewFrame().
void pump() {
    std::vector<Ev> local;
    {
        std::lock_guard<std::mutex> lk(g_evMutex);
        if (g_events.empty()) return;
        local.swap(g_events);
    }
    ImGuiIO& io = ImGui::GetIO();
    for (const Ev& e : local) {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        if (e.isButton) io.AddMouseButtonEvent(0, e.down);
        else            io.AddMousePosEvent(e.x, e.y);
    }
}

bool init(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
    g_orientation.store(_orientation & 3);
    g_screenW.store(_screen_w);
    g_screenH.store(_screen_h);
    g_running.store(true);

    bool result = false;
    const char* path = "/dev/input/";
    auto dir = opendir(path);
    if (!dir) return false;

    dirent* ptr;
    while ((ptr = readdir(dir))) {
        if (!strstr(ptr->d_name, "event")) continue;

        char event_path[128];
        snprintf(event_path, sizeof(event_path), "%s%s", path, ptr->d_name);
        int fd = open(event_path, O_RDONLY | O_CLOEXEC);
        if (fd == -1) continue;

        Panel panel;
        panel.fd = fd;

        // Берём устройство, если у него есть мультитач ЛИБО одиночный тач.
        // Диапазон осей нужен для пересчёта координат, поэтому он обязателен.
        const bool haveMt = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &panel.absX) != -1 &&
                            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &panel.absY) != -1 &&
                            panel.absX.maximum > panel.absX.minimum &&
                            panel.absY.maximum > panel.absY.minimum;
        bool haveSt = false;
        if (!haveMt) {
            uint8_t keyBits[(KEY_MAX + 7) / 8] = {0};
            const bool hasBtnTouch =
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0 &&
                (keyBits[BTN_TOUCH / 8] & (1 << (BTN_TOUCH % 8))) != 0;
            haveSt = hasBtnTouch &&
                     ioctl(fd, EVIOCGABS(ABS_X), &panel.absX) != -1 &&
                     ioctl(fd, EVIOCGABS(ABS_Y), &panel.absY) != -1 &&
                     panel.absX.maximum > panel.absX.minimum &&
                     panel.absY.maximum > panel.absY.minimum;
            if (!haveSt) {
                close(fd);
                continue;
            }
        }
        panel.canBeUsed = true;

        printf("[touch] %s abs x[%d..%d] y[%d..%d] mt=%d -> screen %dx%d orient=%u\n",
               event_path, panel.absX.minimum, panel.absX.maximum,
               panel.absY.minimum, panel.absY.maximum, (int)haveMt,
               _screen_w, _screen_h, (unsigned)(_orientation & 3));
        fflush(stdout);

        g_panels.push_back(panel);
        result = true;
        std::thread(input_thread, panel).detach();
    }
    closedir(dir);
    return result;
}

void update(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
    if (_screen_w > 0) g_screenW.store(_screen_w);
    if (_screen_h > 0) g_screenH.store(_screen_h);
    g_orientation.store(_orientation & 3);
}

void updateOrientation(uint8_t _orientation) {
    g_orientation.store(_orientation & 3);
}


void shutdown() {
    g_running.store(false);
    for (auto& p : g_panels)
        if (p.fd != -1) {
            close(p.fd);
            p.fd = -1;
        }
    g_panels.clear();
    {
        std::lock_guard<std::mutex> lk(g_evMutex);
        g_events.clear();
    }
}

} // namespace touch

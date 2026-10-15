#pragma once
// =============================================================================
//  HitEffect.h — красивые визуальные эффекты при хите и килле
//  Подключение: вызвать HitEffect::tick(drawList, screenW, screenH) каждый кадр
//               внутри ImGui-фрейма (после ImGui::NewFrame, до ImGui::Render).
//  При хите:    HitEffect::onHit(screenX, screenY, damage);
//  При килле:   HitEffect::onKill(screenX, screenY);
// =============================================================================
#pragma once
#include "ImGui/imgui.h"
#include <vector>
#include <cmath>
#include <cstdlib>

namespace HitEffect {

// ─── конфиги ─────────────────────────────────────────────────────────────────
static bool enabled     = true;   // вкл/выкл эффекты
static bool hitEnabled  = true;   // эффект при хите
static bool killEnabled = true;   // эффект при килле
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

static constexpr float kPI = 3.14159265f;

// ── Particle ─────────────────────────────────────────────────────────────────
struct Particle {
    float x, y;       // текущая позиция (screen px)
    float vx, vy;     // скорость
    float life;       // 0..1  (1 = только что создана)
    float size;       // начальный радиус
    ImVec4 color;     // rgba
    bool  square;     // квадрат вместо круга
};

// ── HitNumber (урон цифрой) ───────────────────────────────────────────────────
struct HitNumber {
    float x, y;
    float vy;
    float life;       // 0..1
    int   damage;
    ImVec4 color;
};

// ── KillBanner ("KILL!") ─────────────────────────────────────────────────────
struct KillBanner {
    float x, y;
    float vy;
    float scale;      // анимированный масштаб
    float life;       // 0..1
    float bounce;     // фаза bounce
};

static std::vector<Particle>   s_particles;
static std::vector<HitNumber>  s_hitNumbers;
static std::vector<KillBanner> s_banners;

inline float randf(float lo, float hi) {
    return lo + (hi - lo) * ((float)(rand() % 10000) / 10000.f);
}

// lerp цвета
inline ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return { a.x + (b.x - a.x)*t,
             a.y + (b.y - a.y)*t,
             a.z + (b.z - a.z)*t,
             a.w + (b.w - a.w)*t };
}

// ── HSV → RGBA ───────────────────────────────────────────────────────────────
inline ImVec4 hsv(float h, float s, float v, float a=1.f) {
    float r,g,b;
    ImGui::ColorConvertHSVtoRGB(h,s,v,r,g,b);
    return {r,g,b,a};
}

// ── спавн частиц при хите ────────────────────────────────────────────────────
inline void spawnHitParticles(float sx, float sy) {
    // Оранжево-красные «искры»
    for (int i = 0; i < 14; i++) {
        float angle = randf(0.f, 2.f * kPI);
        float speed = randf(60.f, 220.f);
        float hue   = randf(0.02f, 0.12f); // красно-оранжевый
        Particle p;
        p.x = sx; p.y = sy;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed - randf(20.f, 80.f); // лёгкий подъём
        p.life = 1.f;
        p.size = randf(3.f, 7.f);
        p.color = hsv(hue, 1.f, 1.f, 1.f);
        p.square = (rand() % 3 == 0);
        s_particles.push_back(p);
    }
    // Белые «вспышки» в центре
    for (int i = 0; i < 5; i++) {
        float angle = randf(0.f, 2.f * kPI);
        float speed = randf(20.f, 80.f);
        Particle p;
        p.x = sx; p.y = sy;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.life = 1.f;
        p.size = randf(2.f, 4.f);
        p.color = {1.f, 1.f, 1.f, 1.f};
        p.square = false;
        s_particles.push_back(p);
    }
}

// ── спавн частиц при килле ───────────────────────────────────────────────────
inline void spawnKillParticles(float sx, float sy) {
    // Золото/пурпур/синий взрыв
    static const ImVec4 killPalette[] = {
        {1.0f, 0.84f, 0.0f, 1.f},  // золото
        {0.6f, 0.0f,  1.0f, 1.f},  // пурпур
        {0.0f, 0.8f,  1.0f, 1.f},  // голубой
        {1.0f, 1.0f,  1.0f, 1.f},  // белый
    };
    for (int i = 0; i < 28; i++) {
        float angle = randf(0.f, 2.f * kPI);
        float speed = randf(80.f, 360.f);
        Particle p;
        p.x = sx; p.y = sy;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed - randf(40.f, 120.f);
        p.life = 1.f;
        p.size = randf(4.f, 11.f);
        p.color = killPalette[rand() % 4];
        p.square = (rand() % 4 == 0);
        s_particles.push_back(p);
    }
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
//  Публичный API
// ─────────────────────────────────────────────────────────────────────────────

// Вызвать при попадании (sx,sy — экранные координаты цели)
inline void onHit(float sx, float sy, int damage = 0) {
    if (!enabled || !hitEnabled) return;
    detail::spawnHitParticles(sx, sy);

    if (damage > 0) {
        detail::HitNumber hn;
        hn.x = sx + detail::randf(-20.f, 20.f);
        hn.y = sy - 10.f;
        hn.vy = -55.f;
        hn.life = 1.f;
        hn.damage = damage;
        // цвет зависит от урона: зелёный → жёлтый → красный
        float t = (float)damage / 100.f;
        if (t > 1.f) t = 1.f;
        hn.color = detail::lerpColor({0.4f,1.f,0.4f,1.f},{1.f,0.3f,0.3f,1.f}, t);
        detail::s_hitNumbers.push_back(hn);
    }
}

// Вызвать при килле (sx,sy — последняя экранная позиция врага)
inline void onKill(float sx, float sy) {
    if (!enabled || !killEnabled) return;
    detail::spawnKillParticles(sx, sy);

    detail::KillBanner kb;
    kb.x = sx;
    kb.y = sy - 30.f;
    kb.vy = -40.f;
    kb.scale = 0.f;     // вырастает
    kb.life  = 1.f;
    kb.bounce = 0.f;
    detail::s_banners.push_back(kb);
}

// Вызывать каждый кадр (dt — секунды с прошлого кадра)
inline void tick(ImDrawList* dl, float dt) {
    if (!enabled) return;
    if (!dl) return;

    using namespace detail;

    // ── частицы ──────────────────────────────────────────────────────────────
    for (auto it = s_particles.begin(); it != s_particles.end(); ) {
        auto& p = *it;
        p.vy += 320.f * dt;       // гравитация
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.vx *= (1.f - 3.f * dt); // торможение по X
        p.life -= dt * 1.8f;

        if (p.life <= 0.f) { it = s_particles.erase(it); continue; }

        float alpha = p.life * p.life; // квадратный fade
        ImVec4 c = p.color; c.w *= alpha;
        float r = p.size * p.life;

        if (p.square) {
            dl->AddRectFilled(
                {p.x - r, p.y - r}, {p.x + r, p.y + r},
                ImGui::ColorConvertFloat4ToU32(c),
                r * 0.3f);
        } else {
            dl->AddCircleFilled({p.x, p.y}, r,
                ImGui::ColorConvertFloat4ToU32(c), 10);
        }
        // обводка у крупных
        if (p.size > 5.f) {
            ImVec4 edge = c; edge.w *= 0.5f;
            dl->AddCircle({p.x, p.y}, r,
                ImGui::ColorConvertFloat4ToU32(edge), 10, 1.2f);
        }
        ++it;
    }

    // ── цифры урона ──────────────────────────────────────────────────────────
    for (auto it = s_hitNumbers.begin(); it != s_hitNumbers.end(); ) {
        auto& h = *it;
        h.y  += h.vy * dt;
        h.vy *= (1.f - 4.f * dt);
        h.life -= dt * 1.2f;
        if (h.life <= 0.f) { it = s_hitNumbers.erase(it); continue; }

        float alpha = h.life < 0.3f ? h.life / 0.3f : 1.f;
        ImVec4 c = h.color; c.w = alpha;

        char buf[16];
        snprintf(buf, sizeof(buf), "%d", h.damage);

        // тень
        ImVec4 shadow = {0,0,0, alpha*0.7f};
        dl->AddText(nullptr, 16.f,
            {h.x - 1 + 1, h.y - 1 + 1},
            ImGui::ColorConvertFloat4ToU32(shadow), buf);
        // текст
        dl->AddText(nullptr, 16.f,
            {h.x, h.y},
            ImGui::ColorConvertFloat4ToU32(c), buf);
        ++it;
    }

    // ── баннер KILL! ─────────────────────────────────────────────────────────
    for (auto it = s_banners.begin(); it != s_banners.end(); ) {
        auto& kb = *it;
        kb.y    += kb.vy * dt;
        kb.vy   *= (1.f - 3.f * dt);
        kb.life -= dt * 0.9f;
        kb.bounce += dt * 12.f;
        if (kb.life <= 0.f) { it = s_banners.erase(it); continue; }

        // scale: быстрый pop-in, потом плавный fade
        float targetScale = kb.life > 0.7f ? 1.f : kb.life / 0.7f;
        kb.scale += (targetScale - kb.scale) * dt * 18.f;
        float s = kb.scale * (1.f + 0.06f * sinf(kb.bounce));

        float alpha = kb.life < 0.25f ? kb.life / 0.25f : 1.f;
        float fontSize = 26.f * s;

        const char* txt = "KILL!";
        ImVec2 tsz = ImGui::CalcTextSize(txt);

        float ox = kb.x - tsz.x * s * 0.5f;
        float oy = kb.y - tsz.y * s * 0.5f;

        // свечение (несколько слоёв тени с размытием вручную)
        ImVec4 glow = {1.f, 0.8f, 0.f, alpha * 0.4f};
        for (int g = 3; g >= 1; g--) {
            float gd = (float)g * 2.f * s;
            ImU32 gc = ImGui::ColorConvertFloat4ToU32(glow);
            dl->AddText(nullptr, fontSize, {ox - gd, oy}, gc, txt);
            dl->AddText(nullptr, fontSize, {ox + gd, oy}, gc, txt);
            dl->AddText(nullptr, fontSize, {ox, oy - gd}, gc, txt);
            dl->AddText(nullptr, fontSize, {ox, oy + gd}, gc, txt);
        }
        // чёрная обводка
        ImVec4 sh = {0,0,0, alpha * 0.9f};
        ImU32  sc = ImGui::ColorConvertFloat4ToU32(sh);
        dl->AddText(nullptr, fontSize, {ox-2*s, oy}, sc, txt);
        dl->AddText(nullptr, fontSize, {ox+2*s, oy}, sc, txt);
        dl->AddText(nullptr, fontSize, {ox, oy-2*s}, sc, txt);
        dl->AddText(nullptr, fontSize, {ox, oy+2*s}, sc, txt);
        // основной текст: золотой градиент (двойной слой)
        ImVec4 top = {1.f, 0.98f, 0.5f, alpha};
        ImVec4 bot = {1.f, 0.55f, 0.1f, alpha};
        // верхняя половина
        dl->AddText(nullptr, fontSize, {ox, oy},
            ImGui::ColorConvertFloat4ToU32(top), txt);
        // нижняя половина с красным оттенком (второй проход)
        dl->AddText(nullptr, fontSize * 0.5f, {ox, oy + fontSize * 0.5f},
            ImGui::ColorConvertFloat4ToU32(bot), txt);

        ++it;
    }
}

// Очистить все активные эффекты (например при закрытии меню)
inline void clear() {
    detail::s_particles.clear();
    detail::s_hitNumbers.clear();
    detail::s_banners.clear();
}

} // namespace HitEffect

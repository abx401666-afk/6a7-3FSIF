#include "../include/Lens.hpp"
#include "../include/Memory.hpp"
#include "../include/Log.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/UETypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

std::string Lens_Status();

namespace {
    constexpr int kPerChunk = 0x10000, kItemSize = 0x18, kSlice = 4096;

    struct Effect { uintptr_t obj; LensParams p; bool enabled; };

    std::unordered_map<uintptr_t, bool> g_isLensClass;
    std::vector<uintptr_t> g_found;
    std::vector<Effect> g_effects;
    LensParams g_params;
    int g_cursor = 0, g_total = 0, g_passes = 0;
    bool g_classLoaded = false;
    double g_lastRefresh = -1.0, g_passStart = -1.0;

    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }

    bool IsLensClass(uintptr_t cls) {
        const auto it = g_isLensClass.find(cls);
        if (it != g_isLensClass.end()) return it->second;
        uint32_t idx = 0;
        if (!ReadRemote(cls + OFF(ObjectName), idx)) return false;
        const std::string n = Skeleton_FName(idx);
        if (n.empty()) return false;
        const bool yes = n == "imperfectereffect_lensdistortion";
        g_isLensClass[cls] = yes;
        return yes;
    }

    void ScanSlice() {
        const uintptr_t arr = GetRemoteModuleBase() + Offsets::GObjects;
        uintptr_t objects = 0;
        int32_t num = 0, chunks = 0;
        if (!ReadRemote(arr, objects) || !ReadRemote(arr + OFF(ObjArrayNumElements), num) || !ReadRemote(arr + OFF(ObjArrayNumChunks), chunks)) return;
        if (!Plausible(objects) || num <= 0 || num > 8 * 1024 * 1024 || chunks <= 0) return;
        g_total = num;
        if (g_cursor >= num) {
            if (Targets_Now() - g_passStart < 6.0) return;
            g_cursor = 0; ++g_passes;
        }
        if (g_cursor == 0) {
            g_passStart = Targets_Now();
            g_found.erase(std::remove_if(g_found.begin(), g_found.end(), [](uintptr_t o) {
                uintptr_t c = 0; return !ReadRemote(o + OFF(ObjectClass), c) || !IsLensClass(c);
            }), g_found.end());
        }
        const int chunk = g_cursor / kPerChunk, inChunk = g_cursor % kPerChunk;
        uintptr_t chunkPtr = 0;
        if (chunk >= chunks || !ReadRemote(objects + chunk * 8ull, chunkPtr) || !Plausible(chunkPtr)) { g_cursor = (chunk + 1) * kPerChunk; return; }
        const int count = (std::min)({ kSlice, kPerChunk - inChunk, num - g_cursor });
        std::vector<uint8_t> items(static_cast<size_t>(count) * kItemSize);
        if (!ReadRemoteBytes(chunkPtr + static_cast<uintptr_t>(inChunk) * kItemSize, items.data(), items.size())) { g_cursor += count; return; }
        for (int i = 0; i < count; ++i) {
            uintptr_t obj = 0;
            std::memcpy(&obj, items.data() + static_cast<size_t>(i) * kItemSize, 8);
            if (!Plausible(obj)) continue;
            struct { uint64_t vt; uint32_t flags; uint32_t index; uintptr_t cls; } head{};
            if (!ReadRemote(obj, head) || !Plausible(head.cls)) continue;
            if (head.flags & 0x10) continue;
            if (!g_classLoaded) {
                uint32_t nameIdx = 0;
                ReadRemote(obj + OFF(ObjectName), nameIdx);
                const std::string on = Skeleton_FName(nameIdx);
                if (on == "imperfectereffect_lensdistortion") g_classLoaded = true;
            }
            if (IsLensClass(head.cls) && std::find(g_found.begin(), g_found.end(), obj) == g_found.end()) g_found.push_back(obj);
        }
        g_cursor += count;
    }

    void Refresh() {
        g_effects.clear();
        for (uintptr_t o : g_found) {
            uint8_t raw[0x100] = {};
            if (!ReadRemoteBytes(o, raw, sizeof(raw))) continue;
            auto B = [&](int64_t off) { return off >= 0 && off < static_cast<int64_t>(sizeof(raw)) ? raw[off] : uint8_t{ 0 }; };
            auto F = [&](int64_t off) {
                if (off < 0 || off + 4 > static_cast<int64_t>(sizeof(raw))) return 0.0f;
                float v; std::memcpy(&v, raw + off, 4); return std::isfinite(v) ? v : 0.0f;
            };
            Effect e{ o, {}, B(OFF(LensEnabled)) != 0 };
            LensParams& p = e.p;
            p.method = B(OFF(LensMethod));
            p.intensity = F(OFF(LensIntensity)); p.start = F(OFF(LensStart)); p.end = F(OFF(LensEnd)); p.invert = B(OFF(LensInvert)) != 0;
            const int64_t c = OFF(LensCoefficients);
            p.k1 = F(c); p.k2 = F(c + 4); p.k3 = F(c + 8); p.k4 = F(c + 12); p.p1 = F(c + 16); p.p2 = F(c + 20);
            const float m = F(OFF(LensMagnifier));
            p.magnifier = m > 0.05f && m < 20.0f ? m : 1.0f;
            p.valid = e.enabled;
            g_effects.push_back(e);
        }
        g_params = {};
        float best = -1.0f;
        for (const Effect& e : g_effects) {
            if (!e.enabled) continue;
            const float strength = std::fabs(e.p.k1) + std::fabs(e.p.k2) + std::fabs(e.p.intensity) + std::fabs(e.p.magnifier - 1.0f);
            if (strength > best) { best = strength; g_params = e.p; }
        }
        static std::string lastLogged;
        const std::string now = Lens_Status();
        if (g_params.valid && now != lastLogged) { lastLogged = now; Log_Add(Log_Info, "%s", now.c_str()); }
    }

    void Source(float cx, float cy, const LensParams& p, float& sx, float& sy) {
        const float r2 = cx * cx + cy * cy;
        if (p.method == 1) {
            const float radial = 1.0f + r2 * (p.k1 + r2 * (p.k2 + r2 * (p.k3 + r2 * p.k4)));
            float px = cx * radial + 2.0f * p.p1 * cx * cy + p.p2 * (r2 + 2.0f * cx * cx);
            float py = cy * radial + p.p1 * (r2 + 2.0f * cy * cy) + 2.0f * p.p2 * cx * cy;
            const float blend = p.intensity > 0.0f && p.intensity <= 1.0f ? p.intensity : 1.0f;
            sx = cx + (px - cx) * blend;
            sy = cy + (py - cy) * blend;
        } else {
            const float r = std::sqrt(r2);
            float t = p.end > p.start + 1e-4f ? (r - p.start) / (p.end - p.start) : (r >= p.start ? 1.0f : 0.0f);
            t = std::clamp(t, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t);
            const float a = p.intensity * t;
            const float f = p.invert ? 1.0f + a : 1.0f - a;
            sx = cx * f; sy = cy * f;
        }
        sx /= p.magnifier; sy /= p.magnifier;
    }
}

void Lens_Update() {
    if (!IsGameProcessAttached()) return;
    ScanSlice();
    const double now = Targets_Now();
    if (now - g_lastRefresh > 0.25) { g_lastRefresh = now; Refresh(); }
}

const LensParams& Lens_Get() { return g_params; }

void Lens_Apply(float& x, float& y, float w, float h) {
    const LensParams& p = g_params;
    if (!p.valid || w < 1.0f || h < 1.0f) return;
    const float ax = w / h;
    const float tx = (x / w - 0.5f) * ax, ty = y / h - 0.5f;
    float cx = tx, cy = ty;
    for (int i = 0; i < 16; ++i) {
        float sx, sy;
        Source(cx, cy, p, sx, sy);
        const float ex = tx - sx, ey = ty - sy;
        if (ex * ex + ey * ey < 1e-10f) break;
        const float lc = std::sqrt(cx * cx + cy * cy), ls = std::sqrt(sx * sx + sy * sy);
        const float gain = ls > 1e-5f && lc > 1e-5f ? std::clamp(lc / ls, 0.2f, 5.0f) : p.magnifier;
        cx += ex * gain; cy += ey * gain;
    }
    if (!std::isfinite(cx) || !std::isfinite(cy)) return;
    x = (cx / ax + 0.5f) * w;
    y = (cy + 0.5f) * h;
}

std::string Lens_Status() {
    char b[160];
    const LensParams& p = g_params;
    if (g_found.empty()) {
        if (g_total > 0) std::snprintf(b, sizeof(b), "Game lens: searching %d%%", static_cast<int>(100.0 * std::min(g_cursor, g_total) / g_total));
        else std::snprintf(b, sizeof(b), "Game lens: waiting for game");
        if (g_passes > 0) std::snprintf(b, sizeof(b), g_classLoaded ? "Game lens: plugin loaded, effect not in use" : "Game lens: not used by the game");
    } else if (!p.valid) {
        std::snprintf(b, sizeof(b), "Game lens: %zu found, none on", g_found.size());
    } else if (p.method == 1) {
        std::snprintf(b, sizeof(b), "Lens poly k1 %.3f k2 %.3f k3 %.3f  i %.2f  mag %.2f", p.k1, p.k2, p.k3, p.intensity, p.magnifier);
    } else {
        std::snprintf(b, sizeof(b), "Lens radial i %.2f  %.2f-%.2f%s  mag %.2f", p.intensity, p.start, p.end, p.invert ? " inv" : "", p.magnifier);
    }
    return b;
}

#define IMGUI_DEFINE_MATH_OPERATORS
#include "../include/Misc.hpp"
#include "../include/Targets.hpp"
#include "../include/CombatLog.hpp"
#include "../include/ESP.hpp"
#include "../include/World.hpp"
#include "../include/Visibility.hpp"
#include <imgui_internal.h>
#include <Windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace {
    struct Step { FVector pos; double t; bool loud; };
    struct Track { FVector last{}; double dist = 0.0; bool init = false; double seen = 0.0; };
    struct Tracer {
        FVector a, b;
        double t;
        bool hit;
        FVector cam, fw;
        double shotAt;
        double enemyDist;
        uint32_t probe;
        bool pending;
        bool draw;
    };
    struct Marker { double t; bool kill; uintptr_t pawn; FVector pos; bool placed = false; FVector off{}; };

    std::deque<Step> g_steps;
    std::unordered_map<uintptr_t, Track> g_tracks;
    std::deque<Tracer> g_tracers;
    std::deque<Marker> g_markers;
    std::unordered_map<uintptr_t, double> g_lethalAt;
    constexpr double kMarkerLife = 0.45;
    double g_lastEvent = -1.0;
    int g_lastShots = -1;

    ImU32 Col(const ImVec4& c, float a) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * (a < 0 ? 0 : a > 1 ? 1 : a)));
    }
    ImU32 WithAlpha(ImU32 c, float a) {
        const float al = ((c >> IM_COL32_A_SHIFT) & 0xFF) * (a < 0 ? 0 : a > 1 ? 1 : a);
        return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al + 0.5f) << IM_COL32_A_SHIFT);
    }

    std::vector<uint8_t> g_wav[4];
    float g_wavVol = -1.0f;

    void BuildWav(std::vector<uint8_t>& out, int type, float vol) {
        const int rate = 22050;
        const double len = type == 1 ? 0.45 : type == 2 ? 0.05 : type == 3 ? 0.06 : 0.18;
        const int n = static_cast<int>(rate * len);
        std::vector<int16_t> pcm(n);
        uint32_t seed = 12345;
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / rate;
            double v = 0.0;
            switch (type) {
            case 0: v = std::sin(2 * 3.14159265 * 1320 * t) * std::exp(-t * 22); break;
            case 1: v = (0.6 * std::sin(2 * 3.14159265 * 880 * t) + 0.4 * std::sin(2 * 3.14159265 * 1760 * t)) * std::exp(-t * 7); break;
            case 2: seed = seed * 1664525u + 1013904223u;
                    v = ((seed >> 16) / 32768.0 - 1.0) * 0.5 * std::exp(-t * 90) + std::sin(2 * 3.14159265 * 2200 * t) * std::exp(-t * 60); break;
            default: v = std::sin(2 * 3.14159265 * 2600 * t) * std::exp(-t * 70); break;
            }
            const double a = t < 0.003 ? t / 0.003 : 1.0;
            pcm[i] = static_cast<int16_t>(std::lround(v * a * vol * 30000.0));
        }
        const uint32_t dataBytes = n * 2;
        out.resize(44 + dataBytes);
        auto w32 = [&](size_t o, uint32_t v) { std::memcpy(&out[o], &v, 4); };
        auto w16 = [&](size_t o, uint16_t v) { std::memcpy(&out[o], &v, 2); };
        std::memcpy(&out[0], "RIFF", 4); w32(4, 36 + dataBytes); std::memcpy(&out[8], "WAVEfmt ", 8);
        w32(16, 16); w16(20, 1); w16(22, 1); w32(24, rate); w32(28, rate * 2); w16(32, 2); w16(34, 16);
        std::memcpy(&out[36], "data", 4); w32(40, dataBytes);
        std::memcpy(&out[44], pcm.data(), dataBytes);
    }

    HWAVEOUT g_dev = nullptr;
    WAVEHDR  g_hdr{};
    bool     g_devTried = false;
    void StopWave() {
        if (!g_dev) return;
        waveOutReset(g_dev);
        if (g_hdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_dev, &g_hdr, sizeof(g_hdr));
        g_hdr = {};
    }

    void Play(int type) {
        const float vol = g_Misc.SoundVolume < 0 ? 0 : g_Misc.SoundVolume > 1 ? 1 : g_Misc.SoundVolume;
        if (vol != g_wavVol) { PlaySoundW(nullptr, nullptr, 0); StopWave(); for (auto& w : g_wav) w.clear(); g_wavVol = vol; }
        if (type < 0 || type > 3) type = 0;
        if (g_wav[type].empty()) BuildWav(g_wav[type], type, vol);
        HWAVEOUT& dev = g_dev; WAVEHDR& hdr = g_hdr;
        if (!dev && !g_devTried) {
            g_devTried = true;
            WAVEFORMATEX fmt{}; fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 1; fmt.nSamplesPerSec = 22050;
            fmt.wBitsPerSample = 16; fmt.nBlockAlign = 2; fmt.nAvgBytesPerSec = 44100;
            if (waveOutOpen(&dev, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) dev = nullptr;
        }
        if (dev) {
            StopWave();
            hdr.lpData = reinterpret_cast<LPSTR>(g_wav[type].data() + 44);
            hdr.dwBufferLength = static_cast<DWORD>(g_wav[type].size() - 44);
            if (waveOutPrepareHeader(dev, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR && waveOutWrite(dev, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) return;
        }
        PlaySoundW(reinterpret_cast<LPCWSTR>(g_wav[type].data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    FVector Forward(const FRotator& r) {
        const double p = DegToRad(r.Pitch), y = DegToRad(r.Yaw);
        return { std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) };
    }
}

void Fx_TestSound(bool kill) { Play(kill ? g_Misc.KillSoundType : 3); }

void Fx_Update() {
    const TargetFrame& f = Targets_Get();
    const double now = f.time;

    if (g_Misc.Footsteps) {
        for (const Target& t : f.targets) {
            if (t.drone || t.stale) continue;
            Track& tr = g_tracks[t.pawn];
            tr.seen = now;
            const FVector feet{ t.pos.X, t.pos.Y, t.pos.Z - t.halfHeight };
            if (!tr.init) { tr.last = feet; tr.init = true; continue; }
            const double dx = feet.X - tr.last.X, dy = feet.Y - tr.last.Y, dz = feet.Z - tr.last.Z;
            tr.last = feet;
            const double step = std::sqrt(dx * dx + dy * dy);
            const double sp2d = std::sqrt(t.vel.X * t.vel.X + t.vel.Y * t.vel.Y);
            if (step > 300.0) { tr.dist = 0; continue; }
            if (std::fabs(t.vel.Z) > 120.0 || std::fabs(dz) > 40.0) continue;
            if (sp2d < 180.0) { tr.dist = 0; continue; }
            tr.dist += step;
            if (tr.dist >= 140.0) {
                tr.dist = 0;
                if (t.distM <= g_Misc.FootstepRange) g_steps.push_back({ feet, now, sp2d > 420.0 });
            }
        }
        for (auto it = g_tracks.begin(); it != g_tracks.end();)
            it = now - it->second.seen > 3.0 ? g_tracks.erase(it) : std::next(it);
    } else { g_tracks.clear(); }
    while (!g_steps.empty() && now - g_steps.front().t > 1.4) g_steps.pop_front();
    while (g_steps.size() > 256) g_steps.pop_front();

    const auto& ev = CombatLog_Events();
    if (g_lastEvent < 0.0) g_lastEvent = ev.empty() ? now : ev.back().time;
    for (const CombatEvent& e : ev) {
        if (e.time <= g_lastEvent) continue;
        if (e.kind == Combat_Hit || e.kind == Combat_Kill) {
            const bool lethal = e.kind == Combat_Hit && e.hpLeft >= 0.0f && e.hpLeft <= 0.5f;
            bool dup = false;
            if (e.kind == Combat_Kill && e.pawn)
                for (const Marker& m : g_markers) if (m.kill && m.pawn == e.pawn) { dup = true; break; }
            if (e.kind == Combat_Kill && e.pawn && g_lethalAt.count(e.pawn) && now - g_lethalAt[e.pawn] < 2.0) dup = true;
            if (lethal && e.pawn) g_lethalAt[e.pawn] = now;
            if (g_Misc.HitMarker && !dup) {
                const bool kill = e.kind == Combat_Kill || lethal;
                Marker* same = nullptr;
                if (!kill && e.pawn) for (Marker& m : g_markers) if (!m.kill && m.pawn == e.pawn) same = &m;
                if (same) { same->t = now; same->pos = FVector{ e.px, e.py, e.pz }; same->placed = false; }
                else { Marker m{}; m.t = now; m.kill = kill; m.pawn = e.pawn; m.pos = FVector{ e.px, e.py, e.pz }; g_markers.push_back(m); }
            }
            if (e.kind == Combat_Hit && g_Misc.HitSound && !(e.hpLeft >= 0.0f && e.hpLeft <= 0.5f)) Play(3);
        }
    }
    {
        static int lastPings = -1;
        const int pings = CombatLog_KillPings();
        if (lastPings >= 0 && pings > lastPings && g_Misc.KillSound) Play(g_Misc.KillSoundType);
        lastPings = pings;
    }
    if (!ev.empty()) g_lastEvent = ev.back().time > g_lastEvent ? ev.back().time : g_lastEvent;
    while (!g_markers.empty() && now - g_markers.front().t > kMarkerLife) g_markers.pop_front();
    for (auto it = g_lethalAt.begin(); it != g_lethalAt.end();) it = now - it->second > 3.0 ? g_lethalAt.erase(it) : std::next(it);
    while (g_markers.size() > 24) g_markers.pop_front();

    const int shots = CombatLog_Shots();
    if (g_lastShots < 0 || shots < g_lastShots) g_lastShots = shots;
    if (shots > g_lastShots && f.camValid && (g_Misc.Tracers || g_Misc.HitMarker)) {
        const FVector fw = Forward(f.cam.Rotation);
        const double yaw = DegToRad(f.cam.Rotation.Yaw), pitch = DegToRad(f.cam.Rotation.Pitch);
        const FVector right{ -std::sin(yaw), std::cos(yaw), 0.0 };
        const FVector up{ -std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch) };
        double enemy = 1e9;
        for (const Target& t : f.targets) {
            if (t.drone || t.friendly) continue;
            const FVector d = t.pos - f.cam.Location;
            const double along = d.Dot(fw);
            if (along <= 0) continue;
            const FVector perp = d - fw * along;
            if (perp.Length() < t.radius + 30.0 && along < enemy) enemy = along;
        }
        FVector muzzle = f.cam.Location + fw * 70.0 + right * 14.0 - up * 12.0;
        if (f.localPawn) {
            const HeldItem* held = World_HeldBy(f.localPawn);
            FVector gp{};
            if (held && World_ItemMeshPos(held->actor, gp) && (gp - f.cam.Location).Length() < 150.0)
                muzzle = gp + fw * 45.0;
        }
        Tracer tr{};
        tr.a = muzzle;
        tr.cam = f.cam.Location; tr.fw = fw;
        tr.enemyDist = enemy;
        tr.shotAt = now;
        tr.t = now;
        tr.hit = enemy < 3999.0;
        tr.b = f.cam.Location + fw * (enemy < 3999.0 ? enemy : 4000.0);
        tr.probe = Visibility_ProbeStart(f.cam.Location, fw, 15000.0);
        tr.pending = tr.probe != 0;
        tr.draw = g_Misc.Tracers;
        g_tracers.push_back(tr);
    }
    g_lastShots = shots;
    for (Tracer& t : g_tracers) {
        if (!t.pending) continue;
        double dist = 0.0;
        const int r = Visibility_ProbeResult(t.probe, dist);
        if (r == 1 && dist > 1.0) {
            t.b = t.cam + t.fw * dist;
            t.hit = t.enemyDist <= dist + 60.0;
            if (t.hit) t.b = t.cam + t.fw * (std::min)(dist, t.enemyDist + 20.0);
        }
        if (r != 0 || now - t.shotAt > 0.25) { t.pending = false; t.t = now; }
    }
    {
        const double keep = (std::max)(static_cast<double>(g_Misc.TracerSeconds), 1.0);
        while (!g_tracers.empty() && !g_tracers.front().pending && now - g_tracers.front().t > keep) g_tracers.pop_front();
    }

    for (Marker& m : g_markers) {
        if (m.placed || now - m.t > 0.4) continue;
        FVector base = m.pos;
        double rad = 45.0, hh = 95.0;
        bool live = false;
        for (const Target& t : f.targets)
            if (t.pawn == m.pawn) { base = t.pos; rad = t.radius; hh = t.halfHeight; live = true; break; }
        if (!live) base = { m.pos.X, m.pos.Y, m.pos.Z - hh * 0.45 };
        for (auto it = g_tracers.rbegin(); it != g_tracers.rend(); ++it) {
            const Tracer& t = *it;
            if (t.pending || t.shotAt > m.t + 0.05 || t.shotAt < m.t - 0.5) continue;
            const double dx = t.b.X - base.X, dy = t.b.Y - base.Y, dz = t.b.Z - base.Z;
            if (dx * dx + dy * dy > (rad + 50.0) * (rad + 50.0) || std::fabs(dz) > hh + 35.0) continue;
            if (m.kill || !live) m.pos = t.b;
            else m.off = t.b - base;
            if (!m.placed && now - m.t < 0.1) m.t = now;
            m.placed = true;
            break;
        }
    }
    while (g_tracers.size() > 160) g_tracers.pop_front();
}

void Fx_Render(ImDrawList* dl, float  , float  ) {
    if (!dl) return;
    const double now = Targets_Get().time;

    if (g_Misc.Footsteps) {
        for (const Step& s : g_steps) {
            const float k = static_cast<float>((now - s.t) / 1.4);
            const double r = (s.loud ? 25.0 : 15.0) + k * (s.loud ? 70.0 : 45.0);
            ImVec2 pts[20];
            int n = 0;
            for (int i = 0; i < 20; ++i) {
                const double a = i * 6.2831853 / 20;
                if (!ESP_Project({ s.pos.X + std::cos(a) * r, s.pos.Y + std::sin(a) * r, s.pos.Z }, pts[n])) { n = 0; break; }
                ++n;
            }
            if (n == 20) dl->AddPolyline(pts, n, Col(g_Misc.FootstepColor, 1.0f - k), ImDrawFlags_Closed, s.loud ? 2.0f : 1.4f);
        }
    }

    if (g_Misc.Tracers) {
        const float dur = g_Misc.TracerSeconds > 0.1f ? g_Misc.TracerSeconds : 0.1f;
        const float th = g_Misc.TracerThickness < 0.5f ? 0.5f : g_Misc.TracerThickness > 3.0f ? 3.0f : g_Misc.TracerThickness;
        const int style = g_Misc.TracerStyle < 0 || g_Misc.TracerStyle >= TracerStyle_Count ? 0 : g_Misc.TracerStyle;
        const ImVec4& tc = g_Misc.TracerColor;
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        auto quad = [&](ImVec2 pa, ImVec2 pb, float w0, float w1, ImU32 c0, ImU32 c1) {
            const ImVec2 d = pb - pa;
            const float len = ImSqrt(d.x * d.x + d.y * d.y);
            if (len < 0.5f) return;
            const ImVec2 n(-d.y / len, d.x / len);
            dl->PrimReserve(6, 4);
            const ImDrawIdx i0 = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(pa + n * w0, uv, c0); dl->PrimWriteVtx(pa - n * w0, uv, c0);
            dl->PrimWriteVtx(pb - n * w1, uv, c1); dl->PrimWriteVtx(pb + n * w1, uv, c1);
            dl->PrimWriteIdx(i0); dl->PrimWriteIdx(static_cast<ImDrawIdx>(i0 + 1)); dl->PrimWriteIdx(static_cast<ImDrawIdx>(i0 + 2));
            dl->PrimWriteIdx(i0); dl->PrimWriteIdx(static_cast<ImDrawIdx>(i0 + 2)); dl->PrimWriteIdx(static_cast<ImDrawIdx>(i0 + 3));
        };
        auto hue = [](float h, float a) {
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.75f, 1.0f, r, g, b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a < 0 ? 0 : a > 1 ? 1 : a));
        };
        const float clock = static_cast<float>(now);
        for (const Tracer& t : g_tracers) {
            if (t.pending || !t.draw) continue;
            const float age = static_cast<float>(now - t.t), k = age / dur;
            if (k >= 1.0f) continue;
            const float head = ImMin(1.0f, age / 0.05f);
            const float fk = k < 0.5f ? 0.0f : (k - 0.5f) / 0.5f;
            const float fade = (1.0f - fk) * (1.0f - fk * 0.5f);
            const float thin = th * (1.0f - 0.35f * fk);
            auto at = [&](float u) { return t.a + (t.b - t.a) * static_cast<double>(u); };
            ImVec2 pa, pb;

            switch (style) {
            default:
            case TracerStyle_Beam:
                if (!ESP_ProjectSegment(t.a, at(head), pa, pb)) continue;
                quad(pa, pb, 1.2f * thin, 3.0f * thin, Col(tc, 0.05f * fade), Col(tc, 0.22f * fade));
                quad(pa, pb, 0.45f * thin, 1.0f * thin, Col(tc, 0.35f * fade), Col(tc, fade));
                break;
            case TracerStyle_Line:
                if (!ESP_ProjectSegment(t.a, at(head), pa, pb)) continue;
                dl->AddLine(pa, pb, IM_COL32(0, 0, 0, static_cast<int>(150 * fade)), 2.6f * thin);
                dl->AddLine(pa, pb, Col(tc, fade), 1.2f * thin);
                break;
            case TracerStyle_Laser:
                if (!ESP_ProjectSegment(t.a, at(head), pa, pb)) continue;
                quad(pa, pb, 2.5f * thin, 5.0f * thin, Col(tc, 0.10f * fade), Col(tc, 0.30f * fade));
                quad(pa, pb, 1.0f * thin, 2.0f * thin, Col(tc, 0.45f * fade), Col(tc, 0.85f * fade));
                quad(pa, pb, 0.35f * thin, 0.7f * thin, IM_COL32(255, 255, 255, static_cast<int>(150 * fade)), IM_COL32(255, 255, 255, static_cast<int>(255 * fade)));
                break;
            case TracerStyle_Dashed: {
                if (!ESP_ProjectSegment(t.a, at(head), pa, pb)) continue;
                const ImVec2 d = pb - pa;
                const float len = ImSqrt(d.x * d.x + d.y * d.y);
                if (len < 1.0f) break;
                const ImVec2 u = d / len;
                const float dash = 9.0f * th, gap = 6.0f * th, period = dash + gap;
                const float shift = std::fmod(age * 60.0f, period);
                for (float s0 = shift - period; s0 < len; s0 += period) {
                    const float x0 = ImMax(0.0f, s0), x1 = ImMin(len, s0 + dash);
                    if (x1 <= x0) continue;
                    const float al = fade * (0.35f + 0.65f * (x1 / len));
                    dl->AddLine(pa + u * x0, pa + u * x1, IM_COL32(0, 0, 0, static_cast<int>(130 * al)), 2.8f * thin);
                    dl->AddLine(pa + u * x0, pa + u * x1, Col(tc, al), 1.4f * thin);
                }
                break;
            }
            case TracerStyle_Comet: {
                const float travel = ImMin(1.0f, k / 0.4f);
                const float e = 1.0f - (1.0f - travel) * (1.0f - travel);
                const float tail = ImMax(0.0f, e - 0.35f);
                if (!ESP_ProjectSegment(at(tail), at(e), pa, pb)) continue;
                quad(pa, pb, 0.2f * thin, 2.6f * thin, Col(tc, 0.0f), Col(tc, 0.35f * fade));
                quad(pa, pb, 0.1f * thin, 1.1f * thin, Col(tc, 0.0f), Col(tc, fade));
                dl->AddCircleFilled(pb, 2.6f * thin, Col(tc, 0.4f * fade), 12);
                dl->AddCircleFilled(pb, 1.5f * thin, IM_COL32(255, 255, 255, static_cast<int>(240 * fade)), 10);
                break;
            }
            case TracerStyle_Rainbow: {
                constexpr int kPieces = 24;
                for (int i = 0; i < kPieces; ++i) {
                    const float u0 = head * i / kPieces, u1 = head * (i + 1) / kPieces;
                    ImVec2 qa, qb;
                    if (!ESP_ProjectSegment(at(u0), at(u1), qa, qb)) continue;
                    const float al = fade * (0.35f + 0.65f * u1);
                    const float h0 = u0 * 0.8f - clock * 0.6f, h1 = u1 * 0.8f - clock * 0.6f;
                    quad(qa, qb, 2.4f * thin, 2.4f * thin, hue(h0, 0.18f * al), hue(h1, 0.18f * al));
                    quad(qa, qb, 0.9f * thin, 0.9f * thin, hue(h0, al), hue(h1, al));
                }
                if (!ESP_ProjectSegment(t.a, at(head), pa, pb)) continue;
                break;
            }
            }

            const bool comet = style == TracerStyle_Comet;
            if (head < 1.0f && !comet) { dl->AddCircleFilled(pb, 2.0f, IM_COL32(255, 255, 255, static_cast<int>(240 * fade)), 10); continue; }
            if (!g_Misc.TracerImpact) continue;
            if (comet && k < 0.4f) continue;
            ImVec2 pe;
            if (!ESP_Project(t.b, pe)) continue;
            const ImU32 ic = style == TracerStyle_Rainbow ? hue(0.8f - clock * 0.6f, fade) : Col(tc, fade);
            if (t.hit) {
                const float since = comet ? age - 0.4f * dur : age - 0.05f;
                const float ik = ImMin(1.0f, since / 0.35f);
                if (ik < 1.0f) dl->AddCircle(pe, 2.5f + 7.0f * ik, WithAlpha(ic, 1.0f - ik), 18, 1.4f);
                dl->AddCircleFilled(pe, 2.2f, ic, 10);
            } else {
                dl->AddCircleFilled(pe, 1.6f, WithAlpha(ic, 0.8f), 8);
            }
        }
    }

    if (g_Misc.HitMarker && !g_markers.empty()) {
        const TargetFrame& f = Targets_Get();
        const float size = g_Misc.HitMarkerSize < 3.0f ? 3.0f : g_Misc.HitMarkerSize > 12.0f ? 12.0f : g_Misc.HitMarkerSize;
        for (const Marker& m : g_markers) {
            const float k = static_cast<float>((now - m.t) / kMarkerLife);
            if (k >= 1.0f) continue;
            if (!m.placed && now - m.t < 0.1) continue;
            FVector wp = m.pos;
            if (m.pawn && !m.kill)
                for (const Target& t : f.targets)
                    if (t.pawn == m.pawn) {
                        wp = m.placed ? t.pos + m.off : FVector{ t.pos.X, t.pos.Y, t.pos.Z + t.halfHeight * 0.45 };
                        break;
                    }
            ImVec2 c;
            if ((wp.X == 0.0 && wp.Y == 0.0 && wp.Z == 0.0) || !ESP_Project(wp, c)) continue;
            const float pk = k * 5.0f < 1.0f ? k * 5.0f : 1.0f;
            const float pop = 1.0f + 0.15f * (1.0f - pk);
            const float arm = size * 0.75f * (m.kill ? 1.2f : 1.0f) * pop;
            const float gap = 2.5f + 1.0f * k;
            const float fade = k < 0.5f ? 1.0f : 1.0f - (k - 0.5f) / 0.5f;
            const ImU32 col = Col(m.kill ? g_Misc.KillColor : g_Misc.HitColor, fade);
            const ImU32 sh = IM_COL32(0, 0, 0, static_cast<int>(120 * fade));
            const float th = m.kill ? 1.6f : 1.3f;
            for (int i = 0; i < 4; ++i) {
                const float dx = (i & 1) ? 0.7071f : -0.7071f, dy = (i & 2) ? 0.7071f : -0.7071f;
                const ImVec2 a(c.x + dx * gap, c.y + dy * gap), b(c.x + dx * (gap + arm), c.y + dy * (gap + arm));
                dl->AddLine(a, b, sh, th + 1.2f);
                dl->AddLine(a, b, col, th);
            }
        }
    }
}

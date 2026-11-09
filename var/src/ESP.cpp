#include "../include/ESP.hpp"
#include "../include/Visibility.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/World.hpp"
#include "../include/WeaponIcons.hpp"
#include "../include/Lens.hpp"
#include <imgui.h>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

struct Projector {
    bool valid = false;
    FCameraCache cam{};
    float fov = 90.0f, w = 0.0f, h = 0.0f;

    bool operator()(const FVector& world, ImVec2& out) const {
        if (!valid) return false;
        float x = 0.0f, y = 0.0f;
        if (!WorldToScreen(world, cam.Location, cam.Rotation, fov, w, h, x, y)) return false;
        if (Lens_Get().valid) Lens_Apply(x, y, w, h);
        out = {x, y};
        return std::isfinite(x) && std::isfinite(y);
    }
};
Projector g_proj;
constexpr float kFovAdjust = -8.0f;     // camera FOV vs the picture actually rendered
constexpr double kDroneDelay = 0.060;    // drone feed lags the camera by ~60 ms

ImU32 Col(const ImVec4& v, float alphaMul = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x, v.y, v.z, v.w * alphaMul));
}

void OutlinedText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* text) {
    const ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.8f));
    dl->AddText({p.x + 1.0f, p.y + 1.0f}, shadow, text);
    dl->AddText(p, col, text);
}

void CenteredText(ImDrawList* dl, float cx, float y, ImU32 col, const char* text) {
    const ImVec2 sz = ImGui::CalcTextSize(text);
    OutlinedText(dl, {cx - sz.x * 0.5f, y}, col, text);
}

struct Rect { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; float W() const { return x1 - x0; } float H() const { return y1 - y0; } };

void CornerBox(ImDrawList* dl, const Rect& b, ImU32 col, float th) {
    const float cw = (std::max)(3.0f, b.W() * 0.28f), ch = (std::max)(3.0f, b.H() * 0.20f);
    auto L = [&](float ax, float ay, float bx, float by) { dl->AddLine({ax, ay}, {bx, by}, col, th); };
    L(b.x0, b.y0, b.x0 + cw, b.y0); L(b.x0, b.y0, b.x0, b.y0 + ch);
    L(b.x1, b.y0, b.x1 - cw, b.y0); L(b.x1, b.y0, b.x1, b.y0 + ch);
    L(b.x0, b.y1, b.x0 + cw, b.y1); L(b.x0, b.y1, b.x0, b.y1 - ch);
    L(b.x1, b.y1, b.x1 - cw, b.y1); L(b.x1, b.y1, b.x1, b.y1 - ch);
}

double AngleDiff(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

bool CapsuleRect(const FVector& pos, float radius, float halfHeight, const FVector& rightDir,
                 float sw, float sh, Rect& r) {
    const FVector side = rightDir * static_cast<double>(radius);
    const FVector up{0.0, 0.0, static_cast<double>(halfHeight)};
    ImVec2 p[4];
    if (!g_proj(pos + up - side, p[0]) || !g_proj(pos + up + side, p[1]) ||
        !g_proj(pos - up + side, p[2]) || !g_proj(pos - up - side, p[3])) return false;
    r.x0 = (std::min)({p[0].x, p[1].x, p[2].x, p[3].x});
    r.x1 = (std::max)({p[0].x, p[1].x, p[2].x, p[3].x});
    r.y0 = (std::min)({p[0].y, p[1].y, p[2].y, p[3].y});
    r.y1 = (std::max)({p[0].y, p[1].y, p[2].y, p[3].y});
    return r.W() >= 1.0f && r.H() >= 1.0f && r.W() < sw * 4.0f && r.H() < sh * 4.0f &&
           r.x1 > -sw && r.x0 < sw * 2.0f && r.y1 > -sh && r.y0 < sh * 2.0f;
}

bool OnScreen(const Rect& r, float sw, float sh) {
    return r.x1 > 0.0f && r.x0 < sw && r.y1 > 0.0f && r.y0 < sh;
}

void Box3DCorners(ImDrawList* dl, const FVector& pos, double yawDeg, float radius, float halfHeight, ImU32 col, float th) {
    const double a = DegToRad(yawDeg), cs = std::cos(a), sn = std::sin(a);
    const double hx = radius, hy = radius * 0.8, hz = halfHeight;
    ImVec2 c[8];
    for (int i = 0; i < 8; ++i) {
        const double lx = (i & 1) ? hx : -hx, ly = (i & 2) ? hy : -hy, lz = (i & 4) ? hz : -hz;
        if (!g_proj({pos.X + lx * cs - ly * sn, pos.Y + lx * sn + ly * cs, pos.Z + lz}, c[i])) return;
    }
    const float k = 0.25f;
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 cc = pass == 0 ? IM_COL32(0, 0, 0, 180) : col;
        const float w = pass == 0 ? th + 1.5f : th;
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit <= 4; bit <<= 1) {
                const ImVec2 o = c[i], d = c[i ^ bit];
                dl->AddLine(o, { o.x + (d.x - o.x) * k, o.y + (d.y - o.y) * k }, cc, w);
            }
    }
}

void Box3D(ImDrawList* dl, const FVector& pos, double yawDeg, float radius, float halfHeight, ImU32 col, float th) {
    const double a = DegToRad(yawDeg), cs = std::cos(a), sn = std::sin(a);
    const double hx = radius, hy = radius * 0.8, hz = halfHeight;
    ImVec2 c[8];
    for (int i = 0; i < 8; ++i) {
        const double lx = (i & 1) ? hx : -hx, ly = (i & 2) ? hy : -hy, lz = (i & 4) ? hz : -hz;
        if (!g_proj({pos.X + lx * cs - ly * sn, pos.Y + lx * sn + ly * cs, pos.Z + lz}, c[i])) return;
    }
    static const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& k : e) dl->AddLine(c[k[0]], c[k[1]], IM_COL32(0, 0, 0, 180), th + 1.5f);
    for (const auto& k : e) dl->AddLine(c[k[0]], c[k[1]], col, th);
}

float HealthBar(ImDrawList* dl, const ESPConfig& cfg, const Rect& b, double hp, double maxHp, float alpha) {
    const float ratio = (std::clamp)(static_cast<float>(hp / maxHp), 0.0f, 1.0f);
    const float th = (std::clamp)(cfg.HealthWidth, 1.0f, 10.0f);
    const bool vertical = cfg.HealthPos == HealthPos_Left || cfg.HealthPos == HealthPos_Right;
    const float gap = 4.0f;
    float x0, y0, x1, y1;
    switch (cfg.HealthPos) {
    case HealthPos_Right:  x0 = b.x1 + gap; x1 = x0 + th; y0 = b.y0; y1 = b.y1; break;
    case HealthPos_Top:    y1 = b.y0 - gap; y0 = y1 - th; x0 = b.x0; x1 = b.x1; break;
    case HealthPos_Bottom: y0 = b.y1 + gap; y1 = y0 + th; x0 = b.x0; x1 = b.x1; break;
    default:               x1 = b.x0 - gap; x0 = x1 - th; y0 = b.y0; y1 = b.y1; break;
    }
    const float fx1 = vertical ? x1 : x0 + (x1 - x0) * ratio;
    const float fy0 = vertical ? y1 - (y1 - y0) * ratio : y0;

    const ImVec4 lo = cfg.HealthLowColor, hi = cfg.HealthFullColor;
    auto lerp = [&](float t) { return ImVec4(lo.x + (hi.x - lo.x) * t, lo.y + (hi.y - lo.y) * t, lo.z + (hi.z - lo.z) * t, lo.w + (hi.w - lo.w) * t); };
    if (cfg.HealthBackground) dl->AddRectFilled({x0, y0}, {x1, y1}, IM_COL32(0, 0, 0, static_cast<int>(150 * alpha)));
    if (cfg.HealthOutline) dl->AddRect({x0 - 1.0f, y0 - 1.0f}, {x1 + 1.0f, y1 + 1.0f}, IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)));
    if (ratio > 0.0f) {
        switch (cfg.HealthColorMode) {
        case HealthColor_Solid:
            dl->AddRectFilled({x0, fy0}, {fx1, y1}, Col(hi, alpha));
            break;
        case HealthColor_Fade:
            dl->AddRectFilled({x0, fy0}, {fx1, y1}, Col(lerp(ratio), alpha));
            break;
        default: {
            const ImU32 cLow = Col(lo, alpha), cTop = Col(lerp(ratio), alpha);
            if (vertical) dl->AddRectFilledMultiColor({x0, fy0}, {fx1, y1}, cTop, cTop, cLow, cLow);
            else          dl->AddRectFilledMultiColor({x0, fy0}, {fx1, y1}, cLow, cTop, cTop, cLow);
            break;
        }
        }
    }
    if (cfg.ShowHealthText && ratio < 0.999f) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f", hp);
        const ImVec2 sz = ImGui::CalcTextSize(buf);
        ImVec2 tp;
        switch (cfg.HealthPos) {
        case HealthPos_Right:  tp = {x1 + 2.0f, fy0 - sz.y * 0.5f}; break;
        case HealthPos_Top:
        case HealthPos_Bottom: tp = {fx1 - sz.x * 0.5f, (y0 + y1) * 0.5f - sz.y * 0.5f}; break;
        default:               tp = {x0 - sz.x - 2.0f, fy0 - sz.y * 0.5f}; break;
        }
        OutlinedText(dl, tp, IM_COL32(230, 230, 230, static_cast<int>(230 * alpha)), buf);
    }
    return vertical ? 0.0f : th + gap + 1.0f;
}

void OffscreenArrow(ImDrawList* dl, const ESPConfig& cfg, const FCameraCache& cam, const FVector& pos,
                    float sw, float sh, ImU32 col) {
    const FRotator need = CalcAngle(cam.Location, pos);
    const double rel = DegToRad(AngleDiff(need.Yaw, cam.Rotation.Yaw));
    const float cx = sw * 0.5f, cy = sh * 0.5f;
    const float dx = static_cast<float>(std::sin(rel)), dy = static_cast<float>(-std::cos(rel));
    const ImVec2 tip{cx + dx * cfg.OffscreenRadius, cy + dy * cfg.OffscreenRadius};
    const float s = cfg.OffscreenSize;
    const ImVec2 back{tip.x - dx * s * 1.6f, tip.y - dy * s * 1.6f};
    const ImVec2 l{back.x - dy * s * 0.75f, back.y + dx * s * 0.75f};
    const ImVec2 r{back.x + dy * s * 0.75f, back.y - dx * s * 0.75f};
    dl->AddTriangleFilled(tip, l, r, col);
    dl->AddTriangle(tip, l, r, IM_COL32(0, 0, 0, 200), 1.0f);
}

}

bool ESP_Project(const FVector& world, ImVec2& out) { return g_proj(world, out); }

bool ESP_ProjectSegment(const FVector& a, const FVector& b, ImVec2& outA, ImVec2& outB) {
    if (!g_proj.valid) return false;
    const double pitch = DegToRad(g_proj.cam.Rotation.Pitch), yaw = DegToRad(g_proj.cam.Rotation.Yaw);
    const FVector fw{ std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
    constexpr double kNear = 50.0;
    const double da = (a - g_proj.cam.Location).Dot(fw) - kNear, db = (b - g_proj.cam.Location).Dot(fw) - kNear;
    if (da < 0.0 && db < 0.0) return false;
    FVector pa = a, pb = b;
    if (da < 0.0) pa = a + (b - a) * (da / (da - db));
    if (db < 0.0) pb = b + (a - b) * (db / (db - da));
    return g_proj(pa, outA) && g_proj(pb, outB);
}

extern ESPConfig g_ESP;

namespace {
    struct TrailPt { double t; FVector p; };
    struct Trail { std::vector<TrailPt> pts; ImVec4 col; double goneAt = -1.0; };
    std::unordered_map<uintptr_t, Trail> g_trails;

    void DrawGrenadeTrails(const ESPConfig& cfg, ImDrawList* dl) {
        const double now = Targets_Now();
        for (auto& kv : g_trails) if (kv.second.goneAt < 0.0) kv.second.goneAt = now;
        for (const ThrownGrenade& g : World_Grenades()) {
            const bool want = cfg.ShowGrenades;
            if (!want || g.pos.LengthSq() < 1.0) continue;
            Trail& tr = g_trails[g.actor];
            tr.goneAt = -1.0;
            tr.col = g.friendly ? cfg.GrenadeFriendlyColor : cfg.GrenadeColor;
            if (tr.pts.empty() || (tr.pts.back().p - g.pos).LengthSq() > 4.0) tr.pts.push_back({ now, g.pos });
            if (tr.pts.size() > 600) tr.pts.erase(tr.pts.begin());
        }
        for (auto it = g_trails.begin(); it != g_trails.end();) {
            Trail& tr = it->second;
            const double fadeOut = tr.goneAt < 0.0 ? 1.0 : 1.0 - (now - tr.goneAt) / 1.5;
            if (fadeOut <= 0.0 || tr.pts.size() < 2) {
                if (fadeOut <= 0.0) { it = g_trails.erase(it); continue; }
                ++it; continue;
            }
            const size_t n = tr.pts.size();
            const float a = static_cast<float>(fadeOut);
            for (size_t i = 1; i < n; ++i) {
                ImVec2 pa, pb;
                if (!ESP_ProjectSegment(tr.pts[i - 1].p, tr.pts[i].p, pa, pb)) continue;
                const float k = static_cast<float>(i) / static_cast<float>(n - 1);
                const float al = a * (0.2f + 0.8f * k);
                dl->AddLine(pa, pb, IM_COL32(0, 0, 0, static_cast<int>(120 * al)), 4.0f);
                dl->AddLine(pa, pb, Col(tr.col, 0.25f * al), 5.0f);
                dl->AddLine(pa, pb, Col(tr.col, al), 1.8f);
            }
            if (tr.goneAt < 0.0) {
                ImVec2 hp;
                if (g_proj(tr.pts.back().p, hp)) {
                    dl->AddCircleFilled(hp, 3.2f, Col(tr.col, 0.35f * a), 12);
                    dl->AddCircleFilled(hp, 1.8f, IM_COL32(255, 255, 255, static_cast<int>(230 * a)), 10);
                }
            }
            ++it;
        }
    }
}

void ESP_Render(const ESPConfig& cfg, ImDrawList* dl, float sw, float sh) {
    const TargetFrame& frame = Targets_Get();
    g_proj.valid = false;
    if (!dl || sw < 1.0f || sh < 1.0f || !frame.camValid) return;

    bool droneView = false;
    if (frame.localPawn) {
        const FVector lp = GetActorLocation(frame.localPawn);
        droneView = (frame.cam.Location - lp).Length() > 250.0;
    }

    const double delay = droneView ? kDroneDelay : 0.0;
    const double renderT = frame.time - delay;
    FCameraCache cam = delay > 0.0005 ? Targets_CameraAt(renderT, frame.cam) : frame.cam;
    if (!Targets_CameraUsable(cam)) cam = frame.cam;

    g_proj.cam = cam;
    g_proj.fov = (std::clamp)(cam.FOV + kFovAdjust, 5.0f, 175.0f);
    g_proj.w = sw; g_proj.h = sh;
    g_proj.valid = true;
    DrawGrenadeTrails(cfg, dl);
    if (!cfg.Enabled) return;

    const double yaw = DegToRad(cam.Rotation.Yaw);
    const FVector rightDir{-std::sin(yaw), std::cos(yaw), 0.0};

    int enemies = 0, visibleEnemies = 0;

    for (const Target& t : frame.targets) {
        if (t.drone && (!cfg.ShowDrones || (t.friendly && !cfg.ShowFriendlyDrones))) continue;
        const bool mate = t.friendly && !t.drone;
        if (mate && !cfg.ShowTeammates) continue;
        if (t.distM > cfg.MaxDistance) continue;
        if (!t.drone && !mate) { ++enemies; if (t.visible) ++visibleEnemies; }

        const FVector pos = delay > 0.0005 ? Targets_PositionAt(t.pawn, renderT, t.pos) : t.pos;
        const FVector shift = pos - t.pos;

        float alpha = t.stale ? 0.6f : 1.0f;
        if (cfg.DistanceFade && cfg.MaxDistance > 1.0f)
            alpha *= (std::clamp)(1.25f - t.distM / cfg.MaxDistance, 0.25f, 1.0f);

        ImVec4 base = t.drone ? (t.friendly ? cfg.FriendlyDroneColor : cfg.DroneColor)
                    : mate ? cfg.TeammateColor
                    : (cfg.VisibilityColors && t.visible) ? cfg.VisibleColor : cfg.HiddenColor;
        const ImU32 col = Col(base, alpha);
        const ImVec4 boxBase = (t.drone || mate) ? base
                             : cfg.VisibilityColors ? (t.visible ? cfg.VisibleColor : cfg.HiddenColor) : cfg.BoxColor;
        const ImVec4 boneBase = mate ? cfg.TeammateColor : cfg.BoneColor;
        const ImU32 boxCol = Col(boxBase, alpha);

        Rect b;
        const bool hasRect = CapsuleRect(pos, t.radius, t.halfHeight, rightDir, sw, sh, b);

        if (!hasRect || !OnScreen(b, sw, sh)) {
            if (cfg.ShowOffscreen)
                OffscreenArrow(dl, cfg, cam, pos, sw, sh, cfg.OffscreenCustomColor && !mate && !t.drone ? Col(cfg.OffscreenColor, alpha) : col);
            if (!hasRect) continue;
        }

        const float cx = (b.x0 + b.x1) * 0.5f;

        if (cfg.ShowSnaplines) {
            const float oy = cfg.SnaplineOrigin == SnapOrigin_Top ? 0.0f
                           : cfg.SnaplineOrigin == SnapOrigin_Center ? sh * 0.5f : sh;
            const float ty = cfg.SnaplineOrigin == SnapOrigin_Top ? b.y0 : b.y1;
            dl->AddLine({sw * 0.5f, oy}, {cx, ty}, cfg.SnapCustomColor ? Col(cfg.SnapColor, alpha) : Col(base, alpha * 0.6f),
                        (std::clamp)(cfg.SnapThickness, 0.5f, 5.0f));
        }

        if (cfg.FilledBoxes) dl->AddRectFilled({b.x0, b.y0}, {b.x1, b.y1}, Col(boxBase, alpha * 0.15f));
        if (cfg.ShowBoxes) {
            switch (cfg.BoxStyle) {
            case BoxStyle_3D:
                Box3D(dl, pos, t.yaw, t.radius, t.halfHeight, boxCol, cfg.BoxThickness);
                break;
            case BoxStyle_3DCorner:
                Box3DCorners(dl, pos, t.yaw, t.radius, t.halfHeight, boxCol, cfg.BoxThickness);
                break;
            case BoxStyle_Corner:
                CornerBox(dl, b, IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)), cfg.BoxThickness + 1.5f);
                CornerBox(dl, b, boxCol, cfg.BoxThickness);
                break;
            default:
                dl->AddRect({b.x0 - 1, b.y0 - 1}, {b.x1 + 1, b.y1 + 1}, IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)),
                            0.0f, 0, cfg.BoxThickness + 1.0f);
                dl->AddRect({b.x0, b.y0}, {b.x1, b.y1}, boxCol, 0.0f, 0, cfg.BoxThickness);
                break;
            }
        }

        FVector headWorld = pos + FVector{0.0, 0.0, t.halfHeight * 0.80};
        if (!t.drone && (cfg.ShowBones || cfg.ShowHeadCircle || cfg.ShowViewDir)) {
            if (cfg.ShowBones) {
                SkeletonPose pose;
                if (Skeleton_GetPose(t.pawn, pose)) {
                    const ImU32 boneCol = Col(boneBase, alpha);
                    for (const auto& [wa, wb] : pose.lines) {
                        ImVec2 sa, sb;
                        if (g_proj(wa + shift, sa) && g_proj(wb + shift, sb))
                            dl->AddLine(sa, sb, boneCol, cfg.BoneThickness);
                    }
                }
            }
            SkeletonAimBones bones;
            if ((cfg.ShowHeadCircle || cfg.ShowViewDir) && Skeleton_GetAimBones(t.pawn, bones) && bones.hasHead)
                headWorld = bones.head + shift + FVector{0.0, 0.0, 6.0};

            ImVec2 hs;
            if (cfg.ShowHeadCircle && g_proj(headWorld, hs)) {
                ImVec2 edge;
                float r = 4.0f;
                if (g_proj(headWorld + rightDir * 11.0, edge)) r = (std::clamp)(std::fabs(edge.x - hs.x), 2.0f, 60.0f);
                dl->AddCircle(hs, r, Col(boneBase, alpha), std::clamp(static_cast<int>(r * 1.5f), 24, 96), cfg.BoneThickness);
            }
            if (cfg.ShowViewDir) {
                const double a = DegToRad(t.yaw);
                const FVector tip = headWorld + FVector{std::cos(a), std::sin(a), 0.0} * cfg.ViewDirLength;
                ImVec2 s0, s1;
                if (g_proj(headWorld, s0) && g_proj(tip, s1))
                    dl->AddLine(s0, s1, cfg.ViewDirCustomColor ? Col(cfg.ViewDirColor, alpha) : Col(base, alpha * 0.9f), 1.0f);
            }
        }

        float topPad = 0.0f, bottomPad = 0.0f;
        if (cfg.ShowHealth && !t.drone && t.health >= 0.0) {
            const float used = HealthBar(dl, cfg, b, t.health, t.maxHealth, alpha);
            if (cfg.HealthPos == HealthPos_Top) topPad = used;
            else if (cfg.HealthPos == HealthPos_Bottom) bottomPad = used;
        }

        std::string top;
        if (cfg.ShowTypeTag && !t.drone) top = t.bot ? "[BOT]" : "[PLAYER]";
        if (t.drone) {
            if (cfg.ShowNames || cfg.DroneLabel) top = t.friendly ? "Friendly drone" : "Enemy drone";
        } else if (cfg.ShowNames) {
            std::string name = t.name;
            if (name.empty()) name = t.bot ? "Bot" : "Player";
            if (!top.empty()) top += ' ';
            top += name;
        }
        if (!top.empty()) {
            const ImVec2 sz = ImGui::CalcTextSize(top.c_str());
            const ImU32 nameCol = (cfg.NameCustomColor && !t.drone) ? Col(cfg.NameColor, alpha) : col;
            OutlinedText(dl, {cx - sz.x * 0.5f, b.y0 - topPad - sz.y - 3.0f}, nameCol, top.c_str());
        }

        float by = b.y1 + 2.0f + bottomPad;
        if (cfg.ShowDistance) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0fm", t.distM);
            CenteredText(dl, cx, by, Col(cfg.DistanceColor, alpha), buf);
            by += ImGui::GetFontSize() + 1.0f;
        }

        if (cfg.ShowWeapon && !t.drone) {
            if (const HeldItem* held = World_HeldBy(t.pawn)) {
                const ImU32 wc = Col(cfg.WeaponColor, alpha);
                const bool reload = cfg.WeaponReloading && held->reloading;
                if (cfg.WeaponMode != WeaponMode_Text) {
                    const float ih = (std::clamp)(b.W() * 0.22f, 8.0f, 16.0f);
                    const float iw = WeaponIcon_Width(held->kind, ih, held->name.c_str());
                    WeaponIcon_Draw(dl, held->kind, {cx - iw * 0.5f, by + 1.0f}, ih, wc, true, held->name.c_str());
                    by += ih + 3.0f;
                }
                if (cfg.WeaponMode != WeaponMode_Icon || reload) {
                    std::string w = cfg.WeaponMode == WeaponMode_Icon ? std::string() : held->name;
                    if (reload) w += w.empty() ? "reloading" : " (reloading)";
                    CenteredText(dl, cx, by, reload ? Col(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), alpha) : wc, w.c_str());
                }
            }
        }

    }

    if (cfg.ShowGrenades) {
        const double now = Targets_Now();
        for (const ThrownGrenade& g : World_Grenades()) {
            const float dist = static_cast<float>((g.pos - cam.Location).Length() / 100.0);
            if (dist > cfg.MaxDistance) continue;
            const ImU32 gc = Col(g.friendly ? cfg.GrenadeFriendlyColor : cfg.GrenadeColor);
            if (cfg.GrenadeRing && cfg.GrenadeRadius > 0.1f) {
                const double r = cfg.GrenadeRadius * 100.0;
                ImVec2 prev; bool havePrev = false;
                for (int i = 0; i <= 40; ++i) {
                    const double a = i * (2.0 * kPi / 40.0);
                    ImVec2 sp;
                    const bool ok = g_proj({g.pos.X + std::cos(a) * r, g.pos.Y + std::sin(a) * r, g.pos.Z}, sp);
                    if (ok && havePrev) dl->AddLine(prev, sp, Col(g.friendly ? cfg.GrenadeFriendlyColor : cfg.GrenadeColor, 0.45f), 1.5f);
                    prev = sp; havePrev = ok;
                }
            }
            ImVec2 sp;
            if (!g_proj(g.pos, sp) || sp.x < -50 || sp.x > sw + 50 || sp.y < -50 || sp.y > sh + 50) continue;
            const float ih = 14.0f, iw = WeaponIcon_Width(Weapon_Grenade, ih, g.name.c_str());
            dl->AddCircleFilled(sp, 12.0f, IM_COL32(0, 0, 0, 120), 20);
            dl->AddCircle(sp, 12.0f, gc, 20, 1.5f);
            WeaponIcon_Draw(dl, Weapon_Grenade, {sp.x - iw * 0.5f, sp.y - ih * 0.55f}, ih, gc, false, g.name.c_str());
            char buf[64];
            if (cfg.GrenadeTimer && g.fuse > 0.0) {
                const double left = (std::max)(0.0, g.fuse - (now - g.thrownAt));
                std::snprintf(buf, sizeof(buf), "%s  %.1fs  %.0fm", g.name.c_str(), left, dist);
                const float frac = static_cast<float>(left / g.fuse);
                if (frac > 0.0f) {
                    dl->PathArcTo(sp, 15.0f, -static_cast<float>(kPi) * 0.5f, -static_cast<float>(kPi) * 0.5f + static_cast<float>(kPi) * 2.0f * frac, 28);
                    dl->PathStroke(frac < 0.3f ? IM_COL32(255, 70, 70, 255) : gc, 0, 2.0f);
                }
            } else {
                std::snprintf(buf, sizeof(buf), "%s  %.0fm", g.name.c_str(), dist);
            }
            CenteredText(dl, sp.x, sp.y + 17.0f, gc, buf);
        }
    }

    if (cfg.ShowEnemyCount) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d enemies  (%d visible)", enemies, visibleEnemies);
        CenteredText(dl, sw * 0.5f, 12.0f, enemies ? IM_COL32(255, 90, 90, 255) : IM_COL32(200, 200, 200, 200), buf);
    }
}

float ESP_FovAdjust() { return kFovAdjust; }

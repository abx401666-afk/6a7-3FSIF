#include "../include/Aimbot.hpp"
#include "../include/ESP.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include <imgui.h>
bool Menu_Launched();
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
extern ESPConfig g_ESP;
#include <cstring>
#include <vector>

namespace {

constexpr ULONGLONG kTickMs              = 8;
constexpr double    kInitialDPC          = 0.30;
constexpr double    kMinDPC              = 0.010;
constexpr double    kMaxDPC              = 1.50;
constexpr int       kBootSamples         = 5;
constexpr ULONGLONG kLockGraceMs         = 120;
constexpr ULONGLONG kInFlightMs          = 40;
constexpr double    kHeadLift            = 7.0;

constexpr double kCapsuleHeight[AimBone_Count] = { 0.72, 0.58, 0.28, -0.15, -0.50 };

double AngleDiff(double target, double current) {
    double d = std::fmod(target - current, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

FVector Forward(const FRotator& r) {
    const double p = DegToRad(r.Pitch), y = DegToRad(r.Yaw);
    return { std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) };
}

double AngleTo(const FVector& from, const FRotator& view, const FVector& to) {
    const FVector d = (to - from).Normalized();
    return RadToDeg(std::acos(std::clamp(d.Dot(Forward(view)), -1.0, 1.0)));
}

struct Axis {
    double dpc = kInitialDPC;
    int    n = 0;
    std::vector<double> boot;

    void Reset() { dpc = kInitialDPC; n = 0; boot.clear(); }

    void Learn(double observed) {
        if (!std::isfinite(observed) || observed < kMinDPC || observed > kMaxDPC) return;
        if (n < kBootSamples) {
            boot.push_back(observed);
            ++n;
            if (boot.size() >= 3) {
                std::vector<double> s = boot;
                std::sort(s.begin(), s.end());
                dpc = s[s.size() / 2];
            }
            if (n >= kBootSamples) boot.clear();
            return;
        }
        const double ratio = observed / dpc;
        if (ratio < 0.5 || ratio > 2.0) return;
        dpc = std::clamp(dpc * 0.92 + observed * 0.08, kMinDPC, kMaxDPC);
        ++n;
    }
};

struct State {
    Axis yaw[2], pitch[2];
    int  mode = 0;

    bool      pending = false;
    FRotator  pendingFrom{};
    ULONGLONG pendingMs = 0;
    LONG      pendingX = 0, pendingY = 0;
    float     remX = 0.0f, remY = 0.0f;

    uintptr_t locked = 0;
    int       lockedBone = -1;
    ULONGLONG lockMissingSince = 0;
    ULONGLONG lostAt = 0;
    bool      haveAimPoint = false;
    FVector   aimPoint{};

    double    prevError = 0.0;
    int       diverging = 0;
    ULONGLONG pauseUntil = 0;

    bool toggled = false;
    bool keyWasDown = false;

    ULONGLONG lastTick = 0;

    uintptr_t trigCandidate = 0;
    ULONGLONG trigSince = 0;
    bool      trigDown = false;
    ULONGLONG trigDownAt = 0;
    ULONGLONG trigCooldownUntil = 0;
};
State g;

void ClearTarget() {
    if (g.locked) g.lostAt = GetTickCount64();
    g.locked = 0;
    g.lockedBone = -1;
    g.lockMissingSince = 0;
    g.haveAimPoint = false;
    g.prevError = 0.0;
    g.diverging = 0;
    g.remX = g.remY = 0.0f;
}

void DropPending() {
    g.pending = false;
    g.pendingX = g.pendingY = 0;
}

void LearnFromControl(const FRotator& control, ULONGLONG now) {
    if (!g.pending) return;
    const ULONGLONG elapsed = now - g.pendingMs;
    if (elapsed < 12) return;
    if (elapsed > 150) { DropPending(); return; }

    const double dYaw = AngleDiff(control.Yaw, g.pendingFrom.Yaw);
    const double dPitch = AngleDiff(control.Pitch, g.pendingFrom.Pitch);
    bool consumed = false;
    if (std::abs(g.pendingX) >= 2 && std::abs(dYaw) >= 0.02 && std::abs(dYaw) <= 12.0) {
        g.yaw[g.mode].Learn(dYaw / static_cast<double>(g.pendingX));
        consumed = true;
    }
    if (std::abs(g.pendingY) >= 2 && std::abs(dPitch) >= 0.02 && std::abs(dPitch) <= 12.0) {
        g.pitch[g.mode].Learn(dPitch / -static_cast<double>(g.pendingY));
        consumed = true;
    }
    if (consumed || (std::abs(g.pendingX) < 2 && std::abs(g.pendingY) < 2)) DropPending();
}

bool SendMouse(double dx, double dy, LONG& sx, LONG& sy) {
    const double ax = dx + g.remX, ay = dy + g.remY;
    sx = static_cast<LONG>(std::lround(ax));
    sy = static_cast<LONG>(std::lround(ay));
    g.remX = static_cast<float>(ax - sx);
    g.remY = static_cast<float>(ay - sy);
    if (sx == 0 && sy == 0) return false;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = sx;
    in.mi.dy = sy;
    if (SendInput(1, &in, sizeof(in)) != 1) { sx = sy = 0; return false; }
    return true;
}

void SendClick(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
}

bool KeyDown(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool InsideCapsule(const FVector& p, const Target& t) {
    return std::fabs(p.Z - t.pos.Z) <= t.halfHeight + 30.0 &&
           std::hypot(p.X - t.pos.X, p.Y - t.pos.Y) <= t.radius * 3.0 + 40.0;
}

bool BonePoint(const SkeletonAimBones& b, int bone, FVector& out) {
    switch (bone) {
    case AimBone_Head:   if (!b.hasHead) return false;   out = b.head; out.Z += kHeadLift; return true;
    case AimBone_Neck:   if (!b.hasNeck) return false;   out = b.neck;   return true;
    case AimBone_Chest:  if (!b.hasChest) return false;  out = b.chest;  return true;
    case AimBone_Pelvis: if (!b.hasPelvis) return false; out = b.pelvis; return true;
    case AimBone_Legs:   if (!b.hasLegs) return false;   out = b.legs;   return true;
    default: return false;
    }
}

bool AimPoint(const Target& t, const AimbotConfig& cfg, const FVector& eye, const FRotator& view,
              int prefer, FVector& out, int& chosen) {
    unsigned mask = cfg.TargetBones & ((1u << AimBone_Count) - 1u);
    if (mask == 0) mask = 1u << AimBone_Chest;
    if (t.drone) mask = 1u << AimBone_Chest;

    SkeletonAimBones bones;
    const bool skel = !t.drone && Skeleton_GetAimBones(t.pawn, bones);

    double best = 1e9, preferAngle = 1e9;
    FVector bestP{}, preferP{};
    int bestBone = -1;
    for (int i = 0; i < AimBone_Count; ++i) {
        if (!(mask & (1u << i))) continue;
        FVector p = t.pos;
        if (!t.drone) p.Z += t.halfHeight * kCapsuleHeight[i];
        FVector bp;
        if (skel && BonePoint(bones, i, bp) && InsideCapsule(bp, t)) p = bp;
        const double a = AngleTo(eye, view, p);
        if (!std::isfinite(a)) continue;
        if (i == prefer) { preferAngle = a; preferP = p; }
        if (a < best) { best = a; bestP = p; bestBone = i; }
    }
    if (bestBone < 0) return false;

    if (prefer >= 0 && preferAngle < 1e8 && preferAngle <= best + 2.0) { out = preferP; chosen = prefer; }
    else { out = bestP; chosen = bestBone; }

    if (cfg.Prediction && cfg.PredictionMs > 0.0f) {
        const double lead = std::clamp(static_cast<double>(cfg.PredictionMs), 0.0, 400.0) / 1000.0;
        const double speed = t.vel.Length();
        if (std::isfinite(speed) && speed < 2000.0) out = out + t.vel * lead;
    }
    return true;
}

bool Eligible(const Target& t, const AimbotConfig& cfg) {
    if (t.drone && !cfg.TargetDrones) return false;
    if (t.friendly) return false;
    if (cfg.OnlyVisible && !t.drone && !t.visible) return false;
    if (t.distM > cfg.MaxDistance) return false;
    return true;
}

double SegmentDistance(const FVector& p0, const FVector& p1, const FVector& q0, const FVector& q1) {
    const FVector d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const double a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
    double s = 0.0, t = 0.0;
    if (a <= 1e-9 && e <= 1e-9) return (p0 - q0).Length();
    if (a <= 1e-9) { t = std::clamp(f / e, 0.0, 1.0); }
    else {
        const double c = d1.Dot(r);
        if (e <= 1e-9) { s = std::clamp(-c / a, 0.0, 1.0); }
        else {
            const double b = d1.Dot(d2), den = a * e - b * b;
            s = den > 1e-9 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0.0) { t = 0.0; s = std::clamp(-c / a, 0.0, 1.0); }
            else if (t > 1.0) { t = 1.0; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    return ((p0 + d1 * s) - (q0 + d2 * t)).Length();
}

uintptr_t TargetUnderCrosshair(const AimbotConfig& cfg, const TargetFrame& frame,
                               const FVector& eye, const FRotator& view) {
    const FVector end = eye + Forward(view) * 30000.0;
    uintptr_t hit = 0;
    double hitDist = 1e18;
    for (const Target& t : frame.targets) {
        if (t.stale || (t.drone && !cfg.TargetDrones)) continue;
        if (t.friendly) continue;
        if (cfg.TriggerVisibleOnly && !t.drone && !t.visible) continue;
        if (t.distM > cfg.MaxDistance) continue;
        const double core = (std::max)(0.0, static_cast<double>(t.halfHeight - t.radius));
        const FVector a = t.pos - FVector{0.0, 0.0, core}, b = t.pos + FVector{0.0, 0.0, core};
        const double r = t.radius * 0.8;
        if (SegmentDistance(eye, end, a, b) <= r) {
            const double d = (t.pos - eye).LengthSq();
            if (d < hitDist) { hitDist = d; hit = t.pawn; }
        }
    }
    return hit;
}

void TickTriggerbot(const AimbotConfig& cfg, const TargetFrame& frame, const FVector& eye,
                    const FRotator& view, ULONGLONG now) {
    if (g.trigDown && now - g.trigDownAt >= 25) {
        SendClick(false);
        g.trigDown = false;
        g.trigCooldownUntil = now + 90;
    }
    if (!cfg.Triggerbot || (cfg.TriggerKey != 0 && !KeyDown(cfg.TriggerKey))) {
        g.trigCandidate = 0;
        return;
    }
    if (g.trigDown || now < g.trigCooldownUntil) return;
    if (!g.trigDown && KeyDown(VK_LBUTTON)) return;

    const uintptr_t hit = TargetUnderCrosshair(cfg, frame, eye, view);
    if (!hit) { g.trigCandidate = 0; return; }
    if (hit != g.trigCandidate) { g.trigCandidate = hit; g.trigSince = now; return; }
    if (now - g.trigSince < static_cast<ULONGLONG>((std::max)(0.0f, cfg.TriggerDelayMs))) return;
    SendClick(true);
    g.trigDown = true;
    g.trigDownAt = now;
}

void ResetAll() {
    State keep = g;
    g = State{};
    for (int i = 0; i < 2; ++i) { g.yaw[i] = keep.yaw[i]; g.pitch[i] = keep.pitch[i]; }
    g.toggled = keep.toggled;
    g.keyWasDown = keep.keyWasDown;
    if (keep.trigDown) SendClick(false);
}

}

void Aimbot_Tick(const AimbotConfig& cfg) {
    if (!cfg.Enabled) { ResetAll(); return; }
    {
        static uintptr_t lastWorld = 0, lastPawn = 0;
        const uintptr_t world = GetWorld(), pawn = GetLocalPawn();
        if (world != lastWorld || pawn != lastPawn) { ResetAll(); lastWorld = world; lastPawn = pawn; }
        if (!pawn) { return; }
    }
    extern bool g_MenuOpen;
    if (g_MenuOpen) { ClearTarget(); DropPending(); if (g.trigDown) { SendClick(false); g.trigDown = false; } return; }

    const ULONGLONG now = GetTickCount64();
    if (g.lastTick && now - g.lastTick < kTickMs) return;

    const bool keyDown = KeyDown(cfg.AimKey);
    if (cfg.KeyMode == AimKeyMode_Toggle && keyDown && !g.keyWasDown) g.toggled = !g.toggled;
    g.keyWasDown = keyDown;
    const bool active = cfg.KeyMode == AimKeyMode_Always ||
                        (cfg.KeyMode == AimKeyMode_Hold && keyDown) ||
                        (cfg.KeyMode == AimKeyMode_Toggle && g.toggled);

    const FCameraCache cam = GetCameraCache();
    if (!Targets_CameraUsable(cam)) { ClearTarget(); return; }
    FRotator control{};
    if (!GetControlRotation(control)) control = cam.Rotation;

    const int mode = IsLocalPawnAiming() ? 1 : 0;
    if (mode != g.mode) { g.mode = mode; DropPending(); g.remX = g.remY = 0.0f; }
    LearnFromControl(control, now);

    const TargetFrame& frame = Targets_Get();
    TickTriggerbot(cfg, frame, cam.Location, control, now);

    if (now < g.pauseUntil) { return; }
    if (g.pending && now - g.pendingMs < kInFlightMs) return;

    const double dt = g.lastTick ? std::clamp((now - g.lastTick) / 1000.0, 1.0 / 240.0, 0.05) : 1.0 / 60.0;
    g.lastTick = now;

    if (!active) { ClearTarget(); return; }

    double yawStep = 0.0, pitchStep = 0.0;

    if (active) {
        const double halfFov = std::clamp(static_cast<double>(cfg.FOV) * 0.5, 0.5, 90.0);
        const Target* lockedT = nullptr;
        const Target* best = nullptr;
        double bestScore = 1e18;
        FVector lockedP{}, bestP{};
        int lockedBone = -1, bestBone = -1;

        for (const Target& t : frame.targets) {
            if (!Eligible(t, cfg)) continue;
            const bool isLocked = t.pawn == g.locked;
            FVector p;
            int bone = -1;
            if (!AimPoint(t, cfg, cam.Location, control, isLocked ? g.lockedBone : -1, p, bone)) continue;
            const double ang = AngleTo(cam.Location, control, p);
            if (!std::isfinite(ang) || ang > (isLocked ? halfFov * 1.2 : halfFov)) continue;
            if (isLocked) { lockedT = &t; lockedP = p; lockedBone = bone; continue; }

            double score = ang;
            if (cfg.Priority == AimPriority_Distance) score = t.distM * 1000.0 + ang;
            else if (cfg.Priority == AimPriority_Health) score = (t.health >= 0.0 ? t.health : 1e4) * 1000.0 + ang;
            if (score < bestScore) { bestScore = score; best = &t; bestP = p; bestBone = bone; }
        }

        const Target* target = nullptr;
        FVector point{};
        int bone = -1;
        if (g.locked && !lockedT) {
            if (!g.lockMissingSince) g.lockMissingSince = now;
            if (now - g.lockMissingSince < kLockGraceMs) { return; }
            ClearTarget();
        } else if (lockedT) {
            g.lockMissingSince = 0;
        }

        if (lockedT && (cfg.StickyTarget || !best)) { target = lockedT; point = lockedP; bone = lockedBone; }
        else if (lockedT && best) {
            const double la = AngleTo(cam.Location, control, lockedP), ba = AngleTo(cam.Location, control, bestP);
            if (ba + 3.0 < la) { target = best; point = bestP; bone = bestBone; }
            else { target = lockedT; point = lockedP; bone = lockedBone; }
        } else if (best) {
            if (g.lostAt && now - g.lostAt < static_cast<ULONGLONG>((std::max)(0.0f, cfg.SwitchDelayMs))) {

                best = nullptr;
            } else { target = best; point = bestP; bone = bestBone; }
        }

        if (target) {
            if (target->pawn != g.locked) {
                g.locked = target->pawn;
                g.prevError = 0.0;
                g.diverging = 0;
                g.remX = g.remY = 0.0f;
                DropPending();
            }
            g.lockedBone = bone;
            g.aimPoint = point;
            g.haveAimPoint = true;

            const FRotator need = CalcAngle(cam.Location, point);
            const double yawErr = AngleDiff(need.Yaw, control.Yaw);
            const double pitchErr = AngleDiff(need.Pitch, control.Pitch);
            const double err = std::hypot(yawErr, pitchErr);

            if (std::isfinite(err) && err >= 0.12) {
                if (g.prevError > 0.0 && err > 3.0 && err > g.prevError * 1.05 + 0.05) {
                    if (++g.diverging >= 8) {
                        g.yaw[g.mode].Reset();
                        g.pitch[g.mode].Reset();
                        ClearTarget();
                        DropPending();
                        g.pauseUntil = now + 150;

                        return;
                    }
                } else {
                    g.diverging = 0;
                }
                g.prevError = err;

                const double smoothing = std::clamp(static_cast<double>(cfg.Smoothing), 1.0, 30.0);
                const double gain = std::clamp((2.0 / smoothing) * (dt * 60.0), 0.05, 0.5);
                const double maxStep = 3.5 / std::sqrt(smoothing);
                yawStep = std::clamp(yawErr * gain, -maxStep, maxStep);
                pitchStep = std::clamp(pitchErr * gain, -maxStep, maxStep);
            } else {
                g.diverging = 0;
            }

        } else if (!best) {
            ClearTarget();
        }
    } else {
        ClearTarget();

    }

    if (yawStep == 0.0 && pitchStep == 0.0) return;
    const double maxCounts = 25.0 / std::sqrt(std::clamp(static_cast<double>(cfg.Smoothing), 1.0, 30.0));
    const double mx = std::clamp(yawStep / g.yaw[g.mode].dpc, -maxCounts * 4, maxCounts * 4);
    const double my = std::clamp(-pitchStep / g.pitch[g.mode].dpc, -maxCounts * 4, maxCounts * 4);

    LONG sx = 0, sy = 0;
    if (SendMouse(mx, my, sx, sy)) {
        if (!g.pending) {
            g.pendingFrom = control;
            g.pendingMs = now;
            g.pendingX = sx;
            g.pendingY = sy;
            g.pending = true;
        } else {
            g.pendingX += sx;
            g.pendingY += sy;
        }
    }
}

void Aimbot_Render(const AimbotConfig& cfg, float sw, float sh) {
    if (!cfg.Enabled || sw < 1.0f || sh < 1.0f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    if (cfg.DrawFOV && Menu_Launched()) {
        const TargetFrame& frame = Targets_Get();
        static double stableFov = 0.0, pendFov = 0.0, pendSince = 0.0;
        const double nowS = ImGui::GetTime();
        if (frame.camValid && std::isfinite(frame.cam.FOV) && frame.cam.FOV > 5.0f && frame.cam.FOV < 175.0f) {
            const double f = frame.cam.FOV;
            if (stableFov <= 0.0) stableFov = f;
            else if (std::fabs(f - stableFov) > 0.05) {
                if (std::fabs(f - pendFov) > 0.05) { pendFov = f; pendSince = nowS; }
                else if (nowS - pendSince >= 0.5) stableFov = f;
            } else pendFov = stableFov;
        }
        const double camFov = std::clamp(stableFov + ESP_FovAdjust(), 5.0, 175.0);
        const double halfCone = std::clamp(static_cast<double>(cfg.FOV) * 0.5, 0.5, 90.0);
        const double camTan = std::tan(DegToRad(camFov * 0.5));
        if (stableFov > 0.0 && std::isfinite(camTan) && camTan > 1e-6) {
            const float r = static_cast<float>(sw * 0.5 * std::tan(DegToRad(halfCone)) / camTan);
            if (std::isfinite(r) && r > 0.0f && r < sw * 2.0f)
                dl->AddCircle({sw * 0.5f, sh * 0.5f}, r, ImGui::ColorConvertFloat4ToU32(cfg.FovColor), std::clamp(static_cast<int>(r * 1.2f), 96, 512), 1.0f);
        }
    }

    if (cfg.DrawTarget && g.locked && g.haveAimPoint) {
        ImVec2 p;
        if (ESP_Project(g.aimPoint, p)) {
            const ImU32 col = ImGui::ColorConvertFloat4ToU32(cfg.MarkerColor);
            const ImU32 out = IM_COL32(0, 0, 0, 190);
            const float r = std::clamp(cfg.MarkerSize, 2.0f, 30.0f);
            const float spin = cfg.MarkerSpin ? static_cast<float>(ImGui::GetTime() * 1.6) : 0.0f;
            switch (cfg.MarkerStyle) {
            default:
            case 0:
                if (cfg.MarkerOutline) dl->AddCircleFilled(p, r * 0.6f + 1.5f, out, 20);
                dl->AddCircleFilled(p, r * 0.6f, col, 20);
                break;
            case 1:
                if (cfg.MarkerOutline) dl->AddCircle(p, r, out, 32, 3.5f);
                dl->AddCircle(p, r, col, 32, 1.6f);
                dl->AddCircleFilled(p, 1.6f, col, 12);
                if (cfg.MarkerSpin) for (int i = 0; i < 4; ++i) {
                    const float a = spin + i * 1.5708f;
                    dl->AddLine({ p.x + std::cos(a) * (r + 2), p.y + std::sin(a) * (r + 2) }, { p.x + std::cos(a) * (r + 6), p.y + std::sin(a) * (r + 6) }, col, 1.6f);
                }
                break;
            case 2: {
                const float g0 = r * 0.35f, g1 = r * 1.2f;
                for (int i = 0; i < 4; ++i) {
                    const float a = spin + i * 1.5708f, c = std::cos(a), sn = std::sin(a);
                    const ImVec2 a0(p.x + c * g0, p.y + sn * g0), a1(p.x + c * g1, p.y + sn * g1);
                    if (cfg.MarkerOutline) dl->AddLine(a0, a1, out, 3.5f);
                    dl->AddLine(a0, a1, col, 1.6f);
                }
                break;
            }
            case 3: {
                const float h = r * 1.6f, l = r * 0.7f;
                for (int i = 0; i < 4; ++i) {
                    const float a = spin + i * 1.5708f + 0.7854f, c = std::cos(a), sn = std::sin(a);
                    const ImVec2 corner(p.x + c * h, p.y + sn * h);
                    const float a1 = a + 2.3562f, a2 = a - 2.3562f;
                    const ImVec2 e1(corner.x + std::cos(a1) * l, corner.y + std::sin(a1) * l), e2(corner.x + std::cos(a2) * l, corner.y + std::sin(a2) * l);
                    if (cfg.MarkerOutline) { dl->AddLine(corner, e1, out, 3.5f); dl->AddLine(corner, e2, out, 3.5f); }
                    dl->AddLine(corner, e1, col, 1.6f);
                    dl->AddLine(corner, e2, col, 1.6f);
                }
                break;
            }
            }
        }
    }
}

void Aimbot_Suspend() {
    if (g.trigDown) { SendClick(false); g.trigDown = false; }
    g.trigCandidate = 0;
    ClearTarget();
    DropPending();
    g.lastTick = 0;
}

uintptr_t Aimbot_LockedTarget() { return g.locked; }
bool Aimbot_Toggled() { return g.toggled; }

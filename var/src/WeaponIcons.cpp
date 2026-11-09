#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../include/WeaponIcons.hpp"
#include "../include/World.hpp"
#include "../include/WeaponIconData.hpp"
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <string>
#include <cctype>

namespace {
    struct Painter {
        ImDrawList* dl; ImVec2 o; float s; ImU32 col;
        ImVec2 P(float x, float y) const { return { o.x + x * s, o.y + y * s }; }
        void R(float x0, float y0, float x1, float y1, float round = 0.0f) const {
            dl->AddRectFilled(P(x0, y0), P(x1, y1), col, round * s);
        }
        void Q(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) const {
            dl->AddQuadFilled(P(ax, ay), P(bx, by), P(cx, cy), P(dx, dy), col);
        }
        void T(float ax, float ay, float bx, float by, float cx, float cy) const {
            dl->AddTriangleFilled(P(ax, ay), P(bx, by), P(cx, cy), col);
        }
        void Circle(float cx, float cy, float r) const { dl->AddCircleFilled(P(cx, cy), r * s, col, 20); }
        void Ring(float cx, float cy, float r, float th) const { dl->AddCircle(P(cx, cy), r * s, col, 16, th * s); }
        void Line(float ax, float ay, float bx, float by, float th) const { dl->AddLine(P(ax, ay), P(bx, by), col, th * s); }
    };

    enum Model { M_AK, M_M4, M_SCAR, M_FAL, M_SVD, M_UZI, M_GLOCK, M_DEAGLE, M_KOBRA, M_M19,
                 M_FOSTECH, M_SHOTGUN, M_FLASH, M_FRAG, M_MB78, M_KNIFE, M_BOMB, M_Count };
    int ModelOf(const char* name) {
        if (!name || !*name) return -1;
        static const char* const k[M_Count] = { "AK-47", "M4A1", "SCAR", "FAL", "Dragunov", "UZI", "Glock 17", "Desert Eagle",
                                                "Kobra", "M19", "Fostech", "Shotgun", "Flashbang", "G67 Frag", "MB78", "Knife", "Bomb" };
        for (int i = 0; i < M_Count; ++i) if (std::strcmp(name, k[i]) == 0) return i;
        return -1;
    }
    float ModelWidth(int m) {
        static const float w[M_Count] = { 3.3f, 3.2f, 3.1f, 3.5f, 3.9f, 2.2f, 1.45f, 1.75f, 1.9f, 2.3f,
                                          3.0f, 3.4f, 0.75f, 0.85f, 0.75f, 2.3f, 1.3f };
        return w[m];
    }
    ImU32 Hole(const ImU32 col) { return IM_COL32(0, 0, 0, ((col >> IM_COL32_A_SHIFT) & 0xFF) / 2); }

    void Revolver(const Painter& p, float barrelEnd, bool rib) {
        p.R(0.84f, 0.18f, barrelEnd, 0.31f, 0.02f);
        if (rib) p.R(0.95f, 0.13f, barrelEnd - 0.05f, 0.19f);
        p.R(0.84f, 0.31f, 0.84f + (barrelEnd - 0.84f) * 0.65f, 0.37f);
        p.R(barrelEnd - 0.07f, 0.10f, barrelEnd - 0.01f, 0.18f);
        p.R(0.24f, 0.14f, 0.50f, 0.50f, 0.02f);
        p.R(0.46f, 0.12f, 0.88f, 0.48f, 0.06f);
        const ImU32 h = Hole(p.col);
        for (int i = 0; i < 3; ++i) p.dl->AddLine(p.P(0.46f, 0.21f + i * 0.09f), p.P(0.88f, 0.21f + i * 0.09f), h, 0.025f * p.s);
        p.T(0.26f, 0.14f, 0.16f, 0.03f, 0.34f, 0.14f);
        p.Q(0.18f, 0.46f, 0.44f, 0.46f, 0.32f, 1.00f, 0.00f, 0.92f);
        p.Line(0.46f, 0.50f, 0.58f, 0.64f, 0.05f);
        p.Line(0.58f, 0.64f, 0.70f, 0.48f, 0.05f);
    }

    void PaintModel(const Painter& p, int m) {
        const ImU32 h = Hole(p.col);
        switch (m) {
        case M_AK:
            p.Q(0.00f, 0.30f, 0.58f, 0.26f, 0.58f, 0.52f, 0.06f, 0.78f);
            p.R(0.56f, 0.24f, 1.55f, 0.50f, 0.02f);
            p.R(1.50f, 0.27f, 2.30f, 0.47f, 0.04f);
            p.R(1.55f, 0.20f, 2.10f, 0.27f);
            p.R(2.25f, 0.32f, 3.30f, 0.39f);
            p.R(3.05f, 0.20f, 3.11f, 0.32f);
            p.R(1.00f, 0.17f, 1.25f, 0.24f);
            p.Q(1.18f, 0.49f, 1.46f, 0.49f, 1.62f, 0.92f, 1.34f, 1.00f);
            p.Q(0.74f, 0.49f, 0.95f, 0.49f, 0.90f, 0.86f, 0.66f, 0.86f);
            break;
        case M_M4:
            p.Q(0.00f, 0.22f, 0.42f, 0.22f, 0.42f, 0.62f, 0.06f, 0.70f);
            p.R(0.40f, 0.30f, 0.72f, 0.40f);
            p.R(0.70f, 0.24f, 1.55f, 0.50f, 0.02f);
            p.R(0.76f, 0.15f, 1.50f, 0.24f);
            p.R(1.52f, 0.25f, 2.35f, 0.48f, 0.05f);
            for (int i = 0; i < 4; ++i) p.dl->AddLine(p.P(1.65f + i * 0.17f, 0.30f), p.P(1.65f + i * 0.17f, 0.43f), h, 0.03f * p.s);
            p.R(2.30f, 0.33f, 3.20f, 0.39f);
            p.T(2.52f, 0.33f, 2.60f, 0.12f, 2.68f, 0.33f);
            p.Q(1.05f, 0.49f, 1.31f, 0.49f, 1.38f, 0.95f, 1.12f, 0.97f);
            p.Q(0.82f, 0.49f, 1.00f, 0.49f, 0.95f, 0.84f, 0.74f, 0.84f);
            break;
        case M_SCAR:
            p.Q(0.00f, 0.24f, 0.55f, 0.24f, 0.55f, 0.60f, 0.10f, 0.66f);
            p.dl->AddRectFilled(p.P(0.14f, 0.34f), p.P(0.42f, 0.50f), h, 0.04f * p.s);
            p.R(0.55f, 0.19f, 1.75f, 0.48f, 0.03f);
            p.R(0.60f, 0.13f, 1.95f, 0.19f);
            p.R(0.75f, 0.48f, 1.38f, 0.56f);
            p.R(1.75f, 0.29f, 3.00f, 0.37f);
            p.R(2.88f, 0.25f, 3.10f, 0.41f, 0.02f);
            p.R(1.05f, 0.55f, 1.31f, 0.98f, 0.02f);
            p.Q(0.78f, 0.55f, 0.96f, 0.55f, 0.90f, 0.88f, 0.70f, 0.88f);
            break;
        case M_FAL:
            p.Q(0.00f, 0.30f, 0.70f, 0.26f, 0.70f, 0.52f, 0.06f, 0.76f);
            p.R(0.68f, 0.24f, 1.72f, 0.50f, 0.02f);
            p.R(0.95f, 0.08f, 1.47f, 0.13f);
            p.R(0.95f, 0.08f, 1.01f, 0.24f);
            p.R(1.41f, 0.08f, 1.47f, 0.24f);
            p.R(1.70f, 0.27f, 2.45f, 0.47f, 0.04f);
            p.R(2.40f, 0.32f, 3.32f, 0.38f);
            p.R(3.22f, 0.28f, 3.50f, 0.42f, 0.02f);
            p.Q(1.15f, 0.49f, 1.43f, 0.49f, 1.47f, 0.93f, 1.19f, 0.93f);
            p.Q(0.86f, 0.49f, 1.05f, 0.49f, 1.00f, 0.86f, 0.78f, 0.86f);
            break;
        case M_SVD:
            p.Q(0.00f, 0.28f, 0.88f, 0.28f, 0.88f, 0.36f, 0.04f, 0.38f);
            p.Q(0.02f, 0.66f, 0.70f, 0.50f, 0.88f, 0.50f, 0.88f, 0.58f);
            p.R(0.00f, 0.28f, 0.11f, 0.74f, 0.02f);
            p.R(0.58f, 0.34f, 0.70f, 0.56f);
            p.R(0.86f, 0.27f, 1.86f, 0.48f, 0.02f);
            p.R(1.84f, 0.29f, 2.62f, 0.46f, 0.04f);
            p.R(2.58f, 0.34f, 3.90f, 0.39f);
            p.R(3.72f, 0.24f, 3.78f, 0.34f);
            p.R(1.02f, 0.03f, 1.80f, 0.15f, 0.05f);
            p.R(0.94f, 0.05f, 1.04f, 0.13f);
            p.R(1.76f, 0.01f, 1.90f, 0.17f, 0.02f);
            p.R(1.30f, 0.15f, 1.45f, 0.27f);
            p.Q(1.22f, 0.47f, 1.42f, 0.47f, 1.52f, 0.80f, 1.32f, 0.82f);
            break;
        case M_UZI:
            p.R(0.18f, 0.18f, 1.62f, 0.50f, 0.04f);
            p.R(1.58f, 0.29f, 2.20f, 0.38f);
            p.R(0.00f, 0.24f, 0.22f, 0.31f);
            p.R(0.00f, 0.24f, 0.06f, 0.48f);
            p.R(0.30f, 0.10f, 0.55f, 0.19f);
            p.Q(0.70f, 0.49f, 0.98f, 0.49f, 0.98f, 1.00f, 0.70f, 1.00f);
            p.R(1.05f, 0.50f, 1.30f, 0.56f);
            break;
        case M_GLOCK:
            p.R(0.00f, 0.14f, 1.45f, 0.42f, 0.03f);
            for (int i = 0; i < 3; ++i) p.dl->AddLine(p.P(0.10f + i * 0.07f, 0.18f), p.P(0.10f + i * 0.07f, 0.38f), h, 0.025f * p.s);
            p.R(0.10f, 0.41f, 1.15f, 0.52f);
            p.Q(0.08f, 0.50f, 0.50f, 0.50f, 0.40f, 1.00f, 0.00f, 0.97f);
            p.Line(0.50f, 0.52f, 0.66f, 0.66f, 0.06f);
            p.Line(0.66f, 0.66f, 0.80f, 0.52f, 0.06f);
            p.R(1.32f, 0.07f, 1.40f, 0.15f);
            break;
        case M_DEAGLE:
            p.R(0.08f, 0.10f, 1.75f, 0.44f, 0.02f);
            p.T(1.55f, 0.44f, 1.75f, 0.44f, 1.75f, 0.30f);
            p.dl->AddTriangleFilled(p.P(0.95f, 0.18f), p.P(1.40f, 0.18f), p.P(1.40f, 0.30f), h);
            p.R(0.00f, 0.05f, 0.12f, 0.16f);
            p.R(0.12f, 0.43f, 1.30f, 0.55f);
            p.Q(0.10f, 0.53f, 0.58f, 0.53f, 0.50f, 1.00f, 0.00f, 0.98f);
            p.Line(0.58f, 0.55f, 0.74f, 0.70f, 0.07f);
            p.Line(0.74f, 0.70f, 0.90f, 0.55f, 0.07f);
            break;
        case M_KOBRA: Revolver(p, 1.90f, false); break;
        case M_M19:   Revolver(p, 2.30f, true); break;
        case M_FOSTECH:
            p.Q(0.00f, 0.22f, 0.60f, 0.22f, 0.60f, 0.52f, 0.06f, 0.64f);
            p.dl->AddRectFilled(p.P(0.14f, 0.30f), p.P(0.46f, 0.44f), h, 0.04f * p.s);
            p.R(0.58f, 0.17f, 2.02f, 0.50f, 0.04f);
            p.R(0.62f, 0.11f, 1.90f, 0.17f);
            p.R(2.00f, 0.23f, 2.60f, 0.43f, 0.04f);
            p.R(2.58f, 0.28f, 3.00f, 0.38f);
            p.Circle(1.38f, 0.74f, 0.26f);
            p.dl->AddCircleFilled(p.P(1.38f, 0.74f), 0.08f * p.s, h, 12);
            p.Q(0.78f, 0.49f, 0.98f, 0.49f, 0.92f, 0.88f, 0.70f, 0.88f);
            break;
        case M_SHOTGUN:
            p.Q(0.00f, 0.32f, 0.62f, 0.28f, 0.62f, 0.54f, 0.06f, 0.74f);
            p.R(0.60f, 0.25f, 1.45f, 0.53f, 0.02f);
            p.R(1.40f, 0.27f, 3.40f, 0.36f);
            p.R(1.45f, 0.38f, 3.05f, 0.46f);
            p.R(1.85f, 0.35f, 2.45f, 0.54f, 0.04f);
            for (int i = 0; i < 4; ++i) p.dl->AddLine(p.P(1.95f + i * 0.13f, 0.39f), p.P(1.95f + i * 0.13f, 0.51f), h, 0.03f * p.s);
            p.Q(0.78f, 0.52f, 1.00f, 0.52f, 0.94f, 0.88f, 0.70f, 0.88f);
            break;
        case M_FLASH:
            p.R(0.08f, 0.32f, 0.52f, 1.00f, 0.05f);
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 2; ++c)
                p.dl->AddCircleFilled(p.P(0.21f + c * 0.18f, 0.48f + r * 0.18f), 0.04f * p.s, h, 8);
            p.R(0.14f, 0.20f, 0.46f, 0.32f, 0.02f);
            p.Q(0.44f, 0.20f, 0.54f, 0.20f, 0.70f, 0.66f, 0.62f, 0.68f);
            p.Ring(0.14f, 0.12f, 0.08f, 0.05f);
            break;
        case M_FRAG:
            p.Circle(0.42f, 0.62f, 0.36f);
            for (int i = 0; i < 2; ++i) p.dl->AddLine(p.P(0.10f, 0.50f + i * 0.24f), p.P(0.74f, 0.50f + i * 0.24f), h, 0.03f * p.s);
            p.R(0.28f, 0.18f, 0.56f, 0.30f, 0.02f);
            p.Q(0.54f, 0.18f, 0.64f, 0.18f, 0.80f, 0.56f, 0.72f, 0.58f);
            p.Ring(0.20f, 0.14f, 0.09f, 0.05f);
            break;
        case M_MB78:
            p.R(0.06f, 0.26f, 0.58f, 1.00f, 0.08f);
            p.dl->AddRectFilled(p.P(0.06f, 0.56f), p.P(0.58f, 0.64f), h);
            p.R(0.16f, 0.16f, 0.48f, 0.26f, 0.02f);
            p.Q(0.46f, 0.16f, 0.56f, 0.16f, 0.72f, 0.60f, 0.64f, 0.62f);
            p.Ring(0.14f, 0.09f, 0.07f, 0.05f);
            break;
        case M_KNIFE:
            p.R(0.00f, 0.43f, 0.82f, 0.63f, 0.06f);
            for (int i = 0; i < 3; ++i) p.dl->AddLine(p.P(0.18f + i * 0.2f, 0.45f), p.P(0.18f + i * 0.2f, 0.61f), h, 0.03f * p.s);
            p.R(0.80f, 0.30f, 0.89f, 0.76f, 0.02f);
            p.Q(0.88f, 0.40f, 1.95f, 0.40f, 1.95f, 0.64f, 0.88f, 0.64f);
            p.T(1.94f, 0.40f, 2.30f, 0.50f, 1.94f, 0.64f);
            break;
        default:
            p.R(0.00f, 0.22f, 1.30f, 0.92f, 0.08f);
            p.dl->AddCircleFilled(p.P(0.65f, 0.57f), 0.18f * p.s, h, 16);
            p.R(0.20f, 0.08f, 0.30f, 0.22f);
            p.R(1.00f, 0.08f, 1.10f, 0.22f);
            break;
        }
    }

    float Width(int kind) {
        switch (kind) {
        case Weapon_Rifle:   return 3.2f;
        case Weapon_SMG:     return 2.2f;
        case Weapon_Pistol:  return 1.45f;
        case Weapon_Shotgun: return 3.4f;
        case Weapon_Sniper:  return 3.8f;
        case Weapon_Grenade: return 0.85f;
        case Weapon_Knife:   return 2.3f;
        default:             return 1.3f;
        }
    }

    void Paint(const Painter& p, int kind) {
        switch (kind) {
        case Weapon_Rifle:
            p.Q(0.00f, 0.30f, 0.58f, 0.26f, 0.58f, 0.52f, 0.06f, 0.76f);
            p.R(0.56f, 0.24f, 1.55f, 0.50f, 0.02f);
            p.R(1.50f, 0.27f, 2.35f, 0.47f, 0.03f);
            p.R(2.30f, 0.32f, 3.20f, 0.39f);
            p.R(2.95f, 0.22f, 3.01f, 0.33f);
            p.R(1.00f, 0.15f, 1.30f, 0.25f);
            p.Q(1.18f, 0.49f, 1.46f, 0.49f, 1.58f, 0.94f, 1.31f, 1.00f);
            p.Q(0.74f, 0.49f, 0.95f, 0.49f, 0.90f, 0.86f, 0.66f, 0.86f);
            break;
        case Weapon_SMG:
            p.R(0.18f, 0.18f, 1.62f, 0.50f, 0.04f);
            p.R(1.58f, 0.29f, 2.20f, 0.38f);
            p.R(0.00f, 0.24f, 0.22f, 0.31f);
            p.R(0.00f, 0.24f, 0.06f, 0.48f);
            p.R(0.30f, 0.10f, 0.55f, 0.19f);
            p.Q(0.70f, 0.49f, 0.98f, 0.49f, 0.98f, 1.00f, 0.70f, 1.00f);
            p.R(1.05f, 0.50f, 1.30f, 0.56f);
            break;
        case Weapon_Pistol:
            p.R(0.00f, 0.14f, 1.45f, 0.42f, 0.03f);
            p.R(0.10f, 0.41f, 1.15f, 0.52f);
            p.Q(0.08f, 0.50f, 0.50f, 0.50f, 0.40f, 1.00f, 0.00f, 0.97f);
            p.Line(0.50f, 0.52f, 0.66f, 0.66f, 0.06f);
            p.Line(0.66f, 0.66f, 0.80f, 0.52f, 0.06f);
            p.R(1.32f, 0.07f, 1.40f, 0.15f);
            break;
        case Weapon_Shotgun:
            p.Q(0.00f, 0.32f, 0.62f, 0.28f, 0.62f, 0.54f, 0.06f, 0.74f);
            p.R(0.60f, 0.25f, 1.45f, 0.53f, 0.02f);
            p.R(1.40f, 0.27f, 3.40f, 0.36f);
            p.R(1.45f, 0.38f, 3.05f, 0.46f);
            p.R(1.85f, 0.35f, 2.45f, 0.54f, 0.04f);
            p.Q(0.78f, 0.52f, 1.00f, 0.52f, 0.94f, 0.88f, 0.70f, 0.88f);
            break;
        case Weapon_Sniper:
            p.Q(0.00f, 0.34f, 0.80f, 0.30f, 0.80f, 0.56f, 0.06f, 0.78f);
            p.R(0.30f, 0.26f, 0.80f, 0.34f);
            p.R(0.78f, 0.28f, 1.95f, 0.52f, 0.02f);
            p.R(1.90f, 0.33f, 3.80f, 0.40f);
            p.R(1.00f, 0.03f, 1.85f, 0.17f, 0.05f);
            p.R(0.92f, 0.05f, 1.02f, 0.15f);
            p.R(1.80f, 0.01f, 1.95f, 0.19f, 0.02f);
            p.R(1.15f, 0.16f, 1.22f, 0.29f);
            p.R(1.62f, 0.16f, 1.69f, 0.29f);
            p.R(1.30f, 0.51f, 1.52f, 0.76f);
            p.Q(0.95f, 0.50f, 1.15f, 0.50f, 1.08f, 0.86f, 0.86f, 0.86f);
            break;
        case Weapon_Grenade:
            p.Circle(0.42f, 0.62f, 0.36f);
            p.R(0.28f, 0.18f, 0.56f, 0.30f, 0.02f);
            p.Q(0.54f, 0.18f, 0.64f, 0.18f, 0.80f, 0.56f, 0.72f, 0.58f);
            p.Ring(0.20f, 0.14f, 0.09f, 0.05f);
            break;
        case Weapon_Knife:
            p.R(0.00f, 0.43f, 0.82f, 0.63f, 0.06f);
            p.R(0.80f, 0.30f, 0.89f, 0.76f, 0.02f);
            p.Q(0.88f, 0.40f, 1.95f, 0.40f, 1.95f, 0.64f, 0.88f, 0.64f);
            p.T(1.94f, 0.40f, 2.30f, 0.50f, 1.94f, 0.64f);
            break;
        default:
            p.R(0.00f, 0.22f, 1.30f, 0.92f, 0.08f);
            p.dl->AddCircleFilled(p.P(0.65f, 0.57f), 0.18f * p.s, IM_COL32(0, 0, 0, (p.col >> IM_COL32_A_SHIFT) / 2), 16);
            p.R(0.20f, 0.08f, 0.30f, 0.22f);
            p.R(1.00f, 0.08f, 1.10f, 0.22f);
            break;
        }
    }
}

namespace {
    constexpr int kMasks = static_cast<int>(sizeof(kIconMasks) / sizeof(kIconMasks[0]));
    constexpr float kSheetH = 44.0f;
    ImFontAtlasRectId g_rects[kMasks];
    bool g_added = false, g_failed = false;
    int  g_writtenFor = -1;

    int MaskIndex(const char* id) {
        for (int i = 0; i < kMasks; ++i) if (std::strcmp(kIconMasks[i].id, id) == 0) return i;
        return -1;
    }

    std::string Squash(const char* s) {
        std::string o;
        for (; s && *s; ++s) if (std::isalnum(static_cast<unsigned char>(*s))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(*s)));
        return o;
    }

    int SheetIcon(int kind, const char* name) {
        struct M { const char* key; const char* id; };
        static const M kByName[] = {
            { "ka74m", "KA74" }, { "kaus", "KAUS" }, { "ka74", "KA74" }, { "drako", "DRAKO" }, { "m161a", "M16" }, { "m16", "M16" },
            { "skrh", "SKRH" }, { "m4", "M4" },
            { "sg5kf", "SG5KF" }, { "sg5k", "SG5K" }, { "sg9x", "SG9X" }, { "veaper", "VEAPER" }, { "uz1", "UZ1" },
            { "bk102s", "BK102S" }, { "bk101", "BK101" }, { "r12", "R12" }, { "vaiga", "VAIGA" },
            { "tacrivington", "TACRIV" }, { "rivington700", "RIV700" }, { "rivington", "RIV700" }, { "usd", "USD" }, { "lar", "LAR" },
            { "revolver", "REVOLVER" }, { "mlock19", "MLOCK" }, { "cr75", "CR75" }, { "deagle", "DEAGLE" }, { "fx45", "FX45" }, { "m1914", "M1914" },
            { "m67sticky", "STICKY" }, { "sticky", "STICKY" }, { "9bang", "NINEBANG" }, { "flash", "FLASH" }, { "rgn", "FRAG" },
            { "mb78", "SMOKE" }, { "smoke", "SMOKE" }, { "m67", "FRAG" }, { "frag", "FRAG" }, { "grenade", "FRAG" },
            { "knife", "KNIFE" }, { "bomb", "C4" } };
        if (name && *name) {
            const std::string n = Squash(name);
            for (const M& m : kByName) if (n.find(m.key) != std::string::npos) return MaskIndex(m.id);
        }
        switch (kind) {
        case Weapon_Rifle:   return MaskIndex("KA74");
        case Weapon_SMG:     return MaskIndex("SG5K");
        case Weapon_Pistol:  return MaskIndex("MLOCK");
        case Weapon_Shotgun: return MaskIndex("R12");
        case Weapon_Sniper:  return MaskIndex("RIV700");
        case Weapon_Grenade: return MaskIndex("FRAG");
        case Weapon_Knife:   return MaskIndex("KNIFE");
        case Weapon_Other:   return MaskIndex("C4");
        default:             return MaskIndex("KA74");
        }
    }

    bool EnsureSheet() {
        if (g_failed) return false;
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (!atlas) return false;
        if (!g_added) {
            for (int i = 0; i < kMasks; ++i) {
                g_rects[i] = atlas->AddCustomRect(kIconMasks[i].w, kIconMasks[i].h);
                if (g_rects[i] == ImFontAtlasRectId_Invalid) { g_failed = true; return false; }
            }
            g_added = true;
        }
        ImTextureData* tex = atlas->TexData;
        if (!tex || !tex->Pixels) return false;
        if (g_writtenFor == tex->UniqueID) return true;
        for (int i = 0; i < kMasks; ++i) {
            ImFontAtlasRect r;
            if (!atlas->GetCustomRect(g_rects[i], &r)) continue;
            const IconMask& m = kIconMasks[i];
            for (int y = 0; y < m.h; ++y)
                for (int x = 0; x < m.w; ++x) {
                    const uint8_t a = m.a[y * m.w + x];
                    if (tex->Format == ImTextureFormat_Alpha8) *static_cast<uint8_t*>(tex->GetPixelsAt(r.x + x, r.y + y)) = a;
                    else *static_cast<ImU32*>(tex->GetPixelsAt(r.x + x, r.y + y)) = IM_COL32(255, 255, 255, a);
                }
            ImFontAtlasTextureBlockQueueUpload(atlas, tex, r.x, r.y, r.w, r.h);
        }
        g_writtenFor = tex->UniqueID;
        return true;
    }

    void DrawSheet(ImDrawList* dl, int idx, ImVec2 pos, float h, ImU32 col) {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        ImFontAtlasRect r;
        if (!atlas->GetCustomRect(g_rects[idx], &r)) return;
        const float s = h / kSheetH;
        const IconMask& m = kIconMasks[idx];
        const float ih = m.h * s, iw = m.w * s;
        const ImVec2 p0(pos.x, pos.y + (h - ih) * 0.5f);
        dl->AddImage(atlas->TexRef, p0, { p0.x + iw, p0.y + ih }, r.uv0, r.uv1, col);
    }
}

float WeaponIcon_Width(int kind, float h, const char* name) {
    const int idx = SheetIcon(kind, name);
    if (idx >= 0) return kIconMasks[idx].w * (h / kSheetH);
    const int m = ModelOf(name);
    return (m >= 0 ? ModelWidth(m) : Width(kind)) * h;
}

void WeaponIcon_Draw(ImDrawList* dl, int kind, ImVec2 pos, float h, ImU32 col, bool shadow, const char* name) {
    if (!dl || h < 2.0f) return;
    const int idx = SheetIcon(kind, name);
    if (idx >= 0 && EnsureSheet()) {
        if (shadow) {
            const int a = static_cast<int>(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.8f);
            DrawSheet(dl, idx, { pos.x + 1.0f, pos.y + 1.0f }, h, IM_COL32(0, 0, 0, a));
        }
        DrawSheet(dl, idx, pos, h, col);
        return;
    }
    const int m = ModelOf(name);
    auto paint = [&](const Painter& p) { if (m >= 0) PaintModel(p, m); else Paint(p, kind); };
    if (shadow) {
        const int a = static_cast<int>(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.75f);
        paint(Painter{ dl, { pos.x + 1.0f, pos.y + 1.0f }, h, IM_COL32(0, 0, 0, a) });
    }
    paint(Painter{ dl, pos, h, col });
}

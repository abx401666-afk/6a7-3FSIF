#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../include/ESP.hpp"
#include "../include/Aimbot.hpp"
#include "../include/Memory.hpp"
#include "../include/Visibility.hpp"
#include "../include/OverlayRenderer.hpp"
#include "../include/MenuUI.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/UiKit.hpp"
#include "../include/Hud.hpp"
#include "../include/Lens.hpp"
#include "../include/Reload.hpp"
#include "../include/Ammo.hpp"
#include "../include/Misc.hpp"
#include "../include/Config.hpp"
#include "../include/World.hpp"
#include "../include/CombatLog.hpp"
#include "../include/Log.hpp"
#include "../include/GameSettings.hpp"
#include "../include/MenuInput.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

ESPConfig g_ESP{};
AimbotConfig g_Aim{};
bool g_MenuOpen = false;
float g_MenuAnimT = 0.0f;

namespace {
    bool g_pagesBuilt = false;

    std::string g_steamId;
    std::string QuerySteamPersona() {
        DWORD activeUser = 0, size = sizeof(activeUser);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
                     RRF_RT_REG_DWORD, nullptr, &activeUser, &size);

        wchar_t steamPath[MAX_PATH] = {};
        DWORD pathSize = sizeof(steamPath);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                         RRF_RT_REG_SZ, nullptr, steamPath, &pathSize) != ERROR_SUCCESS) return {};

        std::ifstream file(std::filesystem::path(std::wstring(steamPath) + L"\\config\\loginusers.vdf"), std::ios::binary);
        if (!file) return {};
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string vdf = buffer.str();

        const std::string wantedId = activeUser ? std::to_string(76561197960265728ULL + activeUser) : std::string();

        auto valueOf = [](const std::string& block, const char* key) -> std::string {
            const size_t k = block.find(std::string("\"") + key + "\"");
            if (k == std::string::npos) return {};
            const size_t a = block.find('"', k + std::strlen(key) + 2);
            if (a == std::string::npos) return {};
            const size_t b = block.find('"', a + 1);
            return b == std::string::npos ? std::string() : block.substr(a + 1, b - a - 1);
        };

        std::string mostRecentName, mostRecentId;
        size_t pos = 0;
        while ((pos = vdf.find("\"7656", pos)) != std::string::npos) {
            const size_t idEnd = vdf.find('"', pos + 1);
            const size_t open = vdf.find('{', idEnd);
            const size_t close = vdf.find('}', open);
            if (idEnd == std::string::npos || open == std::string::npos || close == std::string::npos) break;
            const std::string id = vdf.substr(pos + 1, idEnd - pos - 1);
            const std::string block = vdf.substr(open, close - open);
            const std::string name = valueOf(block, "PersonaName");
            if (!wantedId.empty() && id == wantedId && !name.empty()) { g_steamId = id; return name; }
            if (valueOf(block, "MostRecent") == "1") { mostRecentName = name; mostRecentId = id; }
            pos = close;
        }
        g_steamId = mostRecentName.empty() ? wantedId : mostRecentId;
        return mostRecentName;
    }

    const std::string& SteamPersona() {
        static std::string name;
        static ULONGLONG nextTry = 0;
        const ULONGLONG now = GetTickCount64();
        if (name.empty() && now >= nextTry) {
            name = QuerySteamPersona();
            nextTry = now + 5000;
        }
        return name;
    }

    ImTextureData* g_avatarTex = nullptr;
    bool LoadSteamAvatar(std::vector<uint8_t>& rgba) {
        DWORD activeUser = 0, size = sizeof(activeUser);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
                     RRF_RT_REG_DWORD, nullptr, &activeUser, &size);
        wchar_t steamPath[MAX_PATH] = {};
        DWORD pathSize = sizeof(steamPath);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                         RRF_RT_REG_SZ, nullptr, steamPath, &pathSize) != ERROR_SUCCESS) return false;
        const std::wstring dir = std::wstring(steamPath) + L"\\config\\avatarcache\\";
        std::wstring id;
        if (activeUser) id = std::to_wstring(76561197960265728ULL + activeUser);
        else { SteamPersona(); id.assign(g_steamId.begin(), g_steamId.end()); }
        if (id.empty()) return false;
        const std::wstring file = dir + id + L".png";
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IWICImagingFactory* wic = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICFormatConverter* conv = nullptr;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&wic))) &&
            SUCCEEDED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &frame)) &&
            SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame, 64, 64, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            rgba.resize(64 * 64 * 4);
            ok = SUCCEEDED(conv->CopyPixels(nullptr, 64 * 4, static_cast<UINT>(rgba.size()), rgba.data()));
        }
        if (conv) conv->Release();
        if (scaler) scaler->Release();
        if (frame) frame->Release();
        if (dec) dec->Release();
        if (wic) wic->Release();
        if (SUCCEEDED(co)) CoUninitialize();
        return ok;
    }
    ImTextureData* SteamAvatar() {
        static ULONGLONG nextTry = 0;
        if (g_avatarTex) {
            if (g_avatarTex->Status == ImTextureStatus_Destroyed) g_avatarTex->SetStatus(ImTextureStatus_WantCreate);
            return g_avatarTex->Status == ImTextureStatus_OK ? g_avatarTex : nullptr;
        }
        const ULONGLONG now = GetTickCount64();
        if (now < nextTry) return nullptr;
        nextTry = now + 5000;
        std::vector<uint8_t> rgba;
        if (!LoadSteamAvatar(rgba)) return nullptr;
        g_avatarTex = IM_NEW(ImTextureData)();
        g_avatarTex->Create(ImTextureFormat_RGBA32, 64, 64);
        std::memcpy(g_avatarTex->GetPixels(), rgba.data(), rgba.size());
        ImGui::RegisterUserTexture(g_avatarTex);
        return nullptr;
    }

    struct SubPage { const char* name; std::function<void()> body; };
    struct Page { const char* icon; const char* name; std::vector<SubPage> subs; int cur = 0; };
    std::vector<Page> g_pages;
    int   g_curPage = 0;
    char  g_search[64] = {};
    float g_contentT = 1.0f;

    constexpr float kMenuW = 780.0f, kMenuH = 480.0f, kSideW = 158.0f;
    constexpr float kNavTop = 72.0f;

    int    g_slideDir = 0;
    double g_slideStart = -10.0;
    void StartSlide(int dir) { g_slideDir = dir; g_slideStart = ImGui::GetTime(); }

    void color_row(bool* custom, ImVec4* c) {
        if (kit::color("Color", c)) *custom = true;
    }
    using kit::center_buttons;

    void RegisterPanels() {
        using namespace kit;
        register_panel("profile", "Settings", "Look and behaviour of the menu.", [] {
            section("Appearance");
            static const char* const kModes[] = { "Dark", "Light" };
            segmented("Mode", &kit::theme.mode, kModes, 2);
            color("Accent color", &kit::theme.accent);
            section("Opacity");
            slider("Menu", &kit::theme.menuAlpha, 0.5f, 1.0f, "%.2f");
            slider("Widgets", &kit::theme.hudAlpha, 0.3f, 1.0f, "%.2f");
            section("Logs");
            if (button_row("Log files", "Open")) {
                wchar_t dir[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) {
                    std::wstring d = std::wstring(dir) + L"\\BodycamMod\\logs";
                    CreateDirectoryW((std::wstring(dir) + L"\\BodycamMod").c_str(), nullptr);
                    CreateDirectoryW(d.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
        });
        register_panel("m_rapid", "Rapid fire", "Faster fire rate, and holding fire keeps shooting.", [] {
            slider("Fire rate", &g_Misc.FireRateMul, 1.0f, 5.0f, "%.1fx");
            slider("Auto clicks / s", &g_Misc.AutoFireCps, 2.0f, 30.0f, "%.0f");
        });
        register_panel("m_auto", "Auto fire", "Hold fire and it keeps clicking for you.", [] {
            slider("Clicks per second", &g_Misc.AutoFireCps, 2.0f, 30.0f, "%.0f");
            note("Turns on at max speed with Rapid fire. Works with semi-auto guns too.");
        });
        register_panel("m_reload", "Instant reload", "Reload animation speed multiplier.", [] {
            slider("Reload speed", &g_Misc.ReloadSpeedMul, 2.0f, 30.0f, "%.0fx");
            note("Too high can skip the mag swap; lower it if ammo stops refilling.");
        });
        register_panel("m_damage", "Damage multiplier", "Scales the Damage attribute of the held weapon.", [] {
            slider("Multiplier", &g_Misc.DamageMul, 1.0f, 10.0f, "%.1fx");
        });
        register_panel("m_wspeed", "Weapon speed", "Scales the weapon Speed attribute (handling).", [] {
            slider("Multiplier", &g_Misc.WeaponSpeedMul, 1.0f, 4.0f, "%.1fx");
        });
        register_panel("m_fly", "Fly settings", "Flies where you look.", [] {
            slider("Speed", &g_Misc.FlySpeed, 300.0f, 5000.0f, "%.0f");
            slider("Response", &g_Misc.FlyResponse, 1.0f, 15.0f, "%.1f");
            keybind("Up key", &g_Misc.FlyUpKey);
            keybind("Down key", &g_Misc.FlyDownKey);
            keybind("Boost key", &g_Misc.FlyBoostKey);
        });
        register_panel("m_tp", "Teleport", "Jumps to the enemy at your crosshair.", [] {
            keybind("Key", &g_Misc.TeleportKey);
            static const char* const kSides[3] = { "Behind", "In front", "Above" };
            combo("Land", &g_Misc.TeleportSide, kSides, 3);
            slider("Distance", &g_Misc.TeleportDist, 50.0f, 600.0f, "%.0f");
            toggle("Next enemy on kill", &g_Misc.AutoTeleport);
            note("After a teleport you stay put until that enemy dies, then jump straight to the next one.");
        });
        register_panel("m_speed", "Speedhack", "Your character's time scale.", [] {
            slider("Speed", &g_Misc.SpeedMul, 1.0f, 4.0f, "%.2fx");
        });
        register_panel("m_freeze", "Freeze bots", "Time scale of every bot. 0 = frozen.", [] {
            slider("Bot speed", &g_Misc.BotTimeScale, 0.0f, 1.0f, "%.2fx");
        });
        register_panel("m_steps", "Footstep settings", "Rings where enemies step, from their movement.", [] {
            slider("Range", &g_Misc.FootstepRange, 5.0f, 100.0f, "%.0fm");
            color("Color", &g_Misc.FootstepColor);
        });
        register_panel("m_hit", "Hit marker settings", "Cross on the enemy you hit, follows it for a moment.", [] {
            slider("Size", &g_Misc.HitMarkerSize, 3.0f, 12.0f, "%.0f");
            color("Hit color", &g_Misc.HitColor);
            color("Kill color", &g_Misc.KillColor);
            toggle("Hit sound", &g_Misc.HitSound);
            if (button_row("Preview sound", "Test")) Fx_TestSound(false);
        });
        register_panel("m_tracer", "Tracer settings", "Line along your aim for every shot.", [] {
            static const char* const kTracerStyles[TracerStyle_Count] = { "Beam", "Line", "Laser", "Dashed", "Comet", "Rainbow" };
            combo("Style", &g_Misc.TracerStyle, kTracerStyles, TracerStyle_Count);
            slider("Duration", &g_Misc.TracerSeconds, 0.3f, 5.0f, "%.1fs");
            slider("Thickness", &g_Misc.TracerThickness, 0.5f, 3.0f, "%.1fx");
            toggle("Impact marker", &g_Misc.TracerImpact);
            if (g_Misc.TracerStyle != TracerStyle_Rainbow) color("Color", &g_Misc.TracerColor);
        });
        register_panel("m_kill", "Kill sound", "Played when your kill counter goes up.", [] {
            static const char* const kSounds[3] = { "Ping", "Bell", "Click" };
            combo("Sound", &g_Misc.KillSoundType, kSounds, 3);
            slider("Volume", &g_Misc.SoundVolume, 0.0f, 1.0f, "%.2f");
            if (button_row("Preview sound", "Test")) Fx_TestSound(true);
        });
        auto hudColour = [](int idx) { color_row(&g_Hud.CustomColor[idx], &g_Hud.Color[idx]); };
        register_panel("box", "Box settings", "Style, fill, thickness and colors.", [] {
            static const char* const kStyles[BoxStyle_Count] = { "Full box", "Corner box", "3D box", "3D corners" };
            combo("Style", &g_ESP.BoxStyle, kStyles, BoxStyle_Count);
            toggle("Filled", &g_ESP.FilledBoxes);
            slider("Thickness", &g_ESP.BoxThickness, 0.5f, 4.0f, "%.1f");
            toggle("Visible / hidden colors", &g_ESP.VisibilityColors);
            if (g_ESP.VisibilityColors) {
                color("Visible", &g_ESP.VisibleColor);
                color("Hidden", &g_ESP.HiddenColor);
            } else {
                color("Color", &g_ESP.BoxColor);
            }
            spacing(9);
            toggle_color("Teammates", &g_ESP.ShowTeammates, &g_ESP.TeammateColor);
        });
        register_panel("skeleton", "Skeleton settings", "Bone lines drawn from the live pose.", [] {
            slider("Thickness", &g_ESP.BoneThickness, 0.5f, 3.0f, "%.1f");
            color("Color", &g_ESP.BoneColor);
            toggle("Head circle", &g_ESP.ShowHeadCircle);
        });
        register_panel("vischeck", "Visibility colors", "Colors for visible and hidden enemies.", [] {
            color("Visible", &g_ESP.VisibleColor);
            color("Hidden", &g_ESP.HiddenColor);
        });
        register_panel("viewdir", "View direction", "Line along where the player faces.", [] {
            slider("Length", &g_ESP.ViewDirLength, 30.0f, 400.0f, "%.0fcm");
            color_row(&g_ESP.ViewDirCustomColor, &g_ESP.ViewDirColor);
        });
        register_panel("names", "Name settings", "Label above the box.", [] {
            toggle("Bot / player tag", &g_ESP.ShowTypeTag);
            color_row(&g_ESP.NameCustomColor, &g_ESP.NameColor);
        });
        register_panel("health", "Health bar settings", "Position, size and colors.", [] {
            static const char* const kPos[HealthPos_Count] = { "Left", "Right", "Top", "Bottom" };
            static const char* const kMode[HealthColor_Count] = { "Gradient", "Fade", "Solid" };
            combo("Position", &g_ESP.HealthPos, kPos, HealthPos_Count);
            slider("Width", &g_ESP.HealthWidth, 1.0f, 8.0f, "%.0fpx");
            combo("Color mode", &g_ESP.HealthColorMode, kMode, HealthColor_Count);
            color(g_ESP.HealthColorMode == HealthColor_Solid ? "Color" : "Full health", &g_ESP.HealthFullColor);
            if (g_ESP.HealthColorMode != HealthColor_Solid) color("Low health", &g_ESP.HealthLowColor);
            spacing(9);
            toggle("Outline", &g_ESP.HealthOutline);
            toggle("Background", &g_ESP.HealthBackground);
            toggle("Health number", &g_ESP.ShowHealthText);
        });
        register_panel("distance", "Distance settings", "Metres under the box.", [] {
            color("Color", &g_ESP.DistanceColor);
        });
        register_panel("weapon", "Weapon settings", "What each enemy is holding.", [] {
            static const char* const kModes[WeaponMode_Count] = { "Text", "Icon", "Icon + text" };
            combo("Mode", &g_ESP.WeaponMode, kModes, WeaponMode_Count);
            color("Color", &g_ESP.WeaponColor);
            toggle("Show reloading", &g_ESP.WeaponReloading);
            note(World_Status().c_str());
        });
        register_panel("drones", "Drone settings", "FPV drones, by team.", [] {
            color("Enemy color", &g_ESP.DroneColor);
            toggle_color("Friendly drones", &g_ESP.ShowFriendlyDrones, &g_ESP.FriendlyDroneColor);
            toggle("Enemy / friendly label", &g_ESP.DroneLabel);
        });
        register_panel("grenades", "Grenade settings", "Grenades in flight: timer, danger ring, trail.", [] {
            color("Enemy color", &g_ESP.GrenadeColor);
            color("Friendly color", &g_ESP.GrenadeFriendlyColor);
            toggle("Fuse timer", &g_ESP.GrenadeTimer);
            toggle("Danger ring", &g_ESP.GrenadeRing);
            if (g_ESP.GrenadeRing) slider("Ring radius", &g_ESP.GrenadeRadius, 1.0f, 15.0f, "%.1fm");
        });
        register_panel("snap", "Snapline settings", "Where the lines start on screen.", [] {
            static const char* const kSnap[SnapOrigin_Count] = { "Bottom", "Center", "Top" };
            combo("Origin", &g_ESP.SnaplineOrigin, kSnap, SnapOrigin_Count);
            slider("Thickness", &g_ESP.SnapThickness, 0.5f, 4.0f, "%.1f");
            color_row(&g_ESP.SnapCustomColor, &g_ESP.SnapColor);
        });
        register_panel("offscreen", "Arrow settings", "Indicators for targets outside the view.", [] {
            slider("Radius", &g_ESP.OffscreenRadius, 60.0f, 500.0f, "%.0fpx");
            slider("Size", &g_ESP.OffscreenSize, 5.0f, 30.0f, "%.0fpx");
            color_row(&g_ESP.OffscreenCustomColor, &g_ESP.OffscreenColor);
        });
        register_panel("fov", "FOV settings", "Cone drawn around the crosshair.", [] {
            toggle_color("Draw FOV circle", &g_Aim.DrawFOV, &g_Aim.FovColor);
        });
        register_panel("marker", "Target marker", "Drawn on the point the aimbot is locked to.", [] {
            static const char* const kStyles[4] = { "Dot", "Ring", "Crosshair", "Brackets" };
            combo("Style", &g_Aim.MarkerStyle, kStyles, 4);
            slider("Size", &g_Aim.MarkerSize, 2.0f, 20.0f, "%.0fpx");
            color("Color", &g_Aim.MarkerColor);
            toggle("Outline", &g_Aim.MarkerOutline);
            if (g_Aim.MarkerStyle != 0) toggle("Spin", &g_Aim.MarkerSpin);
        });
        register_panel("trigger", "Triggerbot settings", "Fires when the crosshair is on a target.", [] {
            keybind("Trigger key", &g_Aim.TriggerKey);
            slider("Delay", &g_Aim.TriggerDelayMs, 0.0f, 300.0f, "%.0fms");
            toggle("Visible targets only", &g_Aim.TriggerVisibleOnly);
        });
        register_panel("predict", "Prediction settings", "Leads moving targets.", [] {
            slider("Lead time", &g_Aim.PredictionMs, 0.0f, 300.0f, "%.0fms");
        });
        register_panel("watermark", "Watermark settings", "Additional display settings for details.", [hudColour] {
            toggle("Brand name", &g_Hud.WmBrand);
            toggle("Show username", &g_Hud.WmUser);
            toggle("Game status", &g_Hud.WmStatus);
            toggle("Show FPS", &g_Hud.WmFps);
            toggle("Current time", &g_Hud.WmTime);
            hudColour(Hud_Watermark);
        });
        register_panel("ammo", "Ammo settings", "Weapon in your hands.", [hudColour] {
            toggle("Magazine bar", &g_Hud.AmmoBar);
            toggle("Weapon icon", &g_Hud.AmmoWeaponIcon);
            note(Reload_Status().c_str());
            slider("Low ammo at", &g_Hud.AmmoLowPct, 0.0f, 100.0f, "%.0f%%");
            hudColour(Hud_Ammo);
            note(Ammo_Status().c_str());
        });
        register_panel("kills", "Kills settings", "Your kills in this match.", [hudColour] {
            toggle("Deaths", &g_Hud.KillsDeaths);
            toggle("K/D ratio", &g_Hud.KillsKD);
            hudColour(Hud_Kills);
        });
        register_panel("healthw", "Health settings", "Your own health.", [hudColour] {
            toggle("Health number", &g_Hud.HealthNumber);
            toggle("Damage trail", &g_Hud.HealthDamageTrail);
            hudColour(Hud_Health);
        });
        register_panel("combatlog", "Combat feed settings", "Kill, hit and miss lines.", [] {
            toggle_color("Kills", &g_Hud.ClKills, &g_Hud.ClKillColor);
            toggle_color("Hits", &g_Hud.ClHits, &g_Hud.ClHitColor);
            toggle_color("Misses", &g_Hud.ClMisses, &g_Hud.ClMissColor);
            spacing(9);
            slider("Show for", &g_Hud.ClSeconds, 1.0f, 15.0f, "%.0fs");
            float lines = static_cast<float>(g_Hud.ClMaxLines);
            if (slider("Max lines", &lines, 1.0f, 12.0f, "%.0f")) g_Hud.ClMaxLines = static_cast<int>(lines + 0.5f);
            note("Miss reasons are best guesses.");
        });
        register_panel("keybinds", "Keybinds settings", "Additional display settings for details.", [hudColour] {
            toggle("Only active binds", &g_Hud.KbOnlyActive);
            hudColour(Hud_Keybinds);
        });
        register_panel("stats", "Stats settings", "Enemy counters at the top right.", [hudColour] {
            toggle("Show visible count", &g_Hud.StatsVisible);
            hudColour(Hud_Stats);
        });
        register_panel("targethud", "Target HUD settings", "Additional display settings for details.", [hudColour] {
            static const char* const kStyle[] = { "HP line", "HP circle" };
            combo("Style", &g_Hud.TargetStyle, kStyle, 2);
            toggle("Display nickname", &g_Hud.TargetName);
            toggle("Show distance", &g_Hud.TargetDist);
            toggle("Show health bar", &g_Hud.TargetBar);
            hudColour(Hud_Target);
        });
        register_panel("music", "Music player", "Plays music from your PC.", [hudColour] {
            toggle("Media buttons", &g_Hud.MusicButtons);
            toggle("Equalizer bars", &g_Hud.MusicBars);
            hudColour(Hud_Music);
            note("Buttons work while the menu is open and control whatever is playing.");
        });
        register_panel("notify", "Notification settings", "Toasts in the bottom-left corner.", [] {
            slider("Duration", &g_Hud.NotifySeconds, 1.0f, 6.0f, "%.1fs");
            toggle("Notify on toggle", &kit::theme.notifyOnToggle);
            color_row(&g_Hud.NotifyCustomColor, &g_Hud.NotifyColor);
        });
    }

    void BuildPages() {
        using namespace kit;
        g_pages.clear();

        g_pages.push_back({ ICON_FA_EYE, "Visuals", {
            { "Players", [] {
                column(0);
                section("ESP");
                toggle("Enable ESP", &g_ESP.Enabled);
                toggle("Boxes", &g_ESP.ShowBoxes, "box");
                toggle("Skeleton", &g_ESP.ShowBones, "skeleton");
                toggle("View direction", &g_ESP.ShowViewDir, "viewdir");
                section("Range");
                slider("Max distance", &g_ESP.MaxDistance, 50.0f, 1000.0f, "%.0fm");
                toggle("Fade with distance", &g_ESP.DistanceFade);

                column(1);
                section("Labels");
                toggle("Names", &g_ESP.ShowNames, "names");
                toggle("Health bar", &g_ESP.ShowHealth, "health");
                toggle("Distance", &g_ESP.ShowDistance, "distance");
                toggle("Weapon", &g_ESP.ShowWeapon, "weapon");
                section("Indicators");
                toggle("Snaplines", &g_ESP.ShowSnaplines, "snap");
                toggle("Off-screen arrows", &g_ESP.ShowOffscreen, "offscreen");
                toggle("Enemy counter", &g_ESP.ShowEnemyCount);
            } },
            { "World", [] {
                column(0);
                section("Objects");
                toggle("Drones", &g_ESP.ShowDrones, "drones");
                toggle("Grenades", &g_ESP.ShowGrenades, "grenades");
                column(1);
                section("Effects");
                toggle("Footstep ESP", &g_Misc.Footsteps, "m_steps");
                toggle("Bullet tracers", &g_Misc.Tracers, "m_tracer");
                toggle("Hit markers", &g_Misc.HitMarker, "m_hit");
            } },
        } });

        g_pages.push_back({ ICON_FA_CROSSHAIRS, "Aimbot", {
            { "Targeting", [] {
                column(0);
                section("General");
                toggle("Enable aimbot", &g_Aim.Enabled);
                static const char* const kModes[AimKeyMode_Count] = { "Hold", "Toggle", "Always" };
                combo("Activation", &g_Aim.KeyMode, kModes, AimKeyMode_Count);
                if (g_Aim.KeyMode != AimKeyMode_Always) keybind("Aim key", &g_Aim.AimKey);
                static const char* const kBones[AimBone_Count] = { "Head", "Neck", "Chest", "Pelvis", "Legs" };
                multi("Target bones", &g_Aim.TargetBones, kBones, AimBone_Count);
                if (g_Aim.TargetBones == 0) g_Aim.TargetBones = 1u << AimBone_Chest;
                static const char* const kPrio[AimPriority_Count] = { "Crosshair", "Distance", "Health" };
                combo("Target priority", &g_Aim.Priority, kPrio, AimPriority_Count);
                section("Filters");
                toggle("Only visible", &g_Aim.OnlyVisible);
                toggle("Target drones", &g_Aim.TargetDrones);

                column(1);
                section("Targeting");
                slider("Field of view", &g_Aim.FOV, 1.0f, 90.0f, "%.0f\xc2\xb0", "fov");
                slider("Smoothing", &g_Aim.Smoothing, 1.0f, 30.0f, "%.1f");
                slider("Max distance", &g_Aim.MaxDistance, 10.0f, 1000.0f, "%.0fm");
                toggle("Sticky target", &g_Aim.StickyTarget);
                slider("Switch delay", &g_Aim.SwitchDelayMs, 0.0f, 1000.0f, "%.0fms");
                section("Drawing");
                toggle("Draw target marker", &g_Aim.DrawTarget, "marker");
            } },
            { "Assist", [] {
                column(0);
                section("Prediction");
                toggle("Movement prediction", &g_Aim.Prediction, "predict");
                if (g_Aim.Prediction) slider("Lead time", &g_Aim.PredictionMs, 0.0f, 300.0f, "%.0fms");

                column(1);
                section("Triggerbot");
                toggle("Triggerbot", &g_Aim.Triggerbot, "trigger");
                keybind("Trigger key", &g_Aim.TriggerKey);
                if (g_Aim.TriggerKey == 0) note("No key: active whenever enabled.");
                slider("Trigger delay", &g_Aim.TriggerDelayMs, 0.0f, 300.0f, "%.0fms");
                toggle("Visible targets only", &g_Aim.TriggerVisibleOnly);
            } },
        } });

        g_pages.push_back({ ICON_FA_WRENCH, "Miscellaneous", {
            { "Weapon", [] {
                column(0);
                section("Accuracy");
                toggle("No recoil", &g_Misc.NoRecoil);
                toggle("No spread", &g_Misc.NoSpread);
                section("Firing");
                if (toggle("Rapid fire", &g_Misc.RapidFire, "m_rapid")) {
                    g_Misc.AutoFire = g_Misc.RapidFire;
                    if (g_Misc.RapidFire) g_Misc.AutoFireCps = 30.0f;
                }
                set_disabled(Misc_IsHost() != 1);
                toggle("Wallbang", &g_Misc.Wallbang);
                set_disabled(false);
                column(1);
                section("Damage");
                toggle("Damage multiplier", &g_Misc.DamageBoost, "m_damage");
                toggle("Weapon speed", &g_Misc.WeaponSpeed, "m_wspeed");
                section("Ammo");
                toggle("Infinite ammo", &g_Misc.InfiniteAmmo);
                toggle("Instant reload", &g_Misc.InstaReload, "m_reload");
                section("Feedback");
                toggle("Kill sound", &g_Misc.KillSound, "m_kill");
            } },
            { "Player", [] {
                const int host = Misc_IsHost();
                column(0);
                section("Lobby");
                value_row("You are", host == 1 ? "Host" : host == 0 ? "Client" : "Not in a match",
                          host == 1 ? C(80, 220, 140) : C(140, 140, 156));
                set_disabled(host != 1);
                section("Movement");
                toggle("Fly", &g_Misc.Fly, "m_fly");
                toggle("Noclip", &g_Misc.Noclip);
                toggle("Speedhack", &g_Misc.Speedhack, "m_speed");
                toggle("Teleport to enemy", &g_Misc.Teleport, "m_tp");
                set_disabled(false);
                column(1);
                set_disabled(host != 1);
                section("Survival");
                toggle("God mode", &g_Misc.GodMode);
                toggle("No drone cooldown", &g_Misc.NoDroneCooldown);
                section("World");
                toggle("Freeze bots", &g_Misc.FreezeBots, "m_freeze");
                set_disabled(false);
            } },
        } });

        g_pages.push_back({ ICON_FA_TABLE_CELLS_LARGE, "Widgets", {
            { "Elements", [] {
                column(0);
                section("Info");
                toggle("Watermark", &g_Hud.Watermark, "watermark");
                toggle("Keybinds", &g_Hud.Keybinds, "keybinds");
                toggle("Notifications", &g_Hud.Notifications, "notify");
                toggle("Enemy stats", &g_Hud.Stats, "stats");
                column(1);
                section("Combat");
                toggle("Ammo", &g_Hud.Ammo, "ammo");
                toggle("Health", &g_Hud.Health, "healthw");
                toggle("Kills", &g_Hud.Kills, "kills");
                toggle("Kill / hit / miss feed", &g_Hud.CombatLog, "combatlog");
                toggle("Target HUD", &g_Hud.TargetHud, "targethud");
                section("Media");
                toggle("Music player", &g_Hud.Music, "music");
                section("Layout");
                static const char* const kReset[] = { ICON_FA_ROTATE_LEFT "  Reset widget positions" };
                if (center_buttons("hudreset", kReset, 1) == 0) Hud_ResetPositions();
                note("Drag any widget while the menu is open.");
            } },
        } });

        g_pages.push_back({ ICON_FA_FOLDER_OPEN, "Configs", {
            { "Manage", [] {
                static std::string selected;
                static char nameBuf[33] = {};
                static std::string deleteArmedFor;
                static double deleteArmed = -10.0;
                const std::vector<ConfigInfo> list = Config_List();
                bool selectedExists = false;
                for (const ConfigInfo& c : list) if (c.name == selected) selectedExists = true;
                if (!selectedExists) selected = !Config_Active().empty() && Config_Exists(Config_Active()) ? Config_Active()
                                              : (list.empty() ? std::string() : list.front().name);
                auto notify = [](const char* icon, const std::string& text) { Hud_Notify(icon, text.c_str()); };
                auto ago = [](double a, char* out, size_t n) {
                    if (a < 60) std::snprintf(out, n, "Saved just now");
                    else if (a < 3600) std::snprintf(out, n, "Saved %d min ago", static_cast<int>(a / 60));
                    else if (a < 86400) std::snprintf(out, n, "Saved %d h ago", static_cast<int>(a / 3600));
                    else std::snprintf(out, n, "Saved %d days ago", static_cast<int>(a / 86400));
                };
                auto doLoad = [&](const std::string& n) {
                    if (Config_Load(n)) notify(ICON_FA_FILE_IMPORT, "\"" + n + "\" has been loaded");
                    else notify(ICON_FA_XMARK, Config_LastError());
                };
                auto doSave = [&](const std::string& n) {
                    if (Config_Save(n)) notify(ICON_FA_FLOPPY_DISK, "\"" + n + "\" has been saved");
                    else notify(ICON_FA_XMARK, Config_LastError());
                };
                auto doDelete = [&](const std::string& n) {
                    const bool armed = deleteArmedFor == n && ImGui::GetTime() - deleteArmed < 3.0;
                    if (!armed) { deleteArmedFor = n; deleteArmed = ImGui::GetTime(); return; }
                    deleteArmedFor.clear();
                    if (Config_Delete(n)) { notify(ICON_FA_TRASH, "\"" + n + "\" has been deleted"); if (selected == n) selected.clear(); }
                    else notify(ICON_FA_XMARK, Config_LastError());
                };

                kit::column(0);
                char hdr[48];
                std::snprintf(hdr, sizeof(hdr), "Your configs  \xc2\xb7  %d", static_cast<int>(list.size()));
                kit::section(hdr);
                if (list.empty()) kit::note("Nothing saved yet. Name one on the right and press Create.");
                for (const ConfigInfo& c : list) {
                    char sub[64];
                    ago(c.ageSeconds, sub, sizeof(sub));
                    const bool active = c.name == Config_Active();
                    const bool armed = deleteArmedFor == c.name && ImGui::GetTime() - deleteArmed < 3.0;
                    switch (kit::config_card(c.name.c_str(), sub, selected == c.name, active, active && Config_Dirty(),
                                             Config_Autoload() == c.name, armed)) {
                    case kit::ConfigCard_Select: selected = c.name; break;
                    case kit::ConfigCard_Load:   selected = c.name; doLoad(c.name); break;
                    case kit::ConfigCard_Save:   selected = c.name; doSave(c.name); break;
                    case kit::ConfigCard_Delete: doDelete(c.name); break;
                    case kit::ConfigCard_Rename: {
                        const std::string to = Config_Sanitize(kit::config_card_rename_text());
                        if (to == c.name) break;
                        if (to.empty()) notify(ICON_FA_XMARK, "Type a name (letters, numbers, - _)");
                        else if (Config_Exists(to)) notify(ICON_FA_XMARK, "\"" + to + "\" already exists");
                        else if (Config_Rename(c.name, to)) { notify(ICON_FA_PEN, "Renamed to \"" + to + "\""); selected = to; }
                        else notify(ICON_FA_XMARK, Config_LastError());
                        break;
                    }
                    default: break;
                    }
                }
                if (!list.empty()) kit::note("Double-click to load, pencil to rename.");

                kit::column(1);
                kit::section("Create");
                const bool enter = kit::input_row("Name", nameBuf, sizeof(nameBuf), "");
                static const char* const kCreate[] = { ICON_FA_PLUS "  Create from current settings" };
                if (kit::buttons_row("create", kCreate, 1) == 0 || enter) {
                    const std::string name = Config_Sanitize(nameBuf);
                    if (name.empty()) notify(ICON_FA_XMARK, "Type a name first (letters, numbers, - _)");
                    else if (Config_Exists(name)) notify(ICON_FA_XMARK, "\"" + name + "\" already exists");
                    else if (Config_Save(name)) { notify(ICON_FA_FLOPPY_DISK, "\"" + name + "\" has been created"); selected = name; nameBuf[0] = 0; }
                    else notify(ICON_FA_XMARK, Config_LastError());
                }

                static std::string selHdr;
                selHdr = selected.empty() ? std::string("Selected") : "Selected  \xc2\xb7  " + selected;
                kit::section(selHdr.c_str());
                if (selected.empty()) {
                    kit::note("Pick a config on the left.");
                } else {
                    const bool active = selected == Config_Active();
                    kit::value_row("Status", active ? (Config_Dirty() ? "Loaded, unsaved changes" : "Loaded") : "Not loaded",
                                   active ? (Config_Dirty() ? kit::C(255, 190, 90) : kit::Accent()) : kit::C(140, 140, 156));
                    bool autoload = Config_Autoload() == selected;
                    if (kit::toggle("Load on startup", &autoload)) Config_SetAutoload(autoload ? selected : std::string());
                }

                kit::section("Tools");
                static const char* const kTools[] = { ICON_FA_ROTATE_LEFT "  Reset all settings", ICON_FA_FOLDER_OPEN "  Open folder" };
                const int tr = kit::center_buttons("tools", kTools, 2);
                if (tr == 0) {
                    Config_ResetDefaults();
                    notify(ICON_FA_ROTATE_LEFT, "Settings reset to defaults");
                } else if (tr == 1) Config_OpenFolder();
            } },
        } });
    }

    ImVec2 g_menuPos2{ -1, -1 };

    void DrawSidebar(ImDrawList* dl, ImVec2 o) {
        using kit::C;
        {
            const ImVec2 ta(o.x + 16, o.y + 15), tb(ta.x + 36, ta.y + 36);
            dl->AddRectFilled(ta, tb, C(24, 24, 31), 9.0f);
            dl->AddRectFilledMultiColor({ ta.x + 1, ta.y + 1 }, { tb.x - 1, ta.y + 14 }, C(255, 255, 255, 7), C(255, 255, 255, 7), C(255, 255, 255, 0), C(255, 255, 255, 0));
            dl->AddRect(ta, tb, C(255, 255, 255, 18), 9.0f);
            const ImVec2 c((ta.x + tb.x) * 0.5f, (ta.y + tb.y) * 0.5f);
            const ImVec2 p0(c.x + 4.2f, c.y - 10.0f), p1(c.x - 4.2f, c.y + 10.0f);
            const float r = 2.1f;
            const float ang = std::atan2(p1.y - p0.y, p1.x - p0.x);
            const int v0 = dl->VtxBuffer.Size;
            dl->PathArcTo(p0, r, ang + IM_PI * 0.5f, ang + IM_PI * 1.5f, 10);
            dl->PathArcTo(p1, r, ang - IM_PI * 0.5f, ang + IM_PI * 0.5f, 10);
            dl->PathFillConvex(C(255, 255, 255));
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, { c.x, p0.y - r }, { c.x, p1.y + r },
                                                          kit::mix(C(169, 156, 255), kit::Accent(), 0.35f), C(91, 140, 255));
        }
        const float bx = o.x + 62;
        const float slashW = kit::TextSize(kit::Bold(), 18.0f, "/").x;
        kit::Text(dl, kit::Bold(), 18.0f, { bx, o.y + 15 }, kit::Accent(), "/");
        kit::Text(dl, kit::Bold(), 18.0f, { bx + 1 + slashW, o.y + 15 }, C(245, 245, 250), "var");
        kit::Text(dl, kit::Regular(), 11.5f, { bx + 1, o.y + 36 }, C(125, 125, 138), "Bodycam");
        dl->AddLine({ o.x + 12, o.y + kNavTop - 9 }, { o.x + kSideW - 12, o.y + kNavTop - 9 }, C(255, 255, 255, 10));

        static float indicatorY = -1.0f;
        {
            const float targetY = static_cast<float>(g_curPage) * 30.0f;
            if (indicatorY < 0.0f) indicatorY = targetY;
            indicatorY = ImLerp(indicatorY, targetY, ImMin(1.0f, ImGui::GetIO().DeltaTime * 16.0f));
            if (!kit::searching()) {
                const ImVec2 a(o.x + 10, o.y + kNavTop + indicatorY), b(o.x + kSideW - 10, o.y + kNavTop + indicatorY + 28);
                dl->AddRectFilled(a, b, C(31, 31, 38), 6.0f);
                kit::Capsule(dl, { a.x, a.y + 7 }, { a.x + 4.0f, b.y - 7 }, kit::Accent());
            }
        }
        float y = o.y + kNavTop;
        for (int i = 0; i < static_cast<int>(g_pages.size()); ++i) {
            const Page& p = g_pages[i];
            const ImVec2 a(o.x + 10, y), b(o.x + kSideW - 10, y + 28);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##nav", b - a) && g_curPage != i) {
                StartSlide(i > g_curPage ? 1 : -1);
                g_curPage = i; g_contentT = 0.0f; g_search[0] = 0; kit::set_filter("");
            }
            const bool hov = ImGui::IsItemHovered();
            const ImGuiID nid = ImGui::GetItemID();
            ImGui::PopID();
            const bool act = g_curPage == i && !kit::searching();
            const float h = kit::anim(nid, hov && !act ? 1.0f : 0.0f, 16.0f, 0.0f);
            const float at = kit::anim(nid + 1, act ? 1.0f : 0.0f, 14.0f, 0.0f);
            if (h > 0.01f) dl->AddRectFilled(a, b, C(255, 255, 255, static_cast<int>(6 * h)), 6.0f);
            const ImU32 tc = kit::mix(kit::mix(C(125, 125, 138), C(195, 195, 205), h), C(240, 240, 246), at);
            const float cy = (a.y + b.y) * 0.5f;
            const ImVec2 is = kit::TextSize(kit::Regular(), 12.0f, p.icon);
            const ImVec2 ls = kit::TextSize(kit::Regular(), 12.5f, p.name);
            kit::Text(dl, kit::Regular(), 12.0f, { ImFloor(a.x + 18 - is.x * 0.5f), ImFloor(cy - is.y * 0.5f) }, kit::mix(tc, kit::Accent(), at), p.icon);
            kit::Text(dl, kit::Regular(), 12.5f, { a.x + 34, ImFloor(cy - ls.y * 0.5f) }, tc, p.name);
            y += 30;
        }

        const float fy = o.y + kMenuH - 52;
        dl->AddLine({ o.x + 12, fy }, { o.x + kSideW - 12, fy }, C(255, 255, 255, 10));
        {
            const ImVec2 pa(o.x + 8, fy + 6), pb(o.x + kSideW - 8, fy + 46);
            ImGui::SetCursorScreenPos(pa);
            const bool click = ImGui::InvisibleButton("##profile", pb - pa);
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const float h = kit::anim(ImGui::GetItemID(), hov || kit::panel_is_open("profile") ? 1.0f : 0.0f, 16.0f, 0.0f);
            if (h > 0.01f) dl->AddRectFilled(pa, pb, C(255, 255, 255, static_cast<int>(8 * h)), 8.0f);
            if (click) {
                if (kit::panel_is_open("profile")) kit::close_panel("profile");
                else {
                    const float pw = 228.0f;
                    float px = o.x - pw - 12.0f;
                    if (px < 8.0f) px = o.x + kSideW + 8.0f;
                    kit::open_panel_bottom("profile", px, o.y + kMenuH);
                }
            }
        }
        const ImVec2 c(o.x + 28, fy + 26);
        if (ImTextureData* av = SteamAvatar()) {
            dl->AddImageRounded(av->GetTexRef(), { c.x - 14, c.y - 14 }, { c.x + 14, c.y + 14 }, { 0, 0 }, { 1, 1 }, C(255, 255, 255), 14.0f);
        } else {
            dl->AddCircleFilled(c, 14.0f, C(36, 36, 44), 24);
            kit::TextCentered(dl, kit::Regular(), 12.0f, c, C(230, 230, 238), ICON_FA_USER);
        }
        const std::string& persona = SteamPersona();
        dl->PushClipRect({ o.x + 48, fy }, { o.x + kSideW - 32, fy + 52 }, true);
        {
            const char* nm = persona.empty() ? "Offline" : persona.c_str();
            const ImVec2 ns = kit::TextSize(kit::Bold(), 12.5f, nm);
            kit::Text(dl, kit::Bold(), 12.5f, { o.x + 50, c.y - ns.y * 0.5f }, C(236, 236, 242), nm);
        }
        dl->PopClipRect();
    }

    void GoToConfigsPage() {
        for (int i = 0; i < static_cast<int>(g_pages.size()); ++i)
            if (std::strcmp(g_pages[i].name, "Configs") == 0) { g_curPage = i; g_contentT = 0.0f; }
        g_search[0] = 0;
        kit::set_filter("");
    }

    std::string AgeText(double a) {
        char b[32];
        if (a < 60) std::snprintf(b, sizeof(b), "Just now");
        else if (a < 3600) std::snprintf(b, sizeof(b), "%d min ago", static_cast<int>(a / 60));
        else if (a < 86400) std::snprintf(b, sizeof(b), "%d h ago", static_cast<int>(a / 3600));
        else std::snprintf(b, sizeof(b), "%d days ago", static_cast<int>(a / 86400));
        return b;
    }

    bool PopButton(ImDrawList* dl, const char* id, ImVec2 a, ImVec2 b, const char* icon, const char* text,
                   bool primary, bool enabled = true) {
        using kit::C;
        ImGui::SetCursorScreenPos(a);
        ImGui::BeginDisabled(!enabled);
        const bool click = ImGui::InvisibleButton(id, b - a);
        ImGui::EndDisabled();
        const bool hov = enabled && ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const float h = kit::anim(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f, 0.0f);
        const ImU32 bg = !enabled ? C(30, 30, 37) : primary ? kit::mix(kit::Accent(0.85f), kit::Accent(1.0f), h)
                                                            : kit::mix(C(33, 33, 40), C(46, 46, 56), h);
        dl->AddRectFilled(a, b, bg, 6.0f);
        if (!primary && enabled) dl->AddRect(a, b, C(255, 255, 255, static_cast<int>(7 + 7 * h)), 6.0f);
        const ImU32 fg = !enabled ? C(95, 95, 108) : primary ? C(255, 255, 255) : C(215, 215, 225);
        const ImVec2 is = icon ? kit::TextSize(kit::Regular(), 11.5f, icon) : ImVec2(0, 0);
        const ImVec2 ts = text ? kit::TextSize(kit::Regular(), 12.0f, text) : ImVec2(0, 0);
        const float gap = icon && text ? 7.0f : 0.0f;
        float x = ImFloor((a.x + b.x - is.x - gap - ts.x) * 0.5f);
        const float cy = (a.y + b.y) * 0.5f;
        if (icon && !text) kit::TextCentered(dl, kit::Regular(), 11.5f, { (a.x + b.x) * 0.5f, cy }, fg, icon);
        else if (icon) { kit::Text(dl, kit::Regular(), 11.5f, { x, cy - is.y * 0.5f }, fg, icon); x += is.x + gap; }
        if (text) kit::Text(dl, kit::Regular(), 12.0f, { x, cy - ts.y * 0.5f }, fg, text);
        return click && enabled;
    }

    bool   g_cfgPopOpen = false;
    ImVec2 g_cfgPopMin{}, g_cfgPopMax{};
    ImVec2 g_cfgPopOffset{ 0, 0 };

    void FancyTip(ImVec2 anchor, const char* icon, const char* text, float t) {
        if (t < 0.01f) return;
        using kit::C;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float fs = 11.5f;
        const ImVec2 ts = kit::TextSize(kit::Regular(), fs, text);
        const ImVec2 is = icon ? kit::TextSize(kit::Regular(), 10.5f, icon) : ImVec2(0, 0);
        const float w = ts.x + (icon ? is.x + 7 : 0) + 20, h = 26;
        const float lift = (1.0f - t) * 4.0f;
        const ImVec2 a(ImFloor(anchor.x - w * 0.5f), ImFloor(anchor.y - h - 8 + lift)), b(a.x + w, a.y + h);
        const float al = ImGui::GetStyle().Alpha * t;
        auto A = [al](ImU32 c) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((c >> IM_COL32_A_SHIFT) & 0xFF) * al) << IM_COL32_A_SHIFT); };
        for (int i = 1; i <= 3; ++i) fg->AddRectFilled({ a.x - i * 1.5f, a.y - i * 1.5f + 2 }, { b.x + i * 1.5f, b.y + i * 1.5f + 2 }, A(IM_COL32(0, 0, 0, 30 - i * 8)), 7.0f + i);
        fg->AddRectFilled(a, b, A(IM_COL32(30, 30, 37, 250)), 7.0f);
        fg->AddRect(a, b, A(IM_COL32(255, 255, 255, 18)), 7.0f);
        const ImVec2 tip(anchor.x, b.y + 5);
        fg->AddTriangleFilled({ tip.x - 5, b.y }, { tip.x + 5, b.y }, tip, A(IM_COL32(30, 30, 37, 250)));
        float x = a.x + 10;
        const float cy = (a.y + b.y) * 0.5f;
        if (icon) {
            kit::Text(fg, kit::Regular(), 10.5f, { x, cy - is.y * 0.5f }, A(kit::Accent()), icon);
            x += is.x + 7;
        }
        kit::Text(fg, kit::Regular(), fs, { x, cy - ts.y * 0.5f }, A(IM_COL32(230, 230, 238, 255)), text);
    }

    void ConfigPopup(const std::string& active, bool dirty, bool visible) {
        using kit::C;
        constexpr float W = 276.0f;
        static char newName[33] = {};
        auto notify = [](const char* icon, const std::string& t) { Hud_Notify(icon, t.c_str()); };

        if (!visible) return;
        ImGui::SetNextWindowSize({ W, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.086f, 0.106f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.07f));
        ImGui::Begin("##cfgpop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                     (g_MenuOpen && g_cfgPopOpen ? 0 : ImGuiWindowFlags_NoInputs));
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(6);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        float y = p.y;
        {
            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("##cfgdrag", { W - 96, 58 });
            const bool dh = ImGui::IsItemHovered(), da = ImGui::IsItemActive();
            if (dh || da) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (da && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                g_cfgPopOffset.x += ImGui::GetIO().MouseDelta.x;
                g_cfgPopOffset.y += ImGui::GetIO().MouseDelta.y;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) g_cfgPopOffset = { 0, 0 };
        }
        {
            const ImVec2 ws = ImGui::GetWindowSize();
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            for (int i = 1; i <= 6; ++i) {
                const float g = i * 2.5f;
                bg->AddRectFilled({ p.x - g, p.y - g + 4 }, { p.x + ws.x + g, p.y + ws.y + g + 4 },
                                  C(0, 0, 0, static_cast<int>(22 - i * 3)), 10.0f + g);
            }
        }

        const std::vector<ConfigInfo> list = Config_List();
        const ImVec2 titleSz = kit::TextSize(kit::Bold(), 14.0f, "Configs");
        const float titleMid = y + 22.0f;
        kit::Text(dl, kit::Bold(), 14.0f, { p.x + 16, titleMid - titleSz.y * 0.5f }, C(240, 240, 246), "Configs");
        {
            char n[8]; std::snprintf(n, sizeof(n), "%d", static_cast<int>(list.size()));
            const float tx = p.x + 16 + titleSz.x + 8;
            const ImVec2 ns = kit::TextSize(kit::Regular(), 10.5f, n);
            const float bw = ImMax(18.0f, ns.x + 12.0f);
            dl->AddRectFilled({ tx, titleMid - 8 }, { tx + bw, titleMid + 8 }, C(255, 255, 255, 14), 8.0f);
            kit::TextCentered(dl, kit::Regular(), 10.5f, { tx + bw * 0.5f, titleMid }, C(175, 175, 188), n);
        }
        {
            const ImU32 dot = active.empty() ? C(110, 110, 124) : dirty ? C(255, 190, 90) : C(110, 220, 150);
            const char* st = active.empty() ? "No config loaded" : dirty ? "Unsaved changes" : "All changes saved";
            const float statusMid = y + 41.0f;
            dl->AddCircleFilled({ p.x + 19.0f, statusMid }, 3.0f, dot, 12);
            std::string line = active.empty() ? st : active + "  \xc2\xb7  " + st;
            dl->PushClipRect({ p.x + 16, y + 30 }, { p.x + W - 96, y + 52 }, true);
            kit::Text(dl, kit::Regular(), 11.0f, { p.x + 28, statusMid - kit::TextSize(kit::Regular(), 11.0f, "Ag").y * 0.5f },
                      C(140, 140, 154), line.c_str());
            dl->PopClipRect();
        }
        if (PopButton(dl, "##cfgsave", { p.x + W - 86, y + 18 }, { p.x + W - 14, y + 45 }, ICON_FA_FLOPPY_DISK, "Save",
                      dirty, !active.empty())) {
            if (Config_Save(active)) notify(ICON_FA_FLOPPY_DISK, "\"" + active + "\" has been saved");
            else notify(ICON_FA_XMARK, Config_LastError());
        }
        y += 60;
        dl->AddLine({ p.x, y }, { p.x + W, y }, C(255, 255, 255, 10));

        y += 12;
        {
            const ImVec2 fa(p.x + 12, y), fb(p.x + W - 48, y + 30);
            const bool focused = ImGui::GetID("##cfgnew") == ImGui::GetActiveID();
            const float ff = kit::anim(ImGui::GetID("##cfgnewfoc"), focused ? 1.0f : 0.0f, 16.0f, 0.0f);
            dl->AddRectFilled(fa, fb, kit::mix(C(27, 27, 33), C(31, 31, 38), ff), 7.0f);
            dl->AddRect(fa, fb, ff > 0.01f ? kit::Accent(0.55f * ff) : C(255, 255, 255, 7), 7.0f, 0, 1.0f);
            const float fh = fb.y - fa.y, fmid = fa.y + fh * 0.5f;
            const ImVec2 ps = kit::TextSize(kit::Regular(), 11.0f, ICON_FA_PLUS);
            kit::Text(dl, kit::Regular(), 11.0f, { fa.x + 12, fmid - ps.y * 0.5f }, focused ? kit::Accent() : C(120, 120, 134), ICON_FA_PLUS);
            ImGui::SetCursorScreenPos({ fa.x + 12 + ps.x + 9, fa.y });
            ImGui::PushFont(kit::Regular(), 12.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, (fh - ImGui::GetFontSize()) * 0.5f });
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.47f, 0.47f, 0.53f, 1));
            ImGui::SetNextItemWidth(fb.x - fa.x - (12 + ps.x + 9) - 10);
            const bool enter = ImGui::InputTextWithHint("##cfgnew", "New config name", newName, sizeof(newName),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
            ImGui::PopFont();
            const std::string name = Config_Sanitize(newName);
            const bool create = PopButton(dl, "##cfgcreate", { fb.x + 6, y }, { p.x + W - 12, y + 30 }, ICON_FA_CHECK, nullptr,
                                          !name.empty(), !name.empty());
            if ((enter || create) && !name.empty()) {
                if (Config_Exists(name)) notify(ICON_FA_XMARK, "\"" + name + "\" already exists");
                else if (Config_Save(name)) { notify(ICON_FA_FLOPPY_DISK, "\"" + name + "\" has been created"); newName[0] = 0; }
                else notify(ICON_FA_XMARK, Config_LastError());
            }
        }
        y += 42;

        {
            const char* lbl = "Saved configs";
            kit::Capsule(dl, { p.x + 12.5f, y + 2.5f }, { p.x + 15.5f, y + 12.5f }, kit::Accent(0.9f));
            kit::Text(dl, kit::Bold(), 12.0f, { p.x + 21, y }, C(226, 226, 234), lbl);
            const float lx0 = p.x + 21 + kit::TextSize(kit::Bold(), 12.0f, lbl).x + 10, lx1 = p.x + W - 12;
            dl->AddRectFilledMultiColor({ lx0, y + 7 }, { lx1, y + 8 }, C(255, 255, 255, 18), C(255, 255, 255, 0),
                                        C(255, 255, 255, 0), C(255, 255, 255, 18));
        }
        y += 22;
        const std::string& autoload = Config_Autoload();
        if (list.empty()) {
            const char* t1 = "No configs yet";
            const char* t2 = "Name one above to save your settings";
            kit::Text(dl, kit::Regular(), 18.0f, { p.x + (W - kit::TextSize(kit::Regular(), 18.0f, ICON_FA_FILE_LINES).x) * 0.5f, y + 8 },
                      C(80, 80, 92), ICON_FA_FILE_LINES);
            kit::Text(dl, kit::Bold(), 12.0f, { p.x + (W - kit::TextSize(kit::Bold(), 12.0f, t1).x) * 0.5f, y + 34 }, C(190, 190, 200), t1);
            kit::Text(dl, kit::Regular(), 11.0f, { p.x + (W - kit::TextSize(kit::Regular(), 11.0f, t2).x) * 0.5f, y + 51 }, C(120, 120, 134), t2);
            y += 76;
        } else {
            constexpr float rowH = 42.0f;
            const float listH = ImMin(static_cast<float>(list.size()), 5.0f) * rowH;
            ImGui::SetCursorScreenPos({ p.x, y });
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.12f));
            ImGui::BeginChild("##cfglist", { W, listH }, 0, ImGuiWindowFlags_NoBackground);
            ImDrawList* ldl = ImGui::GetWindowDrawList();
            for (size_t i = 0; i < list.size(); ++i) {
                const ConfigInfo& c = list[i];
                const bool isActive = c.name == active;
                const ImVec2 ra(p.x + 12, ImGui::GetCursorScreenPos().y), rb(p.x + W - 12, ra.y + rowH - 4);
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetCursorScreenPos(ra);
                const bool click = ImGui::InvisibleButton("##row", { rb.x - ra.x, rowH });
                const bool hov = ImGui::IsMouseHoveringRect(ra, rb) && ImGui::IsItemHovered();
                ImGui::PopID();
                const float rh = kit::anim(ImGui::GetID(c.name.c_str()), hov ? 1.0f : 0.0f, 16.0f, 0.0f);
                if (hov && !isActive) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (isActive) {
                    ldl->AddRectFilled(ra, rb, kit::Accent(0.12f), 8.0f);
                    ldl->AddRect(ra, rb, kit::Accent(0.35f), 8.0f);
                } else if (rh > 0.01f) {
                    ldl->AddRectFilled(ra, rb, C(255, 255, 255, static_cast<int>(8 * rh)), 8.0f);
                    ldl->AddRect(ra, rb, C(255, 255, 255, static_cast<int>(12 * rh)), 8.0f);
                }
                const float tmid = (ra.y + rb.y) * 0.5f;
                const ImVec2 ia(ra.x + 6, tmid - 13), ib(ra.x + 32, tmid + 13);
                ldl->AddRectFilled(ia, ib, isActive ? kit::Accent(0.9f) : C(36, 36, 44), 7.0f);
                const ImVec2 is = kit::TextSize(kit::Regular(), 11.5f, ICON_FA_FILE_LINES);
                kit::Text(ldl, kit::Regular(), 11.5f, { (ia.x + ib.x - is.x) * 0.5f, (ia.y + ib.y - is.y) * 0.5f },
                          isActive ? kit::OnAccent() : C(165, 165, 178), ICON_FA_FILE_LINES);
                ldl->PushClipRect({ ra.x + 40, ra.y }, { rb.x - 56, rb.y }, true);
                const float rmid = (ra.y + rb.y) * 0.5f;
                kit::Text(ldl, kit::Bold(), 12.5f, { ra.x + 42, rmid - 15.0f }, C(236, 236, 242), c.name.c_str());
                std::string sub = AgeText(c.ageSeconds);
                if (c.name == autoload) sub += "  \xc2\xb7  " ICON_FA_STAR " Startup";
                kit::Text(ldl, kit::Regular(), 10.5f, { ra.x + 42, rmid + 1.5f }, C(125, 125, 140), sub.c_str());
                ldl->PopClipRect();
                if (isActive) {
                    const char* tag = dirty ? "Edited" : "Active";
                    const float tw = kit::TextSize(kit::Regular(), 10.5f, tag).x;
                    const float m = (ra.y + rb.y) * 0.5f;
                    const float th = kit::TextSize(kit::Regular(), 10.5f, tag).y;
                    const ImVec2 ta(rb.x - tw - 22, m - 8), tb(rb.x - 8, m + 8);
                    ldl->AddRectFilled(ta, tb, dirty ? C(255, 190, 90, 40) : kit::Accent(0.25f), 8.0f);
                    kit::Text(ldl, kit::Regular(), 10.5f, { ta.x + (tb.x - ta.x - tw) * 0.5f, m - th * 0.5f },
                              dirty ? C(255, 200, 120) : C(225, 220, 255), tag);
                } else if (rh > 0.01f) {
                    const float m = (ra.y + rb.y) * 0.5f;
                    const ImVec2 ls = kit::TextSize(kit::Regular(), 11.0f, "Load"), cs = kit::TextSize(kit::Regular(), 9.0f, ICON_FA_CHEVRON_RIGHT);
                    const float slide = (1.0f - rh) * 6.0f;
                    kit::Text(ldl, kit::Regular(), 11.0f, { rb.x - 12 - cs.x - 6 - ls.x + slide, m - ls.y * 0.5f }, kit::Accent(rh), "Load");
                    kit::Text(ldl, kit::Regular(), 9.0f, { rb.x - 12 - cs.x + slide, m - cs.y * 0.5f }, kit::Accent(rh), ICON_FA_CHEVRON_RIGHT);
                }
                if (click && !isActive) {
                    if (Config_Load(c.name)) notify(ICON_FA_FILE_IMPORT, "\"" + c.name + "\" has been loaded");
                    else notify(ICON_FA_XMARK, Config_LastError());
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            y += listH + 6;
        }

        dl->AddLine({ p.x, y }, { p.x + W, y }, C(255, 255, 255, 10));
        y += 8;
        if (PopButton(dl, "##cfgmanage", { p.x + 12, y }, { p.x + W - 50, y + 28 }, ICON_FA_SLIDERS, "Manage configs", false)) {
            GoToConfigsPage();
            g_cfgPopOpen = false;
        }
        if (PopButton(dl, "##cfgfolder", { p.x + W - 44, y }, { p.x + W - 12, y + 28 }, ICON_FA_FOLDER_OPEN, nullptr, false))
            Config_OpenFolder();
        {
            const float th = kit::anim(ImGui::GetID("##cfgfoldertip"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f, 0.0f);
            FancyTip({ p.x + W - 28, y }, ICON_FA_FOLDER_OPEN, "Open config folder", kit::ease_out(th));
        }
        y += 40;

        ImGui::SetCursorScreenPos({ p.x, y });
        ImGui::Dummy({ W, 0 });
        ImGui::PopStyleVar();
        g_cfgPopMin = ImGui::GetWindowPos();
        g_cfgPopMax = g_cfgPopMin + ImGui::GetWindowSize();
        ImGui::End();
    }

    bool   g_searchOpen = false;
    ImVec2 g_searchPopMin{}, g_searchPopMax{};
    ImVec2 g_searchPopOffset{ 0, 0 };

    void SearchDropdown(ImVec2 anchor) {
        using kit::C;
        static float t = 0.0f;
        const float dt = ImMin(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
        const bool open = g_searchOpen && g_search[0];
        static std::string query;
        if (g_search[0]) query = g_search;
        t = open ? ImMin(1.0f, t + dt / 0.26f) : ImMax(0.0f, t - dt / 0.17f);
        if (t <= 0.0f) { g_searchPopOffset = { 0, 0 }; return; }
        float a, sc, dr;
        if (open) {
            const float inv = t - 1.0f;
            const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
            a = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); sc = 0.94f + 0.06f * back; dr = (1.0f - a) * 10.0f;
        } else {
            const float c = 1.0f - t;
            a = 1.0f - c * c * (3.0f - 2.0f * c); a *= a; sc = 1.0f - 0.05f * c * c; dr = c * c * 8.0f;
        }
        constexpr float W = 210.0f, maxH = 300.0f;
        {
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            g_searchPopOffset.x = ImClamp(g_searchPopOffset.x, -anchor.x, ds.x - anchor.x - W);
            g_searchPopOffset.y = ImClamp(g_searchPopOffset.y, -anchor.y, ds.y - anchor.y - 80.0f);
        }
        static float listH = 60.0f, listTarget = 60.0f, chrome = 52.0f;
        listH = listTarget;
        const float shownList = ImMin(listH, maxH);
        ImGui::SetNextWindowPos(anchor + g_searchPopOffset, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ W, std::floor(shownList + chrome) });
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 6, 8 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 6, 2 });
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.086f, 0.106f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.07f));
        ImGui::Begin("##searchpop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | (open ? 0 : ImGuiWindowFlags_NoInputs));
        if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        static int lastCount = 0;
        {
            const ImVec2 c = ImGui::GetCursorScreenPos();
            const float hw = W - 12, hh = 34;
            ImGui::InvisibleButton("##searchdrag", { hw - 24, hh });
            const bool dh = ImGui::IsItemHovered(), da = ImGui::IsItemActive();
            if (dh || da) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (da && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) g_searchPopOffset += ImGui::GetIO().MouseDelta;
            if (dh && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) g_searchPopOffset = { 0, 0 };
            if (kit::MacClose("##searchclose", { std::floor(c.x + hw - 7.0f) + 0.5f, std::floor(c.y + 9.0f) + 0.5f }, dl)) {
                g_search[0] = 0;
                g_searchOpen = false;
            }
            ImGui::SetCursorScreenPos({ c.x, c.y + hh });
            const float l1 = c.y + 9.0f;
            const ImVec2 is = kit::TextSize(kit::Regular(), 11.0f, ICON_FA_MAGNIFYING_GLASS);
            kit::Text(dl, kit::Regular(), 11.0f, { c.x + 4, l1 - is.y * 0.5f }, kit::Accent(), ICON_FA_MAGNIFYING_GLASS);
            const ImVec2 ts = kit::TextSize(kit::Bold(), 12.5f, "Results");
            kit::Text(dl, kit::Bold(), 12.5f, { c.x + 21, l1 - ts.y * 0.5f }, C(238, 238, 244), "Results");
            {
                char n[12]; std::snprintf(n, sizeof(n), "%d", lastCount);
                const ImVec2 ns = kit::TextSize(kit::Regular(), 10.5f, n);
                const float bw = ImMax(18.0f, ns.x + 12.0f), bx = c.x + 21 + ts.x + 7;
                dl->AddRectFilled({ bx, l1 - 7.5f }, { bx + bw, l1 + 7.5f }, C(255, 255, 255, 14), 7.5f);
                kit::TextCentered(dl, kit::Regular(), 10.5f, { bx + bw * 0.5f, l1 }, C(175, 175, 188), n);
            }
            {
                std::string q = std::string("for \"") + query + "\"";
                dl->PushClipRect({ c.x, c.y }, { c.x + hw, c.y + hh }, true);
                kit::Text(dl, kit::Regular(), 11.0f, { c.x + 21, c.y + 18 }, C(128, 128, 142), q.c_str());
                dl->PopClipRect();
            }
            dl->AddRectFilledMultiColor({ c.x, c.y + hh }, { c.x + hw, c.y + hh + 1 },
                                        C(255, 255, 255, 0), C(255, 255, 255, 16), C(255, 255, 255, 16), C(255, 255, 255, 0));
            ImGui::Dummy({ hw, 3 });
        }
        float& lastH = listTarget;
        const float listTop = ImGui::GetCursorPosY();
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.12f));
        ImGui::BeginChild("##searchlist", { W - 12, shownList }, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGuiWindow* lw = ImGui::GetCurrentWindow();
            static float target = 0.0f;
            static ImGuiID lastQuery = 0;
            const ImGuiID qid = ImHashStr(query.c_str());
            if (qid != lastQuery) { lastQuery = qid; target = 0.0f; ImGui::SetScrollY(0.0f); }
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f && ImGui::IsWindowHovered()) target -= wheel * 72.0f;
            target = ImClamp(target, 0.0f, lw->ScrollMax.y);
            float cur = lw->Scroll.y;
            const float k = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 16.0f);
            cur = ImFabs(target - cur) < 0.5f ? target : cur + (target - cur) * k;
            if (cur != lw->Scroll.y) ImGui::SetScrollY(cur);
        }
        const float y0 = ImGui::GetCursorPosY();
        kit::set_filter(query.c_str());
        kit::compact_rows(true);
        bool any = false;
        const float width = ImGui::GetContentRegionAvail().x;
        for (int pi = 0; pi < static_cast<int>(g_pages.size()); ++pi) {
            Page& pg = g_pages[pi];
            for (int si = 0; si < static_cast<int>(pg.subs.size()); ++si) {
                const std::string ctx = std::string(pg.name) + "  /  " + pg.subs[si].name;
                ImGui::PushID(pi * 64 + si);
                ImGuiWindow* lw = ImGui::GetCurrentWindow();
                const float startY = lw->DC.CursorPos.y, startMax = lw->DC.CursorMaxPos.y;
                kit::begin_page(width);
                kit::set_search_context(ctx.c_str());
                pg.subs[si].body();
                const bool matched = kit::page_had_matches();
                any |= matched;
                kit::end_page();
                const float endY = matched ? kit::last_match_bottom() : startY;
                lw->DC.CursorPos.y = endY;
                lw->DC.CursorMaxPos.y = ImMax(startMax, endY);
                ImGui::PopID();
            }
        }
        kit::compact_rows(false);
        kit::set_filter("");
        lastCount = any ? kit::search_match_count() : 0;
        if (!any) {
            ImDrawList* ldl = ImGui::GetWindowDrawList();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            const float cx = c.x + width * 0.5f;
            ldl->AddCircleFilled({ cx, c.y + 24 }, 15.0f, C(255, 255, 255, 10), 24);
            const ImVec2 is = kit::TextSize(kit::Regular(), 13.0f, ICON_FA_MAGNIFYING_GLASS);
            kit::Text(ldl, kit::Regular(), 13.0f, { cx - is.x * 0.5f, c.y + 24 - is.y * 0.5f }, C(140, 140, 154), ICON_FA_MAGNIFYING_GLASS);
            const char* m1 = "No settings found";
            const char* m2 = "Try a shorter or different word";
            const ImVec2 s1 = kit::TextSize(kit::Bold(), 12.0f, m1), s2 = kit::TextSize(kit::Regular(), 11.0f, m2);
            kit::Text(ldl, kit::Bold(), 12.0f, { cx - s1.x * 0.5f, c.y + 46 }, C(220, 220, 230), m1);
            kit::Text(ldl, kit::Regular(), 11.0f, { cx - s2.x * 0.5f, c.y + 63 }, C(120, 120, 134), m2);
            ImGui::Dummy({ width, 84 });
        }
        lastH = ImGui::GetCursorPosY() - y0 - (any ? 3.0f : 0.0f);
        {
            ImGuiWindow* lw = ImGui::GetCurrentWindow();
            static float lastScroll = 0.0f, vis = 0.0f;
            const bool active = ImGui::IsWindowHovered() || lw->Scroll.y != lastScroll;
            lastScroll = lw->Scroll.y;
            vis = ImLerp(vis, active ? 1.0f : 0.0f, ImMin(1.0f, ImGui::GetIO().DeltaTime * (active ? 14.0f : 4.0f)));
            if (lw->ScrollMax.y > 0.5f && vis > 0.01f) {
                const float viewH = lw->Size.y, total = viewH + lw->ScrollMax.y;
                const float th = ImMax(24.0f, viewH * viewH / total);
                const float ty = lw->Pos.y + 2 + (viewH - th - 4) * (lw->Scroll.y / lw->ScrollMax.y);
                const float x1 = lw->Pos.x + lw->Size.x + 4.5f;
                ImDrawList* ldl = lw->DrawList;
                ldl->PushClipRect(lw->Pos, lw->Pos + lw->Size + ImVec2(6, 0), false);
                kit::Capsule(ldl, { x1 - 3, ty }, { x1, ty + th }, C(255, 255, 255, static_cast<int>(60 * vis)));
                ldl->PopClipRect();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        chrome = listTop + 6.0f;
        g_searchPopMin = ImGui::GetWindowPos();
        g_searchPopMax = g_searchPopMin + ImGui::GetWindowSize();
        ImGui::End();
        ImGui::PopStyleVar();
        const ImVec2 c = (g_searchPopMin + g_searchPopMax) * 0.5f;
        {
            const ImVec2 hs = (g_searchPopMax - g_searchPopMin) * (0.5f * sc);
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            for (int i = 1; i <= 6; ++i) {
                const float g = i * 2.5f;
                bg->AddRectFilled({ c.x - hs.x - g, c.y - hs.y - g + 4 + dr }, { c.x + hs.x + g, c.y + hs.y + g + 4 + dr },
                                  C(0, 0, 0, static_cast<int>((22 - i * 3) * a)), 10.0f + g);
            }
        }
        if (sc != 1.0f || dr != 0.0f) {
            ImGuiWindow* pw = ImGui::FindWindowByName("##searchpop");
            for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
                if (!w->Active || !pw || (w != pw && w->RootWindow != pw)) continue;
                for (ImDrawVert& v : w->DrawList->VtxBuffer) { v.pos.x = c.x + (v.pos.x - c.x) * sc; v.pos.y = c.y + (v.pos.y - c.y) * sc + dr; }
                for (ImDrawCmd& cmd : w->DrawList->CmdBuffer) {
                    cmd.ClipRect.x = c.x + (cmd.ClipRect.x - c.x) * sc; cmd.ClipRect.z = c.x + (cmd.ClipRect.z - c.x) * sc;
                    cmd.ClipRect.y = c.y + (cmd.ClipRect.y - c.y) * sc + dr; cmd.ClipRect.w = c.y + (cmd.ClipRect.w - c.y) * sc + dr;
                }
            }
        }
    }

    void DrawTopBar(ImDrawList* dl, ImVec2 o) {
        using kit::C;
        const float x0 = o.x + kSideW + 12;

        const ImVec2 sa(x0, o.y + 12), sb(x0 + 210, o.y + 40);
        {
            const float f = kit::anim(ImGui::GetID("##searchfoc"), ImGui::GetActiveID() == ImGui::GetID("##search") ? 1.0f : 0.0f, 16.0f, 0.0f);
            dl->AddRectFilled(sa, sb, kit::mix(C(27, 27, 33), C(31, 31, 38), f), 7.0f);
            if (f > 0.01f) dl->AddRect(sa, sb, kit::Accent(0.55f * f), 7.0f);
        }
        const float fieldH = sb.y - sa.y, midY = sa.y + fieldH * 0.5f, inset = 12.0f;
        const ImVec2 isz = kit::TextSize(kit::Regular(), 11.5f, ICON_FA_MAGNIFYING_GLASS);
        const bool hasText = g_search[0] != 0;
        kit::Text(dl, kit::Regular(), 11.5f, { sa.x + inset, midY - isz.y * 0.5f },
                  hasText ? C(200, 200, 212) : C(120, 120, 134), ICON_FA_MAGNIFYING_GLASS);
        const float textX = sa.x + inset + isz.x + 9.0f;
        const float fontSz = 12.5f;
        ImGui::SetCursorScreenPos({ textX, sa.y });
        ImGui::PushFont(kit::Regular(), fontSz);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, (fieldH - ImGui::GetFontSize()) * 0.5f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.47f, 0.47f, 0.53f, 1));
        ImGui::SetNextItemWidth(sb.x - textX - inset);
        auto capFirst = [](ImGuiInputTextCallbackData* d) -> int {
            int lead = 0;
            while (lead < d->BufTextLen && d->Buf[lead] == ' ') ++lead;
            if (lead) d->DeleteChars(0, lead);
            if (d->BufTextLen > 0 && d->Buf[0] >= 'a' && d->Buf[0] <= 'z') { d->Buf[0] = static_cast<char>(d->Buf[0] - 32); d->BufDirty = true; }
            return 0;
        };
        if (ImGui::InputTextWithHint("##search", "Search element", g_search, sizeof(g_search), ImGuiInputTextFlags_CallbackEdit, capFirst)) {
            const bool was = g_searchOpen;
            g_searchOpen = g_search[0] != 0;
            (void)was;
        }
        if (ImGui::IsItemActivated() && g_search[0]) g_searchOpen = true;
        const bool searchFieldHov = ImGui::IsItemHovered();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::PopFont();
        if (g_searchOpen && g_MenuOpen) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool inPop = mp.x >= g_searchPopMin.x && mp.y >= g_searchPopMin.y && mp.x < g_searchPopMax.x && mp.y < g_searchPopMax.y;
            const bool onPanel = kit::mouse_on_ui() && !ImGui::IsMouseHoveringRect({ o.x, o.y }, { o.x + kMenuW, o.y + kMenuH }, false);
            const bool onCfg = ImGui::IsMouseHoveringRect({ o.x + kMenuW - 152, o.y + 12 }, { o.x + kMenuW - 14, o.y + 40 }, false) ||
                               (g_cfgPopOpen && mp.x >= g_cfgPopMin.x && mp.y >= g_cfgPopMin.y && mp.x < g_cfgPopMax.x && mp.y < g_cfgPopMax.y);
            if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inPop && !searchFieldHov && !onPanel && !onCfg) ||
                (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !kit::any_panel_open()) || !g_search[0])
                g_searchOpen = false;
        }
        SearchDropdown({ sa.x, sb.y + 6 });

        const ImVec2 ca(o.x + kMenuW - 152, o.y + 12), cb(o.x + kMenuW - 14, o.y + 40);
        ImGui::SetCursorScreenPos(ca);
        const bool cfgClick = ImGui::InvisibleButton("##cfg", cb - ca);
        const bool cfgHov = ImGui::IsItemHovered();
        const float ph = kit::anim(ImGui::GetItemID(), cfgHov || g_cfgPopOpen ? 1.0f : 0.0f, 16.0f, 0.0f);
        dl->AddRectFilled(ca, cb, kit::mix(C(27, 27, 33), C(36, 36, 44), ph), 7.0f);
        if (ph > 0.01f) dl->AddRect(ca, cb, kit::Accent(0.35f * ph), 7.0f);
        const std::string& active = Config_Active();
        const bool dirty = Config_Dirty();
        dl->PushClipRect(ca, { cb.x - 30, cb.y }, true);
        const float pillMid = (ca.y + cb.y) * 0.5f;
        kit::Text(dl, kit::Regular(), 12.5f, { ca.x + (dirty ? 24.0f : 12.0f), pillMid - kit::TextSize(kit::Regular(), 12.5f, "Ag").y * 0.5f },
                  active.empty() ? C(130, 130, 144) : C(225, 225, 233), active.empty() ? "No config" : active.c_str());
        dl->PopClipRect();
        if (dirty) dl->AddCircleFilled({ ca.x + 15.5f, pillMid }, 3.5f, C(255, 190, 90), 12);
        {
            const ImVec2 fs = kit::TextSize(kit::Regular(), 12.0f, ICON_FA_FOLDER);
            kit::TextCentered(dl, kit::Regular(), 12.0f, { cb.x - 12 - fs.x * 0.5f, pillMid }, C(170, 170, 182), ICON_FA_FOLDER);
        }
        {
            const float th = kit::anim(ImGui::GetID("##cfgpilltip"), cfgHov && dirty && !g_cfgPopOpen ? 1.0f : 0.0f, 14.0f, 0.0f);
            FancyTip({ (ca.x + cb.x) * 0.5f, ca.y }, ICON_FA_CIRCLE_EXCLAMATION, "Unsaved changes", kit::ease_out(th));
        }
        if (cfgClick) g_cfgPopOpen = !g_cfgPopOpen;
        else if (g_cfgPopOpen && g_MenuOpen) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool inPop = mp.x >= g_cfgPopMin.x && mp.y >= g_cfgPopMin.y && mp.x < g_cfgPopMax.x && mp.y < g_cfgPopMax.y;
            if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inPop && !cfgHov && !kit::mouse_on_ui()) ||
                (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !kit::any_panel_open()))
                g_cfgPopOpen = false;
        }
        {
            static float popT = 0.0f;
            const float pdt = ImMin(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
            popT = g_cfgPopOpen ? ImMin(1.0f, popT + pdt / 0.18f) : ImMax(0.0f, popT - pdt / 0.13f);
            if (popT <= 0.0f && !g_cfgPopOpen) g_cfgPopOffset = { 0, 0 };
            float pt, pscale, pdrop;
            if (g_cfgPopOpen) {
                const float inv = popT - 1.0f;
                const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
                pt = 1.0f - (1.0f - popT) * (1.0f - popT) * (1.0f - popT);
                pscale = 0.94f + 0.06f * back;
                pdrop = (1.0f - pt) * 10.0f;
            } else {
                const float c = 1.0f - popT;
                pt = 1.0f - c * c * (3.0f - 2.0f * c); pt *= pt;
                pscale = 1.0f - 0.05f * c * c;
                pdrop = c * c * 8.0f;
            }
            {
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                g_cfgPopOffset.x = ImClamp(g_cfgPopOffset.x, 276.0f - cb.x, ds.x - cb.x);
                g_cfgPopOffset.y = ImClamp(g_cfgPopOffset.y, -cb.y, ds.y - cb.y - 120.0f);
            }
            const bool visible = popT > 0.0f;
            if (visible)
                ImGui::SetNextWindowPos({ cb.x + g_cfgPopOffset.x, cb.y + 6 + g_cfgPopOffset.y }, ImGuiCond_Always, { 1.0f, 0.0f });
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * pt);
            ConfigPopup(active, dirty, visible);
            ImGui::PopStyleVar();
            if (visible && (pscale != 1.0f || pdrop != 0.0f)) {
                if (ImGuiWindow* pw = ImGui::FindWindowByName("##cfgpop")) {
                    const ImVec2 c(cb.x + g_cfgPopOffset.x, cb.y + 6 + g_cfgPopOffset.y);
                    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
                        if (!w->Active || (w != pw && w->RootWindow != pw)) continue;
                        for (ImDrawVert& v : w->DrawList->VtxBuffer) {
                            v.pos.x = c.x + (v.pos.x - c.x) * pscale;
                            v.pos.y = c.y + (v.pos.y - c.y) * pscale + pdrop;
                        }
                        for (ImDrawCmd& cmd : w->DrawList->CmdBuffer) {
                            cmd.ClipRect.x = c.x + (cmd.ClipRect.x - c.x) * pscale;
                            cmd.ClipRect.z = c.x + (cmd.ClipRect.z - c.x) * pscale;
                            cmd.ClipRect.y = c.y + (cmd.ClipRect.y - c.y) * pscale + pdrop;
                            cmd.ClipRect.w = c.y + (cmd.ClipRect.w - c.y) * pscale + pdrop;
                        }
                    }
                }
            }
        }

        auto drag = [](const char* id, ImVec2 a, ImVec2 b) {
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton(id, b - a);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                g_menuPos2.x += ImGui::GetIO().MouseDelta.x;
                g_menuPos2.y += ImGui::GetIO().MouseDelta.y;
            }
        };
        drag("##drag1", { sb.x + 8, o.y }, { ca.x - 8, o.y + 46 });
        drag("##drag2", { o.x, o.y }, { o.x + kSideW, o.y + kNavTop - 12 });

        const float by = o.y + 54;
        dl->AddLine({ x0, o.y + 79 }, { o.x + kMenuW - 14, o.y + 79 }, C(255, 255, 255, 8));
        {
            static int lastPage = -1, lastSub = -1;
            const int sub = g_pages[g_curPage].cur;
            if (lastPage >= 0 && (lastPage != g_curPage || lastSub != sub)) kit::close_panels_except("profile");
            lastPage = g_curPage; lastSub = sub;
        }
        if (kit::searching()) {
            kit::Text(dl, kit::Regular(), 12.0f, { x0 + 2, by + 1 }, C(150, 150, 162), ICON_FA_MAGNIFYING_GLASS);
            kit::Text(dl, kit::Regular(), 12.5f, { x0 + 22, by }, C(150, 150, 162), "Search");
            const float ax = x0 + 22 + kit::TextSize(kit::Regular(), 12.5f, "Search").x + 10;
            kit::Text(dl, kit::Regular(), 11.0f, { ax, by + 1.5f }, C(110, 110, 122), ICON_FA_ARROW_RIGHT_LONG);
            kit::Text(dl, kit::Regular(), 12.5f, { ax + 22, by }, C(240, 240, 246), g_search);
            return;
        }
        Page& page = g_pages[g_curPage];
        kit::Text(dl, kit::Regular(), 12.0f, { x0 + 2, by + 1 }, C(150, 150, 162), page.icon);
        kit::Text(dl, kit::Regular(), 12.5f, { x0 + 22, by }, C(150, 150, 162), page.name);
        float x = x0 + 22 + kit::TextSize(kit::Regular(), 12.5f, page.name).x + 10;
        kit::Text(dl, kit::Regular(), 11.0f, { x, by + 1.5f }, C(110, 110, 122), ICON_FA_ARROW_RIGHT_LONG);
        x += 24;
        float ulTargetX = x, ulTargetW = 0.0f;
        for (int i = 0; i < static_cast<int>(page.subs.size()); ++i) {
            const char* n = page.subs[i].name;
            const ImVec2 ts = kit::TextSize(kit::Regular(), 12.5f, n);
            ImGui::SetCursorScreenPos({ x - 4, by - 3 });
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##sub", { ts.x + 8, ts.y + 6 }) && page.cur != i) { StartSlide(i > page.cur ? 1 : -1); page.cur = i; g_contentT = 0.0f; }
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool act = page.cur == i;
            const float h = kit::anim(ImGui::GetID(n) + 7, hov ? 1.0f : 0.0f, 16.0f, 0.0f);
            const float at = kit::anim(ImGui::GetID(n) + 8, act ? 1.0f : 0.0f, 14.0f, 0.0f);
            kit::Text(dl, kit::Regular(), 12.5f, { x, by },
                      kit::mix(kit::mix(C(105, 105, 118), C(190, 190, 200), h), C(240, 240, 246), at), n);
            if (act) { ulTargetX = x; ulTargetW = ts.x; }
            x += ts.x + 16;
        }
        static int ulPage = -1;
        static float ulX = 0.0f, ulW = 0.0f;
        const float relTarget = ulTargetX - o.x;
        if (ulPage != g_curPage) { ulX = relTarget; ulW = ulTargetW; ulPage = g_curPage; }
        const float k = ImMin(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
        ulX = ImLerp(ulX, relTarget, k);
        ulW = ImLerp(ulW, ulTargetW, k);
        kit::Capsule(dl, { o.x + ulX, by + 17.5f }, { o.x + ulX + ulW, by + 20.5f }, kit::Accent());
    }

    void DrawContent(ImVec2 o) {
        const ImVec2 a(o.x + kSideW + 12, o.y + 82), b(o.x + kMenuW - 8, o.y + kMenuH - 10);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1, 1, 1, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1, 1, 1, 0.22f));
        ImGui::BeginChild("##content", b - a, 0, ImGuiWindowFlags_NoBackground);
        const int slideVtx0 = ImGui::GetWindowDrawList()->VtxBuffer.Size;

        if (g_contentT <= 0.0f) { kit::restart_entrance(); ImGui::SetScrollY(0.0f); }
        g_contentT = 1.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha);

        const float width = ImGui::GetContentRegionAvail().x - 10.0f;
        {
            Page& p = g_pages[g_curPage];
            p.cur = ImClamp(p.cur, 0, static_cast<int>(p.subs.size()) - 1);
            ImGui::PushID(g_curPage * 64 + p.cur);
            kit::begin_page(width);
            p.subs[p.cur].body();
            kit::end_page();
            ImGui::PopID();
        }
        ImGui::Dummy({ 0, 6 });
        ImGui::PopStyleVar();
        {
            const float st = static_cast<float>((ImGui::GetTime() - g_slideStart) / 0.30);
            if (g_slideDir != 0 && st < 1.0f && !kit::searching()) {
                const float dx = g_slideDir * (1.0f - kit::ease_out(st)) * 46.0f;
                ImVector<ImDrawVert>& vb = ImGui::GetWindowDrawList()->VtxBuffer;
                for (int i = slideVtx0; i < vb.Size; ++i) vb[i].pos.x += dx;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }
}

const std::string& Menu_SteamPersona() { return SteamPersona(); }

void Menu_Setup() {
    ui::apply_style();
    ui::load_fonts(ImGui::GetIO());
    ImGui::GetIO().ConfigDebugIgnoreFocusLoss = true;
    Config_Init();
}

static bool g_launched = false;
bool Menu_Launched() { return g_launched; }
void Menu_RenderHud() {
    Config_SaveSession();
    if (g_MenuOpen) g_launched = true;
    if (!g_launched) { Hud_RenderToasts(); return; }
    Hud_Render(g_MenuOpen);
}

void Menu_Render() {
    static bool insertWasDown = false;
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;

    HWND foreground = GetForegroundWindow();
    DWORD foregroundPid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    const bool utilityForeground = foreground == D3D12Overlay_GetWindow();
    const bool gameForeground = GetAttachedProcessId() != 0 && foregroundPid == GetAttachedProcessId();
    const bool targetForeground = utilityForeground || gameForeground || foreground == nullptr;

    if (g_MenuOpen && !targetForeground) g_MenuOpen = false;
    if (insertDown && !insertWasDown && targetForeground) g_MenuOpen = !g_MenuOpen;
    insertWasDown = insertDown;

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_MenuOpen;

    static bool polledDown[3] = {};
    if (g_MenuOpen) {
        MenuInput_Feed(io);
        POINT cursor{};
        if (MenuInput_CursorPos(cursor) && ScreenToClient(D3D12Overlay_GetWindow(), &cursor))
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        const int vks[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
        for (int b = 0; b < 3; ++b) {
            const bool down = MenuInput_KeyDown(vks[b]);
            if (down != polledDown[b] || down != io.MouseDown[b]) io.AddMouseButtonEvent(b, down);
            polledDown[b] = down;
        }
    } else {
        for (int b = 0; b < 3; ++b) {
            if (polledDown[b]) io.AddMouseButtonEvent(b, false);
            polledDown[b] = false;
        }
    }

    static float openT = 0.0f;
    static bool  wasOpen = false;
    if (g_MenuOpen != wasOpen) {
        if (!g_MenuOpen) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        wasOpen = g_MenuOpen;
    }
    const float dt = (std::min)(io.DeltaTime, 1.0f / 50.0f);
    openT = g_MenuOpen ? (std::min)(1.0f, openT + dt / 0.26f) : (std::max)(0.0f, openT - dt / 0.17f);
    g_MenuAnimT = openT;
    if (!g_MenuOpen && openT <= 0.0f) return;
    float eased, scale, drop;
    if (g_MenuOpen) {
        const float t = openT, inv = t - 1.0f;
        const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
        eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        scale = 0.94f + 0.06f * back;
        drop = (1.0f - eased) * 10.0f;
    } else {
        const float c = 1.0f - openT;
        eased = 1.0f - c * c * (3.0f - 2.0f * c);
        eased = eased * eased;
        scale = 1.0f - 0.05f * c * c;
        drop = c * c * 8.0f;
    }

    if (!g_pagesBuilt) {
        BuildPages();
        RegisterPanels();
        kit::on_toggle = [](const char* label, bool on) {
            if (!kit::theme.notifyOnToggle) return;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "\"%s\" has been %s", label, on ? "enabled" : "disabled");
            Hud_Notify(on ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF, buf);
        };
        g_pagesBuilt = true;
    }

    static bool placed = false;
    if (!placed) {
        g_menuPos2 = { (io.DisplaySize.x - kMenuW) * 0.5f, (io.DisplaySize.y - kMenuH) * 0.5f };
        placed = true;
    }
    g_menuPos2.x = (std::clamp)(g_menuPos2.x, 0.0f, (std::max)(0.0f, io.DisplaySize.x - kMenuW));
    g_menuPos2.y = (std::clamp)(g_menuPos2.y, 0.0f, (std::max)(0.0f, io.DisplaySize.y - kMenuH));

    ImGui::SetNextWindowPos({ g_menuPos2.x, g_menuPos2.y }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ kMenuW, kMenuH });
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    ImGui::Begin("##BodycamMod", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
        (g_MenuOpen ? 0 : ImGuiWindowFlags_NoInputs));
    {
        using kit::C;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        const int bgA = static_cast<int>(kit::theme.menuAlpha * 255);
        {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            const ImVec2 c(o.x + kMenuW * 0.5f, o.y + kMenuH * 0.5f + drop);
            const ImVec2 hs(kMenuW * 0.5f * scale, kMenuH * 0.5f * scale);
            for (int i = 1; i <= 8; ++i) {
                const float g = i * 3.0f;
                bg->AddRectFilled({ c.x - hs.x - g, c.y - hs.y - g + 6 }, { c.x + hs.x + g, c.y + hs.y + g + 6 },
                                  C(0, 0, 0, static_cast<int>((26 - i * 3) * eased)), 11.0f + g);
            }
        }
        dl->AddRectFilled(o, { o.x + kMenuW, o.y + kMenuH }, C(19, 19, 24, bgA), 11.0f);
        dl->AddRectFilled(o, { o.x + kSideW, o.y + kMenuH }, C(15, 15, 19, bgA), 11.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddLine({ o.x + kSideW, o.y }, { o.x + kSideW, o.y + kMenuH }, C(255, 255, 255, 9));
        dl->AddRect(o, { o.x + kMenuW, o.y + kMenuH }, C(255, 255, 255, 13), 11.0f);

        DrawSidebar(dl, o);
        DrawTopBar(dl, o);
        DrawContent(o);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    kit::render_panels();
    ImGui::PopStyleVar();

    if (scale != 1.0f || drop != 0.0f) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImVec2 c(g_menuPos2.x + kMenuW * 0.5f, g_menuPos2.y + kMenuH * 0.5f);
        auto belongs = [](ImGuiWindow* w) {
            const char* n = w->RootWindow ? w->RootWindow->Name : w->Name;
            return std::strncmp(n, "##BodycamMod", 12) == 0 || std::strncmp(n, "##panel_", 8) == 0 || std::strncmp(n, "##cfgpop", 8) == 0 || std::strncmp(n, "##searchpop", 11) == 0;
        };
        for (ImGuiWindow* w : g.Windows) {
            if (!w->Active || !belongs(w)) continue;
            ImDrawList* dl = w->DrawList;
            for (ImDrawVert& v : dl->VtxBuffer) {
                v.pos.x = c.x + (v.pos.x - c.x) * scale;
                v.pos.y = c.y + (v.pos.y - c.y) * scale + drop;
            }
            for (ImDrawCmd& cmd : dl->CmdBuffer) {
                cmd.ClipRect.x = c.x + (cmd.ClipRect.x - c.x) * scale;
                cmd.ClipRect.z = c.x + (cmd.ClipRect.z - c.x) * scale;
                cmd.ClipRect.y = c.y + (cmd.ClipRect.y - c.y) * scale + drop;
                cmd.ClipRect.w = c.y + (cmd.ClipRect.w - c.y) * scale + drop;
            }
        }
    }
}

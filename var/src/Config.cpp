#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../include/Config.hpp"
#include "../include/Misc.hpp"
#include "../include/ESP.hpp"
#include "../include/Aimbot.hpp"
#include "../include/Hud.hpp"
#include "../include/UiKit.hpp"
#include "../include/Targets.hpp"
#include "../include/GameSettings.hpp"
#include "../include/Log.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <variant>

#pragma comment(lib, "Shell32.lib")

extern ESPConfig g_ESP;
extern AimbotConfig g_Aim;

namespace fs = std::filesystem;

namespace {
    using Ptr = std::variant<bool*, int*, unsigned*, float*, ImVec4*>;
    struct Field { std::string key; Ptr ptr; };

    std::vector<Field> g_fields;
    std::string g_active, g_autoload, g_error, g_activeText;
    std::vector<ConfigInfo> g_list;
    double g_listTime = -10.0;
    bool g_listDirty = true;
    double g_dirtyCheck = -10.0;
    bool g_dirty = false;

    double Now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    void Register() {
        if (!g_fields.empty()) return;
        auto add = [](const char* k, Ptr p) { g_fields.push_back({ k, p }); };
#define E(f) add("esp." #f, &g_ESP.f)
#define A(f) add("aim." #f, &g_Aim.f)
#define H(f) add("hud." #f, &g_Hud.f)
#define T(f) add("theme." #f, &kit::theme.f)
#define M(f) add("misc." #f, &g_Misc.f)
        M(NoRecoil); M(NoSpread); M(RapidFire); M(FireRateMul); M(InfiniteAmmo);
        M(Speedhack); M(SpeedMul); M(FreezeBots); M(BotTimeScale);
        M(AutoFire); M(AutoFireCps); M(InstaReload); M(ReloadSpeedMul); M(DamageBoost); M(DamageMul); M(WeaponSpeed); M(WeaponSpeedMul);
        M(GodMode); M(Wallbang); M(NoDroneCooldown); M(Fly); M(Noclip); M(FlySpeed); M(FlyUpKey); M(FlyDownKey); M(FlyBoostKey); M(FlyResponse); M(Teleport); M(TeleportKey); M(TeleportDist); M(TeleportSide); M(AutoTeleport);
        M(Footsteps); M(FootstepRange); M(FootstepColor); M(HitMarker); M(HitMarkerSize); M(HitColor); M(KillColor); M(HitSound);
        M(Tracers); M(TracerSeconds); M(TracerColor); M(TracerStyle); M(TracerThickness); M(TracerImpact); M(KillSound); M(KillSoundType); M(SoundVolume);
        E(Enabled); E(ShowBoxes); E(BoxStyle); E(FilledBoxes); E(BoxThickness);
        E(ShowBones); E(BoneThickness); E(ShowHeadCircle); E(ShowViewDir); E(ViewDirLength);
        E(ShowNames); E(ShowTypeTag); E(ShowHealth); E(ShowHealthText); E(ShowDistance);
        E(ShowSnaplines); E(SnaplineOrigin); E(ShowOffscreen); E(OffscreenRadius); E(OffscreenSize); E(ShowEnemyCount);
        E(VisibilityColors); E(ShowTeammates); E(ShowDrones); E(MaxDistance); E(DistanceFade);
        E(BoxColor); E(HiddenColor); E(VisibleColor); E(DroneColor); E(TeammateColor); E(BoneColor);
        E(HealthPos); E(HealthWidth); E(HealthColorMode); E(HealthOutline); E(HealthBackground); E(HealthFullColor); E(HealthLowColor);
        E(NameCustomColor); E(NameColor); E(DistanceColor); E(ShowWeapon); E(WeaponMode); E(WeaponColor); E(WeaponReloading);
        E(SnapCustomColor); E(SnapColor); E(SnapThickness); E(OffscreenCustomColor); E(OffscreenColor);
        E(ViewDirCustomColor); E(ViewDirColor); E(ShowFriendlyDrones); E(DroneLabel); E(FriendlyDroneColor);
        E(ShowGrenades); E(GrenadeTimer); E(GrenadeRing); E(GrenadeRadius); E(GrenadeColor); E(GrenadeFriendlyColor);

        A(Enabled); A(KeyMode); A(AimKey); A(TargetBones); A(Priority); A(FOV); A(Smoothing); A(MaxDistance);
        A(OnlyVisible); A(TargetDrones); A(StickyTarget); A(SwitchDelayMs);
        A(Prediction); A(PredictionMs);
        A(Triggerbot); A(TriggerKey); A(TriggerDelayMs); A(TriggerVisibleOnly);
        A(DrawFOV); A(DrawTarget); A(FovColor); A(MarkerColor); A(MarkerStyle); A(MarkerSize); A(MarkerOutline); A(MarkerSpin);

        H(Watermark); H(WmBrand); H(WmUser); H(WmStatus); H(WmFps); H(WmTime);
        H(Ammo); H(AmmoBar); H(AmmoLowPct); H(Keybinds); H(KbOnlyActive); H(Stats); H(StatsVisible);
        H(TargetHud); H(TargetStyle); H(TargetName); H(TargetDist); H(TargetBar);
        H(Notifications); H(NotifySeconds); H(NotifyCustomColor); H(NotifyColor); H(Music); H(MusicButtons); H(MusicBars);
        H(AmmoWeaponIcon); H(Kills); H(KillsDeaths); H(KillsKD); H(Health); H(HealthNumber); H(HealthDamageTrail);
        H(CombatLog); H(ClKills); H(ClHits); H(ClMisses); H(ClSeconds); H(ClMaxLines); H(ClKillColor); H(ClHitColor); H(ClMissColor);
        for (int i = 0; i < Hud_Count; ++i) {
            char k[32];
            std::snprintf(k, sizeof(k), "hud.PosX%d", i); add(k, &g_Hud.PosX[i]);
            std::snprintf(k, sizeof(k), "hud.PosY%d", i); add(k, &g_Hud.PosY[i]);
            std::snprintf(k, sizeof(k), "hud.CustomColor%d", i); add(k, &g_Hud.CustomColor[i]);
            std::snprintf(k, sizeof(k), "hud.Color%d", i); add(k, &g_Hud.Color[i]);
        }

        T(accent); T(menuAlpha); T(hudAlpha); T(notifyOnToggle); T(mode);
#undef E
#undef A
#undef H
#undef T
    }

    fs::path Root() {
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        fs::path base = (n && n < MAX_PATH) ? fs::path(buf) : fs::current_path();
        return base / L"BodycamMod";
    }
    fs::path Dir() { return Root() / L"configs"; }
    fs::path PathFor(const std::string& name) { return Dir() / fs::path(name + ".cfg"); }
    std::string ToUtf8(const fs::path& p) { const std::u8string u = p.u8string(); return std::string(u.begin(), u.end()); }
    fs::path AutoloadFile() { return Root() / L"autoload.txt"; }
    fs::path SessionFile() { return Root() / L"last_session.txt"; }
    std::string g_sessionText;
    double g_sessionCheck = 0.0;
    bool g_sessionReady = false;

    std::string Serialize() {
        Register();
        std::ostringstream o;
        o << "# BodycamMod config\n";
        char buf[96];
        for (const Field& f : g_fields) {
            std::visit([&](auto* p) {
                using P = std::remove_pointer_t<decltype(p)>;
                if constexpr (std::is_same_v<P, bool>) std::snprintf(buf, sizeof(buf), "%d", *p ? 1 : 0);
                else if constexpr (std::is_same_v<P, int>) std::snprintf(buf, sizeof(buf), "%d", *p);
                else if constexpr (std::is_same_v<P, unsigned>) std::snprintf(buf, sizeof(buf), "%u", *p);
                else if constexpr (std::is_same_v<P, float>) std::snprintf(buf, sizeof(buf), "%.4f", *p);
                else std::snprintf(buf, sizeof(buf), "%.4f %.4f %.4f %.4f", p->x, p->y, p->z, p->w);
            }, f.ptr);
            o << f.key << " = " << buf << "\n";
        }
        return o.str();
    }

    std::string Trim(std::string s) {
        const auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        const auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    void Apply(const std::string& text) {
        Register();
        std::unordered_map<std::string, const Field*> byKey;
        for (const Field& f : g_fields) byKey[f.key] = &f;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto it = byKey.find(Trim(line.substr(0, eq)));
            if (it == byKey.end()) continue;
            const std::string v = Trim(line.substr(eq + 1));
            const char* s = v.c_str();
            std::visit([&](auto* p) {
                using P = std::remove_pointer_t<decltype(p)>;
                if constexpr (std::is_same_v<P, bool>) *p = std::atoi(s) != 0;
                else if constexpr (std::is_same_v<P, int>) *p = std::atoi(s);
                else if constexpr (std::is_same_v<P, unsigned>) *p = static_cast<unsigned>(std::strtoul(s, nullptr, 10));
                else if constexpr (std::is_same_v<P, float>) { const float x = std::strtof(s, nullptr); if (std::isfinite(x)) *p = x; }
                else {
                    ImVec4 c = *p;
                    if (std::sscanf(s, "%f %f %f %f", &c.x, &c.y, &c.z, &c.w) == 4) *p = c;
                }
            }, it->second->ptr);
        }
        g_ESP.BoxStyle = std::clamp(g_ESP.BoxStyle, 0, BoxStyle_Count - 1);
        g_ESP.SnaplineOrigin = std::clamp(g_ESP.SnaplineOrigin, 0, SnapOrigin_Count - 1);
        g_Aim.KeyMode = std::clamp(g_Aim.KeyMode, 0, AimKeyMode_Count - 1);
        g_Aim.Priority = std::clamp(g_Aim.Priority, 0, AimPriority_Count - 1);
        g_Aim.TargetBones &= (1u << AimBone_Count) - 1;
        if (!g_Aim.TargetBones) g_Aim.TargetBones = 1u << AimBone_Chest;
        g_Aim.AimKey = std::clamp(g_Aim.AimKey, 0, 254);
        g_Aim.TriggerKey = std::clamp(g_Aim.TriggerKey, 0, 254);
        g_Hud.TargetStyle = std::clamp(g_Hud.TargetStyle, 0, 1);
        g_Hud.AmmoLowPct = std::clamp(g_Hud.AmmoLowPct, 0.0f, 100.0f);
        g_Misc.HitMarkerSize = std::clamp(g_Misc.HitMarkerSize, 3.0f, 12.0f);
        g_Misc.TracerSeconds = std::clamp(g_Misc.TracerSeconds, 0.3f, 5.0f);
        g_Misc.TracerStyle = std::clamp(g_Misc.TracerStyle, 0, TracerStyle_Count - 1);
        g_Misc.TracerThickness = std::clamp(g_Misc.TracerThickness, 0.5f, 3.0f);
        g_ESP.HealthPos = std::clamp(g_ESP.HealthPos, 0, HealthPos_Count - 1);
        g_ESP.HealthColorMode = std::clamp(g_ESP.HealthColorMode, 0, HealthColor_Count - 1);
        g_ESP.HealthWidth = std::clamp(g_ESP.HealthWidth, 1.0f, 10.0f);
        g_ESP.WeaponMode = std::clamp(g_ESP.WeaponMode, 0, WeaponMode_Count - 1);
        g_ESP.GrenadeRadius = std::clamp(g_ESP.GrenadeRadius, 0.5f, 20.0f);
        g_Hud.ClMaxLines = std::clamp(g_Hud.ClMaxLines, 1, 12);
        g_Hud.ClSeconds = std::clamp(g_Hud.ClSeconds, 1.0f, 15.0f);
        kit::theme.menuAlpha = std::clamp(kit::theme.menuAlpha, 0.5f, 1.0f);
        kit::theme.hudAlpha = std::clamp(kit::theme.hudAlpha, 0.3f, 1.0f);
        kit::theme.mode = kit::theme.mode == 1 ? 1 : 0;
    }

    bool ReadFile(const fs::path& p, std::string& out) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool WriteFile(const fs::path& p, const std::string& text) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        const fs::path tmp = p.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f << text;
            if (!f) return false;
        }
        fs::rename(tmp, p, ec);
        if (ec) { fs::remove(tmp, ec); return false; }
        return true;
    }

    void SaveAutoload() { WriteFile(AutoloadFile(), g_autoload + "\n"); }
}

std::string Config_Sanitize(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') out += static_cast<char>(c);
        if (out.size() >= 32) break;
    }
    out = Trim(out);
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

void Config_Init() {
    Register();
    std::error_code ec;
    fs::create_directories(Dir(), ec);
    std::string a;
    if (ReadFile(AutoloadFile(), a)) g_autoload = Config_Sanitize(Trim(a));
    if (!g_autoload.empty() && Config_Exists(g_autoload)) Config_Load(g_autoload);
    std::string sess;
    if (ReadFile(SessionFile(), sess) && !sess.empty()) {
        if (sess.rfind("#active=", 0) == 0) {
            const size_t nl = sess.find('\n');
            const std::string act = Config_Sanitize(Trim(sess.substr(8, nl == std::string::npos ? std::string::npos : nl - 8)));
            if (!act.empty() && act != g_active && Config_Exists(act)) Config_Load(act);
        }
        Apply(sess);
        g_dirty = false; g_dirtyCheck = -10.0;
        Log_Add(Log_Info, "Restored settings from last session");
    }
    g_sessionText = Serialize();
    g_sessionReady = true;
}

void Config_SaveSession(bool force) {
    if (!g_sessionReady) return;
    const double now = Now();
    if (!force && now - g_sessionCheck < 1.0) return;
    g_sessionCheck = now;
    const std::string text = Serialize();
    if (!force && text == g_sessionText) return;
    if (WriteFile(SessionFile(), "#active=" + g_active + "\n" + text)) g_sessionText = text;
}

std::vector<ConfigInfo> Config_List() {
    const double now = Now();
    if (!g_listDirty && now - g_listTime < 1.0) return g_list;
    g_listDirty = false;
    g_listTime = now;
    g_list.clear();
    std::error_code ec;
    struct Row { ConfigInfo info; fs::file_time_type t; };
    std::vector<Row> rows;
    const auto clockNow = fs::file_time_type::clock::now();
    for (const auto& e : fs::directory_iterator(Dir(), ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".cfg") continue;
        Row r;
        r.info.name = ToUtf8(e.path().stem());
        r.t = e.last_write_time(ec);
        r.info.ageSeconds = std::chrono::duration<double>(clockNow - r.t).count();
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.t > b.t; });
    for (const Row& r : rows) g_list.push_back(r.info);
    return g_list;
}

bool Config_Exists(const std::string& name) {
    std::error_code ec;
    return !name.empty() && fs::exists(PathFor(name), ec);
}

bool Config_Save(const std::string& raw) {
    const std::string name = Config_Sanitize(raw);
    if (name.empty()) { g_error = "Enter a config name"; return false; }
    const std::string text = Serialize();
    if (!WriteFile(PathFor(name), text)) { g_error = "Could not write " + name + ".cfg"; Log_Add(Log_Error, "%s", g_error.c_str()); return false; }
    Log_Add(Log_Info, "Config \"%s\" saved", name.c_str());
    g_active = name;
    g_activeText = text;
    g_listDirty = true;
    g_dirty = false; g_dirtyCheck = -10.0;
    return true;
}

bool Config_Load(const std::string& name) {
    std::string text;
    if (!ReadFile(PathFor(name), text)) { g_error = "Could not read " + name + ".cfg"; Log_Add(Log_Error, "%s", g_error.c_str()); return false; }
    Log_Add(Log_Info, "Config \"%s\" loaded", name.c_str());
    Apply(text);
    g_active = name;
    g_activeText = Serialize();
    g_dirty = false; g_dirtyCheck = -10.0;
    return true;
}

bool Config_Delete(const std::string& name) {
    std::error_code ec;
    if (!fs::remove(PathFor(name), ec)) { g_error = "Could not delete " + name; return false; }
    if (g_active == name) { g_active.clear(); g_activeText.clear(); }
    if (g_autoload == name) { g_autoload.clear(); SaveAutoload(); }
    g_listDirty = true;
    return true;
}

bool Config_Rename(const std::string& from, const std::string& raw) {
    const std::string to = Config_Sanitize(raw);
    if (to.empty()) { g_error = "Enter a new name"; return false; }
    if (to == from) return true;
    if (Config_Exists(to)) { g_error = "\"" + to + "\" already exists"; return false; }
    std::error_code ec;
    fs::rename(PathFor(from), PathFor(to), ec);
    if (ec) { g_error = "Could not rename " + from; return false; }
    if (g_active == from) g_active = to;
    if (g_autoload == from) { g_autoload = to; SaveAutoload(); }
    g_listDirty = true;
    return true;
}

void Config_ResetDefaults() {
    g_ESP = ESPConfig{};
    g_Aim = AimbotConfig{};
    const HudConfig keep = g_Hud;
    g_Hud = HudConfig{};
    for (int i = 0; i < Hud_Count; ++i) { g_Hud.PosX[i] = keep.PosX[i]; g_Hud.PosY[i] = keep.PosY[i]; }
    kit::theme = kit::Theme{};
}

const std::string& Config_Active() { return g_active; }

bool Config_Dirty() {
    if (g_active.empty()) return false;
    const double now = Now();
    if (now - g_dirtyCheck > 0.25) { g_dirtyCheck = now; g_dirty = Serialize() != g_activeText; }
    return g_dirty;
}

const std::string& Config_Autoload() { return g_autoload; }
void Config_SetAutoload(const std::string& name) { g_autoload = name; SaveAutoload(); }

void Config_OpenFolder() {
    std::error_code ec;
    fs::create_directories(Dir(), ec);
    ShellExecuteW(nullptr, L"open", Dir().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string Config_LastError() { return g_error; }

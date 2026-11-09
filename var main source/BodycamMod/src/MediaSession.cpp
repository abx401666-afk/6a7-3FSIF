#include "../include/MediaSession.hpp"
#include "../include/Log.hpp"
#include <Windows.h>
#include <roapi.h>
#include <winstring.h>
#include <shcore.h>
#include <wincodec.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
    const GUID IID_ManagerStatics = { 0x2050c4ee, 0x11a0, 0x57de, { 0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45 } };
    const GUID IID_AsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    const GUID IID_IStreamLocal = { 0x0000000c, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

    enum {
        kStatics_RequestAsync = 6,
        kMgr_GetCurrentSession = 6,
        kSess_SourceAppUserModelId = 6, kSess_TryGetMediaPropertiesAsync = 7, kSess_GetTimelineProperties = 8,
        kSess_GetPlaybackInfo = 9, kSess_TrySkipNextAsync = 16, kSess_TrySkipPreviousAsync = 17, kSess_TryTogglePlayPauseAsync = 20,
        kProps_Title = 6, kProps_Subtitle = 7, kProps_AlbumArtist = 8, kProps_Artist = 9, kProps_AlbumTitle = 10, kProps_Thumbnail = 15,
        kTime_StartTime = 6, kTime_EndTime = 7, kTime_Position = 10, kTime_LastUpdatedTime = 11,
        kInfo_PlaybackStatus = 7,
        kAsyncOp_GetResults = 8,
        kAsyncInfo_Status = 7,
        kStreamRef_OpenReadAsync = 6,
    };

    template <class... A>
    HRESULT Call(void* obj, int slot, A... a) {
        using F = HRESULT(STDMETHODCALLTYPE*)(void*, A...);
        return reinterpret_cast<F>((*reinterpret_cast<void***>(obj))[slot])(obj, a...);
    }
    void Release(void*& p) { if (p) { static_cast<IUnknown*>(p)->Release(); p = nullptr; } }

    std::string FromHString(HSTRING h) {
        std::string out;
        if (!h) return out;
        UINT32 n = 0;
        const wchar_t* w = WindowsGetStringRawBuffer(h, &n);
        if (w && n) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
            out.resize(static_cast<size_t>(len));
            WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), out.data(), len, nullptr, nullptr);
        }
        WindowsDeleteString(h);
        return out;
    }

    void* Await(void* op, DWORD timeoutMs = 3000) {
        if (!op) return nullptr;
        void* info = nullptr;
        void* result = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(op)->QueryInterface(IID_AsyncInfo, &info)) && info) {
            const ULONGLONG start = GetTickCount64();
            int status = 0;
            while (SUCCEEDED(Call(info, kAsyncInfo_Status, &status)) && status == 0 && GetTickCount64() - start < timeoutMs) Sleep(5);
            if (status == 1) Call(op, kAsyncOp_GetResults, &result);
            Release(info);
        }
        Release(op);
        return result;
    }

    void WaitOnly(void* op, DWORD timeoutMs) {
        void* info = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(op)->QueryInterface(IID_AsyncInfo, &info)) && info) {
            const ULONGLONG start = GetTickCount64();
            int status = 0;
            while (SUCCEEDED(Call(info, kAsyncInfo_Status, &status)) && status == 0 && GetTickCount64() - start < timeoutMs) Sleep(5);
            Release(info);
        }
        Release(op);
    }

    std::string SourceName(const std::string& aumid) {
        std::string l = aumid;
        for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        struct M { const char* key; const char* name; };
        static const M kMap[] = { { "spotify", "Spotify" }, { "chrome", "Chrome" }, { "msedge", "Edge" }, { "firefox", "Firefox" },
                                  { "brave", "Brave" }, { "opera", "Opera" }, { "zunemusic", "Media Player" }, { "vlc", "VLC" },
                                  { "applemusic", "Apple Music" }, { "itunes", "iTunes" }, { "tidal", "TIDAL" }, { "deezer", "Deezer" },
                                  { "soundcloud", "SoundCloud" }, { "discord", "Discord" }, { "foobar", "foobar2000" }, { "winamp", "Winamp" },
                                  { "wmplayer", "Media Player" }, { "mediaplayer", "Media Player" }, { "zunevideo", "Films & TV" },
                                  { "windows.photos", "Photos" }, { "mpc-hc", "MPC-HC" }, { "mpc-be", "MPC-BE" }, { "potplayer", "PotPlayer" },
                                  { "musicbee", "MusicBee" }, { "aimp", "AIMP" }, { "youtube", "YouTube" }, { "amazonmusic", "Amazon Music" } };
        for (const M& m : kMap) if (l.find(m.key) != std::string::npos) return m.name;
        std::string s = aumid;
        const size_t bang = s.find('!');
        if (bang != std::string::npos) s = s.substr(0, bang);
        const size_t slash = s.find_last_of("\\/");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        if (s.size() > 4 && (s.compare(s.size() - 4, 4, ".exe") == 0 || s.compare(s.size() - 4, 4, ".EXE") == 0)) s.resize(s.size() - 4);
        const size_t us = s.find('_');
        if (us != std::string::npos) s.resize(us);
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s = s.substr(dot + 1);
        if (s.empty() || s.size() > 20 || s.find_first_of("{}-") != std::string::npos) return {};
        for (const char c : s) if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ') return {};
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        return s;
    }

    std::mutex           g_lock;
    MediaInfo            g_info;
    std::vector<uint8_t> g_art;
    double               g_posAt = 0.0;
    std::atomic<bool>    g_started{ false }, g_run{ false }, g_failed{ false };
    std::atomic<int>     g_cmd{ Media_None };

    double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    bool DecodeArt(void* streamRef, std::vector<uint8_t>& out) {
        out.clear();
        void* op = nullptr;
        if (FAILED(Call(streamRef, kStreamRef_OpenReadAsync, &op))) return false;
        void* ras = Await(op);
        if (!ras) return false;
        IStream* stream = nullptr;
        HRESULT hr = CreateStreamOverRandomAccessStream(static_cast<IUnknown*>(ras), IID_IStreamLocal, reinterpret_cast<void**>(&stream));
        Release(ras);
        if (FAILED(hr) || !stream) return false;
        IWICImagingFactory* wic = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICFormatConverter* conv = nullptr;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&wic))) &&
            SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &frame))) {
            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);
            if (w && h && SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
                SUCCEEDED(scaler->Initialize(frame, 128, 128, WICBitmapInterpolationModeFant)) &&
                SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                out.resize(128 * 128 * 4);
                ok = SUCCEEDED(conv->CopyPixels(nullptr, 128 * 4, static_cast<UINT>(out.size()), out.data()));
                if (!ok) out.clear();
            }
        }
        if (conv) conv->Release();
        if (scaler) scaler->Release();
        if (frame) frame->Release();
        if (dec) dec->Release();
        if (wic) wic->Release();
        stream->Release();
        return ok;
    }

    void Worker() {
        if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) { g_failed = true; return; }
        void* statics = nullptr;
        void* manager = nullptr;
        {
            static const wchar_t kClass[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
            HSTRING_HEADER hdr; HSTRING cls = nullptr;
            if (SUCCEEDED(WindowsCreateStringReference(kClass, static_cast<UINT32>(wcslen(kClass)), &hdr, &cls)))
                RoGetActivationFactory(cls, IID_ManagerStatics, &statics);
            void* op = nullptr;
            if (statics && SUCCEEDED(Call(statics, kStatics_RequestAsync, &op))) manager = Await(op, 8000);
            Release(statics);
        }
        if (!manager) {
            Log_Add(Log_Error, "Music: Windows media controls unavailable");
            g_failed = true;
            RoUninitialize();
            return;
        }
        Log_Add(Log_Info, "Music: reading Windows media controls");
        std::string artKey;
        while (g_run) {
            void* session = nullptr;
            Call(manager, kMgr_GetCurrentSession, &session);
            MediaInfo info;
            std::vector<uint8_t> art;
            bool artChanged = false;
            if (session) {
                const int cmd = g_cmd.exchange(Media_None);
                if (cmd != Media_None) {
                    void* op = nullptr;
                    const int slot = cmd == Media_Next ? kSess_TrySkipNextAsync : cmd == Media_Previous ? kSess_TrySkipPreviousAsync : kSess_TryTogglePlayPauseAsync;
                    if (SUCCEEDED(Call(session, slot, &op)) && op) WaitOnly(op, 1500);
                }
                info.available = true;
                HSTRING h = nullptr;
                if (SUCCEEDED(Call(session, kSess_SourceAppUserModelId, &h))) info.source = SourceName(FromHString(h));
                void* pinfo = nullptr;
                if (SUCCEEDED(Call(session, kSess_GetPlaybackInfo, &pinfo)) && pinfo) {
                    int st = 0;
                    if (SUCCEEDED(Call(pinfo, kInfo_PlaybackStatus, &st))) info.playing = st == 4;
                    Release(pinfo);
                }
                void* tl = nullptr;
                if (SUCCEEDED(Call(session, kSess_GetTimelineProperties, &tl)) && tl) {
                    int64_t start = 0, end = 0, pos = 0, upd = 0;
                    Call(tl, kTime_StartTime, &start);
                    Call(tl, kTime_EndTime, &end);
                    Call(tl, kTime_Position, &pos);
                    Call(tl, kTime_LastUpdatedTime, &upd);
                    Release(tl);
                    info.duration = end > start ? (end - start) / 1.0e7 : 0.0;
                    double p = (pos - start) / 1.0e7;
                    if (info.playing && upd > 0) {
                        FILETIME ft; GetSystemTimeAsFileTime(&ft);
                        const int64_t nowFt = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
                        const double since = (nowFt - upd) / 1.0e7;
                        if (since > 0.0 && since < 24 * 3600.0) p += since;
                    }
                    if (info.duration > 0.0) p = p < 0.0 ? 0.0 : p > info.duration ? info.duration : p;
                    info.position = p < 0.0 ? 0.0 : p;
                }
                void* op = nullptr;
                if (SUCCEEDED(Call(session, kSess_TryGetMediaPropertiesAsync, &op))) {
                    if (void* props = Await(op)) {
                        HSTRING s = nullptr;
                        if (SUCCEEDED(Call(props, kProps_Title, &s))) info.title = FromHString(s);
                        s = nullptr; if (SUCCEEDED(Call(props, kProps_Artist, &s))) info.artist = FromHString(s);
                        if (info.artist.empty()) { s = nullptr; if (SUCCEEDED(Call(props, kProps_AlbumArtist, &s))) info.artist = FromHString(s); }
                        if (info.artist.empty()) { s = nullptr; if (SUCCEEDED(Call(props, kProps_Subtitle, &s))) info.artist = FromHString(s); }
                        s = nullptr; if (SUCCEEDED(Call(props, kProps_AlbumTitle, &s))) info.album = FromHString(s);
                        const std::string key = info.source + "|" + info.title + "|" + info.artist;
                        if (key != artKey) {
                            void* thumb = nullptr;
                            if (SUCCEEDED(Call(props, kProps_Thumbnail, &thumb)) && thumb) { DecodeArt(thumb, art); Release(thumb); }
                            if (!art.empty() || info.title.empty()) artKey = key;
                            artChanged = true;
                        }
                        Release(props);
                    }
                }
                Release(session);
            } else {
                g_cmd = Media_None;
                if (!artKey.empty()) { artKey.clear(); artChanged = true; }
            }
            {
                std::lock_guard<std::mutex> l(g_lock);
                const uint32_t ver = g_info.artVersion + (artChanged ? 1u : 0u);
                g_info = info;
                g_info.artVersion = ver;
                g_posAt = Now();
                if (artChanged) g_art = std::move(art);
            }
            for (int i = 0; i < 25 && g_run && g_cmd == Media_None; ++i) Sleep(20);
        }
        Release(manager);
        RoUninitialize();
    }

    void Start() {
        if (g_started.exchange(true)) return;
        g_run = true;
        std::thread(Worker).detach();
    }
}

bool Media_Get(MediaInfo& out) {
    Start();
    if (g_failed) return false;
    std::lock_guard<std::mutex> l(g_lock);
    out = g_info;
    if (out.playing) {
        out.position += Now() - g_posAt;
        if (out.duration > 0.0 && out.position > out.duration) out.position = out.duration;
    }
    return true;
}

uint32_t Media_GetArt(std::vector<uint8_t>& rgba) {
    std::lock_guard<std::mutex> l(g_lock);
    rgba = g_art;
    return g_info.artVersion;
}

void Media_Send(MediaCommand cmd) { g_cmd = cmd; }


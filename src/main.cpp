// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#include "video.h"
#include "audio.h"
#include "nvidia_profile.h"
#include <shellapi.h>
#include <cmath>

namespace {
constexpr UINT CmdPlay = 100, CmdFullscreen = 101, CmdMute = 102, CmdReconnect = 103,
    CmdMonitor = 104, CmdExit = 105, CmdHelp = 106, CmdVideo = 1000, CmdAudio = 2000,
    CmdOutput = 3000, CmdFormat = 4000, CmdDelay = 8000, CmdScaling = 9000,
    CmdNative60 = 110, CmdSharp50 = 111, CmdQuality30 = 112, CmdStats = 113, CmdRendererInfo = 114, CmdFramegen = 115;
constexpr int Delays[] = {0, 20, 40, 80, 120, 200};
constexpr const wchar_t* ScalingNames[] = {L"Original", L"Lanczos sharp", L"NVIDIA RTX VSR (requires driver setting)", L"GPU standard"};

struct Settings {
    std::wstring videoId, audioId, outputId, mode;
    int volume = 80, delay = 0, scaling = 0;
    bool mute = false;
    std::filesystem::path path = DataDirectory() / L"settings.ini";
    void Load() {
        auto read = [&](const wchar_t* key) {
            wchar_t value[4096]{}; GetPrivateProfileStringW(L"Viewer", key, L"", value, 4096, path.c_str());
            return std::wstring(value);
        };
        videoId = read(L"Video"); audioId = read(L"Audio"); outputId = read(L"Output"); mode = read(L"Format");
        volume = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Viewer", L"Volume", 80, path.c_str())), 0, 100);
        delay = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Viewer", L"AudioDelayMs", 0, path.c_str())), 0, 200);
        mute = GetPrivateProfileIntW(L"Viewer", L"Mute", 0, path.c_str()) != 0;
        scaling = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Viewer", L"Scaling", 0, path.c_str())), 0, 3);
    }
    void Save() {
        auto write = [&](const wchar_t* key, const std::wstring& value) {
            if (!WritePrivateProfileStringW(L"Viewer", key, value.c_str(), path.c_str())) Log(L"Could not save settings.");
        };
        write(L"Video", videoId); write(L"Audio", audioId); write(L"Output", outputId); write(L"Format", mode);
        write(L"Volume", std::to_wstring(volume)); write(L"AudioDelayMs", std::to_wstring(delay));
        write(L"Mute", mute ? L"1" : L"0");
        write(L"Scaling", std::to_wstring(scaling));
    }
};

struct Options {
    bool windowed = false, probe = false, noAudio = false, exclusive = false, checkRuntime = false;
    int smokeSeconds = 0;
    int scaling = -1;
    std::wstring preset;
    int setFramegen = -1;
    std::filesystem::path output = DataDirectory() / L"diagnostics";
};

int FindId(const std::vector<Device>& devices, const std::wstring& id) {
    for (int i = 0; i < static_cast<int>(devices.size()); ++i) if (devices[i].id == id) return i;
    return -1;
}
std::wstring Preferred(const std::vector<Device>& devices, const std::wstring& text) {
    for (const auto& d : devices) if (d.name.find(text) != std::wstring::npos) return d.id;
    return {};
}
std::wstring MenuText(std::wstring label) {
    size_t pos = 0; while ((pos = label.find(L'&', pos)) != std::wstring::npos) { label.insert(pos, 1, L'&'); pos += 2; }
    return label;
}
std::vector<HMONITOR> Monitors() {
    std::vector<HMONITOR> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor); return TRUE;
    }, reinterpret_cast<LPARAM>(&result)); return result;
}

class App {
public:
    HWND window = nullptr, hud = nullptr;
    HINSTANCE instance;
    Settings settings;
    Options options;
    VideoPreview video;
    AudioMonitor audio;
    std::vector<Device> cameras, inputs, outputs;
    std::vector<VideoMode> modes;
    std::wstring status = L"Opening capture card...", audioWarning;
    bool fullscreen = false, cursorHidden = false, paused = false, closing = false;
    bool smokeEntered = false, smokeExited = false, smokeRestarted = false, smokeCompared = false;
    bool rendererStats = false, menuOpen = false;
    bool smoothMotion = false;
    Handle profileHelper;
    bool pendingSmoothMotion = false;
    long framesBeforeRestart = 0;
    int result = 0;
    RECT savedBounds{};
    ULONGLONG hudUntil = 0, started = 0;
    POINT lastMouse{-1, -1};
    HFONT font = nullptr;

    App(HINSTANCE value, Options args) : instance(value), options(std::move(args)) {
        settings.Load();
        if (options.scaling >= 0) settings.scaling = options.scaling;
        if (!options.preset.empty()) Preset(options.preset);
        try { smoothMotion = SmoothMotionEnabled(); } catch (const Failure& e) { Log(e.message); }
    }
    ~App() { if (font) DeleteObject(font); }

    void Refresh() {
        cameras = VideoDevices(); inputs = AudioDevices(eCapture); outputs = AudioDevices(eRender);
        // Only the known HDMI card is auto-selected. Never fall back to a webcam or microphone.
        if (settings.videoId.empty()) settings.videoId = Preferred(cameras, L"USB3.0 Video");
        if (settings.audioId.empty()) settings.audioId = Preferred(inputs, L"USB3.0 Audio");
    }
    void Stop() {
        audio.Stop(); video.Stop(); SetThreadExecutionState(ES_CONTINUOUS);
    }
    void Start(bool refresh = true) {
        Stop(); paused = false; audioWarning.clear(); modes.clear();
        status = L"Opening capture card..."; InvalidateRect(window, nullptr, TRUE);
        try {
            if (refresh) Refresh();
            if (FindId(cameras, settings.videoId) < 0)
                throw Failure{L"Select your capture card from the Video device menu.\nReconnect a missing card and press R.", E_FAIL};
            modes = VideoModes(settings.videoId);
            if (modes.empty()) throw Failure{L"The capture device did not report any usable video formats.", E_FAIL};
            auto selected = std::find_if(modes.begin(), modes.end(), [&](const auto& m) { return m.label == settings.mode; });
            if (selected == modes.end()) selected = std::find_if(modes.begin(), modes.end(), [](const auto& m) {
                return m.width == 2560 && m.height == 1440 && m.label.find(L"MJPEG") != std::wstring::npos;
            });
            if (selected == modes.end()) selected = std::find_if(modes.begin(), modes.end(), [](const auto& m) {
                return m.width == 1920 && m.height == 1080;
            });
            if (selected == modes.end()) selected = modes.begin();
            settings.mode = selected->label;
            video.Start(window, settings.videoId, *selected, settings.scaling, options.exclusive || smoothMotion);
            video.ShowStats(rendererStats);
            status = settings.mode;
            if (!options.noAudio && settings.audioId != L"none") {
                if (FindId(inputs, settings.audioId) < 0)
                    audioWarning = L"Select USB3.0 Audio from the Audio input menu.";
                else {
                    audio.SetVolume(settings.volume, settings.mute);
                    audio.Start(settings.audioId, settings.outputId, settings.delay);
                }
            }
            SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
        } catch (const Failure& error) { Stop(); status = error.message; Log(status); }
        UpdateMenu(); UpdateTitle(); ShowHud(); InvalidateRect(window, nullptr, TRUE);
    }
    HMENU MakeMenu() {
        HMENU root = CreatePopupMenu();
        AppendMenuW(root, MF_STRING, CmdPlay, video.Running() ? L"Stop and release capture card\tSpace" : L"Start capture\tSpace");
        AppendMenuW(root, MF_STRING, CmdReconnect, L"Reconnect / refresh devices\tR");
        AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root, MF_STRING | (fullscreen ? MF_CHECKED : 0), CmdFullscreen, L"Fullscreen\tF11");
        AppendMenuW(root, MF_STRING, CmdMonitor, L"Move to next monitor\tTab");
        AppendMenuW(root, MF_STRING | (settings.mute ? MF_CHECKED : 0), CmdMute, L"Mute\tM");
        HMENU presets = CreatePopupMenu();
        AppendMenuW(presets, MF_STRING, CmdNative60, L"720p / 60 fps + upscaling");
        AppendMenuW(presets, MF_STRING, CmdSharp50, L"1080p / 50 fps + upscaling");
        AppendMenuW(presets, MF_STRING, CmdQuality30, L"1440p / 30 fps");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(presets), L"Capture presets");
        HMENU scaling = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(std::size(ScalingNames)); ++i)
            AppendMenuW(scaling, MF_STRING | (settings.scaling == i ? MF_CHECKED : 0), CmdScaling+i, ScalingNames[i]);
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(scaling), L"Upscaling");
        AppendMenuW(root, MF_STRING | (smoothMotion ? MF_CHECKED : 0) | (profileHelper.value ? MF_GRAYED : 0),
            CmdFramegen, L"NVIDIA Smooth Motion (experimental; restart required)");
        AppendMenuW(root, MF_STRING | (rendererStats ? MF_CHECKED : 0) | (video.Enhanced() ? 0 : MF_GRAYED), CmdStats, L"Renderer performance overlay\tS");
        AppendMenuW(root, MF_STRING, CmdRendererInfo, L"Renderer details");
        HMENU videos = CreatePopupMenu(), audios = CreatePopupMenu(), sinks = CreatePopupMenu(), formats = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(cameras.size()) && i < 900; ++i)
            AppendMenuW(videos, MF_STRING | (settings.videoId == cameras[i].id ? MF_CHECKED : 0), CmdVideo + i, MenuText(cameras[i].name).c_str());
        if (cameras.empty()) AppendMenuW(videos, MF_GRAYED, 0, L"No video capture devices found");
        AppendMenuW(audios, MF_STRING | (settings.audioId == L"none" ? MF_CHECKED : 0), CmdAudio, L"Off");
        for (int i = 0; i < static_cast<int>(inputs.size()) && i < 899; ++i)
            AppendMenuW(audios, MF_STRING | (settings.audioId == inputs[i].id ? MF_CHECKED : 0), CmdAudio + 1 + i, MenuText(inputs[i].name).c_str());
        AppendMenuW(sinks, MF_STRING | (settings.outputId.empty() ? MF_CHECKED : 0), CmdOutput, L"Windows default (press R after changing it)");
        for (int i = 0; i < static_cast<int>(outputs.size()) && i < 899; ++i)
            AppendMenuW(sinks, MF_STRING | (settings.outputId == outputs[i].id ? MF_CHECKED : 0), CmdOutput + 1 + i, MenuText(outputs[i].name).c_str());
        for (int i = 0; i < static_cast<int>(modes.size()) && i < 3900; ++i)
            AppendMenuW(formats, MF_STRING | (settings.mode == modes[i].label ? MF_CHECKED : 0), CmdFormat + i, modes[i].label.c_str());
        if (modes.empty()) AppendMenuW(formats, MF_GRAYED, 0, L"Start a video device to load formats");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(videos), L"Video device");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(formats), L"Video format");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(audios), L"Audio input");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(sinks), L"Sound output");
        HMENU delays = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(std::size(Delays)); ++i) {
            auto label = std::to_wstring(Delays[i]) + L" ms" + (i == 0 ? L" (minimum delay)" : L"");
            AppendMenuW(delays, MF_STRING | (settings.delay == Delays[i] ? MF_CHECKED : 0), CmdDelay + i, label.c_str());
        }
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(delays), L"Additional audio delay");
        AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root, MF_STRING, CmdHelp, L"Controls and troubleshooting\tF1");
        AppendMenuW(root, MF_STRING, CmdExit, L"Exit\tAlt+F4");
        return root;
    }
    void UpdateMenu() {
        HMENU previous = GetMenu(window);
        HMENU next = nullptr;
        if (!fullscreen) { next = CreateMenu(); AppendMenuW(next, MF_POPUP, reinterpret_cast<UINT_PTR>(MakeMenu()), L"Viewer"); }
        SetMenu(window, next); if (previous) DestroyMenu(previous); DrawMenuBar(window);
    }
    void ContextMenu(POINT p) {
        cursorHidden = false; SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        HMENU menu = MakeMenu();
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
        DestroyMenu(menu); if (command) Command(command); ShowHud();
    }
    void Command(UINT command) {
        if (command == CmdFullscreen) ToggleFullscreen();
        else if (command == CmdMonitor) NextMonitor();
        else if (command == CmdNative60) { Preset(L"720p60"); Start(); }
        else if (command == CmdSharp50) { Preset(L"1080p50"); Start(); }
        else if (command == CmdQuality30) { Preset(L"1440p30"); Start(); }
        else if (command == CmdStats) { rendererStats = !rendererStats; video.ShowStats(rendererStats); UpdateMenu(); }
        else if (command == CmdRendererInfo) MessageBoxW(window, video.RendererInfo().c_str(), L"SwitchView renderer", MB_OK);
        else if (command == CmdFramegen) {
            if (profileHelper.value) return;
            pendingSmoothMotion = !smoothMotion;
            try { SetSmoothMotion(pendingSmoothMotion); ProfileUpdated(); }
            catch (const Failure& e) {
                if (e.code != E_ACCESSDENIED) throw;
                wchar_t executable[32768]{};
                GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
                auto report = DataDirectory() / L"diagnostics" / L"profile";
                std::wstring arguments = L"--set-framegen " + std::wstring(pendingSmoothMotion ? L"on" : L"off") +
                    L" --output \"" + report.wstring() + L"\"";
                SHELLEXECUTEINFOW launch{sizeof(launch)};
                launch.fMask = SEE_MASK_NOCLOSEPROCESS; launch.hwnd = window;
                launch.lpVerb = L"runas"; launch.lpFile = executable;
                launch.lpParameters = arguments.c_str(); launch.nShow = SW_HIDE;
                if (!ShellExecuteExW(&launch)) {
                    if (GetLastError() == ERROR_CANCELLED) return;
                    throw Failure{L"Windows could not start the Smooth Motion settings helper.", HRESULT_FROM_WIN32(GetLastError())};
                }
                profileHelper.value = launch.hProcess; UpdateMenu();
            }
        }
        else if (command == CmdMute) { settings.mute = !settings.mute; Volume(0); }
        else if (command == CmdPlay) {
            if (video.Running()) { Stop(); paused = true; status = L"Capture stopped. The card is available to OBS.\nPress Space to start again."; UpdateMenu(); UpdateTitle(); InvalidateRect(window, nullptr, TRUE); }
            else Start();
        } else if (command == CmdReconnect) Start();
        else if (command == CmdExit) PostMessageW(window, WM_CLOSE, 0, 0);
        else if (command == CmdHelp) MessageBoxW(window,
            L"F11 / F / double-click: fullscreen\nEsc: return to window\nTab: move to next monitor\nM: mute\nUp / Down / mouse wheel: volume\nSpace: stop/start and release the capture card\nR: reconnect devices\nRight-click: device, format, output, and audio delay\nAlt+F4: exit\n\n"
            L"Close OBS or deactivate its capture source before starting.\n1440p is limited to 30 fps on this card; try 1080p/50 or 720p/60 for smoother motion.\n"
            L"Upscaling: Original, Lanczos sharp, or NVIDIA RTX VSR.\nVSR also requires Super resolution enabled in NVIDIA settings.\nS: renderer performance overlay\n"
            L"If audio is early, add audio delay from the menu.\nAfter changing Windows' default output, press R.\n\nSettings and logs: %LOCALAPPDATA%\\SwitchView\nNo recording or network streaming.",
            L"SwitchView controls", MB_OK | MB_ICONINFORMATION);
        else if (command >= CmdVideo && command < CmdVideo + cameras.size()) {
            settings.videoId = cameras[command - CmdVideo].id; settings.mode.clear(); Start(false);
        } else if (command >= CmdAudio && command <= CmdAudio + inputs.size()) {
            settings.audioId = command == CmdAudio ? L"none" : inputs[command - CmdAudio - 1].id; RestartAudio();
        } else if (command >= CmdOutput && command <= CmdOutput + outputs.size()) {
            settings.outputId = command == CmdOutput ? L"" : outputs[command - CmdOutput - 1].id; RestartAudio();
        } else if (command >= CmdFormat && command < CmdFormat + modes.size()) {
            settings.mode = modes[command - CmdFormat].label; Start(false);
        } else if (command >= CmdDelay && command < CmdDelay + std::size(Delays)) {
            settings.delay = Delays[command - CmdDelay]; RestartAudio();
        } else if (command >= CmdScaling && command < CmdScaling + std::size(ScalingNames)) {
            int mode = command - CmdScaling;
            bool inplace = video.Enhanced() && mode != 0;
            settings.scaling = mode;
            if (inplace) { video.SetScaling(mode); video.ShowStats(rendererStats); UpdateMenu(); UpdateTitle(); }
            else Start();
        }
        ShowHud();
    }
    void ProfileUpdated() {
        smoothMotion = SmoothMotionEnabled();
        if (smoothMotion != pendingSmoothMotion)
            throw Failure{L"NVIDIA did not retain the requested Smooth Motion setting.", E_FAIL};
        if (smoothMotion && settings.scaling == 0) settings.scaling = 3;
        settings.Save(); UpdateMenu(); ShowHud();
        MessageBoxW(window,
            L"SwitchView's driver profile was updated. Exit and reopen the viewer to apply it.\n\n"
            L"To try 30 to 60 fps, select 1440p / 30 fps and GPU standard. Driver-generated frames are not included in the capture counter.\n\n"
            L"Fullscreen uses exclusive presentation for Smooth Motion. Press Esc to return to the settings menu.\n\n"
            L"Smooth Motion is experimental with this viewer. Enabling the profile does not confirm interpolation is active. It can add delay and visual artifacts.",
            L"Restart SwitchView", MB_OK | MB_ICONINFORMATION);
    }
    void Preset(const std::wstring& name) {
        if (name == L"720p60") settings.mode = L"1280 x 720  |  60 fps  |  MJPEG";
        else if (name == L"1080p50") settings.mode = L"1920 x 1080  |  50 fps  |  MJPEG";
        else if (name == L"1440p30") settings.mode = L"2560 x 1440  |  30 fps  |  MJPEG";
        else throw Failure{L"Unknown capture preset: " + name, E_INVALIDARG};
    }
    void RestartAudio() {
        audio.Stop(); audioWarning.clear();
        if (video.Running() && settings.audioId != L"none" && !options.noAudio) {
            if (FindId(inputs, settings.audioId) >= 0) audio.Start(settings.audioId, settings.outputId, settings.delay);
            else audioWarning = L"Select the capture card under Audio input.";
        }
        UpdateMenu(); UpdateTitle();
    }
    void Volume(int change) {
        settings.volume = std::clamp(settings.volume + change, 0, 100);
        audio.SetVolume(settings.volume, settings.mute); UpdateTitle(); ShowHud();
    }
    void ToggleFullscreen() {
        fullscreen = !fullscreen;
        if (fullscreen) {
            GetWindowRect(window, &savedBounds);
            SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
            UpdateMenu();
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
            SetWindowPos(window, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top, SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
            UpdateMenu();
            SetWindowPos(window, HWND_NOTOPMOST, savedBounds.left, savedBounds.top,
                savedBounds.right - savedBounds.left, savedBounds.bottom - savedBounds.top, SWP_FRAMECHANGED);
            cursorHidden = false; SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        video.Resize(window); ShowHud();
    }
    void NextMonitor() {
        auto screens = Monitors(); if (screens.size() < 2) return;
        HMONITOR current = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        auto found = std::find(screens.begin(), screens.end(), current);
        size_t index = found == screens.end() ? 0 : (found - screens.begin() + 1) % screens.size();
        MONITORINFO target{sizeof(target)}; GetMonitorInfoW(screens[index], &target);
        RECT bounds; GetWindowRect(window, &bounds);
        int width = fullscreen ? target.rcMonitor.right - target.rcMonitor.left : std::min(bounds.right - bounds.left, target.rcWork.right - target.rcWork.left);
        int height = fullscreen ? target.rcMonitor.bottom - target.rcMonitor.top : std::min(bounds.bottom - bounds.top, target.rcWork.bottom - target.rcWork.top);
        const RECT& rect = fullscreen ? target.rcMonitor : target.rcWork;
        SetWindowPos(window, nullptr, rect.left + (rect.right - rect.left - width) / 2,
            rect.top + (rect.bottom - rect.top - height) / 2, width, height, SWP_NOZORDER);
        if (fullscreen) { savedBounds = target.rcWork; InflateRect(&savedBounds, -80, -80); }
        ShowHud();
    }
    std::wstring AudioLabel() {
        if (!audioWarning.empty()) return audioWarning;
        if (settings.audioId == L"none" || options.noAudio) return L"Audio off";
        return settings.mute ? L"Muted" : L"Volume " + std::to_wstring(settings.volume) + L"%";
    }
    void UpdateTitle() {
        auto label = L"SwitchView  |  " + (video.Running() ? settings.mode : L"Capture stopped") + L"  |  " + ScalingNames[settings.scaling] + L"  |  " + AudioLabel();
        SetWindowTextW(window, label.c_str());
    }
    void ShowHud() {
        if ((options.exclusive || smoothMotion) && fullscreen && video.Enhanced()) return;
        if (!hud || IsIconic(window)) return;
        hudUntil = GetTickCount64() + 2800; cursorHidden = false;
        RECT rect; GetClientRect(window, &rect);
        POINT origin{0,0}; ClientToScreen(window, &origin);
        int width = std::min(900L, std::max(250L, rect.right - 40));
        SetWindowPos(hud, HWND_TOP, origin.x + (rect.right - width) / 2,
            origin.y + rect.bottom - 100, width, 76, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hud, nullptr, TRUE);
    }
    void PaintHud(HDC dc) {
        RECT rect; GetClientRect(hud, &rect);
        HBRUSH brush = CreateSolidBrush(RGB(22,26,32)); FillRect(dc, &rect, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(235,238,243)); SelectObject(dc, font);
        RECT first{18,10,rect.right - 18,36};
        std::wstring line = video.Running() ? settings.mode + L"  |  " + ScalingNames[settings.scaling] + L"  |  " + AudioLabel() : L"SwitchView  |  Right-click to choose a capture device";
        DrawTextW(dc, line.c_str(), -1, &first, DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(dc, RGB(166,178,192)); RECT second{18,42,rect.right-18,68};
        DrawTextW(dc, L"F11  fullscreen     Esc  window     M  mute     Up/Down  volume     Right-click  settings", -1, &second, DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    void Tick() {
        if (profileHelper.value && WaitForSingleObject(profileHelper.value, 0) == WAIT_OBJECT_0) {
            DWORD code = 1; GetExitCodeProcess(profileHelper.value, &code);
            CloseHandle(profileHelper.value); profileHelper.value = nullptr; UpdateMenu();
            if (code != 0) throw Failure{
                L"NVIDIA rejected the profile change. See %LOCALAPPDATA%\\SwitchView\\diagnostics\\profile\\framegen-profile.txt.", E_FAIL};
            ProfileUpdated();
        }
        auto error = video.PollError();
        if (!error.empty()) { Stop(); status = error; UpdateMenu(); InvalidateRect(window, nullptr, TRUE); ShowHud(); }
        auto audioState = audio.Stats();
        if (!audioState.error.empty() && audioWarning != audioState.error) {
            audioWarning = audioState.error; UpdateTitle(); ShowHud();
        }
        if (GetTickCount64() > hudUntil && audioWarning.empty() && !menuOpen) {
            ShowWindow(hud, SW_HIDE);
            if (fullscreen && GetForegroundWindow() == window) { cursorHidden = true; SetCursor(nullptr); }
        }
        if (options.smokeSeconds && started) SmokeTick();
    }
    void SmokeTick() {
        ULONGLONG elapsed = (GetTickCount64() - started) / 1000;
        if (elapsed >= 2 && !smokeEntered) { ToggleFullscreen(); smokeEntered = true; }
        if (elapsed >= 3 && !smokeExited) {
            ToggleFullscreen(); smokeExited = true;
            settings.mute = !settings.mute; Volume(0); settings.mute = !settings.mute; Volume(0);
        }
        if (elapsed >= 5 && !smokeRestarted) {
            framesBeforeRestart = video.Frames();
            Start(); smokeRestarted = true;
        }
        if (elapsed >= 7 && !fullscreen) ToggleFullscreen();
        if (elapsed >= 9 && !smokeCompared && video.Enhanced()) {
            std::filesystem::create_directories(options.output);
            try { video.CompareScaling(options.output); }
            catch (const Failure& e) { Log(e.message); }
            smokeCompared = true;
        }
        if (elapsed < static_cast<ULONGLONG>(options.smokeSeconds)) return;
        auto stats = audio.Stats();
        bool passed = video.Frames() > 5 && framesBeforeRestart > 5 && smokeEntered && smokeExited &&
            (options.noAudio || (stats.running && stats.rendered > 0));
        std::filesystem::create_directories(options.output);
        try { video.Snapshot(options.output / L"preview.bmp"); }
        catch (const Failure& e) { passed = false; Log(e.message); }
        if (video.Enhanced()) {
            try {
                video.ShowStats(true); video.Paint(window, nullptr);
                video.Snapshot(options.output / L"renderer-stats.bmp");
            } catch (const Failure& e) { Log(e.message); }
        }
        std::ofstream report(options.output / L"smoke-test.txt");
        report << "result=" << (passed ? "PASS" : "FAIL") << "\nformat=" << Utf8(settings.mode)
            << "\nvideo_frames_before_restart=" << framesBeforeRestart << "\nvideo_frames_after_restart=" << video.Frames()
            << "\nfullscreen_enter_exit=" << (smokeEntered && smokeExited) << "\nvolume_mute_controls_exercised=1"
            << "\naudio_running=" << stats.running << "\naudio_captured_frames=" << stats.captured
            << "\naudio_rendered_frames=" << stats.rendered << "\naudio_non_silent_frames=" << stats.nonSilent
            << "\naudio_peak=" << stats.peak << "\naudio_sample_rate=" << stats.sampleRate
            << "\naudio_channels=" << stats.channels << "\naudio_underruns=" << stats.underruns
            << "\naudio_trimmed_frames=" << stats.trimmed << "\naudio_output=" << Utf8(stats.outputName)
            << "\naudio_error=" << Utf8(stats.error) << "\nstatus=" << Utf8(status) << '\n';
        report << "scaling_mode=" << settings.scaling << "\nrenderer_info=\n" << Utf8(video.RendererInfo()) << '\n';
        report << "smooth_motion_profile=" << smoothMotion << '\n';
        result = passed ? 0 : 1; PostMessageW(window, WM_CLOSE, 0, 0); started = 0;
    }
    static LRESULT CALLBACK HudProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); }
        if (app && msg == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); app->PaintHud(dc); EndPaint(hwnd, &ps); return 0; }
        if (msg == WM_NCHITTEST) return HTTRANSPARENT;
        if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, msg, w, l);
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); app->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); }
        if (!app) return DefWindowProcW(hwnd, msg, w, l);
        try { return app->Message(msg, w, l); }
        catch (const Failure& e) { Log(e.message); MessageBoxW(hwnd, e.message.c_str(), L"SwitchView", MB_OK | MB_ICONERROR); return 0; }
        catch (const std::exception&) { Log(L"Unexpected application error."); MessageBoxW(hwnd, L"An unexpected error occurred. Restart SwitchView.", L"SwitchView", MB_OK | MB_ICONERROR); return 0; }
    }
    LRESULT Message(UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_CREATE: {
            font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            hud = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"SwitchViewHud", L"", WS_POPUP,
                0, 0, 800, 76, window, nullptr, instance, this);
            SetTimer(window, 1, 200, nullptr); PostMessageW(window, WM_APP + 1, 0, 0); return 0;
        }
        case WM_APP + 1:
            if (!options.windowed && !options.smokeSeconds) ToggleFullscreen();
            Start(); started = GetTickCount64(); return 0;
        case WM_COMMAND: Command(LOWORD(w)); return 0;
        case WM_ENTERMENULOOP: menuOpen = true; return 0;
        case WM_EXITMENULOOP: menuOpen = false; ShowHud(); return 0;
        case WM_CONTEXTMENU: {
            POINT p{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
            if (p.x == -1 && p.y == -1) GetCursorPos(&p);
            ContextMenu(p); return 0;
        }
        case WM_LBUTTONDBLCLK: ToggleFullscreen(); return 0;
        case WM_KEYDOWN:
            if (w == VK_F11 || w == 'F') ToggleFullscreen();
            else if (w == VK_ESCAPE) { if (fullscreen) ToggleFullscreen(); }
            else if (w == 'M') Command(CmdMute);
            else if (w == 'R') Command(CmdReconnect);
            else if (w == 'S') Command(CmdStats);
            else if (w == VK_SPACE) Command(CmdPlay);
            else if (w == VK_TAB) NextMonitor();
            else if (w == VK_UP) Volume(5);
            else if (w == VK_DOWN) Volume(-5);
            else if (w == VK_F1) Command(CmdHelp);
            else break;
            return 0;
        case WM_SYSKEYDOWN: if (w == VK_RETURN) { ToggleFullscreen(); return 0; } break;
        case WM_MOUSEWHEEL: Volume(static_cast<short>(HIWORD(w)) / WHEEL_DELTA * 5); return 0;
        case WM_MOUSEMOVE: {
            POINT p; GetCursorPos(&p);
            if (std::abs(p.x - lastMouse.x) + std::abs(p.y - lastMouse.y) > 4) { lastMouse = p; ShowHud(); }
            return 0;
        }
        case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && cursorHidden) { SetCursor(nullptr); return TRUE; } break;
        case WM_SIZE: video.Resize(window); if (hud) ShowWindow(hud, SW_HIDE); InvalidateRect(window, nullptr, TRUE); return 0;
        case WM_MOVE: if (hud) ShowWindow(hud, SW_HIDE); return 0;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && hud) ShowWindow(hud, SW_HIDE); return 0;
        case WM_DISPLAYCHANGE: video.DisplayChanged(); video.Resize(window); return 0;
        case WM_DPICHANGED: {
            auto bounds = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {480, 300}; return 0;
        case WM_TIMER: Tick(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(window, &ps);
            RECT rect; GetClientRect(window, &rect);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (video.Running()) video.Paint(window, dc);
            else {
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220,226,235)); SelectObject(dc, font);
                InflateRect(&rect, -48, -48);
                std::wstring text = L"SwitchView\n\n" + status + L"\n\nRight-click for devices and settings.\nF11: fullscreen   |   F1: controls   |   R: reconnect";
                DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_WORDBREAK);
            }
            EndPaint(window, &ps); return 0;
        }
        case WM_CLOSE:
            if (closing) return 0;
            closing = true; KillTimer(window, 1); Stop();
            if (!options.smokeSeconds) settings.Save();
            if (hud) { DestroyWindow(hud); hud = nullptr; }
            DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(result); return 0;
        }
        return DefWindowProcW(window, msg, w, l);
    }
    int Run(int show) {
        WNDCLASSW wc{}; wc.hInstance = instance; wc.lpszClassName = L"SwitchView";
        wc.lpfnWndProc = WindowProc; wc.style = CS_DBLCLKS; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClassW(&wc); wc.lpszClassName = L"SwitchViewHud"; wc.lpfnWndProc = HudProc; RegisterClassW(&wc);
        MONITORINFO screen{sizeof(screen)}; GetMonitorInfoW(MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY), &screen);
        int width = std::min(1280L, screen.rcWork.right - screen.rcWork.left - 80);
        int height = std::min(800L, screen.rcWork.bottom - screen.rcWork.top - 80);
        HWND created = CreateWindowExW(0, L"SwitchView", L"SwitchView", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            screen.rcWork.left + 40, screen.rcWork.top + 40, width, height, nullptr, nullptr, instance, this);
        if (!created) throw Failure{L"Could not create the viewer window.", HRESULT_FROM_WIN32(GetLastError())};
        ShowWindow(created, show); UpdateWindow(created);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        return static_cast<int>(message.wParam);
    }
};

int Probe(const Options& options) {
    std::filesystem::create_directories(options.output);
    std::ofstream out(options.output / L"devices.txt");
    auto videos = VideoDevices();
    out << "VIDEO DEVICES\n";
    for (const auto& device : videos) {
        out << Utf8(device.name) << "\n  ID: " << Utf8(device.id) << '\n';
        // Query only the HDMI card's capabilities during a diagnostic run.
        if (device.name.find(L"USB3.0 Video") == std::wstring::npos) continue;
        try { for (const auto& mode : VideoModes(device.id)) out << "  " << Utf8(mode.label) << '\n'; }
        catch (const Failure& e) { out << "  ERROR: " << Utf8(e.message) << '\n'; }
    }
    out << "\nAUDIO INPUTS\n";
    for (const auto& device : AudioDevices(eCapture)) out << Utf8(device.name) << "\n  ID: " << Utf8(device.id) << '\n';
    out << "\nAUDIO OUTPUTS\n";
    for (const auto& device : AudioDevices(eRender)) out << Utf8(device.name) << "\n  ID: " << Utf8(device.id) << '\n';
    try { VideoModes(L"missing-device-for-negative-test"); out << "\nMISSING DEVICE TEST: FAIL\n"; return 1; }
    catch (const Failure&) { out << "\nMISSING DEVICE TEST: PASS (explicit error; no fallback to camera)\n"; }
    return out ? 0 : 1;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoScope com(COINIT_APARTMENTTHREADED);
        Options options;
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) return 1;
        std::unique_ptr<LPWSTR, decltype(&LocalFree)> args(argv, &LocalFree);
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--windowed") options.windowed = true;
            else if (arg == L"--exclusive") options.exclusive = true;
            else if (arg == L"--probe") options.probe = true;
            else if (arg == L"--check-runtime") options.checkRuntime = true;
            else if (arg == L"--no-audio") options.noAudio = true;
            else if (arg == L"--scaling" && i + 1 < argc) options.scaling = std::clamp(_wtoi(argv[++i]), 0, 3);
            else if (arg == L"--preset" && i + 1 < argc) options.preset = argv[++i];
            else if (arg == L"--set-framegen" && i + 1 < argc) {
                std::wstring state = argv[++i];
                if (state != L"on" && state != L"off") throw Failure{L"--set-framegen requires on or off.", E_INVALIDARG};
                options.setFramegen = state == L"on" ? 1 : 0;
            }
            else if (arg == L"--output" && i + 1 < argc) options.output = argv[++i];
            else if (arg == L"--smoke-test" && i + 1 < argc) options.smokeSeconds = std::clamp(_wtoi(argv[++i]), 10, 60);
            else throw Failure{L"Unknown or incomplete argument: " + arg, E_INVALIDARG};
        }
        if (options.checkRuntime) {
            std::filesystem::create_directories(options.output);
            std::ofstream report(options.output / L"runtime-check.txt");
            try { ValidateGpuRuntime(); report << "result=PASS\nrenderer_abi=0.10.7.2560\ndecoder=LAV Video\n"; return report ? 0 : 1; }
            catch (const Failure& e) { report << "result=FAIL\nerror=" << Utf8(e.message) << '\n'; return 1; }
        }
        if (options.probe) return Probe(options);
        if (options.setFramegen >= 0) {
            std::filesystem::create_directories(options.output);
            std::ofstream report(options.output / L"framegen-profile.txt");
            try {
                bool before = SmoothMotionEnabled(); SetSmoothMotion(options.setFramegen != 0);
                report << "previous=" << before << "\nrequested=" << options.setFramegen << "\nreadback=" << SmoothMotionEnabled() << '\n';
                return 0;
            } catch (const Failure& e) { report << "error=" << Utf8(e.message) << '\n'; return 1; }
        }
        HANDLE rawMutex = CreateMutexW(nullptr, FALSE, L"Local\\SwitchView.CaptureViewer");
        DWORD mutexError = GetLastError(); Handle singleInstance(rawMutex);
        if (mutexError == ERROR_ALREADY_EXISTS) {
            HWND existing = FindWindowW(L"SwitchView", nullptr);
            if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
            return 0;
        }
        App app(instance, options); return app.Run(show);
    } catch (const Failure& error) { Log(error.message); MessageBoxW(nullptr, error.message.c_str(), L"SwitchView", MB_OK | MB_ICONERROR); }
    catch (const std::exception&) { MessageBoxW(nullptr, L"SwitchView could not start. Check the settings folder and available disk space.", L"SwitchView", MB_OK | MB_ICONERROR); }
    return 1;
}

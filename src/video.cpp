// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#include "video.h"
#include <cmath>
#include "../third_party/lav-video/LAVVideoSettings.h"

namespace {
ComPtr<IBaseFilter> CaptureDecoder() {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    auto path = std::filesystem::path(exe).parent_path() / L"LAVVideo.ax";
    static HMODULE module = nullptr;
    if (!module) module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw Failure{L"The GPU capture decoder is missing. Reinstall SwitchView or use Original.", HRESULT_FROM_WIN32(GetLastError())};
    auto create = reinterpret_cast<HRESULT (WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    if (!create) throw Failure{L"The capture decoder has an invalid entry point.", E_FAIL};
    constexpr CLSID clsid = {0xee30215d,0x164f,0x4a92,{0xa4,0xeb,0x9d,0x4c,0x13,0x39,0x0f,0x9f}};
    ComPtr<IClassFactory> factory; Check(create(clsid, IID_PPV_ARGS(&factory)), L"Load GPU capture decoder");
    ComPtr<IBaseFilter> decoder; Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&decoder)), L"Create GPU capture decoder");
    ComPtr<ILAVVideoSettings> settings; Check(decoder.As(&settings), L"Configure MJPEG decoding");
    Check(settings->SetRuntimeConfig(TRUE), L"Keep decoder settings local to SwitchView");
    // YUY2 retains the card's 4:2:2 chroma and can enter the D3D11 video processor.
    for (int i = 0; i < LAVOutPixFmt_NB; ++i)
        Check(settings->SetPixelFormat(static_cast<LAVOutPixFmts>(i), i == LAVOutPixFmt_YUY2), L"Select YUY2 decoded video");
    return decoder;
}

ComPtr<IBaseFilter> EnhancedRenderer() {
    wchar_t exe[32768]{};
    if (!GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe))))
        throw Failure{L"Cannot find the viewer installation.", E_FAIL};
    auto path = std::filesystem::path(exe).parent_path() / L"MpcVideoRenderer64.ax";
    // Keep the module resident until process exit, after all COM and graphics teardown.
    static HMODULE module = nullptr;
    if (!module) module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw Failure{L"The optional GPU renderer is missing. Reinstall SwitchView or select Original.", HRESULT_FROM_WIN32(GetLastError())};
    auto create = reinterpret_cast<HRESULT (WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    if (!create) throw Failure{L"The GPU renderer has an invalid entry point.", E_FAIL};
    constexpr CLSID clsid = {0x71f080aa,0x8661,0x4093,{0xb1,0x5e,0x4f,0x69,0x03,0xe7,0x7d,0x0a}};
    ComPtr<IClassFactory> factory;
    Check(create(clsid, IID_PPV_ARGS(&factory)), L"Load optional GPU renderer");
    ComPtr<IBaseFilter> renderer;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&renderer)), L"Create GPU renderer");
    ComPtr<IExFilterConfig> config; Check(renderer.As(&config), L"Check GPU renderer version");
    __int64 version = 0; Check(config->Flt_GetInt64("version", &version), L"Read GPU renderer version");
    constexpr __int64 expected = (static_cast<__int64>(10) << 32) | (7 << 16) | 2560;
    if (version != expected) throw Failure{L"The GPU renderer version does not match SwitchView. Reinstall it.", E_FAIL};
    return renderer;
}

void WriteBitmap(const std::filesystem::path& path, const BYTE* dib, size_t available) {
    if (available < sizeof(BITMAPINFOHEADER)) throw Failure{L"Invalid diagnostic image.", E_FAIL};
    auto info = reinterpret_cast<const BITMAPINFOHEADER*>(dib);
    if (info->biSize < sizeof(BITMAPINFOHEADER) || info->biWidth <= 0 || info->biHeight == 0 ||
        info->biWidth > 16384 || std::abs(info->biHeight) > 16384 || info->biBitCount != 32 || info->biCompression != BI_RGB)
        throw Failure{L"Unsupported diagnostic bitmap format.", E_FAIL};
    size_t bytes = static_cast<size_t>(info->biWidth) * std::abs(info->biHeight) * 4;
    if (info->biSize + bytes > available) throw Failure{L"Truncated diagnostic image.", E_FAIL};
    BITMAPFILEHEADER file{}; file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + info->biSize; file.bfSize = file.bfOffBits + static_cast<DWORD>(bytes);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(dib), info->biSize + bytes);
    if (!out) throw Failure{L"Could not write the diagnostic image.", E_FAIL};
}

struct MediaType {
    AM_MEDIA_TYPE* value = nullptr;
    ~MediaType() {
        if (!value) return;
        CoTaskMemFree(value->pbFormat);
        if (value->pUnk) value->pUnk->Release();
        CoTaskMemFree(value);
    }
};
ComPtr<ICreateDevEnum> DeviceEnumerator() {
    ComPtr<ICreateDevEnum> result;
    Check(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&result)), L"Enumerate video devices"); return result;
}
std::wstring MonikerId(IMoniker* moniker) {
    LPOLESTR raw = nullptr;
    Check(moniker->GetDisplayName(nullptr, nullptr, &raw), L"Read video device ID");
    std::wstring id(raw); CoTaskMemFree(raw); return id;
}
ComPtr<IBaseFilter> OpenDevice(const std::wstring& id) {
    ComPtr<IEnumMoniker> list;
    HRESULT hr = DeviceEnumerator()->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &list, 0);
    Check(hr, L"Enumerate video devices");
    if (hr == S_OK) {
        ComPtr<IMoniker> moniker;
        while (list->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
            if (MonikerId(moniker.Get()) != id) continue;
            ComPtr<IBaseFilter> filter;
            Check(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter)), L"Open video capture device");
            return filter;
        }
    }
    throw Failure{L"The selected video capture device is disconnected. Reconnect it and press R.", E_FAIL};
}
ComPtr<IAMStreamConfig> StreamConfig(IBaseFilter* source) {
    ComPtr<ICaptureGraphBuilder2> builder;
    Check(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&builder)), L"Create capture builder");
    ComPtr<IAMStreamConfig> config;
    Check(builder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, source,
        IID_IAMStreamConfig, reinterpret_cast<void**>(config.GetAddressOf())), L"Read capture formats");
    return config;
}
bool FormatFields(AM_MEDIA_TYPE* mt, BITMAPINFOHEADER*& bitmap, REFERENCE_TIME*& interval) {
    if (mt->formattype == FORMAT_VideoInfo && mt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto info = reinterpret_cast<VIDEOINFOHEADER*>(mt->pbFormat);
        bitmap = &info->bmiHeader; interval = &info->AvgTimePerFrame; return true;
    }
    if (mt->formattype == FORMAT_VideoInfo2 && mt->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        auto info = reinterpret_cast<VIDEOINFOHEADER2*>(mt->pbFormat);
        bitmap = &info->bmiHeader; interval = &info->AvgTimePerFrame; return true;
    }
    return false;
}
std::wstring SubtypeName(const GUID& type) {
    if (type == MEDIASUBTYPE_MJPG) return L"MJPEG";
    if (type == MEDIASUBTYPE_YUY2) return L"YUY2";
    if (type == MEDIASUBTYPE_NV12) return L"NV12";
    if (type == MEDIASUBTYPE_RGB24) return L"RGB24";
    if (type == MEDIASUBTYPE_RGB32) return L"RGB32";
    wchar_t fourcc[5]{};
    for (int i = 0; i < 4; ++i) fourcc[i] = (type.Data1 >> (i * 8)) & 0xff;
    for (int i = 0; i < 4; ++i) if (fourcc[i] < 32 || fourcc[i] > 126) return L"Video";
    return fourcc;
}

}

std::vector<Device> VideoDevices() {
    std::vector<Device> devices;
    ComPtr<IEnumMoniker> list;
    HRESULT hr = DeviceEnumerator()->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &list, 0);
    Check(hr, L"Enumerate video devices");
    if (hr != S_OK) return devices;
    ComPtr<IMoniker> moniker;
    while (list->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        ComPtr<IPropertyBag> props;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&props)))) continue;
        VARIANT name; VariantInit(&name);
        if (SUCCEEDED(props->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR)
            devices.push_back({name.bstrVal, MonikerId(moniker.Get())});
        VariantClear(&name);
    }
    return devices;
}

void ValidateGpuRuntime() {
    auto renderer = EnhancedRenderer();
    auto decoder = CaptureDecoder();
}

std::vector<VideoMode> VideoModes(const std::wstring& deviceId) {
    auto source = OpenDevice(deviceId);
    auto config = StreamConfig(source.Get());
    int count = 0, size = 0;
    Check(config->GetNumberOfCapabilities(&count, &size), L"Read capture format count");
    if (size < sizeof(VIDEO_STREAM_CONFIG_CAPS) || size > 65536 || count < 0 || count > 4096)
        throw Failure{L"Capture driver returned invalid format capabilities.", E_FAIL};
    std::vector<BYTE> caps(size);
    std::vector<VideoMode> modes;
    for (int i = 0; i < count; ++i) {
        MediaType mt;
        if (FAILED(config->GetStreamCaps(i, &mt.value, caps.data()))) continue;
        BITMAPINFOHEADER* bitmap = nullptr; REFERENCE_TIME* current = nullptr;
        if (!FormatFields(mt.value, bitmap, current)) continue;
        auto limits = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(caps.data());
        REFERENCE_TIME fastest = limits->MinFrameInterval > 0 ? limits->MinFrameInterval : *current;
        if (fastest <= 0 || bitmap->biWidth <= 0 || bitmap->biHeight == 0) continue;
        std::vector<REFERENCE_TIME> intervals{fastest};
        for (int fps : {60, 50, 30}) {
            REFERENCE_TIME value = 10000000 / fps;
            if (value >= fastest && value <= limits->MaxFrameInterval &&
                std::abs(static_cast<double>(value - fastest)) > 1000) intervals.push_back(value);
        }
        for (auto interval : intervals) {
            VideoMode mode{i, bitmap->biWidth, std::abs(bitmap->biHeight), interval, {}};
            std::wostringstream label;
            label << mode.width << L" x " << mode.height << L"  |  " << std::fixed << std::setprecision(0)
                << 10000000.0 / interval << L" fps  |  " << SubtypeName(mt.value->subtype);
            mode.label = label.str();
            if (std::none_of(modes.begin(), modes.end(), [&](const auto& m) { return m.label == mode.label; }))
                modes.push_back(mode);
        }
    }
    std::stable_sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        return a.interval < b.interval;
    });
    return modes;
}

void VideoPreview::Start(HWND window, const std::wstring& deviceId, const VideoMode& mode, int scaling, bool exclusive) {
    exclusiveMode = exclusive;
    Stop();
    try {
        Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), L"Create video graph");
        ComPtr<ICaptureGraphBuilder2> builder;
        Check(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&builder)), L"Create capture builder");
        Check(builder->SetFiltergraph(graph.Get()), L"Set capture graph");
        auto source = OpenDevice(deviceId);
        Check(graph->AddFilter(source.Get(), L"Capture card"), L"Add capture card");
        auto config = StreamConfig(source.Get());
        int count = 0, size = 0;
        Check(config->GetNumberOfCapabilities(&count, &size), L"Read capture capabilities");
        if (mode.index < 0 || mode.index >= count || size < sizeof(VIDEO_STREAM_CONFIG_CAPS) || size > 65536)
            throw Failure{L"The capture format changed. Press R to refresh the device.", E_FAIL};
        std::vector<BYTE> caps(size);
        MediaType mt;
        Check(config->GetStreamCaps(mode.index, &mt.value, caps.data()), L"Read selected capture format");
        BITMAPINFOHEADER* bitmap = nullptr; REFERENCE_TIME* interval = nullptr;
        if (!FormatFields(mt.value, bitmap, interval)) throw Failure{L"Unsupported capture format.", E_FAIL};
        *interval = mode.interval;
        Check(config->SetFormat(mt.value), L"Set video resolution and frame rate");
        ComPtr<IBaseFilter> renderer;
        if (scaling != 0) {
            renderer = EnhancedRenderer();
            Check(graph->AddFilter(renderer.Get(), L"GPU video preview"), L"Add GPU renderer");
            Check(renderer.As(&enhancedSettings), L"Read GPU scaling controls");
            Check(renderer.As(&enhancedConfig), L"Read GPU renderer controls");
            Check(renderer.As(&enhancedWindow), L"Read GPU video window");
            Check(renderer.As(&enhancedVideo), L"Read GPU video dimensions");
            SetScaling(scaling);
            Check(enhancedWindow->put_Owner(reinterpret_cast<OAHWND>(window)), L"Attach GPU preview to window");
        } else {
            Check(CoCreateInstance(CLSID_VideoMixingRenderer9, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&renderer)), L"Create Windows video renderer");
            Check(graph->AddFilter(renderer.Get(), L"Video preview"), L"Add video renderer");
            ComPtr<IVMRFilterConfig9> rendererConfig;
            Check(renderer.As(&rendererConfig), L"Configure video renderer");
            Check(rendererConfig->SetRenderingMode(VMR9Mode_Windowless), L"Set windowless rendering");
            Check(renderer.As(&display), L"Get video display");
            Check(display->SetVideoClippingWindow(window), L"Attach video to window");
            Check(display->SetAspectRatioMode(VMR9ARMode_LetterBox), L"Preserve video aspect ratio");
            display->SetBorderColor(RGB(0, 0, 0));
        }
        // The preview path removes capture timestamps through Smart Tee when needed.
        ComPtr<IBaseFilter> decoder;
        if (enhancedWindow && mt.value->subtype == MEDIASUBTYPE_MJPG) {
            decoder = CaptureDecoder();
            Check(graph->AddFilter(decoder.Get(), L"MJPEG video decoder"), L"Add MJPEG decoder");
        }
        Check(builder->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, source.Get(), decoder.Get(),
            renderer.Get()), L"Connect capture preview; close OBS if it is using the device");

        if (enhancedWindow)
            Check(enhancedWindow->put_MessageDrain(reinterpret_cast<OAHWND>(window)), L"Route preview controls");
        Check(graph.As(&control), L"Get video controls");
        Check(graph.As(&events), L"Get video events");
        renderer->QueryInterface(IID_IQualProp, reinterpret_cast<void**>(quality.GetAddressOf()));
        Resize(window);
        Check(control->Run(), L"Start capture; close OBS if it is using the device");
        Log(L"Video started: " + mode.label + L"; scaling=" + std::to_wstring(scaling));
    } catch (...) { Stop(); throw; }
}
void VideoPreview::Stop() {
    if (control) control->Stop();
    enhancedVideo.Reset(); enhancedWindow.Reset(); enhancedConfig.Reset(); enhancedSettings.Reset();
    quality.Reset(); display.Reset(); events.Reset(); control.Reset(); graph.Reset();
}
void VideoPreview::Resize(HWND window) {
    RECT rect; GetClientRect(window, &rect);
    if (rect.right <= 0 || rect.bottom <= 0) return;
    if (display) display->SetVideoPosition(nullptr, &rect);
    if (enhancedWindow && enhancedVideo) {
        enhancedWindow->SetWindowPosition(0, 0, rect.right, rect.bottom);
        long x = 0, y = 0;
        if (FAILED(enhancedVideo->GetPreferredAspectRatio(&x, &y)) || x <= 0 || y <= 0)
            enhancedVideo->GetVideoSize(&x, &y);
        if (x > 0 && y > 0) {
            double scale = std::min(static_cast<double>(rect.right) / x, static_cast<double>(rect.bottom) / y);
            long width = static_cast<long>(x * scale), height = static_cast<long>(y * scale);
            enhancedVideo->SetDestinationPosition((rect.right-width)/2, (rect.bottom-height)/2, width, height);
        }
    }
}
void VideoPreview::Paint(HWND window, HDC dc) {
    if (display) display->RepaintVideo(window, dc);
    if (enhancedConfig) enhancedConfig->Flt_SetBool("cmd_redraw", true);
}
void VideoPreview::DisplayChanged() { if (display) display->DisplayModeChanged(); }
long VideoPreview::Frames() const { int n = 0; if (quality) quality->get_FramesDrawn(&n); return n; }
std::wstring VideoPreview::PollError() {
    if (!events) return {};
    long code = 0; LONG_PTR a = 0, b = 0;
    std::wstring error;
    while (events->GetEvent(&code, &a, &b, 0) == S_OK) {
        if (code == EC_ERRORABORT) error = L"Video capture stopped (" + Hex(static_cast<HRESULT>(a)) + L"). Press R to reconnect.";
        if (code == EC_DEVICE_LOST) error = L"Video device changed or disconnected. Press R to reconnect.";
        events->FreeEventParams(code, a, b);
    }
    return error;
}
void VideoPreview::Snapshot(const std::filesystem::path& path) {
    if (enhancedConfig) {
        void* raw = nullptr; unsigned size = 0;
        Check(enhancedConfig->Flt_GetBin("displayedImage", &raw, &size), L"Read GPU processed frame");
        std::unique_ptr<void, decltype(&LocalFree)> pixels(raw, &LocalFree);
        WriteBitmap(path, static_cast<BYTE*>(raw), size); return;
    }
    if (!display) throw Failure{L"No video to snapshot.", E_FAIL};
    BYTE* raw = nullptr;
    Check(display->GetCurrentImage(&raw), L"Read rendered test frame");
    std::unique_ptr<BYTE, decltype(&CoTaskMemFree)> pixels(raw, &CoTaskMemFree);
    auto info = reinterpret_cast<BITMAPINFOHEADER*>(raw);
    DWORD bytes = ((info->biWidth * info->biBitCount + 31) / 32) * 4 * std::abs(info->biHeight);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + info->biSize;
    file.bfSize = file.bfOffBits + bytes;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(raw), info->biSize + bytes);
    if (!out) throw Failure{L"Could not write the diagnostic frame.", E_FAIL};
}

void VideoPreview::SetScaling(int mode) {
    if (!enhancedSettings || mode < 1 || mode > 3) return;
    scalingMode = mode;
    Settings_t settings;
    settings.bUseD3D11 = true;
    settings.bExclusiveFS = exclusiveMode;
    settings.iTexFormat = TEXFMT_8INT;
    settings.bVPScaling = mode != 1;
    settings.iVPSuperRes = mode == 2 ? SUPERRES_1440p : SUPERRES_Disable;
    settings.iUpscaling = UPSCALE_Lanczos3;
    settings.iDownscaling = DOWNSCALE_Lanczos;
    settings.iVPDeinterlacing = DEINT_Disable;
    settings.bDeintDouble = false;
    settings.bInterpolateAt50pct = false;
    settings.bAdjustPresentTime = false;
    settings.bHdrPassthrough = false;
    settings.bVPRTXVideoHDR = false;
    enhancedSettings->SetSettings(settings);
}

void VideoPreview::CompareScaling(const std::filesystem::path& directory) {
    if (!enhancedSettings || !control) return;
    int restore = scalingMode;
    Check(control->Pause(), L"Pause video for a same-frame upscaling comparison");
    OAFilterState state;
    Check(control->GetState(2000, &state), L"Wait for paused video");
    try {
        for (auto [mode, name] : {std::pair<int,const wchar_t*>{3, L"standard.bmp"},
            {2, L"vsr.bmp"}, {1, L"lanczos.bmp"}}) {
            SetScaling(mode);
            Check(enhancedConfig->Flt_SetBool("cmd_redraw", true), L"Redraw comparison frame");
            Snapshot(directory / name);
        }
        SetScaling(restore); Check(control->Run(), L"Resume video after comparison");
    } catch (...) { SetScaling(restore); control->Run(); throw; }
}

void VideoPreview::ShowStats(bool enabled) {
    if (!enhancedSettings) return;
    Settings_t settings; enhancedSettings->GetSettings(settings);
    settings.bShowStats = enabled; enhancedSettings->SetSettings(settings);
}

std::wstring VideoPreview::RendererInfo() const {
    if (!enhancedSettings) return L"Original Windows renderer";
    std::wstring info;
    Check(enhancedSettings->GetVideoProcessorInfo(info), L"Read GPU renderer diagnostics");
    return info;
}

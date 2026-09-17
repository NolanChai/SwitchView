// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#pragma once
#include "common.h"
#include <dshow.h>
#include <dvdmedia.h>
#include <d3d9.h>
#include <vmr9.h>
#include <VersionHelpers.h>
#include "../third_party/mpc-video-renderer/IVideoRenderer.h"
#include "../third_party/mpc-video-renderer/FilterInterfaces.h"

struct VideoMode {
    int index = 0, width = 0, height = 0;
    REFERENCE_TIME interval = 0;
    std::wstring label;
};

std::vector<Device> VideoDevices();
std::vector<VideoMode> VideoModes(const std::wstring& deviceId);
void ValidateGpuRuntime();

class VideoPreview {
    ComPtr<IGraphBuilder> graph;
    ComPtr<IMediaControl> control;
    ComPtr<IMediaEventEx> events;
    ComPtr<IVMRWindowlessControl9> display;
    ComPtr<IQualProp> quality;
    ComPtr<IVideoWindow> enhancedWindow;
    ComPtr<IBasicVideo2> enhancedVideo;
    ComPtr<IVideoRenderer> enhancedSettings;
    ComPtr<IExFilterConfig> enhancedConfig;
    int scalingMode = 0;
    bool exclusiveMode = false;
public:
    ~VideoPreview() { Stop(); }
    void Start(HWND window, const std::wstring& deviceId, const VideoMode& mode, int scaling = 0, bool exclusive = false);
    void Stop();
    void Resize(HWND window);
    void Paint(HWND window, HDC dc);
    void DisplayChanged();
    bool Running() const { return control != nullptr; }
    long Frames() const;
    std::wstring PollError();
    void Snapshot(const std::filesystem::path& path);
    void CompareScaling(const std::filesystem::path& directory);
    void SetScaling(int mode);
    void ShowStats(bool enabled);
    std::wstring RendererInfo() const;
    bool Enhanced() const { return enhancedSettings != nullptr; }
};

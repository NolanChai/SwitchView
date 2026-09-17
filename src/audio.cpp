// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#include "audio.h"
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <cmath>

namespace {
ComPtr<IMMDeviceEnumerator> Enumerator() {
    ComPtr<IMMDeviceEnumerator> result;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&result)), L"Enumerate audio devices"); return result;
}
std::wstring DeviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> props;
    Check(device->OpenPropertyStore(STGM_READ, &props), L"Read audio device properties");
    PROPVARIANT name; PropVariantInit(&name);
    Check(props->GetValue(PKEY_Device_FriendlyName, &name), L"Read audio device name");
    std::wstring result = name.vt == VT_LPWSTR ? name.pwszVal : L"Audio device";
    PropVariantClear(&name); return result;
}
float Peak(const BYTE* data, UINT32 frames, const WAVEFORMATEX* format) {
    if (!data) return 0;
    bool floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
        floating = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    float peak = 0;
    const size_t samples = static_cast<size_t>(frames) * format->nChannels;
    for (size_t i = 0; i < samples; ++i) {
        float sample = 0;
        if (floating && format->wBitsPerSample == 32) {
            memcpy(&sample, data + i * 4, 4);
        } else if (!floating && format->wBitsPerSample == 16) {
            int16_t value; memcpy(&value, data + i * 2, 2); sample = value / 32768.0f;
        } else if (!floating && format->wBitsPerSample == 32) {
            int32_t value; memcpy(&value, data + i * 4, 4); sample = value / 2147483648.0f;
        } else if (!floating && format->wBitsPerSample == 24) {
            const BYTE* p = data + i * 3;
            int32_t value = (p[0] << 8) | (p[1] << 16) | (static_cast<uint32_t>(p[2]) << 24);
            sample = value / 2147483648.0f;
        }
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}
struct AudioTask {
    DWORD index = 0;
    HANDLE handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
    ~AudioTask() { if (handle) AvRevertMmThreadCharacteristics(handle); }
};
struct StartedClient {
    IAudioClient* client;
    ~StartedClient() { if (client) client->Stop(); }
};
}

std::vector<Device> AudioDevices(EDataFlow flow) {
    auto enumerator = Enumerator();
    ComPtr<IMMDeviceCollection> collection;
    Check(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection), L"List audio endpoints");
    UINT count = 0; Check(collection->GetCount(&count), L"Count audio endpoints");
    std::vector<Device> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device; Check(collection->Item(i, &device), L"Read audio endpoint");
        LPWSTR id = nullptr; Check(device->GetId(&id), L"Read audio endpoint ID");
        std::wstring copy(id); CoTaskMemFree(id);
        result.push_back({DeviceName(device.Get()), copy});
    }
    return result;
}
void AudioMonitor::Start(const std::wstring& inputId, const std::wstring& outputId, int delayMs) {
    Stop();
    { std::lock_guard lock(mutex); stats = {}; }
    if (inputId.empty()) return;
    if (!stopEvent.value) throw Failure{L"Could not create the audio stop event.", HRESULT_FROM_WIN32(GetLastError())};
    ResetEvent(stopEvent.value);
    worker = std::thread(&AudioMonitor::Run, this, inputId, outputId, std::clamp(delayMs, 0, 200));
}
void AudioMonitor::Stop() {
    if (stopEvent.value) SetEvent(stopEvent.value);
    if (worker.joinable()) worker.join();
    std::lock_guard lock(mutex); stats.running = false;
}
void AudioMonitor::Run(std::wstring inputId, std::wstring outputId, int delayMs) {
    try {
        CoScope com(COINIT_MULTITHREADED);
        AudioTask priority;
        auto enumerator = Enumerator();
        ComPtr<IMMDevice> input, output;
        Check(enumerator->GetDevice(inputId.c_str(), &input), L"Open capture-card audio input");
        if (outputId.empty())
            Check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &output), L"Open Windows default sound output");
        else Check(enumerator->GetDevice(outputId.c_str(), &output), L"Open selected sound output");
        ComPtr<IAudioClient> captureClient, renderClient;
        Check(input->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(captureClient.GetAddressOf())), L"Activate capture audio");
        Check(output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(renderClient.GetAddressOf())), L"Activate playback audio");
        WAVEFORMATEX* raw = nullptr;
        Check(captureClient->GetMixFormat(&raw), L"Read capture audio format");
        std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, &CoTaskMemFree);
        if (!raw->nSamplesPerSec || !raw->nBlockAlign || !raw->nChannels || raw->wBitsPerSample == 8)
            throw Failure{L"The audio input has an unsupported PCM format.", AUDCLNT_E_UNSUPPORTED_FORMAT};
        Handle captured(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        Handle rendered(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!captured.value || !rendered.value) throw Failure{L"Could not create audio events.", E_FAIL};
        Check(captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            0, 0, raw, nullptr), L"Initialize capture-card audio");
        Check(captureClient->SetEventHandle(captured.value), L"Set audio capture event");
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST;
        Check(renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 300000, 0, raw, nullptr),
            L"Initialize sound output");
        Check(renderClient->SetEventHandle(rendered.value), L"Set audio playback event");
        ComPtr<IAudioCaptureClient> capture;
        ComPtr<IAudioRenderClient> render;
        ComPtr<ISimpleAudioVolume> gain;
        Check(captureClient->GetService(IID_PPV_ARGS(&capture)), L"Get capture audio buffer");
        Check(renderClient->GetService(IID_PPV_ARGS(&render)), L"Get playback audio buffer");
        Check(renderClient->GetService(IID_PPV_ARGS(&gain)), L"Get playback volume");
        UINT32 bufferFrames = 0;
        Check(renderClient->GetBufferSize(&bufferFrames), L"Read playback buffer size");
        UINT32 target = std::min<UINT32>(bufferFrames, std::max<DWORD>(1, raw->nSamplesPerSec * 20 / 1000));
        size_t reserve = static_cast<size_t>(raw->nSamplesPerSec) * delayMs / 1000;
        size_t maxQueued = reserve + raw->nSamplesPerSec * 60 / 1000;
        AudioQueue queue(raw->nBlockAlign, maxQueued);
        AudioStats local;
        local.sampleRate = raw->nSamplesPerSec; local.channels = raw->nChannels;
        local.outputName = DeviceName(output.Get());
        int lastVolume = -1; bool lastMute = false, primed = false;
        Check(captureClient->Start(), L"Start audio capture"); StartedClient captureStop{captureClient.Get()};
        Check(renderClient->Start(), L"Start audio playback"); StartedClient renderStop{renderClient.Get()};
        local.running = true;
        Log(L"Audio started: " + std::to_wstring(raw->nSamplesPerSec) + L" Hz, " +
            std::to_wstring(raw->nChannels) + L" channels -> " + local.outputName);
        HANDLE waits[] = {stopEvent.value, captured.value, rendered.value};
        while (WaitForMultipleObjects(3, waits, FALSE, 100) != WAIT_OBJECT_0) {
            int nextVolume = volume.load(); bool nextMute = muted.load();
            if (nextVolume != lastVolume || nextMute != lastMute) {
                Check(gain->SetMasterVolume(nextVolume / 100.0f, nullptr), L"Set playback volume");
                Check(gain->SetMute(nextMute, nullptr), L"Set playback mute");
                lastVolume = nextVolume; lastMute = nextMute;
            }
            UINT32 packet = 0;
            Check(capture->GetNextPacketSize(&packet), L"Read capture packet size");
            while (packet) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD packetFlags = 0;
                Check(capture->GetBuffer(&data, &frames, &packetFlags, nullptr, nullptr), L"Read captured sound");
                bool silent = (packetFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                float peak = silent ? 0 : Peak(data, frames, raw);
                local.peak = std::max(local.peak, peak);
                local.captured += frames;
                if (peak > 0.00001f) local.nonSilent += frames;
                if (queue.Size() + frames > maxQueued) local.trimmed += queue.Size() + frames - maxQueued;
                queue.Push(data, frames, silent);
                Check(capture->ReleaseBuffer(frames), L"Release captured sound");
                Check(capture->GetNextPacketSize(&packet), L"Read capture packet size");
            }
            UINT32 padding = 0;
            Check(renderClient->GetCurrentPadding(&padding), L"Read playback timing");
            // Keep one capture period in reserve at startup so render/capture event ordering
            // does not insert a gap before the second packet arrives.
            if (!primed && queue.Size() >= reserve + target + raw->nSamplesPerSec / 100) primed = true;
            if (primed && padding < target) {
                UINT32 frames = target - padding; BYTE* dest = nullptr;
                Check(render->GetBuffer(frames, &dest), L"Get playback buffer");
                size_t copied = queue.Pop(dest, frames, reserve);
                if (copied < frames) ++local.underruns;
                Check(render->ReleaseBuffer(frames, copied ? 0 : AUDCLNT_BUFFERFLAGS_SILENT), L"Play captured sound");
                local.rendered += copied;
            }
            { std::lock_guard lock(mutex); stats = local; }
        }
    } catch (const Failure& error) {
        Log(error.message);
        std::lock_guard lock(mutex); stats.error = error.message + L". Press R to reconnect.";
    } catch (const std::exception&) {
        Log(L"Audio worker failed.");
        std::lock_guard lock(mutex); stats.error = L"Audio worker failed. Press R to reconnect.";
    }
    std::lock_guard lock(mutex); stats.running = false;
}

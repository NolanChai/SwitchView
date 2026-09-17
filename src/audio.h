// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#pragma once
#include "common.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <thread>

std::vector<Device> AudioDevices(EDataFlow flow);

// A bounded queue of complete PCM frames. Overflow discards the oldest frames.
class AudioQueue {
    std::vector<BYTE> bytes;
    size_t stride, capacity, head = 0, count = 0;
public:
    AudioQueue(size_t frameBytes, size_t frames) : bytes(frameBytes * frames), stride(frameBytes), capacity(frames) {}
    size_t Size() const { return count; }
    void KeepNewest(size_t frames) {
        if (count <= frames) return;
        head = (head + count - frames) % capacity; count = frames;
    }
    void Push(const BYTE* data, size_t frames, bool silent) {
        if (frames > capacity) {
            if (data) data += (frames - capacity) * stride;
            frames = capacity;
        }
        KeepNewest(capacity - frames);
        for (size_t i = 0; i < frames; ++i) {
            BYTE* dest = bytes.data() + ((head + count + i) % capacity) * stride;
            if (silent) memset(dest, 0, stride); else memcpy(dest, data + i * stride, stride);
        }
        count += frames;
    }
    size_t Pop(BYTE* dest, size_t frames, size_t reserve = 0) {
        size_t available = count > reserve ? count - reserve : 0;
        size_t copied = std::min(frames, available);
        for (size_t i = 0; i < copied; ++i)
            memcpy(dest + i * stride, bytes.data() + ((head + i) % capacity) * stride, stride);
        memset(dest + copied * stride, 0, (frames - copied) * stride);
        head = (head + copied) % capacity; count -= copied; return copied;
    }
};

struct AudioStats {
    bool running = false;
    uint64_t captured = 0, rendered = 0, nonSilent = 0, underruns = 0, trimmed = 0;
    unsigned sampleRate = 0, channels = 0;
    float peak = 0;
    std::wstring error, outputName;
};

class AudioMonitor {
    Handle stopEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker;
    mutable std::mutex mutex;
    AudioStats stats;
    std::atomic<int> volume{80};
    std::atomic<bool> muted{false};
    void Run(std::wstring inputId, std::wstring outputId, int delayMs);
public:
    ~AudioMonitor() { Stop(); }
    void Start(const std::wstring& inputId, const std::wstring& outputId, int delayMs);
    void Stop();
    void SetVolume(int value, bool mute) { volume = std::clamp(value, 0, 100); muted = mute; }
    AudioStats Stats() const { std::lock_guard lock(mutex); return stats; }
};

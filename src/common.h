// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <memory>
#include <sstream>
#include <iomanip>

using Microsoft::WRL::ComPtr;

struct Failure { std::wstring message; HRESULT code; };
inline std::wstring Hex(HRESULT hr) {
    std::wostringstream s; s << L"0x" << std::hex << std::uppercase << std::setw(8)
        << std::setfill(L'0') << static_cast<unsigned long>(hr); return s.str();
}
inline void Check(HRESULT hr, const wchar_t* operation) {
    if (FAILED(hr)) throw Failure{std::wstring(operation) + L" (" + Hex(hr) + L")", hr};
}
inline std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
inline std::filesystem::path DataDirectory() {
    PWSTR path = nullptr;
    Check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path), L"Find local settings folder");
    std::filesystem::path dir(path); CoTaskMemFree(path); dir /= L"SwitchView";
    std::filesystem::create_directories(dir); return dir;
}
inline void Log(const std::wstring& text) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    try {
        auto path = DataDirectory() / L"viewer.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 1024 * 1024)
            std::ofstream(path, std::ios::trunc).close();
        SYSTEMTIME t; GetLocalTime(&t);
        std::ofstream out(path, std::ios::app);
        out << t.wYear << '-' << t.wMonth << '-' << t.wDay << ' ' << t.wHour << ':'
            << t.wMinute << ':' << t.wSecond << " " << Utf8(text) << '\n';
    } catch (...) { /* Logging must not prevent capture cleanup. */ }
}

struct Device { std::wstring name, id; };
struct CoScope {
    HRESULT hr;
    explicit CoScope(DWORD mode) : hr(CoInitializeEx(nullptr, mode)) { Check(hr, L"Initialize COM"); }
    ~CoScope() { CoUninitialize(); }
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

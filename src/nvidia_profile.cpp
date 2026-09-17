// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#include "nvidia_profile.h"
#include <d3d11.h>
#include "../third_party/nvapi/nvapi.h"

namespace {
// Driver-profile IDs used by NVIDIA Profile Inspector. These are driver-specific,
// so this feature is optional and failures must never prevent normal capture.
constexpr NvU32 SmoothMotion = 0xB0D384C0;
constexpr NvU32 AllowedApis = 0xB0CC0875;
constexpr wchar_t ProfileName[] = L"SwitchView capture viewer";

class ProfileSession {
    HMODULE module = nullptr;
    using Query = void* (__cdecl*)(NvU32);
    Query query = nullptr;
    template<typename T> T Function(NvU32 id) {
        void* function = query(id);
        if (!function) throw Failure{L"NVIDIA profile API is unavailable on this driver.", E_NOTIMPL};
        return reinterpret_cast<T>(function);
    }
    void CheckNv(NvAPI_Status status, const wchar_t* operation) {
        if (status == NVAPI_INVALID_USER_PRIVILEGE) throw Failure{
            L"Windows administrator approval is required to change SwitchView's Smooth Motion profile.", E_ACCESSDENIED};
        if (status != NVAPI_OK) throw Failure{std::wstring(operation) + L" (NVAPI " + std::to_wstring(status) + L")", E_FAIL};
    }
public:
    NvDRSSessionHandle session = nullptr;
    decltype(&NvAPI_DRS_DestroySession) destroy = nullptr;
    decltype(&NvAPI_DRS_FindProfileByName) findProfile = nullptr;
    decltype(&NvAPI_DRS_FindApplicationByName) findApp = nullptr;
    // Current drivers expose these settings through the extended DRS entry points.
    // Signatures/IDs: NVIDIA Profile Inspector, Native/NVAPI/NvapiDrsWrapper.cs.
    using GetSetting = NvAPI_Status (__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING*, NvU32*);
    using SetSetting = NvAPI_Status (__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*, NvU32, NvU32);
    GetSetting getSetting = nullptr;
    ProfileSession() {
        module = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw Failure{L"NVIDIA's driver API was not found.", E_NOTIMPL};
        try {
            query = reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface"));
            if (!query) throw Failure{L"NVIDIA profile API is unavailable.", E_NOTIMPL};
            CheckNv(Function<decltype(&NvAPI_Initialize)>(0x0150e828)(), L"Initialize NVIDIA driver API");
            destroy = Function<decltype(&NvAPI_DRS_DestroySession)>(0xdad9cff8);
            findProfile = Function<decltype(&NvAPI_DRS_FindProfileByName)>(0x7e4a9a0b);
            findApp = Function<decltype(&NvAPI_DRS_FindApplicationByName)>(0xeee566b2);
            getSetting = Function<GetSetting>(0xea99498d);
            CheckNv(Function<decltype(&NvAPI_DRS_CreateSession)>(0x0694d52e)(&session), L"Open NVIDIA profile session");
            CheckNv(Function<decltype(&NvAPI_DRS_LoadSettings)>(0x375dbd6b)(session), L"Read NVIDIA profiles");
        } catch (...) { if (session && destroy) destroy(session); FreeLibrary(module); throw; }
    }
    ~ProfileSession() { if (session) destroy(session); if (module) FreeLibrary(module); }
    static void CopyName(NvAPI_UnicodeString& dest, const wchar_t* source) {
        if (wcslen(source) >= NVAPI_UNICODE_STRING_MAX) throw Failure{L"Application path is too long for NVIDIA profiles.", E_INVALIDARG};
        wcscpy_s(reinterpret_cast<wchar_t*>(dest), NVAPI_UNICODE_STRING_MAX, source);
    }
    bool Enabled() {
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
        NvAPI_UnicodeString appName{}; CopyName(appName, path);
        NVDRS_APPLICATION app{}; app.version = NVDRS_APPLICATION_VER;
        NvDRSProfileHandle profile = nullptr;
        auto found = findApp(session, appName, &profile, &app);
        if (found == NVAPI_EXECUTABLE_NOT_FOUND) return false;
        CheckNv(found, L"Find SwitchView's NVIDIA profile");
        NVDRS_SETTING setting{}; setting.version = NVDRS_SETTING_VER;
        NvU32 flags = 0;
        auto read = getSetting(session, profile, SmoothMotion, &setting, &flags);
        if (read == NVAPI_SETTING_NOT_FOUND) return false;
        CheckNv(read, L"Read Smooth Motion setting");
        return setting.settingType == NVDRS_DWORD_TYPE && setting.u32CurrentValue == 1;
    }
    void Set(bool enabled) {
        NvAPI_UnicodeString name{}; CopyName(name, ProfileName);
        NvDRSProfileHandle profile = nullptr;
        auto found = findProfile(session, name, &profile);
        if (found == NVAPI_PROFILE_NOT_FOUND) {
            if (!enabled) return;
            NVDRS_PROFILE info{}; info.version = NVDRS_PROFILE_VER; CopyName(info.profileName, ProfileName);
            CheckNv(Function<decltype(&NvAPI_DRS_CreateProfile)>(0xcc176068)(session, &info, &profile), L"Create SwitchView's NVIDIA profile");
        } else CheckNv(found, L"Find SwitchView's NVIDIA profile");
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
        NvAPI_UnicodeString appName{}; CopyName(appName, path);
        NVDRS_APPLICATION app{}; app.version = NVDRS_APPLICATION_VER;
        NvDRSProfileHandle current = nullptr;
        auto associated = findApp(session, appName, &current, &app);
        if (associated == NVAPI_EXECUTABLE_NOT_FOUND) {
            app = {}; app.version = NVDRS_APPLICATION_VER;
            CopyName(app.appName, path); CopyName(app.userFriendlyName, L"SwitchView");
            CheckNv(Function<decltype(&NvAPI_DRS_CreateApplication)>(0x4347a9de)(session, profile, &app), L"Associate only SwitchView with its GPU profile");
        } else {
            CheckNv(associated, L"Read SwitchView's GPU profile association");
            if (current != profile) throw Failure{L"SwitchView already belongs to a different NVIDIA profile. Configure Smooth Motion in NVIDIA App for that profile.", E_FAIL};
        }
        auto set = Function<SetSetting>(0x8a2cf5f5);
        for (auto [id, value] : {std::pair<NvU32,NvU32>{SmoothMotion, enabled ? 1u : 0u}, {AllowedApis, 2u}}) {
            NVDRS_SETTING setting{}; setting.version = NVDRS_SETTING_VER;
            setting.settingId = id; setting.settingType = NVDRS_DWORD_TYPE; setting.u32CurrentValue = value;
            CheckNv(set(session, profile, &setting, 0, 0), L"Set SwitchView's Smooth Motion profile");
        }
        CheckNv(Function<decltype(&NvAPI_DRS_SaveSettings)>(0xfcbc7e14)(session), L"Save SwitchView's NVIDIA profile");
        Log(enabled ? L"SwitchView Smooth Motion driver profile enabled; restart required." : L"SwitchView Smooth Motion driver profile disabled; restart required.");
    }
};
}
bool SmoothMotionEnabled() { ProfileSession session; return session.Enabled(); }
void SetSmoothMotion(bool enabled) { ProfileSession session; session.Set(enabled); }

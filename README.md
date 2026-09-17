# SwitchView

[![Windows build](https://github.com/NolanChai/SwitchView/actions/workflows/ci.yml/badge.svg)](https://github.com/NolanChai/SwitchView/actions/workflows/ci.yml)

A native Windows app for playing an HDMI capture card's picture and sound on your PC. Built for a Nintendo Switch 2 setup, with device selection for other DirectShow capture cards. OBS is not required.

- Fullscreen viewing and direct WASAPI audio monitoring.
- GPU scaling with Lanczos or NVIDIA RTX Video Super Resolution.
- Experimental NVIDIA Smooth Motion for frame generation.
- Portable x64 app, optional per-user installation, local settings, and no network activity during playback.

## Get started

Download the portable Windows ZIP from [Releases](https://github.com/NolanChai/SwitchView/releases), extract the **complete folder**, and open `SwitchView.exe`. The first release is a preview and is not Authenticode-signed; Windows may identify it as an unknown publisher.

1. Connect the console's HDMI output to a capture card connected to the PC.
2. Close OBS or deactivate its capture source so the card is available.
3. Open SwitchView. Press **Esc**, then use **Viewer → Video device** and **Audio input** if the card is not selected.
4. Select a supported video format and sound output, then press **F11** for fullscreen.

The tested `USB3.0 Video` / `USB3.0 Audio` devices are recognized automatically. Other devices need explicit selection. The app does not automatically fall back to a webcam or microphone. Compatibility beyond the tested setup is not yet established.

Optional Start menu installation, from the extracted release folder:

```powershell
.\install.ps1
```

`-DesktopShortcut` adds a desktop shortcut. Installation defaults to `%LOCALAPPDATA%\Programs\SwitchView` and does not need administrator privileges. Exit the installed viewer before updating it.

## Controls

| Control | Action |
| --- | --- |
| F11, F, Alt+Enter, double-click | Toggle fullscreen |
| Esc | Return to a window and settings |
| Tab | Move to the next monitor |
| M | Mute/unmute |
| Up / Down / mouse wheel | App volume |
| Space | Stop/start and release the capture devices |
| R | Refresh devices and reconnect |
| S | GPU renderer performance overlay |
| F1 | Help |
| Alt+F4 | Exit |

Right-click for settings, or use **Viewer** in windowed mode. With Smooth Motion enabled, press **Esc** first: exclusive fullscreen hides the popup HUD and settings menu.

Audio defaults to the Windows output selected when capture starts. Press R after changing the system default, or choose a specific output. If sound arrives before the picture, try **Additional audio delay** (20–200 ms). Volume and mute affect SwitchView's playback session.

## Upscaling and frame generation

Choose a capture format first, then select **Upscaling**:

| Mode | Processing |
| --- | --- |
| Original | Windows VMR9; works without the optional GPU libraries |
| Lanczos sharp | D3D11 shader scaling with Lanczos3 |
| NVIDIA RTX VSR | NVIDIA video super resolution through D3D11 |
| GPU standard | D3D11 video processing without a super-resolution request |

RTX VSR also needs **Super resolution** enabled in NVIDIA Control Panel → Video → Adjust video image settings, or NVIDIA App → System → Video. A renderer `SuperResolution*` indicator means the request succeeded; it does not prove the driver's model is active. See [NVIDIA's RTX Video FAQ](https://nvidia.custhelp.com/app/answers/detail/a_id/5448). At 1440p input on a 1440p display, there is no enlargement to perform.

**NVIDIA Smooth Motion** is experimental. Enable its menu toggle, approve the one-time Windows prompt if required, and restart SwitchView. This changes only the application's NVIDIA profile. GPU modes then use exclusive fullscreen; keep the viewer focused for consistent pacing. Press Esc to adjust settings. Toggle Smooth Motion off and restart to disable it.

The [tested setup](docs/validation.md) presented a 1440p/30 capture feed at **60.01 fps** using an RTX 4070 SUPER and driver 610.88. This does not establish the same behavior on other drivers, GPUs, or cards. NVIDIA documents support on [RTX 40/50 cards in compatible applications](https://nvidia.custhelp.com/app/answers/detail/a_id/5621). The capture counter still reports the input rate, and enabling the profile alone does not prove interpolation is active. Frame generation can add latency and visual artifacts. It does not increase the console game's simulation rate or input responsiveness.

This app processes HDMI video; it does not have the game-engine depth buffers and motion vectors used by DLSS integration.

### Tested capture-card formats

| MJPEG input | Driver-advertised rate | Tradeoff |
| --- | --- | --- |
| 2560 × 1440 | 30 fps | Most captured detail; Smooth Motion can target 60 fps presentation |
| 1920 × 1080 | 50 fps | More direct motion samples than 1440p/30 |
| 1280 × 720 | Approximately 60 fps | Highest reported 16:9 capture rate on this card |

The preset menu is tailored to these modes. Other cards expose different formats through **Video format**. A 60 fps capture feed can contain repeated frames from a 30 fps game. No end-to-end latency improvement over OBS has been measured.

## Build

Windows x64, PowerShell 7, Git, Visual Studio 2022 or newer with **Desktop development with C++**, and a Windows SDK:

```powershell
git clone https://github.com/NolanChai/SwitchView.git
cd SwitchView
.\build.ps1 -Test
.\dist\SwitchView.exe
```

The first build downloads pinned MPC Video Renderer and LAV Video archives, verifies SHA-256 hashes, and copies only required runtime files. Later builds support `-Offline`. Headers and notices are tracked; binaries, caches, settings, and diagnostics are ignored. No DirectShow filters are registered globally.

C++17 / Win32 provides the UI. DirectShow supplies capture video; WASAPI uses event-driven shared-mode capture/playback with a bounded audio queue. GPU rendering uses app-local MPC Video Renderer; LAV decodes MJPEG into YUY2. Original mode uses the Windows MJPEG decoder and VMR9. The optional NVIDIA profile helper loads the installed system NVAPI library. No NVIDIA driver binaries or models are redistributed.

CI verifies the build, audio queue behavior, portable installation, version metadata, runtime DLL loading/ABI, and rejection of missing dependencies, damaged files, and unsafe paths. Physical capture, synchronization, and GPU frame generation require hardware testing. See [Contributing](CONTRIBUTING.md) and [release preparation](docs/releasing.md).

## Diagnostics and local data

Settings and logs live in `%LOCALAPPDATA%\SwitchView`. Ordinary use does not save video or audio. Diagnostic commands explicitly save frames and device information:

```powershell
# Enumerate devices and check the missing-device error path.
Start-Process .\dist\SwitchView.exe -ArgumentList '--probe --output diagnostics' -Wait

# Requires a free capture card. Exercise capture/audio, fullscreen, and restart.
Start-Process .\dist\SwitchView.exe -ArgumentList '--smoke-test 12 --output diagnostics' -Wait
Get-Content .\diagnostics\smoke-test.txt
```

Use `--scaling 0|1|2|3`, `--preset 720p60|1080p50|1440p30`, `--windowed`, or `--no-audio` for a specific run. GPU smoke tests also save same-frame scaling comparisons. Review diagnostics for personal device IDs, paths, and gameplay images before sharing them. Tests do not overwrite saved viewer settings.

To uninstall, turn Smooth Motion off first if enabled, then remove the installation folder and Start menu shortcut. Remove `%LOCALAPPDATA%\SwitchView` separately to discard settings/logs. The NVIDIA application profile remains with Smooth Motion disabled.

## License

Copyright (C) 2026 Nolan Chai. **GPL-3.0-or-later**; see [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). Binary releases include matching SwitchView and dependency-source archives.

SwitchView is an independent project, not affiliated with Nintendo or NVIDIA. No game assets, console firmware, or ROMs are included.

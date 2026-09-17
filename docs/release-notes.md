SwitchView is a native Windows capture-card viewer with direct audio monitoring,
GPU upscaling, and experimental NVIDIA Smooth Motion frame generation.

### Install

Download **SwitchView-0.1.0-windows-x64.zip**, extract the complete folder, and open
`SwitchView.exe`. Close OBS or deactivate its capture source first. Press Esc for
the settings menu. Select the card and its audio input if they are not detected.
The optional `install.ps1` adds a per-user Start menu shortcut.

### GPU features

Choose **Upscaling → NVIDIA RTX VSR** after enabling Super resolution in the
NVIDIA driver settings. **Lanczos sharp** is also available. Smooth Motion uses
an application-specific driver profile and may request Windows administrator
approval. Restart the viewer after changing it. Keep the viewer focused in
fullscreen; Esc returns to a window for settings.

The tested RTX 4070 SUPER / driver 610.88 setup presented a 1440p/30 capture feed
at 60.01 fps in focused fullscreen. This is not a guarantee for other hardware,
drivers, or games. Interpolation can add delay and artifacts. The capture counter
continues to show the input rate. See the repository's validation document.

### Release files

- `SwitchView-0.1.0-windows-x64.zip`: portable app, dependencies, notices, installer.
- `SwitchView-0.1.0-source.zip`: SwitchView source and build scripts.
- `SwitchView-0.1.0-dependency-sources.zip`: corresponding upstream source,
  recursive submodules, build scripts, and notices for bundled native components.
- `SwitchView-0.1.0-SHA256SUMS.txt`: archive integrity hashes.

This first release is a preview and is not Authenticode-signed. Windows may show
an unknown-publisher prompt. Capture-card coverage is currently limited. Windows
10/11 x64 is the target; the physical validation was performed on Windows 11.

GPL-3.0-or-later. Independent project; no affiliation with Nintendo or NVIDIA.

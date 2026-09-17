# MPC Video Renderer

Pinned upstream: https://github.com/Aleksoid1978/VideoRenderer/releases/tag/0.10.7

Release archive: `MpcVideoRenderer-0.10.7.2560.zip`

Archive SHA-256 (also supplied by the GitHub release API):
`3d8461d05354f7c3db8ec95e46c92b5737d2e401697a4990eaf6dc9eaa1ee147`

x64 binary SHA-256:
`7d63a92b3a3fe277fd01ce9e8f2509d0fc50f7392e3bb1cc6a5f6877a363a412`

`IVideoRenderer.h`, `FilterInterfaces.h`, and `LICENSE.txt` are unmodified from tag 0.10.7 (commit fd3a829d820042437a4ad796b36324d88369cd0e). The renderer and interfaces are GPL-3.0-or-later; see `LICENSE.txt` for the full license. The release's dependency-source archive includes the full tracked source tree and both pinned submodules, exported by scripts/package.ps1.

SwitchView loads only its app-local x64 renderer with `DllGetClassObject`. It does not run the upstream registration scripts or change the system's DirectShow registration. The wrapper checks the exact ABI version before passing `Settings_t` and supplies settings in memory without saving the renderer's global registry settings. VSR activation is controlled separately by the NVIDIA driver.

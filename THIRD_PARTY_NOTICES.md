# Third-party components

SwitchView is Copyright (C) 2026 Nolan Chai, licensed under GPL-3.0-or-later.
See LICENSE. Upstream components retain their own copyright and license notices.

| Component | Version | License | Distribution |
| --- | --- | --- | --- |
| [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer) | 0.10.7.2560 | GPL-3.0-or-later | Unmodified x64 renderer and interface headers |
| [LAV Filters](https://github.com/Nevcairiel/LAVFilters) | 0.83 | GPL-2.0-or-later | Unmodified LAV Video and runtime dependencies |
| FFmpeg, as built by LAV | Pinned LAV submodule | GPL-3.0-or-later configuration; individual files retain their licenses | avcodec, avutil, avfilter, avformat, swscale, swresample |
| libbluray and libudfread | Pinned LAV submodules | LGPL-2.1-or-later and upstream file notices | libbluray runtime dependency |
| BaseClasses and MinHook | Pinned MPC submodules | Upstream notices, including BSD-style licenses | Used by MPC Video Renderer |
| [NVIDIA NVAPI](https://github.com/NVIDIA/nvapi) | 87dca625e83fd89a983e19b904e5f3a580da90d2 | MIT | Headers only; the installed system driver is loaded at runtime |

Runtime downloads and per-file hashes are pinned in `dependencies.lock.json`.
The portable package includes primary license texts under `licenses/`.
Every binary release must be accompanied by its SwitchView source archive and
dependency-source archive on the same release page. The latter includes the
exact upstream source trees, recursive submodules, build scripts, and all their
notices. See [source packaging](docs/dependency-sources.md).

NVIDIA Profile Inspector's documented driver-setting IDs and function signatures
are referenced in the NVAPI provenance file. No Profile Inspector executable or
implementation is bundled. No NVIDIA driver, DLSS model, console firmware, ROM,
game asset, or gameplay screenshot is distributed with SwitchView.

Nintendo Switch is a trademark of Nintendo. NVIDIA and GeForce are trademarks of
NVIDIA Corporation. SwitchView is an independent project and is not affiliated
with or endorsed by those companies.

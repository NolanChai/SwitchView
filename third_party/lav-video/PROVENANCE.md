# LAV Video 0.83

Unmodified x64 binaries from the official release:
https://github.com/Nevcairiel/LAVFilters/releases/tag/0.83

Archive: LAVFilters-0.83-x64.zip
SHA-256: `0126982f47157bb86a6dbb43c4f332f7f98beba9ad552c19b65f9db2e7d4f186`

The installed subset is listed and pinned in SHA256SUMS.txt. The manifest requires
the accompanying FFmpeg and libbluray libraries even though SwitchView uses only
the video decoder. No registration scripts are run. LAVVideoSettings.h comes
from the same upstream tag's include directory. SetRuntimeConfig keeps settings
local to this process; only YUY2 decoder output is enabled for the D3D11 processor.

LAV Filters is GPL-2.0-or-later (COPYING). Source and build instructions:
https://github.com/Nevcairiel/LAVFilters/tree/0.83
The source's pinned submodules supply FFmpeg, libbluray, and other dependencies;
use a recursive checkout of tag 0.83 to obtain the corresponding source tree.
The third-party code and binary licenses remain those of their upstream projects.

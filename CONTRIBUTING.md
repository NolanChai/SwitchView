# Contributing

Build on Windows x64 with Visual Studio 2022 or newer, the Desktop development
with C++ workload, a Windows SDK, Git, and PowerShell 7:

```powershell
.\build.ps1 -Test
```

The first build downloads pinned upstream GPU dependencies and verifies archive
and per-file SHA-256 hashes. Subsequent builds can use `-Offline`. Runtime files,
build outputs, private settings, diagnostics, and gameplay captures are ignored.
Do not commit them. Keep vendored headers unmodified and retain their notices.

Keep changes focused. Describe the affected behavior and how you checked it.
Use the audio queue and package tests for relevant changes. Hardware testing is
needed for capture, audio, fullscreen, or rendering changes; report the device,
format, GPU, driver, and observed result. CI has no capture card or NVIDIA GPU
and cannot establish synchronization, perceived smoothness, or input latency.

New capture cards should require explicit selection when their identity is
unknown. Avoid automatic fallback to webcams or microphones. Keep diagnostic
recording opt-in and driver settings scoped to this application's profile.

Please file reproducible bugs through the issue template. For vulnerabilities,
see [SECURITY.md](SECURITY.md). Contributions are accepted under the project's
GPL-3.0-or-later license; contributors retain copyright in their changes.

# Local capture and GPU validation

Tested September 16, 2026 on RTX 4070 SUPER, driver 610.88, XG27AQDMG
2560×1440 at 239.972 Hz, USB3.0 Video capture, and USB3.0 Audio at 48 kHz stereo.

The GPU 720p/60 smoke test passed capture, fullscreen transitions, device
stop/reopen, and audio monitoring with zero underruns. A paused-frame 2560×1440
comparison produced different standard GPU, VSR, and Lanczos outputs. Standard
versus VSR differed by a mean 3.24 levels per RGB channel, well above simple
one-level rounding. The user confirmed enabling the driver's Super resolution
setting. This comparison demonstrates changed processing, not recovered source
detail or an objective quality improvement.

Smooth Motion profile write/readback succeeded after Windows elevation. The
profile is limited to SwitchView. NvPresent64.dll loaded after restarting the
installed executable.

PresentMon 2.5.1 was downloaded from its official GameTechDev release and verified
against the release API's SHA-256 digest. A 1440p/30 borderless run recorded
29.994 displayed fps. Exclusive fullscreen activated the faster presentation
path; while other windows overlapped it, frame pacing was inconsistent.

The focused exclusive-fullscreen trace (`diagnostics/framegen-focused.csv`)
recorded 1,197 presents over approximately 20 seconds, all in Hardware:
Independent Flip mode. Mean display interval: 16.6638 ms (60.0103 fps). Median:
16.688 ms. The capture configuration remained 1440p/30 and deinterlacing/double
rate processing were disabled. PresentMon labeled these driver-intercepted
presents as Application, so that field does not identify generated frames here.
The evidence establishes a 60 Hz output cadence from this 30 Hz capture path;
it does not measure pixel accuracy or end-to-end controller/audio latency.

The viewer selects the same exclusive presentation setting automatically when
the saved NVIDIA Smooth Motion profile is enabled. Esc returns to a window for
settings. Original rendering remains available without GPU dependencies.

The user confirmed that motion looks smoother and Esc works. The final installed
build passed its 1440p/30 smoke test with Smooth Motion's profile enabled, exclusive
D3D11 presentation, working audio, and zero audio underruns. Installed and built
executables matched SHA-256
`51b0365e9a02f09659af40b8e756fcbca3287612abd92a31b8a85e39d3f11f93`.
The normal Start menu launch was then reopened at 1440p/30 with GPU standard.

## Release preparation

The versioned 0.1.0 build retains the same playback implementation. Release
checks additionally verify x64/version metadata, dependency DLL loading and
renderer ABI, portable installation into an isolated folder, explicit rejection
of missing optional dependencies, corrupt-file rejection, and unsafe-path
rejection. These tests do not open capture devices or change NVIDIA profiles.
The historical executable hash above identifies the original hardware-tested
build; use the release checksum file for the distributed versioned package.

On September 17, the versioned package also passed a 720p/60 VSR capture smoke
run with device restart, fullscreen transitions, flowing video/audio samples,
and zero audio underruns. The input was black and silent during this run, so it
does not repeat the earlier active-game picture, sound, or frame-generation
assessment. A clean Windows 2022 GitHub runner passed the build, dependency
downloads, audio queue, and package/runtime checks.

Audio queue unit checks cover ordering, bounded overflow, wraparound, silence,
delay reserve, and underrun filling. Ordinary use records no video or audio.

# Changelog

## 0.1.0

Initial preview release for Windows x64.

- DirectShow capture-card video with event-driven WASAPI audio monitoring.
- Fullscreen/windowed viewing, device and format selection, volume, mute, and
  additional audio delay.
- D3D11 rendering with Lanczos scaling and NVIDIA RTX Video Super Resolution.
- Experimental, application-scoped NVIDIA Smooth Motion with exclusive
  fullscreen presentation. A tested 1440p/30 setup reached 60.01 displayed fps.
- Portable distribution, optional per-user installation, pinned dependencies,
  complete dependency-source archives, checksums, and Windows CI.

Hardware coverage is limited to the configuration in [validation](docs/validation.md).
No end-to-end latency improvement or general capture-card compatibility is claimed.

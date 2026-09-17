# Security

The latest release is the supported version. GPU modes load bundled native
libraries, so use the complete official release and verify its checksum file.
Checksums detect changed files; they do not replace a trusted download source.
The initial release is not Authenticode-signed.

Report vulnerabilities privately through GitHub's **Security → Report a
vulnerability** on this repository. Include the affected version and a minimal
reproduction. Avoid posting private device IDs, local paths, or gameplay captures
in public reports. No response-time guarantee is currently offered.

SwitchView performs no network requests during playback. The build bootstrap
downloads pinned dependency archives over HTTPS and verifies their hashes.
Settings and diagnostic files stay local. Ordinary capture does not save video
or audio. The optional Smooth Motion setting launches an elevated helper only
when NVIDIA requires it; playback itself runs with ordinary user privileges.

# Corresponding dependency sources

The dependency-source ZIP on each SwitchView release contains complete tracked
source trees for the exact MPC Video Renderer and LAV Filters tags used by that
release, including their recursive submodules. The revisions and binary hashes
are recorded in `dependencies.lock.json` alongside the source directories.
All upstream copyright and license notices are retained.

MPC includes `external/BaseClasses` and `external/minhook`. LAV includes FFmpeg,
libbluray, libudfread, and qsdecoder. qsdecoder is part of the upstream source
tree but its optional Intel Quick Sync DLL is not shipped in SwitchView.

Use the upstream READMEs and build scripts in each source directory to build
those components. LAV's build scripts specify its FFmpeg configuration; Windows
SDK/MSVC, MSYS2 and other upstream build-tool prerequisites are not bundled.
These are source snapshots without Git metadata. Scripts that derive version
numbers from Git need a checkout of the recorded commit/tag; SwitchView's
`scripts/fetch-sources.ps1` reconstructs and verifies those checkouts and all
submodule revisions. Release binaries are unmodified upstream artifacts, not
claimed to be byte-for-byte reproducible with arbitrary toolchain versions.

From a SwitchView source checkout:

```powershell
.\scripts\fetch-sources.ps1
.\scripts\package.ps1
```

The package script exports only committed tracked source, verifies dependency
revisions, and creates both source archives next to the portable binary archive.
Upload all archives and the release checksum file together. Retain the source
archives for as long as the associated binary release remains available.

# Preparing a release

1. Update VERSION, CHANGELOG.md, and docs/release-notes.md. Record hardware
   validation separately from CI results.
2. Review dependency hashes, revisions, licenses, and notices. Run the package
   tests and the relevant capture/audio checks. Keep personal diagnostics local.
3. Commit the reviewed source. Run `./scripts/package.ps1` from a clean checkout,
   or run **Prepare draft release** in GitHub Actions.
4. Inspect the portable ZIP and both corresponding-source ZIPs. Upload all three
   with the release checksum file. Keep source archives available alongside the
   binaries. The workflow creates a draft preview and never overwrites a release.
5. Review the draft and its exact target commit, CI result, checksums, and notes.
   Publish manually when approved. To make a private preparation repository
   public, change repository visibility before publishing the draft.

The portable installer and package tests accept `-InstallDirectory` and
`-NoShortcut` to verify an isolated installation without replacing the daily app.
Code signing is not configured. Do not describe checksums as a publisher signature.

# GitHub releases for RoboDesk firmware

GitHub Releases can host the application image, its OTA signature, and a small `manifest.txt` as release assets. Public repository releases are available on GitHub Free; each asset must be under 2 GiB and GitHub documents no total release-size or bandwidth limit.

## Keep the public repository safe

- `secrets.h` is ignored by Git. It may contain Wi-Fi credentials, a Gemini API key, and the dashboard PIN.
- `.ota-secrets/` is ignored. It contains the Windows DPAPI-protected private signing key and release-version counter.
- `firmware-releases/` and generated `.bin`, `.sig`, `.elf`, and `.map` files are ignored. Add firmware binaries as GitHub Release assets, not source commits.
- `secrets.example.h` contains placeholders. The packaging script builds from a temporary source copy using that file as `secrets.h` so a local secret can never enter the release binary.
- A public release is readable by anyone. Do not publish the existing locally provisioned v1/v2 binaries; build a new generic image with `tools/prepare_github_release.ps1`.

## Prepare an asset set

For the public repository `akh-211/robodesk`, choose a new increasing release number and run PowerShell from this project directory:

```powershell
.\tools\prepare_github_release.ps1 -Repository akh-211/robodesk -Version 3
```

The script stages source without `secrets.h`, the signing key, and previous binaries; substitutes `secrets.example.h`; builds the ESP32-S3 16 MB image; signs and verifies it with the local DPAPI key; and writes these assets to a temporary output directory:

- `RoboDeskSonicCharacter.ino.bin`
- `RoboDeskSonicCharacter.ino.bin.sig`
- `manifest.txt`

Create a GitHub Release tagged `v3`, upload all three assets, and publish it. The manifest URL for the device will be:
`https://github.com/akh-211/robodesk/releases/latest/download/manifest.txt`.

The manifest schema is newline-separated `key=value`: `format`, `board`, `version`, `size`, `sha256`, `image`, `signature`, and `url`. The firmware verifies the signed image/version/size/SHA-256 before activation. The device-side HTTPS manifest checker is not active yet; it still needs the repository slug and a follow-up firmware build before GitHub releases can be installed directly from the dashboard.

Release versions are monotonic. Preparing a package consumes its version locally even if it is not published; use the next higher number if a package needs to be rebuilt.

Keep `.ota-secrets/firmware-signing-key.dpapi` backed up securely. It is protected for this Windows account; losing it prevents future signed firmware from being accepted by the installed public key.

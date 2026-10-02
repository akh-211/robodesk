# GitHub releases for RoboDesk firmware

GitHub Releases can host the application image, its OTA signature, and a small `manifest.txt` as release assets. Public repository releases are available on GitHub Free; each asset must be under 2 GiB and GitHub documents no total release-size or bandwidth limit.

## Keep the public repository safe

- `secrets.h` is ignored by Git. It may contain Wi-Fi credentials, a Gemini API key, and the dashboard PIN.
- `.ota-secrets/` is ignored. It contains the Windows DPAPI-protected private signing key and release-version counter.
- `firmware-releases/` and generated `.bin`, `.sig`, `.elf`, and `.map` files are ignored. Add firmware binaries as GitHub Release assets, not source commits.
- `secrets.example.h` contains placeholders. The packaging script builds from a temporary source copy using that file as `secrets.h` so a local secret can never enter the release binary.
- A public release is readable by anyone. Do not publish the existing locally provisioned v1/v2 binaries; build a new generic image with `tools/prepare_github_release.ps1`.

## Prepare an asset set

The LivingEyes dependency is a separate checkout, not part of this firmware repo. By default, host/release tools select `%USERPROFILE%\OneDrive\Documents\LivingEyes-merge\LivingEyes`, not the old installed `Arduino/libraries/LivingEyes`. `-LivingEyesRoot` explicitly selects a different checkout and overrides `ROBODESK_LIVINGEYES_ROOT`; neither bypasses the pin. `tools/livingeyes_fingerprint.py` hashes `library.properties` and every file under `src/` in sorted relative-path order, including file names and raw bytes. `tools/livingeyes-pin.json` records the approved EmoOLED merger source (`396851b9104a6f5e163008f7d1ea13eb9e48c08ea7b0e587879a3122ee903287`), whose strict LivingEyes native suite and RoboDesk host suites passed on 2026-09-29. CMake/CTest and physical qualification remain separate pending gates; any subsequent source change fails closed until the digest is reviewed and deliberately updated. The immutable pre-merger rollback snapshot remains outside both repositories; restore it before this merger if rollback is required.

For the public repository `akh-211/robodesk`, from this project directory in PowerShell **only when authorized and ready to sign**, set `$nextVersion` to the next unused release number **greater than 5** (the value `6` below is illustrative, not a release decision):

```powershell
$eyes = Join-Path $env:USERPROFILE 'OneDrive/Documents/LivingEyes-merge/LivingEyes'
python .\tools\livingeyes_fingerprint.py $eyes --pin .\tools\livingeyes-pin.json
$nextVersion = 6 # Example only; check the next unused version before signing.
.\tools\prepare_github_release.ps1 -Repository akh-211/robodesk -Version $nextVersion -LivingEyesRoot $eyes
```

The example release command must **not** be run during merger verification: a successful package consumes its version and signs an image. The script verifies the LivingEyes fingerprint before staging, passes the checkout's parent to Arduino CLI with `--libraries`, and checks the CLI's verbose resolver output for the **exact selected checkout path**. It checks the fingerprint again after compilation and refuses to sign on any mismatch, missing resolver evidence, or wrong library selection. It stages source without `secrets.h`, the signing key, and previous binaries; substitutes `secrets.example.h`; builds the ESP32-S3 16 MB image; signs and verifies it with the local DPAPI key; and writes these assets to a temporary output directory:

- `RoboDeskSonicCharacter.ino.bin`
- `RoboDeskSonicCharacter.ino.bin.sig`
- `manifest.txt`

When release gates have passed and publication is authorized, tag the approved version `v<nextVersion>` (greater than v5), upload all three matching assets, and publish. The manifest URL for the device is:
`https://github.com/akh-211/robodesk/releases/latest/download/manifest.txt`.

The manifest schema is newline-separated `key=value`: `format`, `board`, `version`, `size`, `sha256`, `image`, `signature`, and `url`. The corrected updater is designed to fetch the latest manifest over HTTPS using the ESP certificate bundle, allow only HTTPS redirects to approved GitHub release-asset hosts, and download the image from the tag-pinned URL. It checks image size, SHA-256, board, monotonic version, and ECDSA signature before staging the inactive slot. These checks and the redirect flow still need end-to-end device verification; host tests or a target build alone do not establish successful on-device GitHub OTA, rollback, or data preservation.

## Dashboard update prerequisites and v5 bootstrap

The dashboard's **Update from GitHub** check/install needs the robot connected to station/router Wi-Fi with internet access, DNS, and a synchronized clock for certificate-verified HTTPS; the setup AP and local-only Wi-Fi are not enough. Installation also requires an idle robot with WakeNet disarmed. If WakeNet is armed, select **Touch-to-talk**, save the setting, let the robot reboot, then retry. Keep power and network connectivity stable during an update. The signed **local firmware upload** is a separate dashboard operation on trusted local Wi-Fi and does not need internet or HTTPS time synchronization.

A device still running the old updater (including the published v5 image) has a broken redirect path, so its GitHub Check/Install buttons cannot be relied on to bootstrap the redirect fix. First install a compatible signed application version **greater than v5** containing the corrected updater, using a backup-first USB procedure or, if the installed firmware's signed local dashboard upload is available and accepts the version/signature, that local upload. Do not replace the partition table or model partition as part of an application update. Verify boot, rollback behavior, and retained settings/data on the device. Only after that bootstrap can future higher-version signed GitHub releases be candidates for dashboard GitHub check/install; successful on-device operation is not yet established.

Release versions are monotonic. Preparing a package consumes its version locally even if it is not published; use the next higher number if a package needs to be rebuilt.

Keep `.ota-secrets/firmware-signing-key.dpapi` backed up securely. It is protected for this Windows account; losing it prevents future signed firmware from being accepted by the installed public key.

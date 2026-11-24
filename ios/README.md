# ClassicMac for iPad

The iPad build uses UTM SE's on-device QEMU runtime. iPadOS does not allow the
macOS app's bundled `Process`-based QEMU executables, so the emulator is linked
as shared frameworks and runs with QEMU's threaded-code interpreter. The beta
is iPad-only, targets iPadOS 14 or newer, and includes the `m68k` and `ppc`
engines needed for the Quadra 800 and Power Macintosh G4 presets.

This first iPad beta deliberately uses UTM's upstream Classic Mac hardware
support. It does not yet include the emulated ATI Radeon or
`nubus-qfb` display device, and emulation is slower than the Apple Silicon
macOS build because TestFlight apps cannot use JIT compilation.

## Build

The build is pinned to UTM commit
`8e4de50817e76a83d6840212311627a78dd4f8b2` and to the matching iOS TCI sysroot
artifact. It generates the complete iPad icon set from
`Resources/AppIcon.png`, applies `ios/utm-classicmac.patch`, and produces an
unsigned archive by default:

```bash
./scripts/build-ipad.sh
```

GitHub Actions workflow artifacts expire after 90 days. If the pinned `gh run download` sysroot artifact has expired, pass `SYSROOT_URL` (and optionally `SYSROOT_SHA256`) pointing to a preserved `sysroot.tgz` archive before running `./scripts/build-ipad.sh`.

Build products stay under the gitignored `build/ipad` directory. Override the
version or build number with `CLASSICMAC_MARKETING_VERSION` and
`CLASSICMAC_BUILD_NUMBER`.

## TestFlight

After the `com.classicmac.emulator` app record exists in App Store Connect,
supply your Apple Developer Team ID and App Store Connect API key credentials
via environment variables (`APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, and
`ASC_KEY_FILE`) to sign the archive, export and validate the IPA, and upload it:

```bash
APPLE_TEAM_ID=YOUR_TEAM_ID \
ASC_KEY_ID=YOUR_KEY_ID \
ASC_ISSUER_ID=YOUR_ISSUER_ID \
ASC_KEY_FILE=/path/to/AuthKey_YOUR_KEY_ID.p8 \
./scripts/upload-ipad-testflight.sh
```

The temporary mode-600 key copy is removed by the script's exit trap, and
credentials are never stored in the repository or build directory.

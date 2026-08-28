# ClassicMac Mac OS 9.2.1 Installer CD

`scripts/build-os9-921-installer-cd.sh` clones an original bootable Mac OS
9.2.1 CD and adds a mandatory package to Apple's regular Installer 4.5.5
script. Easy Install, Custom Install, and Reinstall therefore all install the
ClassicMac additions without launching a second installer.

## Build

```bash
./scripts/build-os9-921-installer-cd.sh \
  /path/to/macos_921_ppc.iso \
  dist/ClassicMac-Mac-OS-9.2.1.iso
```

The source and output arguments are optional. They default to
`context/macos_921_ppc.iso` and
`dist/ClassicMac-Mac-OS-9.2.1.iso`, respectively.

The script downloads the checked archives in `guestcd/manifest.tsv`, builds
GXMetal from source, expands the old installers on the host, preserves classic
resource forks and Finder metadata, patches the Installer application, and
verifies that Apple's partition map and pre-HFS boot-driver blocks did not
change.

The USB Overdrive archive's registration document is copied to
`Apple Extras:USB Overdrive`. A separately supplied Transmit registration
document can be included without committing it:

```bash
CLASSICMAC_TRANSMIT_SERIAL_FILE=/private/path/Transmit-Registration.txt \
  ./scripts/build-os9-921-installer-cd.sh
```

## Installed layout

- `System Folder:Extensions`: GXMetal, GXMetal Input, GXMetal Startup, the two
  USB Overdrive drivers, StuffIt Engine, and StuffIt Engine PowerPlug.
- `System Folder:Control Panels`: USB Overdrive.
- `Apple Extras`: StuffIt Expander 5.5, DropStuff 5.5, Transmit 1.6 PPC,
  GXMetal test/repair and RAVE-selection tools, documentation, and
  registration documents.

The integration uses ordinary format-1 Installer file atoms, the same method
used by the Aladdin and Internet packages on Apple's CD. Package 24000 is
appended to stock hidden package 5000, which is referenced by every Core
System Software choice. Payload files live beside `Installation Tome`, where
Installer's relative source-file resolution expects them.

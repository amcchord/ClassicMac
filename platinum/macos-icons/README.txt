Original Mac OS 9 icons

Copyright Apple Computer, Inc. These Apple assets are separate from the
MIT-licensed fonts, drawings and control code in the other platinum folders.
No Apple font, complete System/Finder resource fork or guest application is
included here. The existing Iconfactory machine artwork remains separate.

The preferred CD, floppy, folder, clipboard, alert, hard-disk and preferences
families are exported by Mac OS 9.2.1 Icon Services inside a disposable public
OS9 fixture. The native reference utility calls GetIconRef with creator 'macs'
and IconRefToIconFamily(kSelectorAllAvailableData). This preserves original
32-bit colour and 8-bit alpha which are absent from some flat System resources.
The selected additional resource members come from the public SuperDeluxe v2
Mac OS 9.2.2 System, Finder 9.2 and Monitors 8.6.3 files, mounted read-only.

manifest.json records source resource-fork hashes, original resource type/ID
or Icon Services OSType, family SHA256 and every member SHA256. Families from
Icon Services are unmodified complete native exports. The other .icns files
contain the appropriate original small/large resource members. No blur,
recolouring, invented alpha, upscaling or downscaling is applied to exports.
Original one-bit masks remain where the native family has no 8-bit alpha.

Build/run platinum/reference/reference.c in a disposable Mac OS 9 guest.
Copy its Native-*.icns files from System Folder/Startup Items after normal
shutdown. Re-extract using platinum/tools/extract_macos_icons.py
--system-folder PATH --icon-services-dir DIR. Regenerate embedded Swift and
desktop PNG exports with no arguments; --check validates all hashes/exports.

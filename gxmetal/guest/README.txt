GXMetal 2.0.1 for Mac OS 9
==========================

GXMetal is ClassicMac's host-accelerated QuickDraw 3D RAVE engine. It lets
compatible Power Mac games send 3D work to Metal on the host Mac while keeping
Apple Software RAVE available as a safe fallback.

This release is fully tested on Mac OS 9.2.2. Mac OS 8.5 and 8.6 support is
still being validated and will receive another pass when updated system
images are available.

INSTALL OR UPDATE
-----------------

1. Start a ClassicMac Power Mac G4 machine in Mac OS 9.
2. Open the ClassicMac Tools CD, then open the GXMetal folder.
3. Double-click Install GXMetal and review the confirmation message.
4. Click Install. The installer safely replaces older GXMetal, GXMetal
   Startup, and GXMetal Input copies in the active System Folder's Extensions
   folder.
5. Restart the emulated Mac.
6. Look for the puzzle-piece M icon in the startup extension row.
7. Run GXMetal Test. Do not rely on the startup icon alone: the test confirms
   the installed GXMetal version, RAVE discovery, the host transport,
   rendering correctness, presentation, and software fallback. GXMetal Test
   2.0.1 rejects a mismatched driver and tells you to reinstall and restart.

Keep the complete GXMetal folder together while the installer runs. GXMetal is
the RAVE driver; GXMetal Startup draws the icon during boot; GXMetal Input is
an InputSprocket bridge that lets games use ClassicMac's seamless mouse. You
can install directly from the read-only Tools CD.

WHAT THE STARTUP ICON MEANS
---------------------------

The puzzle-piece M icon means Mac OS loaded the GXMetal Startup companion. It
does not by itself prove that the host Metal renderer is available. GXMetal
Test must report PASS before you try a game.

SAFE FALLBACK
-------------

GXMetal declines unsupported displays, clip regions, protocol versions, and
host configurations. QuickDraw 3D RAVE can then select Apple Software RAVE.
The conformance test explicitly checks that the software engine remains usable.

UNINSTALL OR RECOVER
--------------------

To remove GXMetal, move System Folder:Extensions:GXMetal, GXMetal Startup, and
GXMetal Input to Disabled Extensions or the Trash, then restart. Apple
Software RAVE remains installed.

If Mac OS ever has trouble starting after an extension change, restart while
holding Shift to disable extensions. Move GXMetal, GXMetal Startup, and GXMetal
Input out of the Extensions folder, then restart normally. Your ClassicMac
disk image and game files are not changed by the host accelerator.

TESTED GAME
-----------

Nanosaur, Carmageddon II, and Quake III Arena Demo are the primary real-game
tests. GXMetal 2.0.1 has been exercised through their title screens and
extended gameplay with multitexturing, lightmaps, textures, depth, clipping,
fog, water, alpha effects, camera movement, HUDs, and dynamic textures.
Rendering bugs should be reported with a screenshot, the Mac OS version,
display resolution and color depth, and whether GXMetal Test passes.

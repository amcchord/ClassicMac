GXMetal 2.2.3 for Mac OS 9
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
   2.2.3 rejects a mismatched driver and tells you to reinstall and
   restart.

Keep the complete GXMetal folder together while the installer runs. GXMetal is
the RAVE driver; GXMetal Startup draws the icon during boot; GXMetal Input is
an InputSprocket mouse that supplies the relative movement and button elements
classic games actually consume. It reads host-relative deltas directly while
active, preserves even very short clicks, and restores ClassicMac's seamless
pointer afterward. You can install directly from the read-only Tools CD.

WHAT THE STARTUP ICON MEANS
---------------------------

The puzzle-piece M icon means Mac OS loaded the GXMetal Startup companion. It
does not by itself prove that the host Metal renderer is available. GXMetal
Test must report PASS before you try a game.

SAFE FALLBACK
-------------

GXMetal accepts bounded complex QuickDraw regions, including disjoint spans and
holes, and uses the same exact clipping for clear, draw, and present. It still
declines unsupported displays, oversized region lists, protocol versions, and
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

TESTED GAMES AND OPENGL
-----------------------

Nanosaur, Carmageddon II, Quake III Arena Demo, and the repository's ten-game
campaign exercise public RAVE, ATI-private RAVE/OpenGL, and Apple OpenGL paths.
GXMetal 2.2.3 has reviewed gameplay evidence for Bugdom, Future Cop, Combat
Mission, Weekend Warrior, Cro-Mag Rally, Dark Vengeance, Myth II, Oni, HAVOC,
and sound-disabled Unreal Tournament. Those routes cover multitexturing,
lightmaps, dynamic textures, depth, clipping, fog, water, alpha effects,
camera movement, HUDs, process restart, and recovery. Quake III additionally
retains five reviewed Q3DM1 views covering its arches and other world geometry.

This evidence is not a guarantee that every RAVE or OpenGL title works. Audio,
long soaks, and clean quit/relaunch are tracked separately per game. Unreal
Tournament currently requires in-game sound to be disabled under the test
VM's audio path; sound-enabled startup also fails with Apple Software RAVE and
is not classified as a GXMetal rendering defect.

GXMetal AGL Probe is included beside GXMetal Test. It requires an accelerated
Apple OpenGL pixel format, renders known geometry, verifies independent MIN,
MAG, and mip filtering with distinct readback colors (including ARB texture
unit 1 when available), checks that glReadPixels leaves the display unchanged,
and tears the context down.
A passing probe proves the core Apple OpenGL/ATI driver path; it does not mean
that every OpenGL game or extension has been qualified.

Rendering bugs should be reported with a screenshot, the Mac OS version,
display resolution and color depth, whether GXMetal Test and GXMetal AGL Probe
pass, and the title of the affected game.

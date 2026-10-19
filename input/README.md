# Game mouse input

InputSprocket games need relative motion and buttons from a device their Mac
OS driver enumerates. The Virtio tablet supplies Finder cursor coordinates;
it does not provide an InputSprocket mouse. Power Mac launches now include a
standard USB HID mouse, supported by the guest's existing Apple/USB Overdrive
software. No game or guest system files are patched.

`classicmac-game-mouse.patch` keeps an active seamless tablet in charge of
Finder even if the USB driver starts polling later. ATI 3D draws with a hidden
hardware cursor select the HID mouse for both motion and buttons and notify
the frontend to offer capture. The ATI Mac driver hides its cursor by parking
it offscreen with CUR_EN still set, so visibility includes position. Restoring
the cursor or resetting the card restores tablet input. Button releases at
handoff avoid leaving a held click on the previous device.

The patch applies after the ATI model/macOS overlay. It changes behavior only
for the explicitly named `usb-mouse,id=classicmac-game-mouse`. The native Mac
view and browser/Electron frontends use the engine's pointer-mode event.

`tests/superdeluxe-quake3.py` uses the native shared-framebuffer and input
protocol against a disposable APFS clone of an independently supplied guest
disk. It captures Finder, menu motion/clicks, arena look/fire, cinematic
cadence/audio, and return to Finder. Guest disks, recorded game audio and
profiling outputs remain local and are not application release assets.

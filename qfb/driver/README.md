# Okay, Solra, what have you done now?

This is the declaration ROM and driver for a nonexistent video card. If you are emulating a Quadra 800 using QEMU (which, let's be honest, who isn't?), and you're using a version that has my paravirtualized NuBus framebuffer in it, you can use this.

Why?

- Any arbitrary resolution up to 3840x2160!
- Any QuickDraw-supported color depth, including Thousands! (missing from the native "macfb" framebuffer)
- Very slightly improved performance!
- Gamma correction! (adding a very slight performance hit that probably balances out the above)
- Multiple monitors!

# How do I use this?

If you're using a build of QEMU that supports this device at all, you already have this firmware. Change `-M q800` to `-M q800,fb=qemu` and it will load up this firmware and you'll be ready to go. Just specify whatever resolution you want with `-g WIDTHxHEIGHTxDEPTH`. (My personal favorite is `-g 960x600x16`.)

If it's not working because it `failed to find romfile "mac_qfb.rom"`, add the path containing `mac_qfb.rom` to the search path with `-L`, or just drop it in the working directory of the emulator.

If you get a message like `Property 'q800-machine.fb' not found`, it's because your build of QEMU doesn't contain the `nubus-qfb` framebuffer device and this repository won't be able to help you make that work.

# Multiple monitors?

If you want to use additional monitors, you can add them with e.g. `-device nubus-qfb,width=960,height=600,depth=16`. You can have up to three `nubus-qfb` devices (including the one created by `fb=qemu`). If you leave the `fb` setting at its default, `fb=mac`, this gets you up to four monitors, but the fourth one will only be able to do one of the three resolutions supported by the built-in video card.

The `-g` option only applies to framebuffers created by the machine (`q800,fb=mac` or `q800,fb=qemu`). For each framebuffer you add with `-device`, you should specify width, height, and depth options. The defaults are `width=640,height=480,depth=8`. If you are adding `nubus-qfb` devices manually, I recommend using `-M q800,fb=none` and eschewing the `-g` option entirely.

# Don't I need to install a driver, or a system extension, or something?

Nope.

The entire driver is included in the ROM. Slot Manager loads it, interrogates it, initializes it, and gets the driver hooked up to QuickDraw, which... draws on it, quickly. Viva plug-and-play.

In theory, you could install a "more recent" driver in the Extensions folder or (heaven forbid) in the System File, and it would replace this driver at runtime. In practice, since this isn't a real card with a real ROM, it's simple and easy just to replace your `mac_qfb.rom` with a freshly-built one.

# How do I build it?

Install [Retro68](https://github.com/autc04/Retro68), put it in your `PATH`, run `make`. Apple's non-redistributable Universal Interfaces aren't needed, and, in fact, the ROM won't even build unmodified if you try to use them.

If you have a recent version of Lua installed, you will end up with `bin/mac_qfb.rom` ready to use.

If you don't have a recent version of Lua installed, you will end up with `bin/mac_qfb.bin` instead. This does not have a valid checksum, but is otherwise completely usable. (See below.)

If you want to build extra debug code, and get way too much information poured onto stderr, uncomment the `DEBUG=1` line in the Makefile and rebuild. Or just grab a debug ROM from the Releases tab on the right.

# Hey, this makes all the graphics too bright!

I think you mean *correct*. Until the release of Mac OS X 10.6 "Snow Leopard", Macintoshes targeted a display gamma of 1.8, as opposed to the 2.2-ish gamma that other systems (including PCs) use. Nearly every Macintosh emulator to date has not included any gamma correction, and thus has shown all the graphics too *dark*. Since this driver actually supports gamma correction, its graphics will seem too bright by comparison.

If it bothers you, you may be able to use the Monitors control panel to set a different gamma curve. Uncorrected or 2.2 will probably look right to you. Feel free to crank up the gamma even more if PAL or old SGI workstations are your jam. Personally, I found that it didn't *feel* like MacOS until I got the gamma tables working.

# Why does it still work if the ROM has a bad checksum?

Slot Manager checks each slot for a valid declaration ROM, and initializes only the ones that pass several checks, including a checksum test. So why, in spite of this, can a `mac_qfb.rom` with an invalid checksum still work?

To explain this, I need to explain a bit about how NuBus video card declaration ROMs work on the Macintosh. To oversimplify greatly, *every single individual supported combination of resolution and pixel depth* must be *individually listed* in the ROM itself. There are ways of getting around this, but none that work on all versions of the System Software. Therefore, in order to support an arbitrary user-specified resolution, QEMU must patch the correct resolution information into the declaration ROM, and in order for that ROM to still count as valid, it must then patch in a correct checksum as well.

As a side effect, if you try to use a `mac_qfb.rom` with an invalid checksum, QEMU will fix the checksum on your behalf. It will print a warning if the ROM checksum is invalid going in, and it will also generally refuse to touch a ROM that doesn't seem valid, but otherwise it will work.

# Hey, this repository illegally includes part of Apple's Universal Interfaces!

Actually, it doesn't.

Retro68 comes bundled with the "Multiversal Interfaces", which were forward-engineered from a reverse-engineered Macintosh emulator. These interfaces have gaps, particularly when it comes to video drivers. The missing interfaces were transcribed and/or translated, by me, from freely-licensed sample code and documentation from Apple.

Sources:

- Apple Computer, Inc. *Designing Cards and Drivers for the Macintosh Family*. 3rd ed., Addison-Wesley, 1992.
- Apple Computer, Inc. *Designing PCI Cards and Drivers for Power Macintosh Computers*. Developer Press, 1996.
- Apple Computer, Inc. *d e v e l o p: The Apple Technical Journal*, January 1990 issue.
- Apple Computer, Inc. *Iɴsɪᴅᴇ Mᴀᴄɪɴᴛᴏsʜ: Devices*. Addison-Wesley, 1994.
- Apple Developer Group, Developer CD Series, Nov/Dec 1992 issue *"Wayne's GWorld"*.

I also present this exchange found in the April 1990 issue of *d e v e l o p*:

> I like *d e v e l o p*. It’s cool. But what’s the deal with the code contained therein (on CD-ROM)? Can we use it? Can we distribute it? Both of those (at least the first) would seem to be the intent of *d e v e l o p*. But the lawyer’s funfest at the back would seem to say otherwise. I wondered about this before I saw the article in MacWeek, but now I’m really confused. Is use of code contained on the CD-ROM as limited as seems to be implied by the CD-ROM container’s text? Or what? —Robert

> You can freely use, copy, and distribute the code that’s included in *d e v e l o p*. Many thanks to Teri Drenker in Apple’s software licensing group for this issue’s revised licensing agreement. —Louella

# Legalese

The driver, declaration ROM, and associated build tools and scripts in this repository, collectively `mac_qfb_driver`, are copyright 2022, Solra Bizna, and licensed under either of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the `mac_qfb_driver` repository by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or restrictions.

## Well, except...

The following files:

- `include/extraverse.h`
- `src/defs.s`

contain information transcribed and/or translated from freely-available Apple documentation and example code, and could be considered to be under the respective copyright.

The following files:

- `include/printf.h`
- `src/printf.c`

are copyright 2014-2019 [Marco Paland](mailto:info@paland.com), PALANDesign Hannover, Germany, and are distributed under the MIT License. (They are included in the build only when debugging is enabled.)

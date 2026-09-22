# Copland Activities

In ClassicMac 3.2.1, choose **File → Download a Mac → Copland D11E4 Activities**
to create a new machine with a 512 MiB disk. Existing machines keep their own
files; downloading this image does not upgrade or replace them.

Open **Activities** on the Copland desktop:

| Activity | What to try |
| --- | --- |
| **Anarcho → Anarcho** | Write notes, save and reopen short text documents. Launch the app once before opening its documents from Finder. |
| **Solitaire → Eric’s Solitaire Sample** | Play cards; Command-N deals a new board. |
| **MineSweeper → MineSweeper** | Double-click to uncover a cell, single-click to mark a mine; Command-N starts another game. |
| **GXSlidemaster** | Use the left/right arrows to explore Apple’s live graphics and typography demonstrations. |
| **Reading** | Open original field notes, a boot-and-save lab, graphics observations and a build/source guide in Anarcho. |
| **My Documents → Notebook** | A small editable notebook for recording experiments. |

The reading guides link to the full original Apple developer documentation for
study on the host Mac. Original author readmes are retained with both added
freeware apps. Apps already on the reference disk stay available under
Applications unless moved into Activities.

Save before leaving an app. **Quitting classic apps can still assert or freeze**;
use ClassicMac’s **Restart** between activities if necessary. Continue Copland
requests a debugger continuation, but it cannot repair every failure. The
persistent clock prevents the old restart-related directory date errors.
This remains an unfinished developer preview, not a reliable place for important
work. Some Control Panels, networking, sound, host clipboard paste, shared folders,
removable media and browser display remain unavailable.

The new image includes a precisely checked D11E4 clipboard compatibility patch:
zero-byte PutClassicScrap and UnloadClassicScrap writes become successful no-ops.
Positive-length transfers keep the original implementation; invalid negative
lengths retain the original error handling. This addresses the empty-clipboard
failure that stopped editors during Open/Save. Other assertions are not hidden.
The untouched v1 image remains available for historical comparison.

## What the other WinWorld versions contain

Inspection of the downloaded archives found:

- **D7E1** contains a Scarecrow system, MacBrowser, an AppearanceCP application
  and Default/Z themes. It offers different early UI experiments. Its
  AppearanceCP cannot simply be copied into D11E4: it requires a different
  FileSystems code fragment. A full D7 boot has not been qualified here.
- **8.0.B5** appears mislabeled as Copland. The nested archive contains a 1997
  Mac OS 8 installer, conventional control panels and 68040 support, consistent
  with the later shipping Mac OS 8 line. More working panels in that build would
  not establish a more complete version of the Copland rewrite.
- **D11E4** is the June 1996 developer system used for this image. Having more
  files installed does not complete its unfinished operating-system APIs.

## Sources and repeatable preparation

- [Michael Steil’s Copland emulator and reference disk](https://www.pagetable.com/300)
- [WinWorld build archives](https://winworldpc.com/product/mac-os-8/copland)
- [Original Mac OS 8 DDK 0.4 CD](https://www.macintoshrepository.org/531-mac-os-8-0-copland-beta-builds-)
- [Anarcho 1.6 preservation archive](https://www.gryphel.com/c/sw/text/anarcho/index.html)
- [MineSweeper preservation archive](https://www.gryphel.com/c/sw/games/mineswee/index.html)

`scripts/prepare-copland-activities.py` checks exact source hashes, rebuilds a
new HFS volume, preserves original catalog IDs/boot blocks/blessings, applies
the checked clipboard patch, imports the complete freeware packages, creates
original reading activities and verifies the copied data/resource forks. It never writes
the source disk or publishes a build. Requirements: macOS, hfsutils, unar and Python. Never run it while another
hfsutils volume/process is active; those tools use global mount state.

```sh
python3 scripts/prepare-copland-activities.py \
  --source copland.img \
  --mines MineSweeper.sit --anarcho anarcho-16.hqx \
  --output output/copland-activities.img
```

A build must still pass real-guest boot, input, file persistence and restart
qualification before publication. No Apple binaries, ROMs or downloaded apps
are committed to this repository.

# Classic OS base images

System 7.5.5 and Mac OS 8.1 use a Quadra 800, 128 MiB, classic HFS and
640 × 480 indexed color. The archive carries the matched Quadra ROM. Mac OS
8.6 uses a G3, 128 MiB, classic HFS, VM Off, a Virtio startup disk, standard
640 × 480 direct color, relative ADB input and RTL8139 Ethernet. These are
prepared base installs with StuffIt Expander, Transmit and Netscape.

Compatible clients read `https://mcchord.net/classicmac/catalog-v2.json`.
Keep the original catalog unchanged: older Swift clients cannot decode Quadra
entries, and older RetroBoot imports discard the family. ClassicMac 4.6 is the
minimum version. RetroBoot also filters by verified runtime capabilities.

## Mac OS 8.6 Ethernet timing

The original inline RTL8139 send/completion path can stall large transfers with
the original Realtek driver on 8.6. A fresh HTTP guest stopped acknowledging at
37,441 bytes despite advertising a nonzero window. Its RX ring drained with no
missed-packet counter or pending interrupt. Finder could still duplicate the
9.5 MB Netscape app. The same 4 MiB ZIP completed through a paced HTTP server
and matched its exact hash after native shutdown. This isolates burst/network
timing; it does not establish a precise guest-driver root cause.

`powermac/mac-os-8-ethernet-timing.patch` adds `rtl8139.tx-delay-ns`, default
zero. The 8.6 profile opts into 1,000,000 ns. A virtual timer sends and completes
each descriptor after submission, preserving FIFO order across four slots.
Reset cancels pending work; unrealize frees the timer. An optional VMState
subsection preserves deadlines, pending slots and the timer. The upstream
inline path remains the default for other guests. This is separate from the
50 ms CUDA idle IRQ used to wake the older Mac OS main task.

Three fresh guests have repeated 4 MiB passive FTP/StuffIt passes with exact
payload comparisons. Two also passed unpaced HTTP. The final source uses
native PowerPC Transmit 1.2 and Netscape Communicator 4.5. Retained private evidence
and final publication checks are recorded in the task worklog and ignored
`output/classic-base-images` directory. No proprietary media belongs in Git.

The source uses the completed retail installer core and native application
installs. A successful full multi-stage CD installer run is not claimed.
Optical startup and multiple auxiliary Virtio volumes on 8.6 remain unqualified.
The base disables Tools, tablet input, sharing and sound. These image defaults
are part of the compatibility profile; changing them needs separate validation.

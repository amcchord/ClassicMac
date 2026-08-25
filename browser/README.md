# ClassicMac browser display

ClassicMac serves this directory from a per-VM, loopback-only HTTP listener.
QEMU exposes its VNC framebuffer through a second loopback-only WebSocket, and
`viewer.js` connects the two in the user's preferred browser.

`novnc/core`, `novnc/vendor/pako`, and `novnc/LICENSE.txt` come from the
upstream noVNC 1.7.0 release. The tracked changes in `core/rfb.js`,
`core/encodings.js`, and `core/display.js` advertise QEMU's
pointer-type-change pseudo-encoding, translate browser pointer-lock deltas into
QEMU's relative RFB coordinates, preserve ClassicMac's optional secondary-click
and scrolling helpers, and prefer whole-number display scaling. Fractional
downscaling uses exact-aspect CSS dimensions where possible and nearest-neighbor
sampling for sharp classic Mac pixels.

When the VNC endpoint is unavailable, the viewer keeps a single waiting
overlay visible across retry attempts and hides transient noVNC display nodes.
The page explains that the machine may be shut down or restarting and resumes
the display automatically when QEMU becomes available again.

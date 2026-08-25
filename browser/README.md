# ClassicMac browser display

ClassicMac serves this directory from a per-VM, loopback-only HTTP listener.
QEMU exposes its VNC framebuffer through a second loopback-only WebSocket, and
`viewer.js` connects the two in the user's preferred browser.

`novnc/core`, `novnc/vendor/pako`, and `novnc/LICENSE.txt` come from the
upstream noVNC 1.7.0 release. The tracked changes in `core/rfb.js` and
`core/encodings.js` advertise QEMU's pointer-type-change pseudo-encoding,
translate browser pointer-lock deltas into QEMU's relative RFB coordinates,
and preserve ClassicMac's optional secondary-click and scrolling helpers.

# Ready-to-run machine downloads

The macOS app loads `https://mcchord.net/classicmac/catalog.json`. Each template
is immutable: publish a new ID, archive URL and checksum when changing its disk
or settings. Catalog refreshes never modify an installed machine.

## Integration

Present `DownloadMachineSheet { url in store.openBundle(at: url, autostart: false) }`
from the library's Download a Mac action. The sheet selects and names a template,
downloads, verifies, installs and dismisses itself after calling the closure.

`VMTemplateMetadata.load(from: config.folder)` reads the imported template's OS
and GXMetal version. This records what was initially supplied, not whether the
guest driver is currently installed, loaded or accelerating a game.

## Publishing a template

Prepare and shut down a clean Power Mac `.classic` package first. Remove private
guest files, recent documents, network credentials, aliases to host folders and
other preparation-machine state inside the guest. Qualify boot, input, sound and
GXMetal on that stopped disk. Packaging does not inspect or scrub HFS contents.

```sh
python3 scripts/package-machine-template.py \
  --bundle /absolute/path/Mac-OS-9.classic \
  --output /absolute/path/mac-os-9.2.1-gxmetal-v1.tar.gz \
  --catalog-output /absolute/path/catalog.json \
  --archive-url https://mcchord.net/classicmac/mac-os-9.2.1-gxmetal-v1.tar.gz \
  --id mac-os-9.2.1-gxmetal-v1 \
  --os-version 'Mac OS 9.2.1' \
  --gxmetal-version 2.3.0
```

Use the actual bundled GXMetal version. Add `--include-preview` only for a
reviewed preview image. The helper creates new files, sanitizes host paths and
IDs in `config.json`, reads the source without modifying it, and reports exact
download size, expanded size and SHA-256. It does not upload files.

Upload the archive first, verify its remote size and checksum, then publish the
catalog. Serve archives as `application/gzip` with `Content-Encoding` absent
(or `identity`), byte ranges enabled, and immutable caching. Serve the catalog
as `application/json` with short/no caching. Redirects must retain HTTPS, host
and port. Redirecting a large archive from `mcchord.net` to another hostname is
intentionally rejected. The archive and catalog must both be available before
exposing the download action in a release.

## Format and bounds

Schema 1 contains `machines`, an array of up to 50 unique entries. Each entry has
`id`, `name`, `summary`, `osVersion`, `gxMetalVersion`, `minimumAppVersion`,
`archiveURL`, `archiveBytes`, `installedBytes`, and lowercase hex `sha256`.
`installedBytes` is exactly the sum of the expanded file sizes, excluding tar
headers and padding. Numeric app versions have two or three components.

An archive is gzip containing a tar stream with flat regular files:

- `config.json` (required; at most 64 KiB; Power Mac G4 only)
- `disk.img` (required; raw disk, positive multiple of 512 bytes)
- `preview.png` (optional; at most 16 MiB)

There is no enclosing directory. GNU positive base-256 sizes support disks over
8 GiB. USTAR octal sizes are also accepted. Links, paths, directories, extended
headers, ownership, permissions, device files, sparse extents, duplicate entries
and additional files are rejected. Total expansion is bounded by the catalog;
both archive and expanded size are capped at 140 GiB. No shell interprets any
archive content. The app verifies SHA-256 in streaming chunks before extraction.

The app saves partial archives under Application Support/ClassicMac/Downloads,
keyed by checksum. Pause or a connection failure retains bytes. Resume validates
Content-Range before appending; a server ignoring Range causes a clean restart.
A checksum failure removes the corrupt archive. Discard Download removes a
partial file. An advisory file lock prevents simultaneous writes to one cache.

Installation checks destination and cache free space, extracts into a private
staging directory on the destination volume, validates the files, constructs a
new VM identity and config, writes provenance, and atomically moves the package
to a unique name. External media/shared-folder paths and boot-from-CD settings
are cleared. Cancellation or failure removes staging; existing machines are
never replaced. The completed archive is removed after a successful import.
The app's bundled ClassicMac Tools volume remains enabled for first startup,
matching the normal Power Mac defaults and providing guest diagnostics/repair.

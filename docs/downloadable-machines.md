# Ready-to-run machine downloads

The macOS app loads `https://mcchord.net/classicmac/catalog.json`. Each template
is immutable: publish a new ID, archive URL and checksum when changing its disk
or settings. Catalog refreshes never modify an installed machine.

## Integration

Present `DownloadMachineSheet { url in store.openBundle(at: url, autostart: false) }`
from the library's Download a Mac action. The sheet selects and names a template,
downloads, verifies, installs and dismisses itself after calling the closure.
The 620 × 620 view can also be embedded in the New Machine wizard with
`DownloadMachineSheet(onBack: goBack, onInstall: openMachine)`. Back is available
only while idle or paused; Cancel remains available, and Escape closes an idle
sheet. A running transfer must first be paused or cancelled.

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
download size, expanded size, guest capacity, initial host storage bound and
SHA-256. It does not upload files. Raw disk capacity is expressed in binary
units: a 32 GB machine has a 32 GiB (`34,359,738,368` byte) raw disk. The guest
filesystem has slightly less usable space after its partition/filesystem data.

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
headers and padding, including every zero byte in a sparse raw disk. Numeric
app versions have two or three components.

Two optional schema-1 fields are supported by ClassicMac 3.0.1:

- `diskCapacityBytes`: the exact logical length of `disk.img`, a positive
  multiple of 512 bytes. Other expanded members may total at most 16 MiB +
  64 KiB; capacity never changes the full `installedBytes` expansion bound.
- `requiredStorageBytes`: a conservative initial allocation bound, in whole
  1 MiB units. This requires `diskCapacityBytes`. Count each nonzero 1 MiB disk
  chunk as 1 MiB, including a partial last chunk; count each all-zero chunk as
  zero. Add the individually rounded-up 1 MiB sizes of config and preview.
  The packager computes this while reading the exact archived bytes. Source
  filesystem allocation (`st_blocks`) is deliberately irrelevant, because an
  overallocated source can still import sparsely.

The importer uses the smaller bound only on APFS volumes that report sparse
support and a compatible allocation size. It creates a raw `.img`, skips zero
chunks and explicitly punches their holes; APFS otherwise allocates some small
seek gaps on file close. Before writing each nonzero chunk, it enforces the
catalog's storage budget, and it checks actual allocation while extracting and
after rewriting settings/provenance. An understated budget rejects the import
and removes staging. Other filesystems and older catalogs retain the full-size
space requirement. All paths keep the exact full expanded-size limit.

Free-space checks include a 256 MiB destination reserve plus the compressed
archive cache requirement (combined when on the same volume). The UI separates
download size, guest capacity and initial host use; the raw disk grows on the
host as the guest writes files, up to its capacity. Guest deletion does not
promise to reclaim host allocation.

Archives retain the original flat regular tar format, so the packager's default
`minimumAppVersion` stays `3.0.0`. ClassicMac 3.0 ignores the optional fields and
can import a 32 GiB disk, but still requires enough free space for the full
expanded size. Accurate smaller storage guidance and the enforced sparse
allocation contract require 3.0.1.

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

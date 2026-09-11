# ClassicMac OS 9 download hosting

## Target

Austin authorized hosting the configured OS 9 template on the existing
50day.io server under mcchord.net. AustinLand's live inventory identifies this
as Linode instance 8852533, `50DayEdge`, `172.104.216.89`. Both domains already
resolve to that host. No DNS, firewall, or virtual-host changes are required.

- Site: `https://mcchord.net`
- Apache document root: `/var/www/mcchord.net`
- Catalog: `https://mcchord.net/classicmac/catalog.json`
- Download directory: `/var/www/mcchord.net/classicmac`
- Existing HTTPS vhost: `/etc/apache2/sites-enabled/001-mcchord.net-le-ssl.conf`
- Existing service: Apache 2.4 on Ubuntu 22.04.
- Access: existing SSH authorization; never place keys in this repository.

The server had 277 GB free during preflight. HTTPS, a valid certificate, and
byte-range support were verified before any write. The `classicmac` path did
not exist at preflight.

## Artifact preparation

`scripts/prepare-os9-template.py` creates a new filesystem using an audited
local OS 9 installation as its system-file source. Its source is mounted
read-only. The source filesystem and free space are not cloned. Source
preferences, documents, caches, keychains, recent-item lists, and host paths
are excluded; the installer adds the current guest GXMetal binaries.

The first template uses verified **Mac OS 9.2.1**, guest GXMetal **2.3.0**,
512 MB RAM, an 8 GB virtual disk, and 1024×768 Thousands of colors. The OS
version is read from the System file's version resource. The OS9 System Folder
blessing uses Apple's documented HFS Plus finderInfo fields, updating both
volume headers after the image is unmounted. See
<https://developer.apple.com/library/archive/technotes/tn/tn1150.html>.

Public template release requires a review of the selected system/software
files and the applicable distribution rights. No commercial games or user
registration files belong in the template. Keep guest compatibility-test
images in the ignored local evidence area; never publish those images.

## Publish and rollback

Publish only qualified versioned archives. Upload to a temporary filename,
verify its size and SHA-256 on the server, then rename it to its final immutable
name. Promote the catalog last with an atomic same-directory rename. Do not
replace an existing immutable archive with different bytes.

Before later catalog changes, retain the old catalog in local release evidence
and a non-public server backup location. Rollback restores that catalog;
existing user machines are never modified. Initial rollback can remove the
catalog from service while retaining the archive for investigation.

After publication verify HTTPS catalog retrieval, archive size, checksum, an
HTTP Range request returning 206, a complete fresh download/import, and boot
from that imported package. No Apache restart is needed for static files.

Record actual publication paths, hashes, validation, and outcomes in the
session journal and release evidence.

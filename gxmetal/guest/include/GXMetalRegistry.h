/*
 * Name Registry handoff from the Power Mac VGA NDRV to the GXMetal tnsl.
 *
 * Classic Mac OS uses one shared 32-bit logical address space, so the display
 * driver can publish the Expansion Manager's logical PCI mappings for use by
 * the RAVE engine. This record is guest-native (big-endian), not part of the
 * little-endian guest/host command wire format.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GXMETAL_REGISTRY_H
#define GXMETAL_REGISTRY_H

#include <stdint.h>

#include "gxmetal_protocol.h"

#define GXMETAL_REGISTRY_PROPERTY       "AAPL,GXMetal"
#define GXMETAL_REGISTRY_VERSION_MAJOR  1u
#define GXMETAL_REGISTRY_VERSION_MINOR  0u
#define GXMETAL_REGISTRY_VERSION \
    ((GXMETAL_REGISTRY_VERSION_MAJOR << 16) | \
     GXMETAL_REGISTRY_VERSION_MINOR)

typedef struct GXMetalRegistryInfo {
    uint32_t magic;
    uint32_t version;
    uint32_t registers_address;
    uint32_t registers_bytes;
    uint32_t shared_address;
    uint32_t shared_bytes;
    uint32_t framebuffer_address;
    uint32_t framebuffer_bytes;
} GXMetalRegistryInfo;

#endif /* GXMETAL_REGISTRY_H */

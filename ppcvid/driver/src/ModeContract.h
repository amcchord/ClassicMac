#ifndef __QEMU_VGA_MODE_CONTRACT_H__
#define __QEMU_VGA_MODE_CONTRACT_H__

/*
 * Pure helpers shared by the NDRV and its host-side regression tests.
 *
 * Classic Mac depth modes are ordinal, not literal bit depths.  The first
 * mode is supplied by the caller so this header does not need the classic
 * Mac SDK headers and can also be compiled by the host test toolchain.
 */
static unsigned long
QemuVgaDepthToDepthMode(unsigned long packedLowDepths,
                        unsigned long depth,
                        unsigned long firstDepthMode)
{
    if (packedLowDepths) {
        switch (depth) {
        case 1:
            return firstDepthMode;
        case 2:
            return firstDepthMode + 1;
        case 4:
            return firstDepthMode + 2;
        case 8:
            return firstDepthMode + 3;
        case 15:
        case 16:
            return firstDepthMode + 4;
        case 24:
        case 32:
            return firstDepthMode + 5;
        default:
            return 0;
        }
    }

    switch (depth) {
    case 8:
        return firstDepthMode;
    case 15:
    case 16:
        return firstDepthMode + 1;
    case 24:
    case 32:
        return firstDepthMode + 2;
    default:
        return 0;
    }
}

static unsigned long
QemuVgaDepthModeToDepth(unsigned long packedLowDepths,
                        unsigned long depthMode,
                        unsigned long firstDepthMode)
{
    if (depthMode < firstDepthMode)
        return 0;

    depthMode -= firstDepthMode;
    if (packedLowDepths) {
        switch (depthMode) {
        case 0:
            return 1;
        case 1:
            return 2;
        case 2:
            return 4;
        case 3:
            return 8;
        case 4:
            return 15;
        case 5:
            return 32;
        default:
            return 0;
        }
    }

    switch (depthMode) {
    case 0:
        return 8;
    case 1:
        return 15;
    case 2:
        return 32;
    default:
        return 0;
    }
}

static unsigned long
QemuVgaStorageDepth(unsigned long depth)
{
    /* DISPI depth 15 is RGB555 stored in a complete 16-bit word. */
    return depth == 15 ? 16 : depth;
}

/*
 * Calculate a mode's row bytes, page size and supported page count.
 * Returns zero for unsupported depths, empty modes, arithmetic overflow, or
 * a mode that does not fit in the mapped framebuffer.  The NDRV intentionally
 * exposes at most two pages even when more would fit.
 */
static int
QemuVgaCalculatePageLayout(unsigned long width,
                           unsigned long height,
                           unsigned long depth,
                           unsigned long framebufferBytes,
                           unsigned long *rowBytes,
                           unsigned long *pageSize,
                           unsigned long *pageCount)
{
    unsigned long storageDepth;
    unsigned long rowBits;
    unsigned long bytes;

    switch (depth) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 15:
    case 16:
    case 24:
    case 32:
        break;
    default:
        return 0;
    }

    if (width == 0 || height == 0 || framebufferBytes == 0)
        return 0;

    storageDepth = QemuVgaStorageDepth(depth);
    if (width > (~0UL - 7) / storageDepth)
        return 0;
    rowBits = width * storageDepth;
    bytes = (rowBits + 7) / 8;
    if (bytes == 0 || height > framebufferBytes / bytes)
        return 0;

    if (rowBytes)
        *rowBytes = bytes;
    bytes *= height;
    if (pageSize)
        *pageSize = bytes;
    if (pageCount)
        *pageCount = bytes <= framebufferBytes / 2 ? 2 : 1;
    return 1;
}

#endif /* __QEMU_VGA_MODE_CONTRACT_H__ */

#ifndef __GAMMA_CONTRACT_H__
#define __GAMMA_CONTRACT_H__

#include <stddef.h>

enum {
	QEMU_VGA_GAMMA_CHANNELS = 3,
	QEMU_VGA_GAMMA_ENTRIES = 256,
	QEMU_VGA_GAMMA_BITS = 8,
	QEMU_VGA_GAMMA_DATA_BYTES =
		QEMU_VGA_GAMMA_CHANNELS * QEMU_VGA_GAMMA_ENTRIES,
	QEMU_VGA_GAMMA_DATA_OFFSET = 12,
	QEMU_VGA_GAMMA_COMPAT_HEADER_BYTES = 14,
	QEMU_VGA_GAMMA_STORAGE_BYTES =
		QEMU_VGA_GAMMA_COMPAT_HEADER_BYTES + QEMU_VGA_GAMMA_DATA_BYTES
};

/* GammaTbl declares the variable payload as a one-element array of shorts.
 * The extra byte array both owns the remaining payload and preserves the two
 * compatibility bytes included by classic clients' `14 + data bytes` sizing.
 */
typedef struct QemuVgaGammaStorage {
	signed short gVersion;
	signed short gType;
	signed short gFormulaSize;
	signed short gChanCnt;
	signed short gDataCnt;
	signed short gDataWidth;
	signed short gFormulaData[1];
	unsigned char trailing[QEMU_VGA_GAMMA_DATA_BYTES];
} QemuVgaGammaStorage;

typedef char QemuVgaGammaShortMustBeTwoBytes[
	(sizeof(signed short) == 2) ? 1 : -1];
typedef char QemuVgaGammaDataOffsetMustBeTwelve[
	(offsetof(QemuVgaGammaStorage, gFormulaData) ==
	 QEMU_VGA_GAMMA_DATA_OFFSET) ? 1 : -1];
typedef char QemuVgaGammaStorageMustMatchClassicSize[
	(sizeof(QemuVgaGammaStorage) == QEMU_VGA_GAMMA_STORAGE_BYTES) ? 1 : -1];

static inline void QemuVgaGammaInitializeIdentity(
	QemuVgaGammaStorage *storage)
{
	unsigned char *data = (unsigned char *)storage->gFormulaData;
	unsigned long i;

	storage->gVersion = 0;
	storage->gType = 0;
	storage->gFormulaSize = 0;
	storage->gChanCnt = QEMU_VGA_GAMMA_CHANNELS;
	storage->gDataCnt = QEMU_VGA_GAMMA_ENTRIES;
	storage->gDataWidth = QEMU_VGA_GAMMA_BITS;
	for (i = 0; i < QEMU_VGA_GAMMA_ENTRIES; i++) {
		data[i] = (unsigned char)i;
		data[QEMU_VGA_GAMMA_ENTRIES + i] = (unsigned char)i;
		data[2 * QEMU_VGA_GAMMA_ENTRIES + i] = (unsigned char)i;
	}
}

#endif

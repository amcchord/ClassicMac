#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "GammaContract.h"

static void test_classic_gamma_table_layout(void)
{
	assert(offsetof(QemuVgaGammaStorage, gFormulaData) == 12);
	assert(sizeof(QemuVgaGammaStorage) == 14 + 3 * 256);
}

static void test_identity_gamma_table(void)
{
	QemuVgaGammaStorage storage;
	unsigned char *data = (unsigned char *)storage.gFormulaData;
	unsigned int channel;
	unsigned int entry;

	memset(&storage, 0xa5, sizeof(storage));
	QemuVgaGammaInitializeIdentity(&storage);

	assert(storage.gVersion == 0);
	assert(storage.gType == 0);
	assert(storage.gFormulaSize == 0);
	assert(storage.gChanCnt == 3);
	assert(storage.gDataCnt == 256);
	assert(storage.gDataWidth == 8);
	for (channel = 0; channel < 3; channel++)
		for (entry = 0; entry < 256; entry++)
			assert(data[channel * 256 + entry] == entry);

	/* The two compatibility bytes are outside the advertised payload. */
	assert(data[3 * 256] == 0xa5);
	assert(data[3 * 256 + 1] == 0xa5);
}

int main(void)
{
	test_classic_gamma_table_layout();
	test_identity_gamma_table();
	puts("ppcvid gamma contract: PASS");
	return 0;
}

#include "GrayPattern.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  const unsigned depths[] = {1, 2, 4, 8, 16, 32};
  unsigned checks = 0;
  for (unsigned d = 0; d < sizeof(depths) / sizeof(depths[0]); ++d)
    for (unsigned width = 512; width <= 3840; ++width) {
      unsigned rowbytes = ((width * depths[d] + 31) / 8) & ~3u;
      unsigned words = rowbytes / 4;
      uint32_t *allocation = malloc((words + 4) * sizeof(uint32_t));
      assert(allocation);
      allocation[0] = allocation[1] = allocation[words + 2] =
          allocation[words + 3] = 0xdeadbeef;
      qfb_fill_pattern_row(allocation + 2, rowbytes, 0x12345678, 0x90abcdef);
      assert(allocation[0] == 0xdeadbeef && allocation[1] == 0xdeadbeef);
      assert(allocation[words + 2] == 0xdeadbeef &&
             allocation[words + 3] == 0xdeadbeef);
      for (unsigned i = 0; i < words; ++i)
        assert(allocation[i + 2] == ((i & 1) ? 0x90abcdef : 0x12345678));
      free(allocation);
      ++checks;
    }
  printf("Passed %u bounded gray scanlines across all widths and six depths.\n",
         checks);
  return 0;
}

/* Bounded scanline fill shared by the ROM and its host regression test. */
#ifndef QFB_GRAY_PATTERN_H
#define QFB_GRAY_PATTERN_H
#include <stdint.h>
static inline void qfb_fill_pattern_row(volatile uint32_t *pixels,
                                        uint32_t rowbytes, uint32_t a,
                                        uint32_t b) {
  for (uint32_t word = 0; word < rowbytes / sizeof(uint32_t); ++word)
    pixels[word] = (word & 1) ? b : a;
}
#endif

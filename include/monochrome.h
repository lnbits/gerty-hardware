#pragma once
#include <cstddef>
#include <cstdint>

namespace Monochrome {
// MSB first, 1 = white, as used by the UC8179 controller. Exact black/white
// survives dithering, including text and QR codes already rendered by LNbits.
inline void writeRow(uint8_t *buffer, int width, int y,
                     const uint16_t *pixels, bool dither) {
  static const uint8_t bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const size_t stride = (width + 7) / 8;
  for (int x = 0; x < width; ++x) {
    const uint16_t p = pixels[x];
    const int r = ((p >> 11) & 31) * 255 / 31;
    const int g = ((p >> 5) & 63) * 255 / 63;
    const int b = (p & 31) * 255 / 31;
    const int gray = (77 * r + 150 * g + 29 * b + 128) >> 8;
    const int threshold = dither ? bayer[y & 3][x & 3] * 16 + 8 : 128;
    uint8_t &byte = buffer[y * stride + x / 8];
    const uint8_t mask = 0x80 >> (x & 7);
    if (gray >= threshold) byte |= mask;
    else byte &= ~mask;
  }
}
}

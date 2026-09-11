#pragma once
#include <cstdint>

// Canvas rotation 1 stores landscape (x,y) at x*height + height-1-y.
// Keep the old canvas separate so every animation frame starts from the same image.
inline void composeSlide(uint16_t *out, const uint16_t *oldCanvas,
                         const uint16_t *nextImage, int width, int height, int shift) {
  if (shift < 0) shift = 0;
  if (shift > width) shift = width;
  for (int x = 0; x < width; ++x) {
    int sourceX = x + shift;
    for (int y = 0; y < height; ++y) {
      out[x * height + height - 1 - y] = sourceX < width
          ? oldCanvas[sourceX * height + height - 1 - y]
          : nextImage[y * width + sourceX - width];
    }
  }
}

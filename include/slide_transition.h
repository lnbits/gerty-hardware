#pragma once
#include <cstdint>

enum class SlideLayout { Landscape, RotatedPortrait };

// The 3.5-inch canvas uses rotation 1; the 4.3-inch canvas uses row-major pixels.
// Keep the old canvas separate so every animation frame starts from the same image.
inline void composeSlide(uint16_t *out, const uint16_t *oldCanvas,
                         const uint16_t *nextImage, int width, int height, int shift,
                         SlideLayout layout = SlideLayout::RotatedPortrait) {
  if (shift < 0) shift = 0;
  if (shift > width) shift = width;
  for (int x = 0; x < width; ++x) {
    int sourceX = x + shift;
    for (int y = 0; y < height; ++y) {
      int destination = layout == SlideLayout::Landscape
          ? y * width + x : x * height + height - 1 - y;
      int source = layout == SlideLayout::Landscape
          ? y * width + sourceX : sourceX * height + height - 1 - y;
      out[destination] = sourceX < width
          ? oldCanvas[source]
          : nextImage[y * width + sourceX - width];
    }
  }
}

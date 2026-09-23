#pragma once
#include <stdint.h>
#include "thinking_badge.h"

namespace ThinkingOverlay {
constexpr int WIDTH = 64, HEIGHT = 48, MARGIN = 8;
inline int left(int width) { return width - WIDTH - MARGIN; }
inline int top(int height) { return height - HEIGHT - MARGIN; }

inline void copyRgbCorner(const uint16_t *pixels, int width, int height,
                          bool rotated, uint16_t *saved) {
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      const int sx = left(width) + x, sy = top(height) + y;
      saved[y * WIDTH + x] = pixels[rotated
          ? sx * height + height - 1 - sy : sy * width + sx];
    }
  }
}

template <typename Canvas>
void draw(Canvas &canvas, int width, int height, bool smooth,
          const uint16_t *background = nullptr) {
  const int x = left(width), y = top(height);
  for (int row = 0; row < HEIGHT; ++row) {
    for (int col = 0; col < WIDTH; ++col) {
      const int index = row * WIDTH + col;
      const uint8_t alpha = THINKING_BADGE_ALPHA[index];
      if (alpha == 0 || (!smooth && alpha < 128)) continue;
      const uint8_t gray = THINKING_BADGE[index];
      uint16_t colour = smooth
          ? uint16_t(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3))
          : (gray >= 128 ? 0xFFFF : 0);
      if (smooth && alpha < 255 && background) {
        const uint16_t old = background[index];
        // The outside edge is black with fractional coverage.
        const int inverse = 255 - alpha;
        colour = (((((old >> 11) & 31) * inverse + 127) / 255) << 11) |
                 (((((old >> 5) & 63) * inverse + 127) / 255) << 5) |
                 (((old & 31) * inverse + 127) / 255);
      }
      canvas.fillRect(x + col, y + row, 1, 1, colour);
    }
  }
}
}

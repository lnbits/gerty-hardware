#pragma once
#include <stdint.h>

namespace StartingScreen {
// Pixel glyphs for ¯\_(ツ)_/¯. Draw explicitly because the built-in LCD and
// e-paper fonts do not all contain the Japanese character or the overbar.
constexpr uint8_t GLYPHS[9][11] = {
    {0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 64, 32, 32, 16, 16, 8, 8, 4, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0},
    {4, 8, 16, 16, 16, 16, 16, 16, 16, 8, 4},
    {0, 0, 0, 36, 36, 0, 2, 4, 8, 48, 0},
    {16, 8, 4, 4, 4, 4, 4, 4, 4, 8, 16},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0},
    {0, 1, 2, 2, 4, 4, 8, 8, 16, 0, 0},
    {0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

template <typename Canvas>
void draw(Canvas &canvas, int width, int height) {
  const int scale = width / 80;
  const int left = (width - 71 * scale) / 2;
  const int top = (height - 11 * scale) / 2;
  canvas.fillScreen(0xFFFF);
  for (int glyph = 0; glyph < 9; ++glyph) {
    for (int row = 0; row < 11; ++row) {
      for (int column = 0; column < 7; ++column) {
        if (GLYPHS[glyph][row] & (64 >> column)) {
          canvas.fillRect(left + (glyph * 8 + column) * scale,
                          top + row * scale, scale, scale, 0x0000);
        }
      }
    }
  }
}
}

#pragma once
#include "expression_bitmaps.h"
#if defined(GERTY_WAVESHARE_C6)
#include "expression_lcd_240x240.h"
#elif defined(GERTY_JC4827W543)
#include "expression_lcd_480x272.h"
#elif defined(GERTY_GUITION)
#include "expression_lcd_480x320.h"
#endif

namespace StartingScreen {
// Render horizontal runs, requiring only fillScreen/fillRect on every driver.
// Scale uniformly so the face remains undistorted on square and wide panels.
template <typename Canvas>
void draw(Canvas &canvas, int width, int height,
          Expressions::Face face = Expressions::Face::Happy) {
#if defined(GERTY_GUITION) || defined(GERTY_WAVESHARE_C6)
  // Native-resolution grayscale runs avoid nearest-neighbor scaling and require
  // no extra framebuffer, including on the C6 without PSRAM.
  const uint8_t *run = LcdExpressions::FACES[static_cast<unsigned>(face)];
  canvas.fillScreen(0xFFFF);
  for (int y = 0; y < LcdExpressions::HEIGHT; ++y) {
    for (int x = 0; x < LcdExpressions::WIDTH;) {
      const int length = int(run[0]) + 1;
      const uint8_t gray = run[1];
      const uint16_t colour = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3);
      if (gray != 255) canvas.fillRect(x, y, length, 1, colour);
      x += length;
      run += 2;
    }
  }
#else
  const int drawWidth = width * Expressions::HEIGHT <= height * Expressions::WIDTH
      ? width : height * Expressions::WIDTH / Expressions::HEIGHT;
  const int drawHeight = drawWidth * Expressions::HEIGHT / Expressions::WIDTH;
  const int left = (width - drawWidth) / 2;
  const int top = (height - drawHeight) / 2;
  const auto &bitmap = Expressions::BITMAPS[static_cast<unsigned>(face)];
  canvas.fillScreen(0xFFFF);
  for (int y = 0; y < drawHeight; ++y) {
    const int sourceY = y * Expressions::HEIGHT / drawHeight;
    int run = -1;
    for (int x = 0; x <= drawWidth; ++x) {
      const int sourceX = x * Expressions::WIDTH / drawWidth;
      const bool black = x < drawWidth &&
          (bitmap[sourceY * (Expressions::WIDTH / 8) + sourceX / 8] &
           (0x80 >> (sourceX & 7)));
      if (black && run < 0) run = x;
      if (!black && run >= 0) {
        canvas.fillRect(left + run, top + y, x - run, 1, 0x0000);
        run = -1;
      }
    }
  }
#endif
}
}

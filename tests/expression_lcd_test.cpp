#include <cassert>
#include <cstdint>
#include <vector>
#include "starting_screen.h"

struct Canvas {
  std::vector<uint16_t> pixels;
  Canvas() : pixels(LcdExpressions::WIDTH * LcdExpressions::HEIGHT, 0) {}
  void fillScreen(uint16_t colour) {
    for (auto &pixel : pixels) pixel = colour;
  }
  void fillRect(int x, int y, int width, int height, uint16_t colour) {
    assert(x >= 0 && y >= 0 && width > 0 && height > 0);
    assert(x + width <= LcdExpressions::WIDTH);
    assert(y + height <= LcdExpressions::HEIGHT);
    for (int row = y; row < y + height; ++row)
      for (int col = x; col < x + width; ++col)
        pixels[row * LcdExpressions::WIDTH + col] = colour;
  }
};
int main() {
  Canvas canvas;
  for (unsigned face = 0; face < 15; ++face) {
    StartingScreen::draw(canvas, LcdExpressions::WIDTH, LcdExpressions::HEIGHT,
                         static_cast<Expressions::Face>(face));
    unsigned black = 0, gray = 0, white = 0;
    for (auto pixel : canvas.pixels) {
      if (pixel == 0) ++black;
      else if (pixel == 0xFFFF) ++white;
      else ++gray;
    }
    assert(black > 100 && gray > 100 && white > canvas.pixels.size() / 2);
    assert(canvas.pixels.front() == 0xFFFF && canvas.pixels.back() == 0xFFFF);
  }
}

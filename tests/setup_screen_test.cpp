#include <cassert>
#include <cstring>
#include <cstdlib>
#include "setup_screen.h"
#include "starting_screen.h"

// Check the actual drawing commands against the built-in LCD font dimensions.
struct Canvas {
  int width, height, size = 1, x = 0, y = 0, lines = 0;
  int minX = 10000, minY = 10000, maxX = 0, maxY = 0;
  void fillScreen(int) {}
  void fillRect(int left, int top, int w, int h, int) {
    assert(left >= 0 && top >= 0 && left + w <= width && top + h <= height);
    if (left < minX) minX = left;
    if (top < minY) minY = top;
    if (left + w > maxX) maxX = left + w;
    if (top + h > maxY) maxY = top + h;
  }
  void setTextWrap(bool wrap) { assert(!wrap); }
  void setTextColor(int) {}
  void setTextSize(int value) { size = value; }
  void setCursor(int left, int top) { x = left; y = top; }
  void print(const char *text) {
    assert(x >= 0 && y >= 0);
    assert(x + int(strlen(text)) * 6 * size <= width);
    assert(y + 8 * size <= height);
    ++lines;
  }
};

int main() {
  Canvas small{240, 240};
  Canvas guition35{480, 320};
  Canvas guition43{480, 272};
  SetupScreen::drawLcd(small, true);
  SetupScreen::drawLcd(guition35, false);
  SetupScreen::drawLcd(guition43, false);
  assert(small.lines == 9 && guition35.lines == 9 && guition43.lines == 9);
  Canvas epaper{960, 540};
  Canvas seeed{800, 480};
  SetupScreen::drawLcd(seeed, false);
  assert(seeed.lines == 9);
  Canvas *screens[] = {&small, &guition35, &guition43, &epaper, &seeed};
  for (Canvas *screen : screens) {
    StartingScreen::draw(*screen, screen->width, screen->height);
    assert(screen->maxX > screen->minX && screen->maxY > screen->minY);
    for (unsigned face = 0; face < 15; ++face) {
      screen->minX = screen->minY = 10000;
      screen->maxX = screen->maxY = 0;
      StartingScreen::draw(*screen, screen->width, screen->height,
                           static_cast<Expressions::Face>(face));
      assert(screen->maxX - screen->minX > screen->width / 2);
      assert(screen->maxY - screen->minY > screen->height / 4);
      assert(abs(screen->minX + screen->maxX - screen->width) <= 3);
      assert(abs(screen->minY + screen->maxY - screen->height) <= 3);
    }
  }
}

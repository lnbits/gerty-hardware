#include <cassert>
#include <cstring>
#include "setup_screen.h"

// Check the actual drawing commands against the built-in LCD font dimensions.
struct Canvas {
  int width, height, size = 1, x = 0, y = 0, lines = 0;
  void fillScreen(int) {}
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
}

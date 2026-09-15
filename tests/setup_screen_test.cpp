#include <cassert>
#include <cstring>
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
  assert(SetupScreen::remainingSeconds(0) == 60);
  assert(SetupScreen::remainingSeconds(999) == 60);
  assert(SetupScreen::remainingSeconds(1000) == 59);
  assert(SetupScreen::remainingSeconds(59999) == 1);
  assert(SetupScreen::remainingSeconds(60000) == 0);
  assert(SetupScreen::remainingSeconds(61000) == 0);
  for (unsigned seconds = 60; seconds > 0; --seconds) {
    SetupScreen::drawCountdown(small, true, seconds);
    SetupScreen::drawCountdown(guition35, false, seconds);
    SetupScreen::drawCountdown(guition43, false, seconds);
  }
  // Clear text-area bounds before testing the shrug's centering.
  small.minX = guition35.minX = guition43.minX = 10000;
  small.minY = guition35.minY = guition43.minY = 10000;
  small.maxX = guition35.maxX = guition43.maxX = 0;
  small.maxY = guition35.maxY = guition43.maxY = 0;
  Canvas epaper{960, 540};
  Canvas *screens[] = {&small, &guition35, &guition43, &epaper};
  for (Canvas *screen : screens) {
    StartingScreen::draw(*screen, screen->width, screen->height);
    assert(screen->maxX - screen->minX > screen->width * 4 / 5);
    assert(screen->minX + screen->maxX >= screen->width - 1);
    assert(screen->minX + screen->maxX <= screen->width + 1);
    assert(screen->minY + screen->maxY >= screen->height - 1);
    assert(screen->minY + screen->maxY <= screen->height + 1);
  }
}

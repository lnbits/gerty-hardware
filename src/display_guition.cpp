#ifdef GERTY_GUITION
#include "display.h"
#include "logging.h"
#include <Arduino_GFX_Library.h>

namespace Display {
// JC3248W535: native portrait AXS15231B; the canvas rotates in software.
static Arduino_ESP32QSPI bus(45, 47, 21, 48, 40, 39);
static Arduino_AXS15231B panel(&bus, GFX_NOT_DEFINED, 0, false, 320, 480);
static Arduino_Canvas canvas(320, 480, &panel, 0, 0, 1);
static bool ready = false;
static int errorWidth = 0;
constexpr int BACKLIGHT = 1;

bool begin() {
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, LOW);
  ready = canvas.begin(40000000);
  if (!ready) return false;
  canvas.fillScreen(0xFFFF);
  canvas.flush();
  digitalWrite(BACKLIGHT, HIGH);
  LOG_INFO("Guition LCD ready: %dx%d colour, deep sleep disabled", WIDTH, HEIGHT);
  return true;
}

void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  memcpy(buffer + y * WIDTH * sizeof(uint16_t), pixels, WIDTH * sizeof(uint16_t));
}

bool present(uint8_t *buffer) {
  if (!ready) return false;
  // The downloaded frame is fully decoded before touching the visible canvas.
  canvas.draw16bitRGBBitmap(0, 0, reinterpret_cast<uint16_t *>(buffer), WIDTH, HEIGHT);
  canvas.flush();
  errorWidth = 0;
  LOG_INFO("LCD colour frame transferred");
  return true;
}

bool showError(const char *message) {
  if (!ready) return false;
  canvas.setTextSize(2);
  canvas.setTextWrap(false);
  canvas.setTextColor(0x0000);
  // Built-in font: 12x16 pixels at size 2; keep long messages on one line.
  String text(message);
  if (text.length() > 38) text = text.substring(0, 35) + "...";
  int width = text.length() * 12;
  errorWidth = max(errorWidth, width + 16);
  canvas.fillRect(WIDTH - errorWidth, HEIGHT - 32, errorWidth, 32, 0xFFFF);
  canvas.setCursor(WIDTH - width - 8, HEIGHT - 24);
  canvas.print(text);
  canvas.flush();
  return true;
}

// The LCD and backlight must remain powered between API checks.
void idle() {}
}
#endif

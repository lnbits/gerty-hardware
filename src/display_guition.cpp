#ifdef GERTY_GUITION
#include "display.h"
#include "logging.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "tap_gate.h"

namespace Display {
// JC3248W535: native portrait AXS15231B; the canvas rotates in software.
static Arduino_ESP32QSPI bus(45, 47, 21, 48, 40, 39);
static Arduino_AXS15231B panel(&bus, GFX_NOT_DEFINED, 0, false, 320, 480);
static Arduino_Canvas canvas(320, 480, &panel, 0, 0, 1);
static bool ready = false;
static int errorWidth = 0;
constexpr int BACKLIGHT = 1;
static bool touchReady = false;
static TapGate touchGate;
constexpr uint8_t TOUCH_ADDRESS = 0x3B;

bool begin() {
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, LOW);
  ready = canvas.begin(40000000);
  if (!ready) return false;
  canvas.fillScreen(0xFFFF);
  canvas.flush();
  digitalWrite(BACKLIGHT, HIGH);
  pinMode(3, INPUT_PULLUP); // AXS15231B interrupt; polling also detects release.
  touchReady = Wire.begin(4, 8, 400000);
  Wire.setTimeOut(20);
  if (touchReady) {
    Wire.beginTransmission(TOUCH_ADDRESS);
    touchReady = Wire.endTransmission() == 0;
  }
  LOG_INFO("Guition touch %s", touchReady ? "ready" : "unavailable");
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

bool nextPageTapped() {
  if (!touchReady) return false;
  static const uint8_t command[] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 8};
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(command, sizeof(command));
  if (Wire.endTransmission() != 0) return false;
  uint8_t data[8];
  if (Wire.requestFrom(TOUCH_ADDRESS, uint8_t(sizeof(data))) != sizeof(data)) return false;
  for (auto &value : data) value = Wire.read();
  uint16_t x = ((data[2] & 0x0F) << 8) | data[3];
  uint16_t y = ((data[4] & 0x0F) << 8) | data[5];
  bool down = data[0] == 0 && data[1] > 0 && data[1] <= 5 && x < 320 && y < 480;
  return touchGate.update(down, millis());
}
}
#endif

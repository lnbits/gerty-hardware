#ifdef GERTY_WAVESHARE_C6
#include "display.h"
#include "setup_screen.h"
#include "logging.h"
#include <Adafruit_ST7789.h>
#include <SPI.h>

namespace Display {
// Waveshare's official ESP32-C6-LCD-1.3 demo pinout.
static Adafruit_ST7789 panel(&SPI, 14, 15, 21);
static bool ready = false;
static int errorWidth = 0;

bool begin() {
  pinMode(22, OUTPUT);
  digitalWrite(22, LOW);
  pinMode(4, OUTPUT); // Keep the SD card deselected on the shared SPI bus.
  digitalWrite(4, HIGH);
  SPI.begin(7, 5, 6);
  panel.init(WIDTH, HEIGHT, SPI_MODE0);
  panel.setRotation(3); // 180 degrees from the previous orientation (rotation 1).
  panel.setSPISpeed(40000000);
  panel.fillScreen(ST77XX_WHITE);
  digitalWrite(22, HIGH);
  ready = true;
  LOG_INFO("Waveshare C6 LCD ready: 240x240 colour, deep sleep disabled");
  return true;
}

bool showSetup() {
  if (!ready) return false;
  SetupScreen::drawLcd(panel, true);
  return true;
}

void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  memcpy(buffer + y * WIDTH * sizeof(uint16_t), pixels, WIDTH * sizeof(uint16_t));
}

bool present(uint8_t *buffer) {
  if (!ready) return false;
  panel.drawRGBBitmap(0, 0, reinterpret_cast<uint16_t *>(buffer), WIDTH, HEIGHT);
  errorWidth = 0;
  return true;
}

bool showError(const char *message) {
  if (!ready) return false;
  String text(message);
  if (text.length() > 38) text = text.substring(0, 35) + "...";
  int width = text.length() * 6;
  errorWidth = max(errorWidth, width + 8);
  panel.fillRect(WIDTH - errorWidth, HEIGHT - 16, errorWidth, 16, ST77XX_WHITE);
  panel.setTextSize(1);
  panel.setTextWrap(false);
  panel.setTextColor(ST77XX_BLACK);
  panel.setCursor(WIDTH - width - 4, HEIGHT - 12);
  panel.print(text);
  return true;
}

void idle() {}
bool nextPageTapped() { return false; } // This model has no touch controller.
}
#endif

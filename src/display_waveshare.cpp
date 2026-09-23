#ifdef GERTY_WAVESHARE_C6
#include "display.h"
#include "setup_screen.h"
#include "starting_screen.h"
#include "thinking_overlay.h"
#include "logging.h"
#include <Adafruit_ST7789.h>
#include <SPI.h>

namespace Display {
// Waveshare's official ESP32-C6-LCD-1.3 demo pinout.
static Adafruit_ST7789 panel(&SPI, 14, 15, 21);
static bool ready = false;
static int errorWidth = 0;

// Mirror only the badge's rectangle, including text, so the C6 needs no full
// retained framebuffer. All ordinary drawing goes through this canvas.
static GFXcanvas16 corner(ThinkingOverlay::WIDTH, ThinkingOverlay::HEIGHT);
class TrackedCanvas : public Adafruit_GFX {
 public:
  TrackedCanvas() : Adafruit_GFX(Display::WIDTH, Display::HEIGHT) {}
  void drawPixel(int16_t x, int16_t y, uint16_t colour) override {
    panel.drawPixel(x, y, colour);
    corner.drawPixel(x - ThinkingOverlay::left(Display::WIDTH),
                     y - ThinkingOverlay::top(Display::HEIGHT), colour);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t colour) override {
    panel.fillRect(x, y, w, h, colour);
    corner.fillRect(x - ThinkingOverlay::left(Display::WIDTH),
                    y - ThinkingOverlay::top(Display::HEIGHT), w, h, colour);
  }
} canvas;
static bool thinking = false;

bool showThinking() {
  if (!ready || !corner.getBuffer()) return false;
  if (thinking) return true;
  ThinkingOverlay::draw(panel, WIDTH, HEIGHT, true, corner.getBuffer());
  thinking = true;
  return true;
}
bool hideThinking() {
  if (!thinking) return true;
  panel.drawRGBBitmap(ThinkingOverlay::left(WIDTH), ThinkingOverlay::top(HEIGHT),
                      corner.getBuffer(), ThinkingOverlay::WIDTH, ThinkingOverlay::HEIGHT);
  thinking = false;
  return true;
}

bool begin() {
  pinMode(22, OUTPUT);
  digitalWrite(22, LOW);
  pinMode(4, OUTPUT); // Keep the SD card deselected on the shared SPI bus.
  digitalWrite(4, HIGH);
  SPI.begin(7, 5, 6);
  panel.init(WIDTH, HEIGHT, SPI_MODE0);
  panel.setRotation(3); // 180 degrees from the previous orientation (rotation 1).
  panel.setSPISpeed(40000000);
  canvas.fillScreen(ST77XX_WHITE);
  digitalWrite(22, HIGH);
  ready = corner.getBuffer() != nullptr;
  LOG_INFO("Waveshare C6 LCD ready: 240x240 colour, deep sleep disabled");
  return ready;
}

bool showStarting() { return showExpression(Expressions::Face::Happy); }

bool showExpression(Expressions::Face face) {
  if (!ready) return false;
  thinking = false;
  StartingScreen::draw(canvas, WIDTH, HEIGHT, face);
  return true;
}

bool showSetup() {
  if (!ready) return false;
  thinking = false;
  SetupScreen::drawLcd(canvas, true);
  return true;
}

void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  memcpy(buffer + y * WIDTH * sizeof(uint16_t), pixels, WIDTH * sizeof(uint16_t));
}

bool present(uint8_t *buffer) {
  if (!ready) return false;
  thinking = false;
  ThinkingOverlay::copyRgbCorner(reinterpret_cast<uint16_t *>(buffer), WIDTH, HEIGHT,
                                false, corner.getBuffer());
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
  canvas.fillRect(WIDTH - errorWidth, HEIGHT - 16, errorWidth, 16, ST77XX_WHITE);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setTextColor(ST77XX_BLACK);
  canvas.setCursor(WIDTH - width - 4, HEIGHT - 12);
  canvas.print(text);
  return true;
}

void idle() {}
bool nextPageTapped() { return false; } // This model has no touch controller.
}
#endif

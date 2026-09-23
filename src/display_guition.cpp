#ifdef GERTY_GUITION
#include "display.h"
#include "setup_screen.h"
#include "starting_screen.h"
#include "thinking_overlay.h"
#include "logging.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "tap_gate.h"
#include "slide_transition.h"

namespace Display {
// JC3248W535: native portrait AXS15231B; the canvas rotates in software.
static Arduino_ESP32QSPI bus(45, 47, 21, 48, 40, 39);
#ifdef GERTY_JC4827W543
static Arduino_NV3041A panel(&bus, GFX_NOT_DEFINED, 0, true);
static Arduino_Canvas canvas(480, 272, &panel);
constexpr SlideLayout SLIDE_LAYOUT = SlideLayout::Landscape;
#else
static Arduino_AXS15231B panel(&bus, GFX_NOT_DEFINED, 0, false, 320, 480);
static Arduino_Canvas canvas(320, 480, &panel, 0, 0, 1);
constexpr SlideLayout SLIDE_LAYOUT = SlideLayout::RotatedPortrait;
#endif
static bool ready = false;
static bool hasFrame = false;
static int errorWidth = 0;
constexpr int BACKLIGHT = 1;
static bool touchReady = false;
static TapGate touchGate;
#ifdef GERTY_JC4827W543
static uint8_t TOUCH_ADDRESS = 0x5D;
static bool touchDown = false;
#else
constexpr uint8_t TOUCH_ADDRESS = 0x3B;
#endif

static bool thinking = false;
static uint16_t savedCorner[ThinkingOverlay::WIDTH * ThinkingOverlay::HEIGHT];

bool showThinking() {
  if (!ready) return false;
  if (thinking) return true;
  ThinkingOverlay::copyRgbCorner(canvas.getFramebuffer(), WIDTH, HEIGHT,
      SLIDE_LAYOUT == SlideLayout::RotatedPortrait, savedCorner);
  ThinkingOverlay::draw(canvas, WIDTH, HEIGHT, true, savedCorner);
  canvas.flush();
  thinking = true;
  return true;
}

bool hideThinking() {
  if (!thinking) return true;
  canvas.draw16bitRGBBitmap(ThinkingOverlay::left(WIDTH), ThinkingOverlay::top(HEIGHT),
                           savedCorner, ThinkingOverlay::WIDTH, ThinkingOverlay::HEIGHT);
  canvas.flush();
  thinking = false;
  return true;
}

bool begin() {
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, LOW);
  ready = canvas.begin(
#ifdef GERTY_JC4827W543
      32000000
#else
      40000000
#endif
  );
  if (!ready) return false;
  canvas.fillScreen(0xFFFF);
  canvas.flush();
  digitalWrite(BACKLIGHT, HIGH);
  pinMode(3, INPUT_PULLUP); // AXS15231B interrupt; polling also detects release.
#ifdef GERTY_JC4827W543
  // GT911 capacitive variant: reset selects address 0x5D.
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
  delay(10);
  digitalWrite(38, HIGH);
  delay(60);
  pinMode(3, INPUT);
  touchReady = Wire.begin(8, 4, 400000);
#else
  touchReady = Wire.begin(4, 8, 400000);
#endif
  Wire.setTimeOut(20);
  if (touchReady) {
    Wire.beginTransmission(TOUCH_ADDRESS);
    touchReady = Wire.endTransmission() == 0;
  }
  LOG_INFO("Guition touch %s", touchReady ? "ready" : "unavailable");
  LOG_INFO("Guition LCD ready: %dx%d colour, deep sleep disabled", WIDTH, HEIGHT);
  return true;
}

bool showStarting() { return showExpression(Expressions::Face::Happy); }

bool showExpression(Expressions::Face face) {
  if (!ready) return false;
  thinking = false;
  StartingScreen::draw(canvas, WIDTH, HEIGHT, face);
  canvas.flush();
  return true;
}

bool showSetup() {
  if (!ready) return false;
  thinking = false;
  SetupScreen::drawLcd(canvas, false);
  canvas.flush();
  return true;
}

void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  memcpy(buffer + y * WIDTH * sizeof(uint16_t), pixels, WIDTH * sizeof(uint16_t));
}

bool present(uint8_t *buffer) {
  if (!ready) return false;
  hideThinking();
  // The downloaded frame is fully decoded before touching the visible canvas.
  uint16_t *oldFrame = nullptr;
  if (hasFrame && Config::LCD_TRANSITION_MS > 0) {
    oldFrame = static_cast<uint16_t *>(ps_malloc(BUFFER_BYTES));
  }
  if (oldFrame) {
    uint16_t *output = canvas.getFramebuffer();
    memcpy(oldFrame, output, BUFFER_BYTES);
    const uint32_t started = millis();
    while (true) {
      uint32_t elapsed = millis() - started;
      if (elapsed >= Config::LCD_TRANSITION_MS) break;
      float t = float(elapsed) / Config::LCD_TRANSITION_MS;
      float eased = t * t * (3.0f - 2.0f * t);
      composeSlide(output, oldFrame, reinterpret_cast<uint16_t *>(buffer),
                   WIDTH, HEIGHT, int(eased * WIDTH), SLIDE_LAYOUT);
      canvas.flush();
      delay(1); // Yield between synchronous QSPI transfers.
    }
    free(oldFrame);
  }
  // Always finish with the exact new frame, including when allocation fails.
  canvas.draw16bitRGBBitmap(0, 0, reinterpret_cast<uint16_t *>(buffer), WIDTH, HEIGHT);
  canvas.flush();
  hasFrame = true;
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
#ifdef GERTY_JC4827W543
  // GT911 status is latched until acknowledged. Retain the press state
  // between reports so the common debounce gate can settle.
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(0x81);
  Wire.write(0x4E);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDRESS, uint8_t(1)) != 1) return false;
  uint8_t status = Wire.read();
  if (status & 0x80) {
    Wire.beginTransmission(TOUCH_ADDRESS);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    if (Wire.endTransmission() != 0) return false;
    touchDown = (status & 0x0F) > 0 && (status & 0x0F) <= 5;
  }
  return touchGate.update(touchDown, millis());
#else
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
#endif
}
}
#endif

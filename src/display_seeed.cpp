#ifdef GERTY_SEEED_TRMNL
#include "display.h"
#include "config.h"
#include "logging.h"
#include "monochrome.h"
#include "setup_screen.h"
#include "starting_screen.h"
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <gdey/GxEPD2_750_GDEY075T7.h>

namespace Display {
namespace {
// Seeed TRMNL OG DIY kit / XIAO ePaper Display Board. GPIO numbers, not D labels.
constexpr int SCK = 7, MOSI = 9, CS = 44, DC = 10, RST = 38, BUSY = 4;
constexpr gpio_num_t ENABLE = GPIO_NUM_43;
constexpr int ADC_ENABLE = 6;
static_assert(WIDTH == GxEPD2_750_GDEY075T7::WIDTH &&
              HEIGHT == GxEPD2_750_GDEY075T7::HEIGHT,
              "Seeed framebuffer must match the UC8179 panel");
GxEPD2_750_GDEY075T7 panel(CS, DC, RST, BUSY);
uint8_t *retainedFrame = nullptr;
bool frameKnown = false;
bool powered = false;
bool timedOut = false;
uint32_t operationStarted = 0;

// GxEPD2 reports busy timeouts only to Serial. Latch a failure before its
// ten-second timeout so the shared protocol never commits a failed refresh.
void busyCallback(const void *) {
  if (millis() - operationStarted >= 9000) timedOut = true;
  delay(1);
}
void startOperation() { operationStarted = millis(); }
bool operationOk() { return !timedOut && digitalRead(BUSY) == HIGH; }

void disconnectPanel() {
  if (powered) panel.end();
  // Prevent back-power through SPI when the panel's supply is disabled.
  for (int pin : {SCK, MOSI, CS, DC, RST}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  digitalWrite(ENABLE, LOW);
  gpio_hold_en(ENABLE);
  gpio_deep_sleep_hold_en();
  powered = false;
}

void connectPanel() {
  gpio_hold_dis(ENABLE);
  digitalWrite(ENABLE, HIGH);
  delay(10);
  SPI.begin(SCK, -1, MOSI, CS);
  panel.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  panel.init(0, true, 10, false);
  panel.setBusyCallback(busyCallback);
  powered = true;
  timedOut = false;
}

// Adafruit's usual 1-bit canvas uses the opposite polarity. Draw directly into
// our white=1 buffer, with the same colour arguments as the shared setup screen.
class Canvas : public Adafruit_GFX {
 public:
  explicit Canvas(uint8_t *data)
      : Adafruit_GFX(Display::WIDTH, Display::HEIGHT), data_(data) {}
  void drawPixel(int16_t x, int16_t y, uint16_t colour) override {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    uint8_t &value = data_[y * (WIDTH / 8) + x / 8];
    const uint8_t mask = 0x80 >> (x & 7);
    if (colour) value |= mask;
    else value &= ~mask;
  }
 private:
  uint8_t *data_;
};
}

bool begin() {
  digitalWrite(ENABLE, LOW);
  pinMode(ENABLE, OUTPUT);
  gpio_hold_dis(ENABLE);
  gpio_deep_sleep_hold_dis();
  pinMode(ADC_ENABLE, OUTPUT);
  digitalWrite(ADC_ENABLE, LOW);
  disconnectPanel();
  if (!retainedFrame) retainedFrame = static_cast<uint8_t *>(ps_malloc(BUFFER_BYTES));
  return retainedFrame != nullptr;
}

void idle() { disconnectPanel(); }
bool nextPageTapped() { return false; }

void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  Monochrome::writeRow(buffer, WIDTH, y, pixels, Config::DITHER);
}

bool present(uint8_t *buffer) {
  if (!buffer || !retainedFrame) return false;
  connectPanel();
  startOperation();
  panel.writeImageForFullRefresh(buffer, 0, 0, WIDTH, HEIGHT);
  bool ok = operationOk();
  if (ok) {
    startOperation();
    panel.refresh(false); // Full refresh avoids accumulated ghosting.
    ok = operationOk();
  }
  if (ok) {
    startOperation();
    panel.powerOff();
    ok = operationOk();
    // BUSY is meaningful while awake, not after the deep-sleep command.
    if (ok) panel.hibernate();
  }
  disconnectPanel();
  if (ok) {
    if (buffer != retainedFrame) memcpy(retainedFrame, buffer, BUFFER_BYTES);
    frameKnown = true;
  } else {
    frameKnown = false;
    LOG_ERROR("Seeed panel BUSY timeout; refresh not committed");
  }
  return ok;
}

bool showSetup() {
  if (!retainedFrame) return false;
  Canvas canvas(retainedFrame);
  SetupScreen::drawLcd(canvas, false);
  return present(retainedFrame);
}

bool showStarting() { return showExpression(Expressions::Face::Happy); }

bool showExpression(Expressions::Face face) {
  if (!retainedFrame) return false;
  Canvas canvas(retainedFrame);
  StartingScreen::draw(canvas, WIDTH, HEIGHT, face);
  return present(retainedFrame);
}

bool showError(const char *message) {
  // PSRAM is lost in deep sleep. Without the previous pixels, a full refresh
  // would destroy the retained image and a differential refresh is unsafe.
  // Shared code still logs the error over USB and retries the pending page.
  if (!frameKnown) return false;
  Canvas canvas(retainedFrame);
  canvas.fillRect(0, HEIGHT - 36, WIDTH, 36, 0xFFFF);
  canvas.setTextWrap(false);
  canvas.setTextColor(0);
  canvas.setTextSize(2);
  canvas.setCursor(12, HEIGHT - 26);
  canvas.print(message);
  return present(retainedFrame);
}
}
#endif

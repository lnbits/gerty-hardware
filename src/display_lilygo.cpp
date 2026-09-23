#if !defined(GERTY_GUITION) && !defined(GERTY_WAVESHARE_C6) && !defined(GERTY_SEEED_TRMNL)
#include "display.h"
#include "setup_screen.h"
#include "starting_screen.h"
#include "thinking_overlay.h"
#include "config.h"
#include "logging.h"
#include "epd_driver.h"
#include "i2s_data_bus.h"
#include "firasans.h"
namespace Display {
RTC_DATA_ATTR int32_t errorWidth = 0;
RTC_DATA_ATTR int32_t errorHeight = 0;
RTC_DATA_ATTR bool cornerKnown = false;
RTC_DATA_ATTR uint8_t savedCorner[ThinkingOverlay::WIDTH * ThinkingOverlay::HEIGHT / 2];
static bool thinking = false;
static Rect_t badgeArea() {
  return {ThinkingOverlay::left(WIDTH), ThinkingOverlay::top(HEIGHT),
          ThinkingOverlay::WIDTH, ThinkingOverlay::HEIGHT};
}
static void saveCorner(const uint8_t *buffer) {
  for (int y = 0; y < ThinkingOverlay::HEIGHT; ++y)
    memcpy(savedCorner + y * (ThinkingOverlay::WIDTH / 2),
           buffer + (ThinkingOverlay::top(HEIGHT) + y) * (WIDTH / 2) + ThinkingOverlay::left(WIDTH) / 2,
           ThinkingOverlay::WIDTH / 2);
  cornerKnown = true;
}
static bool finishDisplay() {
  // The draw call joins its rendering tasks. Also drain the last bus transfer
  // before removing panel power; a timeout must not count as a displayed page.
  const uint32_t started = millis();
  while (i2s_is_busy() && millis() - started < 1000) delay(1);
  const bool idle = !i2s_is_busy();
  LOG_INFO("Display output %s; sequencing power off", idle ? "idle" : "timed out");
  // Unlike epd_poweroff_all(), this disables the positive and negative rails
  // in sequence with the driver's delays and leaves power_disable asserted.
  epd_poweroff();
  LOG_INFO("Display power-off complete");
  if (!idle) return false;
  return true;
}

bool showThinking() {
  if (thinking) return true;
  if (!cornerKnown) return false;
  uint8_t badge[ThinkingOverlay::WIDTH * ThinkingOverlay::HEIGHT / 2];
  memcpy(badge, savedCorner, sizeof(badge));
  for (int i = 0; i < ThinkingOverlay::WIDTH * ThinkingOverlay::HEIGHT; ++i) {
    if (THINKING_BADGE_ALPHA[i] < 128) continue;
    const uint8_t level = THINKING_BADGE[i] >= 128 ? 0x0F : 0;
    // Preserve underlying pixels outside the rounded face frame.
    if (i & 1) badge[i / 2] = (badge[i / 2] & 0x0F) | (level << 4);
    else badge[i / 2] = (badge[i / 2] & 0xF0) | level;
  }
  epd_poweron();
  epd_clear_area(badgeArea());
  epd_draw_grayscale_image(badgeArea(), badge);
  thinking = true;
  return finishDisplay();
}

bool hideThinking() {
  if (!thinking) return true;
  epd_poweron();
  epd_clear_area(badgeArea());
  epd_draw_grayscale_image(badgeArea(), savedCorner);
  const bool ok = finishDisplay();
  if (ok) thinking = false;
  return ok;
}

static void displayStage(const char *message) {
  LOG_INFO("%s", message);
  if (Config::LOG_LEVEL != Config::LogLevel::NONE) Serial.flush();
}

bool begin() { epd_init(); epd_poweroff(); return true; }
void idle() { epd_poweroff(); }
bool nextPageTapped() { return false; }
void writeRow(uint8_t *buffer, int y, const uint16_t *pixels) {
  static const int bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int x = 0; x < WIDTH; ++x) {
    uint16_t p = pixels[x];
    int r = ((p >> 11) & 31) * 255 / 31;
    int g = ((p >> 5) & 63) * 255 / 63;
    int b = (p & 31) * 255 / 31;
    int gray = (77 * r + 150 * g + 29 * b + 128) >> 8;
    if (Config::DITHER && gray > 0 && gray < 255)
      gray += bayer[y & 3][x & 3] - 8;
    int level = constrain((gray + 8) / 17, 0, 15);
    epd_draw_pixel(x, y, level * 17, buffer);
  }
}
bool present(uint8_t *buffer) {
  displayStage("Display power-on starting");
  epd_poweron();
  displayStage("Display clear starting");
  epd_clear();
  displayStage("Display clear complete; grayscale draw starting");
  epd_draw_grayscale_image(epd_full_screen(), buffer);
  displayStage("Display grayscale draw returned");
  bool ok = finishDisplay();
  if (ok) {
    errorWidth = errorHeight = 0;
    saveCorner(buffer);
    thinking = false;
  }
  return ok;
}
bool showStarting() { return showExpression(Expressions::Face::Happy); }

bool showExpression(Expressions::Face face) {
  uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(BUFFER_BYTES));
  if (!buffer) return false;
  struct Canvas {
    uint8_t *data;
    void fillScreen(uint16_t) { memset(data, 0xFF, BUFFER_BYTES); }
    void fillRect(int x, int y, int w, int h, uint16_t) {
      epd_fill_rect(x, y, w, h, 0, data);
    }
  } canvas{buffer};
  StartingScreen::draw(canvas, WIDTH, HEIGHT, face);
  bool ok = present(buffer);
  free(buffer);
  return ok;
}

bool showSetup() {
  uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(BUFFER_BYTES));
  if (!buffer) return false;
  memset(buffer, 0xFF, BUFFER_BYTES);
  int32_t x = 40, y = 60;
  writeln(&FiraSans, SetupScreen::TITLE, &x, &y, buffer);
  x = 40; y = 112;
  writeln(&FiraSans, SetupScreen::SUBTITLE, &x, &y, buffer);
  int32_t row = 180;
  for (const char *line : SetupScreen::LINES) {
    x = 40; y = row;
    writeln(&FiraSans, line, &x, &y, buffer);
    row += 48;
  }
  bool ok = present(buffer);
  free(buffer);
  return ok;
}

bool showError(const char *message) {
  int32_t x = 0, y = 0, left, top, width, height;
  get_text_bounds(&FiraSans, message, &x, &y,
                  &left, &top, &width, &height, nullptr);
  int32_t panelWidth = ((width + 24 + 3) / 4) * 4;
  int32_t panelHeight = height + 24;
  // Include the old message's area when replacing it with a shorter one.
  errorWidth = min(int32_t(EPD_WIDTH), max(errorWidth, panelWidth));
  errorHeight = min(int32_t(EPD_HEIGHT), max(errorHeight, panelHeight));
  Rect_t area = {EPD_WIDTH - errorWidth, EPD_HEIGHT - errorHeight,
                 errorWidth, errorHeight};
  x = EPD_WIDTH - 12 - width - left;
  // The driver's direct-text path places its bitmap at y - height - top.
  y = EPD_HEIGHT - 12 + top;
  // Render once into a framebuffer so the retained badge corner includes the
  // exact error pixels too, and future badge removal restores the error label.
  uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(BUFFER_BYTES));
  uint8_t *patch = static_cast<uint8_t *>(ps_malloc(area.width * area.height / 2));
  if (!buffer || !patch) { free(buffer); free(patch); return false; }
  memset(buffer, 0xFF, BUFFER_BYTES);
  if (cornerKnown) {
    for (int row = 0; row < ThinkingOverlay::HEIGHT; ++row)
      memcpy(buffer + (ThinkingOverlay::top(HEIGHT) + row) * (WIDTH / 2) + ThinkingOverlay::left(WIDTH) / 2,
             savedCorner + row * (ThinkingOverlay::WIDTH / 2), ThinkingOverlay::WIDTH / 2);
  }
  epd_fill_rect(area.x, area.y, area.width, area.height, 255, buffer);
  // Framebuffer text uses the normal baseline rather than direct-text placement.
  y = EPD_HEIGHT - 12 - height - top;
  writeln(&FiraSans, message, &x, &y, buffer);
  for (int row = 0; row < area.height; ++row)
    memcpy(patch + row * (area.width / 2),
           buffer + (area.y + row) * (WIDTH / 2) + area.x / 2, area.width / 2);
  epd_poweron();
  epd_clear_area(area);
  epd_draw_grayscale_image(area, patch);
  const bool ok = finishDisplay();
  if (ok && cornerKnown) saveCorner(buffer);
  if (!ok) cornerKnown = false;
  free(patch);
  free(buffer);
  return ok;
}


} // namespace Display
#endif

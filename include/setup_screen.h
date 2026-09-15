#pragma once
#include <stdint.h>
#include <stdio.h>

namespace SetupScreen {
constexpr const char *TITLE = "LNbits Gerty";
constexpr const char *SUBTITLE = "Setup required";
constexpr const char *LINES[] = {
    "1. Connect USB to your computer.",
    "2. Open Chrome or Edge:",
    "   lnbits.github.io/",
    "   gerty-hardware/",
    "3. Click Connect to configure.",
    "4. Enter Wi-Fi and Gerty API URL.",
    "5. Save settings."};

constexpr uint32_t WINDOW_MS = 60000;
inline uint32_t remainingSeconds(uint32_t elapsedMs) {
  return elapsedMs >= WINDOW_MS ? 0 : (WINDOW_MS - elapsedMs + 999) / 1000;
}
inline void countdownText(char *buffer, size_t size, uint32_t seconds) {
  snprintf(buffer, size, "Starting in %lus", static_cast<unsigned long>(seconds));
}

template <typename Canvas>
void drawCountdown(Canvas &canvas, bool compact, uint32_t seconds) {
  const int margin = compact ? 12 : 20;
  const int y = compact ? 40 : 48;
  canvas.fillRect(margin, y, compact ? 216 : 440, compact ? 8 : 16, 0xFFFF);
  canvas.setTextWrap(false);
  canvas.setTextColor(0x0000);
  canvas.setTextSize(compact ? 1 : 2);
  canvas.setCursor(margin, y);
  char text[32];
  countdownText(text, sizeof(text), seconds);
  canvas.print(text);
}

// Both LCD libraries expose the same basic text drawing API.
template <typename Canvas>
void drawLcd(Canvas &canvas, bool compact, bool configured = false) {
  const int margin = compact ? 12 : 20;
  canvas.fillScreen(0xFFFF);
  canvas.setTextWrap(false);
  canvas.setTextColor(0x0000);
  canvas.setTextSize(compact ? 2 : 3);
  canvas.setCursor(margin, 12);
  canvas.print(TITLE);
  canvas.setTextSize(compact ? 1 : 2);
  canvas.setCursor(margin, compact ? 40 : 48);
  canvas.print(configured ? "Setup available" : SUBTITLE);
  int y = compact ? 66 : 86;
  for (const char *line : LINES) {
    canvas.setCursor(margin, y);
    canvas.print(line);
    y += compact ? 22 : 24;
  }
}
}

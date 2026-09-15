#pragma once

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

// Both LCD libraries expose the same basic text drawing API.
template <typename Canvas>
void drawLcd(Canvas &canvas, bool compact) {
  const int margin = compact ? 12 : 20;
  canvas.fillScreen(0xFFFF);
  canvas.setTextWrap(false);
  canvas.setTextColor(0x0000);
  canvas.setTextSize(compact ? 2 : 3);
  canvas.setCursor(margin, 12);
  canvas.print(TITLE);
  canvas.setTextSize(compact ? 1 : 2);
  canvas.setCursor(margin, compact ? 40 : 48);
  canvas.print(SUBTITLE);
  int y = compact ? 66 : 86;
  for (const char *line : LINES) {
    canvas.setCursor(margin, y);
    canvas.print(line);
    y += compact ? 22 : 24;
  }
}
}

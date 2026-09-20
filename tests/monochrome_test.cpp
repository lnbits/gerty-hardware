#include "monochrome.h"
#include <array>
#include <cassert>
#include <algorithm>

int main() {
  // Guard bytes around a complete panel catch wrong strides and row overflow.
  std::array<uint8_t, 48002> frame;
  std::array<uint16_t, 800> row;
  frame.fill(0xA5);
  row.fill(0xFFFF);
  for (int y = 0; y < 480; ++y)
    Monochrome::writeRow(frame.data() + 1, 800, y, row.data(), true);
  assert(frame.front() == 0xA5 && frame.back() == 0xA5);
  assert(std::all_of(frame.begin() + 1, frame.end() - 1,
                     [](uint8_t b) { return b == 0xFF; }));
  row.fill(0);
  Monochrome::writeRow(frame.data() + 1, 800, 479, row.data(), true);
  assert(frame[47900] == 0xFF && frame[47901] == 0 && frame[48000] == 0);
  assert(frame.back() == 0xA5);

  uint16_t bits[] = {0xFFFF, 0, 0, 0, 0, 0, 0, 0xFFFF};
  uint8_t packed = 0;
  Monochrome::writeRow(&packed, 8, 0, bits, false);
  assert(packed == 0x81); // Both ends of the byte, white=1, MSB first.

  uint16_t colours[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0, 0, 0, 0};
  Monochrome::writeRow(&packed, 8, 0, colours, false);
  assert(packed == 0x50); // Luminance, not an RGB565 numeric threshold.

  uint8_t ramp[4] = {};
  uint16_t gray[] = {0x8410, 0x8410, 0x8410, 0x8410};
  int white = 0;
  for (int y = 0; y < 4; ++y) {
    Monochrome::writeRow(ramp, 4, y, gray, true);
    for (int x = 0; x < 4; ++x) white += bool(ramp[y] & (0x80 >> x));
  }
  assert(white == 8); // Mid-gray covers half of a Bayer tile.
}

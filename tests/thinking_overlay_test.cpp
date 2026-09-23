#include <cassert>
#include <vector>
#include "thinking_overlay.h"
struct Canvas {
  int width, height;
  std::vector<uint16_t> pixels;
  Canvas(int w, int h) : width(w), height(h), pixels(w*h, 0x1234) {}
  void fillRect(int x, int y, int w, int h, uint16_t colour) {
    assert(x >= 0 && y >= 0 && x+w <= width && y+h <= height);
    for (int row=y; row<y+h; ++row)
      for (int col=x; col<x+w; ++col) pixels[row*width+col] = colour;
  }
};
int main() {
  int interiorWhite = 0;
  for (int i=0; i<64*48; ++i)
    if (THINKING_BADGE_ALPHA[i]==255 && THINKING_BADGE[i]==255) ++interiorWhite;
  assert(interiorWhite > 500); // Transparency must not leak through the frame.

  const int sizes[][2] = {{240,240}, {480,272}, {480,320}, {800,480}, {960,540}};
  for (const auto &size : sizes) {
    for (bool rotated : {false, true}) {
      std::vector<uint16_t> frame(size[0]*size[1]);
      for (int y=0; y<size[1]; ++y) for (int x=0; x<size[0]; ++x)
        frame[rotated ? x*size[1]+size[1]-1-y : y*size[0]+x] = uint16_t(x*31+y);
      uint16_t saved[ThinkingOverlay::WIDTH*ThinkingOverlay::HEIGHT];
      ThinkingOverlay::copyRgbCorner(frame.data(), size[0], size[1], rotated, saved);
      for (int y=0; y<ThinkingOverlay::HEIGHT; ++y)
        for (int x=0; x<ThinkingOverlay::WIDTH; ++x)
          assert(saved[y*ThinkingOverlay::WIDTH+x] ==
                 uint16_t((ThinkingOverlay::left(size[0])+x)*31+ThinkingOverlay::top(size[1])+y));
    }
    for (bool smooth : {false, true}) {
      Canvas canvas(size[0], size[1]);
      std::vector<uint16_t> background(64*48, 0x1234);
      ThinkingOverlay::draw(canvas, size[0], size[1], smooth, background.data());
      int gray=0, black=0;
      for (int y=0; y<size[1]; ++y) for (int x=0; x<size[0]; ++x) {
        auto pixel=canvas.pixels[y*size[0]+x];
        if (x < ThinkingOverlay::left(size[0]) || x >= size[0]-8 ||
            y < ThinkingOverlay::top(size[1]) || y >= size[1]-8) {
          assert(pixel==0x1234); // Preserve every pixel outside the badge.
        } else {
          const int index=(y-ThinkingOverlay::top(size[1]))*64+x-ThinkingOverlay::left(size[0]);
          if (THINKING_BADGE_ALPHA[index]==0 || (!smooth && THINKING_BADGE_ALPHA[index]<128)) {
            assert(pixel==0x1234); // No white rectangle around the face.
            continue;
          }
          if (pixel==0) ++black;
          else if (pixel!=0xFFFF) ++gray;
        }
      }
      assert(black>50);
      assert(smooth ? gray>50 : gray==0);
    }
  }
}

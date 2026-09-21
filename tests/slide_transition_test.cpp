#include "slide_transition.h"
#include <cassert>
#include <vector>
int main() {
  // Landscape old: [1,2,3] / [4,5,6], stored with rotation 1.
  uint16_t old[] = {4,1,5,2,6,3};
  uint16_t next[] = {7,8,9,10,11,12};
  uint16_t out[6];
  composeSlide(out, old, next, 3, 2, 0);
  for (int i=0; i<6; ++i) assert(out[i] == old[i]);
  composeSlide(out, old, next, 3, 2, 1);
  uint16_t middle[] = {5,2,6,3,10,7};
  for (int i=0; i<6; ++i) assert(out[i] == middle[i]);
  composeSlide(out, old, next, 3, 2, 3);
  uint16_t final[] = {10,7,11,8,12,9};
  for (int i=0; i<6; ++i) assert(out[i] == final[i]);

  // Native landscape must slide columns left without mixing rows.
  uint16_t landscape[] = {1,2,3,4,5,6};
  composeSlide(out, landscape, next, 3, 2, 0, SlideLayout::Landscape);
  for (int i=0; i<6; ++i) assert(out[i] == landscape[i]);
  composeSlide(out, landscape, next, 3, 2, 1, SlideLayout::Landscape);
  uint16_t horizontal[] = {2,3,7,5,6,10};
  for (int i=0; i<6; ++i) assert(out[i] == horizontal[i]);
  composeSlide(out, landscape, next, 3, 2, 3, SlideLayout::Landscape);
  for (int i=0; i<6; ++i) assert(out[i] == next[i]);

  // Final Guition frames must preserve every row and stay inside the buffer.
  for (auto layout : {SlideLayout::RotatedPortrait, SlideLayout::Landscape}) {
    const int width = 480;
    const int height = layout == SlideLayout::RotatedPortrait ? 320 : 272;
    const int size = width * height;
    std::vector<uint16_t> image(size);
    std::vector<uint16_t> guarded(size + 2, 0xFFFF);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        image[y * width + x] = static_cast<uint16_t>(y * 97 + x);
    composeSlide(guarded.data() + 1, nullptr, image.data(),
                 width, height, width, layout);
    assert(guarded.front() == 0xFFFF);
    assert(guarded.back() == 0xFFFF);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const int index = layout == SlideLayout::RotatedPortrait
            ? x * height + height - 1 - y : y * width + x;
        assert(guarded[index + 1] == image[y * width + x]);
      }
  }
}

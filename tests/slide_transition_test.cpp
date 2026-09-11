#include "slide_transition.h"
#include <cassert>
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
}

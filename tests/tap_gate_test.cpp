#include "tap_gate.h"
#include <cassert>
int main() {
  TapGate gate;
  assert(!gate.update(false, 0));
  assert(!gate.update(true, 100));
  assert(!gate.update(false, 120)); // short noise pulse
  assert(!gate.update(true, 200));
  assert(gate.update(true, 240));
  assert(!gate.update(true, 20000)); // long hold: no repeat
  assert(!gate.update(false, 20100));
  assert(!gate.update(true, 20120)); // release bounce
  assert(!gate.update(true, 20170));
  assert(!gate.update(false, 20200));
  assert(!gate.update(false, 20280));
  assert(!gate.update(true, 20300));
  assert(gate.update(true, 20340));
  TapGate wrap;
  assert(!wrap.update(true, UINT32_MAX - 20));
  assert(wrap.update(true, 20));
}

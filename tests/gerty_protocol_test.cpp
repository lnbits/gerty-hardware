#include "gerty_protocol.h"
#include <cassert>

int main() {
  const std::string base = "http://192.168.8.104:5001/gerty/api/v1/gerty/pages/device";
  assert(Gerty::pageUrl(base + "/", 0) == base);
  assert(Gerty::pageUrl(base, 1) == base + "/1");
  assert(Gerty::validPages(0, 8, 1));
  assert(Gerty::validPages(7, 8, 0));
  assert(Gerty::validPages(0, 1, 0));
  assert(!Gerty::validPages(8, 8, 0));
  assert(!Gerty::validPages(0, 0, 0));
  assert(!Gerty::validPages(0, 8, 8));
  assert(Gerty::pageAfterUnavailable(0, 8) == 1);
  assert(Gerty::pageAfterUnavailable(7, 8) == 0);
  assert(Gerty::pageAfterUnavailable(0, 1) == 0);
  assert(Gerty::pageAfterUnavailable(0, 0) == 1);
  assert(Gerty::pageAfterUnavailable(1, 0) == 2);
  assert(Gerty::pageAfterUnavailable(UINT32_MAX, 0) == 0);
  assert(Gerty::isWebUrl(base));
  assert(Gerty::isWebUrl("https://example/image.png"));
  assert(!Gerty::isWebUrl("file:///image.png"));
}

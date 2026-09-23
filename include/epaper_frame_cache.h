#ifndef GERTY_EPAPER_FRAME_CACHE_H
#define GERTY_EPAPER_FRAME_CACHE_H
#include <LittleFS.h>

// Seeed powers its controller and PSRAM off during deep sleep. Cache the clean
// frame so full refreshes of a temporary badge never erase the retained page.
namespace EpaperFrameCache {
inline uint32_t checksum(const uint8_t *data, size_t size) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 16777619u;
  return value ? value : 1;
}
inline bool mount() {
  // This display's filesystem is reserved for the recoverable frame cache.
  return LittleFS.begin(true);
}
inline bool load(uint8_t *data, size_t size, uint32_t expected) {
  if (!expected || !mount()) return false;
  File file = LittleFS.open("/display-frame.bin", "r");
  return file && file.size() == size && file.read(data, size) == size &&
         checksum(data, size) == expected;
}
inline uint32_t save(const uint8_t *data, size_t size) {
  if (!mount()) return 0;
  File file = LittleFS.open("/display-frame.tmp", "w");
  if (!file) return 0;
  const bool written = file.write(data, size) == size;
  file.close();
  if (!written || !LittleFS.rename("/display-frame.tmp", "/display-frame.bin")) return 0;
  return checksum(data, size);
}
}
#endif

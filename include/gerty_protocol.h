#pragma once
#include <cstdint>
#include <cctype>
#include <string>

namespace Gerty {
inline std::string mediaType(std::string value) {
  value = value.substr(0, value.find(';'));
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  value = value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
  for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

inline bool isWebUrl(const std::string &url) {
  return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

inline std::string pageUrl(std::string base, uint32_t page) {
  while (!base.empty() && base.back() == '/') base.pop_back();
  return page == 0 ? base : base + "/" + std::to_string(page);
}

inline bool validPages(uint32_t page, uint32_t count, uint32_t next) {
  return count > 0 && page < count && next < count;
}

inline uint32_t pageAfterUnavailable(uint32_t page, uint32_t count) {
  // Until a manifest supplies the count, probe the following page. A 404
  // sends the firmware back to the base endpoint.
  if (count > 0) return page >= count - 1 ? 0 : page + 1;
  return page == UINT32_MAX ? 0 : page + 1;
}
}

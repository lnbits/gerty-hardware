#pragma once
#include <Arduino.h>

namespace Config {
constexpr char MANIFEST_URL[] = "https://192.168.8.104:8443/manifest.json";
constexpr uint32_t DEFAULT_REFRESH_SECONDS = 300;
constexpr uint32_t MIN_REFRESH_SECONDS = 30;
constexpr uint32_t MAX_REFRESH_SECONDS = 300;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t DOWNLOAD_TIMEOUT_MS = 30000;
constexpr size_t MAX_JSON_BYTES = 4096;
constexpr size_t MAX_PNG_BYTES = 2 * 1024 * 1024;
constexpr bool DITHER = true;
}

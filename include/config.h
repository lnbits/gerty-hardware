#pragma once
#include <Arduino.h>

namespace Config {
enum class LogLevel : uint8_t { NONE, ERROR, INFO, DEBUG };
constexpr LogLevel LOG_LEVEL = LogLevel::DEBUG;
#ifdef GERTY_GUITION
constexpr bool DEEP_SLEEP_ENABLED = false; // LCD must remain powered.
#else
constexpr bool DEEP_SLEEP_ENABLED = true;
#endif
#ifdef GERTY_GUITION
// constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/BGLKRokPPtdExU5StLgUct";
constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/cc6bHchLPUgyhGZP9sojre";
#else
constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/75nJhEiBGk5MMBz2iR8Spk";
#endif
// Use the base /pages/<id> URL, without a page suffix.
constexpr uint32_t DEFAULT_REFRESH_SECONDS = 300;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t DOWNLOAD_TIMEOUT_MS = 30000;
constexpr size_t MAX_JSON_BYTES = 4096;
constexpr size_t MAX_PNG_BYTES = 2 * 1024 * 1024;
constexpr bool DITHER = true;
// Guition only: next page slides in from the right. Set to 0 to disable.
constexpr uint32_t LCD_TRANSITION_MS = 400;
}

#pragma once
#include <Arduino.h>

namespace Config {
enum class LogLevel : uint8_t { NONE, ERROR, INFO, DEBUG };
constexpr LogLevel LOG_LEVEL = LogLevel::DEBUG;
#if defined(GERTY_GUITION) || defined(GERTY_WAVESHARE_C6)
constexpr bool DEEP_SLEEP_ENABLED = false; // LCD must remain powered.
#else
constexpr bool DEEP_SLEEP_ENABLED = true;
#endif
#ifdef GERTY_RELEASE
constexpr char MANIFEST_URL[] = "";
#elif defined(GERTY_SEEED_TRMNL)
// Configure through USB, or set a feed using the epaper_800x480 profile.
constexpr char MANIFEST_URL[] = "";
#elif defined(GERTY_WAVESHARE_C6)
// Set this to a Gerty feed configured for 240 x 240 colour PNGs.
constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/7S8WoPo3vNP2CqDixYvc5D";
#elif defined(GERTY_GUITION)
// constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/BGLKRokPPtdExU5StLgUct";
constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/BGLKRokPPtdExU5StLgUct";
#else
constexpr char MANIFEST_URL[] = "https://sats.pw/gerty/api/v1/gerty/pages/75nJhEiBGk5MMBz2iR8Spk";
#endif
// Use the base /pages/<id> URL, without a page suffix.
constexpr uint32_t DEFAULT_REFRESH_SECONDS = 300;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t DOWNLOAD_TIMEOUT_MS = 30000;
constexpr size_t MAX_JSON_BYTES = 4096;
#ifdef GERTY_WAVESHARE_C6
constexpr size_t MAX_PNG_BYTES = 512 * 1024; // Download staged in flash, not RAM.
#else
constexpr size_t MAX_PNG_BYTES = 2 * 1024 * 1024;
#endif
constexpr bool DITHER = true;
// Guition only: next page slides in from the right. Set to 0 to disable.
constexpr uint32_t LCD_TRANSITION_MS = 400;
}

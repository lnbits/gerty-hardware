#pragma once
#include <ArduinoJson.h>
#include <cstdint>

namespace Gerty {
enum class SleepResponse { Awake, Sleep, Invalid };

inline SleepResponse readSleepResponse(const JsonDocument &doc, uint32_t &seconds) {
  seconds = 0;
  // Older servers omit sleep_mode entirely.
  if (doc["sleep_mode"].isUnbound()) return SleepResponse::Awake;
  if (!doc["sleep_mode"].is<bool>()) return SleepResponse::Invalid;
  if (!doc["sleep_mode"].as<bool>()) return SleepResponse::Awake;
  if (!doc["schema_version"].is<int>() || doc["schema_version"].as<int>() != 1 ||
      !doc["sleep_seconds"].is<uint32_t>() || doc["sleep_seconds"].as<uint32_t>() == 0)
    return SleepResponse::Invalid;
  seconds = doc["sleep_seconds"].as<uint32_t>();
  return SleepResponse::Sleep;
}
}

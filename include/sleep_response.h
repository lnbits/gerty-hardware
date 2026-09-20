#pragma once
#include <ArduinoJson.h>
#include <cstdint>

namespace Gerty {
enum class SleepResponse { Awake, Sleep, Invalid };

inline SleepResponse readSleepResponse(JsonVariantConst doc, uint32_t &seconds) {
  seconds = 0;
  if (!doc.is<JsonObjectConst>()) return SleepResponse::Invalid;
  const JsonObjectConst object = doc.as<JsonObjectConst>();
  // Older manifests omit sleep_mode. Explicit null or non-booleans are invalid.
  if (object["sleep_mode"].isUnbound()) return SleepResponse::Awake;
  if (!object["sleep_mode"].is<bool>()) return SleepResponse::Invalid;
  if (!object["sleep_mode"].as<bool>()) return SleepResponse::Awake;
  if (!object["schema_version"].is<int>() ||
      object["schema_version"].as<int>() != 1 ||
      !object["sleep_seconds"].is<uint32_t>() ||
      object["sleep_seconds"].as<uint32_t>() == 0) return SleepResponse::Invalid;
  seconds = object["sleep_seconds"].as<uint32_t>();
  return SleepResponse::Sleep;
}
}

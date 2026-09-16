#include "sleep_response.h"
#include <cassert>

int main() {
  JsonDocument doc;
  uint32_t seconds = 123;
  assert(!deserializeJson(doc, R"({"schema_version":1,"sleep_mode":true,"sleep_seconds":28800,"wake_at":"2026-09-17T06:00:00+01:00"})"));
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Sleep);
  assert(seconds == 28800);
  // The server duration is authoritative, even without a wake_at timestamp.
  doc.remove("wake_at");
  doc["sleep_seconds"] = UINT32_MAX;
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Sleep);
  assert(uint64_t(seconds) * 1000000ULL == 4294967295000000ULL);
  for (const char *invalid : {"0", "-1", "4294967296", "1.5", "null", "\"28800\""}) {
    JsonDocument value;
    assert(!deserializeJson(value, invalid));
    doc["sleep_seconds"] = value.as<JsonVariant>();
    assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Invalid);
    assert(seconds == 0);
  }
  doc.remove("sleep_seconds");
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Invalid);
  doc["sleep_mode"] = false;
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Awake);
  doc.remove("sleep_mode");
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Awake);
  doc["sleep_mode"] = "true";
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Invalid);
  doc["sleep_mode"] = true;
  doc["sleep_seconds"] = 28800;
  doc["schema_version"] = 2;
  assert(Gerty::readSleepResponse(doc, seconds) == Gerty::SleepResponse::Invalid);
}

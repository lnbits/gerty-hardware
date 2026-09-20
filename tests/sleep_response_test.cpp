#include "sleep_response.h"
#include <cassert>

int main() {
  using Gerty::SleepResponse;
  struct Case { const char *json; SleepResponse result; uint32_t seconds; };
  const Case cases[] = {
      {"{}", SleepResponse::Awake, 0},
      {"{\"sleep_mode\":false}", SleepResponse::Awake, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":3600}", SleepResponse::Sleep, 3600},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":4294967295}", SleepResponse::Sleep, UINT32_MAX},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":0}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":-1}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":1.5}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":4294967296}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true,\"sleep_seconds\":\"60\"}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":1,\"sleep_mode\":true}", SleepResponse::Invalid, 0},
      {"{\"schema_version\":2,\"sleep_mode\":true,\"sleep_seconds\":60}", SleepResponse::Invalid, 0},
      {"{\"sleep_mode\":true,\"sleep_seconds\":60}", SleepResponse::Invalid, 0},
      {"{\"sleep_mode\":1}", SleepResponse::Invalid, 0},
      {"{\"sleep_mode\":null}", SleepResponse::Invalid, 0},
      {"[]", SleepResponse::Invalid, 0},
      {"null", SleepResponse::Invalid, 0},
  };
  for (const auto &test : cases) {
    JsonDocument doc;
    assert(!deserializeJson(doc, test.json));
    uint32_t seconds = 123;
    assert(Gerty::readSleepResponse(doc, seconds) == test.result);
    assert(seconds == test.seconds);
  }
}

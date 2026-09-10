#pragma once
#include "config.h"

// Gate the call as well as formatting so disabled logs do not evaluate arguments.
#define GERTY_LOG(level, label, format, ...) do { \
  if (Config::LOG_LEVEL >= Config::LogLevel::level) { \
    Serial.printf("[%s] " format "\n", label, ##__VA_ARGS__); \
  } \
} while (0)

#define LOG_ERROR(format, ...) GERTY_LOG(ERROR, "ERROR", format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) GERTY_LOG(INFO, "INFO", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) GERTY_LOG(DEBUG, "DEBUG", format, ##__VA_ARGS__)

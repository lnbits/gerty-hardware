#pragma once
#include <ArduinoJson.h>
#include <Preferences.h>

// One atomic NVS value prevents partially saved credentials after power loss.
namespace Provisioning {
String ssid, password, endpoint, line;
bool dropping = false;

bool validSettings(const String &s, const String &p, const String &u) {
  return s.length() > 0 && s.length() <= 32 &&
      (p.length() == 0 || (p.length() >= 8 && p.length() <= 63)) &&
      u.length() <= 1024 && u.indexOf(' ') < 0 &&
      (u.startsWith("https://") || u.startsWith("http://")) &&
      u.substring(u.indexOf("://") + 3).length() > 0;
}

bool loadSavedSettings() {
  Preferences prefs;
  if (!prefs.begin("gerty-config", true)) {
    Serial.println("GERTY_CONFIG_MISSING Cannot open saved settings");
    return false;
  }
  String value = prefs.getString("settings", "");
  prefs.end();
  JsonDocument doc;
  auto error = deserializeJson(doc, value);
  if (error) {
    Serial.printf("GERTY_CONFIG_INVALID Cannot decode saved settings: %s\n", error.c_str());
    return false;
  }
  String s = doc["ssid"] | "";
  String p = doc["password"] | "";
  String u = doc["endpoint"] | "";
  if (!validSettings(s, p, u)) {
    // Report lengths only: never print credentials or the endpoint here.
    Serial.printf("GERTY_CONFIG_INVALID Saved field lengths: ssid=%u password=%u endpoint=%u\n",
                  unsigned(s.length()), unsigned(p.length()), unsigned(u.length()));
    return false;
  }
  ssid = s; password = p; endpoint = u;
  Serial.println("GERTY_CONFIG_LOADED Saved settings loaded");
  return true;
}
void poll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 4096 && !dropping) line += c;
      else { line = ""; dropping = true; }
      continue;
    }
    if (dropping) { dropping = false; line = ""; continue; }
    if (line == "GERTY_HELLO") Serial.println("GERTY_READY");
    else if (line.startsWith("GERTY_CONFIG ")) {
      JsonDocument doc;
      auto error = deserializeJson(doc, line.substring(13));
      String s = doc["ssid"] | "";
      String p = doc["password"] | "";
      String u = doc["endpoint"] | "";
      if (error || !validSettings(s, p, u)) {
        Serial.println("GERTY_ERROR Invalid settings");
      } else {
        String value;
        serializeJson(doc, value);
        Preferences prefs;
        bool saved = prefs.begin("gerty-config", false);
        if (saved) { saved = prefs.putString("settings", value) == value.length(); prefs.end(); }
        // Reopen and decode through the same path used at boot before acknowledging.
        if (saved) saved = loadSavedSettings() && ssid == s && password == p && endpoint == u;
        if (saved) {
          Serial.println("GERTY_SAVED");
          Serial.flush();
          delay(250);
          ESP.restart();
        } else Serial.println("GERTY_ERROR Could not save settings");
      }
    }
    line = "";
  }
}
void begin(const char *defaultSsid, const char *defaultPassword, const char *defaultEndpoint) {
  ssid = defaultSsid; password = defaultPassword; endpoint = defaultEndpoint;
  const bool loaded = loadSavedSettings();
  const bool configured = validSettings(ssid, password, endpoint);
  // A verified save restarts in software: start immediately instead of waiting again.
  // Physical reset/power-on still offers the documented 60-second setup window.
  const bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  const bool savedRestart = loaded && esp_reset_reason() == ESP_RST_SW;
  if (!configured || (!timerWake && !savedRestart)) {
    Serial.println(configured
        ? "GERTY_SETUP_WINDOW Configured; starting in 60 seconds"
        : "GERTY_SETUP_REQUIRED Waiting for Wi-Fi and endpoint configuration");
    Serial.println("GERTY_READY");
    uint32_t started = millis();
    while (!configured || millis() - started < 60000) {
      poll(); delay(10);
    }
  }
  Serial.println("GERTY_STARTING Initializing display and Wi-Fi");
}
}

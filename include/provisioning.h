#pragma once
#include <ArduinoJson.h>
#include <Preferences.h>

// One atomic NVS value prevents partially saved credentials after power loss.
namespace Provisioning {
String ssid, password, endpoint, line;
bool dropping = false;
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
      if (error || s.length() == 0 || s.length() > 32 ||
          (p.length() != 0 && (p.length() < 8 || p.length() > 63)) ||
          u.length() > 1024 || u.indexOf(' ') >= 0 ||
          !(u.startsWith("https://") || u.startsWith("http://")) ||
          u.substring(u.indexOf("://") + 3).length() == 0) {
        Serial.println("GERTY_ERROR Invalid settings");
      } else {
        String value;
        serializeJson(doc, value);
        Preferences prefs;
        bool saved = prefs.begin("gerty-config", false);
        if (saved) { saved = prefs.putString("settings", value) == value.length(); prefs.end(); }
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
  Preferences prefs;
  if (prefs.begin("gerty-config", true)) {
    JsonDocument doc;
    if (!deserializeJson(doc, prefs.getString("settings", ""))) {
      ssid = doc["ssid"].as<String>(); password = doc["password"].as<String>();
      endpoint = doc["endpoint"].as<String>();
    }
    prefs.end();
  }
  // New devices wait indefinitely. Reset configured devices to get a 60s setup window.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("GERTY_READY");
    uint32_t started = millis();
    while (ssid.isEmpty() || endpoint.isEmpty() || millis() - started < 60000) {
      poll(); delay(10);
    }
  }
}
}
